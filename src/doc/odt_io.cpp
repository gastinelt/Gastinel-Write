#include "doc/odt_io.h"

#include "doc/file_bytes.h"
#include "doc/zip.h"

#include <cstdint>

namespace gw::doc {
namespace {

constexpr char kMimeType[] = "application/vnd.oasis.opendocument.text";

// --- Generation XML -------------------------------------------------------
// Le XML est ecrit et relu a la main. Le sous-ensemble est assez petit pour
// que ce soit raisonnable, et cest la contrainte du projet : aucune
// dependance externe, donc pas de pugixml ni de tinyxml.

std::string escape_xml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    } else {
        out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
        out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
    }
}

// Entites XML predefinies plus les references numeriques. Les entites
// personnalisees (declarees par une DTD) ne sont pas gerees : le sous-ensemble
// nen produit pas et un .odt nen contient pas en pratique.
std::string unescape_xml(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '&') {
            out.push_back(text[i]);
            ++i;
            continue;
        }
        const std::size_t end = text.find(';', i);
        if (end == std::string::npos || end - i > 10) {
            // Esperluette isolee : on la conserve telle quelle plutot que de
            // perdre du contenu.
            out.push_back('&');
            ++i;
            continue;
        }
        const std::string entity = text.substr(i + 1, end - i - 1);
        if (entity == "amp") out.push_back('&');
        else if (entity == "lt") out.push_back('<');
        else if (entity == "gt") out.push_back('>');
        else if (entity == "quot") out.push_back('"');
        else if (entity == "apos") out.push_back('\'');
        else if (entity.size() > 1 && entity[0] == '#') {
            const bool hex = entity[1] == 'x' || entity[1] == 'X';
            const std::string digits = entity.substr(hex ? 2 : 1);
            std::uint32_t cp = 0;
            bool ok = !digits.empty();
            for (char d : digits) {
                int v = -1;
                if (d >= '0' && d <= '9') v = d - '0';
                else if (hex && d >= 'a' && d <= 'f') v = d - 'a' + 10;
                else if (hex && d >= 'A' && d <= 'F') v = d - 'A' + 10;
                if (v < 0) { ok = false; break; }
                cp = cp * (hex ? 16u : 10u) + static_cast<std::uint32_t>(v);
            }
            if (ok && cp != 0 && cp <= 0x10FFFF) append_utf8(out, cp);
            else out += "&" + entity + ";";
        } else {
            out += "&" + entity + ";"; // entite inconnue : laissee intacte
        }
        i = end + 1;
    }
    return out;
}

// Attributs de mise en forme de paragraphe, dans notre propre espace de noms.
// Les valeurs par defaut ne sont PAS ecrites : un paragraphe sans mise en forme
// produit exactement le meme <text:p> quavant ce chantier, donc un document
// sans style donne un fichier identique a ceux des versions precedentes.
std::string paragraph_attributes(const Paragraph& p) {
    std::string out;
    switch (p.heading) {
        case HeadingLevel::H1: out += " gw:heading=\"1\""; break;
        case HeadingLevel::H2: out += " gw:heading=\"2\""; break;
        case HeadingLevel::H3: out += " gw:heading=\"3\""; break;
        case HeadingLevel::None: break;
    }
    switch (p.list) {
        case ListKind::Bullet: out += " gw:list=\"bullet\""; break;
        case ListKind::Numbered: out += " gw:list=\"number\""; break;
        case ListKind::None: break;
    }
    switch (p.align) {
        case Alignment::Center: out += " gw:align=\"center\""; break;
        case Alignment::Right: out += " gw:align=\"right\""; break;
        case Alignment::Left: break;
    }
    return out;
}

// Contenu dun paragraphe : le texte, decoupe en <text:span> partout ou une
// plage porte un style. Une plage au style par defaut sort en texte nu, sans
// balise — cest ce qui garde les fichiers courts et lisibles a loeil.
std::string paragraph_content(const Paragraph& p) {
    p.ensure_decompressed();
    if (p.spans.empty()) return escape_xml(p.text);

    std::string out;
    std::size_t offset = 0;
    for (const StyleSpan& span : p.spans) {
        const std::string slice = p.text.substr(offset, span.length);
        offset += span.length;
        if (span.style.is_default()) {
            out += escape_xml(slice);
            continue;
        }
        out += "<text:span";
        if (span.style.bold) out += " gw:b=\"1\"";
        if (span.style.italic) out += " gw:i=\"1\"";
        if (span.style.underline) out += " gw:u=\"1\"";
        if (!span.style.font.empty()) out += " gw:font=\"" + escape_xml(span.style.font) + "\"";
        out += ">" + escape_xml(slice) + "</text:span>";
    }
    return out;
}

