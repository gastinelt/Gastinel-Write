// app/menu_bar.h — barre de menu principale (Fichier / Édition / Affichage).
//
// Meme nature que TitleBar (app/chrome.h) et FormatBar (app/format_bar.h) : ce
// n est PAS une fenetre, ni un menu systeme. C est une bande dessinee dans le
// meme framebuffer que le reste, et trois rectangles qui servent au hit-test.
// Aucun HWND enfant, donc aucun menu natif a themer, et la barre se presente
// par le meme backend (OpenGL ou GDI) que le contenu.
//
// ELLE NE CONTIENT AUCUNE ENTREE. C est volontaire, et c est la difference avec
// une barre de menu classique : cette classe ne connait que les TITRES. Le
// contenu deroulant est un ContextMenu, exactement celui du clic droit et du
// selecteur de police — un menu deroulant complet (clavier, survol, fermeture
// au clic exterieur, recadrage aux bords de la fenetre) existe deja dans le
// programme, et en ecrire un second n apporterait qu une seconde serie de bugs.
// Cette classe ne fait donc que dire QUEL menu a ete clique et OU l ancrer.
//
// ELLE NE PORTE AUCUNE LOGIQUE D EDITION non plus. App construit les entrees,
// App traduit l identifiant choisi — et il le traduit par un appel aux memes
// fonctions que le raccourci clavier correspondant. Aucune entree de menu n a
// de logique propre : c est la condition pour que les chemins ne divergent
// jamais, et c est deja la regle que suivent la barre de mise en forme et le
// menu contextuel.
//
// DISCRETION AU REPOS : trois mots gris sur le fond de page, aucun filet,
// aucune surface. La bande ne se materialise que sous le curseur ou quand un
// deroulant est ouvert. C est ce qui la rend compatible avec le mode
// clavier-first, ou tout le reste de la barre d outils a disparu.
#pragma once

#include <string>
#include <vector>

#include "app/fonts.h"
#include "render/bitmap.h"

namespace gw::app {

enum class MenuBarMenu {
    None,
    File,
    Edit,
    View,
};

class MenuBar {
public:
    // Recalcule les rectangles. `top` est le bas de la barre de titre.
    // `fonts` sert a mesurer les titres : leur largeur depend du libelle, et le
    // hit-test en a besoin avant tout dessin.
    void layout(int width, int top, int inset, FontSet& fonts);

    int height() const;
    // Ordonnee du premier pixel sous la bande.
    int bottom() const { return top_ + height(); }

    bool contains_point(int x, int y) const;

    // Survol. Retourne vrai si l affichage doit changer.
    bool set_cursor(int x, int y);
    void clear_cursor();

    MenuBarMenu menu_at(int x, int y) const;

    // Coin inferieur gauche du titre : le deroulant s y ancre, pour qu il sorte
    // sous son titre et non sous le curseur.
    void anchor_for(MenuBarMenu menu, int& x, int& y) const;

    // Titre dont le deroulant est ouvert, dessine en surface pleine. C est un
    // ETAT AFFICHE et non detenu : App le pose et l efface en meme temps qu il
    // ouvre et ferme le ContextMenu, donc la barre ne peut pas montrer un menu
    // ouvert qui ne l est plus.
    void set_open(MenuBarMenu menu) { open_ = menu; }
    MenuBarMenu open() const { return open_; }

    void draw(render::Framebuffer& fb, FontSet& fonts) const;

private:
    struct Title {
        int x = 0, y = 0, w = 0, h = 0;
        MenuBarMenu menu = MenuBarMenu::None;
        std::string label;

        bool contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    std::vector<Title> titles_;

    int top_ = 0;
    int inset_ = 0;
    int width_ = 0;

    MenuBarMenu hovered_ = MenuBarMenu::None;
    MenuBarMenu open_ = MenuBarMenu::None;
};

} // namespace gw::app
