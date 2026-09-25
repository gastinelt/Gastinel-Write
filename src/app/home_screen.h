// app/home_screen.h — ecran daccueil.
//
// Saffiche dans la zone cliente de la fenetre principale, sous le chrome
// custom : ce nest pas une fenetre separee. Aucune animation dapparition, la
// vue se substitue simplement a lediteur.
//
// Layout, dans lordre vertical :
//   icone parametres (coin superieur gauche, discrete)
//   deux boutons daction, immediatement visibles
//   grille des notes recentes : 2 colonnes, marges laterales de 17 % de chaque
//   cote, 7 lignes visibles, defilement vertical simple au-dela.
#pragma once

#include <string>
#include <vector>

#include "app/fonts.h"
#include "render/bitmap.h"
#include "render/rasterizer.h"

namespace gw::app {

// Element clique par lutilisateur, rendu a lappelant qui decide quoi en faire.
enum class HomeAction {
    None,
    Settings,
    CreateNote,
    OpenNote,
    OpenRecent, // lindice est donne par selected_recent()
};

class HomeScreen {
public:
    void build_icons();

    // Recalcule les rectangles. `content_top` est le bas du chrome.
    // `fonts` sert a mesurer les libelles des boutons : leur largeur depend du
    // texte, et le hit-test en a besoin avant tout dessin.
    void layout(int width, int height, int content_top, int inset, std::size_t recent_count,
                FontSet& fonts);

    // Met a jour lelement survole. Retourne true si laffichage doit changer.
    bool set_cursor(int x, int y);
    void clear_cursor();

    // Element sous le curseur au moment du clic.
    HomeAction action_at(int x, int y) const;
    int selected_recent() const { return hovered_card_; }

    // Defilement a la molette. `delta` est en crans WHEEL_DELTA.
    // Retourne true si la position a change.
    bool scroll_by(int wheel_delta);

    void draw(render::Framebuffer& fb, FontSet& fonts,
              const std::vector<std::string>& recent) const;

private:
    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
        bool contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    // Rectangle dune carte a lindice donne, defilement applique.
    Rect card_rect(std::size_t index) const;
    int card_at(int x, int y) const;

    Rect settings_{};
    Rect create_{};
    Rect open_{};

    int content_left_ = 0;   // debut de la colonne utile (17 % de la largeur)
    int content_width_ = 0;  // 66 % de la largeur
    int grid_top_ = 0;
    int viewport_top_ = 0;
    int viewport_bottom_ = 0;
    int column_width_ = 0;

    int scroll_ = 0;     // en pixels, toujours >= 0
    int scroll_max_ = 0;
    std::size_t recent_count_ = 0;

    bool hover_settings_ = false;
    bool hover_create_ = false;
    bool hover_open_ = false;
    int hovered_card_ = -1;

    render::RasterizedGlyph icon_settings_;
    render::RasterizedGlyph icon_file_;
};

} // namespace gw::app
