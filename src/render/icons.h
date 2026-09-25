// render/icons.h — icones du chrome, decrites comme des contours.
//
// Les trois boutons de la barre de titre ne sont PAS des images bitmap ni des
// primitives de dessin dediees : ce sont des contours fermes passes au meme
// rasterizer que les glyphes de texte. Un seul chemin de rasterization dans
// tout le programme, donc un seul endroit ou une regression de remplissage
// peut apparaitre.
//
// Les contours sont decrits dans une boite carree de kIconUnitsPerEm unites,
// axe Y vers le haut comme en TrueType, origine en bas a gauche.
#pragma once

#include "font/ttf.h"

namespace gw::render {

// Cote de la boite de dessin des icones, en unites de design.
constexpr std::uint16_t kIconUnitsPerEm = 1000;

enum class IconKind {
    Minimize, // barre horizontale
    Maximize, // carre evide
    Restore,  // carre evide, plus deux traits decales derriere
    Close,    // deux barres diagonales

    // Icones de linterface daccueil.
    Settings, // trois curseurs horizontaux — volontairement pas un engrenage
    File,     // feuille a coin corne, base commune a tous les types de fichier

    // Icones de la barre de mise en forme.
    //
    // SEULS les alignements sont des icones. Gras, italique, souligne, niveaux
    // de titre et marqueurs de liste sont dessines avec de vraies lettres, dans
    // la variante quils appliquent : le bouton Gras montre un B semi-gras, le
    // bouton Italique un I penche. Un pictogramme aurait dit la meme chose en
    // moins bien, et il aurait fallu le redessiner a chaque changement de
    // charte typographique. Lalignement, lui, na pas de lettre qui le
    // represente : cest le seul cas ou licone est le bon outil.
    AlignLeft,   // quatre lignes de texte, drapeau a gauche
    AlignCenter, // quatre lignes de texte, centrees
    AlignRight,  // quatre lignes de texte, drapeau a droite
};

// Construit les contours de licone. Le resultat se rasterise exactement comme
// un glyphe : rasterize_glyph(icon, kIconUnitsPerEm, taille_px, out).
font::Glyph make_icon(IconKind kind);

} // namespace gw::render
