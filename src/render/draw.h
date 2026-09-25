// render/draw.h — primitives de composition dans le framebuffer.
//
// Volontairement pauvre : des rectangles pleins et le report d une couverture
// 8 bits. Tout le reste (texte, icones du chrome) passe par le rasterizer de
// contours, pas par des primitives dediees. Ajouter ici un cercle ou une
// bordure arrondie serait le debut d un second moteur de rendu parallele.
#pragma once

#include <cstdint>

#include "render/bitmap.h"

namespace gw::render {

// Rectangle plein, coordonnees en pixels, bornes automatiquement au
// framebuffer. `w`/`h` negatifs ou nuls ne dessinent rien.
void fill_rect(Framebuffer& fb, int x, int y, int w, int h, std::uint32_t argb);

// Rectangle evide de `thickness` pixels, dessine vers l interieur.
void stroke_rect(Framebuffer& fb, int x, int y, int w, int h, int thickness,
                 std::uint32_t argb);

// Reporte une image BGRA a alpha droit, mise a lechelle vers `dst_w` x `dst_h`.
//
// Reduction : filtre boite (moyenne des pixels sources couverts). Le logo est
// fourni en 477 px et pose en 144, soit un facteur 3,3 : au plus proche voisin,
// deux pixels sources sur trois seraient jetes et les diagonales partiraient en
// escalier. Ce nest pas la meme question que labsence dantialiasing du
// rasterizer de glyphes : ici on rechantillonne une image deja tramee, pas une
// forme vectorielle.
//
// Agrandissement : plus proche voisin, conserve. Interpoler ne ferait que
// ramollir des angles droits, ce que la charte refuse.
void blit_image(Framebuffer& fb, const Framebuffer& src, int dst_x, int dst_y, int dst_w,
                int dst_h);

// Reporte une couverture 8 bits a la position donnee (coin haut-gauche).
// Sert aux icones du chrome, qui sont rasterisees hors atlas.
void blit_coverage(Framebuffer& fb, const Coverage8& src, int x, int y, std::uint32_t argb);

} // namespace gw::render
