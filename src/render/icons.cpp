#include "render/icons.h"

#include <cmath>
#include <vector>

namespace gw::render {
namespace {

// Epaisseur de trait des icones, en unites de design. A 10 px de cote cela
// donne un trait de 1 px, ce qui est le poids visuel des icones systeme.
constexpr float kStroke = 100.0f;

void add_point(std::vector<font::GlyphPoint>& c, float x, float y) {
    font::GlyphPoint p;
    p.x = x;
    p.y = y;
    p.on_curve = true; // les icones sont entierement polygonales
    c.push_back(p);
}

// Rectangle aligne sur les axes.
//
// `reverse` inverse le sens de parcours. Cest ce qui distingue un contour
// exterieur dun trou : la regle nonzero additionne les sens de traversee, donc
// un contour interieur parcouru a lenvers annule le remplissage du contour qui
// lentoure. Deux contours de meme sens qui se chevauchent, eux, restent
// pleins — cest ce qui rend la croix de fermeture correcte.
void add_rect(font::Glyph& g, float x0, float y0, float x1, float y1, bool reverse = false) {
    std::vector<font::GlyphPoint> c;
    if (!reverse) {
        add_point(c, x0, y0);
        add_point(c, x1, y0);
        add_point(c, x1, y1);
        add_point(c, x0, y1);
    } else {
        add_point(c, x0, y1);
        add_point(c, x1, y1);
        add_point(c, x1, y0);
        add_point(c, x0, y0);
    }
    g.contours.push_back(std::move(c));
}

// Carre evide : contour exterieur, puis contour interieur en sens inverse.
void add_ring(font::Glyph& g, float x0, float y0, float x1, float y1, float stroke) {
    add_rect(g, x0, y0, x1, y1, false);
    add_rect(g, x0 + stroke, y0 + stroke, x1 - stroke, y1 - stroke, true);
}

// Barre diagonale : un quadrilatere epaissi perpendiculairement au segment.
// Les deux barres de la croix sont emises dans le meme sens pour que leur
// intersection reste pleine (winding 2) au lieu de se creuser.
void add_diagonal(font::Glyph& g, float ax, float ay, float bx, float by, float half) {
    const float dx = bx - ax;
    const float dy = by - ay;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f) return;
    const float nx = -dy / len * half;
    const float ny = dx / len * half;

    std::vector<font::GlyphPoint> c;
    add_point(c, ax + nx, ay + ny);
    add_point(c, bx + nx, by + ny);
    add_point(c, bx - nx, by - ny);
    add_point(c, ax - nx, ay - ny);
    g.contours.push_back(std::move(c));
}

} // namespace

font::Glyph make_icon(IconKind kind) {
    font::Glyph g;
    constexpr float k = static_cast<float>(kIconUnitsPerEm);

    switch (kind) {
        case IconKind::Minimize:
            // Barre centree verticalement, pleine largeur.
            add_rect(g, 0.0f, k * 0.45f, k, k * 0.45f + kStroke);
            break;

        case IconKind::Maximize:
            add_ring(g, 0.0f, 0.0f, k, k, kStroke);
            break;

        case IconKind::Restore: {
            // Carre au premier plan, plus deux traits qui suggerent la fenetre
            // situee derriere. Les traits sont volontairement disjoints du
            // carre : deux contours qui se chevauchent partiellement rendraient
            // le resultat dependant de leur orientation relative.
            add_ring(g, 0.0f, 0.0f, k * 0.75f, k * 0.75f, kStroke);
            add_rect(g, k * 0.25f, k - kStroke, k, k);            // trait du haut
            add_rect(g, k - kStroke, k * 0.25f, k, k);            // trait de droite
            break;
        }

        case IconKind::Settings: {
            // Trois curseurs : une barre pleine largeur, plus un curseur carre
            // pose dessus a une abscisse differente sur chaque ligne. Le
            // curseur et la barre sont emis dans le meme sens, donc leur
            // recouvrement reste plein au lieu de se creuser.
            const float bar = kStroke * 0.8f;
            const float knob = kStroke * 2.2f;
            const float rows[3] = {k * 0.80f, k * 0.46f, k * 0.12f};
            const float knob_x[3] = {k * 0.62f, k * 0.22f, k * 0.50f};
            for (int i = 0; i < 3; ++i) {
                add_rect(g, 0.0f, rows[i], k, rows[i] + bar);
                add_rect(g, knob_x[i], rows[i] - (knob - bar) * 0.5f, knob_x[i] + knob,
                         rows[i] + bar + (knob - bar) * 0.5f);
            }
            break;
        }

        case IconKind::File: {
            // Feuille au format portrait avec le coin superieur droit corne.
            // Dessinee comme un contour ferme unique plutot que par
            // composition : un seul contour ne peut pas se creuser par erreur.
            const float left = k * 0.18f;
            const float right = k * 0.82f;
            const float top = k * 0.96f;
            const float bottom = k * 0.04f;
            const float fold = k * 0.22f; // cote du coin corne

            std::vector<font::GlyphPoint> outer;
            add_point(outer, left, bottom);
            add_point(outer, right, bottom);
            add_point(outer, right, top - fold);
            add_point(outer, right - fold, top);
            add_point(outer, left, top);
            g.contours.push_back(std::move(outer));

            // Evidement, decale du meme trait partout sauf sur la corne, ou il
            // suit la diagonale : cest ce qui donne le pli visible.
            const float s = kStroke * 0.9f;
            std::vector<font::GlyphPoint> inner;
            add_point(inner, left + s, top - s);
            add_point(inner, right - fold - s * 0.4f, top - s);
            add_point(inner, right - s, top - fold - s * 0.4f);
            add_point(inner, right - s, bottom + s);
            add_point(inner, left + s, bottom + s);
            g.contours.push_back(std::move(inner));
            break;
        }

        case IconKind::AlignLeft:
        case IconKind::AlignCenter:
        case IconKind::AlignRight: {
            // Quatre lignes de texte schematisees : les 1re et 3e pleine
            // largeur, les 2e et 4e plus courtes. Cest le contraste entre les
            // deux longueurs qui rend le bord aligne lisible — quatre barres de
            // meme longueur donneraient la meme icone dans les trois cas.
            const float bar = kStroke * 0.85f;
            const float rows[4] = {k * 0.82f, k * 0.56f, k * 0.30f, k * 0.04f};
            const float shortened = k * 0.62f;
            for (int i = 0; i < 4; ++i) {
                const bool full = (i % 2) == 0;
                const float width = full ? k : shortened;
                float x0 = 0.0f;
                if (!full) {
                    if (kind == IconKind::AlignCenter) x0 = (k - width) * 0.5f;
                    else if (kind == IconKind::AlignRight) x0 = k - width;
                }
                add_rect(g, x0, rows[i], x0 + width, rows[i] + bar);
            }
            break;
        }

        case IconKind::Close: {
            const float m = k * 0.07f; // marge, pour que la croix tienne dans la boite
            add_diagonal(g, m, m, k - m, k - m, kStroke * 0.7f);
            add_diagonal(g, m, k - m, k - m, m, kStroke * 0.7f);
            break;
        }
    }

    g.x_min = 0.0f;
    g.y_min = 0.0f;
    g.x_max = k;
    g.y_max = k;
    return g;
}

} // namespace gw::render
