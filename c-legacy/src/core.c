#include "core.h"

void buf_init(Buf *b)
{
    b->data = 0;
    b->len = b->cap = 0;
    b->oom = 0;
}

void buf_free(Buf *b)
{
    if (b->data) gw_free(b->data);
    buf_init(b);
}

int buf_reserve(Buf *b, u32 extra)
{
    u32 need, cap;
    u8 *p;
    if (b->oom) return 0;
    need = b->len + extra;
    if (need < b->len) { b->oom = 1; return 0; }
    if (need <= b->cap) return 1;
    cap = b->cap ? b->cap : 256;
    while (cap < need) {
        if (cap > 0x7FFFFFFFUL) { cap = need; break; }
        cap *= 2;
    }
    p = (u8 *)(b->data ? gw_realloc(b->data, cap) : gw_alloc(cap));
    if (!p) { b->oom = 1; return 0; }
    b->data = p;
    b->cap = cap;
    return 1;
}

void buf_push(Buf *b, u8 c)
{
    if (b->len < b->cap || buf_reserve(b, 1)) b->data[b->len++] = c;
}

void buf_append(Buf *b, const void *data, u32 len)
{
    if (len && buf_reserve(b, len)) {
        gw_copy(b->data + b->len, data, len);
        b->len += len;
    }
}

void buf_str(Buf *b, const char *s)
{
    buf_append(b, s, gw_strlen(s));
}

void buf_u16le(Buf *b, u16 v)
{
    buf_push(b, (u8)(v & 0xFF));
    buf_push(b, (u8)(v >> 8));
}

void buf_u32le(Buf *b, u32 v)
{
    buf_u16le(b, (u16)(v & 0xFFFF));
    buf_u16le(b, (u16)(v >> 16));
}

void gw_copy(void *dst, const void *src, u32 n)
{
    u8 *d = (u8 *)dst;
    const u8 *s = (const u8 *)src;
    while (n--) *d++ = *s++;
}

int gw_equal(const void *a, const void *b, u32 n)
{
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    while (n--) if (*x++ != *y++) return 0;
    return 1;
}

u32 gw_strlen(const char *s)
{
    const char *p = s;
    while (*p) ++p;
    return (u32)(p - s);
}

u32 crc32(const u8 *data, u32 len)
{
    static u32 table[256];
    static int ready = 0;
    u32 crc = 0xFFFFFFFFUL, i;
    if (!ready) {
        for (i = 0; i < 256; ++i) {
            u32 c = i;
            int k;
            for (k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320UL ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = 1;
    }
    for (i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFUL;
}
