// doc/text_edit.h — coeur du moteur d edition : curseur, selection, mutations.
//
// MODULE PUR. Il ne connait ni Win32, ni pixels, ni police : uniquement des
// paragraphes de texte UTF-8, un curseur et une ancre. C est ce qui permet de
// le verifier entierement hors interface (`gw_edit_tests`), la ou un moteur
// d edition classique n est testable qu en simulant des frappes.
//
// LA FRONTIERE, ET POURQUOI ELLE EST LA : tout ce qui se raisonne en
// *caracteres* est ici. Tout ce qui se raisonne en *lignes affichees* est dans
// app/editor_view — parce qu une ligne affichee n existe pas sans une largeur
// de colonne et des metriques de police. Deplacer le curseur d une ligne vers
// le haut n est donc pas une operation de ce fichier ; la vue calcule la
// position visee et la pose ici par set_caret().
//
// POSITIONS EN OCTETS, PAS EN CARACTERES. Le texte est de l UTF-8 et les
// offsets sont des index d octets, toujours poses sur une frontiere de
// caractere. L alternative — compter en points de code — imposerait un
// parcours depuis le debut du paragraphe a chaque acces, pour un confort
// d ecriture qui ne concerne que ce fichier. Les fonctions de deplacement sont
// les seules a franchir des frontieres, et elles sont testees pour ca.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <functional>
#include <memory>

#include "doc/document.h"

namespace gw::doc {

// Position dans le document : quel paragraphe, et a quel octet dedans.
struct Position {
    std::size_t paragraph = 0;
    std::size_t offset = 0;
};

bool operator==(const Position& a, const Position& b);
bool operator!=(const Position& a, const Position& b);
bool operator<(const Position& a, const Position& b);

// Nature de la derniere mutation, pour le regroupement d annulations.
//
// Sans regroupement, annuler apres avoir tape une phrase demanderait autant de
// Ctrl+Z que de caracteres, ce que personne n attend d un editeur.
enum class EditKind {
    None,
    Typing,   // insertion de texte au fil de la frappe
    Deleting, // effacement caractere par caractere
    Other,    // tout le reste : collage, retour a la ligne, suppression de selection
};

class TextEdit {
public:
    TextEdit();

    // --- Document ---
    const Document& document() const { return document_; }
    Document& document() { return document_; }

    // Remplace le document (ouverture d un fichier, nouvelle note). Remet le
    // curseur au debut et vide l historique : les annulations d un document ne
    // veulent rien dire dans un autre.
    void reset(Document document);

    // --- Curseur et selection ---
    //
    // L ancre est l extremite fixe de la selection, le curseur l extremite
    // mobile. Quand les deux coincident, il n y a pas de selection.
    const Position& caret() const { return caret_; }
    const Position& anchor() const { return anchor_; }
    bool has_selection() const { return caret_ != anchor_; }

    // Bornes ordonnees de la selection. Egales s il n y en a pas.
    Position selection_begin() const;
    Position selection_end() const;
    std::string selected_text() const;

    // Pose le curseur. `extend` conserve l ancre (Maj + deplacement) ; sinon
    // l ancre suit, donc la selection disparait.
    void set_caret(Position position, bool extend);
    void select_all();
    // Selection du mot sous la position, pour le double-clic. Sur un espace,
    // selectionne la plage d espaces : c est le comportement des editeurs
    // courants et il evite un double-clic sans effet.
    void select_word_at(Position position);

    // --- Deplacements horizontaux ---
    //
    // Rendent la position visee sans la poser, pour que la vue puisse les
    // composer. Les versions `move_*` posent le curseur.
    Position position_left(Position from, bool by_word) const;
    Position position_right(Position from, bool by_word) const;
    void move_left(bool extend, bool by_word);
    void move_right(bool extend, bool by_word);

    // Debut / fin de paragraphe. La vue passe par set_caret() quand elle veut
    // un debut / fin de *ligne affichee*, qui n est pas la meme chose.
    void move_paragraph_start(bool extend);
    void move_paragraph_end(bool extend);
    void move_document_start(bool extend);
    void move_document_end(bool extend);

