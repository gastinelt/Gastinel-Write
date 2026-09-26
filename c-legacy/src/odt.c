#include "odt.h"
#include "zip.h"

static const char kMimeType[] = "application/vnd.oasis.opendocument.text";

static const char kManifest[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
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

static const char kStyles[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<office:document-styles"
    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
    " office:version=\"1.2\"/>\n";

/* Identique a l'en-tete C++, espace de noms gw: compris, pour que les
   fichiers soient indiscernables de ceux de la version C++. */
static const char kContentHead[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<office:document-content"
    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
    " xmlns:gw=\"urn:gastinel:write:format:1.0\""
    " office:version=\"1.2\">\n"
    " <office:body>\n  <office:text>\n";

static const char kContentTail[] =
    "  </office:text>\n </office:body>\n</office:document-content>\n";

static void escape_xml(Buf *b, const u8 *s, u32 n)
{
    u32 i;
    for (i = 0; i < n; ++i) {
        switch (s[i]) {
        case '&': buf_str(b, "&amp;"); break;
        case '<': buf_str(b, "&lt;"); break;
        case '>': buf_str(b, "&gt;"); break;
        case '"': buf_str(b, "&quot;"); break;
        default:  buf_push(b, s[i]); break;
        }
    }
}

int odt_build(const u8 *text, u32 len, Buf *out)
{
    Buf content;
    ZipWriter zip;
    u32 start = 0, end, i;
    int ok;

    buf_init(&content);
    buf_str(&content, kContentHead);
    for (i = 0; i <= len; ++i) {
        if (i < len && text[i] != '\n') continue;
        end = i;
        if (end > start && text[end - 1] == '\r') --end;
        if (end == start) {
            buf_str(&content, "   <text:p/>\n");
        } else {
            buf_str(&content, "   <text:p>");
            escape_xml(&content, text + start, end - start);
            buf_str(&content, "</text:p>\n");
        }
        start = i + 1;
    }
    buf_str(&content, kContentTail);

    /* `mimetype` DOIT etre premier et non compresse : un outil reconnait le
       type en lisant les premiers octets du fichier. */
    zip_writer_init(&zip);
    zip_writer_add(&zip, "mimetype", (const u8 *)kMimeType, gw_strlen(kMimeType));
    zip_writer_add(&zip, "META-INF/manifest.xml", (const u8 *)kManifest, gw_strlen(kManifest));
    zip_writer_add(&zip, "styles.xml", (const u8 *)kStyles, gw_strlen(kStyles));
    zip_writer_add(&zip, "content.xml", content.data, content.len);
    ok = !content.oom && zip_writer_finish(&zip);
    buf_free(&content);

    if (!ok) { buf_free(&zip.out); return 0; }
    *out = zip.out;
    return 1;
}

static void put_utf8(Buf *b, u32 cp)
{
    if (cp < 0x80) {
        buf_push(b, (u8)cp);
    } else if (cp < 0x800) {
        buf_push(b, (u8)(0xC0 | (cp >> 6)));
        buf_push(b, (u8)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        buf_push(b, (u8)(0xE0 | (cp >> 12)));
        buf_push(b, (u8)(0x80 | ((cp >> 6) & 0x3F)));
        buf_push(b, (u8)(0x80 | (cp & 0x3F)));
    } else {
        buf_push(b, (u8)(0xF0 | (cp >> 18)));
        buf_push(b, (u8)(0x80 | ((cp >> 12) & 0x3F)));
        buf_push(b, (u8)(0x80 | ((cp >> 6) & 0x3F)));
        buf_push(b, (u8)(0x80 | (cp & 0x3F)));
    }
}

static int starts_with(const u8 *s, u32 n, const char *prefix)
{
    u32 k = gw_strlen(prefix);
    return n >= k && gw_equal(s, prefix, k);
}

/* Decode une entite a partir de s[0] == '&'. Renvoie le nombre d'octets
   consommes, ou 0 si ce n'est pas une entite reconnue. */
static u32 entity(const u8 *s, u32 n, Buf *out)
{
    u32 end, i, cp = 0;
    int hex, ok = 1;
    for (end = 1; end < n && end <= 10 && s[end] != ';'; ++end) {}
    if (end >= n || s[end] != ';') return 0;

    if (end == 4 && starts_with(s + 1, 3, "amp")) { buf_push(out, '&'); return 5; }
    if (end == 3 && starts_with(s + 1, 2, "lt"))  { buf_push(out, '<'); return 4; }
    if (end == 3 && starts_with(s + 1, 2, "gt"))  { buf_push(out, '>'); return 4; }
    if (end == 5 && starts_with(s + 1, 4, "quot")) { buf_push(out, '"'); return 6; }
    if (end == 5 && starts_with(s + 1, 4, "apos")) { buf_push(out, '\''); return 6; }
    if (end < 3 || s[1] != '#') return 0;

    hex = s[2] == 'x' || s[2] == 'X';
    for (i = hex ? 3 : 2; i < end; ++i) {
        u8 c = s[i];
        int v = -1;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (hex && c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (hex && c >= 'A' && c <= 'F') v = c - 'A' + 10;
        if (v < 0) { ok = 0; break; }
        cp = cp * (hex ? 16 : 10) + (u32)v;
        if (cp > 0x10FFFF) { ok = 0; break; }
    }
    if (!ok || i == (u32)(hex ? 3 : 2) || cp == 0) return 0;
    put_utf8(out, cp);
    return end + 1;
}

/* Valeur entiere de text:c="n" dans une balise <text:s .../>, 1 par defaut. */
static u32 space_count(const u8 *tag, u32 n)
{
    u32 i, v = 0;
    for (i = 0; i + 8 <= n; ++i) {
        if (starts_with(tag + i, n - i, "text:c=\"")) {
            for (i += 8; i < n && tag[i] >= '0' && tag[i] <= '9' && v < 10000; ++i)
                v = v * 10 + (tag[i] - '0');
            return v ? v : 1;
        }
    }
    return 1;
}

/* Contenu d'un paragraphe : balises retirees (sauf tab/saut/espaces), entites decodees. */
static void paragraph_text(const u8 *s, u32 n, Buf *out)
{
    u32 i = 0, j, k, used;
    while (i < n) {
        if (s[i] == '<') {
            for (j = i; j < n && s[j] != '>'; ++j) {}
            if (j >= n) break;
            if (starts_with(s + i, j - i, "<text:tab")) buf_push(out, '\t');
            else if (starts_with(s + i, j - i, "<text:line-break")) buf_push(out, '\n');
            else if (starts_with(s + i, j - i, "<text:s") &&
                     (s[i + 7] == '/' || s[i + 7] == ' ' || s[i + 7] == '>'))
                for (k = space_count(s + i, j - i); k; --k) buf_push(out, ' ');
            i = j + 1;
        } else if (s[i] == '&' && (used = entity(s + i, n - i, out)) != 0) {
            i += used;
        } else {
            buf_push(out, s[i++]);
        }
    }
}

static int is_delim(u8 c)
{
    return c == '>' || c == '/' || c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* Balayage textuel des <text:p> et <text:h> : pas un parseur XML complet,
   suffisant pour nos fichiers et pour un .odt tiers simple. */
static void extract_paragraphs(const u8 *x, u32 n, Buf *out)
{
    u32 i = 0, tag_end, close, count = 0;
    char kind;
    while (i + 8 < n) {
        if (x[i] != '<' || !starts_with(x + i, n - i, "<text:") ||
            (x[i + 6] != 'p' && x[i + 6] != 'h') || !is_delim(x[i + 7])) {
            ++i;
            continue;
        }
        kind = (char)x[i + 6];
        for (tag_end = i; tag_end < n && x[tag_end] != '>'; ++tag_end) {}
        if (tag_end >= n) break;

        if (count++) buf_push(out, '\n');
        if (x[tag_end - 1] == '/') { i = tag_end + 1; continue; }

        for (close = tag_end + 1; close + 9 <= n; ++close) {
            if (x[close] == '<' && x[close + 1] == '/' &&
                starts_with(x + close + 2, n - close - 2, "text:") &&
                x[close + 7] == (u8)kind && x[close + 8] == '>')
                break;
        }
        if (close + 9 > n) close = n;
        paragraph_text(x + tag_end + 1, close - tag_end - 1, out);
        i = close + 9;
    }
}

/* " gw:" (espace devant) ne correspond qu'aux attributs, pas a la
   declaration xmlns:gw de la racine. */
static int has_gw_attribute(const u8 *x, u32 n)
{
    u32 i;
    for (i = 0; i + 4 <= n; ++i)
        if (x[i] == ' ' && x[i + 1] == 'g' && x[i + 2] == 'w' && x[i + 3] == ':') return 1;
    return 0;
}

const char *odt_read(const u8 *zip, u32 len, Buf *out, int *formatted)
{
    Buf content;
    const char *err;
    buf_init(&content);
    err = zip_extract(zip, len, "content.xml", &content);
    if (!err) {
        if (formatted) *formatted = has_gw_attribute(content.data, content.len);
        extract_paragraphs(content.data, content.len, out);
        if (out->oom) err = "memoire insuffisante";
    }
    buf_free(&content);
    return err;
}
