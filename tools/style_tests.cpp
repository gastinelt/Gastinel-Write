// tools/style_tests.cpp — verification du modele de document enrichi.
//
// Meme principe que gw_edit_tests : doc/style.cpp et doc/text_edit.cpp sont des
// modules purs (ni fenetre, ni police, ni pixel), donc tout ce qui suit se
// verifie sans simuler une frappe ni ouvrir une interface.
//
// Depuis le mode clavier-first, il couvre aussi doc/markdown_live.cpp, qui est
// pur pour les memes raisons et se teste donc au meme endroit : une « frappe »
// y est un appel a insert_text() suivi d apply_markdown_live(), exactement la
// sequence que App::on_char produit.
//
// Ce banc couvre exactement ce que le moteur d edition ne couvrait pas :
// l application d un style sur une plage, le chevauchement de deux styles, la
// survie du style a une insertion en milieu de plage, et la traversee d une
// frontiere de style par une suppression. Le retour a la ligne, le rendu des
// graisses et la barre d outils n en font pas partie : ils dependent de
// metriques de police et vivent dans app/.
//
// Usage : gw_style_tests
#include "doc/document.h"
#include "doc/markdown_live.h"
#include "doc/style.h"
#include "doc/text_edit.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
    std::printf("%s  %s\n", condition ? "[ OK ]" : "[FAIL]", what.c_str());
    if (!condition) ++failures;
}

void check_equal(const std::string& got, const std::string& expected, const std::string& what) {
    const bool ok = got == expected;
    std::printf("%s  %s\n", ok ? "[ OK ]" : "[FAIL]", what.c_str());
    if (!ok) {
        std::printf("       attendu : %s\n       obtenu  : %s\n", expected.c_str(), got.c_str());
        ++failures;
    }
}

using gw::doc::Alignment;
using gw::doc::CharStyle;
using gw::doc::Document;
using gw::doc::HeadingLevel;
using gw::doc::ListKind;
using gw::doc::Paragraph;
using gw::doc::Position;
using gw::doc::StyleSpan;
using gw::doc::TextEdit;

// Representation lisible d un paragraphe stylise : le texte decoupe plage par
// plage, chaque plage prefixee de ses attributs.
//
//   "un " + gras("mot") + " ici"   ->   [ ]un [b]mot[ ] ici
//
// Comparer des chaines plutot que des vecteurs de plages rend les echecs
// lisibles sans ecrire de dump : c est le meme parti pris que flatten() dans
// gw_edit_tests.
std::string describe(const Paragraph& p) {
    if (p.spans.empty()) return "[ ]" + p.text;

    std::string out;
    std::size_t offset = 0;
    for (const StyleSpan& s : p.spans) {
        std::string flags;
        flags += s.style.bold ? 'b' : ' ';
        flags += s.style.italic ? 'i' : ' ';
        flags += s.style.underline ? 'u' : ' ';
        if (!s.style.font.empty()) flags += "=" + s.style.font;
        out += "[" + flags + "]" + p.text.substr(offset, s.length);
        offset += s.length;
    }
    return out;
}

// Meme representation, mais sans les colonnes italique/souligne : la plupart
// des cas ne testent que le gras et le bruit nuirait a la lecture.
std::string describe_bold(const Paragraph& p) {
    if (p.spans.empty()) return "[ ]" + p.text;
    std::string out;
    std::size_t offset = 0;
    for (const StyleSpan& s : p.spans) {
        out += std::string("[") + (s.style.bold ? "b" : " ") + "]" + p.text.substr(offset, s.length);
        offset += s.length;
    }
    return out;
}

Paragraph make_bold_middle() {
    // "un mot gras ici" avec "mot gras" en gras.
    Paragraph p{"un mot gras ici"};
    gw::doc::style_apply(p.spans, p.text.size(), 3, 11, [](CharStyle& s) { s.bold = true; });
    return p;
}

void set_bold(CharStyle& s) { s.bold = true; }

// --- Plages : application, normalisation, chevauchement ---------------------