    // --- Mutations ---
    //
    // Toutes suppriment d abord la selection s il y en a une, comme partout
    // ailleurs : taper avec du texte selectionne le remplace.
    void insert_text(const std::string& utf8);
    // Insere des paragraphes deja construits (collage Markdown). Le premier
    // commence a la ligne du curseur, le texte apres le curseur est recolle au
    // dernier : c est le comportement d une frappe qui ferait une fin de ligne.
    // Les attributs de paragraphe de `blocks` sont conserves ; le texte qui
    // suivait le curseur prend ceux du dernier bloc.
    void insert_blocks(std::vector<Paragraph> blocks);
    void insert_paragraph_break();

    // --- Tableaux ----------------------------------------------------------
    //
    // Insere un tableau vide de `rows` x `cols` apres le curseur. Si le curseur
    // est dans un paragraphe ordinaire, celui-ci est coupe en deux : la queue
    // passe apres le tableau. Si le curseur est deja dans un tableau, le
    // nouveau tableau se place apres celui-ci, jamais a l interieur. Le curseur
    // se pose dans la premiere cellule.
    void insert_table(std::size_t rows, std::size_t cols);
    // Deplace le curseur a la cellule suivante (`backward` : precedente). Dans la
    // derniere cellule, ajoute une ligne vide en fin de tableau, comme Tab dans
    // un traitement de texte. Retourne faux si le curseur n est pas dans un
    // tableau ou si la cellule precedente n existe pas.
    bool move_cell(bool backward);
    // Supprime le tableau qui contient le curseur. Si le document n a plus rien,
    // il reste un paragraphe vide.
    bool delete_table();
    // Vrai si le curseur est dans une cellule de tableau.
    bool caret_in_table() const;

    void erase_backward(bool by_word);
    void erase_forward(bool by_word);
    // Retourne faux s il n y avait rien a supprimer.
    bool delete_selection();

    // --- Mise en forme -------------------------------------------------
    //
    // POURQUOI ICI ET PAS DANS LA BARRE D OUTILS : la mise en forme est une
    // mutation du document au meme titre qu une frappe. Elle doit donc passer
    // par le meme historique, sous peine qu un Ctrl+Z apres un clic sur Gras
    // saute par-dessus le changement de style et remonte a la mutation
    // precedente. Le bouton n a aucune logique propre : il appelle ces
    // fonctions, exactement comme une touche du clavier.

    // Style de la frappe a venir : le style « en attente » s il a ete arme par
    // une bascule sans selection, sinon celui du caractere qui precede le
    // curseur. C est ce que la barre d outils affiche comme etat actif.
    CharStyle current_char_style() const;

    // Bascules. Avec une selection : la plage est modifiee immediatement, et
    // l attribut est ETEINT si toute la selection le porte deja, allume sinon
    // (regle habituelle : un bouton bascule ce qu il montre). Sans selection :
    // le style est arme pour la frappe suivante et rien n est modifie — donc
    // rien n est empile dans l historique, parce qu il n y a rien a annuler.
    void toggle_bold();
    void toggle_italic();
    void toggle_underline();
    // Nom d affichage de la police ; vide = police par defaut du document.
    // Sans selection, devient la police par defaut du document ET le style de
    // frappe : c est ce que fait un selecteur de police curseur pose dans un
    // document vide.
    void set_font(const std::string& name);

    // --- Mise en forme de paragraphe ---
    //
    // S applique a TOUS les paragraphes touches par la selection, pas au seul
    // paragraphe du curseur : selectionner trois lignes et cliquer sur « puce »
    // doit donner trois puces.
    void set_heading(HeadingLevel level);
    void set_list(ListKind kind);
    void set_alignment(Alignment align);

    // Valeur affichee par la barre d outils. Sur une selection couvrant
    // plusieurs paragraphes de valeurs differentes, rend celle du premier :
    // un bouton doit montrer quelque chose, et le premier paragraphe est celui
    // ou l utilisateur a commence sa selection dans le cas courant.
    HeadingLevel current_heading() const;
    ListKind current_list() const;
    Alignment current_alignment() const;

