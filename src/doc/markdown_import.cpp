// doc/markdown_import.cpp — voir markdown_import.h.
//
// Deux passes sans arbre : une passe par ligne pour la structure (titre, liste,
// citation, bloc de code, tableau), puis une passe par segment pour le texte en
// ligne (gras, italique, liens). Chaque segment est pose par paragraph_insert
// avec son style impose : c est la seule facon d ecrire des plages sans
// desynchroniser le texte, et elle garde les invariants de doc/style.h.
#include "doc/markdown_import.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "doc/style.h"

namespace gw::doc {
namespace {

// Un morceau de texte et le style qu il porte.
struct Segment {
    std::string text;
    CharStyle style;
};

bool is_blank(char c) { return c == ' ' || c == '\t'; }

// Les octets non ASCII comptent comme des lettres : « é_x » n est pas une
// emphase, pas plus que « nom_de_variable ».
bool is_word_byte(char c) {
    const auto u = static_cast<unsigned char>(c);
    return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u >= 0x80;
}

std::string strip(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && is_blank(s[b])) ++b;
    while (e > b && is_blank(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Ajoute `text` a la suite des segments, en fusionnant avec le dernier s il
// porte le meme style.
void append(std::vector<Segment>& out, const std::string& text, const CharStyle& style) {
    if (text.empty()) return;
    if (!out.empty() && out.back().style == style) {
        out.back().text += text;
        return;
    }
    out.push_back(Segment{text, style});
}

// Cherche la fermeture d un marqueur ouvert en `open` (`count` fois `c`). Rend
// npos si elle n existe pas. Le contenu doit etre non vide et ne pas commencer
// ni finir par un espace (regle CommonMark : « 3 * 4 * 5 » n est pas une
// emphase). Un `_` ne se ferme pas au milieu d un mot.
std::size_t find_closing(const std::string& s, std::size_t open, char c, std::size_t count) {
    const std::size_t content = open + count;
    const std::string marker(count, c);
    std::size_t p = content;
    while ((p = s.find(marker, p)) != std::string::npos) {
        const bool nonempty = p > content;
        const bool tight = nonempty && !is_blank(s[p - 1]);
        // Une fermeture simple ne doit pas etre la moitie d une paire.
        const bool run_ok =
            count > 1 || (s[p - 1] != c && (p + 1 >= s.size() || s[p + 1] != c));
        const bool word_ok =
            c != '_' || p + count >= s.size() || !is_word_byte(s[p + count]);
        if (tight && run_ok && word_ok) return p;
        ++p;
    }
    return std::string::npos;
}

// Decoupe `s` en segments de style, recursivement pour les emphases imbriquees.
void parse_inline(const std::string& s, const CharStyle& base, std::vector<Segment>& out) {
    static const std::string kPunctuation = "!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";

    std::string plain;
    auto flush = [&] {
        append(out, plain, base);
        plain.clear();
    };

    std::size_t i = 0;
    while (i < s.size()) {
        const char c = s[i];

        // Echappement : le caractere suivant est du texte brut.
        if (c == '\\' && i + 1 < s.size() && kPunctuation.find(s[i + 1]) != std::string::npos) {
            plain += s[i + 1];
            i += 2;
            continue;
        }

        // Code en ligne : contenu verbatim, sans emphase a l interieur.
        if (c == '`') {
            const std::size_t close = s.find('`', i + 1);
            if (close != std::string::npos && close > i + 1) {
                flush();
                append(out, s.substr(i + 1, close - i - 1), base);
                i = close + 1;
                continue;
            }
        }

        // Lien [texte](url) et image ![alt](url) : seul le texte est garde.
        const std::size_t bracket = (c == '!' && i + 1 < s.size() && s[i + 1] == '[') ? i + 1 : i;
        if (bracket < s.size() && s[bracket] == '[') {
            const std::size_t rb = s.find(']', bracket + 1);
            if (rb != std::string::npos && rb + 1 < s.size() && s[rb + 1] == '(') {
                const std::size_t rp = s.find(')', rb + 2);
                if (rp != std::string::npos) {
                    flush();
                    parse_inline(s.substr(bracket + 1, rb - bracket - 1), base, out);
                    i = rp + 1;
                    continue;
                }
            }
        }

        // Barre : marqueurs retires, pas de champ de modele pour le trait.
        if (c == '~' && i + 1 < s.size() && s[i + 1] == '~') {
            const std::size_t close = find_closing(s, i, '~', 2);
            if (close != std::string::npos) {
                flush();
                parse_inline(s.substr(i + 2, close - i - 2), base, out);
                i = close + 2;
                continue;
            }
        }

        // Gras (**x**, __x__) et italique (*x*, _x_).
        if (c == '*' || c == '_') {
            const std::size_t count = (i + 1 < s.size() && s[i + 1] == c) ? 2 : 1;
            const bool word_start = c == '*' || i == 0 || !is_word_byte(s[i - 1]);
            const bool content_ok = i + count < s.size() && !is_blank(s[i + count]);
            if (word_start && content_ok) {
                const std::size_t close = find_closing(s, i, c, count);
                if (close != std::string::npos) {
                    CharStyle inner = base;
                    if (count == 2) {
                        inner.bold = true;
                    } else {
                        inner.italic = true;
                    }
                    flush();
                    parse_inline(s.substr(i + count, close - i - count), inner, out);
                    i = close + count;
                    continue;
                }
            }
        }

        plain += c;
        ++i;
    }
    flush();
}

std::vector<Segment> inline_of(const std::string& text) {
    std::vector<Segment> segments;
    parse_inline(text, CharStyle{}, segments);
    return segments;
}

// Construit un paragraphe a partir de ses segments. On passe par paragraph_insert
// pour que les plages soient celles que produit le reste du programme.
Paragraph build(const std::vector<Segment>& segments, HeadingLevel heading, ListKind list) {
    Paragraph p;
    p.heading = heading;
    p.list = list;
    for (const Segment& seg : segments) {
        paragraph_insert(p, p.text.size(), seg.text, &seg.style);
    }
    return p;
}

// Ligne de regle horizontale : trois marques identiques ou plus, espaces permis.
bool is_rule(const std::string& t) {
    char mark = 0;
    std::size_t count = 0;
    for (const char c : t) {
        if (is_blank(c)) continue;
        if (c != '-' && c != '*' && c != '_') return false;
        if (mark == 0) {
            mark = c;
        } else if (c != mark) {
            return false;
        }
        ++count;
    }
    return count >= 3;
}

// Ligne de separation d un tableau : « |---|:--:| ».
bool is_table_rule(const std::string& t) {
    bool has_dash = false;
    for (const char c : t) {
        if (c == '-') {
            has_dash = true;
        } else if (c != '|' && c != ':' && !is_blank(c)) {
            return false;
        }
    }
    return has_dash;
}

// Cellules d une ligne de tableau : « | a | b | » -> {"a", "b"}. Le « | » de
// tete et celui de queue sont facultatifs ; une cellule vide reste une cellule.
std::vector<std::string> split_cells(const std::string& row) {
    std::vector<std::string> cells;
    std::size_t cell = row.empty() || row[0] != '|' ? 0 : 1;
    while (cell <= row.size()) {
        std::size_t bar = row.find('|', cell);
        if (bar == std::string::npos) bar = row.size();
        cells.push_back(strip(row.substr(cell, bar - cell)));
        cell = bar + 1;
    }
    if (row.size() > 1 && row.back() == '|' && cells.back().empty()) cells.pop_back();
    return cells;
}

} // namespace

std::vector<Paragraph> markdown_to_blocks(const std::string& markdown) {
    std::vector<Paragraph> blocks;
    bool in_fence = false;
    // Pas de lignes vides en rafale, ni en tete : une ligne vide ne produit un
    // paragraphe vide que si la precedente ne l etait pas deja.
    bool last_blank = true;

    auto add = [&](Paragraph&& p) {
        last_blank = p.text.empty() && p.is_plain();
        blocks.push_back(std::move(p));
    };
    auto blank = [&] {
        if (!last_blank) add(Paragraph{});
    };

    std::vector<std::string> lines;
    {
        std::size_t start = 0;
        while (true) {
            const std::size_t end = markdown.find('\n', start);
            std::string line = markdown.substr(start, end == std::string::npos
                                                          ? std::string::npos
                                                          : end - start);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            if (end == std::string::npos) break;
            start = end + 1;
        }
    }

    for (std::size_t li = 0; li < lines.size(); ++li) {
        const std::string& line = lines[li];
        const std::string t0 = strip(line);

        // Bloc de code : les lignes sont du texte brut, sans analyse.
        if (t0.rfind("```", 0) == 0 || t0.rfind("~~~", 0) == 0) {
            in_fence = !in_fence;
        } else if (in_fence) {
            if (t0.empty()) {
                blank();
            } else {
                add(build({Segment{line, CharStyle{}}}, HeadingLevel::None, ListKind::None));
            }
        } else {
            // Citations : le marqueur saute, le texte suit la meme logique.
            std::string t = t0;
            while (!t.empty() && t[0] == '>') t = strip(t.substr(1));

            if (t.empty()) {
                blank();
            } else if (is_rule(t)) {
                blank();
            } else if (t[0] == '#') {
                std::size_t n = 0;
                while (n < t.size() && t[n] == '#') ++n;
                if (n <= 6 && (n == t.size() || is_blank(t[n]))) {
                    std::string content = strip(t.substr(n));
                    while (!content.empty() && content.back() == '#') content.pop_back();
                    content = strip(content);
                    if (content.empty()) {
                        blank();
                    } else {
                        const HeadingLevel level = n == 1   ? HeadingLevel::H1
                                                   : n == 2 ? HeadingLevel::H2
                                                            : HeadingLevel::H3;
                        add(build(inline_of(content), level, ListKind::None));
                    }
                } else {
                    add(build(inline_of(t), HeadingLevel::None, ListKind::None));
                }
            } else if (t[0] == '|' && li + 1 < lines.size() &&
                       is_table_rule(strip(lines[li + 1]))) {
                // Tableau : la ligne d en-tete, la separation, puis toutes les
                // lignes qui commencent par « | ». Chaque cellule devient un
                // paragraphe de tableau (cf. Paragraph::table_id).
                std::vector<std::vector<std::string>> rows{split_cells(t)};
                li += 2; // en-tete et separation
                while (li < lines.size()) {
                    const std::string row = strip(lines[li]);
                    if (row.empty() || row[0] != '|') break;
                    rows.push_back(split_cells(row));
                    ++li;
                }
                --li; // la boucle avancera de nouveau

                std::size_t cols = 0;
                for (const auto& row : rows) cols = std::max(cols, row.size());
                for (std::size_t r = 0; r < rows.size(); ++r) {
                    for (std::size_t c = 0; c < cols; ++c) {
                        const std::string value = c < rows[r].size() ? rows[r][c] : "";
                        std::vector<Segment> segments = inline_of(value);
                        if (r == 0) {
                            // La premiere ligne est l en-tete : en gras, comme
                            // dans le rendu Markdown usuel.
                            for (Segment& seg : segments) seg.style.bold = true;
                        }
                        Paragraph cell = build(segments, HeadingLevel::None, ListKind::None);
                        cell.table_id = 1; // rebase par TextEdit::insert_blocks
                        cell.table_row = static_cast<std::uint32_t>(r);
                        cell.table_col = static_cast<std::uint32_t>(c);
                        cell.table_cols = static_cast<std::uint32_t>(cols);
                        add(std::move(cell));
                    }
                }
            } else if (t.size() >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') &&
                       is_blank(t[1])) {
                add(build(inline_of(strip(t.substr(2))), HeadingLevel::None, ListKind::Bullet));
            } else {
                std::size_t digits = 0;
                while (digits < t.size() && t[digits] >= '0' && t[digits] <= '9') ++digits;
                if (digits > 0 && digits + 1 < t.size() &&
                    (t[digits] == '.' || t[digits] == ')') && is_blank(t[digits + 1])) {
                    add(build(inline_of(strip(t.substr(digits + 2))), HeadingLevel::None,
                              ListKind::Numbered));
                } else {
                    add(build(inline_of(t), HeadingLevel::None, ListKind::None));
                }
            }
        }
    }

    // Les lignes vides de queue ne doivent pas creer d espace en fin de collage.
    while (!blocks.empty() && blocks.back().text.empty() && blocks.back().is_plain()) {
        blocks.pop_back();
    }
    if (blocks.empty()) blocks.emplace_back();
    return blocks;
}

} // namespace gw::doc
