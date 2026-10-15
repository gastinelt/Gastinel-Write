#include "doc/style.h"

#include "doc/document.h"
#include "compress/compress_lz.h"

#include <algorithm>

namespace gw::doc {
namespace {

// Deplie une liste canonique (eventuellement vide) en une liste explicite qui
// couvre `text_length` octets. Travailler sur une forme explicite evite de
// traiter le cas « liste vide » dans chaque operation ; style_normalize()
// ramene ensuite a la forme canonique.
std::vector<StyleSpan> expanded(const std::vector<StyleSpan>& spans, std::size_t text_length) {
    std::vector<StyleSpan> out = spans;
    style_normalize(out, text_length);
    if (out.empty() && text_length > 0) out.push_back(StyleSpan{text_length, CharStyle{}});
    return out;
}

// Decoupe la liste pour qu une frontiere de plage existe exactement a
// `offset`. Sans effet si `offset` est deja une frontiere, ou hors du texte.
void split_at(std::vector<StyleSpan>& spans, std::size_t offset) {
    if (offset == 0) return;
    std::size_t seen = 0;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const std::size_t end = seen + spans[i].length;
        if (offset == end) return;   // deja une frontiere
        if (offset < end) {
            StyleSpan tail{end - offset, spans[i].style};
            spans[i].length = offset - seen;
            spans.insert(spans.begin() + static_cast<std::ptrdiff_t>(i) + 1, std::move(tail));
            return;
        }
        seen = end;
    }
}

} // namespace

// --- Comparaisons -----------------------------------------------------------

bool operator==(const CharStyle& a, const CharStyle& b) {
    return a.bold == b.bold && a.italic == b.italic && a.underline == b.underline &&
           a.font == b.font;
}
bool operator!=(const CharStyle& a, const CharStyle& b) { return !(a == b); }

bool operator==(const StyleSpan& a, const StyleSpan& b) {
    return a.length == b.length && a.style == b.style;
}

// --- Normalisation ----------------------------------------------------------

void style_normalize(std::vector<StyleSpan>& spans, std::size_t text_length) {
    // 1. Plages vides : elles n ont aucun sens et casseraient la fusion.
    spans.erase(std::remove_if(spans.begin(), spans.end(),
                               [](const StyleSpan& s) { return s.length == 0; }),
                spans.end());

    // 2. Recalage sur la taille reelle du texte. Trop long : on tronque. Trop
    //    court : on complete au style par defaut. Aucun des deux ne devrait
    //    arriver depuis le moteur d edition — mais un fichier relu peut mentir,
    //    et un modele incoherent doit se reparer, pas se propager.
    std::size_t seen = 0;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        if (seen >= text_length) {
            spans.erase(spans.begin() + static_cast<std::ptrdiff_t>(i), spans.end());
            break;
        }
        spans[i].length = std::min(spans[i].length, text_length - seen);
        seen += spans[i].length;
    }
    if (seen < text_length) spans.push_back(StyleSpan{text_length - seen, CharStyle{}});

    // 3. Fusion des voisines identiques.
    std::vector<StyleSpan> merged;
    merged.reserve(spans.size());
    for (StyleSpan& s : spans) {
        if (!merged.empty() && merged.back().style == s.style) {
            merged.back().length += s.length;
        } else {
            merged.push_back(std::move(s));
        }
    }

    // 4. Forme canonique : un paragraphe sans mise en forme n a pas de plages.
    if (merged.empty() || (merged.size() == 1 && merged[0].style.is_default())) {
        spans.clear();
        return;
    }
    spans = std::move(merged);
}

// --- Lecture ----------------------------------------------------------------

CharStyle style_at(const std::vector<StyleSpan>& spans, std::size_t offset) {
    std::size_t seen = 0;
    for (const StyleSpan& s : spans) {
        if (offset < seen + s.length) return s.style;
        seen += s.length;
    }
    return CharStyle{};
}

