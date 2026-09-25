// tools/edit_tests.cpp — verification du coeur du moteur d edition.
//
// `doc::TextEdit` est un module pur : ni fenetre, ni police, ni pixel. C est
// ce qui permet de verifier ici la totalite de la logique d edition — celle ou
// se cachent les vrais bugs (frontieres UTF-8, selections a cheval sur
// plusieurs paragraphes, groupement des annulations) — sans simuler une seule
// frappe clavier.
//
// Ce qui n est PAS teste ici, parce que ce n est pas dans ce module : le
// retour a la ligne, le defilement et la correspondance clic -> position
// dependent des metriques de police et vivent dans app/editor_view.
//
// Usage : gw_edit_tests
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
        std::printf("       attendu : \"%s\"\n       obtenu  : \"%s\"\n", expected.c_str(),
                    got.c_str());
        ++failures;
    }
}

using gw::doc::Document;
using gw::doc::Paragraph;
using gw::doc::Position;
using gw::doc::TextEdit;

// Document construit depuis des lignes, pour que les cas de test restent
// lisibles a cote de leur assertion.
Document make_document(const std::vector<std::string>& lines) {
    Document doc;
    for (const std::string& line : lines) doc.paragraphs().push_back(Paragraph{line});
    return doc;
}

// Tout le document remis a plat, separateur explicite : c est la forme la plus
// courte pour comparer une structure de paragraphes a ce qu on attend.
std::string flatten(const TextEdit& edit) {
    std::string out;
    for (const Paragraph& p : edit.document().paragraphs()) {
        if (!out.empty()) out += "|";
        out += p.text;
    }
    return out;
}

void type(TextEdit& edit, const std::string& text) { edit.insert_text(text); }

// --- 1. Etat initial --------------------------------------------------------

void test_empty_document() {
    TextEdit edit;
    check(edit.document().paragraphs().size() == 1,
          "document vierge : un paragraphe vide, jamais zero");
    check(edit.caret() == Position{0, 0}, "curseur au debut");
    check(!edit.has_selection(), "aucune selection");
    check(!edit.document().is_dirty(), "document vierge non modifie");

    // Un document charge sans aucun paragraphe ne doit pas laisser le curseur
    // sans position valide.
    edit.reset(Document{});
    check(edit.document().paragraphs().size() == 1,
          "reset sur document vide : un paragraphe est cree");
}

// --- 2. Frontieres UTF-8 ----------------------------------------------------

void test_utf8_boundaries() {
    // « éàü » : trois caracteres, six octets. « € » : trois octets.
    const std::string text = "éàü€";
    check(text.size() == 9, "vecteur : 4 caracteres sur 9 octets");

    std::size_t i = 0;
    i = gw::doc::next_char_boundary(text, i);
    check(i == 2, "avance d un caractere a deux octets");
    i = gw::doc::next_char_boundary(text, i);
    check(i == 4, "avance du deuxieme");
    i = gw::doc::next_char_boundary(text, i);
    check(i == 6, "avance du troisieme");
    i = gw::doc::next_char_boundary(text, i);
    check(i == 9, "avance de l euro, trois octets");
    check(gw::doc::next_char_boundary(text, i) == 9, "en fin de chaine, n avance plus");

    check(gw::doc::previous_char_boundary(text, 9) == 6, "recule de l euro");
    check(gw::doc::previous_char_boundary(text, 6) == 4, "recule du troisieme");
    check(gw::doc::previous_char_boundary(text, 0) == 0, "au debut, ne recule plus");

    // Position tombee au milieu d une sequence : ramenee sur la frontiere.
    TextEdit edit;
    edit.reset(make_document({text}));
    edit.set_caret(Position{0, 1}, false);
    check(edit.caret().offset == 0, "position au milieu d une sequence : ramenee sur la frontiere");
}

