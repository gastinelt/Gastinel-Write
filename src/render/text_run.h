// render/text_run.h — pose dune ligne de texte UTF-8.
//
// Il ny a pas de moteur de mise en page ici : une chaine, une ligne de base,
// des avances mises bout a bout. Le retour a la ligne, la justification et la
// bidirectionnalite viendront avec le moteur de document, pas avant.
//
// DETTE TECHNIQUE : aucun crenage. Les avances sortent de hmtx sans
// ajustement par paire (kern et GPOS ne sont pas lus). Cf. README.
#pragma once

#include <cstdint>
#include <string>

#include "font/ttf.h"
#include "render/bitmap.h"
#include "render/glyph_atlas.h"

namespace gw::render {

// Decode le code point a la position `i` et avance `i`.
// Une sequence invalide rend U+FFFD et consomme un octet : le rendu ne doit
// jamais boucler ni deborder sur une entree mal formee.
std::uint32_t next_codepoint(const std::string& utf8, std::size_t& i);

// Largeur en pixels de la chaine, sans la dessiner. Insere les glyphes
// manquants dans latlas au passage, donc `atlas` nest pas const.
int measure_utf8(const std::string& text, const font::TtfFont& font, GlyphAtlas& atlas);

// Dessine la chaine depuis la ligne de base (pen_x, pen_y).
// Retourne labscisse finale du curseur.
int draw_utf8(Framebuffer& fb, const std::string& text, int pen_x, int pen_y,
              const font::TtfFont& font, GlyphAtlas& atlas, std::uint32_t argb);

// --- Sous-chaines --------------------------------------------------------
//
// L editeur ne pose jamais un paragraphe entier : il pose des tranches
// [begin, end) decoupees par le retour a la ligne. Passer par substr() a
// chaque frame allouerait une chaine par ligne affichee et par mesure ; ces
// trois variantes travaillent en place sur les memes octets.
//
// `begin` et `end` sont supposes sur des frontieres de caractere. Une position
// intermediaire ne fait pas boucler : le decodeur rend U+FFFD et avance.

int measure_utf8_range(const std::string& text, std::size_t begin, std::size_t end,
                       const font::TtfFont& font, GlyphAtlas& atlas);

int draw_utf8_range(Framebuffer& fb, const std::string& text, std::size_t begin, std::size_t end,
                    int pen_x, int pen_y, const font::TtfFont& font, GlyphAtlas& atlas,
                    std::uint32_t argb);

// Frontiere de caractere de [begin, end) dont le bord gauche est le plus
// proche de `x`, `x` etant compte depuis le debut de la tranche.
//
// C est la fonction du clic : elle rend la position ou poser le curseur.
// Elle vise le bord le plus proche et non le caractere survole, pour qu un clic
// sur la moitie droite d une lettre place le curseur apres elle — sans quoi il
// serait impossible de se placer en fin de ligne a la souris.
std::size_t offset_at_x(const std::string& text, std::size_t begin, std::size_t end, int x,
                        const font::TtfFont& font, GlyphAtlas& atlas);

// Hauteur de ligne recommandee pour un corps donne, deduite de hhea.
int line_height(const font::TtfFont& font, float pixel_size);

// Distance entre le haut de la ligne et la ligne de base, pour un corps donne.
// Cest ce quil faut pour centrer verticalement une ligne dans une bande :
// centrer sur la bbox des glyphes ferait sauter la ligne de base selon les
// caracteres presents dans la chaine.
int ascent_pixels(const font::TtfFont& font, float pixel_size);

} // namespace gw::render
