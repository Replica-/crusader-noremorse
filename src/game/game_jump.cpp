// The targeted jump (keyboard and mouse mode): J tapped is the original blind jump; J held shows where the player would land
// (a dashed arc and a ghost of the player) and the jump goes there when J is released.
#include "game/game.hpp"

#include "engine/world/actor_anim.hpp"
#include "engine/world/jump.hpp"
#include "engine/world/player_commands.hpp"

#include <algorithm>
#include <cmath>

namespace crusader {

namespace {

constexpr std::uint32_t kHoldTicks = 12;  // J held this long (about a quarter of a second) is a targeted jump
constexpr int kJumpRange = 256;          // the farthest landing, usecode units

}  // namespace

// @orig none: port-only targeted jump
bool Game::jump_key(const GameKeyEvent& e) {
    if (!config_.keyboard_mouse || e.scancode != 0x39 || e.extended || !world_ || world_->state.controlled_npc != 1) return false;
    if (e.pressed) {
        if (e.count <= 1 && !jump_held_ && !world_->state.avatar_in_stasis && !world_->state.selection.active) {
            jump_held_ = true;
            jump_down_at_ = kernel_->ticks();
        }
        return true;
    }
    if (!jump_held_) return true;
    const std::uint32_t held = kernel_->ticks() - jump_down_at_;
    update_jump_preview();
    jump_held_ = false;
    const bool targeted = held >= kHoldTicks;
    const bool valid = jump_preview_.on;
    jump_preview_.on = false;
    if (!player_may_act(*world_)) return true;
    if (!targeted) {
        jump(*kernel_, *world_, anims_);  // a tap: the blind jump of the original
    } else if (valid) {
        start_targeted_jump(*kernel_, *world_, anims_, 1, jump_landing_);
    }
    return true;
}

// While J is held: where the player would land, wanted at the pointer, and the picture of it.
// @orig none: port-only targeted jump
void Game::update_jump_preview() {
    jump_preview_.on = false;
    if (!jump_held_ || !world_ || modal() || movie_) return;
    if (kernel_->ticks() - jump_down_at_ < kHoldTicks) return;
    const Item* player = world_->get(1);
    if (!player || world_->is_dead(1) || !player_may_act(*world_)) return;
    const World::Point from = world_->location(*player);
    std::vector<World::Point> arc;
    World::Point landing{};
    if (!plan_jump(*world_, 1, from, pointer_goal(), kJumpRange, landing, &arc)) return;
    jump_landing_ = landing;

    const GameState& s = world_->state;
    auto project = [&](const World::Point& p) {
        const auto ux = static_cast<std::int16_t>(p.x), uy = static_cast<std::int16_t>(p.y);
        return std::pair<float, float>{static_cast<float>(((ux - uy) >> 1) - s.view_left),
                                       static_cast<float>((static_cast<std::uint16_t>(ux + uy) >> 2) - p.z - s.view_top)};
    };
    jump_preview_.arc.clear();
    for (const World::Point& p : arc) jump_preview_.arc.push_back(project(p));
    const auto at = project(landing);
    jump_preview_.x = at.first;
    jump_preview_.y = at.second;

    // The ghost: the player standing, facing the way of the jump.
    int dir = direction_16(from.x, from.y, landing.x, landing.y);
    const AnimAction* a = anims_.action(player->shape, 0);
    const Shape* shape = shapes_.get(player->shape);
    jump_preview_.width = 0;
    if (!a || !shape || a->frame_count() == 0) {
        jump_preview_.on = true;
        return;
    }
    if (!a->sixteen_directions() && (dir & 1)) dir = (dir - 1) & 0xF;
    const AnimFrame* f = a->frame(dir, 0);
    if (!f || f->frame >= shape->frames().size()) {
        jump_preview_.on = true;
        return;
    }
    const std::uint32_t key = (static_cast<std::uint32_t>(player->shape) << 16 | f->frame) * 2 + (f->mirrored ? 1u : 0u) + 1u;
    const ShapeFrame& sf = shape->frames()[f->frame];
    jump_preview_.mirrored = f->mirrored;
    jump_preview_.width = sf.width;
    jump_preview_.height = sf.height;
    jump_preview_.xoff = sf.xoff;
    jump_preview_.yoff = sf.yoff;
    if (jump_preview_.key != key && sf.width > 0 && sf.height > 0) {
        jump_preview_.key = key;
        jump_preview_.rgba.assign(static_cast<std::size_t>(sf.width) * static_cast<std::size_t>(sf.height), 0);
        const Palette& palette = s.game_palette;
        for (int y = 0; y < sf.height; ++y) {
            for (int x = 0; x < sf.width; ++x) {
                const int p = sf.pixel(x, y);
                if (p < 0) continue;
                const Rgb c = palette.colors[static_cast<std::size_t>(p) & 0xFF];
                jump_preview_.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(sf.width) + static_cast<std::size_t>(x)] =
                    0xFF000000u | static_cast<std::uint32_t>(c.b) << 16 | static_cast<std::uint32_t>(c.g) << 8 | c.r;
            }
        }
    }
    jump_preview_.on = true;
}

}  // namespace crusader
