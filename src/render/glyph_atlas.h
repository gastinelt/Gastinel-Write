// render/glyph_atlas.h — atlas de glyphes rasterises.
//
// L atlas est une unique image de couverture 8 bits en RAM. C est
// volontairement la meme structure pour les deux backends :
//   - mode OpenGL : l atlas est uploade tel quel en texture (GL_ALPHA) ;
//   - mode GDI    : il reste en RAM et sert de source au compositeur CPU.
// Un seul pipeline logique, deux presentations — c est la contrainte
// d architecture posee au depart.
//
// Packing : algorithme d etageres (shelf). Les glyphes sont poses de gauche a
// droite sur une etagere ; quand la largeur est atteinte, on ouvre une etagere
// sous la plus haute de la precedente. Simple, sans defragmentation, largement
// suffisant pour un jeu de glyphes latin a taille fixe. Un packer par
// arbre binaire serait justifie le jour ou on melangera plusieurs corps.
#pragma once

#include <cstdint>
#include <map>

#include "font/ttf.h"
#include "render/bitmap.h"
#include "render/rasterizer.h"

namespace gw::render {

// Synthese de graisse et d inclinaison appliquee a chaque glyphe insere.
//
// Elle appartient a l atlas et non a l appelant parce qu un glyphe
// synthetise doit etre mis en cache comme les autres : le recalculer a
// chaque pose couterait une rasterization par caractere et par frame. Un
// atlas represente donc une variante complete — police, graisse, pente,
// corps — et non seulement un corps.
struct Synthesis {
    // Cisaillement horizontal, en fraction de la hauteur : x += y * slant.
    // 0 = pas d inclinaison. ~0.2 correspond a une douzaine de degres,
    // la pente des italiques latins courants.
    float slant = 0.0f;
    // Dilatation horizontale de la couverture, en pixels. 0 = pas de faux
    // gras. Applique APRES rasterization : epaissir le contour lui-meme
    // demanderait de decaler chaque segment le long de sa normale, donc de
    // gerer les auto-intersections aux angles vifs — un algorithme entier
    // pour un pixel de plus.
    int embolden = 0;
};

// Emplacement d un glyphe dans l atlas, plus ses metriques de pose.
struct AtlasEntry {
    int x = 0, y = 0;  // coin haut-gauche dans l atlas
    int w = 0, h = 0;  // dimensions du bitmap
    int bearing_x = 0; // decalage horizontal depuis la position du curseur
    int bearing_y = 0; // hauteur au-dessus de la ligne de base
    int advance = 0;   // avance horizontale en pixels
};

class GlyphAtlas {
public:
    // `width` est la largeur fixe de l atlas, `max_height` sa hauteur maximale.
    //
    // L atlas ne pre-alloue PAS max_height : il demarre a quelques lignes et
    // grandit quand une etagere deborde. Mesure a l appui, un jeu latin a un
    // corps donne n occupe qu une vingtaine de lignes ; pre-allouer un carre
    // complet gaspillait plus de 95 % de chaque atlas, et ce gaspillage etait
    // paye une fois par couple (graisse, corps).
    void reset(int width, int max_height, float pixel_size, Synthesis synthesis = {});

    // Rasterise le glyphe s il n est pas deja present, et retourne son entree.
    // Retourne nullptr si le glyphe est introuvable ou si l atlas est plein.
    const AtlasEntry* get_or_insert(const font::TtfFont& font, std::uint32_t codepoint);

    const Coverage8& image() const { return image_; }

    float pixel_size() const { return pixel_size_; }

    // Passe a true a chaque insertion, remis a false par le backend une fois
    // la texture re-uploadee. Evite un upload par frame.
    bool dirty() const { return dirty_; }
    void clear_dirty() { dirty_ = false; }

private:
    // Reserve un rectangle sur l etagere courante, en agrandissant l atlas au
    // besoin. Retourne false seulement si la hauteur maximale est atteinte.
    bool allocate(int w, int h, int& out_x, int& out_y);

    Coverage8 image_;
    std::map<std::uint32_t, AtlasEntry> entries_;
    const AtlasEntry* ascii_entries_[128] = {nullptr};
    float pixel_size_ = 16.0f;
    Synthesis synthesis_{};
    int max_height_ = 0;

    int shelf_x_ = 0;      // curseur horizontal sur l etagere courante
    int shelf_y_ = 0;      // sommet de l etagere courante
    int shelf_height_ = 0; // hauteur du plus grand glyphe de l etagere
    bool dirty_ = false;
};

// Compose un glyphe de l atlas dans le framebuffer, a la couleur `argb`.
//
// `pen_x` / `pen_y` sont la position du curseur sur la ligne de base, en
// pixels ecran. La couverture sert de coefficient de melange : en v0 elle vaut
// 0 ou 255, donc le melange est un simple test, mais le code est deja ecrit
// pour l antialiasing a venir.
void blit_glyph(Framebuffer& fb, const Coverage8& atlas, const AtlasEntry& entry, int pen_x,
                int pen_y, std::uint32_t argb);

} // namespace gw::render
