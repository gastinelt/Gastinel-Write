#include "app/home_screen.h"

#include "app/i18n.h"
#include "app/recent_files.h"
#include "app/theme.h"
#include "render/draw.h"
#include "render/icons.h"
#include "render/text_run.h"

#include <algorithm>

namespace gw::app {
namespace {

// Marges laterales : 17 % de chaque cote, donc 66 % de largeur utile.
constexpr float kSideMarginRatio = 0.17f;

constexpr int kVisibleRows = 7;
constexpr int kColumns = 2;

constexpr int kSettingsBox = 26;   // zone cliquable de licone parametres
constexpr int kSettingsIcon = 14;  // licone elle-meme, discrete
constexpr int kSettingsMargin = 10;

constexpr int kButtonHeight = 38;
constexpr int kButtonPaddingX = 22;
constexpr int kButtonGap = 12;
constexpr int kButtonsTop = 18;    // sous licone parametres

constexpr int kCardHeight = 44;    // juste de quoi tenir un titre sur une ligne
constexpr int kCardGapX = 12;
constexpr int kCardGapY = 8;
constexpr int kCardPaddingX = 14;
constexpr int kGridTopGap = 26;

constexpr int kFileIconHeight = 18;
constexpr int kExtensionGap = 7;

constexpr float kButtonTextSize = 15.0f;
constexpr float kCardTitleSize = 17.0f;
constexpr float kExtensionSize = 12.0f;
constexpr float kEmptyNoticeSize = 14.0f;

constexpr int kScrollStep = 3 * (kCardHeight + kCardGapY); // trois lignes par cran

// Libelles des deux boutons daction. Definis une seule fois : layout() les
// mesure pour dimensionner les rectangles, draw() les dessine — les deux
// doivent voir exactement la meme chaine.
constexpr char kCreateLabel[] = "Créer une nouvelle note";
constexpr char kOpenLabel[] = "Ouvrir une note";

// Tronque a lellipsis pour tenir dans `max_width`.
//
// La coupe se fait sur des points de code entiers, jamais au milieu dune
// sequence UTF-8 : couper a loctet produirait un glyphe de remplacement en
// bout de ligne. Le texte nest jamais renvoye a la ligne ni laisse deborder.
std::string ellipsize(const std::string& text, int max_width, const font::TtfFont& font,
                      render::GlyphAtlas& atlas) {
    if (render::measure_utf8(text, font, atlas) <= max_width) return text;

    const std::string suffix = "...";
    const int suffix_width = render::measure_utf8(suffix, font, atlas);

    // Frontieres de points de code, pour ne couper quentre deux caracteres.
    std::vector<std::size_t> boundaries;
    std::size_t i = 0;
    while (i < text.size()) {
        boundaries.push_back(i);
        render::next_codepoint(text, i);
    }

    for (std::size_t n = boundaries.size(); n > 0; --n) {
        const std::string candidate = text.substr(0, boundaries[n - 1]);
        if (render::measure_utf8(candidate, font, atlas) + suffix_width <= max_width) {
            return candidate + suffix;
        }
    }
    return suffix;
}

} // namespace

void HomeScreen::build_icons() {
    render::rasterize_glyph(render::make_icon(render::IconKind::Settings),
                            render::kIconUnitsPerEm, static_cast<float>(kSettingsIcon),
                            icon_settings_);
    render::rasterize_glyph(render::make_icon(render::IconKind::File), render::kIconUnitsPerEm,
                            static_cast<float>(kFileIconHeight), icon_file_);
}

void HomeScreen::layout(int width, int height, int content_top, int inset,
                        std::size_t recent_count, FontSet& fonts) {
    recent_count_ = recent_count;

    content_left_ = static_cast<int>(static_cast<float>(width) * kSideMarginRatio);
    content_width_ = width - 2 * content_left_;
    if (content_width_ < 120) content_width_ = std::max(0, width - 2 * inset);

    settings_ = Rect{inset + kSettingsMargin, content_top + kSettingsMargin, kSettingsBox,
                     kSettingsBox};

    // Les boutons sont poses dans la colonne utile, alignes a gauche, pour
    // saligner sur la grille qui les suit.
    const int buttons_y = settings_.y + settings_.h + kButtonsTop;
    const font::TtfFont& semibold = fonts.font(Weight::SemiBold);
    render::GlyphAtlas& button_atlas = fonts.atlas(Weight::SemiBold, kButtonTextSize);
    const int create_w =
        render::measure_utf8(i18n::tr(i18n::StringId::HomeNewNote), semibold, button_atlas) + 2 * kButtonPaddingX;
    const int open_w =
        render::measure_utf8(i18n::tr(i18n::StringId::HomeOpenNote), semibold, button_atlas) + 2 * kButtonPaddingX;

    create_ = Rect{content_left_, buttons_y, create_w, kButtonHeight};
    open_ = Rect{content_left_ + create_w + kButtonGap, buttons_y, open_w, kButtonHeight};

    grid_top_ = buttons_y + kButtonHeight + kGridTopGap;
    viewport_top_ = grid_top_;
    // Sept lignes au maximum sans defilement, et jamais au-dela du bas de la
    // fenetre : cest la plus contraignante des deux bornes qui gagne.
    const int seven_rows = kVisibleRows * kCardHeight + (kVisibleRows - 1) * kCardGapY;
    viewport_bottom_ = std::min(height - inset, grid_top_ + seven_rows);

    column_width_ = (content_width_ - (kColumns - 1) * kCardGapX) / kColumns;

    const int rows = static_cast<int>((recent_count_ + kColumns - 1) / kColumns);
    const int total_height = rows > 0 ? rows * kCardHeight + (rows - 1) * kCardGapY : 0;
    const int viewport_height = std::max(0, viewport_bottom_ - viewport_top_);
    scroll_max_ = std::max(0, total_height - viewport_height);
    scroll_ = std::clamp(scroll_, 0, scroll_max_);
}

HomeScreen::Rect HomeScreen::card_rect(std::size_t index) const {
    const int row = static_cast<int>(index) / kColumns;
    const int col = static_cast<int>(index) % kColumns;
    Rect r;
    r.x = content_left_ + col * (column_width_ + kCardGapX);
    r.y = grid_top_ + row * (kCardHeight + kCardGapY) - scroll_;
    r.w = column_width_;
    r.h = kCardHeight;
    return r;
}

int HomeScreen::card_at(int x, int y) const {
    // Un clic ne compte que dans la zone de defilement : une carte a moitie
    // sortie du cadre ne doit pas etre cliquable sur sa partie masquee.
    if (y < viewport_top_ || y >= viewport_bottom_) return -1;
    for (std::size_t i = 0; i < recent_count_; ++i) {
        if (card_rect(i).contains(x, y)) return static_cast<int>(i);
    }
    return -1;
}

bool HomeScreen::set_cursor(int x, int y) {
    const bool settings = settings_.contains(x, y);
    const bool create = create_.w > 0 && create_.contains(x, y);
    const bool open = open_.w > 0 && open_.contains(x, y);
    const int card = card_at(x, y);

    const bool changed = settings != hover_settings_ || create != hover_create_ ||
                         open != hover_open_ || card != hovered_card_;
    hover_settings_ = settings;
    hover_create_ = create;
    hover_open_ = open;
    hovered_card_ = card;
    return changed;
}

void HomeScreen::clear_cursor() {
    hover_settings_ = false;
    hover_create_ = false;
    hover_open_ = false;
    hovered_card_ = -1;
}

HomeAction HomeScreen::action_at(int x, int y) const {
    if (settings_.contains(x, y)) return HomeAction::Settings;
    if (create_.w > 0 && create_.contains(x, y)) return HomeAction::CreateNote;
    if (open_.w > 0 && open_.contains(x, y)) return HomeAction::OpenNote;
    if (card_at(x, y) >= 0) return HomeAction::OpenRecent;
    return HomeAction::None;
}

bool HomeScreen::scroll_by(int wheel_delta) {
    if (scroll_max_ <= 0) return false;
    const int previous = scroll_;
    scroll_ = std::clamp(scroll_ - wheel_delta * kScrollStep / 120, 0, scroll_max_);
    return scroll_ != previous;
}

void HomeScreen::draw(render::Framebuffer& fb, FontSet& fonts,
                      const std::vector<std::string>& recent) const {
    const Theme& t = theme();
    const font::TtfFont& regular = fonts.font(Weight::Regular);
    const font::TtfFont& semibold = fonts.font(Weight::SemiBold);
    render::GlyphAtlas& button_atlas = fonts.atlas(Weight::SemiBold, kButtonTextSize);
    render::GlyphAtlas& title_atlas = fonts.atlas(Weight::SemiBold, kCardTitleSize);
    render::GlyphAtlas& small_atlas = fonts.atlas(Weight::Regular, kExtensionSize);

    // --- Grille des notes recentes ---
    const int card_ascent = render::ascent_pixels(semibold, kCardTitleSize);
    const int card_line = render::line_height(semibold, kCardTitleSize);
    const int ext_ascent = render::ascent_pixels(regular, kExtensionSize);
    const int ext_line = render::line_height(regular, kExtensionSize);

    for (std::size_t i = 0; i < recent.size() && i < recent_count_; ++i) {
        const Rect r = card_rect(i);
        if (r.y + r.h < viewport_top_ || r.y > viewport_bottom_) continue; // hors cadre

        const bool hovered = (static_cast<int>(i) == hovered_card_);
        render::fill_rect(fb, r.x, r.y, r.w, r.h, hovered ? t.hover_fill : t.background);
        render::stroke_rect(fb, r.x, r.y, r.w, r.h, 1, t.foreground);

        // Reserve a droite : icone de fichier plus le texte de lextension.
        const std::string ext = extension_of(recent[i]);
        const int ext_width = ext.empty() ? 0 : render::measure_utf8(ext, regular, small_atlas);
        const int icon_width = icon_file_.coverage.width;
        const int right_block = icon_width + (ext.empty() ? 0 : kExtensionGap + ext_width);

        const int title_max = r.w - 2 * kCardPaddingX - right_block - kCardGapX;
        const std::string title =
            ellipsize(file_name_of(recent[i]), std::max(0, title_max), semibold, title_atlas);

        const int baseline = r.y + (r.h - card_line) / 2 + card_ascent;
        render::draw_utf8(fb, title, r.x + kCardPaddingX, baseline, semibold, title_atlas, t.foreground);

        // Icone generique + extension reelle : une seule forme de base, seul le
        // texte change selon le type.
        const int icon_x = r.x + r.w - kCardPaddingX - right_block;
        const int icon_y = r.y + (r.h - icon_file_.coverage.height) / 2;
        render::blit_coverage(fb, icon_file_.coverage, icon_x, icon_y, t.subtle);
        if (!ext.empty()) {
            const int ext_baseline = r.y + (r.h - ext_line) / 2 + ext_ascent;
            render::draw_utf8(fb, ext, icon_x + icon_width + kExtensionGap, ext_baseline, regular,
                              small_atlas, t.subtle);
        }
    }

    // Les cartes debordent volontairement du cadre pendant le defilement ; on
    // recouvre ensuite les bandes hors cadre avec le fond. Le fond etant un
    // aplat, cest strictement equivalent a un decoupage, pour trois lignes de
    // code au lieu dun rectangle de decoupe propage dans toutes les primitives.
    render::fill_rect(fb, 0, 0, fb.width, viewport_top_, t.background);
    if (viewport_bottom_ < fb.height) {
        render::fill_rect(fb, 0, viewport_bottom_, fb.width, fb.height - viewport_bottom_, t.background);
    }

    if (recent.empty()) {
        render::GlyphAtlas& notice_atlas = fonts.atlas(Weight::Regular, kEmptyNoticeSize);
        const int baseline = grid_top_ + render::ascent_pixels(regular, kEmptyNoticeSize);
        render::draw_utf8(fb, i18n::tr(i18n::StringId::HomeNoRecent), content_left_, baseline, regular,
                          notice_atlas, t.subtle);
    }

    // --- Icone parametres ---
    if (hover_settings_) {
        render::fill_rect(fb, settings_.x, settings_.y, settings_.w, settings_.h, t.hover_fill);
    }
    if (!icon_settings_.coverage.empty()) {
        const int ix = settings_.x + (settings_.w - icon_settings_.coverage.width) / 2;
        const int iy = settings_.y + (settings_.h - icon_settings_.coverage.height) / 2;
        render::blit_coverage(fb, icon_settings_.coverage, ix, iy, t.foreground);
    }

    // --- Boutons daction ---
    const int text_ascent = render::ascent_pixels(semibold, kButtonTextSize);
    const int text_line = render::line_height(semibold, kButtonTextSize);

    // "Creer" : fond blanc, filet noir, texte noir.
    render::fill_rect(fb, create_.x, create_.y, create_.w, create_.h,
                      hover_create_ ? t.hover_fill : t.background);
    render::stroke_rect(fb, create_.x, create_.y, create_.w, create_.h, 1, t.foreground);
    render::draw_utf8(fb, i18n::tr(i18n::StringId::HomeNewNote), create_.x + kButtonPaddingX,
                      create_.y + (create_.h - text_line) / 2 + text_ascent, semibold,
                      button_atlas, t.foreground);

    // "Ouvrir" : fond noir, texte blanc.
    render::fill_rect(fb, open_.x, open_.y, open_.w, open_.h, hover_open_ ? t.inverted_hover : t.inverted_fill);
    render::draw_utf8(fb, i18n::tr(i18n::StringId::HomeOpenNote), open_.x + kButtonPaddingX,
                      open_.y + (open_.h - text_line) / 2 + text_ascent, semibold, button_atlas,
                      t.inverted_text);
}

} // namespace gw::app
