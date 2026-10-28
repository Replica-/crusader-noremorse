// The weapon wheel (Tab) and the item wheel (I): a ring of what the player carries, to choose from with the mouse or the arrows.
#include "game/game.hpp"

#include "core/dtable.hpp"
#include "engine/world/actor_anim.hpp"
#include "engine/world/inventory.hpp"
#include "engine/world/loadout.hpp"
#include "engine/world/item_selection.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace crusader {

namespace {

std::string name_or_code(const char* name, std::uint16_t shape) {
    if (name && std::string(name) != "INVALID" && name[0] != '\0') return name;
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "ITEM %X", shape);
    return buffer;
}

}  // namespace

// @orig none: port-only wheels
void Game::open_wheel(int kind) {
    if (!world_ || modal() || movie_ || world_->state.controlled_npc != 1 || world_->state.avatar_in_stasis) return;
    set_map(false);
    wheel_ = WheelOverlay{};
    wheel_.on = true;
    wheel_.kind = kind;
    wheel_.title = kind == 0 ? "WEAPONS" : "ITEMS";
    const auto list = kind == 0 ? carried_weapons(*world_) : carried_items(*world_);
    for (const LoadoutEntry& e : list) {
        WheelEntry w;
        w.id = e.id;
        w.active = e.active;
        w.stashed = e.slot == 0;
        if (kind == 0) {
            w.label = name_or_code(dtable::weapon_name(e.shape), e.shape);
            w.icon = wheel_icon(3, dtable::weapon_icon(e.shape));
            if (const std::uint16_t ammo = dtable::ammo_shape(e.shape)) {
                const ObjId clips = carried(*world_, ammo);
                w.detail = std::to_string(e.quality) + " ROUNDS";
                if (clips != 0) w.detail += ", " + std::to_string(world_->get(clips)->quality) + " CLIPS";
            }
        } else {
            const bool family12 = world_->family_of(e.shape) == 12;
            w.label = name_or_code(family12 ? dtable::family12_name(e.shape) : dtable::item_name(e.shape), e.shape);
            w.icon = family12 ? wheel_icon(4, dtable::item_icon_family12(e.shape)) : wheel_icon(5, dtable::item_icon(e.shape));
            if (e.quality > 1) w.detail = "X" + std::to_string(e.quality);
        }
        wheel_.entries.push_back(std::move(w));
    }
    wheel_.cx = static_cast<float>(view_w_) * 0.5f;
    wheel_.cy = static_cast<float>(view_h_) * 0.5f;
    // The entry in hand starts highlighted.
    for (std::size_t i = 0; i < wheel_.entries.size(); ++i) {
        if (wheel_.entries[i].active) wheel_.hover = static_cast<int>(i);
    }
}

// @orig none: port-only wheels
void Game::close_wheel() {
    wheel_ = WheelOverlay{};
}

// @orig none: port-only wheels
void Game::choose_wheel_entry() {
    if (!wheel_.on || wheel_.hover < 0 || wheel_.hover >= static_cast<int>(wheel_.entries.size())) return;
    const ObjId id = static_cast<ObjId>(wheel_.entries[static_cast<std::size_t>(wheel_.hover)].id);
    if (wheel_.kind == 0) choose_weapon(*kernel_, *world_, anims_, id);
    else choose_item(*world_, id);
    close_wheel();
}

// @orig none: port-only wheels
void Game::wheel_mouse(int kind, int x, int y) {
    if (!wheel_.on) return;
    wheel_.cx = static_cast<float>(view_w_) * 0.5f;
    wheel_.cy = static_cast<float>(view_h_) * 0.5f;
    if (kind == 2) {  // the right button closes it
        close_wheel();
        return;
    }
    const float dx = static_cast<float>(x) - wheel_.cx, dy = static_cast<float>(y) - wheel_.cy;
    const float distance = std::sqrt(dx * dx + dy * dy);
    const std::size_t n = wheel_.entries.size();
    int under = -1;
    if (n > 0 && distance >= wheel_.inner * 0.6f) {
        // Entry 0 is at the top, the rest clockwise.
        constexpr float kPi = 3.14159265f;
        float angle = std::atan2(dy, dx) + kPi / 2.0f;
        if (angle < 0) angle += 2.0f * kPi;
        under = static_cast<int>(angle / (2.0f * kPi / static_cast<float>(n))) % static_cast<int>(n);
    }
    if (kind == 0) {
        wheel_.hover = under;
    } else if (kind == 1 && under >= 0) {
        wheel_.hover = under;
        choose_wheel_entry();
    }
}

}  // namespace crusader

