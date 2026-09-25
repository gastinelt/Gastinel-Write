#include "app/settings_screen.h"

#include "app/i18n.h"
#include "app/theme.h"
#include "render/draw.h"
#include "render/glyph_atlas.h"
#include "render/text_run.h"

#include <algorithm>

namespace gw::app {
namespace {

// Memes marges laterales que l accueil : passer d un ecran a l autre ne doit
// pas decaler la colonne de contenu.
constexpr float kSideMarginRatio = 0.17f;

constexpr int kTopMargin = 10;
constexpr int kBackBox = 26;
constexpr int kTitleGap = 26;
constexpr int kSectionGap = 30;
constexpr int kRowGap = 22;
constexpr int kControlHeight = 30;
constexpr int kButtonPaddingX = 18;
constexpr int kStepperBox = 30;
constexpr int kStepperGap = 8;
constexpr int kStepperValueWidth = 46;
constexpr int kFontRowHeight = 28;
constexpr int kFontRowGap = 4;
constexpr int kRemoveBox = 24;
constexpr int kFontsListGap = 12;

constexpr float kTitleSize = 24.0f;
constexpr float kLabelSize = 15.0f;
constexpr float kControlSize = 14.0f;
constexpr float kNoticeSize = 13.0f;

constexpr char kTitle[] = "Paramètres";
constexpr char kBack[] = "←";

constexpr char kStartupHeader[] = "Au démarrage";
constexpr char kOpenLastNoteLabel[] = "Aller dans la dernière note ouverte";
constexpr char kQuoteLabel[] = "Afficher la citation du jour";

constexpr char kDarkLabel[] = "Mode sombre";
// Le libelle dit ce que la bascule fait, pas comment elle s appelle en interne :
// « clavier-first » ne veut rien dire pour qui ouvre les parametres.
constexpr char kKeyboardLabel[] = "Mode clavier (Markdown en direct)";
constexpr char kSizeLabel[] = "Taille de police par défaut";
constexpr char kFontsLabel[] = "Polices";
constexpr char kImportLabel[] = "Importer une police (.ttf)";
constexpr char kRemoveGlyph[] = "×";

// Libelles de la bascule. Mesures tous les deux au layout : le rectangle ne
// doit pas changer de largeur quand l etat change, sinon le bouton se deplace
const char* on_label(i18n::Language lang) {
    switch (lang) {
        case i18n::Language::French: return "Activé";
        case i18n::Language::English: return "On";
        case i18n::Language::Spanish: return "Activado";
        case i18n::Language::German: return "Ein";
        case i18n::Language::Italian: return "Attivo";
        case i18n::Language::Portuguese: return "Ativado";
        case i18n::Language::Dutch: return "Aan";
        case i18n::Language::Polish: return "Włączone";
        case i18n::Language::Russian: return "Вкл";
        case i18n::Language::Turkish: return "Açık";
        default: return "On";
    }
}

const char* off_label(i18n::Language lang) {
    switch (lang) {
        case i18n::Language::French: return "Désactivé";
        case i18n::Language::English: return "Off";
        case i18n::Language::Spanish: return "Desactivado";
        case i18n::Language::German: return "Aus";
        case i18n::Language::Italian: return "Disattivo";
        case i18n::Language::Portuguese: return "Desativado";
        case i18n::Language::Dutch: return "Uit";
        case i18n::Language::Polish: return "Wyłączone";
        case i18n::Language::Russian: return "Выкл";
        case i18n::Language::Turkish: return "Kapalı";
        default: return "Off";
    }
}

const char* builtin_tag(i18n::Language lang) {
    switch (lang) {
        case i18n::Language::French: return "intégrée";
        case i18n::Language::English: return "built-in";
        case i18n::Language::Spanish: return "integrada";
        case i18n::Language::German: return "integriert";
        case i18n::Language::Italian: return "integrata";
        case i18n::Language::Portuguese: return "integrada";
        case i18n::Language::Dutch: return "ingebouwd";
        case i18n::Language::Polish: return "wbudowana";
        case i18n::Language::Russian: return "встроенный";
        case i18n::Language::Turkish: return "yerleşik";
        default: return "built-in";
    }
}

// Texte centre verticalement dans un rectangle, aligne a gauche apres padding.
void draw_in_box(render::Framebuffer& fb, const std::string& text, int x, int y, int h,
                 const font::TtfFont& font, render::GlyphAtlas& atlas, std::uint32_t color) {
    const int line = render::line_height(font, atlas.pixel_size());
    const int ascent = render::ascent_pixels(font, atlas.pixel_size());
    render::draw_utf8(fb, text, x, y + (h - line) / 2 + ascent, font, atlas, color);
}

void draw_centered(render::Framebuffer& fb, const std::string& text, int x, int y, int w, int h,
                   const font::TtfFont& font, render::GlyphAtlas& atlas, std::uint32_t color) {
    const int width = render::measure_utf8(text, font, atlas);
    draw_in_box(fb, text, x + (w - width) / 2, y, h, font, atlas, color);
}

void draw_toggle(render::Framebuffer& fb, int x, int y, int w, int h, bool on, bool hovered,
                 const font::TtfFont& font, render::GlyphAtlas& atlas, i18n::Language lang) {
    const Theme& t = theme();
    render::fill_rect(fb, x, y, w, h,
                      on ? (hovered ? t.inverted_hover : t.inverted_fill)
                         : (hovered ? t.hover_fill : t.background));
    if (!on) render::stroke_rect(fb, x, y, w, h, 1, t.foreground);
    draw_centered(fb, on ? on_label(lang) : off_label(lang), x, y, w, h, font, atlas,
                  on ? t.inverted_text : t.foreground);
}

void draw_choice_button(render::Framebuffer& fb, int x, int y, int w, int h, const std::string& text,
                        bool active, bool hovered, const font::TtfFont& font, render::GlyphAtlas& atlas) {
    const Theme& t = theme();
    render::fill_rect(fb, x, y, w, h,
                      active ? (hovered ? t.inverted_hover : t.inverted_fill)
                             : (hovered ? t.hover_fill : t.background));
    if (!active) render::stroke_rect(fb, x, y, w, h, 1, t.foreground);
    draw_centered(fb, text, x, y, w, h, font, atlas,
                  active ? t.inverted_text : t.foreground);
}

} // namespace

void SettingsScreen::layout(int width, int height, int content_top, int inset,
                            const Settings& settings, FontSet& fonts) {
    (void)height; // l ecran ne defile pas : sa hauteur ne contraint aucun calcul

    content_left_ = static_cast<int>(static_cast<float>(width) * kSideMarginRatio);
    content_width_ = width - 2 * content_left_;
    if (content_width_ < 200) content_width_ = std::max(0, width - 2 * inset);

    back_ = Rect{inset + 10, content_top + kTopMargin, kBackBox, kBackBox};

    const font::TtfFont& semibold = fonts.font(Weight::SemiBold);
    render::GlyphAtlas& control_atlas = fonts.atlas(Weight::SemiBold, kControlSize);

    title_top_ = back_.y + back_.h + kTitleGap;

    const int toggle_w = 110;

    // --- Au demarrage ---
    startup_header_row_ = title_top_ + render::line_height(semibold, kTitleSize) + kSectionGap;
    open_last_note_row_ = startup_header_row_ + render::line_height(semibold, kLabelSize) + 10;
    open_last_note_toggle_ = Rect{content_left_ + content_width_ - toggle_w, open_last_note_row_,
                                  toggle_w, kControlHeight};

    quote_row_ = open_last_note_row_ + kControlHeight + kRowGap;
    quote_toggle_ = Rect{content_left_ + content_width_ - toggle_w, quote_row_, toggle_w,
                         kControlHeight};

    int next_section_top = quote_row_ + kControlHeight;
    if (settings.show_quote_of_the_day()) {
        quote_dur_row_ = quote_row_ + kControlHeight + 10;
        const i18n::Language lang = settings.language();
        const int dur_load_w = render::measure_utf8(i18n::tr(i18n::StringId::SettingsQuoteDurLoading, lang), semibold, control_atlas) + 2 * 12;
        const int dur_5s_w = render::measure_utf8(i18n::tr(i18n::StringId::SettingsQuoteDur5s, lang), semibold, control_atlas) + 2 * 12;
        const int dur_15s_w = render::measure_utf8(i18n::tr(i18n::StringId::SettingsQuoteDur15s, lang), semibold, control_atlas) + 2 * 12;
        constexpr int kChoiceGap = 8;
        constexpr int kChoiceH = 26;

        quote_dur_15s_ = Rect{content_left_ + content_width_ - dur_15s_w, quote_dur_row_, dur_15s_w,
                              kChoiceH};
        quote_dur_5s_ = Rect{quote_dur_15s_.x - kChoiceGap - dur_5s_w, quote_dur_row_, dur_5s_w,
                             kChoiceH};
        quote_dur_loading_ = Rect{quote_dur_5s_.x - kChoiceGap - dur_load_w, quote_dur_row_,
                                  dur_load_w, kChoiceH};

        next_section_top = quote_dur_row_ + kChoiceH;
    } else {
        quote_dur_loading_ = Rect{};
        quote_dur_5s_ = Rect{};
        quote_dur_15s_ = Rect{};
    }

    dark_row_ = next_section_top + kSectionGap;
    dark_toggle_ = Rect{content_left_ + content_width_ - toggle_w, dark_row_, toggle_w,
                        kControlHeight};

    keyboard_row_ = dark_row_ + kControlHeight + kRowGap;
    keyboard_toggle_ = Rect{content_left_ + content_width_ - toggle_w, keyboard_row_, toggle_w,
                            kControlHeight};

    smooth_row_ = keyboard_row_ + kControlHeight + kRowGap;
    smooth_toggle_ = Rect{content_left_ + content_width_ - toggle_w, smooth_row_, toggle_w,
                          kControlHeight};

    lang_row_ = smooth_row_ + kControlHeight + kRowGap;
    constexpr int kArrowW = 28;
    constexpr int kLangBoxW = 150;
    lang_next_ = Rect{content_left_ + content_width_ - kArrowW, lang_row_, kArrowW, kControlHeight};
    lang_prev_ = Rect{content_left_ + content_width_ - kLangBoxW - kArrowW, lang_row_, kArrowW, kControlHeight};

    size_row_ = lang_row_ + kControlHeight + kRowGap;
    size_up_ = Rect{content_left_ + content_width_ - kStepperBox, size_row_, kStepperBox,
                    kControlHeight};
    size_value_x_ = size_up_.x - kStepperGap - kStepperValueWidth;
    size_down_ = Rect{size_value_x_ - kStepperGap - kStepperBox, size_row_, kStepperBox,
                      kControlHeight};

    fonts_top_ = size_row_ + kControlHeight + kSectionGap;

    // Liste des polices : une ligne par entree, bouton de retrait a droite pour
    // les seules polices importees.
    remove_buttons_.clear();
    remove_targets_.clear();
    const int list_top = fonts_top_ + render::line_height(semibold, kLabelSize) + kFontsListGap;
    int row_y = list_top;
    const std::vector<FontEntry>& list = settings.fonts();
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (!list[i].built_in) {
            remove_buttons_.push_back(Rect{content_left_ + content_width_ - kRemoveBox,
                                           row_y + (kFontRowHeight - kRemoveBox) / 2, kRemoveBox,
                                           kRemoveBox});
            remove_targets_.push_back(i);
        }
        row_y += kFontRowHeight + kFontRowGap;
    }

