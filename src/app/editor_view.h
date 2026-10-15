// app/editor_view.h — vue de l editeur : retour a la ligne, defilement, dessin.
//
// C est l autre moitie du moteur d edition. `doc::TextEdit` raisonne en
// caracteres ; ce fichier raisonne en *lignes affichees*, ce qui n a de sens
// qu avec une largeur de colonne et des metriques de police. La frontiere est
// expliquee dans doc/text_edit.h.
//
// Consequence pratique : tout ce qui depend de l apparence est ici et nulle
// part ailleurs. Un deplacement vers le haut, un clic, une page vers le bas
// sont des questions de vue — elles se resolvent en une `doc::Position` que la
// vue pose ensuite dans TextEdit.
//
// LA MISE EN FORME EST INTERPRETEE ICI, PAS STOCKEE ICI. Le modele dit « ce
// paragraphe est un H2 », « cette plage est en gras » ; c est ce fichier qui
// decide qu un H2 se dessine a 1,5 fois le corps en semi-gras. Le rapport entre
// un niveau de titre et une taille en pixels est une decision typographique,
// donc une decision de vue : la changer ne doit pas toucher au format de
// fichier ni au moteur d edition.
//
// LIGNES DE HAUTEUR VARIABLE. Depuis les titres, deux lignes voisines n ont
// plus la meme hauteur. Chaque VisualLine porte donc la sienne, et l ordonnee
// d une ligne se calcule en cumulant les hauteurs depuis la premiere ligne
// affichee — au lieu d une simple multiplication. Le defilement reste compte
// en LIGNES et non en pixels : c est ce qui garde la molette previsible quand
// on traverse un titre.
//
// RECALCUL COMPLET A CHAQUE MODIFICATION. Le decoupage en lignes est refait
// integralement des que le texte ou la largeur changent, sans cache par
// paragraphe. Mesure : sur le plus gros document a portee de main (33 Kio, 206
// paragraphes), un passage complet reste sous la milliseconde, et une frappe
// entraine de toute facon une remesure pour le dessin. Un cache incremental
// serait la premiere optimisation a faire si un document dix fois plus gros
// devenait courant — pas avant, parce qu il ajoute une invalidation a tenir
// juste.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "app/fonts.h"
#include "doc/text_edit.h"
#include "render/bitmap.h"

namespace gw::app {

struct VisualLine;

// Une cellule d une rangee de tableau : ses lignes de texte, empilees a partir
// de la marge haute de la cellule. `x` est relatif au debut du contenu.
struct TableCell {
    std::size_t paragraph = 0;
    int x = 0;
    int width = 0;
    int padding = 0;
    std::vector<VisualLine> lines;
};

// Une ligne telle qu elle est affichee : une tranche d octets d un paragraphe,
// plus tout ce que la mise en forme lui impose.
//
// UNE RANGEE DE TABLEAU EST UNE SEULE LIGNE. Ses cellules, côte à côte, portent
// chacune leur propre texte : `cells` est alors non vide et begin/end ne
// servent pas. Ainsi la somme des hauteurs, le defilement et le rail restent
// ceux d un texte sans tableau.
struct VisualLine {
    std::size_t paragraph = 0;
    std::size_t begin = 0; // premier octet affiche
    std::size_t end = 0;   // premier octet non affiche
    bool continues = false; // vrai si cette ligne poursuit un paragraphe coupe

    int height = 0;    // hauteur de la ligne, dictee par le niveau de titre
    int ascent = 0;    // du haut de la ligne a la ligne de base
    int indent = 0;    // retrait gauche (paragraphes de liste)
    int offset_x = 0;  // decalage d alignement, nul a gauche
    // Air reservee au-dessus du texte, sur la premiere ligne d un titre. Elle
    // est comprise dans `height` et dans `ascent` : ce champ existe pour que le
    // curseur et le fond de selection sachent ou commence le texte reel.
    int space_above = 0;

    // Puce ou numero, uniquement sur la PREMIERE ligne d un paragraphe de
    // liste. Les lignes de continuation sont retraitees sans marqueur, comme
    // dans tout traitement de texte.
    std::string marker;

    // Non vide : cette ligne est une rangee de tableau.
    std::vector<TableCell> cells;
};

class EditorView {
public:
    // Recalcule geometrie et decoupage. A appeler apres toute modification du
    // texte, tout redimensionnement et tout changement de corps.
    void layout(const doc::TextEdit& edit, int width, int height, int top_offset, int inset,
                FontSet& fonts, float body_size);

    void draw(render::Framebuffer& fb, const doc::TextEdit& edit, FontSet& fonts, float body_size,
              bool caret_visible) const;

    // --- Correspondance point <-> position ---
    doc::Position position_at_point(const doc::TextEdit& edit, int x, int y, FontSet& fonts,
                                    float body_size) const;
    bool contains_point(int x, int y) const;

    // --- Deplacements qui n existent que dans la vue ---
    //
    // `desired_x` est l abscisse visee, memorisee entre deux deplacements
    // verticaux. Sans elle, descendre a travers une ligne courte ramenerait le
    // curseur en fin de ligne courte, puis toutes les suivantes s aligneraient
    // dessus : le curseur « fondrait » vers la gauche.
    doc::Position position_above(const doc::TextEdit& edit, FontSet& fonts, float body_size) const;
    doc::Position position_below(const doc::TextEdit& edit, FontSet& fonts, float body_size) const;
    doc::Position position_page(const doc::TextEdit& edit, FontSet& fonts, float body_size,
                                int direction) const;
    doc::Position line_start(const doc::TextEdit& edit) const;
    doc::Position line_end(const doc::TextEdit& edit) const;