void test_typing_utf8() {
    TextEdit edit;
    type(edit, "é");
    check(edit.caret() == (Position{0, 2}), "un caractere accentue avance de deux octets");
    type(edit, "t");
    check_equal(flatten(edit), "ét", "insertion apres un caractere multi-octets");

    edit.erase_backward(false);
    check_equal(flatten(edit), "é", "retour arriere efface un caractere, pas un octet");
    edit.erase_backward(false);
    check_equal(flatten(edit), "", "retour arriere efface le caractere accentue en entier");
}

// --- 3. Deplacements --------------------------------------------------------

void test_movement_across_paragraphs() {
    TextEdit edit;
    edit.reset(make_document({"un", "deux"}));

    edit.move_right(false, false);
    edit.move_right(false, false);
    check(edit.caret() == (Position{0, 2}), "fin du premier paragraphe");
    edit.move_right(false, false);
    check(edit.caret() == (Position{1, 0}),
          "un pas de plus franchit la coupure de paragraphe");
    edit.move_left(false, false);
    check(edit.caret() == (Position{0, 2}), "et le retour la refranchit");

    edit.move_document_end(false);
    check(edit.caret() == (Position{1, 4}), "fin de document");
    edit.move_right(false, false);
    check(edit.caret() == (Position{1, 4}), "en fin de document, le curseur ne sort pas");
    edit.move_document_start(false);
    edit.move_left(false, false);
    check(edit.caret() == (Position{0, 0}), "au debut de document, le curseur ne sort pas");
}

void test_word_movement() {
    TextEdit edit;
    edit.reset(make_document({"le chat   dort"}));

    edit.move_document_start(false);
    edit.move_right(false, true);
    check(edit.caret() == (Position{0, 3}), "Ctrl+Droite : debut du mot suivant");
    edit.move_right(false, true);
    check(edit.caret() == (Position{0, 10}), "saute le mot puis les espaces multiples");

    edit.move_document_end(false);
    edit.move_left(false, true);
    check(edit.caret() == (Position{0, 10}), "Ctrl+Gauche : debut du mot courant");
    edit.move_left(false, true);
    check(edit.caret() == (Position{0, 3}),
          "puis debut du mot precedent, sans s arreter sur les espaces");
}

void test_selection_collapse() {
    TextEdit edit;
    edit.reset(make_document({"abcdef"}));
    edit.set_caret(Position{0, 2}, false);
    edit.set_caret(Position{0, 4}, true);
    check(edit.has_selection(), "selection posee");
    check_equal(edit.selected_text(), "cd", "texte selectionne");

    // Sans Maj, une fleche replie la selection sur son bord, sans bouger d un
    // caractere de plus.
    edit.move_left(false, false);
    check(edit.caret() == (Position{0, 2}), "Gauche replie la selection sur son debut");
    check(!edit.has_selection(), "et la fait disparaitre");

    edit.set_caret(Position{0, 2}, false);
    edit.set_caret(Position{0, 4}, true);
    edit.move_right(false, false);
    check(edit.caret() == (Position{0, 4}), "Droite replie la selection sur sa fin");
}

void test_select_word() {
    TextEdit edit;
    edit.reset(make_document({"le chat dort"}));

    edit.select_word_at(Position{0, 4}); // au milieu de « chat »
    check_equal(edit.selected_text(), "chat", "double-clic dans un mot");

    edit.select_word_at(Position{0, 2}); // sur l espace
    check_equal(edit.selected_text(), " ", "double-clic sur un espace prend les espaces");

    edit.reset(make_document({"élan"}));
    edit.select_word_at(Position{0, 0});
    check_equal(edit.selected_text(), "élan", "les lettres accentuees font partie du mot");
}

// --- 4. Mutations -----------------------------------------------------------

void test_paragraph_split_and_merge() {
    TextEdit edit;
    edit.reset(make_document({"abcdef"}));
    edit.set_caret(Position{0, 3}, false);
    edit.insert_paragraph_break();
    check_equal(flatten(edit), "abc|def", "Entree coupe le paragraphe au curseur");
    check(edit.caret() == (Position{1, 0}), "curseur en tete du nouveau paragraphe");

    edit.erase_backward(false);
    check_equal(flatten(edit), "abcdef", "retour arriere en tete de paragraphe refusionne");
    check(edit.caret() == (Position{0, 3}), "curseur a la jointure");

    edit.move_document_end(false);
    edit.erase_forward(false);
    check_equal(flatten(edit), "abcdef", "Suppr en fin de document ne fait rien");
}