void test_apply_and_normalize() {
    Paragraph p = make_bold_middle();
    check_equal(describe_bold(p), "[ ]un [b]mot gras[ ] ici",
                "style applique sur une plage : trois segments");

    // Un paragraphe entierement au style par defaut revient a la forme
    // canonique (aucune plage), meme apres un aller-retour d application.
    Paragraph q{"texte simple"};
    gw::doc::style_apply(q.spans, q.text.size(), 0, q.text.size(), set_bold);
    gw::doc::style_apply(q.spans, q.text.size(), 0, q.text.size(),
                         [](CharStyle& s) { s.bold = false; });
    check(q.spans.empty(), "retour au style par defaut : la liste de plages redevient vide");

    // Deux applications adjacentes de meme style fusionnent.
    Paragraph r{"abcdef"};
    gw::doc::style_apply(r.spans, r.text.size(), 0, 3, set_bold);
    gw::doc::style_apply(r.spans, r.text.size(), 3, 6, set_bold);
    check(r.spans.size() == 1 && r.spans[0].length == 6,
          "deux plages adjacentes de meme style fusionnent en une seule");
}

void test_overlapping_styles() {
    // "abcdefghij" : gras sur [0,6), italique sur [3,9).
    Paragraph p{"abcdefghij"};
    gw::doc::style_apply(p.spans, p.text.size(), 0, 6, set_bold);
    gw::doc::style_apply(p.spans, p.text.size(), 3, 9, [](CharStyle& s) { s.italic = true; });

    check_equal(describe(p), "[b  ]abc[bi ]def[ i ]ghi[   ]j",
                "chevauchement gras/italique : trois zones distinctes plus le reste");

    // La zone de chevauchement porte bien les deux attributs, et eteindre le
    // gras n y touche pas a l italique.
    gw::doc::style_apply(p.spans, p.text.size(), 0, 6, [](CharStyle& s) { s.bold = false; });
    check_equal(describe(p), "[   ]abc[ i ]defghi[   ]j",
                "extinction du gras : l italique du chevauchement survit");
}

void test_insert_inherits() {
    Paragraph p = make_bold_middle();
    // Insertion en plein milieu du mot gras : le texte insere herite du gras.
    gw::doc::paragraph_insert(p, 6, "XX", nullptr);
    check_equal(describe_bold(p), "[ ]un [b]motXX gras[ ] ici",
                "insertion en milieu de plage grasse : le texte insere herite du gras");

    // A la frontiere droite de la plage grasse, on herite du caractere qui
    // precede — donc encore du gras. C est ce qui permet de continuer a ecrire
    // en gras a la fin d un mot gras.
    Paragraph q = make_bold_middle();
    gw::doc::paragraph_insert(q, 11, "!", nullptr);
    check_equal(describe_bold(q), "[ ]un [b]mot gras![ ] ici",
                "insertion en fin de plage grasse : herite du gras (caractere precedent)");

    // A la frontiere gauche, le caractere precedent est maigre : on reste
    // maigre.
    Paragraph r = make_bold_middle();
    gw::doc::paragraph_insert(r, 3, "?", nullptr);
    check_equal(describe_bold(r), "[ ]un ?[b]mot gras[ ] ici",
                "insertion en debut de plage grasse : reste maigre (caractere precedent)");

    // Style impose : c est le chemin de la bascule armee sans selection.
    Paragraph s{"abc"};
    CharStyle forced;
    forced.bold = true;
    gw::doc::paragraph_insert(s, 3, "GRAS", &forced);
    check_equal(describe_bold(s), "[ ]abc[b]GRAS",
                "style impose : le texte insere ne suit pas son voisinage");

    // Insertion a l offset 0 d un paragraphe entierement gras : herite du gras
    // qui suit, faute de caractere qui precede.
    Paragraph t{"gras"};
    gw::doc::style_apply(t.spans, t.text.size(), 0, 4, set_bold);
    gw::doc::paragraph_insert(t, 0, "X", nullptr);
    check_equal(describe_bold(t), "[b]Xgras",
                "insertion au tout debut : herite du caractere qui suit");
}

void test_erase_across_boundary() {
    // Suppression a cheval sur la frontiere maigre -> gras.
    Paragraph p = make_bold_middle(); // "un [mot gras] ici"
    gw::doc::paragraph_erase(p, 2, 3); // retire " mo"
    check_equal(describe_bold(p), "[ ]un[b]t gras[ ] ici",
                "suppression traversant une frontiere de style : les deux cotes se recollent");

    // Suppression de la totalite de la plage grasse : les deux moities maigres
    // fusionnent et le paragraphe redevient sans plages.
    Paragraph q = make_bold_middle();
    gw::doc::paragraph_erase(q, 3, 8);
    check(q.spans.empty(), "suppression de toute la plage grasse : retour a la forme canonique");
    check_equal(q.text, "un  ici", "suppression de toute la plage grasse : texte attendu");
}