CharStyle style_before(const std::vector<StyleSpan>& spans, std::size_t offset) {
    if (spans.empty()) return CharStyle{};
    if (offset == 0) return spans.front().style;
    // `offset - 1` designe forcement un octet du caractere precedent : une
    // plage ne peut pas etre vide, donc l octet appartient bien a la plage
    // qu on cherche, meme si la position n est pas une frontiere de plage.
    return style_at(spans, offset - 1);
}

bool style_range_all(const std::vector<StyleSpan>& spans, std::size_t text_length,
                     std::size_t begin, std::size_t end,
                     const std::function<bool(const CharStyle&)>& predicate) {
    begin = std::min(begin, text_length);
    end = std::min(end, text_length);
    if (begin >= end) return false; // plage vide : rien a affirmer dessus

    if (spans.empty()) return predicate(CharStyle{});

    std::size_t seen = 0;
    for (const StyleSpan& s : spans) {
        const std::size_t span_end = seen + s.length;
        if (span_end > begin && seen < end) {
            if (!predicate(s.style)) return false;
        }
        seen = span_end;
        if (seen >= end) break;
    }
    return true;
}

// --- Ecriture ---------------------------------------------------------------

void style_apply(std::vector<StyleSpan>& spans, std::size_t text_length, std::size_t begin,
                 std::size_t end, const std::function<void(CharStyle&)>& mutate) {
    begin = std::min(begin, text_length);
    end = std::min(end, text_length);
    if (begin >= end) return;

    std::vector<StyleSpan> work = expanded(spans, text_length);
    split_at(work, begin);
    split_at(work, end);

    std::size_t seen = 0;
    for (StyleSpan& s : work) {
        const std::size_t span_end = seen + s.length;
        // Grace aux deux coupures, toute plage intersectant [begin, end) y est
        // entierement contenue : le test sur son debut suffit.
        if (seen >= begin && span_end <= end) mutate(s.style);
        seen = span_end;
    }

    style_normalize(work, text_length);
    spans = std::move(work);
}

Paragraph::Paragraph(const Paragraph& other) {
    other.ensure_decompressed();
    text = other.text;
    heading = other.heading;
    list = other.list;
    align = other.align;
    spans = other.spans;
    table_id = other.table_id;
    table_row = other.table_row;
    table_col = other.table_col;
    table_cols = other.table_cols;
    version = other.version;
    compressed_text = other.compressed_text;
    uncompressed_size = other.uncompressed_size;
}

Paragraph& Paragraph::operator=(const Paragraph& other) {
    if (this != &other) {
        other.ensure_decompressed();
        text = other.text;
        heading = other.heading;
        list = other.list;
        align = other.align;
        spans = other.spans;
        table_id = other.table_id;
        table_row = other.table_row;
        table_col = other.table_col;
        table_cols = other.table_cols;
        version = other.version;
        compressed_text = other.compressed_text;
        uncompressed_size = other.uncompressed_size;
    }
    return *this;
}

void Paragraph::compress() const {
    if (text.size() >= 64 && compressed_text.empty()) {
        compressed_text = compress::lz_compress(text.data(), text.size());
        uncompressed_size = text.size();
        text.clear();
        text.shrink_to_fit();
    }
}

void Paragraph::decompress() const {
    if (!compressed_text.empty()) {
        text = compress::lz_decompress_string(compressed_text.data(), compressed_text.size(), uncompressed_size);
        compressed_text.clear();
        compressed_text.shrink_to_fit();
    }
}

// --- Mutations coordonnees (declarees dans doc/document.h) --------------------

void paragraph_insert(Paragraph& p, std::size_t offset, const std::string& chunk,
                      const CharStyle* forced) {
    if (chunk.empty()) return;
    p.ensure_decompressed();
    ++p.version;
    offset = std::min(offset, p.text.size());

    const std::size_t before = p.text.size();
    const CharStyle inherited = forced != nullptr ? *forced : style_before(p.spans, offset);

    p.text.insert(offset, chunk);

    // Cas frequent : rien a decrire. Un paragraphe sans plages ou l on insere
    // du texte sans style reste un paragraphe sans plages.
    if (p.spans.empty() && inherited.is_default()) return;

    std::vector<StyleSpan> work = expanded(p.spans, before);
    split_at(work, offset);

    // Insertion de la nouvelle plage a la frontiere. On la pose apres la plage
    // qui se termine en `offset` : c est ce qui fait que le texte tape a la fin
    // d un mot gras continue en gras plutot que de rouvrir la plage suivante.
    std::size_t seen = 0;
    std::size_t index = work.size();
    for (std::size_t i = 0; i < work.size(); ++i) {
        if (seen == offset) { index = i; break; }
        seen += work[i].length;
    }
    if (offset == 0) index = 0;
    work.insert(work.begin() + static_cast<std::ptrdiff_t>(index),
                StyleSpan{chunk.size(), inherited});

    style_normalize(work, p.text.size());
    p.spans = std::move(work);
}

