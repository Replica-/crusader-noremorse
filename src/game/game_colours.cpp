// The main character's colours (port addition): the colour screen's overlay, and the player's sprite drawn again by the graphics
// card in the chosen colours over the frame (see GameConfig::player_colours, core/player_colours.hpp).
#include "game/game.hpp"

#include "engine/ui/colours_screen.hpp"
#include "engine/world/actor_anim.hpp"
#include "engine/world/attack.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

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

namespace crusader {

// How far the player is from the nearest of his pictures: the angle between where the pointer is and the direction he faces (one
// of 16, 22.5 degrees apart; 8, 45 apart, for an action with fewer pictures), in whole degrees, when he stands facing the pointer.
// @orig none: port-only turn rotation
void Game::compute_turn_angle() {
    turn_angle_ = 0;
    turn_ = {};
    player_sprite_.on = false;
    if (!config_.turn_rotation || !config_.keyboard_mouse || !world_ || modal() || movie_ || !free_pointer()) return;
    if (world_->state.controlled_npc != 1 || world_->is_dead(1) || actor_busy(*kernel_, 1)) return;
    const Item* me = world_->get(1);
    const auto record = world_->npc_record(1);
    if (!me || record.empty()) return;
    const std::uint16_t last = static_cast<std::uint16_t>(record[npc::kLastAnim] | record[npc::kLastAnim + 1] << 8);
    const AnimAction* action = anims_.action(me->shape, last);
    if (!action) return;
    turn_step_ = action->sixteen_directions() ? 1 : 2;
    const World::Point goal = pointer_goal();
    const double dx = goal.x - me->x, dy = goal.y - me->y;
    if (std::abs(dx) + std::abs(dy) < 24) return;
    // Direction k points (kDirectionDx, kDirectionDy)[k], k * 22.5 degrees clockwise from -y.
    constexpr double kPi = 3.14159265358979;
    const double wanted = std::atan2(dx, -dy) * 180.0 / kPi;
    double turn = wanted - static_cast<double>(world_->npc_direction(1)) * 22.5;
    while (turn > 180.0) turn -= 360.0;
    while (turn <= -180.0) turn += 360.0;
    if (std::abs(turn) > turn_step_ * 11.25 + 0.5) return;
    turn_angle_ = static_cast<int>(std::lround(turn));
}

// Before the player is drawn: the frame under it, to put back, and to tell what is drawn over it later.
// @orig none: port-only turn rotation
void Game::turn_before(const Surface8& screen, const DrawItem& d) {
    if (turn_angle_ == 0 || d.item->id != 1) return;
    constexpr int kMargin = 90;  // room for the weapon
    const int x0 = std::max(0, d.left - kMargin), y0 = std::max(0, d.top - kMargin);
    const int x1 = std::min(screen.width, d.right + kMargin), y1 = std::min(screen.height, d.bottom + kMargin);
    if (x1 <= x0 || y1 <= y0) return;
    turn_.x = x0, turn_.y = y0, turn_.w = x1 - x0, turn_.h = y1 - y0;
    turn_.before.resize(static_cast<std::size_t>(turn_.w) * static_cast<std::size_t>(turn_.h));
    for (int y = 0; y < turn_.h; ++y) {
        const std::uint8_t* row = screen.row(y0 + y) + x0;
        std::copy(row, row + turn_.w, turn_.before.begin() + static_cast<std::ptrdiff_t>(y) * turn_.w);
    }
    turn_.have = true;
}

namespace {

// A picture of the player (body and the weapon in his hands) on a layer of its own, its anchor at (sx, sy) on the frame.
void fill_layer(SpriteLayer& out, const ShapeFrame& body, bool mirrored, int sx, int sy, const ShapeFrame* weapon, int weapon_x,
                int weapon_y) {
    const int left = mirrored ? sx + body.xoff - (body.width - 1) : sx - body.xoff, top = sy - body.yoff;
    int l = left, t = top, r = left + body.width, b = top + body.height;
    if (weapon) {
        l = std::min(l, weapon_x - weapon->xoff);
        t = std::min(t, weapon_y - weapon->yoff);
        r = std::max(r, weapon_x - weapon->xoff + weapon->width);
        b = std::max(b, weapon_y - weapon->yoff + weapon->height);
    }
    Surface8 scratch(r - l, b - t);
    scratch.clear(255);
    body.draw(scratch, sx - l, sy - t, mirrored);
    if (weapon) weapon->draw(scratch, weapon_x - l, weapon_y - t, false);
    out.x = l, out.y = t, out.w = r - l, out.h = b - t;
    out.pivot_x = static_cast<float>(left - l) + static_cast<float>(body.width) * 0.5f;
    out.pivot_y = static_cast<float>(top - t) + static_cast<float>(body.height) * 0.5f;
    out.index = scratch.pixels;
}

// How the silhouette of picture `a` has to be moved to become the one of picture `b` (the next direction): for the rows both
// have, a least-squares fit of  x_b = pivot + scale * (x_a - pivot) + shear * (y - pivot_y) + shift  to the two edges of each row.
struct TurnFit {
    float scale = 1, shear = 0, shift = 0;
};

TurnFit fit_turn(const SpriteLayer& a, const SpriteLayer& b) {
    struct Edges {
        int lo, hi;
    };
    auto edges = [](const SpriteLayer& l, int y) {
        Edges e{1 << 30, -1};
        const int row = y - l.y;
        if (row < 0 || row >= l.h) return e;
        for (int x = 0; x < l.w; ++x) {
            if (l.index[static_cast<std::size_t>(row) * static_cast<std::size_t>(l.w) + static_cast<std::size_t>(x)] == 255) continue;
            e.lo = std::min(e.lo, l.x + x);
            e.hi = std::max(e.hi, l.x + x + 1);
        }
        return e;
    };
    const double px = a.x + a.pivot_x, py = a.y + a.pivot_y;
    // Normal equations for (scale, shear, shift) over  x_b - px = scale * (x_a - px) + shear * (y - py) + shift.
    double m[3][3] = {}, r[3] = {};
    int samples = 0;
    for (int y = std::max(a.y, b.y); y < std::min(a.y + a.h, b.y + b.h); ++y) {
        const Edges ea = edges(a, y), eb = edges(b, y);
        if (ea.hi < 0 || eb.hi < 0) continue;
        const double dy = y - py;
        for (int side = 0; side < 2; ++side) {
            const double xa = (side == 0 ? ea.lo : ea.hi) - px, xb = (side == 0 ? eb.lo : eb.hi) - px;
            const double row[3] = {xa, dy, 1.0};
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) m[i][j] += row[i] * row[j];
                r[i] += row[i] * xb;
            }
            ++samples;
        }
    }
    TurnFit fit;
    if (samples < 6) return fit;
    // Cramer's rule.
    auto det = [](double q[3][3]) {
        return q[0][0] * (q[1][1] * q[2][2] - q[1][2] * q[2][1]) - q[0][1] * (q[1][0] * q[2][2] - q[1][2] * q[2][0]) +
               q[0][2] * (q[1][0] * q[2][1] - q[1][1] * q[2][0]);
    };
    const double d = det(m);
    if (std::abs(d) < 1e-6) return fit;
    double v[3];
    for (int c = 0; c < 3; ++c) {
        double q[3][3];
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j) q[i][j] = j == c ? r[i] : m[i][j];
        v[c] = det(q) / d;
    }
    fit.scale = static_cast<float>(std::clamp(v[0], 0.5, 1.6));
    fit.shear = static_cast<float>(std::clamp(v[1], -1.0, 1.0));
    fit.shift = static_cast<float>(std::clamp(v[2], -40.0, 40.0));
    return fit;
}