    // --- Mode clavier-first : conversion Markdown au fil de la frappe ---
    //
    // A appeler apres chaque caractere insere, quand le mode est actif. Si le
    // caractere qui vient d etre tape ferme une syntaxe reconnue par
    // doc/markdown_live.h, les marqueurs sont retires et le formatage
    // correspondant est pose. Retourne vrai si le document a change.
    //
    // POURQUOI ICI ET PAS DANS App : exactement la meme raison que les
    // bascules de la barre d outils. Une conversion est une mutation du
    // document, elle doit entrer dans l historique — sinon Ctrl+Z juste apres
    // une conversion sauterait par-dessus et remonterait avant la frappe du
    // mot. Elle empile UNE entree : le premier Ctrl+Z rend les marqueurs
    // visibles, le second annule la frappe.
    bool apply_markdown_live();

    // --- Historique ---
    bool can_undo() const { return !undo_.empty(); }
    bool can_redo() const { return !redo_.empty(); }
    bool undo();
    bool redo();

    // --- Bornage ---
    // Ramene une position dans les limites du document et sur une frontiere de
    // caractere. La vue s en sert : une position deduite d un clic peut viser
    // au-dela de la derniere ligne.
    Position clamp(Position position) const;

    // Nombre d entrees d annulation conservees. Au-dela, les plus anciennes
    // sont oubliees : l historique est un confort, pas une garantie, et un
    // document tenu ouvert des heures ne doit pas grossir sans borne.
    static constexpr std::size_t kMaxUndoDepth = 256;

private:
    // Vrai si aller a `target` franchirait la frontiere d une cellule. Sert aux
    // suppressions, qui ne doivent jamais fusionner une cellule avec sa voisine.
    bool crosses_table(Position target) const;

    struct Snapshot {
        std::vector<std::shared_ptr<const Paragraph>> paragraphs;
        Position caret;
        Position anchor;
        // Le style en attente fait partie de l etat d edition : annuler une
        // frappe grasse doit rendre le curseur a l etat « gras arme », sinon
        // la frappe suivante repartirait en maigre sans que rien ne l explique.
        std::optional<CharStyle> pending_style;
        std::string default_font;
    };

    Snapshot capture() const;
    void restore(const Snapshot& snapshot);
    // Enregistre l etat courant avant une mutation, en fusionnant avec
    // l entree precedente quand la frappe est continue.
    void push_undo(EditKind kind);

    const std::string& text_at(std::size_t paragraph) const;

    // Applique `mutate` a la plage selectionnee, ou arme le style en attente
    // s il n y a pas de selection. Facteur commun des trois bascules.
    void mutate_char_style(const std::function<void(CharStyle&)>& mutate,
                           const std::function<bool(const CharStyle&)>& already_set);
    // Indices des paragraphes touches par la selection (au moins celui du
    // curseur).
    void for_each_selected_paragraph(const std::function<void(Paragraph&)>& fn);

    Document document_;
    Position caret_;
    Position anchor_;

    // Style arme par une bascule sans selection, consomme par la frappe
    // suivante. Absent = la frappe herite du texte environnant.
    std::optional<CharStyle> pending_style_;

    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    mutable std::vector<std::shared_ptr<const Paragraph>> paragraph_snapshots_;
    EditKind last_kind_ = EditKind::None;
    Position last_edit_end_; // fin de la derniere mutation, pour la continuite
};

// --- Utilitaires UTF-8, exposes parce que la vue en a besoin aussi ---------

// Recule / avance d un caractere complet depuis `offset`. Une position deja
// posee sur une frontiere invalide est ramenee sur la frontiere la plus proche
// plutot que de faire boucler l appelant.
std::size_t previous_char_boundary(const std::string& text, std::size_t offset);
std::size_t next_char_boundary(const std::string& text, std::size_t offset);

} // namespace gw::doc
