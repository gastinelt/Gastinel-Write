// doc/markdown_import.h — conversion d un texte Markdown complet en paragraphes.
//
// MODULE PUR, comme doc/markdown_live.h : ni Win32, ni pixels, ni police. Il sert
// au collage : un texte copie depuis un fichier .md arrive ici et ressort sous
// forme de doc::Paragraph, avec les memes champs que la barre de mise en forme
// (`heading`, `list`, plages de style). Il n y a toujours pas de second format.
//
// CE QUI EST CONVERTI, et rien d autre :
//   # / ## / ### et plus      -> H1 / H2 / H3 (au-dela : H3)
//   - / * / +  en tete        -> puce
//   1. / 1)  en tete          -> liste numerotee
//   **gras**, __gras__        -> gras
//   *italique*, _italique_    -> italique
//   ~~barre~~                 -> texte seul (pas de champ pour le barre)
//   `code`                    -> texte seul (pas de police a chasse fixe)
//   [texte](url), ![alt](url) -> texte seul, l URL est perdue
//   > citation                -> texte seul, sans le marqueur
//   ``` blocs de code        -> lignes en texte brut, sans mise en forme
//   tableau (en-tete, separation, lignes |a|b|) -> une cellule par paragraphe de
//                               tableau ; la premiere ligne est en gras
//   ---  (regle)             -> ligne vide
//
// Les formes douteuses restent du texte : un `_` au milieu d un mot, un `*`
// sans fermeture, une etoile bordee d espaces ne declenchent rien. Meme regle
// que doc/markdown_live : en cas de doute, ne rien retirer.
#pragma once

#include <string>
#include <vector>

#include "doc/document.h"

namespace gw::doc {

// Convertit `markdown` (texte UTF-8, fins de ligne LF ou CRLF) en une liste de
// paragraphes prets a etre inseres. Ne rend jamais une liste vide pour un texte
// non vide.
std::vector<Paragraph> markdown_to_blocks(const std::string& markdown);

} // namespace gw::doc
