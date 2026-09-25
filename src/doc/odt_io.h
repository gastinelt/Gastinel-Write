// doc/odt_io.h — lecture/ecriture du sous-ensemble ODT maison.
//
// AVERTISSEMENT : le format produit nest PAS de lOpenDocument conforme.
//
// Ce qui est ecrit dans larchive :
//   mimetype               "application/vnd.oasis.opendocument.text", en
//                          premiere position et non compresse
//   META-INF/manifest.xml  les trois entrees, rien de plus
//   styles.xml             racine vide : aucun style nommE nest modelise
//   content.xml            un <text:p> par paragraphe, texte echappe, plus les
//                          attributs de mise en forme decrits ci-dessous
//
// --- SCHEMA DE MISE EN FORME -----------------------------------------------
//
// Tout ce qui porte la mise en forme est dans un espace de noms maison,
// declare sur la racine :
//
//   xmlns:gw="urn:gastinel:write:format:1.0"
//
// POURQUOI UN ESPACE DE NOMS A NOUS plutot que les attributs ODF (text:style-
// name + <office:automatic-styles>) : lODF exprime la mise en forme par des
// styles nommes definis a part, avec heritage et familles. Reproduire ce
// mecanisme pour porter trois booleens serait ecrire un tiers dODF sans
// pouvoir tenir les deux autres tiers — donc produire un fichier qui a lair
// conforme et ne lest pas. Sous gw:, le contrat est explicite : un lecteur
// tiers ignore ces attributs (cest ce que la specification XML impose pour un
// espace de noms inconnu) et lit le texte, ce qui est exactement la degradation
// annoncee.
//
// Mise en forme de PARAGRAPHE, en attributs de <text:p> :
//   gw:heading="1"|"2"|"3"        niveau de titre     (absent = corps de texte)
//   gw:list="bullet"|"number"     type de liste       (absent = pas de liste)
//   gw:align="center"|"right"     alignement          (absent = a gauche)
//
// Mise en forme de CARACTERE, en attributs de <text:span> :
//   gw:b="1"   gras
//   gw:i="1"   italique
//   gw:u="1"   souligne
//   gw:font="<nom affiche>"   police de la plage (absent = police par defaut)
//
// Les valeurs par defaut ne sont JAMAIS ecrites, et une plage au style par
// defaut sort en texte nu sans <text:span>. Consequence directe et voulue : un
// document sans aucune mise en forme produit exactement le meme content.xml
// quavant lajout du formatage. Un .odt ecrit par une version precedente se
// relit donc sans style, et un .odt ecrit aujourdhui sans style est
// indistinguable dun ancien.
//
// Exemple complet dun paragraphe :
//   <text:p gw:heading="2" gw:align="center">Un <text:span gw:b="1"
//     gw:u="1">titre</text:span> centre</text:p>
//
// Ce qui nest volontairement pas fait :
//   - aucune validation contre le schema ODF ;
//   - aucun style nomme, tableau, image, note ou lien ;
//   - aucune taille de police par plage : le corps reste global au document
//     (cf. doc/style.h) ;
//   - pas de <text:s> ni <text:tab> : les espaces multiples et les tabulations
//     sont ecrits tels quels dans le texte. Un lecteur ODF strict a le droit
//     de les normaliser, donc un aller-retour via LibreOffice peut les perdre.
//     Laller-retour Gastinel Write -> Gastinel Write, lui, les conserve.
//
// PERIMETRE DE LECTURE : relire un fichier ecrit par Gastinel Write, avec ou
// sans mise en forme. Relire un .odt produit par LibreOffice ou Word reste
// hors perimetre — le parseur est un balayage textuel des <text:p>, pas un
// parseur XML, et il ninterprete AUCUN style ODF. Un fichier tiers simple
// ressort donc en texte brut sans mise en forme ; cest le comportement voulu,
// mais il nest ni garanti ni teste.
#pragma once

#include <string>

#include "doc/document.h"

namespace gw::doc {

bool load_odt(const std::string& path, Document& out, std::string& error);

bool save_odt(const std::string& path, const Document& doc, std::string& error);

} // namespace gw::doc