void test_split_and_append() {
    Paragraph p = make_bold_middle();
    p.heading = HeadingLevel::H2;
    p.list = ListKind::Bullet;
    p.align = Alignment::Center;

    Paragraph tail = gw::doc::paragraph_split(p, 7); // coupe au milieu du gras
    check_equal(describe_bold(p), "[ ]un [b]mot ", "coupure : la tete garde sa part du gras");
    check_equal(describe_bold(tail), "[b]gras[ ] ici",
                "coupure : la queue garde sa part du gras");
    check(tail.heading == HeadingLevel::H2 && tail.list == ListKind::Bullet &&
              tail.align == Alignment::Center,
          "coupure : la queue herite des attributs de paragraphe");

    // Recoller doit rendre exactement le paragraphe de depart.
    gw::doc::paragraph_append(p, tail);
    check_equal(describe_bold(p), "[ ]un [b]mot gras[ ] ici",
                "coupure puis recollage : le style est rendu a l identique");
}

// --- Integration avec le moteur d edition -----------------------------------

TextEdit make_editor(const std::vector<std::string>& lines) {
    Document doc;
    for (const std::string& l : lines) doc.paragraphs().push_back(Paragraph{l});
    TextEdit edit;
    edit.reset(std::move(doc));
    return edit;
}

const Paragraph& para(const TextEdit& edit, std::size_t index) {
    return edit.document().paragraphs()[index];
}

void test_toggle_with_selection() {
    TextEdit edit = make_editor({"un mot gras ici"});
    edit.set_caret(Position{0, 3}, false);
    edit.set_caret(Position{0, 11}, true);
    edit.toggle_bold();
    check_equal(describe_bold(para(edit, 0)), "[ ]un [b]mot gras[ ] ici",
                "bascule Gras sur une selection : la plage est habillee");
    check(edit.document().is_dirty(), "bascule sur une selection : le document devient modifie");

    // Deuxieme appui : la selection est entierement grasse, donc on eteint.
    edit.toggle_bold();
    check(para(edit, 0).spans.empty(),
          "second appui sur une selection entierement grasse : le gras est retire");

    // Selection a moitie grasse : le premier appui doit TOUT mettre en gras,
    // pas basculer chaque caractere.
    TextEdit half = make_editor({"abcdef"});
    half.set_caret(Position{0, 0}, false);
    half.set_caret(Position{0, 3}, true);
    half.toggle_bold();
    half.set_caret(Position{0, 0}, false);
    half.set_caret(Position{0, 6}, true);
    half.toggle_bold();
    check_equal(describe_bold(para(half, 0)), "[b]abcdef",
                "selection a moitie grasse : le premier appui la met entierement en gras");
}

void test_pending_style() {
    TextEdit edit = make_editor({"abc"});
    edit.set_caret(Position{0, 3}, false);
    edit.toggle_bold(); // sans selection : arme la frappe suivante

    check(!edit.document().is_dirty(),
          "bascule sans selection : le document n est pas encore modifie");
    check(edit.current_char_style().bold, "bascule sans selection : l etat affiche passe a gras");

    edit.insert_text("GRAS");
    check_equal(describe_bold(para(edit, 0)), "[ ]abc[b]GRAS",
                "frappe apres une bascule sans selection : le texte sort en gras");

    // La frappe suivante continue en gras par simple heritage.
    edit.insert_text("!");
    check_equal(describe_bold(para(edit, 0)), "[ ]abc[b]GRAS!",
                "la frappe continue en gras sans avoir a rearmer");

    // Un deplacement de curseur desarme le style en attente.
    TextEdit moved = make_editor({"abc"});
    moved.set_caret(Position{0, 3}, false);
    moved.toggle_bold();
    moved.set_caret(Position{0, 0}, false);
    check(!moved.current_char_style().bold,
          "un deplacement de curseur desarme le style en attente");
}

