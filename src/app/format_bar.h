// app/format_bar.h — barre de mise en forme de l editeur.
//
// Meme nature que TitleBar (app/chrome.h) : ce n est PAS une fenetre, ni une
// barre d outils systeme. C est une bande dessinee dans le meme framebuffer que
// le reste, et une poignee de rectangles qui servent au hit-test. Aucun HWND
// enfant, donc aucun controle natif a themer, et la barre se presente par le
// meme backend (OpenGL ou GDI) que le contenu.
//
// ELLE NE PORTE AUCUNE LOGIQUE D EDITION. Un clic rend une FormatAction ; c est
// App qui la traduit en appel a doc::TextEdit, exactement comme il traduit une
// touche du clavier ou une entree du menu contextuel. C est la condition pour
// que les trois chemins ne divergent jamais — et c est aussi ce qui fait que la
// mise en forme depuis la barre passe par l historique d annulation sans effort
// particulier, puisqu elle passe par les memes fonctions.
//
// ETAT AFFICHE, PAS ETAT DETENU. La barre ne se souvient de rien : elle recoit
// a chaque dessin l etat reel du curseur (FormatState) et s y conforme. Sans
// cela il faudrait la resynchroniser apres chaque frappe, chaque clic, chaque
// annulation — et l oublier une seule fois donnerait un bouton qui ment.
//
// DISPOSITION, de gauche a droite, groupes separes par un filet :
//   B I U        attributs de caractere (bascules)
//   H1 H2 H3     niveaux de titre (bascules : recliquer retire le titre)
//   • 1.         listes (bascules)
//   gauche centre droite   alignement (exclusifs)
//   [ Police ]   selecteur, ouvre un menu deroulant
#pragma once

#include <string>
#include <vector>

#include "app/fonts.h"
#include "doc/style.h"
#include "render/bitmap.h"
#include "render/rasterizer.h"

namespace gw::app {

enum class FormatAction {
    None,
    Bold,
    Italic,
    Underline,
    Heading1,
    Heading2,
    Heading3,
    ListBullet,
    ListNumbered,
    AlignLeft,
    AlignCenter,
    AlignRight,
    FontPicker, // ouvre la liste deroulante des polices
};

// Ce que la barre doit refleter. Rempli par App depuis doc::TextEdit a chaque
// dessin.
struct FormatState {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    doc::HeadingLevel heading = doc::HeadingLevel::None;
    doc::ListKind list = doc::ListKind::None;
    doc::Alignment align = doc::Alignment::Left;
    std::string font_name; // libelle affiche dans le selecteur
};

class FormatBar {
public:
    // Rasterise les icones d alignement une fois pour toutes. A rappeler
    // seulement si le facteur d echelle change.
    void build_icons(float scale);

    // Recalcule les rectangles. `top` est le bas de la barre de titre.
    // `fonts` sert a mesurer le libelle du selecteur de police : sa largeur
    // depend du nom affiche, et le hit-test en a besoin avant tout dessin.
    void layout(int width, int top, int inset, const std::string& font_name, FontSet& fonts);

    int height() const;
    // Ordonnee du premier pixel sous la barre : c est de la que commence la
    // zone de texte.
    int bottom() const { return top_ + height(); }

    bool contains_point(int x, int y) const;

    // Survol. Retourne vrai si l affichage doit changer.
    bool set_cursor(int x, int y);
    void clear_cursor();

    FormatAction action_at(int x, int y) const;

    // Coin inferieur gauche du selecteur de police : le menu deroulant s y
    // ancre, pour qu il sorte sous le bouton et non sous le curseur.
    void font_picker_anchor(int& x, int& y) const;

    void draw(render::Framebuffer& fb, FontSet& fonts, const FormatState& state) const;

private:
    struct Button {
        int x = 0, y = 0, w = 0, h = 0;
        FormatAction action = FormatAction::None;
        // Libelle dessine avec une vraie police, dans la variante que le bouton
        // applique. Vide pour les boutons a icone.
        std::string label;
        bool label_bold = false;
        bool label_italic = false;
        bool label_underline = false;
        float label_scale = 1.0f; // relatif au corps de l interface

        bool contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    // Vrai si ce bouton doit apparaitre enfonce pour l etat donne.
    static bool is_active(FormatAction action, const FormatState& state);
    const render::RasterizedGlyph* icon_for(FormatAction action) const;

    std::vector<Button> buttons_;
    std::vector<int> separators_; // abscisses des filets de groupe

    int top_ = 0;
    int inset_ = 0;
    int width_ = 0;

    render::RasterizedGlyph icon_align_left_;
    render::RasterizedGlyph icon_align_center_;
    render::RasterizedGlyph icon_align_right_;

    FormatAction hovered_ = FormatAction::None;
};

} // namespace gw::app
