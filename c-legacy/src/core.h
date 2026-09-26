/* core.h - socle commun des modules purs (sans OS, sans CRT).
 * L'allocateur est fourni par l'hote : HeapAlloc dans l'executable Windows,
 * malloc dans les tests. */
#ifndef GW_CORE_H
#define GW_CORE_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;

void *gw_alloc(u32 size);
void *gw_realloc(void *p, u32 size);
void  gw_free(void *p);

/* Tampon d'octets extensible. `oom` passe a 1 au premier echec d'allocation ;
   les ajouts suivants deviennent sans effet, on ne teste qu'a la fin. */
typedef struct {
    u8  *data;
    u32  len;
    u32  cap;
    int  oom;
} Buf;

void buf_init(Buf *b);
void buf_free(Buf *b);
int  buf_reserve(Buf *b, u32 extra);
void buf_push(Buf *b, u8 c);
void buf_append(Buf *b, const void *data, u32 len);
void buf_str(Buf *b, const char *s);
void buf_u16le(Buf *b, u16 v);
void buf_u32le(Buf *b, u32 v);

void gw_copy(void *dst, const void *src, u32 n);
int  gw_equal(const void *a, const void *b, u32 n);
u32  gw_strlen(const char *s);

u32 crc32(const u8 *data, u32 len);

#endif
