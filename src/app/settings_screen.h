// app/settings_screen.h — ecran des parametres.
//
// Meme principe que l accueil : ce n est pas une fenetre, c est une facon de
// remplir la zone cliente sous le chrome custom. Aucune boite de dialogue
// systeme n est utilisee pour les reglages eux-memes — seul l import de police
// en ouvre une, parce que c est un selecteur de fichier et que l utilisateur
// attend precisement le dialogue systeme a cet endroit (cf. platform/dialogs.h).
//
// Layout, dans l ordre vertical :
//   bouton retour (coin superieur gauche, aligne sur l icone parametres de
//     l accueil pour que l aller-retour ne deplace pas le regard)
//   titre "Parametres"
//   ligne "Mode sombre"                 -> bascule
//   ligne "Mode clavier"                -> bascule
//   ligne "Taille de police par defaut" -> [-] valeur [+]
//   bloc "Polices" : liste + bouton d import
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "app/fonts.h"
#include "app/settings.h"
#include "render/bitmap.h"

namespace gw::app {

enum class SettingsAction {
    None,
    Back,
    ToggleOpenLastNote,
    ToggleQuoteOfTheDay,
    SetQuoteDurationLoading,
    SetQuoteDuration5s,
    SetQuoteDuration15s,
    ToggleDarkMode,
    ToggleKeyboardFirst,
    ToggleSmoothCaret,
    PrevLanguage,
    NextLanguage,
    FontSizeDown,
    FontSizeUp,
    ImportFont,
    RemoveFont, // l indice est donne par selected_font()
};

class SettingsScreen {
public:
    // `content_top` est le bas du chrome. `fonts` sert a mesurer les libelles :
    // la largeur des boutons depend du texte, et le hit-test en a besoin avant
    // tout dessin.
    void layout(int width, int height, int content_top, int inset, const Settings& settings,
                FontSet& fonts);

    bool set_cursor(int x, int y);
    void clear_cursor();

    SettingsAction action_at(int x, int y) const;

    // Indice dans Settings::fonts() de la police dont le bouton de retrait est
    // survole, -1 si aucun. Ce n est PAS l indice du bouton : les entrees
    // integrees n en ont pas, les deux numerotations divergent des la premiere
    // police importee.
    int selected_font() const;

    // Message transitoire affiche sous le bloc polices : resultat du dernier
    // import, reussi ou non. C est le seul retour visible d une operation qui
    // peut echouer pour une dizaine de raisons differentes.
    void set_notice(std::string text) { notice_ = std::move(text); }
    void clear_notice() { notice_.clear(); }

    void draw(render::Framebuffer& fb, FontSet& fonts, const Settings& settings) const;

private:
    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
        bool contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    Rect back_{};

    // Au demarrage
    Rect open_last_note_toggle_{};
    Rect quote_toggle_{};
    Rect quote_dur_loading_{};
    Rect quote_dur_5s_{};
    Rect quote_dur_15s_{};

    // Modes & apparence
    Rect dark_toggle_{};
    Rect keyboard_toggle_{};
    Rect smooth_toggle_{};
    Rect lang_prev_{};
    Rect lang_next_{};
    Rect size_down_{};
    Rect size_up_{};
    Rect import_{};
    std::vector<Rect> remove_buttons_; // un par police importee, dans l ordre

    // Les entrees integrees n ont pas de bouton de retrait : l indice d une
    // case de `remove_buttons_` n est donc pas un indice de `Settings::fonts()`.
    // Cette table fait la correspondance, et evite de la recalculer au clic.
    std::vector<std::size_t> remove_targets_;

    int content_left_ = 0;
    int content_width_ = 0;
    int title_top_ = 0;

    int startup_header_row_ = 0;
    int open_last_note_row_ = 0;
    int quote_row_ = 0;
    int quote_dur_row_ = 0;

    int dark_row_ = 0;
    int keyboard_row_ = 0;
    int smooth_row_ = 0;
    int lang_row_ = 0;
    int size_row_ = 0;
    int fonts_top_ = 0;
    int size_value_x_ = 0;
    int notice_top_ = 0;

    bool hover_back_ = false;
    bool hover_open_last_note_ = false;
    bool hover_quote_ = false;
    bool hover_dur_loading_ = false;
    bool hover_dur_5s_ = false;
    bool hover_dur_15s_ = false;
    bool hover_dark_ = false;
    bool hover_keyboard_ = false;
    bool hover_smooth_ = false;
    bool hover_lang_prev_ = false;
    bool hover_lang_next_ = false;
    bool hover_down_ = false;
    bool hover_up_ = false;
    bool hover_import_ = false;
    int hovered_remove_ = -1;

    std::string notice_;
};

} // namespace gw::app
