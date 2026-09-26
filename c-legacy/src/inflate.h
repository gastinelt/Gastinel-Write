/* inflate.h - decompression DEFLATE (RFC 1951), portee du C++ (src/compress/inflate.cpp).
 * Decompression seulement : l'ecriture zip reste en methode "stored". */
#ifndef GW_INFLATE_H
#define GW_INFLATE_H

#include "core.h"

/* Garde-fou : un flux court peut decrire une sortie enorme. */
#define GW_INFLATE_MAX_OUTPUT (64UL * 1024UL * 1024UL)

/* Flux DEFLATE brut (entree zip de methode 8). Ajoute a `out`.
   Renvoie 0 si succes, sinon un message d'erreur statique. */
const char *inflate_raw(const u8 *data, u32 size, Buf *out, u32 max_output);

#endif
