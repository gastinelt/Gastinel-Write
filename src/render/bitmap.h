// render/bitmap.h — conteneurs image CPU partages par les deux backends.
//
// Tout le rendu (rasterization des glyphes, composition de la page) se fait en
// memoire CPU. Les backends OpenGL et GDI ne font que *presenter* le
// Framebuffer final : c'est ce qui garantit un pixel identique quel que soit
// le chemin de presentation choisi au lancement.
#pragma once

#include <cstdint>
#include <vector>

namespace gw::render {

// Image 8 bits, une valeur de couverture par pixel (0 = vide, 255 = plein).
// Utilisee pour les glyphes rasterises et pour l'atlas.
struct Coverage8 {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // taille = width * height

    void resize(int w, int h) {
        width = w;
        height = h;
        pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    }

    std::uint8_t at(int x, int y) const {
        return pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x];
    }

    void set(int x, int y, std::uint8_t v) {
        pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] = v;
    }

    // Augmente la hauteur en conservant le contenu deja ecrit.
    //
    // Possible sans recopie parce que l image est stockee ligne par ligne et
    // que la largeur ne change pas : les pixels existants occupent deja le
    // prefixe exact du tampon. Les lignes ajoutees sont mises a zero.
    void grow_height(int new_height) {
        if (new_height <= height) return;
        pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(new_height), 0);
        height = new_height;
    }

    bool empty() const { return width <= 0 || height <= 0; }
};

// Image 32 bits au format BGRA, origine en haut a gauche.
//
// Le choix de BGRA n'est pas neutre : c'est la disposition attendue par un
// DIB Win32 (BI_RGB 32bpp), donc le backend GDI peut faire un StretchDIBits
// direct sans conversion. Le backend OpenGL, lui, upload en GL_BGRA (extension
// presente depuis OpenGL 1.2, disponible partout ou un contexte accelere existe).
struct Framebuffer {
    int width = 0;
    int height = 0;
    std::vector<std::uint32_t> pixels; // 0xAARRGGBB en memoire little-endian => B,G,R,A

    void resize(int w, int h) {
        width = w;
        height = h;
        pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0xFFFFFFFFu);
    }

    void clear(std::uint32_t argb) {
        for (auto& p : pixels) p = argb;
    }

    std::uint32_t* row(int y) {
        return pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width);
    }

    bool empty() const { return width <= 0 || height <= 0; }
};

} // namespace gw::render