void paragraph_erase(Paragraph& p, std::size_t offset, std::size_t count) {
    p.ensure_decompressed();
    offset = std::min(offset, p.text.size());
    count = std::min(count, p.text.size() - offset);
    if (count == 0) return;
    ++p.version;

    const std::size_t before = p.text.size();
    p.text.erase(offset, count);

    if (p.spans.empty()) return; // aucun style a recalculer

    std::vector<StyleSpan> work = expanded(p.spans, before);
    split_at(work, offset);
    split_at(work, offset + count);

    std::vector<StyleSpan> kept;
    kept.reserve(work.size());
    std::size_t seen = 0;
    for (StyleSpan& s : work) {
        const std::size_t span_end = seen + s.length;
        const bool inside = seen >= offset && span_end <= offset + count;
        if (!inside) kept.push_back(std::move(s));
        seen = span_end;
    }

    style_normalize(kept, p.text.size());
    p.spans = std::move(kept);
}

Paragraph paragraph_split(Paragraph& p, std::size_t offset) {
    p.ensure_decompressed();
    offset = std::min(offset, p.text.size());
    ++p.version;

    Paragraph tail;
    tail.copy_paragraph_attributes_from(p);
    tail.text = p.text.substr(offset);

    if (!p.spans.empty()) {
        std::vector<StyleSpan> work = expanded(p.spans, p.text.size());
        split_at(work, offset);

        std::vector<StyleSpan> head;
        std::size_t seen = 0;
        for (StyleSpan& s : work) {
            if (seen < offset) {
                head.push_back(s);
            } else {
                tail.spans.push_back(s);
            }
            seen += s.length;
        }
        style_normalize(head, offset);
        p.spans = std::move(head);
        style_normalize(tail.spans, tail.text.size());
    }

    p.text.erase(offset);
    return tail;
}

void paragraph_append(Paragraph& p, const Paragraph& tail) {
    tail.ensure_decompressed();
    if (tail.text.empty()) return;
    p.ensure_decompressed();
    ++p.version;

    std::vector<StyleSpan> work = expanded(p.spans, p.text.size());
    const std::vector<StyleSpan> tail_spans = expanded(tail.spans, tail.text.size());
    work.insert(work.end(), tail_spans.begin(), tail_spans.end());

    p.text += tail.text;
    style_normalize(work, p.text.size());
    p.spans = std::move(work);
}

std::size_t table_begin(const std::vector<Paragraph>& paragraphs, std::size_t index) {
    if (index >= paragraphs.size() || !paragraphs[index].in_table()) return index;
    const std::uint32_t id = paragraphs[index].table_id;
    std::size_t first = index;
    while (first > 0 && paragraphs[first - 1].table_id == id) --first;
    return first;
}

std::size_t table_end(const std::vector<Paragraph>& paragraphs, std::size_t index) {
    if (index >= paragraphs.size() || !paragraphs[index].in_table()) return index + 1;
    const std::uint32_t id = paragraphs[index].table_id;
    std::size_t after = index + 1;
    while (after < paragraphs.size() && paragraphs[after].table_id == id) ++after;
    return after;
}

std::uint32_t next_table_id(const std::vector<Paragraph>& paragraphs) {
    std::uint32_t highest = 0;
    for (const Paragraph& p : paragraphs) {
        if (p.table_id > highest) highest = p.table_id;
    }
    return highest + 1;
}

} // namespace gw::doc