// The picture `share` (0..1) of the way from `a` to the next direction: the fit, taken that far.
void set_transform(SpriteLayer& layer, const TurnFit& fit, double share) {
    layer.scale_x = static_cast<float>(1.0 + (fit.scale - 1.0) * share);
    layer.shear = static_cast<float>(fit.shear * share);
    layer.shift = static_cast<float>(fit.shift * share);
    layer.rotation = 0;
}

}  // namespace

// After the player (and the weapon in his hands) is drawn: take them off the frame, onto pictures of their own: the nearest of his
// pictures and its neighbour towards the pointer.
// @orig none: port-only turn rotation
void Game::turn_capture(Surface8& screen, const DrawItem& d, const ShapeFrame* weapon, int weapon_x, int weapon_y) {
    if (!turn_.have || d.item->id != 1) return;
    PlayerSprite& s = player_sprite_;
    s.count = 1;
    fill_layer(s.layer[0], *d.frame, d.mirrored, d.sx, d.sy, weapon, weapon_x, weapon_y);
    const double span = turn_step_ * 22.5, share = std::min(1.0, std::abs(static_cast<double>(turn_angle_)) / span);
    s.layer[0].alpha = 1.0f;
    // The neighbour towards the pointer: its picture of the same action and frame, in the next direction.
    const auto record = world_->npc_record(1);
    const std::uint16_t last = static_cast<std::uint16_t>(record[npc::kLastAnim] | record[npc::kLastAnim + 1] << 8);
    const AnimAction* action = anims_.action(1, last);
    const Shape* shape = shapes_.get(1);
    const int neighbour = (world_->npc_direction(1) + (turn_angle_ > 0 ? turn_step_ : -turn_step_) + 16) & 0xF;
    const AnimFrame* f = action ? action->frame(neighbour, record[npc::kAnimFrame]) : nullptr;
    if (f && shape && f->frame < shape->frames().size()) {
        const ShapeFrame* neighbour_weapon = nullptr;
        int nwx = 0, nwy = 0;
        const Item* held = (in_combat(*world_, 1) || last == 0xB) ? world_->get(world_->active_weapon(1)) : nullptr;
        if (held && action) {
            const auto piece = weapon_overlay_.piece(held->shape, last, *action, neighbour, record[npc::kAnimFrame]);
            if (piece && !(piece->shape == 0 && piece->frame == 0)) {
                const Shape* ws = shapes_.get(piece->shape);
                if (ws && piece->frame < ws->frames().size()) {
                    neighbour_weapon = &ws->frames()[piece->frame];
                    nwx = d.sx + piece->dx, nwy = d.sy + piece->dy;
                }
            }
        }
        SpriteLayer next;
        fill_layer(next, shape->frames()[f->frame], f->mirrored, d.sx, d.sy, neighbour_weapon, nwx, nwy);
        set_transform(s.layer[0], fit_turn(s.layer[0], next), share);
    }
    // Put the frame back as it was before the player.
    for (int y = 0; y < turn_.h; ++y) {
        std::copy(turn_.before.begin() + static_cast<std::ptrdiff_t>(y) * turn_.w, turn_.before.begin() + static_cast<std::ptrdiff_t>(y + 1) * turn_.w,
                  screen.row(turn_.y + y) + turn_.x);
    }
    s.recolour = player_recolour(world_->state.game_palette, config_.player_colours);
    s.base = world_->state.game_palette;
    turn_.captured = true;
}

