#include "app/chrome.h"

#include "app/theme.h"
#include "render/draw.h"
#include "render/icons.h"
#include "render/text_run.h"

#include <cmath>

namespace gw::app {
namespace {

// Dimensions alignees sur le chrome natif de Windows 10/11 : une barre de
// 32 px et des boutons de 46x32. Sen ecarter ferait immediatement sentir que
// la fenetre nest pas une fenetre systeme.
constexpr int kBarHeight = 32;
constexpr int kButtonWidth = 46;
constexpr int kIconSize = 10;
constexpr float kTitlePixelSize = 14.0f;
constexpr int kTitleLeftMargin = 14;

// Palette monochrome. Le survol est un aplat de gris, jamais un degrade.

} // namespace

void TitleBar::build_icons(float scale) {
    const float size = static_cast<float>(kIconSize) * scale;
    render::rasterize_glyph(render::make_icon(render::IconKind::Minimize),
                            render::kIconUnitsPerEm, size, icon_minimize_);
    render::rasterize_glyph(render::make_icon(render::IconKind::Maximize),
                            render::kIconUnitsPerEm, size, icon_maximize_);
    render::rasterize_glyph(render::make_icon(render::IconKind::Restore),
                            render::kIconUnitsPerEm, size, icon_restore_);
    render::rasterize_glyph(render::make_icon(render::IconKind::Close),
                            render::kIconUnitsPerEm, size, icon_close_);
}

void TitleBar::layout(int client_width, int inset) {
    width_ = client_width;
    inset_ = inset;
    height_ = kBarHeight + inset;

    // Les boutons sont ancres a droite, dans lordre systeme.
    // Ils sarretent a `inset` du bord haut et du bord droit pour ne pas
    // recouvrir la bande de redimensionnement, qui a la priorite au hit-test.
    const int top = inset;
    int right = client_width - inset;

    close_ = ButtonRect{right - kButtonWidth, top, kButtonWidth, kBarHeight};
    right -= kButtonWidth;
    maximize_ = ButtonRect{right - kButtonWidth, top, kButtonWidth, kBarHeight};
    right -= kButtonWidth;
    minimize_ = ButtonRect{right - kButtonWidth, top, kButtonWidth, kBarHeight};
}

CaptionButton TitleBar::button_at(int x, int y) const {
    if (close_.contains(x, y)) return CaptionButton::Close;
    if (maximize_.contains(x, y)) return CaptionButton::MaximizeRestore;
    if (minimize_.contains(x, y)) return CaptionButton::Minimize;
    return CaptionButton::None;
}

bool TitleBar::is_caption_point(int x, int y) const {
    if (y < inset_ || y >= height_) return false;
    if (x < inset_ || x >= width_ - inset_) return false;
    // Un bouton ne doit jamais etre une poignee de deplacement : sinon le clic
    // partirait dans DefWindowProc et le bouton ne se declencherait jamais.
    return button_at(x, y) == CaptionButton::None;
}

const render::RasterizedGlyph& TitleBar::icon_for(CaptionButton b, bool maximized) const {
    switch (b) {
        case CaptionButton::Minimize: return icon_minimize_;
        case CaptionButton::MaximizeRestore: return maximized ? icon_restore_ : icon_maximize_;
        case CaptionButton::Close: return icon_close_;
        default: return icon_minimize_;
    }
}

void TitleBar::draw(render::Framebuffer& fb, const std::string& title, const font::TtfFont& font,
                    render::GlyphAtlas& atlas, bool maximized) const {
    const Theme& t = theme();
    render::fill_rect(fb, 0, 0, fb.width, height_, t.background);

    // Titre, centre verticalement sur les metriques de la police et non sur la
    // bbox des glyphes : sinon la ligne de base sauterait selon les lettres.
    const int ascent = render::ascent_pixels(font, kTitlePixelSize);
    const int line = render::line_height(font, kTitlePixelSize);
    const int baseline = inset_ + (kBarHeight - line) / 2 + ascent;
    render::draw_utf8(fb, title, kTitleLeftMargin + inset_, baseline, font, atlas, t.foreground);

    // Boutons.
    const CaptionButton buttons[] = {CaptionButton::Minimize, CaptionButton::MaximizeRestore,
                                     CaptionButton::Close};
    const ButtonRect* rects[] = {&minimize_, &maximize_, &close_};

    for (int i = 0; i < 3; ++i) {
        const CaptionButton kind = buttons[i];
        const ButtonRect& r = *rects[i];

        std::uint32_t bg = t.background;
        std::uint32_t fg = t.foreground;

        if (pressed_ == kind && hovered_ == kind) {
            // Le bouton de fermeture sinverse au lieu de se griser : cest le
            // seul moyen de le distinguer sans introduire de couleur, la charte
            // etant monochrome.
            bg = (kind == CaptionButton::Close) ? t.foreground : t.pressed_fill;
            fg = (kind == CaptionButton::Close) ? t.background : t.foreground;
        } else if (hovered_ == kind) {
            bg = (kind == CaptionButton::Close) ? t.foreground : t.hover_fill;
            fg = (kind == CaptionButton::Close) ? t.background : t.foreground;
        }

        if (bg != t.background) render::fill_rect(fb, r.x, r.y, r.w, r.h, bg);

        const render::RasterizedGlyph& icon = icon_for(kind, maximized);
        if (!icon.coverage.empty()) {
            const int ix = r.x + (r.w - icon.coverage.width) / 2;
            const int iy = r.y + (r.h - icon.coverage.height) / 2;
            render::blit_coverage(fb, icon.coverage, ix, iy, fg);
        }
    }

    // Filet de separation avec la zone de contenu.
    render::fill_rect(fb, inset_, height_ - 1, fb.width - 2 * inset_, 1, t.separator);
}

} // namespace gw::app
