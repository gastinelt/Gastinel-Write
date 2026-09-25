// platform/gl_backend.cpp — presentation via OpenGL (contexte cree par WGL).
//
// Le backend n utilise que le sous-ensemble OpenGL 1.1 garanti par
// opengl32.dll de Windows : pas de shader, pas de VBO, pas de chargeur
// d extensions. C est deliberatement le denominateur commun le plus bas, pour
// que ce chemin reste valide sur une machine ancienne ou une VM, et que le
// repli GDI ne serve vraiment qu aux cas ou OpenGL est totalement absent.
//
// Le framebuffer CPU est envoye dans une texture, dessinee en plein ecran sur
// un quad. Le GPU ne fait donc que de la mise a l echelle 1:1 — c est
// suffisant tant que le rendu reste CPU, et cela garde les deux backends
// pixel-identiques.
#include "platform/backend.h"
#include "platform/log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <GL/gl.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace gw::platform {
namespace {

// GL_BGRA n existe qu a partir d OpenGL 1.2. Sur un contexte 1.1 (rendu
// logiciel Microsoft), il faut convertir vers RGBA a la main.
constexpr GLenum kGlBgraExt = 0x80E1;

// OpenGL 1.1 n accepte que des textures dont les cotes sont des puissances de
// deux. On alloue donc une texture englobante et on n en utilise qu un coin.
int next_power_of_two(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

class GlBackend final : public IPresentBackend {
public:
    ~GlBackend() override {
        if (texture_ != 0 && hglrc_ != nullptr) {
            wglMakeCurrent(hdc_, hglrc_);
            glDeleteTextures(1, &texture_);
        }
        if (hglrc_ != nullptr) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hglrc_);
        }
        if (hdc_ != nullptr && hwnd_ != nullptr) {
            ReleaseDC(hwnd_, hdc_);
        }
    }

    bool init(NativeHandle window) override {
        HWND hwnd = static_cast<HWND>(window);
        hwnd_ = hwnd;
        hdc_ = GetDC(hwnd);
        if (hdc_ == nullptr) {
            log_error("OpenGL : GetDC a echoue");
            return false;
        }

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 0; // aucun test de profondeur : rendu 2D pur
        pfd.iLayerType = PFD_MAIN_PLANE;

        const int format = ChoosePixelFormat(hdc_, &pfd);
        if (format == 0) {
            log_error("OpenGL : aucun format de pixel compatible (ChoosePixelFormat)");
            return false;
        }
        if (!SetPixelFormat(hdc_, format, &pfd)) {
            log_error("OpenGL : SetPixelFormat a echoue");
            return false;
        }

        hglrc_ = wglCreateContext(hdc_);
        if (hglrc_ == nullptr) {
            log_error("OpenGL : wglCreateContext a echoue");
            return false;
        }
        if (!wglMakeCurrent(hdc_, hglrc_)) {
            log_error("OpenGL : wglMakeCurrent a echoue");
            wglDeleteContext(hglrc_);
            hglrc_ = nullptr;
            return false;
        }

        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        log("OpenGL : contexte cree (version=%s, renderer=%s)",
            version ? version : "inconnue", renderer ? renderer : "inconnu");

        // Un contexte 1.1 est typiquement le rasterizer logiciel de Microsoft.
        // Il fonctionne, mais il ne connait pas GL_BGRA.
        supports_bgra_ = version != nullptr && !(version[0] == '1' && version[2] == '1');
        if (!supports_bgra_) {
            log("OpenGL : contexte 1.1, conversion BGRA->RGBA effectuee sur CPU");
        }

        // Textures non puissance de deux : coeur d OpenGL depuis 2.0, extension
        // avant. Sans elles il faut allouer la puissance de deux englobante,
        // ce qui gaspille beaucoup : une fenetre maximisee de 1920x1028 demande
        // une texture 2048x2048, soit 16 Mo pour 7,5 Mo utiles.
        supports_npot_ = version != nullptr && version[0] >= '2';
        if (!supports_npot_) {
            const char* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
            supports_npot_ = extensions != nullptr &&
                             std::strstr(extensions, "GL_ARB_texture_non_power_of_two") != nullptr;
        }
        log("OpenGL : textures non puissance de deux %s",
            supports_npot_ ? "disponibles" : "indisponibles, repli sur la puissance de deux");

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_BLEND);
        glEnable(GL_TEXTURE_2D);
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        // NEAREST : le framebuffer est presente a l echelle 1:1, un filtrage
        // lineaire ne ferait que flouter le texte.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);

