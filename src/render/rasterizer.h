// render/rasterizer.h — rasterization scanline des contours de glyphes.
//
// Regle de remplissage retenue : NONZERO WINDING.
//
// Pourquoi nonzero plutot que even-odd : c est la regle que la specification
// TrueType impose. Les contours d un glyphe TrueType sont orientes (exterieur
// horaire, interieur anti-horaire) precisement pour que le nonzero creuse les
// contreformes. Avec even-odd, un glyphe dont deux contours exterieurs se
// chevauchent (frequent apres decomposition d un composite, par exemple un
// accent qui mord sur la lettre) presenterait un trou a l intersection. Le
// nonzero est donc le seul choix correct ici, pas une preference.
//
// v0 : aucun antialiasing (couverture binaire 0 ou 255), aucun hinting.
// Cf. README, section dette technique.
#pragma once

#include "font/ttf.h"
#include "render/bitmap.h"

namespace gw::render {

// Resultat de la rasterization d un glyphe a une taille donnee.
struct RasterizedGlyph {
    Coverage8 coverage;

    // Position du coin haut-gauche du bitmap par rapport a l origine du
    // glyphe (le point de base sur la ligne de base), en pixels.
    // bearing_y est positif vers le haut, convention typographique.
    int bearing_x = 0;
    int bearing_y = 0;

    // Avance horizontale en pixels, arrondie a l entier le plus proche.
    int advance = 0;
};

// Rasterise un glyphe a `pixel_size` pixels par em.
//
// Le glyphe est fourni en unites de police ; la conversion utilise
// units_per_em. Le repere TrueType a son axe Y vers le haut, le bitmap a son
// axe Y vers le bas : la conversion est faite ici, une seule fois.
//
// Retourne false si le glyphe est vide ou si la taille demandee est invalide ;
// dans ce cas seules les metriques de `out` sont renseignees.
bool rasterize_glyph(const font::Glyph& glyph, std::uint16_t units_per_em, float pixel_size,
                     RasterizedGlyph& out);

} // namespace gw::render