    const int import_w =
        render::measure_utf8(kImportLabel, semibold, control_atlas) + 2 * kButtonPaddingX;
    import_ = Rect{content_left_, row_y + kFontsListGap, import_w, kControlHeight};

    notice_top_ = import_.y + import_.h + 14;
}

int SettingsScreen::selected_font() const {
    if (hovered_remove_ < 0) return -1;
    const std::size_t i = static_cast<std::size_t>(hovered_remove_);
    if (i >= remove_targets_.size()) return -1;
    return static_cast<int>(remove_targets_[i]);
}

bool SettingsScreen::set_cursor(int x, int y) {
    const bool back = back_.contains(x, y);
    const bool open_last = open_last_note_toggle_.contains(x, y);
    const bool quote = quote_toggle_.contains(x, y);
    const bool dur_loading = quote_dur_loading_.contains(x, y);
    const bool dur_5s = quote_dur_5s_.contains(x, y);
    const bool dur_15s = quote_dur_15s_.contains(x, y);
    const bool dark = dark_toggle_.contains(x, y);
    const bool keyboard = keyboard_toggle_.contains(x, y);
    const bool smooth = smooth_toggle_.contains(x, y);
    const bool lang_prev = lang_prev_.contains(x, y);
    const bool lang_next = lang_next_.contains(x, y);
    const bool down = size_down_.contains(x, y);
    const bool up = size_up_.contains(x, y);
    const bool import_hit = import_.contains(x, y);

    int remove = -1;
    for (std::size_t i = 0; i < remove_buttons_.size(); ++i) {
        if (remove_buttons_[i].contains(x, y)) {
            remove = static_cast<int>(i);
            break;
        }
    }

    const bool changed = back != hover_back_ || open_last != hover_open_last_note_ ||
                         quote != hover_quote_ || dur_loading != hover_dur_loading_ ||
                         dur_5s != hover_dur_5s_ || dur_15s != hover_dur_15s_ ||
                         dark != hover_dark_ || keyboard != hover_keyboard_ ||
                         smooth != hover_smooth_ || lang_prev != hover_lang_prev_ ||
                         lang_next != hover_lang_next_ ||
                         down != hover_down_ || up != hover_up_ ||
                         import_hit != hover_import_ || remove != hovered_remove_;
    hover_back_ = back;
    hover_open_last_note_ = open_last;
    hover_quote_ = quote;
    hover_dur_loading_ = dur_loading;
    hover_dur_5s_ = dur_5s;
    hover_dur_15s_ = dur_15s;
    hover_dark_ = dark;
    hover_keyboard_ = keyboard;
    hover_smooth_ = smooth;
    hover_lang_prev_ = lang_prev;
    hover_lang_next_ = lang_next;
    hover_down_ = down;
    hover_up_ = up;
    hover_import_ = import_hit;
    hovered_remove_ = remove;
    return changed;
}

