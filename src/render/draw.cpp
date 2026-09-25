#include "render/draw.h"

#include <algorithm>

namespace gw::render {
namespace {

// Melange source-over d un pixel unique. Comme pour le texte, le chemin est
// deja ecrit pour des couvertures partielles meme si la v0 ne produit que du
// 0 ou du 255.
inline void blend_pixel(std::uint32_t* dst, std::uint32_t argb, std::uint32_t a) {
    if (a == 0) return;
    if (a == 255) {
        *dst = 0xFF000000u | (argb & 0x00FFFFFFu);
        return;
    }
    const std::uint32_t inv = 255u - a;
    const std::uint32_t d = *dst;
    const std::uint32_t r = (((argb >> 16) & 0xFFu) * a + ((d >> 16) & 0xFFu) * inv) / 255u;
    const std::uint32_t g = (((argb >> 8) & 0xFFu) * a + ((d >> 8) & 0xFFu) * inv) / 255u;
    const std::uint32_t b = ((argb & 0xFFu) * a + (d & 0xFFu) * inv) / 255u;
    *dst = 0xFF000000u | (r << 16) | (g << 8) | b;
}

} // namespace

void fill_rect(Framebuffer& fb, int x, int y, int w, int h, std::uint32_t argb) {
    if (fb.empty() || w <= 0 || h <= 0) return;

    const int x0 = std::max(x, 0);
    const int y0 = std::max(y, 0);
    const int x1 = std::min(x + w, fb.width);
    const int y1 = std::min(y + h, fb.height);
    if (x0 >= x1 || y0 >= y1) return;

    const std::uint32_t value = 0xFF000000u | (argb & 0x00FFFFFFu);
    for (int py = y0; py < y1; ++py) {
        std::uint32_t* row = fb.row(py);
        for (int px = x0; px < x1; ++px) row[px] = value;
    }
}

void stroke_rect(Framebuffer& fb, int x, int y, int w, int h, int thickness,
                 std::uint32_t argb) {
    if (w <= 0 || h <= 0 || thickness <= 0) return;
    const int t = std::min(thickness, std::min(w, h));
    fill_rect(fb, x, y, w, t, argb);                 // haut
    fill_rect(fb, x, y + h - t, w, t, argb);         // bas
    fill_rect(fb, x, y + t, t, h - 2 * t, argb);     // gauche
    fill_rect(fb, x + w - t, y + t, t, h - 2 * t, argb); // droite
}

void blit_image(Framebuffer& fb, const Framebuffer& src, int dst_x, int dst_y, int dst_w,
                int dst_h) {
    if (fb.empty() || src.empty() || dst_w <= 0 || dst_h <= 0) return;

    // Le filtre boite ne sert quen reduction. En agrandissement, chaque pixel
    // destination retombe dans un seul pixel source et la moyenne degenere
    // naturellement au plus proche voisin — le meme code couvre donc les deux
    // cas sans branche supplementaire.
    const bool downscale = src.width > dst_w || src.height > dst_h;

    for (int y = 0; y < dst_h; ++y) {
        const int py = dst_y + y;
        if (py < 0 || py >= fb.height) continue;
        std::uint32_t* dst_row = fb.row(py);

        // Bornes de la zone source couverte par cette ligne destination.
        const int sy0 = y * src.height / dst_h;
        int sy1 = downscale ? ((y + 1) * src.height + dst_h - 1) / dst_h : sy0 + 1;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > src.height) sy1 = src.height;

        for (int x = 0; x < dst_w; ++x) {
            const int px = dst_x + x;
            if (px < 0 || px >= fb.width) continue;

            const int sx0 = x * src.width / dst_w;
            int sx1 = downscale ? ((x + 1) * src.width + dst_w - 1) / dst_w : sx0 + 1;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > src.width) sx1 = src.width;

            // Moyenne ponderee par lalpha : additionner les couleurs sans tenir
            // compte de la transparence ferait remonter la couleur des pixels
            // invisibles et cernerait le logo dun lisere.
            std::uint32_t sum_a = 0, sum_r = 0, sum_g = 0, sum_b = 0;
            std::uint32_t count = 0;

            for (int sy = sy0; sy < sy1; ++sy) {
                const std::uint32_t* src_row = src.pixels.data() +
                                               static_cast<std::size_t>(sy) *
                                                   static_cast<std::size_t>(src.width);
                for (int sx = sx0; sx < sx1; ++sx) {
                    const std::uint32_t s = src_row[sx];
                    const std::uint32_t a = (s >> 24) & 0xFFu;
                    sum_a += a;
                    sum_r += ((s >> 16) & 0xFFu) * a;
                    sum_g += ((s >> 8) & 0xFFu) * a;
                    sum_b += (s & 0xFFu) * a;
                    ++count;
                }
            }
            if (count == 0 || sum_a == 0) continue;

            const std::uint32_t a = sum_a / count;
            const std::uint32_t r = sum_r / sum_a;
            const std::uint32_t g = sum_g / sum_a;
            const std::uint32_t b = sum_b / sum_a;
            blend_pixel(&dst_row[px], (r << 16) | (g << 8) | b, a);
        }
    }
}

void blit_coverage(Framebuffer& fb, const Coverage8& src, int x, int y, std::uint32_t argb) {
    if (fb.empty() || src.empty()) return;

    for (int row = 0; row < src.height; ++row) {
        const int dy = y + row;
        if (dy < 0 || dy >= fb.height) continue;
        std::uint32_t* dst = fb.row(dy);
        for (int col = 0; col < src.width; ++col) {
            const int dx = x + col;
            if (dx < 0 || dx >= fb.width) continue;
            blend_pixel(&dst[dx], argb, src.at(col, row));
        }
    }
}

} // namespace gw::render
