#include "font/ttf.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace gw::font {
namespace {

// --- Lecture big-endian bornee -------------------------------------------
// Toutes les valeurs d'un fichier TrueType sont en big-endian. Chaque
// accesseur verifie les bornes : un fichier tronque ou corrompu doit produire
// un echec propre, jamais une lecture hors buffer.

bool rd_u8(const std::vector<std::uint8_t>& d, std::uint32_t off, std::uint8_t& out) {
    if (off + 1 > d.size()) return false;
    out = d[off];
    return true;
}

bool rd_u16(const std::vector<std::uint8_t>& d, std::uint32_t off, std::uint16_t& out) {
    if (off + 2 > d.size()) return false;
    out = static_cast<std::uint16_t>((d[off] << 8) | d[off + 1]);
    return true;
}

bool rd_i16(const std::vector<std::uint8_t>& d, std::uint32_t off, std::int16_t& out) {
    std::uint16_t v = 0;
    if (!rd_u16(d, off, v)) return false;
    out = static_cast<std::int16_t>(v);
    return true;
}

bool rd_u32(const std::vector<std::uint8_t>& d, std::uint32_t off, std::uint32_t& out) {
    if (off + 4 > d.size()) return false;
    out = (static_cast<std::uint32_t>(d[off]) << 24) |
          (static_cast<std::uint32_t>(d[off + 1]) << 16) |
          (static_cast<std::uint32_t>(d[off + 2]) << 8) |
          static_cast<std::uint32_t>(d[off + 3]);
    return true;
}

// F2Dot14 : entier signe 16 bits interprete comme un fixe 2.14.
// Utilise par les composites pour les facteurs d'echelle.
bool rd_f2dot14(const std::vector<std::uint8_t>& d, std::uint32_t off, float& out) {
    std::int16_t v = 0;
    if (!rd_i16(d, off, v)) return false;
    out = static_cast<float>(v) / 16384.0f;
    return true;
}

// Drapeaux glyf (glyphe simple)
constexpr std::uint8_t kOnCurve      = 0x01;
constexpr std::uint8_t kXShortVector = 0x02;
constexpr std::uint8_t kYShortVector = 0x04;
constexpr std::uint8_t kRepeatFlag   = 0x08;
constexpr std::uint8_t kXSameOrPos   = 0x10;
constexpr std::uint8_t kYSameOrPos   = 0x20;

// Drapeaux glyf (glyphe composite)
constexpr std::uint16_t kArg1And2AreWords = 0x0001;
constexpr std::uint16_t kArgsAreXyValues  = 0x0002;
constexpr std::uint16_t kWeHaveAScale     = 0x0008;
constexpr std::uint16_t kMoreComponents   = 0x0020;
constexpr std::uint16_t kWeHaveXAndYScale = 0x0040;
constexpr std::uint16_t kWeHaveATwoByTwo  = 0x0080;

constexpr int kMaxCompositeDepth = 5;

} // namespace

// -------------------------------------------------------------------------

