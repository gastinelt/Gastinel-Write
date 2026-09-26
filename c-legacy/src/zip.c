#include "zip.h"
#include "inflate.h"

#define SIG_LOCAL   0x04034B50UL
#define SIG_CENTRAL 0x02014B50UL
#define SIG_EOCD    0x06054B50UL
#define DOS_DATE_1980 0x21 /* 1980-01-01, date MS-DOS minimale valide */

void zip_writer_init(ZipWriter *w)
{
    buf_init(&w->out);
    w->count = 0;
}

void zip_writer_add(ZipWriter *w, const char *name, const u8 *data, u32 len)
{
    Buf *o = &w->out;
    u32 name_len = gw_strlen(name);
    int i = w->count;
    if (i >= ZIP_MAX_ENTRIES) { o->oom = 1; return; }

    w->name[i] = name;
    w->offset[i] = o->len;
    w->crc[i] = crc32(data, len);
    w->size[i] = len;
    w->count++;

    buf_u32le(o, SIG_LOCAL);
    buf_u16le(o, 10);            /* version minimale pour "stored" */
    buf_u16le(o, 0);             /* drapeaux */
    buf_u16le(o, 0);             /* methode stored */
    buf_u16le(o, 0);             /* heure */
    buf_u16le(o, DOS_DATE_1980);
    buf_u32le(o, w->crc[i]);
    buf_u32le(o, len);
    buf_u32le(o, len);
    buf_u16le(o, (u16)name_len);
    buf_u16le(o, 0);             /* extra */
    buf_append(o, name, name_len);
    buf_append(o, data, len);
}

int zip_writer_finish(ZipWriter *w)
{
    Buf *o = &w->out;
    u32 central = o->len, central_size, name_len;
    int i;
    for (i = 0; i < w->count; ++i) {
        name_len = gw_strlen(w->name[i]);
        buf_u32le(o, SIG_CENTRAL);
        buf_u16le(o, 20);        /* version de creation */
        buf_u16le(o, 10);        /* version minimale de lecture */
        buf_u16le(o, 0);
        buf_u16le(o, 0);         /* stored */
        buf_u16le(o, 0);
        buf_u16le(o, DOS_DATE_1980);
        buf_u32le(o, w->crc[i]);
        buf_u32le(o, w->size[i]);
        buf_u32le(o, w->size[i]);
        buf_u16le(o, (u16)name_len);
        buf_u16le(o, 0);         /* extra */
        buf_u16le(o, 0);         /* commentaire */
        buf_u16le(o, 0);         /* disque */
        buf_u16le(o, 0);         /* attributs internes */
        buf_u32le(o, 0);         /* attributs externes */
        buf_u32le(o, w->offset[i]);
        buf_append(o, w->name[i], name_len);
    }
    central_size = o->len - central;
    buf_u32le(o, SIG_EOCD);
    buf_u16le(o, 0);             /* numero de disque */
    buf_u16le(o, 0);             /* disque du repertoire central */
    buf_u16le(o, (u16)w->count);
    buf_u16le(o, (u16)w->count);
    buf_u32le(o, central_size);
    buf_u32le(o, central);
    buf_u16le(o, 0);             /* pas de commentaire */
    return !o->oom;
}

static int get16(const u8 *d, u32 len, u32 off, u32 *v)
{
    if (off + 2 > len || off + 2 < off) return 0;
    *v = d[off] | ((u32)d[off + 1] << 8);
    return 1;
}

static int get32(const u8 *d, u32 len, u32 off, u32 *v)
{
    if (off + 4 > len || off + 4 < off) return 0;
    *v = d[off] | ((u32)d[off + 1] << 8) | ((u32)d[off + 2] << 16) | ((u32)d[off + 3] << 24);
    return 1;
}

const char *zip_extract(const u8 *d, u32 len, const char *name, Buf *out)
{
    u32 eocd = 0, back, max_back, sig, count, pos, i;
    u32 method, crc, csize, usize, nlen, xlen, clen, lho, start, before;
    u32 want = gw_strlen(name);
    int found = 0;
    const char *err;

    if (len < 22) return "archive trop courte";

    /* La fin de repertoire central se cherche a rebours : elle suit un
       commentaire d'archive de 65535 octets au plus. */
    max_back = len < 65557UL ? len : 65557UL;
    for (back = 22; back <= max_back; ++back) {
        if (get32(d, len, len - back, &sig) && sig == SIG_EOCD) {
            eocd = len - back;
            found = 1;
            break;
        }
    }
    if (!found) return "ce n'est pas une archive zip";
    if (!get16(d, len, eocd + 10, &count) || !get32(d, len, eocd + 16, &pos))
        return "fin de repertoire central tronquee";

    for (i = 0; i < count; ++i) {
        if (!get32(d, len, pos, &sig) || sig != SIG_CENTRAL) return "repertoire central invalide";
        if (!get16(d, len, pos + 10, &method) || !get32(d, len, pos + 16, &crc) ||
            !get32(d, len, pos + 20, &csize) || !get32(d, len, pos + 24, &usize) ||
            !get16(d, len, pos + 28, &nlen) || !get16(d, len, pos + 30, &xlen) ||
            !get16(d, len, pos + 32, &clen) || !get32(d, len, pos + 42, &lho))
            return "repertoire central tronque";
        if (pos + 46 + nlen > len) return "nom d'entree hors archive";

        if (nlen == want && gw_equal(d + pos + 46, name, want)) {
            if (method != 0 && method != 8) return "methode de compression non geree";

            /* Les longueurs de nom/extra de l'en-tete local peuvent differer
               de celles du repertoire central : on relit celles du local. */
            if (!get32(d, len, lho, &sig) || sig != SIG_LOCAL ||
                !get16(d, len, lho + 26, &nlen) || !get16(d, len, lho + 28, &xlen))
                return "en-tete local invalide";
            start = lho + 30 + nlen + xlen;
            before = out->len;

            if (method == 0) {
                if (start + usize > len || start + usize < start) return "donnees hors archive";
                buf_append(out, d + start, usize);
                if (out->oom) return "memoire insuffisante";
            } else {
                if (start + csize > len || start + csize < start) return "donnees hors archive";
                if (!buf_reserve(out, usize)) return "memoire insuffisante";
                err = inflate_raw(d + start, csize, out, GW_INFLATE_MAX_OUTPUT);
                if (err) return err;
                if (out->len - before != usize) { out->len = before; return "taille decompressee inattendue"; }
            }
            if (crc != 0 && crc32(out->data + before, usize) != crc) {
                out->len = before;
                return "CRC-32 incorrect : donnees corrompues";
            }
            return 0;
        }
        pos += 46 + nlen + xlen + clen;
    }
    return "entree absente de l'archive";
}
