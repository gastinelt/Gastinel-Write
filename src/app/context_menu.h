// app/context_menu.h — menu contextuel dessine maison.
//
// Comme la barre de titre, ce nest pas une fenetre : cest un rectangle
// dessine dans le framebuffer de la fenetre principale.
//
// CONSEQUENCE ASSUMEE : le menu ne peut pas deborder de la fenetre. Un vrai
// menu Win32 est une fenetre popup de niveau superieur, donc il peut sortir de
// lapplication ; ici, un clic droit tout en bas a droite verra le menu se
// replier vers linterieur. Le passage a une fenetre popup dediee reste
// possible plus tard sans toucher a cette interface.
//
// La classe est generique : elle recoit une liste dentrees et dit laquelle a
// ete choisie. Elle ne connait aucune action metier.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "font/ttf.h"
#include "platform/keys.h"
#include "render/bitmap.h"
#include "render/glyph_atlas.h"

namespace gw::app {

struct MenuItem {
    std::string label;
    std::string shortcut; // affiche a droite, purement indicatif
    int id = 0;           // identifiant rendu au choix
    bool enabled = true;
    bool separator = false;

    static MenuItem make_separator() {
        MenuItem item;
        item.separator = true;
        item.enabled = false;
        return item;
    }
};

// Retourne par les gestionnaires devenement pour dire a lappelant quoi faire.
enum class MenuResult {
    Ignored,  // levenement ne concerne pas le menu
    Consumed, // absorbe (survol, clic dans le menu sans validation)
    Chosen,   // une entree a ete validee, cf. chosen_id()
    Dismissed // le menu doit se fermer sans choix
};

class ContextMenu {
public:
    // Ouvre le menu a la position demandee, replie dans les bornes fournies.
    void open(std::vector<MenuItem> items, int x, int y, int bounds_width, int bounds_height,
              const font::TtfFont& font, render::GlyphAtlas& atlas);

    void close();
    bool is_open() const { return open_; }
    int chosen_id() const { return chosen_id_; }

    bool contains(int x, int y) const;

    MenuResult on_mouse_move(int x, int y);
    MenuResult on_left_up(int x, int y);
    MenuResult on_key_down(platform::Key key);

    void draw(render::Framebuffer& fb, const font::TtfFont& font,
              render::GlyphAtlas& atlas) const;

private:
    int item_at(int x, int y) const;

    std::vector<MenuItem> items_;
    std::vector<int> item_tops_; // ordonnee du haut de chaque entree, relative au menu

    bool open_ = false;
    int x_ = 0, y_ = 0, width_ = 0, height_ = 0;
    int hovered_ = -1;
    int chosen_id_ = 0;
};

} // namespace gw::app
