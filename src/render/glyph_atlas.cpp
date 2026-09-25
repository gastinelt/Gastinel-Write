#include "render/glyph_atlas.h"

#include <algorithm>

namespace gw::render {
namespace {

// Un pixel de garde entre glyphes : sans lui, un filtrage bilineaire cote
// OpenGL ferait baver le voisin dans le glyphe courant.
constexpr int kPadding = 1;

// Hauteur initiale d un atlas neuf. Une seule etagere de glyphes tient
// largement dedans pour les corps d interface ; au-dela, l atlas grandit.
constexpr int kInitialHeight = 64;

} // namespace

void GlyphAtlas::reset(int width, int max_height, float pixel_size, Synthesis synthesis) {
    max_height_ = max_height;
    image_.resize(width, std::min(kInitialHeight, max_height));
    entries_.clear();
    for (int i = 0; i < 128; ++i) ascii_entries_[i] = nullptr;
    pixel_size_ = pixel_size;
    synthesis_ = synthesis;
    shelf_x_ = 0;
    shelf_y_ = 0;
    shelf_height_ = 0;
    dirty_ = true;
}

bool GlyphAtlas::allocate(int w, int h, int& out_x, int& out_y) {
    if (w <= 0 || h <= 0) return false;
    if (w > image_.width || h > max_height_) return false;

    if (shelf_x_ + w > image_.width) {
        // Etagere pleine : on en ouvre une nouvelle juste en dessous.
        shelf_y_ += shelf_height_ + kPadding;
        shelf_x_ = 0;
        shelf_height_ = 0;
    }

    const int needed = shelf_y_ + h;
    if (needed > image_.height) {
        if (needed > max_height_) return false; // atlas sature pour de bon

        // Doublement plutot que croissance au plus juste : sinon chaque glyphe
        // un peu plus haut que le precedent provoquerait une reallocation.
        int grown = image_.height;
        while (grown < needed) grown *= 2;
        image_.grow_height(std::min(grown, max_height_));
    }

    out_x = shelf_x_;
    out_y = shelf_y_;
    shelf_x_ += w + kPadding;
    shelf_height_ = std::max(shelf_height_, h);
    return true;
}

const AtlasEntry* GlyphAtlas::get_or_insert(const font::TtfFont& font, std::uint32_t codepoint) {
    if (codepoint < 128 && ascii_entries_[codepoint] != nullptr) {
        return ascii_entries_[codepoint];
    }

    const auto existing = entries_.find(codepoint);
    if (existing != entries_.end()) {
        if (codepoint < 128) ascii_entries_[codepoint] = &existing->second;
        return &existing->second;
    }

    const std::uint16_t gid = font.glyph_index_for_codepoint(codepoint);
    if (gid == 0 && codepoint != 0) return nullptr;

    font::Glyph glyph;
    if (!font.load_glyph(gid, glyph)) return nullptr;

    // Italique synthetique : cisaillement des contours AVANT rasterization,
    // donc les bords penches sont rasterises comme tels et non obtenus en
    // decalant des lignes de pixels deja calculees.
    //
    // Le cisaillement part de la ligne de base (y = 0) : le bas des lettres
    // reste en place et le haut part a droite, ce qui est la geometrie d un
    // oblique. Cisailler autour du centre ferait deriver le mot vers la gauche.
    if (synthesis_.slant != 0.0f) {
        for (auto& contour : glyph.contours) {
            for (auto& point : contour) point.x += point.y * synthesis_.slant;
        }
        // La bbox suit, sans quoi le rasterizer couperait ce qui deborde.
        const float shift_max = std::max(0.0f, glyph.y_max * synthesis_.slant);
        const float shift_min = std::min(0.0f, glyph.y_min * synthesis_.slant);
        glyph.x_min += std::min(shift_max, shift_min);
        glyph.x_max += std::max(shift_max, shift_min);
    }

    RasterizedGlyph raster;
    const bool has_pixels = rasterize_glyph(glyph, font.units_per_em(), pixel_size_, raster);

    // Gras synthetique : la couverture est dilatee horizontalement. Chaque
    // pixel prend le maximum de lui-meme et de ses voisins de gauche, ce qui
    // epaissit les fûts verticaux sans toucher aux horizontales — exactement ce
    // que fait un vrai gras, en premiere approximation. L avance suit, sinon
    // les lettres se toucheraient.
    if (synthesis_.embolden > 0) {
        if (has_pixels && !raster.coverage.empty()) {
            const int extra = synthesis_.embolden;
            Coverage8 widened;
            widened.resize(raster.coverage.width + extra, raster.coverage.height);
            for (int row = 0; row < raster.coverage.height; ++row) {
                for (int col = 0; col < widened.width; ++col) {
                    std::uint8_t best = 0;
                    for (int k = 0; k <= extra; ++k) {
                        const int src = col - k;
                        if (src < 0 || src >= raster.coverage.width) continue;
                        best = std::max(best, raster.coverage.at(src, row));
                    }
                    widened.set(col, row, best);
                }
            }
            raster.coverage = std::move(widened);
        }
        raster.advance += synthesis_.embolden;
    }

    AtlasEntry entry;
    entry.advance = raster.advance;

    if (has_pixels && !raster.coverage.empty()) {
        int x = 0, y = 0;
        if (!allocate(raster.coverage.width, raster.coverage.height, x, y)) return nullptr;

        for (int row = 0; row < raster.coverage.height; ++row) {
            for (int col = 0; col < raster.coverage.width; ++col) {
                image_.set(x + col, y + row, raster.coverage.at(col, row));
            }
        }
        entry.x = x;
        entry.y = y;
        entry.w = raster.coverage.width;
        entry.h = raster.coverage.height;
        entry.bearing_x = raster.bearing_x;
        entry.bearing_y = raster.bearing_y;
        dirty_ = true;
    }
    // Glyphe sans pixels (espace) : l entree est conservee pour son advance.

    const auto inserted = entries_.emplace(codepoint, entry);
    const AtlasEntry* result = &inserted.first->second;
    if (codepoint < 128) ascii_entries_[codepoint] = result;
    return result;
}

void blit_glyph(Framebuffer& fb, const Coverage8& atlas, const AtlasEntry& entry, int pen_x,
                int pen_y, std::uint32_t argb) {
    if (entry.w <= 0 || entry.h <= 0 || fb.empty()) return;

    const int dst_x0 = pen_x + entry.bearing_x;
    const int dst_y0 = pen_y - entry.bearing_y;

    const std::uint32_t src_r = (argb >> 16) & 0xFFu;
    const std::uint32_t src_g = (argb >> 8) & 0xFFu;
    const std::uint32_t src_b = argb & 0xFFu;

    for (int row = 0; row < entry.h; ++row) {
        const int dy = dst_y0 + row;
        if (dy < 0 || dy >= fb.height) continue;
        std::uint32_t* dst = fb.row(dy);

        for (int col = 0; col < entry.w; ++col) {
            const int dx = dst_x0 + col;
            if (dx < 0 || dx >= fb.width) continue;

            const std::uint32_t a = atlas.at(entry.x + col, entry.y + row);
            if (a == 0) continue;

            if (a == 255) {
                dst[dx] = 0xFF000000u | (src_r << 16) | (src_g << 8) | src_b;
                continue;
            }
            // Melange source-over. Inutile en v0 (couverture binaire) mais deja
            // en place pour le jour ou l antialiasing arrivera.
            const std::uint32_t d = dst[dx];
            const std::uint32_t inv = 255u - a;
            const std::uint32_t r = (src_r * a + ((d >> 16) & 0xFFu) * inv) / 255u;
            const std::uint32_t g = (src_g * a + ((d >> 8) & 0xFFu) * inv) / 255u;
            const std::uint32_t b = (src_b * a + (d & 0xFFu) * inv) / 255u;
            dst[dx] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}

} // namespace gw::render
