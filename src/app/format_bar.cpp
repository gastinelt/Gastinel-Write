#include "app/format_bar.h"

#include <algorithm>

#include "app/theme.h"
#include "render/draw.h"
#include "render/icons.h"
#include "render/text_run.h"

namespace gw::app {
namespace {

constexpr int kBarHeight = 36;
constexpr int kButtonSize = 26;
constexpr int kButtonGap = 2;
constexpr int kGroupGap = 14;   // ecart entre deux groupes, filet compris
constexpr int kLeftMargin = 48; // aligne sur la marge de la colonne de texte
constexpr float kLabelSize = 14.0f;
constexpr int kIconSize = 13;

// Le selecteur de police est un bouton texte, pas un carre : sa largeur suit
// le nom affiche, avec un plancher pour qu un nom court ne donne pas un bouton
// minuscule et une borne haute pour qu un nom bavard ne pousse pas le reste
// hors de la fenetre.
constexpr int kFontButtonPadding = 12;
constexpr int kFontButtonMin = 92;
constexpr int kFontButtonMax = 220;

// Chevron du selecteur, dessine en dur : deux pixels de large suffisent a dire
// « ceci ouvre une liste », et cest le seul endroit du programme qui en a
// besoin — une icone de plus dans render/icons pour ca serait disproportionne.
constexpr char kChevron[] = "\xE2\x8C\x84"; // U+2304 CHEVRON BAS

} // namespace

void FormatBar::build_icons(float scale) {
    const float size = static_cast<float>(kIconSize) * scale;
    render::rasterize_glyph(render::make_icon(render::IconKind::AlignLeft),
                            render::kIconUnitsPerEm, size, icon_align_left_);
    render::rasterize_glyph(render::make_icon(render::IconKind::AlignCenter),
                            render::kIconUnitsPerEm, size, icon_align_center_);
    render::rasterize_glyph(render::make_icon(render::IconKind::AlignRight),
                            render::kIconUnitsPerEm, size, icon_align_right_);
}

int FormatBar::height() const { return kBarHeight; }

void FormatBar::layout(int width, int top, int inset, const std::string& font_name,
                       FontSet& fonts) {
    top_ = top;
    inset_ = inset;
    width_ = width;
    buttons_.clear();
    separators_.clear();

    const int y = top + (kBarHeight - kButtonSize) / 2;
    int x = kLeftMargin + inset;

    const auto add = [&](FormatAction action, const std::string& label, bool bold, bool italic,
                         bool underline, float label_scale) {
        Button b;
        b.x = x;
        b.y = y;
        b.w = kButtonSize;
        b.h = kButtonSize;
        b.action = action;
        b.label = label;
        b.label_bold = bold;
        b.label_italic = italic;
        b.label_underline = underline;
        b.label_scale = label_scale;
        buttons_.push_back(std::move(b));
        x += kButtonSize + kButtonGap;
    };

    const auto separator = [&]() {
        x += kGroupGap / 2 - kButtonGap;
        separators_.push_back(x);
        x += kGroupGap / 2;
    };

    // --- Attributs de caractere ---
    // Chaque libelle est dessine dans la variante quil applique : cest la
    // convention universelle de ces trois boutons, et elle sert aussi de
    // temoin — si le B nest pas plus gras que le I, cest que la synthese de
    // graisse est cassee.
    add(FormatAction::Bold, "B", true, false, false, 1.0f);
    add(FormatAction::Italic, "I", false, true, false, 1.0f);
    add(FormatAction::Underline, "U", false, false, true, 1.0f);
    separator();

    // --- Niveaux de titre ---
    // Les libelles sont plus petits que les boutons de caractere : « H1 » tient
    // deux glyphes dans la meme largeur.
    add(FormatAction::Heading1, "H1", true, false, false, 0.82f);
    add(FormatAction::Heading2, "H2", true, false, false, 0.82f);
    add(FormatAction::Heading3, "H3", true, false, false, 0.82f);
    separator();

    // --- Listes ---
    // La puce et le « 1. » sont exactement les marqueurs que la vue dessine
    // dans la page : le bouton montre litteralement son resultat.
    add(FormatAction::ListBullet, "\xE2\x80\xA2", false, false, false, 1.25f);
    add(FormatAction::ListNumbered, "1.", false, false, false, 0.9f);
    separator();

    // --- Alignement (icones) ---
    add(FormatAction::AlignLeft, std::string(), false, false, false, 1.0f);
    add(FormatAction::AlignCenter, std::string(), false, false, false, 1.0f);
    add(FormatAction::AlignRight, std::string(), false, false, false, 1.0f);
    separator();

    // --- Selecteur de police ---
    const FontFace label_face = fonts.face(std::string(), false, false, kLabelSize);
    const int name_width =
        render::measure_utf8(font_name, *label_face.font, *label_face.atlas) +
        render::measure_utf8(kChevron, *label_face.font, *label_face.atlas);
    const int wanted = std::clamp(name_width + kFontButtonPadding * 2, kFontButtonMin,
                                  kFontButtonMax);
    // La barre ne deborde jamais : si la fenetre est trop etroite, cest le
    // selecteur qui retrecit, parce que cest le seul element dont la largeur
    // nest pas dictee par son contenu minimal.
    const int available = std::max(0, width - inset - kLeftMargin / 2 - x);

    const int picker_width = std::min(wanted, available);
    if (picker_width > kButtonSize / 2) {
        Button picker;
        picker.x = x;
        picker.y = y;
        picker.w = picker_width;
        picker.h = kButtonSize;
        picker.action = FormatAction::FontPicker;
        picker.label = font_name;
        buttons_.push_back(std::move(picker));
    } else if (!separators_.empty()) {
        // Pas la place pour le selecteur : son filet de groupe na plus rien a
        // separer et pendrait dans le vide.
        separators_.pop_back();
    }
}

bool FormatBar::contains_point(int x, int y) const {
    return y >= top_ && y < top_ + kBarHeight && x >= inset_ && x < width_ - inset_;
}

FormatAction FormatBar::action_at(int x, int y) const {
    for (const Button& b : buttons_) {
        if (b.contains(x, y)) return b.action;
    }
    return FormatAction::None;
}

bool FormatBar::set_cursor(int x, int y) {
    const FormatAction found = action_at(x, y);
    if (found == hovered_) return false;
    hovered_ = found;
    return true;
}

void FormatBar::clear_cursor() { hovered_ = FormatAction::None; }

void FormatBar::font_picker_anchor(int& x, int& y) const {
    for (const Button& b : buttons_) {
        if (b.action != FormatAction::FontPicker) continue;
        x = b.x;
        y = b.y + b.h;
        return;
    }
    x = inset_ + kLeftMargin;
    y = top_ + kBarHeight;
}

bool FormatBar::is_active(FormatAction action, const FormatState& state) {
    switch (action) {
        case FormatAction::Bold: return state.bold;
        case FormatAction::Italic: return state.italic;
        case FormatAction::Underline: return state.underline;
        case FormatAction::Heading1: return state.heading == doc::HeadingLevel::H1;
        case FormatAction::Heading2: return state.heading == doc::HeadingLevel::H2;
        case FormatAction::Heading3: return state.heading == doc::HeadingLevel::H3;
        case FormatAction::ListBullet: return state.list == doc::ListKind::Bullet;
        case FormatAction::ListNumbered: return state.list == doc::ListKind::Numbered;
        case FormatAction::AlignLeft: return state.align == doc::Alignment::Left;
        case FormatAction::AlignCenter: return state.align == doc::Alignment::Center;
        case FormatAction::AlignRight: return state.align == doc::Alignment::Right;
        default: return false;
    }
}

const render::RasterizedGlyph* FormatBar::icon_for(FormatAction action) const {
    switch (action) {
        case FormatAction::AlignLeft: return &icon_align_left_;
        case FormatAction::AlignCenter: return &icon_align_center_;
        case FormatAction::AlignRight: return &icon_align_right_;
        default: return nullptr;
    }
}

void FormatBar::draw(render::Framebuffer& fb, FontSet& fonts, const FormatState& state) const {
    const Theme& t = theme();

    render::fill_rect(fb, inset_, top_, width_ - 2 * inset_, kBarHeight, t.background);

    for (int sx : separators_) {
        // Le filet ne monte pas jusqu aux bords de la bande : un separateur
        // pleine hauteur donnerait une grille, pas des groupes.
        const int margin = 7;
        render::fill_rect(fb, sx, top_ + margin, 1, kBarHeight - 2 * margin, t.separator);
    }

    for (const Button& b : buttons_) {
        const bool active = is_active(b.action, state);
        const bool hover = hovered_ == b.action;

        std::uint32_t bg = t.background;
        std::uint32_t fg = t.foreground;
        if (active) {
            // Surface inversee pour l etat actif : c est le seul moyen de dire
            // « ceci est allume » dans une charte monochrome, et c est deja ce
            // que fait le bouton plein ailleurs dans l interface.
            bg = hover ? t.inverted_hover : t.inverted_fill;
            fg = t.inverted_text;
        } else if (hover) {
            bg = t.hover_fill;
        }
        if (bg != t.background) render::fill_rect(fb, b.x, b.y, b.w, b.h, bg);

        if (const render::RasterizedGlyph* icon = icon_for(b.action)) {
            if (!icon->coverage.empty()) {
                const int ix = b.x + (b.w - icon->coverage.width) / 2;
                const int iy = b.y + (b.h - icon->coverage.height) / 2;
                render::blit_coverage(fb, icon->coverage, ix, iy, fg);
            }
            continue;
        }

        if (b.label.empty()) continue;

        const float size = kLabelSize * b.label_scale;
        const FontFace face = fonts.face(std::string(), b.label_bold, b.label_italic, size);
        const int line = render::line_height(*face.font, size);
        const int ascent = render::ascent_pixels(*face.font, size);
        const int baseline = b.y + (b.h - line) / 2 + ascent;

        if (b.action == FormatAction::FontPicker) {
            // Libelle cale a gauche, chevron cale a droite : le nom peut alors
            // etre coupe par le bord du bouton sans deplacer le chevron.
            const FontFace plain = fonts.face(std::string(), false, false, kLabelSize);
            const int chevron_width = render::measure_utf8(kChevron, *plain.font, *plain.atlas);
            render::draw_utf8(fb, kChevron, b.x + b.w - chevron_width - kFontButtonPadding / 2,
                              baseline, *plain.font, *plain.atlas, fg);
            render::draw_utf8(fb, b.label, b.x + kFontButtonPadding, baseline, *plain.font,
                              *plain.atlas, fg);
            continue;
        }

        const int label_width = render::measure_utf8(b.label, *face.font, *face.atlas);
        const int label_x = b.x + (b.w - label_width) / 2;
        render::draw_utf8(fb, b.label, label_x, baseline, *face.font, *face.atlas, fg);

        if (b.label_underline) {
            render::fill_rect(fb, label_x, baseline + std::max(2, static_cast<int>(size * 0.14f)),
                              label_width, 1, fg);
        }
    }

    // Filet de separation avec la zone de texte.
    render::fill_rect(fb, inset_, top_ + kBarHeight - 1, width_ - 2 * inset_, 1, t.separator);
}

} // namespace gw::app
