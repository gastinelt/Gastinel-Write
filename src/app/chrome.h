// app/chrome.h — barre de titre redessinee.
//
// La barre nest pas une fenetre : cest une bande dessinee dans le meme
// framebuffer que le reste, et une poignee de rectangles servant au hit-test.
// Il ny a aucun HWND enfant, donc aucun controle natif a themer, et le chrome
// se presente par le meme backend (OpenGL ou GDI) que le contenu.
#pragma once

#include <string>

#include "font/ttf.h"
#include "render/bitmap.h"
#include "render/glyph_atlas.h"
#include "render/rasterizer.h"

namespace gw::app {

enum class CaptionButton {
    None,
    Minimize,
    MaximizeRestore,
    Close,
};

class TitleBar {
public:
    // Rasterise les quatre icones une fois pour toutes. A rappeler seulement si
    // le facteur dechelle change.
    void build_icons(float scale);

    // Recalcule les rectangles pour une largeur de fenetre donnee.
    void layout(int client_width, int inset);

    int height() const { return height_; }

    // Vrai si le point est dans la bande de titre ET hors de tout bouton.
    // Cest ce que le hit-test Win32 traduit en HTCAPTION.
    bool is_caption_point(int x, int y) const;

    // Bouton sous le curseur, None si aucun.
    CaptionButton button_at(int x, int y) const;

    void set_hovered(CaptionButton b) { hovered_ = b; }
    void set_pressed(CaptionButton b) { pressed_ = b; }
    CaptionButton hovered() const { return hovered_; }
    CaptionButton pressed() const { return pressed_; }

    void draw(render::Framebuffer& fb, const std::string& title, const font::TtfFont& font,
              render::GlyphAtlas& atlas, bool maximized) const;

private:
    struct ButtonRect {
        int x = 0, y = 0, w = 0, h = 0;
        bool contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    const render::RasterizedGlyph& icon_for(CaptionButton b, bool maximized) const;

    int height_ = 0;
    int inset_ = 0;
    int width_ = 0;

    ButtonRect minimize_{};
    ButtonRect maximize_{};
    ButtonRect close_{};

    render::RasterizedGlyph icon_minimize_;
    render::RasterizedGlyph icon_maximize_;
    render::RasterizedGlyph icon_restore_;
    render::RasterizedGlyph icon_close_;

    CaptionButton hovered_ = CaptionButton::None;
    CaptionButton pressed_ = CaptionButton::None;
};

} // namespace gw::app