bool TtfFont::load_from_file(const std::string& path, std::string& error) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "impossible d ouvrir " + path;
        return false;
    }
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 12) {
        std::fclose(f);
        error = "fichier trop court pour etre un TTF : " + path;
        return false;
    }
    data_.resize(static_cast<std::size_t>(size));
    const std::size_t read = std::fread(data_.data(), 1, data_.size(), f);
    std::fclose(f);
    if (read != data_.size()) {
        error = "lecture incomplete de " + path;
        return false;
    }

    // En-tete sfnt. On accepte 0x00010000 (TrueType) et le tag "true".
    // "OTTO" (contours CFF) est refuse explicitement : le parser ne sait lire
    // que des contours glyf.
    std::uint32_t sfnt = 0;
    if (!rd_u32(data_, 0, sfnt)) {
        error = "en-tete illisible";
        return false;
    }
    if (sfnt == 0x4F54544Fu) {
        error = "police OpenType/CFF (OTTO) non supportee : il faut un TTF a contours glyf";
        return false;
    }
    // "ttcf" : collection de plusieurs polices dans un seul fichier. Le
    // choisir demanderait une interface de selection de face ; on refuse avec
    // un message qui dit quoi faire plutot qu un "signature inconnue" opaque.
    if (sfnt == 0x74746366u) {
        error = "collection TrueType (.ttc) : extrayez la police voulue en .ttf";
        return false;
    }
    // "wOFF"/"wOF2" : polices web compressees, format d enveloppe different.
    if (sfnt == 0x774F4646u || sfnt == 0x774F4632u) {
        error = "police web (WOFF/WOFF2) non supportee : il faut un .ttf";
        return false;
    }
    if (sfnt != 0x00010000u && sfnt != 0x74727565u) {
        error = "signature sfnt inconnue : ce fichier n est pas un TTF";
        return false;
    }

    std::uint16_t num_tables = 0;
    if (!rd_u16(data_, 4, num_tables)) {
        error = "numTables illisible";
        return false;
    }

    tables_.clear();
    for (std::uint16_t i = 0; i < num_tables; ++i) {
        const std::uint32_t rec = 12u + 16u * i;
        if (rec + 16 > data_.size()) {
            error = "repertoire de tables tronque";
            return false;
        }
        std::string tag(reinterpret_cast<const char*>(&data_[rec]), 4);
        TableRecord tr;
        rd_u32(data_, rec + 8, tr.offset);
        rd_u32(data_, rec + 12, tr.length);
        if (tr.offset > data_.size() || tr.offset + tr.length > data_.size()) {
            // Table hors fichier : on l ignore plutot que d echouer, certaines
            // polices ont des longueurs padees legerement fausses.
            continue;
        }
        tables_.emplace_back(std::move(tag), tr);
    }

    const TableRecord* glyf = find_table("glyf");
    if (!glyf) {
        error = "table glyf absente";
        return false;
    }
    glyf_offset_ = glyf->offset;
    glyf_length_ = glyf->length;

    if (!parse_head(error)) return false;
    if (!parse_maxp(error)) return false;
    if (!parse_hhea_hmtx(error)) return false;
    if (!parse_loca(error)) return false;
    if (!parse_cmap(error)) return false;


    return true;
}

