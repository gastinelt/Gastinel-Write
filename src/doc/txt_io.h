// doc/txt_io.h — lecture/ecriture de fichiers texte brut (.txt).
//
// ENCODAGE, en lecture (heuristique, cf. txt_io.cpp pour le detail) :
//   1. BOM UTF-8            -> retire, contenu lu en UTF-8
//   2. BOM UTF-16 LE ou BE  -> refus explicite avec message (non gere)
//   3. sans BOM, UTF-8 valide -> lu en UTF-8
//   4. sans BOM, UTF-8 invalide -> suppose CP1252, transcode en UTF-8
//
// Ce nest pas une detection dencodage fiable et cela nen a pas la pretention :
// aucune heuristique ne peut lever lambiguite dans le cas general. Elle est
// juste conservatrice — un fichier UTF-8 valide nest jamais mal interprete,
// puisque la validation UTF-8 ne produit quasiment jamais de faux positif sur
// du CP1252 reel.
//
// ENCODAGE, en ecriture : toujours UTF-8 SANS BOM.
//   Un BOM en UTF-8 est optionnel, non recommande par Unicode, et casse plus
//   doutils quil nen aide (scripts shell, compilateurs, diff). Les outils
//   Windows modernes, Bloc-notes compris, lisent lUTF-8 sans BOM.
//
// FINS DE LIGNE :
//   Lecture  : CRLF, LF et CR isole sont tous acceptes comme separateurs. Le
//              modele en memoire ne contient aucun caractere de fin de ligne,
//              la structure en paragraphes le remplace.
//   Ecriture : CRLF. Cest la convention Windows, et cest ce quattend le
//              Bloc-notes historique. La cible du projet etant Windows
//              uniquement, ecrire du LF nuirait sans rien apporter.
#pragma once

#include <string>

#include "doc/document.h"

namespace gw::doc {

bool load_txt(const std::string& path, Document& out, std::string& error);

bool save_txt(const std::string& path, const Document& doc, std::string& error);

// Exposes pour les tests et pour odt_io : conversion dun tampon complet.
bool decode_text_bytes(const std::string& bytes, std::string& utf8_out, std::string& error);
void split_into_paragraphs(const std::string& utf8, Document& out);

} // namespace gw::doc
