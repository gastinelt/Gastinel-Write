// font/ttf.h — parser TTF maison, sous-ensemble strict.
//
// Tables lues en v0 :
//   head  : unitsPerEm, indexToLocFormat
//   maxp  : numGlyphs
//   cmap  : Unicode -> glyph index (formats 4 et 12 uniquement)
//   loca  : offsets dans glyf (format court ou long selon head)
//   glyf  : contours quadratiques (glyphes simples + composites)
//   hmtx  : advanceWidth / leftSideBearing (avec hhea pour numberOfHMetrics)
//
// Volontairement absent (dette technique, cf. README) :
//   kern / GPOS  : aucun crenage
//   fpgm/prep/cvt: aucun hinting (les instructions sont ignorees)
//   CFF / CFF2   : les polices PostScript ne sont pas supportees
//   VORG, vmtx   : pas d'ecriture verticale
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gw::font {

// Un point de contour, en unites de police (em units).
// `on_curve` distingue les points d'ancrage des points de controle
// quadratiques (TrueType n'utilise que des quadratiques, pas de cubiques).
struct GlyphPoint {
    float x = 0.0f;
    float y = 0.0f;
    bool on_curve = false;
};

// Un glyphe decode : ses contours fermes, plus ses metriques.
struct Glyph {
    // Chaque contour est une liste de points ; le dernier point se referme
    // implicitement sur le premier.
    std::vector<std::vector<GlyphPoint>> contours;

    // Bounding box telle que declaree dans glyf (unites de police).
    float x_min = 0.0f, y_min = 0.0f, x_max = 0.0f, y_max = 0.0f;

    // Metriques horizontales (unites de police).
    float advance_width = 0.0f;
    float left_side_bearing = 0.0f;

    bool empty() const { return contours.empty(); }
};

// Police chargee en memoire. Le buffer du fichier est conserve entier :
// les tables sont lues a la demande via des offsets, on ne copie rien.
class TtfFont {
public:
    // Charge et valide un fichier .ttf. En cas d'echec, retourne false et
    // renseigne `error` avec un message lisible.
    bool load_from_file(const std::string& path, std::string& error);

    // Convertit un code point Unicode en index de glyphe.
    // Retourne 0 (.notdef) si absent de la cmap.
    std::uint16_t glyph_index_for_codepoint(std::uint32_t codepoint) const;

    // Decode les contours d'un glyphe. `depth` protege des composites recursifs.
    bool load_glyph(std::uint16_t glyph_index, Glyph& out, int depth = 0) const;

    std::uint16_t units_per_em() const { return units_per_em_; }

    // Metriques verticales globales, en unites de police (table hhea).
    // Necessaires pour centrer verticalement une ligne de texte dans une
    // barre : sans elles, on centrerait sur la bbox du glyphe, ce qui ferait
    // sauter la ligne de base selon les caracteres presents.
    std::int16_t ascender() const { return ascender_; }
    std::int16_t descender() const { return descender_; } // negatif
    std::int16_t line_gap() const { return line_gap_; }
    std::uint16_t num_glyphs() const { return num_glyphs_; }

private:
    struct TableRecord {
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
    };

    // Retourne nullptr si la table est absente.
    const TableRecord* find_table(const char tag[4]) const;

    bool parse_head(std::string& error);
    bool parse_maxp(std::string& error);
    bool parse_hhea_hmtx(std::string& error);
    bool parse_loca(std::string& error);
    bool parse_cmap(std::string& error);

    // Sous-parsers cmap : renvoient false si le format n'est pas gere.
    bool parse_cmap_format4(std::uint32_t subtable_offset);
    bool parse_cmap_format12(std::uint32_t subtable_offset);

    bool load_simple_glyph(std::uint32_t offset, std::uint32_t end, Glyph& out) const;
    bool load_composite_glyph(std::uint32_t offset, std::uint32_t end, Glyph& out, int depth) const;

    std::vector<std::uint8_t> data_;
    std::vector<std::pair<std::string, TableRecord>> tables_;

    std::uint16_t units_per_em_ = 1000;
    std::int16_t index_to_loc_format_ = 0;
    std::uint16_t num_glyphs_ = 0;
    std::uint16_t num_h_metrics_ = 0;
    std::int16_t ascender_ = 0;
    std::int16_t descender_ = 0;
    std::int16_t line_gap_ = 0;

    std::uint32_t glyf_offset_ = 0;
    std::uint32_t glyf_length_ = 0;
    std::uint32_t hmtx_offset_ = 0;

    // loca deplie en offsets absolus dans le fichier (taille = num_glyphs + 1).
    std::vector<std::uint32_t> loca_;

    // cmap aplatie : table de correspondance triee par code point.
    // Un vector trie + recherche binaire suffit largement ici et evite
    // d'embarquer une hash map pour quelques milliers d'entrees.
    std::vector<std::pair<std::uint32_t, std::uint16_t>> cmap_;
};

} // namespace gw::font