void test_paragraph_attributes() {
    TextEdit edit = make_editor({"titre", "corps", "fin"});

    edit.set_caret(Position{0, 0}, false);
    edit.set_heading(HeadingLevel::H1);
    check(para(edit, 0).heading == HeadingLevel::H1 && para(edit, 1).heading == HeadingLevel::None,
          "titre applique au seul paragraphe du curseur");

    // Selection couvrant deux paragraphes : les deux recoivent la puce.
    edit.set_caret(Position{1, 0}, false);
    edit.set_caret(Position{2, 2}, true);
    edit.set_list(ListKind::Bullet);
    check(para(edit, 1).list == ListKind::Bullet && para(edit, 2).list == ListKind::Bullet &&
              para(edit, 0).list == ListKind::None,
          "liste appliquee a tous les paragraphes de la selection");

    edit.set_alignment(Alignment::Center);
    check(para(edit, 1).align == Alignment::Center && para(edit, 2).align == Alignment::Center,
          "alignement applique a tous les paragraphes de la selection");
}

void test_undo_covers_formatting() {
    TextEdit edit = make_editor({"un mot gras ici"});
    edit.set_caret(Position{0, 3}, false);
    edit.set_caret(Position{0, 11}, true);
    edit.toggle_bold();
    check(edit.can_undo(), "une mise en forme est empilee dans l historique");

    edit.undo();
    check(para(edit, 0).spans.empty(), "Ctrl+Z annule la mise en forme");

    edit.redo();
    check_equal(describe_bold(para(edit, 0)), "[ ]un [b]mot gras[ ] ici",
                "Ctrl+Y retablit la mise en forme");

    // Le style de paragraphe passe par le meme historique.
    edit.set_heading(HeadingLevel::H3);
    edit.undo();
    check(para(edit, 0).heading == HeadingLevel::None, "Ctrl+Z annule un changement de titre");
}

void test_delete_across_paragraphs_merges_styles() {
    TextEdit edit = make_editor({"gras ici", "maigre la"});
    // Tout le premier paragraphe en gras.
    edit.select_all();
    edit.set_caret(Position{0, 0}, false);
    edit.set_caret(Position{0, 8}, true);
    edit.toggle_bold();

    // Selection de " ici" + coupure + "maigre" -> fusion des deux paragraphes.
    edit.set_caret(Position{0, 4}, false);
    edit.set_caret(Position{1, 6}, true);
    edit.erase_forward(false);

    check(edit.document().paragraphs().size() == 1, "fusion : un seul paragraphe survit");
    check_equal(describe_bold(para(edit, 0)), "[b]gras[ ] la",
                "fusion : le gras du haut et le maigre du bas cohabitent");
}

void test_paragraph_break_keeps_attributes() {
    TextEdit edit = make_editor({"une puce"});
    edit.set_caret(Position{0, 0}, false);
    edit.set_list(ListKind::Bullet);
    edit.set_caret(Position{0, 4}, false);
    edit.insert_paragraph_break();

    check(edit.document().paragraphs().size() == 2 && para(edit, 0).list == ListKind::Bullet &&
              para(edit, 1).list == ListKind::Bullet,
          "couper une puce en deux donne deux puces");
}

void test_font_by_range() {
    TextEdit edit = make_editor({"deux polices"});
    edit.set_caret(Position{0, 0}, false);
    edit.set_caret(Position{0, 4}, true);
    edit.set_font("Fira Sans");
    check_equal(describe(para(edit, 0)), "[   =Fira Sans]deux[   ] polices",
                "police appliquee a la seule plage selectionnee");

    // Sans selection, le choix devient la police par defaut du document.
    edit.set_caret(Position{0, 12}, false);
    edit.set_font("Inter");
    check_equal(edit.document().default_font(), "Inter",
                "police choisie sans selection : devient la police par defaut du document");
}

void test_malformed_spans_are_repaired() {
    // Un fichier peut mentir sur les longueurs. Le modele doit se reparer, pas
    // propager l incoherence.
    Paragraph p{"abcdef"};
    p.spans.push_back(StyleSpan{100, CharStyle{}}); // trop long
    CharStyle bold;
    bold.bold = true;
    p.spans.push_back(StyleSpan{4, bold}); // au-dela du texte
    gw::doc::style_normalize(p.spans, p.text.size());
    check(p.spans.empty(), "plages trop longues : tronquees et ramenees a la forme canonique");

    Paragraph q{"abcdef"};
    q.spans.push_back(StyleSpan{2, bold}); // trop court : le reste est complete
    gw::doc::style_normalize(q.spans, q.text.size());
    check_equal(describe_bold(q), "[b]ab[ ]cdef",
                "plages trop courtes : le reste est complete au style par defaut");
}

