/* odt.h - sous-ensemble ODT, porte du C++ (src/doc/odt_io.cpp).
 *
 * Cette declinaison n'a pas de mise en forme : elle ecrit un <text:p> par ligne,
 * exactement le content.xml que produit la version C++ pour un document sans
 * style. Les .odt se lisent donc dans les deux sens entre les deux versions ;
 * la mise en forme (attributs gw:) d'un fichier C++ est ignoree a la lecture
 * et perdue si on le reenregistre ici.
 *
 * Lecture : <text:p> et <text:h>, plus <text:tab/>, <text:line-break/> et
 * <text:s/> pour relire proprement un fichier LibreOffice simple. */
#ifndef GW_ODT_H
#define GW_ODT_H

#include "core.h"

/* Texte UTF-8 (lignes separees par \n ou \r\n) -> archive .odt dans `out`. */
int odt_build(const u8 *text, u32 len, Buf *out);

/* Archive .odt -> texte UTF-8, paragraphes separes par \n, ajoute a `out`.
   `formatted` (optionnel) passe a 1 si le fichier porte une mise en forme
   Gastinel (attributs gw:) que cette version ne conservera pas.
   Renvoie 0 si succes, sinon un message d'erreur statique. */
const char *odt_read(const u8 *zip, u32 len, Buf *out, int *formatted);

#endif
