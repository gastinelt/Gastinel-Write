// app/theme.h — palette unique de l application.
//
// La charte Gastinel est monochrome : noir et blanc, plus quelques gris de
// service (survol, texte secondaire, filets). Le mode sombre n introduit
// aucune couleur nouvelle — il echange les roles.
//
// POURQUOI UN ACCESSEUR GLOBAL plutot qu un Theme passe en parametre : chaque
// fonction de dessin de app/ recevrait un argument de plus, propage sur cinq
// niveaux, pour une valeur qui est de fait unique dans le processus. L
// interface graphique est mono-thread (une seule fenetre, une seule boucle de
// messages), donc l etat partage n a pas de contention possible. Le cout d une
// bascule est un booleen et un repaint.
//
// La regle qui remplace le passage explicite : AUCUNE constante de couleur
// n est ecrite ailleurs que dans ce fichier. Un `0xFF......` dans app/ est un
// bug — c est ce qui garantit que le mode sombre atteint bien tout l ecran.
#pragma once

#include <cstdint>

namespace gw::app {

struct Theme {
    std::uint32_t background;  // fond de page
    std::uint32_t foreground;  // texte et filets principaux
    std::uint32_t subtle;      // texte secondaire, icones de service
    std::uint32_t hover_fill;  // survol sur une surface de fond
    std::uint32_t pressed_fill;// enfoncement sur une surface de fond
    std::uint32_t separator;   // filets fins internes (menus, chrome)
    std::uint32_t disabled;    // entree de menu inactive

    // Surface inversee : le bouton plein, le badge actif. En clair elle est
    // noire a texte blanc, en sombre blanche a texte noir.
    std::uint32_t inverted_fill;
    std::uint32_t inverted_text;
    std::uint32_t inverted_hover; // survol de cette meme surface

    // Selection de texte. Distincte de inverted_fill bien qu elle en soit
    // proche : ce n est pas la meme intention, et le jour ou la selection
    // devra s adoucir (elle couvre des lignes entieres, pas un bouton), c est
    // ici qu on le fera sans toucher aux boutons.
    std::uint32_t selection_fill;
    std::uint32_t selection_text;

    // Curseur de saisie et rail de defilement.
    std::uint32_t caret;
    std::uint32_t scrollbar;
};

// Palette courante. Valide des le demarrage : le mode clair est le defaut.
const Theme& theme();

// Bascule clair/sombre. Sans effet visuel immediat : l appelant declenche le
// repaint, parce que lui seul sait s il y a une fenetre a repeindre.
void set_dark_mode(bool dark);
bool dark_mode();

} // namespace gw::app