void SettingsScreen::clear_cursor() {
    hover_back_ = false;
    hover_open_last_note_ = false;
    hover_quote_ = false;
    hover_dur_loading_ = false;
    hover_dur_5s_ = false;
    hover_dur_15s_ = false;
    hover_dark_ = false;
    hover_keyboard_ = false;
    hover_smooth_ = false;
    hover_lang_prev_ = false;
    hover_lang_next_ = false;
    hover_down_ = false;
    hover_up_ = false;
    hover_import_ = false;
    hovered_remove_ = -1;
}

SettingsAction SettingsScreen::action_at(int x, int y) const {
    if (back_.contains(x, y)) return SettingsAction::Back;
    if (open_last_note_toggle_.contains(x, y)) return SettingsAction::ToggleOpenLastNote;
    if (quote_toggle_.contains(x, y)) return SettingsAction::ToggleQuoteOfTheDay;
    if (quote_dur_loading_.contains(x, y)) return SettingsAction::SetQuoteDurationLoading;
    if (quote_dur_5s_.contains(x, y)) return SettingsAction::SetQuoteDuration5s;
    if (quote_dur_15s_.contains(x, y)) return SettingsAction::SetQuoteDuration15s;
    if (dark_toggle_.contains(x, y)) return SettingsAction::ToggleDarkMode;
    if (keyboard_toggle_.contains(x, y)) return SettingsAction::ToggleKeyboardFirst;
    if (smooth_toggle_.contains(x, y)) return SettingsAction::ToggleSmoothCaret;
    if (lang_prev_.contains(x, y)) return SettingsAction::PrevLanguage;
    if (lang_next_.contains(x, y)) return SettingsAction::NextLanguage;
    if (size_down_.contains(x, y)) return SettingsAction::FontSizeDown;
    if (size_up_.contains(x, y)) return SettingsAction::FontSizeUp;
    if (import_.contains(x, y)) return SettingsAction::ImportFont;
    for (const Rect& r : remove_buttons_) {
        if (r.contains(x, y)) return SettingsAction::RemoveFont;
    }
    return SettingsAction::None;
}

