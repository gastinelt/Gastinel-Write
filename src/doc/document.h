// doc/document.h — modele de document en memoire.
//
// Une liste plate de paragraphes. Chaque paragraphe porte son texte UTF-8, sa
// mise en forme de paragraphe (titre, liste, alignement) et la mise en forme
// de caractere de ses plages (cf. doc/style.h pour le codage retenu et sa
// justification).
//
// LE MODELE RESTE PLAT : pas d arbre, pas de sections. Un tableau n est pas un
// niveau de plus : ce sont des paragraphes consecutifs, de meme `table_id`,
// qui se rangent en grille (cf. Paragraph, champs de tableau). Une image devra
// suivre le meme principe, ou un type de bloc a cote de Paragraph — jamais une
// hierarchie generale, qui coute une indirection a chaque parcours.
//
// LES MUTATIONS PASSENT PAR LES FONCTIONS DE CE FICHIER, jamais par un acces
// direct a `text`. Toucher au texte sans toucher aux plages les desynchronise
// silencieusement : la faute ne se voit qu au rendu, plusieurs frappes plus
// tard. Les quatre fonctions ci-dessous sont les seules a muter un paragraphe,
// et chacune tient les deux moities a jour.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "doc/style.h"

namespace gw::doc {

// Un paragraphe : du texte encode en UTF-8, plus sa mise en forme.
struct Paragraph {
    // Les deux constructeurs existent pour que `Paragraph{}` et
    // `Paragraph{"texte"}` continuent de s ecrire comme avant l ajout des
    // champs de style : sans eux, l initialisation par agregat exigerait de
    // citer les quatre nouveaux membres a chaque site d appel, ou de subir un
    // avertissement -Wmissing-field-initializers a chacun.
    Paragraph() = default;
    explicit Paragraph(std::string content) : text(std::move(content)) {}

    Paragraph(const Paragraph& other);
    Paragraph& operator=(const Paragraph& other);
    Paragraph(Paragraph&&) noexcept = default;
    Paragraph& operator=(Paragraph&&) noexcept = default;

    mutable std::string text;

    // --- Mise en forme de paragraphe ---
    HeadingLevel heading = HeadingLevel::None;
    ListKind list = ListKind::None;
    Alignment align = Alignment::Left;

    // --- Mise en forme de caractere ---
    // Vide = tout le paragraphe au style par defaut. Cf. doc/style.h.
    std::vector<StyleSpan> spans;

    // --- Tableau ---
    // Une cellule de tableau est un Paragraph ordinaire. Les cellules d un
    // meme tableau sont consecutives, partagent un `table_id` non nul, et sont
    // rangees ligne par ligne (`table_row`, puis `table_col`). `table_cols` est
    // le nombre de colonnes, porte par chaque cellule pour qu une cellule
    // isolee suffise a reconstruire la grille. `table_id` = 0 : pas de tableau.
    //
    // Pourquoi des paragraphes et pas un bloc a part : chaque cellule garde
    // son texte, ses plages de style et son annulation sans aucun code
    // specifique. Ce qui change (interdire de fusionner deux cellules, Entree
    // = cellule suivante) est dans doc/text_edit.cpp, en un seul endroit.
    std::uint32_t table_id = 0;
    std::uint32_t table_row = 0;
    std::uint32_t table_col = 0;
    std::uint32_t table_cols = 0;

    bool in_table() const { return table_id != 0; }

    // Version incrementee a chaque mutation pour invalider les caches de vue.
    std::uint32_t version = 1;

    // --- Compression LZ memoire pour les paragraphes hors-champ ---
    mutable std::vector<std::uint8_t> compressed_text;
    mutable std::size_t uncompressed_size = 0;

    bool is_compressed() const { return !compressed_text.empty() && text.empty(); }
    void compress() const;
    void decompress() const;
    void ensure_decompressed() const {
        if (is_compressed()) {
            decompress();
        }
    }