// --- Mode clavier-first : syntaxe Markdown au fil de la frappe --------------
//
// `type` reproduit fidelement App::on_char : un caractere insere, puis une
// tentative de conversion. Passer la chaine entiere d un coup ne testerait
// rien, parce que la conversion se declenche sur le caractere de fermeture et
// pas sur le contenu deja present dans le paragraphe.
void type(TextEdit& edit, const std::string& text) {
    for (char c : text) {
        edit.insert_text(std::string(1, c));
        edit.apply_markdown_live();
    }
}

const Paragraph& only_paragraph(const TextEdit& edit) {
    return edit.document().paragraphs().front();
}

void test_markdown_emphasis() {
    {
        TextEdit e;
        type(e, "**gras**");
        check_equal(describe_bold(only_paragraph(e)), "[b]gras",
                    "**gras** : marqueurs retires, contenu en gras");
    }
    {
        TextEdit e;
        type(e, "un *mot* ici");
        check_equal(describe(only_paragraph(e)), "[   ]un [ i ]mot[   ] ici",
                    "*mot* : italique sur le seul contenu encadre");
    }
    {
        TextEdit e;
        type(e, "un _mot_ ici");
        check_equal(describe(only_paragraph(e)), "[   ]un [ i ]mot[   ] ici",
                    "_mot_ : meme resultat que *mot*");
    }
    {
        // Le style se pose la ou il faut meme quand la syntaxe arrive apres du
        // texte deja ecrit : c est le cas courant, pas le cas limite.
        TextEdit e;
        type(e, "debut **fin**");
        check_equal(describe_bold(only_paragraph(e)), "[ ]debut [b]fin",
                    "gras en fin de phrase : le debut n est pas touche");
    }
}

void test_markdown_incomplete_syntax_does_nothing() {
    {
        TextEdit e;
        type(e, "*incomplet");
        check_equal(describe(only_paragraph(e)), "[ ]*incomplet",
                    "un seul * sans fermeture : rien n est converti");
    }
    {
        TextEdit e;
        type(e, "**gras");
        check_equal(describe(only_paragraph(e)), "[ ]**gras",
                    "** sans fermeture : les marqueurs restent du texte");
    }
    {
        // Contenu vide : `****` ne doit pas produire une plage de zero octet.
        TextEdit e;
        type(e, "****");
        check_equal(describe(only_paragraph(e)), "[ ]****", "**** : contenu vide, aucun effet");
    }
    {
        // Le faux positif le plus previsible dans un editeur ou l on tape aussi
        // des identifiants.
        TextEdit e;
        type(e, "nom_de_variable");
        check_equal(describe(only_paragraph(e)), "[ ]nom_de_variable",
                    "souligne interne a un mot : pas d italique");
    }
    {
        // Deuxieme faux positif previsible : une multiplication.
        TextEdit e;
        type(e, "3 * 4 * 5");
        check_equal(describe(only_paragraph(e)), "[ ]3 * 4 * 5",
                    "marqueurs bordes d espaces : pas d italique");
    }
}

void test_markdown_paragraph_prefixes() {
    const struct {
        const char* typed;
        HeadingLevel heading;
        const char* rest;
    } headings[] = {
        {"# Titre", HeadingLevel::H1, "Titre"},
        {"## Titre", HeadingLevel::H2, "Titre"},
        {"### Titre", HeadingLevel::H3, "Titre"},
    };
    for (const auto& c : headings) {
        TextEdit e;
        type(e, c.typed);
        const Paragraph& p = only_paragraph(e);
        check(p.heading == c.heading && p.text == c.rest,
              std::string("\"") + c.typed + "\" : prefixe retire, niveau de titre pose");
    }

    {
        TextEdit e;
        type(e, "- item");
        const Paragraph& p = only_paragraph(e);
        check(p.list == ListKind::Bullet && p.text == "item",
              "\"- item\" : prefixe retire, puce posee");
    }
    {
        TextEdit e;
        type(e, "12. item");
        const Paragraph& p = only_paragraph(e);
        check(p.list == ListKind::Numbered && p.text == "item",
              "\"12. item\" : prefixe retire, liste numerotee posee");
    }
    {
        // Un diese qui n est pas en tete de paragraphe reste du texte.
        TextEdit e;
        type(e, "mot # suite");
        check_equal(only_paragraph(e).text, "mot # suite",
                    "diese en milieu de paragraphe : aucun titre");
    }
    {
        // Quatre dieses ne sont pas un niveau de titre : le modele s arrete a
        // H3, et rien ne doit etre retire silencieusement.
        TextEdit e;
        type(e, "#### trop");
        const Paragraph& p = only_paragraph(e);
        check(p.heading == HeadingLevel::None && p.text == "#### trop",
              "#### : au-dela de H3, aucun effet");
    }
}