        return glGetError() == GL_NO_ERROR;
    }

    void resize(int width, int height) override {
        width_ = width;
        height_ = height;
    }

    void present(const render::Framebuffer& fb) override {
        if (fb.empty() || width_ <= 0 || height_ <= 0) return;
        wglMakeCurrent(hdc_, hglrc_);

        glViewport(0, 0, width_, height_);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // La texture colle exactement au framebuffer quand le pilote accepte
        // les dimensions libres ; sinon on retombe sur la puissance de deux
        // englobante et on n en utilise qu un coin.
        const int needed_w = supports_npot_ ? fb.width : next_power_of_two(fb.width);
        const int needed_h = supports_npot_ ? fb.height : next_power_of_two(fb.height);

        glBindTexture(GL_TEXTURE_2D, texture_);
        if (needed_w > tex_w_ || needed_h > tex_h_) {
            // La texture ne fait que grandir, jamais retrecir. En dimensions
            // libres, coller au pixel pres reallouerait sinon a chaque pixel
            // d un glisser de redimensionnement ; en ne grandissant que quand
            // c est necessaire, on garde le gain memoire sans la reallocation
            // permanente. Le surplus retombe a zero des que la fenetre
            // atteint sa plus grande taille de la session.
            tex_w_ = std::max(tex_w_, needed_w);
            tex_h_ = std::max(tex_h_, needed_h);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex_w_, tex_h_, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, nullptr);
        }

        const void* pixels = fb.pixels.data();
        GLenum format = kGlBgraExt;
        if (!supports_bgra_) {
            convert_.resize(fb.pixels.size());
            for (std::size_t i = 0; i < fb.pixels.size(); ++i) {
                const std::uint32_t p = fb.pixels[i];
                const std::uint32_t r = (p >> 16) & 0xFFu;
                const std::uint32_t g = (p >> 8) & 0xFFu;
                const std::uint32_t b = p & 0xFFu;
                convert_[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
            }
            pixels = convert_.data();
            format = GL_RGBA;
        }
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, fb.width, fb.height, format,
                        GL_UNSIGNED_BYTE, pixels);

        const float u = static_cast<float>(fb.width) / static_cast<float>(tex_w_);
        const float v = static_cast<float>(fb.height) / static_cast<float>(tex_h_);

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // Quad plein ecran en coordonnees normalisees. Le V est inverse : le
        // framebuffer a son origine en haut a gauche, OpenGL en bas a gauche.
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, v); glVertex2f(-1.0f, -1.0f);
        glTexCoord2f(u, v);    glVertex2f(1.0f, -1.0f);
        glTexCoord2f(u, 0.0f); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, 1.0f);
        glEnd();

        SwapBuffers(hdc_);
    }

    BackendKind kind() const override { return BackendKind::OpenGL; }

private:
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC hglrc_ = nullptr;
    GLuint texture_ = 0;
    int tex_w_ = 0, tex_h_ = 0;
    int width_ = 0, height_ = 0;
    bool supports_bgra_ = true;
    bool supports_npot_ = false;
    std::vector<std::uint32_t> convert_;
};

} // namespace

std::unique_ptr<IPresentBackend> create_gl_backend() { return std::make_unique<GlBackend>(); }

} // namespace gw::platform