void SettingsScreen::draw(render::Framebuffer& fb, FontSet& fonts,
                          const Settings& settings) const {
    const Theme& t = theme();
    const font::TtfFont& semibold = fonts.font(Weight::SemiBold);
    const font::TtfFont& regular = fonts.font(Weight::Regular);
    render::GlyphAtlas& title_atlas = fonts.atlas(Weight::SemiBold, kTitleSize);
    render::GlyphAtlas& label_atlas = fonts.atlas(Weight::SemiBold, kLabelSize);
    render::GlyphAtlas& control_atlas = fonts.atlas(Weight::SemiBold, kControlSize);
    render::GlyphAtlas& body_atlas = fonts.atlas(Weight::Regular, kControlSize);
    render::GlyphAtlas& notice_atlas = fonts.atlas(Weight::Regular, kNoticeSize);

    const i18n::Language lang = settings.language();

    // --- Retour ---
    if (hover_back_) render::fill_rect(fb, back_.x, back_.y, back_.w, back_.h, t.hover_fill);
    draw_centered(fb, kBack, back_.x, back_.y, back_.w, back_.h, semibold, label_atlas,
                  t.foreground);

    // --- Titre ---
    render::draw_utf8(fb, i18n::tr(i18n::StringId::SettingsTitle), content_left_,
                      title_top_ + render::ascent_pixels(semibold, kTitleSize), semibold,
                      title_atlas, t.foreground);

    // --- Au démarrage ---
    render::draw_utf8(fb, i18n::tr(i18n::StringId::SettingsStartup), content_left_,
                      startup_header_row_ + render::ascent_pixels(semibold, kLabelSize), semibold,
                      label_atlas, t.foreground);

    draw_in_box(fb, i18n::tr(i18n::StringId::SettingsOpenLast), content_left_, open_last_note_row_, kControlHeight,
                regular, body_atlas, t.foreground);
    draw_toggle(fb, open_last_note_toggle_.x, open_last_note_toggle_.y, open_last_note_toggle_.w,
                open_last_note_toggle_.h, settings.open_last_note(), hover_open_last_note_,
                semibold, control_atlas, lang);

    draw_in_box(fb, i18n::tr(i18n::StringId::SettingsShowQuote), content_left_, quote_row_, kControlHeight, regular, body_atlas,
                t.foreground);
    draw_toggle(fb, quote_toggle_.x, quote_toggle_.y, quote_toggle_.w, quote_toggle_.h,
                settings.show_quote_of_the_day(), hover_quote_, semibold, control_atlas, lang);

    if (settings.show_quote_of_the_day()) {
        draw_in_box(fb, i18n::tr(i18n::StringId::SettingsQuoteDuration, lang), content_left_ + 16, quote_dur_row_,
                    quote_dur_loading_.h, regular, notice_atlas, t.subtle);

        const auto cur_dur = settings.quote_duration();
        draw_choice_button(fb, quote_dur_loading_.x, quote_dur_loading_.y, quote_dur_loading_.w,
                           quote_dur_loading_.h, i18n::tr(i18n::StringId::SettingsQuoteDurLoading, lang),
                           cur_dur == Settings::QuoteDuration::WhileLoading, hover_dur_loading_,
                           semibold, control_atlas);
        draw_choice_button(fb, quote_dur_5s_.x, quote_dur_5s_.y, quote_dur_5s_.w,
                           quote_dur_5s_.h, i18n::tr(i18n::StringId::SettingsQuoteDur5s, lang),
                           cur_dur == Settings::QuoteDuration::FiveSeconds, hover_dur_5s_,
                           semibold, control_atlas);
        draw_choice_button(fb, quote_dur_15s_.x, quote_dur_15s_.y, quote_dur_15s_.w,
                           quote_dur_15s_.h, i18n::tr(i18n::StringId::SettingsQuoteDur15s, lang),
                           cur_dur == Settings::QuoteDuration::FifteenSeconds, hover_dur_15s_,
                           semibold, control_atlas);
    }

    // --- Mode sombre ---
    draw_in_box(fb, i18n::tr(i18n::StringId::SettingsDarkMode), content_left_, dark_row_, kControlHeight, semibold, label_atlas,
                t.foreground);
    draw_toggle(fb, dark_toggle_.x, dark_toggle_.y, dark_toggle_.w, dark_toggle_.h, dark_mode(),
                hover_dark_, semibold, control_atlas, lang);

    // --- Mode clavier ---
    draw_in_box(fb, i18n::tr(i18n::StringId::ActionToggleKeyboard), content_left_, keyboard_row_, kControlHeight, semibold,
                label_atlas, t.foreground);
    draw_toggle(fb, keyboard_toggle_.x, keyboard_toggle_.y, keyboard_toggle_.w,
                keyboard_toggle_.h, settings.keyboard_first(), hover_keyboard_, semibold,
                control_atlas, lang);

    // --- Curseur fluide ---
    draw_in_box(fb, i18n::tr(i18n::StringId::SettingsSmoothCaret), content_left_, smooth_row_, kControlHeight, semibold,
                label_atlas, t.foreground);
    draw_toggle(fb, smooth_toggle_.x, smooth_toggle_.y, smooth_toggle_.w,
                smooth_toggle_.h, settings.smooth_caret(), hover_smooth_, semibold,
                control_atlas, lang);

    // --- Langue ---
    draw_in_box(fb, i18n::tr(i18n::StringId::SettingsLanguage), content_left_, lang_row_, kControlHeight, semibold,
                label_atlas, t.foreground);

    render::fill_rect(fb, lang_prev_.x, lang_prev_.y, lang_prev_.w, lang_prev_.h,
                      hover_lang_prev_ ? t.hover_fill : t.background);
    render::stroke_rect(fb, lang_prev_.x, lang_prev_.y, lang_prev_.w, lang_prev_.h, 1, t.foreground);
    draw_centered(fb, "<", lang_prev_.x, lang_prev_.y, lang_prev_.w, lang_prev_.h, semibold, control_atlas, t.foreground);

    const int lang_name_x = lang_prev_.x + lang_prev_.w;
    const int lang_name_w = lang_next_.x - lang_name_x;
    draw_centered(fb, i18n::language_name(settings.language()), lang_name_x, lang_row_, lang_name_w, kControlHeight,
                  semibold, control_atlas, t.foreground);

    render::fill_rect(fb, lang_next_.x, lang_next_.y, lang_next_.w, lang_next_.h,
                      hover_lang_next_ ? t.hover_fill : t.background);
    render::stroke_rect(fb, lang_next_.x, lang_next_.y, lang_next_.w, lang_next_.h, 1, t.foreground);
    draw_centered(fb, ">", lang_next_.x, lang_next_.y, lang_next_.w, lang_next_.h, semibold, control_atlas, t.foreground);

    // --- Taille de police par defaut ---
    draw_in_box(fb, i18n::tr(i18n::StringId::SettingsFontSize), content_left_, size_row_, kControlHeight, semibold, label_atlas,
                t.foreground);

    const int size_value = settings.default_font_size();
    const bool at_min = size_value <= Settings::kMinFontSize;
    const bool at_max = size_value >= Settings::kMaxFontSize;

    render::fill_rect(fb, size_down_.x, size_down_.y, size_down_.w, size_down_.h,
                      hover_down_ && !at_min ? t.hover_fill : t.background);
    render::stroke_rect(fb, size_down_.x, size_down_.y, size_down_.w, size_down_.h, 1,
                        at_min ? t.disabled : t.foreground);
    draw_centered(fb, "−", size_down_.x, size_down_.y, size_down_.w, size_down_.h, semibold,
                  control_atlas, at_min ? t.disabled : t.foreground);

    draw_centered(fb, std::to_string(size_value), size_value_x_, size_row_, kStepperValueWidth,
                  kControlHeight, semibold, control_atlas, t.foreground);

    render::fill_rect(fb, size_up_.x, size_up_.y, size_up_.w, size_up_.h,
                      hover_up_ && !at_max ? t.hover_fill : t.background);
    render::stroke_rect(fb, size_up_.x, size_up_.y, size_up_.w, size_up_.h, 1,
                        at_max ? t.disabled : t.foreground);
    draw_centered(fb, "+", size_up_.x, size_up_.y, size_up_.w, size_up_.h, semibold, control_atlas,
                  at_max ? t.disabled : t.foreground);

    // --- Polices ---
    render::draw_utf8(fb, i18n::tr(i18n::StringId::SettingsFonts), content_left_,
                      fonts_top_ + render::ascent_pixels(semibold, kLabelSize), semibold,
                      label_atlas, t.foreground);

    const std::vector<FontEntry>& list = settings.fonts();
    int row_y = fonts_top_ + render::line_height(semibold, kLabelSize) + kFontsListGap;
    std::size_t remove_index = 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        render::fill_rect(fb, content_left_, row_y, content_width_, kFontRowHeight, t.background);
        draw_in_box(fb, list[i].display_name, content_left_ + 2, row_y, kFontRowHeight, regular,
                    body_atlas, t.foreground);

        if (list[i].built_in) {
            const std::string tag = builtin_tag(lang);
            const int w = render::measure_utf8(tag, regular, notice_atlas);
            draw_in_box(fb, tag, content_left_ + content_width_ - w, row_y, kFontRowHeight,
                        regular, notice_atlas, t.subtle);
        } else if (remove_index < remove_buttons_.size()) {
            const Rect& r = remove_buttons_[remove_index];
            const bool hovered = static_cast<int>(remove_index) == hovered_remove_;
            if (hovered) render::fill_rect(fb, r.x, r.y, r.w, r.h, t.hover_fill);
            draw_centered(fb, kRemoveGlyph, r.x, r.y, r.w, r.h, semibold, control_atlas,
                          hovered ? t.foreground : t.subtle);
            ++remove_index;
        }
        row_y += kFontRowHeight + kFontRowGap;
    }

    render::fill_rect(fb, import_.x, import_.y, import_.w, import_.h,
                      hover_import_ ? t.inverted_hover : t.inverted_fill);
    draw_centered(fb, i18n::tr(i18n::StringId::SettingsImportFont), import_.x, import_.y, import_.w, import_.h, semibold,
                  control_atlas, t.inverted_text);

    if (!notice_.empty()) {
        render::draw_utf8(fb, notice_, content_left_,
                          notice_top_ + render::ascent_pixels(regular, kNoticeSize), regular,
                          notice_atlas, t.subtle);
    }
}

} // namespace gw::app