void test_markdown_composes_with_existing_style() {
    // La syntaxe apparait dans du texte deja stylise autrement : le nouvel
    // attribut s ajoute, il ne remplace pas. C est la consequence directe de
    // passer par style_apply() plutot que d ecrire les plages a la main.
    TextEdit e;
    type(e, "a");
    e.toggle_italic(); // arme l italique pour la suite de la frappe
    type(e, "**b**");
    check_equal(describe(only_paragraph(e)), "[   ]a[bi ]b",
                "gras tape dans du texte deja italique : les deux attributs cohabitent");
}

void test_markdown_closes_the_emphasis() {
    // Apres la fermeture, le curseur est colle a du texte gras : sans remise a
    // zero explicite, la frappe suivante en heriterait et l utilisateur aurait
    // tape `**` pour rien.
    TextEdit e;
    type(e, "**gras**");
    type(e, " suite");
    check_equal(describe_bold(only_paragraph(e)), "[b]gras[ ] suite",
                "apres la fermeture, la frappe suivante n est plus grasse");
}

void test_markdown_undo_restores_markers() {
    TextEdit e;
    type(e, "**gras**");
    check(e.undo(), "conversion Markdown : annulable");
    check_equal(describe(only_paragraph(e)), "[ ]**gras**",
                "annuler une conversion rend la syntaxe telle qu elle a ete tapee");
    check(e.redo() && only_paragraph(e).text == "gras", "retablir refait la conversion");
}

void test_markdown_and_toolbar_produce_the_same_model() {
    // LE TEST QUI JUSTIFIE TOUT LE CHANTIER : les deux modes de saisie ne sont
    // pas deux formats, c est la meme mise en forme produite autrement. Si
    // cette egalite tombe, basculer d un mode a l autre perd quelque chose.
    TextEdit keyboard;
    type(keyboard, "**gras**");

    TextEdit toolbar;
    toolbar.insert_text("gras");
    toolbar.select_all();
    toolbar.toggle_bold();

    check_equal(describe(only_paragraph(keyboard)), describe(only_paragraph(toolbar)),
                "clavier-first et barre d outils : modele de document identique");
}

void test_markdown_only_runs_when_asked() {
    // La conversion n est jamais implicite : sans appel a apply_markdown_live(),
    // le mode barre d outils voit la syntaxe comme du texte ordinaire. C est ce
    // qui rend la bascule sans effet retroactif.
    TextEdit e;
    e.insert_text("**gras**");
    check_equal(describe(only_paragraph(e)), "[ ]**gras**",
                "mode barre d outils : la syntaxe reste du texte");
}

} // namespace

int main() {
    test_apply_and_normalize();
    test_overlapping_styles();
    test_insert_inherits();
    test_erase_across_boundary();
    test_split_and_append();

    test_toggle_with_selection();
    test_pending_style();
    test_paragraph_attributes();
    test_undo_covers_formatting();
    test_delete_across_paragraphs_merges_styles();
    test_paragraph_break_keeps_attributes();
    test_font_by_range();
    test_malformed_spans_are_repaired();

    test_markdown_emphasis();
    test_markdown_incomplete_syntax_does_nothing();
    test_markdown_paragraph_prefixes();
    test_markdown_composes_with_existing_style();
    test_markdown_closes_the_emphasis();
    test_markdown_undo_restores_markers();
    test_markdown_and_toolbar_produce_the_same_model();
    test_markdown_only_runs_when_asked();

    std::printf("\n%s (%d echec%s)\n", failures == 0 ? "TOUT PASSE" : "DES TESTS ECHOUENT",
                failures, failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
