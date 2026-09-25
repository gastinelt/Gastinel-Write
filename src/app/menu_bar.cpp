#include "app/menu_bar.h"

#include "app/i18n.h"
#include "app/theme.h"
#include "render/draw.h"
#include "render/text_run.h"

namespace gw::app {
namespace {

constexpr int kBarHeight = 28;
constexpr int kTitleHeight = 22;
constexpr int kTitlePadding = 11; // de chaque cote du libelle
constexpr int kTitleGap = 2;
constexpr int kLeftMargin = 48;   // aligne sur la marge de la colonne de texte
constexpr float kLabelSize = 13.0f;

// Les trois seuls menus. Une table plutot qu une suite d appels : l ordre
// d affichage et la liste des menus se lisent alors d un seul coup d oeil, et
// ajouter un menu est une ligne.
struct TitleSpec {
    MenuBarMenu menu;
    const char* label;
};

constexpr TitleSpec kTitles[] = {
    {MenuBarMenu::File, "Fichier"},
    {MenuBarMenu::Edit, "Édition"},
    {MenuBarMenu::View, "Affichage"},
};

} // namespace

int MenuBar::height() const { return kBarHeight; }

void MenuBar::layout(int width, int top, int inset, FontSet& fonts) {
    top_ = top;
    inset_ = inset;
    width_ = width;
    titles_.clear();

    const font::TtfFont& font = fonts.font(Weight::Regular);
    render::GlyphAtlas& atlas = fonts.atlas(Weight::Regular, kLabelSize);

    const int y = top + (kBarHeight - kTitleHeight) / 2;
    int x = kLeftMargin + inset;

    for (const TitleSpec& spec : kTitles) {
        Title title;
        title.menu = spec.menu;
        if (spec.menu == MenuBarMenu::File) title.label = i18n::tr(i18n::StringId::MenuFile);
        else if (spec.menu == MenuBarMenu::Edit) title.label = i18n::tr(i18n::StringId::MenuEdit);
        else if (spec.menu == MenuBarMenu::View) title.label = i18n::tr(i18n::StringId::MenuView);
        else title.label = spec.label;
        title.w = render::measure_utf8(title.label, font, atlas) + 2 * kTitlePadding;
        title.h = kTitleHeight;
        title.x = x;
        title.y = y;
        titles_.push_back(title);
        x += title.w + kTitleGap;
    }
}

bool MenuBar::contains_point(int x, int y) const {
    return y >= top_ && y < top_ + kBarHeight && x >= inset_ && x < width_ - inset_;
}

MenuBarMenu MenuBar::menu_at(int x, int y) const {
    for (const Title& t : titles_) {
        if (t.contains(x, y)) return t.menu;
    }
    return MenuBarMenu::None;
}

bool MenuBar::set_cursor(int x, int y) {
    const MenuBarMenu hit = menu_at(x, y);
    if (hit == hovered_) return false;
    hovered_ = hit;
    return true;
}

void MenuBar::clear_cursor() { hovered_ = MenuBarMenu::None; }

void MenuBar::anchor_for(MenuBarMenu menu, int& x, int& y) const {
    for (const Title& t : titles_) {
        if (t.menu != menu) continue;
        x = t.x;
        y = t.y + t.h;
        return;
    }
    // Menu inconnu : on ancre sous la bande plutot que de laisser l appelant
    // avec des coordonnees non initialisees.
    x = kLeftMargin + inset_;
    y = top_ + kBarHeight;
}

void MenuBar::draw(render::Framebuffer& fb, FontSet& fonts) const {
    const Theme& t = theme();

    render::fill_rect(fb, inset_, top_, width_ - 2 * inset_, kBarHeight, t.background);

    const font::TtfFont& font = fonts.font(Weight::Regular);
    render::GlyphAtlas& atlas = fonts.atlas(Weight::Regular, kLabelSize);
    const int line = render::line_height(font, kLabelSize);
    const int ascent = render::ascent_pixels(font, kLabelSize);

    for (const Title& title : titles_) {
        const bool open = open_ == title.menu;
        const bool hover = hovered_ == title.menu;

        // AU REPOS : rien du tout, et un libelle gris. La bande doit se faire
        // oublier tant qu on ne la vise pas — c est la condition posee au
        // chantier pour qu un menu complet reste compatible avec une interface
        // sans distraction.
        std::uint32_t fg = t.subtle;
        if (open) {
            render::fill_rect(fb, title.x, title.y, title.w, title.h, t.inverted_fill);
            fg = t.inverted_text;
        } else if (hover) {
            render::fill_rect(fb, title.x, title.y, title.w, title.h, t.hover_fill);
            fg = t.foreground;
        }

        const int label_width = render::measure_utf8(title.label, font, atlas);
        render::draw_utf8(fb, title.label, title.x + (title.w - label_width) / 2,
                          title.y + (title.h - line) / 2 + ascent, font, atlas, fg);
    }
}

} // namespace gw::app
