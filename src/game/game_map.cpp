// The mission map (M): the level from above, in the isometric orientation of the game view, only where the player has been
// shown it, zoomed on the player to start with, panned and zoomed with the mouse.
#include "game/game.hpp"

#include "core/dtable.hpp"
#include "engine/world/actor_anim.hpp"
#include "engine/world/render.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace crusader {

namespace {

constexpr int kSeenCell = 128;  // map units (world units, twice the usecode ones)
constexpr int kSeenCells = 512;

int cell_of(float v) { return std::clamp(static_cast<int>(v / kSeenCell), 0, kSeenCells - 1); }

// The isometric projection of a map point in world units, without the height: the footprint seen from the camera.
void project(float x, float y, float& sx, float& sy) {
    sx = (x - y) * 0.25f;
    sy = (x + y) * 0.125f;
}

}  // namespace

// @orig none: port-only mission map
bool Game::seen_at(float wx, float wy) const {
    if (map_seen_.empty()) return false;
    return map_seen_[static_cast<std::size_t>(cell_of(wy)) * kSeenCells + static_cast<std::size_t>(cell_of(wx))];
}

// The cells whose centres fall inside the view (the part of the level the screen shows), within reach of the player, are
// marked as seen.
// @orig none: port-only mission map
void Game::update_seen() {
    if (!world_) return;
    if (map_seen_.size() != static_cast<std::size_t>(kSeenCells) * kSeenCells) {
        map_seen_.assign(static_cast<std::size_t>(kSeenCells) * kSeenCells, false);
    }
    if (++map_seen_frame_ % 6 != 0) return;
    const Item* me = world_->get(1);
    if (!me) return;
    const float px = static_cast<float>(me->x) * 2.0f, py = static_cast<float>(me->y) * 2.0f;
    const GameState& s = world_->state;
    const float width = static_cast<float>(s.screen_right + 1), height = static_cast<float>(s.screen_bottom + 1);
    const float z = static_cast<float>(me->z & 0xFF);
    const int cx0 = cell_of(px), cy0 = cell_of(py);
    for (int cy = std::max(0, cy0 - 16); cy <= std::min(kSeenCells - 1, cy0 + 16); ++cy) {
        for (int cx = std::max(0, cx0 - 16); cx <= std::min(kSeenCells - 1, cx0 + 16); ++cx) {
            const float wx = (static_cast<float>(cx) + 0.5f) * kSeenCell, wy = (static_cast<float>(cy) + 0.5f) * kSeenCell;
            const float sx = (wx - wy) * 0.25f - static_cast<float>(s.view_left);
            const float sy = (wx + wy) * 0.125f - z - static_cast<float>(s.view_top);
            if (sx > -48.0f && sx < width + 48.0f && sy > -48.0f && sy < height + 48.0f) {
                map_seen_[static_cast<std::size_t>(cy) * kSeenCells + static_cast<std::size_t>(cx)] = true;
            }
        }
    }
}

// @orig none: port-only mission map
void Game::set_map(bool on) {
    if (on) close_wheel();
    if (!on) {
        map_ = MapOverlay{};
        map_dragging_ = false;
        return;
    }
    map_ = MapOverlay{};
    map_.on = true;
    // Zoomed on the player.
    if (const Item* me = world_ ? world_->get(1) : nullptr) {
        project(static_cast<float>(me->x) * 2.0f, static_cast<float>(me->y) * 2.0f, map_.center_x, map_.center_y);
    }
    map_.zoom = 0.6f;
    build_map();
}