std::string build_content_xml(const Document& doc) {
    std::string xml;
    xml += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += "<office:document-content";
    xml += " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\"";
    xml += " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\"";
    // Espace de noms maison. Les attributs de mise en forme y sont declares
    // plutot que sous text:, fo: ou style: parce quils NE SONT PAS de lODF :
    // les ecrire sous un prefixe ODF les ferait passer pour conformes, et un
    // lecteur strict aurait raison de les refuser. Sous gw:, un lecteur tiers
    // les ignore proprement et lit le texte — exactement le comportement voulu
    // (cf. len-tete de odt_io.h).
    xml += " xmlns:gw=\"urn:gastinel:write:format:1.0\"";
    xml += " office:version=\"1.2\">\n";
    xml += " <office:body>\n  <office:text>\n";

    for (const Paragraph& p : doc.paragraphs()) {
        const std::string attributes = paragraph_attributes(p);
        if (p.text.empty()) {
            xml += "   <text:p" + attributes + "/>\n";
        } else {
            xml += "   <text:p" + attributes + ">" + paragraph_content(p) + "</text:p>\n";
        }
    }

    xml += "  </office:text>\n </office:body>\n</office:document-content>\n";
    return xml;
}

// styles.xml est present mais vide de tout style : le modele de document ne
// porte encore aucune mise en forme. Le fichier existe uniquement parce que
// des lecteurs ODF le supposent present.
std::string build_styles_xml() {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<office:document-styles"
           " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
           " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
           " office:version=\"1.2\"/>\n";
}

std::string build_manifest_xml() {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<manifest:manifest"
           " xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\""
           " manifest:version=\"1.2\">\n"
           " <manifest:file-entry manifest:full-path=\"/\""
           " manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>\n"
           " <manifest:file-entry manifest:full-path=\"content.xml\""
           " manifest:media-type=\"text/xml\"/>\n"
           " <manifest:file-entry manifest:full-path=\"styles.xml\""
           " manifest:media-type=\"text/xml\"/>\n"
           "</manifest:manifest>\n";
}

// Retire les balises restantes dun contenu de paragraphe et decode les
// entites. Utile si un paragraphe contient des <text:span> : on garde le
// texte, on jette le balisage, ce qui correspond exactement au modele plat
// actuel.
std::string strip_tags_and_unescape(const std::string& fragment) {
    std::string raw;
    raw.reserve(fragment.size());
    bool in_tag = false;
    for (char c : fragment) {
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; continue; }
        if (!in_tag) raw.push_back(c);
    }
    return unescape_xml(raw);
}

// Valeur dun attribut dans une balise ouvrante complete (« <text:p gw:b="1"> »).
// Chaine vide si absent. Les valeurs sont desechappees : un nom de police peut
// contenir une esperluette.
//
// Ce nest pas un lecteur dattributs XML general : il exige des guillemets
// doubles, ce que notre ecrivain produit toujours. Un fichier tiers qui
// utiliserait des apostrophes verrait son attribut ignore, donc relu sans
// style : la degradation attendue, pas une erreur.
std::string attribute_value(const std::string& tag, const std::string& name) {
    const std::string needle = " " + name + "=\"";
    const std::size_t start = tag.find(needle);
    if (start == std::string::npos) return {};
    const std::size_t from = start + needle.size();
    const std::size_t end = tag.find('"', from);
    if (end == std::string::npos) return {};
    return unescape_xml(tag.substr(from, end - from));
}

void apply_paragraph_attributes(const std::string& tag, Paragraph& p) {
    const std::string heading = attribute_value(tag, "gw:heading");
    if (heading == "1") p.heading = HeadingLevel::H1;
    else if (heading == "2") p.heading = HeadingLevel::H2;
    else if (heading == "3") p.heading = HeadingLevel::H3;

    const std::string list = attribute_value(tag, "gw:list");
    if (list == "bullet") p.list = ListKind::Bullet;
    else if (list == "number") p.list = ListKind::Numbered;

    const std::string align = attribute_value(tag, "gw:align");
    if (align == "center") p.align = Alignment::Center;
    else if (align == "right") p.align = Alignment::Right;
    // Toute valeur inconnue laisse le defaut en place : un fichier abime ne
    // doit pas rendre un document illisible.
}

void append_run(Paragraph& p, const std::string& text, const CharStyle& style) {
    if (text.empty()) return;
    p.text += text;
    p.spans.push_back(StyleSpan{text.size(), style});
}

