#include "doc/txt_io.h"

#include "doc/file_bytes.h"

#include <cstdint>

namespace gw::doc {
namespace {

// Table des 32 positions ou CP1252 seloigne de Latin-1 (plage 0x80..0x9F,
// occupee par des caracteres de controle en Latin-1 et par de la ponctuation
// en CP1252). 0 signifie "non attribue" ; on le remplace alors par U+FFFD.
constexpr std::uint32_t kCp1252High[32] = {
    0x20AC, 0x0000, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x0000, 0x017D, 0x0000,
    0x0000, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x0000, 0x017E, 0x0178,
};

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

// Validation UTF-8 stricte : longueurs correctes, octets de continuation
// corrects, pas de surencodage, pas de demi-paire de substitution, pas de code
// point au-dela de U+10FFFF.
//
// La strictesse est le point important : cest elle qui fait que du CP1252
// reel (accents isoles, apostrophes typographiques) echoue presque toujours a
// la validation, et bascule donc dans la bonne branche.
bool is_valid_utf8(const std::string& s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int extra = 0;
        std::uint32_t cp = 0;
        std::uint32_t lower_bound = 0;

        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) { extra = 1; cp = c & 0x1Fu; lower_bound = 0x80; }
        else if ((c & 0xF0) == 0xE0) { extra = 2; cp = c & 0x0Fu; lower_bound = 0x800; }
        else if ((c & 0xF8) == 0xF0) { extra = 3; cp = c & 0x07u; lower_bound = 0x10000; }
        else return false; // 0x80..0xBF isole, ou 0xF8..0xFF : jamais valides

        if (i + static_cast<std::size_t>(extra) >= s.size()) return false;
        for (int k = 1; k <= extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (cp < lower_bound) return false;                    // surencodage
        if (cp > 0x10FFFF) return false;                       // hors Unicode
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;        // demi-paire

        i += static_cast<std::size_t>(extra) + 1;
    }
    return true;
}

std::string cp1252_to_utf8(const std::string& s) {
    std::string out;
    out.reserve(s.size() + s.size() / 4);
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0xA0) {
            const std::uint32_t cp = kCp1252High[c - 0x80];
            append_utf8(out, cp != 0 ? cp : 0xFFFDu);
        } else {
            // 0xA0..0xFF : identiques a Latin-1, donc au code point lui-meme.
            append_utf8(out, c);
        }
    }
    return out;
}

} // namespace

bool decode_text_bytes(const std::string& bytes, std::string& utf8_out, std::string& error) {
    // 1. BOM UTF-8
    if (bytes.size() >= 3 && static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        utf8_out = bytes.substr(3);
        return true;
    }

    // 2. BOM UTF-16 : refus explicite plutot que decodage silencieux errone.
    //    Le Bloc-notes historique produisait ce format sous le nom "Unicode" ;
    //    le support viendra si le besoin se presente.
    if (bytes.size() >= 2) {
        const unsigned char b0 = static_cast<unsigned char>(bytes[0]);
        const unsigned char b1 = static_cast<unsigned char>(bytes[1]);
        if ((b0 == 0xFF && b1 == 0xFE) || (b0 == 0xFE && b1 == 0xFF)) {
            error = "fichier UTF-16 (BOM detecte) : format non gere, convertissez-le en UTF-8";
            return false;
        }
    }

    // 3 et 4. Sans BOM : UTF-8 si le contenu est valide, CP1252 sinon.
    utf8_out = is_valid_utf8(bytes) ? bytes : cp1252_to_utf8(bytes);
    return true;
}

void split_into_paragraphs(const std::string& utf8, Document& out) {
    out.clear();
    if (utf8.empty()) {
        out.paragraphs().push_back(Paragraph{});
        return;
    }

    // Estimation du nombre de paragraphes pour eviter les reallocations
    std::size_t estimated = 1;
    for (char c : utf8) {
        if (c == '\n') ++estimated;
    }
    out.paragraphs().reserve(estimated);

    std::size_t line_start = 0;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const char c = utf8[i];
        if (c == '\r') {
            // CRLF compte pour une seule coupure ; un CR isole (vieux Mac) en
            // vaut une aussi.
            out.paragraphs().push_back(Paragraph{utf8.substr(line_start, i - line_start)});
            ++i;
            if (i < utf8.size() && utf8[i] == '\n') ++i;
            line_start = i;
        } else if (c == '\n') {
            out.paragraphs().push_back(Paragraph{utf8.substr(line_start, i - line_start)});
            ++i;
            line_start = i;
        } else {
            ++i;
        }
    }
    // Le dernier fragment est un paragraphe des lors que le fichier nest pas
    // vide. Un fichier se terminant par un saut de ligne ne produit donc PAS
    // de paragraphe vide final : "a\n" et "a" donnent le meme document.
    if (line_start < utf8.size() || out.paragraphs().empty()) {
        out.paragraphs().push_back(Paragraph{utf8.substr(line_start)});
    }
}

bool load_txt(const std::string& path, Document& out, std::string& error) {
    std::string bytes;
    if (!read_file_binary(path, bytes, error)) return false;

    std::string utf8;
    if (!decode_text_bytes(bytes, utf8, error)) return false;

    split_into_paragraphs(utf8, out);
    out.set_dirty(false);
    return true;
}

bool save_txt(const std::string& path, const Document& doc, std::string& error) {
    std::string data;
    const auto& paragraphs = doc.paragraphs();
    for (std::size_t i = 0; i < paragraphs.size(); ++i) {
        paragraphs[i].ensure_decompressed();
        if (i > 0) data += "\r\n";
        data += paragraphs[i].text;
    }
    return write_file_binary(path, data, error);
}

} // namespace gw::doc