// Once the frame is done: what something drew over the player is not his any more.
// @orig none: port-only turn rotation
void Game::finish_turn_sprite(const Surface8& screen) {
    PlayerSprite& s = player_sprite_;
    if (!turn_.captured) return;
    player_layer_.on = false;  // the rotated sprite carries the chosen colours itself
    // What was drawn over the player since: the frame's pixels that are not what was under him.
    s.ox = turn_.x, s.oy = turn_.y, s.ow = turn_.w, s.oh = turn_.h;
    s.occluder.assign(static_cast<std::size_t>(turn_.w) * static_cast<std::size_t>(turn_.h), 255);
    for (int y = 0; y < turn_.h; ++y) {
        for (int x = 0; x < turn_.w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(turn_.w) + static_cast<std::size_t>(x);
            const std::uint8_t now = screen.row(turn_.y + y)[turn_.x + x];
            if (now != turn_.before[i]) s.occluder[i] = now;
        }
    }
    s.on = true;
    turn_.captured = false;
}

// @orig none: port-only turn rotation
const PlayerSprite* Game::player_sprite() const {
    if (!world_ || modal() || movie_ || !player_sprite_.on) return nullptr;
    return &player_sprite_;
}

}  // namespace crusader

namespace crusader {

// Developer test (--turn-strip): for four directions, the standing picture turned 0..10 degrees towards the next direction by the
// fitted transform (one column each), then the real next picture, to see whether it turns the way the game turns.
// @orig none: port-only turn rotation
void Game::debug_turn_strip(Surface8& out) {
    constexpr int kCellW = 72, kCellH = 112, kAx = 36, kAy = 98;
    const Shape* shape = shapes_.get(1);
    const AnimAction* action = anims_.action(1, 0);
    out = Surface8(kCellW * 12, kCellH * 4);
    out.clear(0);
    if (!shape || !action) return;
    const int dirs[4] = {0, 4, 8, 12};
    for (int row = 0; row < 4; ++row) {
        const AnimFrame* fa = action->frame(dirs[row], 0);
        const int step = action->sixteen_directions() ? 1 : 2;
        const AnimFrame* fb = action->frame((dirs[row] + step) & 0xF, 0);
        if (!fa || !fb) continue;
        SpriteLayer a, b;
        fill_layer(a, shape->frames()[fa->frame], fa->mirrored, kAx, kAy, nullptr, 0, 0);
        fill_layer(b, shape->frames()[fb->frame], fb->mirrored, kAx, kAy, nullptr, 0, 0);
        const TurnFit fit = fit_turn(a, b);
        std::fprintf(stderr, "dir %2d: scale %.3f shear %.3f shift %.2f\n", dirs[row], static_cast<double>(fit.scale),
                     static_cast<double>(fit.shear), static_cast<double>(fit.shift));
        for (int col = 0; col < 12; ++col) {
            const SpriteLayer& src = col == 11 ? b : a;
            SpriteLayer t = src;
            if (col < 11) set_transform(t, fit, col / (22.5 * step));
            for (int y = 0; y < kCellH; ++y) {
                for (int x = 0; x < kCellW; ++x) {
                    const double dyv = y - (t.y + t.pivot_y), dxv = x - (t.x + t.pivot_x) - t.shift - t.shear * dyv;
                    const int px = static_cast<int>(std::floor(t.pivot_x + dxv / t.scale_x)), py = static_cast<int>(std::floor(t.pivot_y + dyv));
                    if (px < 0 || py < 0 || px >= t.w || py >= t.h) continue;
                    const std::uint8_t p = t.index[static_cast<std::size_t>(py) * static_cast<std::size_t>(t.w) + static_cast<std::size_t>(px)];
                    if (p != 255) out.row(row * kCellH + y)[col * kCellW + x] = p;
                }
            }
        }
    }
}

}  // namespace crusader