void test_multi_paragraph_selection() {
    TextEdit edit;
    edit.reset(make_document({"premier", "deuxieme", "troisieme"}));
    edit.set_caret(Position{0, 3}, false);
    edit.set_caret(Position{2, 4}, true);

    check_equal(edit.selected_text(), "mier\ndeuxieme\ntroi",
                "le texte selectionne traverse les paragraphes, coupures comprises");

    edit.erase_backward(false);
    check_equal(flatten(edit), "presieme",
                "la suppression fusionne les deux moities et supprime le milieu");
    check(edit.caret() == (Position{0, 3}), "curseur au point de jonction");
    check(!edit.has_selection(), "plus de selection apres suppression");
}

void test_typing_replaces_selection() {
    TextEdit edit;
    edit.reset(make_document({"bonjour"}));
    edit.set_caret(Position{0, 0}, false);
    edit.set_caret(Position{0, 3}, true);
    type(edit, "X");
    check_equal(flatten(edit), "Xjour", "taper avec une selection la remplace");
}

void test_paste_multiline() {
    TextEdit edit;
    edit.reset(make_document({"ab"}));
    edit.set_caret(Position{0, 1}, false);
    type(edit, "1\n2\n3");
    check_equal(flatten(edit), "a1|2|3b",
                "un collage multiligne produit de vrais paragraphes, pas des 0x0A");
    check(edit.caret() == (Position{2, 1}), "curseur apres le dernier morceau colle");

    // Un texte se terminant par un saut de ligne laisse un paragraphe vide.
    edit.reset(make_document({""}));
    type(edit, "x\n");
    check_equal(flatten(edit), "x|", "saut de ligne final : paragraphe vide en fin");
}

void test_word_deletion() {
    TextEdit edit;
    edit.reset(make_document({"le chat dort"}));
    edit.move_document_end(false);
    edit.erase_backward(true);
    check_equal(flatten(edit), "le chat ", "Ctrl+Retour efface le mot precedent");

    edit.move_document_start(false);
    edit.erase_forward(true);
    check_equal(flatten(edit), "chat ", "Ctrl+Suppr efface le mot suivant");
}

// --- 5. Annulation ----------------------------------------------------------

void test_undo_groups_typing() {
    TextEdit edit;
    for (const char* c : {"b", "o", "n", "j", "o", "u", "r"}) type(edit, c);
    check_equal(flatten(edit), "bonjour", "sept frappes");

    check(edit.undo(), "annulation possible");
    check_equal(flatten(edit), "",
                "une frappe continue s annule d un seul coup, pas caractere par caractere");

    check(edit.redo(), "retablissement possible");
    check_equal(flatten(edit), "bonjour", "le retablissement rend le texte a l identique");
}

void test_undo_splits_on_move() {
    TextEdit edit;
    type(edit, "abc");
    edit.set_caret(Position{0, 0}, false); // deplacement : coupe le groupe
    type(edit, "X");
    check_equal(flatten(edit), "Xabc", "insertion au debut");

    edit.undo();
    check_equal(flatten(edit), "abc", "la premiere annulation ne defait que la seconde frappe");
    edit.undo();
    check_equal(flatten(edit), "", "la seconde annulation defait la premiere");
    check(!edit.can_undo(), "pile d annulation vide");
}

void test_undo_restores_selection_delete() {
    TextEdit edit;
    edit.reset(make_document({"premier", "deuxieme"}));
    edit.select_all();
    edit.erase_backward(false);
    check_equal(flatten(edit), "", "tout supprime");

    check(edit.undo(), "annulation");
    check_equal(flatten(edit), "premier|deuxieme",
                "la structure des paragraphes est rendue telle quelle");
}

