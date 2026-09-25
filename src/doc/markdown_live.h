// doc/markdown_live.h — reconnaissance de syntaxe Markdown au fil de la frappe.
//
// MODULE PUR, comme doc/text_edit.h et doc/style.h : il ne connait ni Win32, ni
// pixels, ni police. Il ne connait meme pas le curseur du document — juste un
// paragraphe et un offset. C est ce qui permet de le verifier entierement dans
// gw_style_tests, sans simuler une seule frappe reelle.
//
// CE QUE CE FICHIER N EST PAS : un parseur Markdown. Il ne lit jamais un
// document en entier, il ne produit jamais d arbre, et il ne sait rien du
// Markdown au-dela des cinq formes listees plus bas. Il repond a une seule
// question, appelee apres chaque caractere insere :
//
//     « le caractere qui vient d etre tape ferme-t-il une syntaxe ? »
//
// PAS DE FORMAT PARALLELE. Le resultat d une conversion est exactement ce que
// produirait la barre de mise en forme : les champs `heading` / `list` de
// doc::Paragraph et les plages de doc::style.h. Le mode clavier-first est une
// autre facon de PRODUIRE le meme modele, pas un second modele. C est pour cela
// que basculer d un mode a l autre ne perd rien : il n y a rien a convertir.
//
// LES MARQUEURS DISPARAISSENT. Une fois la syntaxe reconnue, les `**` ou le
// `# ` sont retires du texte et seul le formatage subsiste — « live preview »,
// pas « source visible ». Consequence assumee : la conversion est
// irreversible autrement que par Ctrl+Z, et le texte enregistre ne contient
// aucun marqueur. C est coherent avec le reste de l application, ou le fichier
// .odt porte du vrai style et non de la syntaxe.
//
// FORMES RECONNUES, et rien d autre :
//   **texte**  -> gras          (declenche par le second `*` fermant)
//   *texte*    -> italique
//   _texte_    -> italique
//   `# `, `## `, `### ` en tete de paragraphe -> H1 / H2 / H3
//   `- ` en tete de paragraphe               -> puce
//   `1. ` (n importe quel nombre) en tete    -> liste numerotee
//
// Pas de liens, pas de citations, pas de code inline : chacun demanderait soit
// un champ de modele qui n existe pas (URL, niveau de citation), soit une
// police a chasse fixe que l application ne fournit pas.
#pragma once

#include <cstddef>

#include "doc/document.h"

namespace gw::doc {

// Ce qu il faut faire au paragraphe. Rendu par markdown_detect(), consomme par
// markdown_apply(). Les deux etapes sont separees pour que la detection soit
// testable sans muter quoi que ce soit.
struct MarkdownRule {
    enum class Kind {
        None,       // rien a faire
        Emphasis,   // retirer deux marqueurs, habiller ce qu ils encadraient
        Paragraph,  // retirer un prefixe de tete, poser un attribut de paragraphe
    };

    Kind kind = Kind::None;

    // --- Kind::Emphasis ---
    // Bornes des deux marqueurs dans le texte AVANT retrait.
    std::size_t open_begin = 0;
    std::size_t open_length = 0;
    std::size_t close_begin = 0;
    std::size_t close_length = 0;
    // Attribut a allumer sur le contenu encadre. Un seul des deux a la fois :
    // `***texte***` n est pas une forme reconnue.
    bool bold = false;
    bool italic = false;

    // --- Kind::Paragraph ---
    std::size_t prefix_length = 0;
    HeadingLevel heading = HeadingLevel::None;
    ListKind list = ListKind::None;

    bool applies() const { return kind != Kind::None; }
};

// Examine le paragraphe autour de `caret` (offset en octets, cense etre juste
// apres le caractere qui vient d etre insere) et dit si une conversion se
// declenche. Ne mute rien.
//
// Rend Kind::None dans tous les cas douteux, et c est voulu : une syntaxe
// incomplete (`*mot` sans fermeture), un `_` interne a un mot
// (`nom_de_variable`), un contenu vide (`**` colle a `**`) ou borde d espaces
// (`** mot **`) ne declenchent rien. Le cout d un faux negatif est que
// l utilisateur tape le marqueur une fois de plus ; le cout d un faux positif
// est du texte qui disparait sous ses doigts.
MarkdownRule markdown_detect(const Paragraph& p, std::size_t caret);

// Applique la conversion : retire les marqueurs et pose le style. Rend le
// nouvel offset du curseur, decale du nombre d octets retires.
//
// N ecrit PAS `p.heading` / `p.list` pour une regle de paragraphe : ces deux
// champs passent par doc::TextEdit, qui doit aussi tenir l historique
// d annulation. Cette fonction ne touche qu au texte et aux plages, via les
// memes paragraph_erase() / style_apply() que le reste du programme.
std::size_t markdown_apply(Paragraph& p, const MarkdownRule& rule, std::size_t caret);

} // namespace gw::doc