// @orig none: port-only mission map
void Game::build_map() {
    const float cx = map_.center_x, cy = map_.center_y, zoom = map_.zoom;  // the view survives a rebuild
    const bool follow = map_.follow;
    map_ = MapOverlay{};
    map_.on = true;
    map_.center_x = cx, map_.center_y = cy, map_.zoom = zoom, map_.follow = follow;
    if (!world_) return;
    std::vector<MapPoly> floors, walls, doors;
    // Doors by the name of their usecode class (RDOOR_N, ...) and the energy walls and force fields that close a way.
    std::map<std::uint16_t, int> door_kind;  // shape -> 0 none, 1 door, 2 force field
    auto door_of = [&](std::uint16_t shape) {
        if (const auto it = door_kind.find(shape); it != door_kind.end()) return it->second;
        const std::string name = usecode_.class_name(shape);
        auto has = [&](const char* s) { return name.find(s) != std::string::npos; };
        const int kind = has("EWALL") || has("FFIELD") || has("FORCEF") ? 2 : (has("DOOR") || has("GATE") || has("HATCH") || has("SHUTTER")) ? 1 : 0;
        door_kind[shape] = kind;
        return kind;
    };
    world_->for_each([&](const Item& item) {
        if (item.off_map || item.parent != 0 || (item.flags & (kItemContained | kItemEthereal | kItemInvisible))) return;
        if (is_npc_id(item.id)) return;
        const ShapeInfo* info = world_->shape_info(item.shape);
        if (!info || info->is(ShapeInfo::kRoof) || info->is(ShapeInfo::kEditor)) return;
        const bool mirrored = item.flags & kItemFlipped;
        const float x = static_cast<float>(item.x) * 2.0f, y = static_cast<float>(item.y) * 2.0f;
        if (!seen_at(x, y)) return;  // not seen yet
        const float x_left = x - (mirrored ? info->footpad_y : info->footpad_x) * static_cast<float>(kFootpadXY);
        const float y_far = y - (mirrored ? info->footpad_x : info->footpad_y) * static_cast<float>(kFootpadXY);
        if (x_left >= x && y_far >= y) return;  // no footprint
        MapPoly p;
        const float px[4] = {x_left, x, x, x_left}, py[4] = {y_far, y_far, y, y};
        for (int i = 0; i < 4; ++i) project(px[i], py[i], p.x[i], p.y[i]);
        const int height = info->footpad_z;
        if (const int door = door_of(item.shape)) {
            // Doors stand out: amber; force fields violet.
            if (door == 1) p.r = 255, p.g = 170, p.b = 40, p.a = 255;
            else p.r = 200, p.g = 95, p.b = 255, p.a = 255;
            // A little fatter than the footprint, so a thin door still shows as a bar.
            const float dx[4] = {x_left - 12, x + 12, x + 12, x_left - 12}, dy[4] = {y_far - 12, y_far - 12, y + 12, y + 12};
            for (int i = 0; i < 4; ++i) project(dx[i], dy[i], p.x[i], p.y[i]);
            doors.push_back(p);
        } else if (info->is(ShapeInfo::kLand)) {
            p.r = 52, p.g = 78, p.b = 98, p.a = 150;  // floors: a faint blue grey
            floors.push_back(p);
        } else if (info->is(ShapeInfo::kSolid) && height >= 2) {
            // Walls and solid blocks: lighter the taller they are.
            const std::uint8_t v = static_cast<std::uint8_t>(std::min(235, 130 + height * 6));
            p.r = v, p.g = v, p.b = static_cast<std::uint8_t>(std::min(255, v + 12)), p.a = 235;
            walls.push_back(p);
        }
    });
    map_.polys = std::move(floors);
    map_.polys.insert(map_.polys.end(), walls.begin(), walls.end());
    map_.polys.insert(map_.polys.end(), doors.begin(), doors.end());

    // Triggers (the level scripts' markers) are everywhere; the ones of a shape that occurs only a few times are the ones that
    // matter (an alarm, an exit, a mission event), so only those are shown.
    std::map<std::uint16_t, int> trigger_counts;
    world_->for_each([&](const Item& item) {
        if (item.off_map || item.parent != 0 || is_npc_id(item.id)) return;
        const ShapeInfo* info = world_->shape_info(item.shape);
        if (info && info->is(ShapeInfo::kEditor)) ++trigger_counts[item.shape];
    });
    map_rare_triggers_.clear();
    for (const auto& [shape, count] : trigger_counts) {
        if (count <= 6) map_rare_triggers_.push_back(shape);
    }
    update_map_markers();
}

