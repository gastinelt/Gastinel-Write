#include "inflate.h"

static const u16 kLengthBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
                                    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const u8 kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const u16 kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
                                  193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
                                  6145, 8193, 12289, 16385, 24577};
static const u8 kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                  6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const u8 kCodeLengthOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
                                        11, 4, 12, 3, 13, 2, 14, 1, 15};

#define MAX_BITS 15

/* Huffman canonique a la facon de "puff" : nombre de codes par longueur,
   symboles tries par longueur puis valeur. Decodage bit a bit. */
typedef struct {
    u16 count[MAX_BITS + 1];
    u16 symbol[288];
} Huffman;

typedef struct {
    const u8   *in;
    u32         in_size;
    u32         in_pos;
    u32         bitbuf;
    int         bitcnt;
    Buf        *out;
    u32         start;      /* debut de notre sortie dans `out` */
    u32         max_output;
    const char *error;
} Inflater;

/* 0 = complet, >0 = incomplet, <0 = sur-souscrit (invalide). */
static int build_huffman(Huffman *h, const u8 *lengths, int n)
{
    u16 offs[MAX_BITS + 1];
    int s, len, left;

    for (len = 0; len <= MAX_BITS; ++len) h->count[len] = 0;
    for (s = 0; s < n; ++s) h->count[lengths[s]]++;
    if (h->count[0] == n) return 0;

    left = 1;
    for (len = 1; len <= MAX_BITS; ++len) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }

    offs[1] = 0;
    for (len = 1; len < MAX_BITS; ++len) offs[len + 1] = (u16)(offs[len] + h->count[len]);
    for (s = 0; s < n; ++s)
        if (lengths[s] != 0) h->symbol[offs[lengths[s]]++] = (u16)s;
    return left;
}

static int fail(Inflater *z, const char *msg)
{
    if (!z->error) z->error = msg;
    return 0;
}

/* Lecture de `need` bits, poids faible d'abord (convention DEFLATE). */
static int bits(Inflater *z, int need, u32 *out)
{
    u32 v = z->bitbuf;
    while (z->bitcnt < need) {
        if (z->in_pos >= z->in_size) return fail(z, "flux DEFLATE tronque");
        v |= (u32)z->in[z->in_pos++] << z->bitcnt;
        z->bitcnt += 8;
    }
    z->bitbuf = v >> need;
    z->bitcnt -= need;
    *out = v & ((1UL << need) - 1UL);
    return 1;
}

static int decode(Inflater *z, const Huffman *h, int *symbol)
{
    int code = 0, first = 0, index = 0, len, count;
    u32 bit;
    for (len = 1; len <= MAX_BITS; ++len) {
        if (!bits(z, 1, &bit)) return 0;
        code |= (int)bit;
        count = h->count[len];
        if (code - first < count) {
            *symbol = h->symbol[index + (code - first)];
            return 1;
        }
        index += count;
        first = (first + count) << 1;
        code <<= 1;
    }
    return fail(z, "code Huffman invalide");
}

static u32 produced(const Inflater *z)
{
    return z->out->len - z->start;
}

static int stored_block(Inflater *z)
{
    u32 len, nlen;
    z->bitbuf = 0;
    z->bitcnt = 0;
    if (z->in_pos + 4 > z->in_size) return fail(z, "bloc stored tronque");
    len  = z->in[z->in_pos] | ((u32)z->in[z->in_pos + 1] << 8);
    nlen = z->in[z->in_pos + 2] | ((u32)z->in[z->in_pos + 3] << 8);
    z->in_pos += 4;
    if ((len ^ 0xFFFFUL) != nlen) return fail(z, "bloc stored : LEN et NLEN incoherents");
    if (z->in_pos + len > z->in_size) return fail(z, "bloc stored : donnees tronquees");
    if (produced(z) + len > z->max_output) return fail(z, "sortie decompressee trop volumineuse");
    buf_append(z->out, z->in + z->in_pos, len);
    z->in_pos += len;
    return !z->out->oom || fail(z, "memoire insuffisante");
}

static int codes(Inflater *z, const Huffman *lencode, const Huffman *distcode)
{
    int symbol, dsym;
    u32 extra, length, distance, from, i;
    for (;;) {
        if (!decode(z, lencode, &symbol)) return 0;
        if (symbol < 256) {
            if (produced(z) >= z->max_output) return fail(z, "sortie decompressee trop volumineuse");
            buf_push(z->out, (u8)symbol);
            if (z->out->oom) return fail(z, "memoire insuffisante");
            continue;
        }
        if (symbol == 256) return 1;

        symbol -= 257;
        if (symbol >= 29) return fail(z, "code de longueur invalide");
        if (!bits(z, kLengthExtra[symbol], &extra)) return 0;
        length = kLengthBase[symbol] + extra;

        if (!decode(z, distcode, &dsym)) return 0;
        if (dsym >= 30) return fail(z, "code de distance invalide");
        if (!bits(z, kDistExtra[dsym], &extra)) return 0;
        distance = kDistBase[dsym] + extra;

        /* Sans ce test, un flux forge lirait avant le debut du tampon. */
        if (distance > produced(z)) return fail(z, "distance de reference hors limites");
        if (produced(z) + length > z->max_output) return fail(z, "sortie decompressee trop volumineuse");
        if (!buf_reserve(z->out, length)) return fail(z, "memoire insuffisante");

        /* Octet par octet : source et destination peuvent se chevaucher. */
        from = z->out->len - distance;
        for (i = 0; i < length; ++i) z->out->data[z->out->len++] = z->out->data[from + i];
    }
}

