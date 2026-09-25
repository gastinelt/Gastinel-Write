#include "render/text_run.h"

#include <cmath>

namespace gw::render {

std::uint32_t next_codepoint(const std::string& utf8, std::size_t& i) {
    const auto byte = [&](std::size_t k) { return static_cast<unsigned char>(utf8[k]); };
    const unsigned char c = byte(i);

    if (c < 0x80) {
        ++i;
        return c;
    }
    int extra = 0;
    std::uint32_t cp = 0;
    if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07u; }
    else { ++i; return 0xFFFDu; }

    if (i + static_cast<std::size_t>(extra) >= utf8.size()) {
        ++i; // sequence tronquee en fin de chaine
        return 0xFFFDu;
    }
    for (int k = 1; k <= extra; ++k) {
        const unsigned char cc = byte(i + static_cast<std::size_t>(k));
        if ((cc & 0xC0) != 0x80) {
            ++i;
            return 0xFFFDu;
        }
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    i += static_cast<std::size_t>(extra) + 1;
    return cp;
}

int measure_utf8(const std::string& text, const font::TtfFont& font, GlyphAtlas& atlas) {
    int width = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::uint32_t cp = next_codepoint(text, i);
        const AtlasEntry* entry = atlas.get_or_insert(font, cp);
        if (entry != nullptr) width += entry->advance;
    }
    return width;
}

int draw_utf8(Framebuffer& fb, const std::string& text, int pen_x, int pen_y,
              const font::TtfFont& font, GlyphAtlas& atlas, std::uint32_t argb) {
    std::size_t i = 0;
    while (i < text.size()) {
        const std::uint32_t cp = next_codepoint(text, i);
        const AtlasEntry* entry = atlas.get_or_insert(font, cp);
        if (entry == nullptr) continue; // glyphe absent : saute, pas de .notdef
        blit_glyph(fb, atlas.image(), *entry, pen_x, pen_y, argb);
        pen_x += entry->advance;
    }
    return pen_x;
}

int measure_utf8_range(const std::string& text, std::size_t begin, std::size_t end,
                       const font::TtfFont& font, GlyphAtlas& atlas) {
    int width = 0;
    std::size_t i = begin;
    while (i < end && i < text.size()) {
        const std::uint32_t cp = next_codepoint(text, i);
        const AtlasEntry* entry = atlas.get_or_insert(font, cp);
        if (entry != nullptr) width += entry->advance;
    }
    return width;
}

int draw_utf8_range(Framebuffer& fb, const std::string& text, std::size_t begin, std::size_t end,
                    int pen_x, int pen_y, const font::TtfFont& font, GlyphAtlas& atlas,
                    std::uint32_t argb) {
    std::size_t i = begin;
    while (i < end && i < text.size()) {
        const std::uint32_t cp = next_codepoint(text, i);
        const AtlasEntry* entry = atlas.get_or_insert(font, cp);
        if (entry == nullptr) continue;
        blit_glyph(fb, atlas.image(), *entry, pen_x, pen_y, argb);
        pen_x += entry->advance;
    }
    return pen_x;
}

std::size_t offset_at_x(const std::string& text, std::size_t begin, std::size_t end, int x,
                        const font::TtfFont& font, GlyphAtlas& atlas) {
    if (x <= 0) return begin;

    int pen = 0;
    std::size_t i = begin;
    while (i < end && i < text.size()) {
        const std::size_t start = i;
        const std::uint32_t cp = next_codepoint(text, i);
        const AtlasEntry* entry = atlas.get_or_insert(font, cp);
        const int advance = entry != nullptr ? entry->advance : 0;

        // Bascule a mi-caractere : au-dela du milieu, c est le bord droit qui
        // est le plus proche, donc la position visee est celle d apres.
        if (x < pen + advance / 2) return start;
        pen += advance;
    }
    return end < text.size() ? end : text.size();
}

int line_height(const font::TtfFont& font, float pixel_size) {
    const float scale = pixel_size / static_cast<float>(font.units_per_em());
    const float h = (static_cast<float>(font.ascender()) -
                     static_cast<float>(font.descender()) +
                     static_cast<float>(font.line_gap())) * scale;
    return static_cast<int>(std::lround(h));
}

int ascent_pixels(const font::TtfFont& font, float pixel_size) {
    const float scale = pixel_size / static_cast<float>(font.units_per_em());
    return static_cast<int>(std::lround(static_cast<float>(font.ascender()) * scale));
}

} // namespace gw::render
