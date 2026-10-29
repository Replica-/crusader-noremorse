// The main character's colours (port addition): the colour screen's overlay, and the player's sprite drawn again by the graphics
// card in the chosen colours over the frame (see GameConfig::player_colours, core/player_colours.hpp).
#include "game/game.hpp"

#include "engine/ui/colours_screen.hpp"

#include <algorithm>

namespace crusader {

// The sprite of the player, as the frame shows it, kept apart on a layer of palette indices (255 where it has none).
// @orig none: port-only player colours
void Game::build_player_layer(const DrawItem& d) {
    player_layer_.on = false;
    if (!config_.player_colours.custom) return;
    const ShapeFrame& f = *d.frame;
    if (f.width <= 0 || f.height <= 0) return;
    Surface8 scratch(f.width, f.height);
    scratch.clear(255);
    f.draw(scratch, d.mirrored ? f.width - 1 - f.xoff : f.xoff, f.yoff, d.mirrored);
    player_layer_.x = d.left;
    player_layer_.y = d.top;
    player_layer_.w = f.width;
    player_layer_.h = f.height;
    player_layer_.index = scratch.pixels;
    player_layer_.recolour = player_recolour(world_->state.game_palette, config_.player_colours);
    player_layer_.base = world_->state.game_palette;
    player_layer_.on = true;
}

// Once the frame is done: what something drew over the player is not his any more.
// @orig none: port-only player colours
void Game::finish_player_layer(const Surface8& screen) {
    PlayerLayer& l = player_layer_;
    if (!l.on) return;
    bool any = false;
    for (int y = 0; y < l.h; ++y) {
        const int sy = l.y + y;
        for (int x = 0; x < l.w; ++x) {
            std::uint8_t& p = l.index[static_cast<std::size_t>(y) * static_cast<std::size_t>(l.w) + static_cast<std::size_t>(x)];
            if (p == 255) continue;
            const int sx = l.x + x;
            if (sy < 0 || sy >= screen.height || sx < 0 || sx >= screen.width || screen.row(sy)[sx] != p || (l.recolour[p] >> 24) == 0) {
                p = 255;
            } else {
                any = true;
            }
        }
    }
    l.on = any;
}

// @orig none: port-only player colours
const PlayerLayer* Game::player_layer() const {
    if (!world_ || modal() || movie_ || !player_layer_.on) return nullptr;
    return &player_layer_;
}

// The colour screen of the options (in the menu or opened by F2), with the character turning in his colours.
// @orig none: port-only player colours
const ColoursOverlay* Game::colours_overlay() {
    const ColoursScreen* screen = menu_ ? menu_->colours() : screen_ ? screen_->colours_screen() : nullptr;
    if (!screen) return nullptr;
    colours_overlay_ = screen->overlay();
    const Shape* shape = shapes_.get(1);
    const AnimAction* a = anims_.action(1, 0);
    const AnimFrame* f = a ? a->frame(screen->direction(), 0) : nullptr;
    if (shape && f && f->frame < shape->frames().size()) {
        const ShapeFrame& sf = shape->frames()[f->frame];
        const auto recolour = player_recolour(palette_, config_.player_colours);
        ColoursOverlay& o = colours_overlay_;
        o.iw = sf.width, o.ih = sf.height;
        o.ax = f->mirrored ? sf.width - 1 - sf.xoff : sf.xoff;
        o.ay = sf.yoff;
        o.image.assign(static_cast<std::size_t>(sf.width) * static_cast<std::size_t>(sf.height), 0);
        for (int y = 0; y < sf.height; ++y) {
            for (int x = 0; x < sf.width; ++x) {
                const int p = sf.pixel(x, y);
                if (p < 0) continue;
                std::uint32_t c = recolour[static_cast<std::size_t>(p)];
                if ((c >> 24) == 0) {
                    const Rgb rgb = palette_.colors[static_cast<std::size_t>(p)];
                    c = 0xFF000000u | static_cast<std::uint32_t>(rgb.b) << 16 | static_cast<std::uint32_t>(rgb.g) << 8 | rgb.r;
                }
                const int dx = f->mirrored ? sf.width - 1 - x : x;
                o.image[static_cast<std::size_t>(y) * static_cast<std::size_t>(sf.width) + static_cast<std::size_t>(dx)] = c;
            }
        }
        std::uint32_t key = static_cast<std::uint32_t>(screen->direction()) + 1;
        for (const std::uint32_t c : config_.player_colours.rgb) key = key * 1000003u ^ c;
        o.image_key = key | 1u;
    }
    return &colours_overlay_;
}

}  // namespace crusader
