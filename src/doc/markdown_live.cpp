#include "doc/markdown_live.h"

#include "doc/style.h"

namespace gw::doc {
namespace {

bool is_space(char c) { return c == ' ' || c == '\t'; }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Vrai si [begin, end) contient au moins un caractere, et ne commence ni ne
// finit par une espace.
//
// POURQUOI LE TEST SUR LES ESPACES : sans lui, une multiplication tapee au fil
// du texte (`3 * 4 * 5`) deviendrait de l italique au troisieme facteur. La
// regle « un marqueur colle a son contenu » est celle de CommonMark, et elle
// suffit a ecarter le cas.
bool content_is_emphasizable(const std::string& text, std::size_t begin, std::size_t end) {
    if (begin >= end) return false;
    if (is_space(text[begin]) || is_space(text[end - 1])) return false;
    return true;
}

// Vrai si aucun octet de [begin, end) ne vaut `marker`. Un marqueur au milieu
// du contenu veut dire qu on s est trompe de paire : on prefere ne rien faire.
bool free_of(const std::string& text, std::size_t begin, std::size_t end, char marker) {
    for (std::size_t i = begin; i < end; ++i) {
        if (text[i] == marker) return false;
    }
    return true;
}

// Vrai si un marqueur ouvrant pose a `index` est bien en debut de mot : soit au
// tout debut du paragraphe, soit precede d une espace.
//
// RESERVE AU SOULIGNE. C est ce test qui empeche `nom_de_variable` de devenir
// de l italique au deuxieme souligne — le faux positif le plus previsible dans
// un editeur ou l on tape aussi du code ou des identifiants. L asterisque n y
// est PAS soumis : `mot**gras**` est une forme legitime, et le souligne est le
// seul des deux marqueurs a apparaitre couramment a l interieur d un mot.
bool opens_a_word(const std::string& text, std::size_t index) {
    return index == 0 || is_space(text[index - 1]);
}

// --- Regles de paragraphe ---------------------------------------------------
//
// Toutes se declenchent sur l espace qui suit le prefixe, et seulement si le
// curseur est exactement au bout de ce prefixe : c est la signature « je viens
// de taper l espace », qui ne peut pas se produire par un simple deplacement du
// curseur.
MarkdownRule detect_paragraph(const Paragraph& p, std::size_t caret) {
    MarkdownRule rule;
    const std::string& t = p.text;
    if (caret < 2 || caret > t.size() || t[caret - 1] != ' ') return rule;

    // Titres. Le niveau est le nombre de '#', et le prefixe doit occuper tout
    // ce qui precede le curseur : `a # ` ne fait pas un titre.
    std::size_t hashes = 0;
    while (hashes < t.size() && t[hashes] == '#') ++hashes;
    if (hashes >= 1 && hashes <= 3 && caret == hashes + 1) {
        // Un paragraphe DEJA titre se re-titre quand meme : le niveau tape
        // remplace le niveau en place. Refuser serait un piege, parce qu un
        // paragraphe herite du niveau de titre de celui qu on vient de couper
        // (cf. paragraph_split) — taper `## ` sous un H1 doit donner un H2,
        // pas laisser les deux dieses en texte.
        rule.kind = MarkdownRule::Kind::Paragraph;
        rule.prefix_length = caret;
        rule.heading = static_cast<HeadingLevel>(hashes);
        return rule;
    }

    // Puce.
    if (caret == 2 && t[0] == '-') {
        rule.kind = MarkdownRule::Kind::Paragraph;
        rule.prefix_length = 2;
        rule.list = ListKind::Bullet;
        return rule;
    }

    // Liste numerotee : un ou plusieurs chiffres, un point, l espace. Ajoutee
    // avec les puces parce qu elle partage exactement leur forme et que
    // ListKind::Numbered existe deja — pas une extension de portee, la meme
    // regle avec un autre prefixe.
    std::size_t digits = 0;
    while (digits < t.size() && is_digit(t[digits])) ++digits;
    // Meme raison que pour les titres : le type de liste herite d un paragraphe
    // a l autre, donc taper `1. ` sous une puce doit basculer en numerotee.
    if (digits >= 1 && caret == digits + 2 && t[digits] == '.') {
        rule.kind = MarkdownRule::Kind::Paragraph;
        rule.prefix_length = caret;
        rule.list = ListKind::Numbered;
        return rule;
    }

    return rule;
}

// --- Gras : `**texte**` -----------------------------------------------------
MarkdownRule detect_bold(const Paragraph& p, std::size_t caret) {
    MarkdownRule rule;
    const std::string& t = p.text;
    // Il faut au minimum `**x**`, soit cinq octets, pour que le contenu ne soit
    // pas vide.
    if (caret < 5 || t[caret - 1] != '*' || t[caret - 2] != '*') return rule;

    const std::size_t content_end = caret - 2;

    // Ouvrant le plus PROCHE, cherche a rebours : `a **b** c **d**` doit
    // apparier le second couple, pas le premier.
    for (std::size_t open = content_end; open-- > 1;) {
        if (t[open] != '*' || t[open - 1] != '*') continue;
        const std::size_t open_begin = open - 1;
        const std::size_t content_begin = open + 1;
        if (!content_is_emphasizable(t, content_begin, content_end)) return rule;
        if (!free_of(t, content_begin, content_end, '*')) return rule;

        rule.kind = MarkdownRule::Kind::Emphasis;
        rule.open_begin = open_begin;
        rule.open_length = 2;
        rule.close_begin = content_end;
        rule.close_length = 2;
        rule.bold = true;
        return rule;
    }
    return rule;
}

// --- Italique : `*texte*` ou `_texte_` --------------------------------------
MarkdownRule detect_italic(const Paragraph& p, std::size_t caret) {
    MarkdownRule rule;
    const std::string& t = p.text;
    if (caret < 3) return rule;

    const char marker = t[caret - 1];
    if (marker != '*' && marker != '_') return rule;

    // Un marqueur double n est jamais de l italique : soit c est du gras, deja
    // traite avant nous, soit c est une tentative de gras qui n a pas trouve
    // son ouvrant — et dans ce second cas il ne faut surtout pas se rabattre
    // sur l italique, qui mangerait un des deux asterisques.
    if (t[caret - 2] == marker) return rule;

    const std::size_t content_end = caret - 1;
    for (std::size_t open = content_end; open-- > 0;) {
        if (t[open] != marker) continue;
        // Marqueur colle a un jumeau : il appartient a une paire double.
        if (open + 1 < t.size() && t[open + 1] == marker) return rule;
        if (open > 0 && t[open - 1] == marker) return rule;
        if (marker == '_' && !opens_a_word(t, open)) return rule;
        if (!content_is_emphasizable(t, open + 1, content_end)) return rule;

        rule.kind = MarkdownRule::Kind::Emphasis;
        rule.open_begin = open;
        rule.open_length = 1;
        rule.close_begin = content_end;
        rule.close_length = 1;
        rule.italic = true;
        return rule;
    }
    return rule;
}

} // namespace

MarkdownRule markdown_detect(const Paragraph& p, std::size_t caret) {
    p.ensure_decompressed();
    if (caret > p.text.size()) return MarkdownRule{};

    // ORDRE IMPOSE : le gras avant l italique, parce que les deux se terminent
    // par un `*` et que seul le test du gras sait distinguer `**` de `*`.
    MarkdownRule rule = detect_paragraph(p, caret);
    if (rule.applies()) return rule;
    rule = detect_bold(p, caret);
    if (rule.applies()) return rule;
    return detect_italic(p, caret);
}

std::size_t markdown_apply(Paragraph& p, const MarkdownRule& rule, std::size_t caret) {
    switch (rule.kind) {
        case MarkdownRule::Kind::None:
            return caret;

        case MarkdownRule::Kind::Paragraph:
            paragraph_erase(p, 0, rule.prefix_length);
            return caret >= rule.prefix_length ? caret - rule.prefix_length : 0;

        case MarkdownRule::Kind::Emphasis: {
            // LE FERMANT D ABORD. Retirer l ouvrant en premier decalerait le
            // fermant de `open_length` octets, et il faudrait s en souvenir a
            // chaque ligne qui suit. En partant de la fin, aucune borne calculee
            // par la detection ne bouge avant d avoir servi.
            paragraph_erase(p, rule.close_begin, rule.close_length);
            paragraph_erase(p, rule.open_begin, rule.open_length);

            const std::size_t content_begin = rule.open_begin;
            const std::size_t content_end = rule.close_begin - rule.open_length;
            const bool bold = rule.bold;
            const bool italic = rule.italic;
            // style_apply et non une ecriture directe des plages : c est la
            // meme fonction que la barre d outils appelle, donc un contenu deja
            // souligne ou dans une autre police garde ses autres attributs.
            style_apply(p.spans, p.text.size(), content_begin, content_end,
                        [bold, italic](CharStyle& s) {
                            if (bold) s.bold = true;
                            if (italic) s.italic = true;
                        });

            const std::size_t removed = rule.open_length + rule.close_length;
            return caret >= removed ? caret - removed : 0;
        }
    }
    return caret;
}

} // namespace gw::doc
