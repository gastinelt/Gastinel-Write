#include "app/context_menu.h"

#include "app/theme.h"
#include "render/draw.h"
#include "render/text_run.h"

#include <algorithm>

namespace gw::app {
namespace {

constexpr float kItemPixelSize = 14.0f;
constexpr int kItemHeight = 26;
constexpr int kSeparatorHeight = 7;
constexpr int kPaddingX = 12;
constexpr int kPaddingY = 4;
constexpr int kShortcutGap = 28;
constexpr int kMinWidth = 170;
constexpr int kBorder = 1;

} // namespace

void ContextMenu::open(std::vector<MenuItem> items, int x, int y, int bounds_width,
                       int bounds_height, const font::TtfFont& font,
                       render::GlyphAtlas& atlas) {
    items_ = std::move(items);
    item_tops_.clear();
    hovered_ = -1;
    chosen_id_ = 0;

    // --- Mesure ---
    int content_width = kMinWidth - 2 * kPaddingX;
    // `height` sert a la fois de curseur vertical et de hauteur cumulee : il
    // demarre donc a la marge haute seule, la marge basse etant ajoutee apres
    // la boucle. Les cumuler des le depart decalerait toutes les entrees vers
    // le bas et collerait la derniere au bord.
    int height = kPaddingY;
    for (const MenuItem& item : items_) {
        item_tops_.push_back(height);
        if (item.separator) {
            height += kSeparatorHeight;
            continue;
        }
        int w = render::measure_utf8(item.label, font, atlas);
        if (!item.shortcut.empty()) {
            w += kShortcutGap + render::measure_utf8(item.shortcut, font, atlas);
        }
        content_width = std::max(content_width, w);
        height += kItemHeight;
    }

    width_ = content_width + 2 * kPaddingX + 2 * kBorder;
    height_ = height + kPaddingY + 2 * kBorder;

    // --- Positionnement ---
    // On tente dabord de replier le menu de lautre cote du curseur, comme le
    // fait un menu systeme ; si meme replie il deborde encore (fenetre trop
    // petite), on le colle simplement au bord.
    x_ = x;
    y_ = y;
    if (x_ + width_ > bounds_width) x_ = x - width_;
    if (y_ + height_ > bounds_height) y_ = y - height_;
    x_ = std::clamp(x_, 0, std::max(0, bounds_width - width_));
    y_ = std::clamp(y_, 0, std::max(0, bounds_height - height_));

    open_ = true;
}

void ContextMenu::close() {
    open_ = false;
    hovered_ = -1;
    items_.clear();
    item_tops_.clear();
}

bool ContextMenu::contains(int x, int y) const {
    return open_ && x >= x_ && x < x_ + width_ && y >= y_ && y < y_ + height_;
}

int ContextMenu::item_at(int x, int y) const {
    if (!contains(x, y)) return -1;
    const int local_y = y - y_ - kBorder;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].separator) continue;
        const int top = item_tops_[i];
        if (local_y >= top && local_y < top + kItemHeight) return static_cast<int>(i);
    }
    return -1;
}

MenuResult ContextMenu::on_mouse_move(int x, int y) {
    if (!open_) return MenuResult::Ignored;
    const int hit = item_at(x, y);
    const int previous = hovered_;
    hovered_ = (hit >= 0 && items_[static_cast<std::size_t>(hit)].enabled) ? hit : -1;
    // On absorbe le mouvement des quon est dans le menu, pour que le contenu
    // en dessous ne reagisse pas au survol a travers lui.
    if (contains(x, y)) return MenuResult::Consumed;
    return hovered_ != previous ? MenuResult::Consumed : MenuResult::Ignored;
}

MenuResult ContextMenu::on_left_up(int x, int y) {
    if (!open_) return MenuResult::Ignored;

    if (!contains(x, y)) return MenuResult::Dismissed; // clic exterieur

    const int hit = item_at(x, y);
    if (hit < 0 || !items_[static_cast<std::size_t>(hit)].enabled) {
        // Clic sur un separateur ou une entree grisee : le menu reste ouvert,
        // comme un menu systeme.
        return MenuResult::Consumed;
    }
    chosen_id_ = items_[static_cast<std::size_t>(hit)].id;
    return MenuResult::Chosen;
}

MenuResult ContextMenu::on_key_down(platform::Key key) {
    if (!open_) return MenuResult::Ignored;
    if (key == platform::Key::Escape) return MenuResult::Dismissed;
    // Toute autre touche est absorbee : tant que le menu est ouvert, le clavier
    // lui appartient.
    return MenuResult::Consumed;
}

void ContextMenu::draw(render::Framebuffer& fb, const font::TtfFont& font,
                       render::GlyphAtlas& atlas) const {
    if (!open_) return;

    const Theme& t = theme();

    // Fond plein et filet noir de 1 px. Pas dombre portee, pas de flou, pas
    // danimation : apparition instantanee, angles droits.
    render::fill_rect(fb, x_, y_, width_, height_, t.background);
    render::stroke_rect(fb, x_, y_, width_, height_, kBorder, t.foreground);

    const int ascent = render::ascent_pixels(font, kItemPixelSize);
    const int line = render::line_height(font, kItemPixelSize);

    for (std::size_t i = 0; i < items_.size(); ++i) {
        const MenuItem& item = items_[i];
        const int top = y_ + kBorder + item_tops_[i];

        if (item.separator) {
            const int mid = top + kSeparatorHeight / 2;
            render::fill_rect(fb, x_ + kBorder, mid, width_ - 2 * kBorder, 1, t.separator);
            continue;
        }

        const bool highlighted = (static_cast<int>(i) == hovered_);
        std::uint32_t fg = item.enabled ? t.foreground : t.disabled;

        if (highlighted) {
            // Survol par inversion : cest la seule mise en avant possible sans
            // sortir du monochrome.
            render::fill_rect(fb, x_ + kBorder, top, width_ - 2 * kBorder, kItemHeight,
                              t.foreground);
            fg = t.background;
        }

        const int baseline = top + (kItemHeight - line) / 2 + ascent;
        render::draw_utf8(fb, item.label, x_ + kBorder + kPaddingX, baseline, font, atlas, fg);

        if (!item.shortcut.empty()) {
            const int w = render::measure_utf8(item.shortcut, font, atlas);
            const int sx = x_ + width_ - kBorder - kPaddingX - w;
            render::draw_utf8(fb, item.shortcut, sx, baseline, font, atlas,
                              highlighted ? fg : t.disabled);
        }
    }
}

} // namespace gw::app