// Decoupe le contenu dun <text:p> en plages.
//
// La lecture est guidee par les <text:span> : tout ce qui se trouve entre deux
// spans est du texte au style par defaut, balises inconnues retirees. Cest ce
// qui permet de relire un .odt tiers — nos spans a nous ny sont pas, ses
// attributs a lui sont ignores, et il ressort en texte brut sans style, ce qui
// est le perimetre annonce.
void parse_paragraph_content(const std::string& inner, Paragraph& p) {
    const std::string span_open = "<text:span";
    const std::string span_close = "</text:span>";

    std::size_t pos = 0;
    while (pos < inner.size()) {
        const std::size_t open = inner.find(span_open, pos);
        if (open == std::string::npos) {
            append_run(p, strip_tags_and_unescape(inner.substr(pos)), CharStyle{});
            break;
        }
        append_run(p, strip_tags_and_unescape(inner.substr(pos, open - pos)), CharStyle{});

        const std::size_t tag_end = inner.find('>', open);
        if (tag_end == std::string::npos) break;
        const std::string tag = inner.substr(open, tag_end - open + 1);

        if (inner[tag_end - 1] == '/') { // <text:span/> : vide
            pos = tag_end + 1;
            continue;
        }

        std::size_t close = inner.find(span_close, tag_end);
        const bool closed = close != std::string::npos;
        if (!closed) close = inner.size();

        CharStyle style;
        style.bold = attribute_value(tag, "gw:b") == "1";
        style.italic = attribute_value(tag, "gw:i") == "1";
        style.underline = attribute_value(tag, "gw:u") == "1";
        style.font = attribute_value(tag, "gw:font");

        append_run(p, strip_tags_and_unescape(inner.substr(tag_end + 1, close - tag_end - 1)),
                   style);
        pos = closed ? close + span_close.size() : inner.size();
    }
    // Forme canonique : plages vides retirees, voisines identiques fusionnees,
    // liste videe si tout est au style par defaut.
    style_normalize(p.spans, p.text.size());
}

// Extraction des <text:p> par balayage textuel.
//
// Ce nest pas un parseur XML : pas de validation, pas de gestion des espaces
// de noms, pas de CDATA. Cest suffisant et honnete pour le sous-ensemble
// quon produit nous-memes. Relire un .odt ecrit par LibreOffice est hors
// perimetre (cf. README).
void extract_paragraphs(const std::string& xml, Document& out) {
    out.clear();

    std::size_t pos = 0;
    while (true) {
        const std::size_t open = xml.find("<text:p", pos);
        if (open == std::string::npos) break;

        // Verifie quon a bien <text:p et non <text:page-number ou autre.
        const std::size_t after = open + 7;
        if (after >= xml.size()) break;
        const char next = xml[after];
        if (next != '>' && next != '/' && next != ' ' && next != '\t' && next != '\n' &&
            next != '\r') {
            pos = after;
            continue;
        }

        const std::size_t tag_end = xml.find('>', open);
        if (tag_end == std::string::npos) break;

        const std::string open_tag = xml.substr(open, tag_end - open + 1);

        if (xml[tag_end - 1] == '/') {
            // <text:p/> : paragraphe vide. Il peut porter des attributs de
            // paragraphe malgre tout (une ligne vide centree, une puce vide).
            Paragraph empty;
            apply_paragraph_attributes(open_tag, empty);
            out.paragraphs().push_back(std::move(empty));
            pos = tag_end + 1;
            continue;
        }

        const std::size_t close = xml.find("</text:p>", tag_end);
        if (close == std::string::npos) break;

        Paragraph paragraph;
        apply_paragraph_attributes(open_tag, paragraph);
        parse_paragraph_content(xml.substr(tag_end + 1, close - tag_end - 1), paragraph);
        out.paragraphs().push_back(std::move(paragraph));
        pos = close + 9;
    }
}

} // namespace

bool save_odt(const std::string& path, const Document& doc, std::string& error) {
    ZipWriter zip;
    // `mimetype` DOIT etre la premiere entree et rester non compressee : cest
    // ce qui permet a un outil de reconnaitre le type en lisant les premiers
    // octets du fichier, sans decompresser. ZipWriter ecrivant tout en
    // "stored", il suffit ici de lajouter en premier.
    zip.add("mimetype", kMimeType);
    zip.add("META-INF/manifest.xml", build_manifest_xml());
    zip.add("styles.xml", build_styles_xml());
    zip.add("content.xml", build_content_xml(doc));

    return write_file_binary(path, zip.build(), error);
}

bool load_odt(const std::string& path, Document& out, std::string& error) {
    std::string bytes;
    if (!read_file_binary(path, bytes, error)) return false;

    ZipReader zip;
    if (!zip.open(std::move(bytes), error)) {
        error = "lecture .odt impossible : " + error;
        return false;
    }

    std::string content;
    if (!zip.extract("content.xml", content, error)) {
        error = "lecture .odt impossible : " + error;
        return false;
    }

    extract_paragraphs(content, out);
    if (out.paragraphs().empty()) {
        // Un document sans aucun <text:p> reste un document valide : il a un
        // paragraphe vide, comme un fichier texte vide.
        out.paragraphs().push_back(Paragraph{});
    }
    out.set_dirty(false);
    return true;
}

} // namespace gw::doc
