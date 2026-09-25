// platform/gdi_backend.cpp — presentation de repli via GDI.
//
// Utilise quand aucun contexte OpenGL ne peut etre cree. Le framebuffer BGRA
// est exactement la disposition memoire d un DIB 32 bits BI_RGB, donc la
// presentation se reduit a un SetDIBitsToDevice : aucune conversion, aucune
// copie intermediaire.
//
// On passe par un DC memoire plutot que de blitter directement sur le DC
// fenetre, pour que la fenetre soit mise a jour en une seule operation et ne
// scintille pas pendant un redimensionnement.
#include "platform/backend.h"
#include "platform/log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace gw::platform {
namespace {

class GdiBackend final : public IPresentBackend {
public:
    ~GdiBackend() override {
        release_surface();
        if (mem_dc_ != nullptr) DeleteDC(mem_dc_);
        if (hdc_ != nullptr && hwnd_ != nullptr) ReleaseDC(hwnd_, hdc_);
    }

    bool init(NativeHandle window) override {
        HWND hwnd = static_cast<HWND>(window);
        hwnd_ = hwnd;
        hdc_ = GetDC(hwnd);
        if (hdc_ == nullptr) {
            log_error("GDI : GetDC a echoue");
            return false;
        }
        mem_dc_ = CreateCompatibleDC(hdc_);
        if (mem_dc_ == nullptr) {
            log_error("GDI : CreateCompatibleDC a echoue");
            return false;
        }
        return true;
    }

    void resize(int width, int height) override {
        if (width == width_ && height == height_) return;
        width_ = width;
        height_ = height;
        release_surface();
        if (width <= 0 || height <= 0) return;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        // Hauteur negative = DIB top-down, pour coller a l origine haut-gauche
        // du Framebuffer. Sans cela l image serait retournee verticalement.
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        dib_ = CreateDIBSection(hdc_, &bmi, DIB_RGB_COLORS, &dib_pixels_, nullptr, 0);
        if (dib_ == nullptr) {
            log_error("GDI : CreateDIBSection a echoue (%dx%d)", width, height);
            return;
        }
        old_bitmap_ = static_cast<HBITMAP>(SelectObject(mem_dc_, dib_));
    }

    void present(const render::Framebuffer& fb) override {
        if (dib_ == nullptr || dib_pixels_ == nullptr || fb.empty()) return;

        // Le framebuffer et le DIB ont la meme taille dans le cas nominal ;
        // pendant un redimensionnement rapide ils peuvent diverger d une frame,
        // d ou la copie ligne a ligne bornee plutot qu un memcpy global.
        const int rows = fb.height < height_ ? fb.height : height_;
        const int cols = fb.width < width_ ? fb.width : width_;
        auto* dst = static_cast<std::uint32_t*>(dib_pixels_);
        for (int y = 0; y < rows; ++y) {
            const std::uint32_t* src =
                fb.pixels.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(fb.width);
            std::uint32_t* row = dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_);
            for (int x = 0; x < cols; ++x) row[x] = src[x];
        }

        BitBlt(hdc_, 0, 0, width_, height_, mem_dc_, 0, 0, SRCCOPY);
    }

    BackendKind kind() const override { return BackendKind::Gdi; }

private:
    void release_surface() {
        if (dib_ != nullptr) {
            if (old_bitmap_ != nullptr) SelectObject(mem_dc_, old_bitmap_);
            DeleteObject(dib_);
            dib_ = nullptr;
            old_bitmap_ = nullptr;
            dib_pixels_ = nullptr;
        }
    }

    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HDC mem_dc_ = nullptr;
    HBITMAP dib_ = nullptr;
    HBITMAP old_bitmap_ = nullptr;
    void* dib_pixels_ = nullptr;
    int width_ = 0, height_ = 0;
};

} // namespace

std::unique_ptr<IPresentBackend> create_gdi_backend() { return std::make_unique<GdiBackend>(); }

} // namespace gw::platform
