// doc/style.h — mise en forme du modele de document.
//
// DEUX GRANULARITES, DEUX REPRESENTATIONS.
//
// Le style de PARAGRAPHE (niveau de titre, type de liste, alignement) porte
// sur le paragraphe entier : trois champs dans Paragraph, rien de plus.
//
// Le style de CARACTERE (gras, italique, souligne, police) porte sur des
// plages. Il est represente par une suite de segments consecutifs — un
// codage par longueurs, pas par bornes absolues :
//
//     texte  : "un mot gras ici"
//     spans  : [ {3, {}}, {8, {bold}}, {4, {}} ]
//                ^ "un "  ^ "mot gras" ^ " ici"
//
// POURQUOI DES LONGUEURS ET NON DES BORNES [debut, fin). Toute mutation de
// texte deplace tout ce qui suit. Avec des bornes absolues, inserer un
// caractere au debut du paragraphe obligerait a decaler chaque borne de chaque
// plage — un parcours complet, et surtout une occasion d oublier une borne.
// Avec des longueurs, seule la plage qui contient le point d insertion change,
// et les suivantes sont correctes sans y toucher. C est la meme raison qui
// fait qu un editeur de texte stocke des « runs » et non des offsets.
//
// INVARIANTS, tenus par style_normalize() apres chaque operation :
//   - somme des longueurs == taille du texte du paragraphe ;
//   - aucune plage de longueur nulle ;
//   - jamais deux plages consecutives de style identique ;
//   - un paragraphe entierement au style par defaut a une liste VIDE.
//     C est la forme canonique : elle rend un document sans mise en forme
//     exactement aussi leger qu avant l ajout de ce fichier, et elle rend la
//     comparaison « ce paragraphe a-t-il un style ? » triviale.
//
// Les plages ne sont PAS alignees sur les frontieres UTF-8 par construction :
// elles le sont parce que toutes les positions qui arrivent ici viennent de
// doc::TextEdit, qui ne produit que des frontieres valides. Une plage coupant
// une sequence multi-octets n aurait pas de sens visuel mais ne casserait rien
// — le rendu redecoupe de toute facon par point de code.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace gw::doc {

// --- Styles de paragraphe ---------------------------------------------------

enum class HeadingLevel : std::uint8_t {
    None = 0,
    H1 = 1,
    H2 = 2,
    H3 = 3,
};

enum class ListKind : std::uint8_t {
    None = 0,
    Bullet,   // puce
    Numbered, // numerotee
};

enum class Alignment : std::uint8_t {
    Left = 0,
    Center,
    Right,
};

// --- Style de caractere -----------------------------------------------------

// Les trois attributs se combinent librement : ce sont des booleens
// independants et non un enum de variantes, parce que « gras italique
// souligne » est un etat legitime.
//
// LA TAILLE DE POLICE N EST PAS ICI, volontairement. Elle reste globale au
// document : c est le corps par defaut des reglages, majore par le niveau de
// titre du paragraphe. Une taille par plage demanderait de porter une unite
// (points ? pixels ? relatif ?) et de la faire survivre a un changement du
// corps par defaut — trois decisions de format pour une granularite dont ce
// chantier n a pas l usage. Le jour ou elle arrive, elle s ajoute ici comme un
// champ de plus, et le codage par longueurs n a pas a bouger.
struct CharStyle {
    bool bold = false;
    bool italic = false;
    bool underline = false;

    // Nom d affichage de la police, tel qu il apparait dans les reglages
    // (« Inter », « Fira Sans »). Vide = police par defaut du document.
    //
    // C est un NOM et non un chemin : un document doit rester lisible si le
    // fichier de police a bouge, et le nom est ce que l utilisateur a choisi.
    // Une police absente retombe sur la police par defaut au rendu, sans que
    // le document perde l information.
    std::string font;

    bool is_default() const {
        return !bold && !italic && !underline && font.empty();
    }
};

bool operator==(const CharStyle& a, const CharStyle& b);
bool operator!=(const CharStyle& a, const CharStyle& b);

// Un segment consecutif de `length` octets partageant le meme style.
struct StyleSpan {
    std::size_t length = 0;
    CharStyle style;
};

bool operator==(const StyleSpan& a, const StyleSpan& b);

// --- Operations sur une liste de plages -------------------------------------
//
// `text_length` est toujours la taille du texte que les plages decrivent
// AVANT l operation. Les appelants de doc/text_edit passent par les fonctions
// de paragraphe plus bas, qui mutent texte et plages ensemble : c est la seule
// facon fiable de ne pas les desynchroniser.

// Retablit les invariants ci-dessus pour un texte de `text_length` octets.
// Une liste trop courte est completee par du style par defaut, une liste trop
// longue est tronquee : une liste heritee d un fichier douteux est ainsi
// toujours ramenee a quelque chose de coherent, jamais rejetee.
void style_normalize(std::vector<StyleSpan>& spans, std::size_t text_length);

// Style du caractere qui COMMENCE a `offset`. Style par defaut si `offset`
// tombe au-dela du texte decrit.
CharStyle style_at(const std::vector<StyleSpan>& spans, std::size_t offset);

// Style du caractere qui PRECEDE `offset` — celui dont herite une frappe a
// cette position. A l offset 0, c est celui qui suit : au tout debut d un
// paragraphe en gras, taper doit donner du gras.
CharStyle style_before(const std::vector<StyleSpan>& spans, std::size_t offset);

// Vrai si toute la plage [begin, end) partage le meme style au sens du
// predicat. Sert aux bascules : un bouton Gras n eteint le gras que si la
// selection est deja entierement grasse.
bool style_range_all(const std::vector<StyleSpan>& spans, std::size_t text_length,
                     std::size_t begin, std::size_t end,
                     const std::function<bool(const CharStyle&)>& predicate);

// Applique `mutate` a chaque plage intersectant [begin, end), en decoupant les
// plages aux bornes. Sans effet si la plage est vide.
void style_apply(std::vector<StyleSpan>& spans, std::size_t text_length, std::size_t begin,
                 std::size_t end, const std::function<void(CharStyle&)>& mutate);

} // namespace gw::doc