// @orig none: port-only mission map
void Game::update_map_markers() {
    if (map_.follow && world_) {
        if (const Item* me = world_->get(1)) project(static_cast<float>(me->x) * 2.0f, static_cast<float>(me->y) * 2.0f, map_.center_x, map_.center_y);
    }
    map_.markers.clear();
    if (!world_) return;
    auto add = [&](float x, float y, int kind) {
        MapMarker m;
        project(x, y, m.x, m.y);
        m.kind = kind;
        map_.markers.push_back(m);
    };
    world_->for_each([&](const Item& item) {
        if (item.off_map || item.parent != 0 || (item.flags & (kItemContained | kItemEthereal))) return;
        const ShapeInfo* info = world_->shape_info(item.shape);
        const float x = static_cast<float>(item.x) * 2.0f, y = static_cast<float>(item.y) * 2.0f;
        if (!seen_at(x, y)) return;
        if (is_npc_id(item.id)) {
            if (item.id == 1) return;
            if (info && info->is(ShapeInfo::kNpc) && !world_->is_dead(item.id) && (item.z & 0xFF) != 0xFE) add(x, y, 1);
            return;
        }
        if (!info) return;
        if (info->is(ShapeInfo::kEditor)) {
            if (std::find(map_rare_triggers_.begin(), map_rare_triggers_.end(), item.shape) != map_rare_triggers_.end()) add(x, y, 5);
            return;
        }
        if (item.flags & kItemInvisible) return;
        const int family = info->family;
        if (family >= 10 && family <= 13) {
            const bool key = dtable::inventory_item_index(item.shape) != 0 || family >= 12;
            add(x, y, key ? 2 : 3);
        } else if (info->is(ShapeInfo::kSelectable)) {
            add(x, y, 4);
        }
    });
    // You, last so you are on top.
    if (const Item* me = world_->get(1)) {
        const float x = static_cast<float>(me->x) * 2.0f, y = static_cast<float>(me->y) * 2.0f;
        add(x, y, 0);
        const int dir = world_->npc_direction(1) & 0xF;
        float hx, hy, ox, oy;
        project(x, y, ox, oy);
        project(x + kDirectionDx[dir] * 64.0f, y + kDirectionDy[dir] * 64.0f, hx, hy);
        map_.heading_x = hx - ox;
        map_.heading_y = hy - oy;
    }
}

// @orig none: port-only mission map
void Game::map_mouse(int kind, int x, int y, int wheel) {
    if (!map_.on) return;
    const MapPanel panel = map_panel(view_w_, view_h_);
    const float pcx = panel.x + panel.w * 0.5f, pcy = panel.y + panel.h * 0.5f;
    switch (kind) {
    case 0:  // a drag moves the map with the mouse
        if (map_dragging_) {
            map_.follow = false;
            map_.center_x -= static_cast<float>(x - map_last_x_) / map_.zoom;
            map_.center_y -= static_cast<float>(y - map_last_y_) / map_.zoom;
        }
        map_last_x_ = x, map_last_y_ = y;
        break;
    case 1:
        map_dragging_ = true;
        map_last_x_ = x, map_last_y_ = y;
        break;
    case 2:
        map_dragging_ = false;
        break;
    case 3: {  // the wheel zooms about the pointer, which keeps the same map point under it
        const float old_zoom = map_.zoom;
        map_.zoom = std::clamp(map_.zoom * std::pow(1.18f, static_cast<float>(wheel)), 0.04f, 3.0f);
        if (!map_.follow) {
            map_.center_x += (static_cast<float>(x) - pcx) * (1.0f / old_zoom - 1.0f / map_.zoom);
            map_.center_y += (static_cast<float>(y) - pcy) * (1.0f / old_zoom - 1.0f / map_.zoom);
        }
        break;
    }
    case 4:  // back on the player, following it again
        map_.follow = true;
        if (const Item* me = world_ ? world_->get(1) : nullptr) {
            project(static_cast<float>(me->x) * 2.0f, static_cast<float>(me->y) * 2.0f, map_.center_x, map_.center_y);
        }
        break;
    default: break;
    }
}

}  // namespace crusader