const TtfFont::TableRecord* TtfFont::find_table(const char tag[4]) const {
    for (const auto& entry : tables_) {
        if (entry.first.size() == 4 && std::memcmp(entry.first.data(), tag, 4) == 0) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool TtfFont::parse_head(std::string& error) {
    const TableRecord* head = find_table("head");
    if (!head || head->length < 54) {
        error = "table head absente ou trop courte";
        return false;
    }
    if (!rd_u16(data_, head->offset + 18, units_per_em_) || units_per_em_ == 0) {
        error = "unitsPerEm invalide";
        return false;
    }
    if (!rd_i16(data_, head->offset + 50, index_to_loc_format_)) {
        error = "indexToLocFormat illisible";
        return false;
    }
    if (index_to_loc_format_ != 0 && index_to_loc_format_ != 1) {
        error = "indexToLocFormat inattendu";
        return false;
    }
    return true;
}

bool TtfFont::parse_maxp(std::string& error) {
    const TableRecord* maxp = find_table("maxp");
    if (!maxp || maxp->length < 6) {
        error = "table maxp absente ou trop courte";
        return false;
    }
    if (!rd_u16(data_, maxp->offset + 4, num_glyphs_) || num_glyphs_ == 0) {
        error = "numGlyphs invalide";
        return false;
    }
    return true;
}

bool TtfFont::parse_hhea_hmtx(std::string& error) {
    const TableRecord* hhea = find_table("hhea");
    const TableRecord* hmtx = find_table("hmtx");
    if (!hhea || hhea->length < 36 || !hmtx) {
        error = "tables hhea/hmtx absentes";
        return false;
    }
    if (!rd_u16(data_, hhea->offset + 34, num_h_metrics_) || num_h_metrics_ == 0) {
        error = "numberOfHMetrics invalide";
        return false;
    }
    rd_i16(data_, hhea->offset + 4, ascender_);
    rd_i16(data_, hhea->offset + 6, descender_);
    rd_i16(data_, hhea->offset + 8, line_gap_);

    hmtx_offset_ = hmtx->offset;
    return true;
}

bool TtfFont::parse_loca(std::string& error) {
    const TableRecord* loca = find_table("loca");
    if (!loca) {
        error = "table loca absente";
        return false;
    }
    const std::uint32_t count = static_cast<std::uint32_t>(num_glyphs_) + 1u;
    loca_.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        std::uint32_t rel = 0;
        if (index_to_loc_format_ == 0) {
            // Format court : les offsets sont stockes divises par deux.
            std::uint16_t half = 0;
            if (!rd_u16(data_, loca->offset + i * 2u, half)) {
                error = "loca (format court) tronquee";
                return false;
            }
            rel = static_cast<std::uint32_t>(half) * 2u;
        } else {
            if (!rd_u32(data_, loca->offset + i * 4u, rel)) {
                error = "loca (format long) tronquee";
                return false;
            }
        }
        loca_[i] = glyf_offset_ + rel;
    }
    return true;
}

bool TtfFont::parse_cmap(std::string& error) {
    const TableRecord* cmap = find_table("cmap");
    if (!cmap || cmap->length < 4) {
        error = "table cmap absente";
        return false;
    }
    std::uint16_t num_sub = 0;
    if (!rd_u16(data_, cmap->offset + 2, num_sub)) {
        error = "cmap : numTables illisible";
        return false;
    }

    // Selection de la sous-table : on prefere (3,10) UCS-4 puis (3,1) BMP,
    // qui couvrent tout ce qu une police Windows moderne expose. La plateforme
    // 0 (Unicode) sert de repli.
    std::uint32_t best_offset = 0;
    int best_score = -1;
    for (std::uint16_t i = 0; i < num_sub; ++i) {
        const std::uint32_t rec = cmap->offset + 4u + 8u * i;
        std::uint16_t platform = 0, encoding = 0;
        std::uint32_t sub_off = 0;
        if (!rd_u16(data_, rec, platform) || !rd_u16(data_, rec + 2, encoding) ||
            !rd_u32(data_, rec + 4, sub_off)) {
            continue;
        }
        int score = -1;
        if (platform == 3 && encoding == 10) score = 3;
        else if (platform == 3 && encoding == 1) score = 2;
        else if (platform == 0) score = 1;
        if (score > best_score) {
            best_score = score;
            best_offset = cmap->offset + sub_off;
        }
    }
    if (best_score < 0) {
        error = "cmap : aucune sous-table Unicode exploitable";
        return false;
    }

    std::uint16_t format = 0;
    if (!rd_u16(data_, best_offset, format)) {
        error = "cmap : format illisible";
        return false;
    }
    bool ok = false;
    if (format == 4) {
        ok = parse_cmap_format4(best_offset);
    } else if (format == 12) {
        ok = parse_cmap_format12(best_offset);
    } else {
        error = "cmap : format " + std::to_string(format) + " non gere (attendu 4 ou 12)";
        return false;
    }
    if (!ok) {
        error = "cmap : sous-table tronquee";
        return false;
    }

    std::sort(cmap_.begin(), cmap_.end());
    cmap_.erase(std::unique(cmap_.begin(), cmap_.end(),
                            [](const std::pair<std::uint32_t, std::uint16_t>& a,
                               const std::pair<std::uint32_t, std::uint16_t>& b) {
                                return a.first == b.first;
                            }),
                cmap_.end());
    return true;
}

bool TtfFont::parse_cmap_format4(std::uint32_t off) {
    std::uint16_t seg_x2 = 0;
    if (!rd_u16(data_, off + 6, seg_x2) || seg_x2 == 0) return false;
    const std::uint32_t seg_count = seg_x2 / 2u;

    const std::uint32_t end_base   = off + 14u;
    const std::uint32_t start_base = end_base + seg_x2 + 2u; // +2 : reservedPad
    const std::uint32_t delta_base = start_base + seg_x2;
    const std::uint32_t range_base = delta_base + seg_x2;

    for (std::uint32_t s = 0; s < seg_count; ++s) {
        std::uint16_t end_code = 0, start_code = 0, range_offset = 0;
        std::int16_t id_delta = 0;
        if (!rd_u16(data_, end_base + s * 2u, end_code) ||
            !rd_u16(data_, start_base + s * 2u, start_code) ||
            !rd_i16(data_, delta_base + s * 2u, id_delta) ||
            !rd_u16(data_, range_base + s * 2u, range_offset)) {
            return false;
        }
        if (start_code > end_code) continue;
        for (std::uint32_t c = start_code; c <= end_code; ++c) {
            if (c == 0xFFFF) continue; // sentinelle de fin de segment
            std::uint16_t gid = 0;
            if (range_offset == 0) {
                gid = static_cast<std::uint16_t>(
                    (c + static_cast<std::uint16_t>(id_delta)) & 0xFFFF);
            } else {
                // Adressage indirect : l offset est relatif a la position du
                // champ idRangeOffset lui-meme. C est la particularite du
                // format 4, et la source d erreur classique.
                const std::uint32_t idx =
                    range_base + s * 2u + range_offset + (c - start_code) * 2u;
                if (!rd_u16(data_, idx, gid)) return false;
                if (gid != 0) {
                    gid = static_cast<std::uint16_t>(
                        (gid + static_cast<std::uint16_t>(id_delta)) & 0xFFFF);
                }
            }
            if (gid != 0) cmap_.emplace_back(c, gid);
        }
    }
    return true;
}

bool TtfFont::parse_cmap_format12(std::uint32_t off) {
    std::uint32_t n_groups = 0;
    if (!rd_u32(data_, off + 12, n_groups)) return false;
    for (std::uint32_t g = 0; g < n_groups; ++g) {
        const std::uint32_t rec = off + 16u + 12u * g;
        std::uint32_t start = 0, end = 0, start_gid = 0;
        if (!rd_u32(data_, rec, start) || !rd_u32(data_, rec + 4, end) ||
            !rd_u32(data_, rec + 8, start_gid)) {
            return false;
        }
        if (end < start || end > 0x10FFFF) continue;
        for (std::uint32_t c = start; c <= end; ++c) {
            const std::uint32_t gid = start_gid + (c - start);
            if (gid != 0 && gid < num_glyphs_) {
                cmap_.emplace_back(c, static_cast<std::uint16_t>(gid));
            }
        }
    }
    return true;
}

std::uint16_t TtfFont::glyph_index_for_codepoint(std::uint32_t codepoint) const {
    const auto it = std::lower_bound(
        cmap_.begin(), cmap_.end(), codepoint,
        [](const std::pair<std::uint32_t, std::uint16_t>& e, std::uint32_t v) {
            return e.first < v;
        });
    if (it == cmap_.end() || it->first != codepoint) return 0;
    return it->second;
}

bool TtfFont::load_glyph(std::uint16_t glyph_index, Glyph& out, int depth) const {
    out = Glyph{};
    if (glyph_index >= num_glyphs_) return false;

    // --- Metriques horizontales (hmtx) ---
    // Les glyphes d indice >= numberOfHMetrics partagent la derniere advance ;
    // seul leur leftSideBearing est stocke, dans le tableau qui suit.
    {
        const std::uint16_t m = std::min<std::uint16_t>(
            glyph_index, static_cast<std::uint16_t>(num_h_metrics_ - 1));
        std::uint16_t adv = 0;
        std::int16_t lsb = 0;
        rd_u16(data_, hmtx_offset_ + m * 4u, adv);
        if (glyph_index < num_h_metrics_) {
            rd_i16(data_, hmtx_offset_ + glyph_index * 4u + 2u, lsb);
        } else {
            const std::uint32_t tail = hmtx_offset_ + num_h_metrics_ * 4u;
            rd_i16(data_, tail + (glyph_index - num_h_metrics_) * 2u, lsb);
        }
        out.advance_width = static_cast<float>(adv);
        out.left_side_bearing = static_cast<float>(lsb);
    }

    const std::uint32_t start = loca_[glyph_index];
    const std::uint32_t end = loca_[glyph_index + 1];
    if (end <= start) {
        // Longueur nulle = glyphe vide (l espace, typiquement). Ce n est pas
        // une erreur : les metriques restent valides.
        return true;
    }
    if (end > glyf_offset_ + glyf_length_ || start + 10 > data_.size()) return false;

    std::int16_t n_contours = 0;
    std::int16_t xmin = 0, ymin = 0, xmax = 0, ymax = 0;
    if (!rd_i16(data_, start, n_contours) || !rd_i16(data_, start + 2, xmin) ||
        !rd_i16(data_, start + 4, ymin) || !rd_i16(data_, start + 6, xmax) ||
        !rd_i16(data_, start + 8, ymax)) {
        return false;
    }
    out.x_min = xmin;
    out.y_min = ymin;
    out.x_max = xmax;
    out.y_max = ymax;

    if (n_contours >= 0) return load_simple_glyph(start, end, out);
    return load_composite_glyph(start, end, out, depth);
}

bool TtfFont::load_simple_glyph(std::uint32_t start, std::uint32_t end, Glyph& out) const {
    std::int16_t n_contours = 0;
    rd_i16(data_, start, n_contours);
    if (n_contours <= 0) return true;

    std::uint32_t p = start + 10;

    // endPtsOfContours : indice du dernier point de chaque contour.
    std::vector<std::uint16_t> end_pts(static_cast<std::size_t>(n_contours));
    for (int i = 0; i < n_contours; ++i) {
        if (!rd_u16(data_, p, end_pts[static_cast<std::size_t>(i)])) return false;
        p += 2;
    }
    const std::uint32_t n_points = static_cast<std::uint32_t>(end_pts.back()) + 1u;
    if (n_points == 0 || n_points > 10000) return false;

    // On saute les instructions de hinting : elles ne sont pas interpretees.
    std::uint16_t instr_len = 0;
    if (!rd_u16(data_, p, instr_len)) return false;
    p += 2u + instr_len;

    // --- Drapeaux, avec run-length (bit kRepeatFlag) ---
    std::vector<std::uint8_t> flags;
    flags.reserve(n_points);
    while (flags.size() < n_points) {
        std::uint8_t f = 0;
        if (!rd_u8(data_, p, f)) return false;
        ++p;
        flags.push_back(f);
        if (f & kRepeatFlag) {
            std::uint8_t repeat = 0;
            if (!rd_u8(data_, p, repeat)) return false;
            ++p;
            for (std::uint8_t r = 0; r < repeat && flags.size() < n_points; ++r) {
                flags.push_back(f);
            }
        }
    }

    // --- Coordonnees, stockees en deltas sur 1 ou 2 octets ---
    // Le couple (kXShortVector, kXSameOrPos) encode a la fois la taille et le
    // signe du delta : c est le point le plus contre-intuitif du format glyf.
    std::vector<std::int32_t> xs(n_points), ys(n_points);
    std::int32_t acc = 0;
    for (std::uint32_t i = 0; i < n_points; ++i) {
        const std::uint8_t f = flags[i];
        if (f & kXShortVector) {
            std::uint8_t d = 0;
            if (!rd_u8(data_, p, d)) return false;
            ++p;
            acc += (f & kXSameOrPos) ? static_cast<std::int32_t>(d)
                                     : -static_cast<std::int32_t>(d);
        } else if (!(f & kXSameOrPos)) {
            std::int16_t d = 0;
            if (!rd_i16(data_, p, d)) return false;
            p += 2;
            acc += d;
        } // sinon : delta nul, x inchange
        xs[i] = acc;
    }
    acc = 0;
    for (std::uint32_t i = 0; i < n_points; ++i) {
        const std::uint8_t f = flags[i];
        if (f & kYShortVector) {
            std::uint8_t d = 0;
            if (!rd_u8(data_, p, d)) return false;
            ++p;
            acc += (f & kYSameOrPos) ? static_cast<std::int32_t>(d)
                                     : -static_cast<std::int32_t>(d);
        } else if (!(f & kYSameOrPos)) {
            std::int16_t d = 0;
            if (!rd_i16(data_, p, d)) return false;
            p += 2;
            acc += d;
        }
        ys[i] = acc;
    }
    (void)end;

    // --- Decoupage en contours ---
    std::uint32_t first = 0;
    for (int c = 0; c < n_contours; ++c) {
        const std::uint32_t last = end_pts[static_cast<std::size_t>(c)];
        if (last < first || last >= n_points) return false;
        std::vector<GlyphPoint> contour;
        contour.reserve(last - first + 1);
        for (std::uint32_t i = first; i <= last; ++i) {
            GlyphPoint gp;
            gp.x = static_cast<float>(xs[i]);
            gp.y = static_cast<float>(ys[i]);
            gp.on_curve = (flags[i] & kOnCurve) != 0;
            contour.push_back(gp);
        }
        out.contours.push_back(std::move(contour));
        first = last + 1;
    }
    return true;
}

bool TtfFont::load_composite_glyph(std::uint32_t start, std::uint32_t end, Glyph& out,
                                   int depth) const {
    if (depth >= kMaxCompositeDepth) return false;

    std::uint32_t p = start + 10;
    for (;;) {
        std::uint16_t flags = 0, glyph_index = 0;
        if (!rd_u16(data_, p, flags) || !rd_u16(data_, p + 2, glyph_index)) return false;
        p += 4;

        // Arguments : soit un decalage xy, soit des indices de points a aligner.
        // On ne gere que le cas xy (kArgsAreXyValues) ; l alignement par points
        // est rarissime hors polices CJK anciennes.
        float dx = 0.0f, dy = 0.0f;
        if (flags & kArg1And2AreWords) {
            std::int16_t a1 = 0, a2 = 0;
            if (!rd_i16(data_, p, a1) || !rd_i16(data_, p + 2, a2)) return false;
            p += 4;
            if (flags & kArgsAreXyValues) {
                dx = static_cast<float>(a1);
                dy = static_cast<float>(a2);
            }
        } else {
            std::uint8_t b1 = 0, b2 = 0;
            if (!rd_u8(data_, p, b1) || !rd_u8(data_, p + 1, b2)) return false;
            p += 2;
            if (flags & kArgsAreXyValues) {
                dx = static_cast<float>(static_cast<std::int8_t>(b1));
                dy = static_cast<float>(static_cast<std::int8_t>(b2));
            }
        }

        // Matrice 2x2 optionnelle.
        float a = 1.0f, b = 0.0f, c = 0.0f, d = 1.0f;
        if (flags & kWeHaveAScale) {
            if (!rd_f2dot14(data_, p, a)) return false;
            d = a;
            p += 2;
        } else if (flags & kWeHaveXAndYScale) {
            if (!rd_f2dot14(data_, p, a) || !rd_f2dot14(data_, p + 2, d)) return false;
            p += 4;
        } else if (flags & kWeHaveATwoByTwo) {
            if (!rd_f2dot14(data_, p, a) || !rd_f2dot14(data_, p + 2, b) ||
                !rd_f2dot14(data_, p + 4, c) || !rd_f2dot14(data_, p + 6, d)) {
                return false;
            }
            p += 8;
        }

        Glyph part;
        if (load_glyph(glyph_index, part, depth + 1)) {
            for (auto& contour : part.contours) {
                for (auto& pt : contour) {
                    const float x = pt.x, y = pt.y;
                    pt.x = a * x + c * y + dx;
                    pt.y = b * x + d * y + dy;
                }
                out.contours.push_back(std::move(contour));
            }
        }

        if (!(flags & kMoreComponents)) break;
        if (p >= end) break;
    }
    return true;
}

} // namespace gw::font