static int fixed_block(Inflater *z)
{
    static Huffman lencode, distcode;
    static int ready = 0;
    if (!ready) {
        u8 lengths[288];
        int i;
        for (i = 0; i < 144; ++i) lengths[i] = 8;
        for (; i < 256; ++i) lengths[i] = 9;
        for (; i < 280; ++i) lengths[i] = 7;
        for (; i < 288; ++i) lengths[i] = 8;
        build_huffman(&lencode, lengths, 288);
        for (i = 0; i < 30; ++i) lengths[i] = 5;
        build_huffman(&distcode, lengths, 30);
        ready = 1;
    }
    return codes(z, &lencode, &distcode);
}

static int dynamic_block(Inflater *z)
{
    /* static : ~1,2 Ko, evite une sonde de pile (__chkstk) sans CRT. */
    static Huffman lencode, litcode, distcode;
    u8 lengths[288 + 30];
    u32 v;
    int nlen, ndist, ncode, i, index, symbol, repeat, err;
    u8 rep;

    if (!bits(z, 5, &v)) return 0;
    nlen = (int)v + 257;
    if (!bits(z, 5, &v)) return 0;
    ndist = (int)v + 1;
    if (!bits(z, 4, &v)) return 0;
    ncode = (int)v + 4;
    if (nlen > 286 || ndist > 30) return fail(z, "en-tete de bloc dynamique invalide");

    for (i = 0; i < 19; ++i) lengths[i] = 0;
    for (i = 0; i < ncode; ++i) {
        if (!bits(z, 3, &v)) return 0;
        lengths[kCodeLengthOrder[i]] = (u8)v;
    }
    if (build_huffman(&lencode, lengths, 19) != 0)
        return fail(z, "alphabet des longueurs de code incomplet");

    index = 0;
    while (index < nlen + ndist) {
        if (!decode(z, &lencode, &symbol)) return 0;
        if (symbol < 16) {
            lengths[index++] = (u8)symbol;
            continue;
        }
        rep = 0;
        if (symbol == 16) {
            if (index == 0) return fail(z, "repetition sans longueur precedente");
            rep = lengths[index - 1];
            if (!bits(z, 2, &v)) return 0;
            repeat = 3 + (int)v;
        } else if (symbol == 17) {
            if (!bits(z, 3, &v)) return 0;
            repeat = 3 + (int)v;
        } else {
            if (!bits(z, 7, &v)) return 0;
            repeat = 11 + (int)v;
        }
        if (index + repeat > nlen + ndist) return fail(z, "repetition de longueurs debordante");
        while (repeat-- > 0) lengths[index++] = rep;
    }
    if (lengths[256] == 0) return fail(z, "code de fin de bloc absent");

    /* Un code incomplet n'est tolere que s'il ne comporte qu'un symbole. */
    err = build_huffman(&litcode, lengths, nlen);
    if (err != 0 && (err < 0 || nlen != litcode.count[0] + litcode.count[1]))
        return fail(z, "alphabet litteral/longueur invalide");
    err = build_huffman(&distcode, lengths + nlen, ndist);
    if (err != 0 && (err < 0 || ndist != distcode.count[0] + distcode.count[1]))
        return fail(z, "alphabet de distances invalide");

    return codes(z, &litcode, &distcode);
}

const char *inflate_raw(const u8 *data, u32 size, Buf *out, u32 max_output)
{
    Inflater z;
    u32 last = 0, type = 0;
    int ok = 1;

    if (!data || size == 0) return "flux DEFLATE vide";
    z.in = data;
    z.in_size = size;
    z.in_pos = 0;
    z.bitbuf = 0;
    z.bitcnt = 0;
    z.out = out;
    z.start = out->len;
    z.max_output = max_output;
    z.error = 0;

    do {
        if (!bits(&z, 1, &last) || !bits(&z, 2, &type)) { ok = 0; break; }
        switch (type) {
        case 0:  ok = stored_block(&z); break;
        case 1:  ok = fixed_block(&z); break;
        case 2:  ok = dynamic_block(&z); break;
        default: ok = fail(&z, "type de bloc DEFLATE reserve (3)"); break;
        }
    } while (ok && !last);

    if (!ok) {
        out->len = z.start;
        return z.error ? z.error : "erreur de decompression";
    }
    return 0;
}
