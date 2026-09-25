#include "app/theme.h"

namespace gw::app {
namespace {

// Mode clair : les valeurs historiques du projet, rassemblees ici sans en
// changer une seule — la bascule ne devait pas etre l occasion de retoucher la
// charte au passage.
constexpr Theme kLight{
    /* background     */ 0xFFFFFFFFu,
    /* foreground     */ 0xFF000000u,
    /* subtle         */ 0xFF8A8A8Au,
    /* hover_fill     */ 0xFFF2F2F2u,
    /* pressed_fill   */ 0xFFDADADAu,
    /* separator      */ 0xFFE0E0E0u,
    /* disabled       */ 0xFFA0A0A0u,
    /* inverted_fill  */ 0xFF000000u,
    /* inverted_text  */ 0xFFFFFFFFu,
    /* inverted_hover */ 0xFF2B2B2Bu,
    /* selection_fill */ 0xFF000000u,
    /* selection_text */ 0xFFFFFFFFu,
    /* caret          */ 0xFF000000u,
    /* scrollbar      */ 0xFFC8C8C8u,
};

// Mode sombre : symetrique, pas « inspire de ». Les gris ne sont pas repris
// tels quels mais renverses autour du milieu (0x8A reste 0x8A, 0xF2 devient
// 0x14). Reutiliser les gris clairs sur fond noir donnerait des survols plus
// lumineux que le texte, ce qui inverserait la hierarchie visuelle.
constexpr Theme kDark{
    /* background     */ 0xFF000000u,
    /* foreground     */ 0xFFFFFFFFu,
    /* subtle         */ 0xFF8A8A8Au, // deja median, lisible sur les deux fonds
    /* hover_fill     */ 0xFF141414u,
    /* pressed_fill   */ 0xFF2A2A2Au,
    /* separator      */ 0xFF303030u,
    /* disabled       */ 0xFF5F5F5Fu,
    /* inverted_fill  */ 0xFFFFFFFFu,
    /* inverted_text  */ 0xFF000000u,
    /* inverted_hover */ 0xFFD8D8D8u,
    /* selection_fill */ 0xFFFFFFFFu,
    /* selection_text */ 0xFF000000u,
    /* caret          */ 0xFFFFFFFFu,
    /* scrollbar      */ 0xFF3C3C3Cu,
};

bool g_dark = false;

} // namespace

const Theme& theme() { return g_dark ? kDark : kLight; }

void set_dark_mode(bool dark) { g_dark = dark; }

bool dark_mode() { return g_dark; }

} // namespace gw::app