    // Vrai si le paragraphe ne porte aucune mise en forme du tout. Sert a
    // l ecriture .odt : un tel paragraphe s ecrit exactement comme avant ce
    // chantier, donc un document sans style produit un fichier identique a
    // ceux des versions precedentes.
    bool is_plain() const {
        return heading == HeadingLevel::None && list == ListKind::None &&
               align == Alignment::Left && spans.empty() && table_id == 0;
    }

    // Attributs de paragraphe seuls, sans le texte ni les plages. Sert au
    // decoupage : les deux moities d un paragraphe coupe gardent son
    // alignement, son niveau de titre et son type de liste.
    void copy_paragraph_attributes_from(const Paragraph& other) {
        heading = other.heading;
        list = other.list;
        align = other.align;
        ++version;
    }
};

// --- Mutations coordonnees texte + plages ------------------------------------

// Insere `chunk` a `offset`.
//
// HERITAGE DU STYLE : si `forced` est nul, le texte insere prend le style du
// caractere qui precede le point d insertion (celui qui suit, a l offset 0).
// C est le comportement de tout traitement de texte : taper au milieu d un mot
// gras produit du gras. `forced` sert au cas inverse — l utilisateur vient
// d appuyer sur Gras sans selection, et la frappe suivante doit imposer ce
// style au lieu d heriter.
void paragraph_insert(Paragraph& p, std::size_t offset, const std::string& chunk,
                      const CharStyle* forced);

// Supprime `count` octets a partir de `offset`. Les plages traversees sont
// raccourcies ; une plage entierement couverte disparait. Rien d autre n est
// affecte : supprimer la totalite d un mot gras au milieu d une phrase laisse
// les deux moities normales adjacentes, qui fusionnent.
void paragraph_erase(Paragraph& p, std::size_t offset, std::size_t count);

// Coupe le paragraphe a `offset` : `p` garde la tete, le paragraphe rendu
// porte la queue. Le nouveau paragraphe herite des attributs de paragraphe de
// `p` (titre, liste, alignement) — couper une puce en deux donne deux puces.
Paragraph paragraph_split(Paragraph& p, std::size_t offset);

// Index du premier paragraphe du tableau auquel appartient `index`, et index
// juste apres son dernier paragraphe. Pour un paragraphe hors tableau, les deux
// valent `index` et `index + 1`.
std::size_t table_begin(const std::vector<Paragraph>& paragraphs, std::size_t index);
std::size_t table_end(const std::vector<Paragraph>& paragraphs, std::size_t index);

// Identifiant libre pour un nouveau tableau : strictement superieur a tous ceux
// deja presents, pour qu un tableau insere ne se confonde jamais avec un autre.
std::uint32_t next_table_id(const std::vector<Paragraph>& paragraphs);

// Concatene `tail` a la fin de `p`. Les attributs de paragraphe de `p` sont
// conserves et ceux de `tail` sont perdus : c est la regle de la fusion par
// retour arriere, ou le paragraphe survivant est celui du haut.
void paragraph_append(Paragraph& p, const Paragraph& tail);

class Document {
public:
    const std::vector<Paragraph>& paragraphs() const { return paragraphs_; }
    std::vector<Paragraph>& paragraphs() { return paragraphs_; }

    void clear() { paragraphs_.clear(); }

    // Vrai si le document a ete modifie depuis le dernier enregistrement.
    bool is_dirty() const { return dirty_; }
    void set_dirty(bool dirty) { dirty_ = dirty; }

    // Police par defaut du document : celle qu appliquent les plages dont
    // `CharStyle::font` est vide. Vide elle aussi = la police par defaut des
    // reglages. Elle n est pas persistee dans le fichier ; c est l etat de
    // frappe courant, pas une propriete du document.
    const std::string& default_font() const { return default_font_; }
    void set_default_font(std::string name) { default_font_ = std::move(name); }

private:
    std::vector<Paragraph> paragraphs_;
    bool dirty_ = false;
    std::string default_font_;
};

} // namespace gw::doc
