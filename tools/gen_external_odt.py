#!/usr/bin/env python3
"""Genere le vecteur `external.odt` attendu par gw_doc_roundtrip.

Le fichier ne doit surtout PAS etre produit par notre propre ecrivain ODT :
tout l interet du test est de lire une archive dont les entrees sont en
methode 8 (DEFLATE), ecrite par un outil tiers. `zipfile` de la bibliotheque
standard Python joue ce role — implementation de reference, independante.

Sans ce fichier, le test se contente d afficher « [SKIP] external.odt absent »
et la lecture du DEFLATE n est jamais exercee.

Usage : python tools/gen_external_odt.py <repertoire_de_sortie>
"""
import os
import sys
import zipfile

MIMETYPE = "application/vnd.oasis.opendocument.text"

# Le premier paragraphe est verifie nommement par le test, ainsi que celui qui
# porte les entites XML : ne pas changer ces deux libelles sans mettre a jour
# tools/doc_roundtrip.cpp.
FIRST = "Document produit par un outil tiers, entrees compressees en methode 8."
ESCAPED = '<balise> & "guillemets"'


def escape(text):
    return (text.replace("&", "&amp;").replace("<", "&lt;")
                .replace(">", "&gt;").replace('"', "&quot;"))


def paragraphs():
    out = [FIRST, ESCAPED, ""]
    # Du texte repetitif mais pas uniforme : assez volumineux et assez varie
    # pour que zlib emette un bloc a Huffman dynamique plutot qu un stored.
    for i in range(1, 204):
        out.append(
            "Paragraphe %d — accents : ete, ou, ca, naive. "
            "Ligne de remplissage destinee a donner au flux une taille "
            "representative d un vrai document." % i)
    return out


def content_xml():
    xml = ['<?xml version="1.0" encoding="UTF-8"?>\n',
           '<office:document-content'
           ' xmlns:office="urn:oasis:names:tc:opendocument:xmlns:office:1.0"'
           ' xmlns:text="urn:oasis:names:tc:opendocument:xmlns:text:1.0"'
           ' office:version="1.2">\n',
           ' <office:body>\n  <office:text>\n']
    for p in paragraphs():
        xml.append("   <text:p/>\n" if not p else
                   "   <text:p>%s</text:p>\n" % escape(p))
    xml.append("  </office:text>\n </office:body>\n</office:document-content>\n")
    return "".join(xml)


MANIFEST = (
    '<?xml version="1.0" encoding="UTF-8"?>\n'
    '<manifest:manifest'
    ' xmlns:manifest="urn:oasis:names:tc:opendocument:xmlns:manifest:1.0"'
    ' manifest:version="1.2">\n'
    ' <manifest:file-entry manifest:full-path="/"'
    ' manifest:media-type="%s"/>\n'
    ' <manifest:file-entry manifest:full-path="content.xml"'
    ' manifest:media-type="text/xml"/>\n'
    '</manifest:manifest>\n' % MIMETYPE)


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, "external.odt")
    content = content_xml()

    with zipfile.ZipFile(path, "w") as z:
        # `mimetype` en clair et en tete, comme le veut la specification ODF.
        z.writestr(zipfile.ZipInfo("mimetype"), MIMETYPE,
                   compress_type=zipfile.ZIP_STORED)
        # Les deux autres en methode 8 : c est ce que le test doit lire.
        z.writestr("META-INF/manifest.xml", MANIFEST,
                   compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)
        z.writestr("content.xml", content,
                   compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)

    with zipfile.ZipFile(path) as z:
        info = z.getinfo("content.xml")
        print("%s : %d paragraphes, content.xml %d -> %d octets (methode %d)" %
              (path, len(paragraphs()), info.file_size, info.compress_size,
               info.compress_type))


if __name__ == "__main__":
    main()
