#include "render/rasterizer.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gw::render {
namespace {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Une arete non horizontale, prete pour le balayage.
// `winding` vaut +1 si l arete descend dans le repere bitmap, -1 sinon :
// c est ce signe qui alimente la regle nonzero.
struct Edge {
    float x0, y0, x1, y1;
    int winding;
};

// Nombre de segments utilises pour approcher une courbe quadratique.
// Calcule a partir de la longueur du polygone de controle : une courbe de 2 px
// n a pas besoin du meme decoupage qu une courbe de 200 px. La borne haute
// evite qu une taille absurde fasse exploser la liste d aretes.
int subdivision_count(const Vec2& p0, const Vec2& p1, const Vec2& p2) {
    const float d = std::hypot(p1.x - p0.x, p1.y - p0.y) + std::hypot(p2.x - p1.x, p2.y - p1.y);
    const int n = static_cast<int>(std::ceil(d / 1.5f));
    return std::clamp(n, 2, 64);
}

void emit_edge(std::vector<Edge>& edges, const Vec2& a, const Vec2& b) {
    if (a.y == b.y) return; // arete horizontale : aucune intersection utile
    if (a.y < b.y) {
        edges.push_back(Edge{a.x, a.y, b.x, b.y, +1});
    } else {
        edges.push_back(Edge{b.x, b.y, a.x, a.y, -1});
    }
}

void emit_quadratic(std::vector<Edge>& edges, const Vec2& p0, const Vec2& ctrl, const Vec2& p1) {
    const int n = subdivision_count(p0, ctrl, p1);
    Vec2 prev = p0;
    for (int i = 1; i <= n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const float u = 1.0f - t;
        Vec2 cur;
        cur.x = u * u * p0.x + 2.0f * u * t * ctrl.x + t * t * p1.x;
        cur.y = u * u * p0.y + 2.0f * u * t * ctrl.y + t * t * p1.y;
        emit_edge(edges, prev, cur);
        prev = cur;
    }
}

// Convertit un contour TrueType en aretes, en resolvant les points de controle
// implicites : deux points off-curve consecutifs sous-entendent un point
// on-curve situe exactement a leur milieu. C est une compression du format, pas
// une approximation — le milieu est la vraie position du point.
void flatten_contour(const std::vector<font::GlyphPoint>& pts, float scale, float origin_x,
                     float origin_y, std::vector<Edge>& edges) {
    const std::size_t n = pts.size();
    if (n < 2) return;

    // Repere bitmap : X vers la droite, Y vers le bas (d ou le signe negatif).
    auto to_device = [&](const font::GlyphPoint& p) {
        return Vec2{p.x * scale - origin_x, origin_y - p.y * scale};
    };
    auto midpoint = [](const Vec2& a, const Vec2& b) {
        return Vec2{(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
    };

    // Determination du point de depart : le contour peut tres bien commencer
    // par un point de controle.
    std::size_t start_index = 0;
    Vec2 start;
    bool found_on_curve = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (pts[i].on_curve) {
            start_index = i;
            start = to_device(pts[i]);
            found_on_curve = true;
            break;
        }
    }
    if (!found_on_curve) {
        // Contour entierement off-curve : le point de depart est le milieu
        // entre le dernier et le premier point de controle.
        start = midpoint(to_device(pts[n - 1]), to_device(pts[0]));
        start_index = 0;
    }

    Vec2 cursor = start;
    bool have_ctrl = false;
    Vec2 ctrl{};

    const std::size_t first = found_on_curve ? start_index + 1 : start_index;
    for (std::size_t k = 0; k < n; ++k) {
        const std::size_t i = (first + k) % n;
        if (found_on_curve && i == start_index) break;

        const Vec2 p = to_device(pts[i]);
        if (pts[i].on_curve) {
            if (have_ctrl) {
                emit_quadratic(edges, cursor, ctrl, p);
                have_ctrl = false;
            } else {
                emit_edge(edges, cursor, p);
            }
            cursor = p;
        } else {
            if (have_ctrl) {
                const Vec2 implied = midpoint(ctrl, p);
                emit_quadratic(edges, cursor, ctrl, implied);
                cursor = implied;
            }
            ctrl = p;
            have_ctrl = true;
        }
    }

    // Fermeture du contour, en consommant un eventuel controle en attente.
    if (have_ctrl) {
        emit_quadratic(edges, cursor, ctrl, start);
    } else {
        emit_edge(edges, cursor, start);
    }
}

} // namespace

bool rasterize_glyph(const font::Glyph& glyph, std::uint16_t units_per_em, float pixel_size,
                     RasterizedGlyph& out) {
    out = RasterizedGlyph{};
    if (units_per_em == 0 || pixel_size <= 0.0f) return false;

    const float scale = pixel_size / static_cast<float>(units_per_em);
    out.advance = static_cast<int>(std::lround(glyph.advance_width * scale));

    if (glyph.empty()) return false;

    // Bornes reelles calculees depuis les points plutot que depuis la bbox
    // declaree : les points de controle peuvent depasser la bbox, et une bbox
    // fausse (ca existe) produirait des contours coupes.
    float min_x = 1e30f, min_y = 1e30f, max_x = -1e30f, max_y = -1e30f;
    for (const auto& contour : glyph.contours) {
        for (const auto& p : contour) {
            min_x = std::min(min_x, p.x);
            min_y = std::min(min_y, p.y);
            max_x = std::max(max_x, p.x);
            max_y = std::max(max_y, p.y);
        }
    }
    if (min_x > max_x) return false;

    // Un pixel de marge de chaque cote absorbe les arrondis.
    const int px_min_x = static_cast<int>(std::floor(min_x * scale)) - 1;
    const int px_max_x = static_cast<int>(std::ceil(max_x * scale)) + 1;
    const int px_min_y = static_cast<int>(std::floor(min_y * scale)) - 1;
    const int px_max_y = static_cast<int>(std::ceil(max_y * scale)) + 1;

    const int w = px_max_x - px_min_x;
    const int h = px_max_y - px_min_y;
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;

    out.bearing_x = px_min_x;
    out.bearing_y = px_max_y; // haut du bitmap, au-dessus de la ligne de base
    out.coverage.resize(w, h);

    std::vector<Edge> edges;
    edges.reserve(256);
    for (const auto& contour : glyph.contours) {
        flatten_contour(contour, scale, static_cast<float>(px_min_x),
                        static_cast<float>(px_max_y), edges);
    }
    if (edges.empty()) return false;

    // --- Balayage ---
    // Une passe par ligne, echantillonnee au centre du pixel (y + 0.5).
    // Les intersections sont collectees puis triees par x ; le compteur de
    // winding s incremente en traversant chaque arete et la ligne est remplie
    // partout ou il est non nul.
    struct Crossing {
        float x;
        int winding;
    };
    std::vector<Crossing> crossings;
    crossings.reserve(32);

    for (int y = 0; y < h; ++y) {
        const float sample_y = static_cast<float>(y) + 0.5f;
        crossings.clear();

        for (const Edge& e : edges) {
            // Intervalle semi-ouvert [y0, y1) : indispensable pour qu un
            // sommet partage par deux aretes ne compte qu une fois.
            if (sample_y < e.y0 || sample_y >= e.y1) continue;
            const float t = (sample_y - e.y0) / (e.y1 - e.y0);
            crossings.push_back(Crossing{e.x0 + t * (e.x1 - e.x0), e.winding});
        }
        if (crossings.size() < 2) continue;

        std::sort(crossings.begin(), crossings.end(),
                  [](const Crossing& a, const Crossing& b) { return a.x < b.x; });

        int winding = 0;
        for (std::size_t i = 0; i + 1 < crossings.size(); ++i) {
            winding += crossings[i].winding;
            if (winding == 0) continue;

            const float span_start = crossings[i].x;
            const float span_end = crossings[i + 1].x;

            // On remplit les pixels dont le centre tombe dans le span.
            int x0 = static_cast<int>(std::ceil(span_start - 0.5f));
            int x1 = static_cast<int>(std::ceil(span_end - 0.5f));
            x0 = std::max(x0, 0);
            x1 = std::min(x1, w);
            for (int x = x0; x < x1; ++x) {
                out.coverage.set(x, y, 255);
            }
        }
    }

    return true;
}

} // namespace gw::render