    // Fige l abscisse visee sur la position courante du curseur. A appeler
    // avant une serie de deplacements verticaux, et apres tout deplacement
    // horizontal ou toute frappe.
    void invalidate_desired_x() { desired_x_ = -1; }

    // --- Defilement ---
    // `delta` en crans WHEEL_DELTA. Retourne vrai si la vue a bouge.
    bool scroll_by(int delta);
    // Ramene le curseur dans la zone visible, sans bouger si c est deja le cas.
    void ensure_caret_visible(const doc::TextEdit& edit);
    void scroll_to_top() { first_line_ = 0; }

    int visible_line_count() const { return visible_lines_; }
    std::size_t line_count() const { return lines_.size(); }

    // --- Animation fluide du curseur (façon Word) ---
    void update_caret_target(const doc::TextEdit& edit, FontSet& fonts, float body_size,
                             bool smooth_enabled);
    bool step_animation(float dt);
    void reset_caret_animation();
    bool is_animating() const { return is_animating_; }
    float anim_caret_x() const { return anim_caret_x_; }
    float anim_caret_y() const { return anim_caret_y_; }
    float anim_caret_h() const { return anim_caret_h_; }

    // --- Typographie de la mise en forme ---------------------------------
    //
    // Exposees parce que la barre d outils dessine ses libelles de titre a la
    // meme echelle relative que le texte : la hierarchie doit etre la meme
    // dans la barre et dans la page, et deux tables de coefficients finiraient
    // toujours par diverger.
    static float heading_scale(doc::HeadingLevel level);
    // Style de caractere REELLEMENT applique a une plage : c est le style du
    // modele, plus ce que le niveau de titre impose (un titre est semi-gras
    // meme si aucune plage ne porte le gras).
    static doc::CharStyle effective_style(const doc::Paragraph& p, doc::CharStyle style);

private:
    // Cellule et ligne de texte d une position, a l interieur d une rangee.
    struct CellSpot {
        std::size_t cell = 0;
        std::size_t line = 0;
    };
    CellSpot spot_in_row(const VisualLine& row, doc::Position position) const;
    doc::Position position_in_cell(const doc::TextEdit& edit, const TableCell& cell,
                                   const VisualLine& line, int x, FontSet& fonts,
                                   float body_size) const;
    // Boite du curseur : abscisse, ordonnee du haut et hauteur, en pixels.
    void caret_box(const doc::TextEdit& edit, doc::Position position, FontSet& fonts,
                   float body_size, int& x, int& top, int& height) const;
    // Pose toutes les rangees d un tableau, de `first` a `end` exclu.
    void layout_table(const doc::TextEdit& edit, std::size_t first, std::size_t end,
                      FontSet& fonts, float body_size);

    std::size_t line_index_for(doc::Position position) const;
    int caret_x_for(const doc::TextEdit& edit, doc::Position position, FontSet& fonts,
                    float body_size) const;
    // Abscisse visee courante, calculee depuis le curseur si elle n est pas
    // encore figee.
    int resolve_desired_x(const doc::TextEdit& edit, FontSet& fonts, float body_size) const;
    doc::Position position_on_line(const doc::TextEdit& edit, std::size_t line, int x,
                                   FontSet& fonts, float body_size) const;

    // Ordonnee du haut d une ligne visible, obtenue en cumulant les hauteurs
    // depuis `first_line_`. Rend content_y_ + content_height_ (donc hors
    // champ) pour une ligne qui n est pas affichee.
    int line_top(std::size_t index) const;

    std::vector<VisualLine> lines_;
    std::vector<std::size_t> paragraph_first_line_;

    // Geometrie, en pixels, recalculee par layout().
    int content_x_ = 0;
    int content_y_ = 0;
    int content_width_ = 0;
    int content_height_ = 0;
    // Hauteur d une ligne de CORPS. Les lignes de titre sont plus hautes ; ce
    // champ sert au dimensionnement approximatif (nombre de lignes visibles,
    // saut de page) et au rail de defilement, pas au placement.
    int line_height_ = 0;
    int ascent_ = 0;
    int visible_lines_ = 0;

    std::size_t first_line_ = 0; // premiere ligne affichee, en index de ligne
    mutable int desired_x_ = -1; // -1 = a recalculer depuis le curseur

    struct ParagraphCache {
        std::uint32_t version = 0;
        int wrap_width = 0;
        float body_size = 0.0f;
        int space_above = 0;
        std::vector<VisualLine> lines;
    };
    std::vector<ParagraphCache> paragraph_cache_;

    float anim_caret_x_ = -1.0f;
    float anim_caret_y_ = -1.0f;
    float anim_caret_h_ = 0.0f;
    float target_caret_x_ = 0.0f;
    float target_caret_y_ = 0.0f;
    float target_caret_h_ = 0.0f;
    bool is_animating_ = false;
};

} // namespace gw::app
