/* zip.h - zip minimal, porte du C++ (src/doc/zip.cpp).
 * Ecriture : methode "stored" uniquement. Lecture : "stored" et "deflate".
 * Pas de zip64, de chiffrement ni de descripteur de donnees. */
#ifndef GW_ZIP_H
#define GW_ZIP_H

#include "core.h"

#define ZIP_MAX_ENTRIES 8

typedef struct {
    Buf  out;
    int  count;
    u32  offset[ZIP_MAX_ENTRIES];
    u32  crc[ZIP_MAX_ENTRIES];
    u32  size[ZIP_MAX_ENTRIES];
    const char *name[ZIP_MAX_ENTRIES];
} ZipWriter;

void zip_writer_init(ZipWriter *w);
/* `name` doit rester valide jusqu'a zip_writer_finish. L'ordre d'ajout est
   l'ordre dans l'archive (`mimetype` doit etre premier pour un .odt). */
void zip_writer_add(ZipWriter *w, const char *name, const u8 *data, u32 len);
/* Ecrit le repertoire central. L'archive complete est dans w->out. */
int  zip_writer_finish(ZipWriter *w);

/* Extrait l'entree `name` de l'archive `zip` et l'ajoute a `out`.
   Renvoie 0 si succes, sinon un message d'erreur statique. */
const char *zip_extract(const u8 *zip, u32 zip_len, const char *name, Buf *out);

#endif