namespace crusader {

// The game's own icon (GUMPS.FLX shape `gump_shape`, frame `frame`) as a picture for the presenter, given once; -1 if there is none.
// @orig none: port-only wheels
int Game::wheel_icon(int gump_shape, int frame) {
    const std::uint32_t key = static_cast<std::uint32_t>(gump_shape) << 16 | static_cast<std::uint32_t>(frame & 0xFFFF);
    if (const auto it = wheel_icon_ids_.find(key); it != wheel_icon_ids_.end()) return it->second;
    const Shape* shape = gumps_.get(static_cast<std::uint16_t>(gump_shape));
    if (!shape || frame < 0 || static_cast<std::size_t>(frame) >= shape->frames().size()) return wheel_icon_ids_[key] = -1;
    const ShapeFrame& f = shape->frames()[static_cast<std::size_t>(frame)];
    if (f.width <= 0 || f.height <= 0) return wheel_icon_ids_[key] = -1;
    WheelIcon icon;
    icon.id = wheel_icon_count_++;
    icon.width = f.width;
    icon.height = f.height;
    icon.rgba.assign(static_cast<std::size_t>(f.width) * static_cast<std::size_t>(f.height), 0);
    const Palette& palette = world_->state.game_palette;
    for (int y = 0; y < f.height; ++y) {
        for (int x = 0; x < f.width; ++x) {
            const int p = f.pixel(x, y);
            if (p < 0) continue;
            const Rgb c = palette.colors[static_cast<std::size_t>(p) & 0xFF];
            icon.rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(f.width) + static_cast<std::size_t>(x)] =
                0xFF000000u | static_cast<std::uint32_t>(c.b) << 16 | static_cast<std::uint32_t>(c.g) << 8 | c.r;
        }
    }
    wheel_icon_ids_[key] = icon.id;
    wheel_icon_uploads_.push_back(std::move(icon));
    return wheel_icon_ids_[key];
}

}  // namespace crusader

namespace crusader {

// The developer flags that stage a feature for the README's pictures (see set_demo).
// @orig none: port-only
void Game::update_demo() {
    if (!world_ || modal() || movie_) return;
    const Item* me = world_->get(1);
    if (!me || me->off_map || world_->state.avatar_in_stasis) return;
    const std::uint32_t now = kernel_->ticks();
    if (demo_blood_ && now - demo_blood_at_ > 25) {
        demo_blood_at_ = now;
        for (int i = 0; i < 4; ++i) {  // a burst on the spot in front of him, thrown in each of four directions
            GameState::BloodBurst burst;
            burst.x = me->x + (i == 0 ? 90 : i == 2 ? -90 : 0);
            burst.y = me->y + (i == 1 ? 90 : i == 3 ? -90 : 0);
            burst.z = me->z & 0xFF;
            burst.dx = i == 0 ? 1.0f : i == 2 ? -1.0f : 0.0f;
            burst.dy = i == 1 ? 1.0f : i == 3 ? -1.0f : 0.0f;
            burst.amount = 30;
            world_->state.blood_bursts.push_back(burst);
        }
    }
    if (demo_hit_bar_) {  // the nearest enemy shows a half-empty bar
        ObjId best = 0;
        int best_distance = 1 << 30;
        for (ObjId id = 2; id < 256; ++id) {
            const Item* npc = world_->get(id);
            const ShapeInfo* si = npc ? world_->shape_info(npc->shape) : nullptr;
            if (!npc || !si || !si->is(ShapeInfo::kNpc) || npc->off_map || world_->is_dead(id) || (npc->z & 0xFF) == 0xFE) continue;
            const int d = std::max(std::abs(npc->x - me->x), std::abs(npc->y - me->y));
            if (d < best_distance) best = id, best_distance = d;
        }
        if (best != 0) world_->state.hit_bars[best] = GameState::HitBar{100, 55, now};
    }
    if (demo_walk_pending_) {
        demo_walk_pending_ = false;
        path_goal_ = World::Point{me->x + demo_walk_dx_, me->y + demo_walk_dy_, me->z};
        path_active_ = true;
        path_hold_ = false;
        path_stuck_ = 0;
        path_item_ = 0;
        path_best_ = 1 << 30;
        path_last_ = world_->location(*me);
        path_points_.clear();
    }
    if (demo_hover_) {  // the pointer over the nearest item in reach
        for (ObjId id = 0x100; id <= 0x27FF; ++id) {
            const Item* item = world_->get(id);
            if (!item || item->off_map || !item_in_range(*world_, id)) continue;
            const GameState& s = world_->state;
            const auto ux = static_cast<std::int16_t>(item->x), uy = static_cast<std::int16_t>(item->y);
            pointer_x_ = ((ux - uy) >> 1) - s.view_left;
            pointer_y_ = (static_cast<std::uint16_t>(ux + uy) >> 2) - s.view_top - (item->z & 0xFF) - 8;
            break;
        }
    }
}

}  // namespace crusader

#include "engine/ui/controls_screen.hpp"
#include "engine/ui/modal_gump.hpp"

namespace crusader {

namespace {

// The controls screen as a screen of its own.
class ControlsGump : public ModalGump {
public:
    explicit ControlsGump(const FontSet& fonts) : screen_(fonts) {}
    void draw(Surface8& surface) const override { screen_.draw(surface); }
    void key(std::uint16_t code) override { screen_.key(code); }
    void mouse_down(int x, int y) override { screen_.mouse_down(x, y); }
    bool closed() const override { return screen_.closed(); }

private:
    ControlsScreen screen_;
};

}  // namespace

// @orig none: port-only
void Game::show_controls_screen() {
    if (!world_ || modal()) return;
    enter_menu_mode();
    world_->state.screen_palette = menu_palette_;
    open_screen(std::make_unique<ControlsGump>(fonts_), [this](const ModalGump&) { leave_menu_mode(); });
}

}  // namespace crusader