void test_redo_is_dropped_by_new_edit() {
    TextEdit edit;
    type(edit, "abc");
    edit.undo();
    check(edit.can_redo(), "retablissement disponible juste apres une annulation");
    type(edit, "z");
    check(!edit.can_redo(),
          "une nouvelle modification abandonne la branche de retablissement");
}

// --- 6. Bornage -------------------------------------------------------------

void test_clamp() {
    TextEdit edit;
    edit.reset(make_document({"abc", "de"}));

    check(edit.clamp(Position{99, 99}) == (Position{1, 2}),
          "position au-dela du document : ramenee a la fin");
    check(edit.clamp(Position{0, 99}) == (Position{0, 3}),
          "offset au-dela du paragraphe : ramene a sa fin");

    // La vue s en sert pour un clic sous la derniere ligne : ca ne doit ni
    // planter ni sortir du document.
    edit.set_caret(Position{50, 0}, false);
    check(edit.caret() == (Position{1, 2}), "un clic hors du texte pose le curseur a la fin");
}

void test_undo_structural_sharing() {
    std::vector<std::string> lines;
    for (int i = 0; i < 50; ++i) {
        lines.push_back("Paragraphe initial " + std::to_string(i));
    }
    TextEdit edit;
    edit.reset(make_document(lines));

    edit.set_caret(Position{25, 5}, false);
    edit.insert_text(" MODIFIE ");
    check(edit.can_undo(), "undo disponible apres modif");

    edit.undo();
    check_equal(edit.document().paragraphs()[25].text, "Paragraphe initial 25",
                "undo restaure le paragraphe 25");
    check_equal(edit.document().paragraphs()[0].text, "Paragraphe initial 0",
                "paragraphe 0 intact");
    check_equal(edit.document().paragraphs()[49].text, "Paragraphe initial 49",
                "paragraphe 49 intact");

    edit.redo();
    check_equal(edit.document().paragraphs()[25].text, "Parag MODIFIE raphe initial 25",
                "redo retablit la modification");
}

void test_paragraph_compression() {
    Paragraph p{"Un texte assez long pour etre compresse efficacement par notre algorithme LZ en memoire vive. Il contient des repetitions pour comprimer."};
    check(!p.is_compressed(), "paragraphe cree non compresse");
    p.compress();
    check(p.is_compressed(), "paragraphe compresse");
    check(p.text.empty(), "tampon text vide apres compression");
    check(!p.compressed_text.empty(), "donnees compressees presentes");
    p.ensure_decompressed();
    check(!p.is_compressed(), "paragraphe decompresse sur demande");
    check_equal(p.text, "Un texte assez long pour etre compresse efficacement par notre algorithme LZ en memoire vive. Il contient des repetitions pour comprimer.",
                "texte restaure fidelement");
}

} // namespace

int main() {
    std::printf("--- Etat initial ---\n");
    test_empty_document();

    std::printf("\n--- Frontieres UTF-8 ---\n");
    test_utf8_boundaries();
    test_typing_utf8();

    std::printf("\n--- Deplacements ---\n");
    test_movement_across_paragraphs();
    test_word_movement();
    test_selection_collapse();
    test_select_word();

    std::printf("\n--- Mutations ---\n");
    test_paragraph_split_and_merge();
    test_multi_paragraph_selection();
    test_typing_replaces_selection();
    test_paste_multiline();
    test_word_deletion();

    std::printf("\n--- Annulation ---\n");
    test_undo_groups_typing();
    test_undo_splits_on_move();
    test_undo_restores_selection_delete();
    test_redo_is_dropped_by_new_edit();
    test_undo_structural_sharing();

    std::printf("\n--- Bornage ---\n");
    test_clamp();

    std::printf("\n--- Compression en memoire ---\n");
    test_paragraph_compression();

    std::printf("\n%s (%d echec%s)\n", failures == 0 ? "TOUT PASSE" : "ECHECS", failures,
                failures > 1 ? "s" : "");
    return failures == 0 ? 0 : 1;
}
