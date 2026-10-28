#include "engine/world/item_selection.hpp"

#include "core/dtable.hpp"
#include "engine/audio/audio.hpp"
#include "engine/ui/gump_keys.hpp"
#include "engine/world/inventory.hpp"

#include <algorithm>
#include <cstdlib>

namespace crusader {

namespace {

constexpr ObjId kPlayer = 1;
constexpr ObjId kFirstItem = 0x100;
constexpr ObjId kLastItem = 0x27FF;
constexpr std::uint16_t kItemFlag1000 = 0x1000;  // meaning unknown (ScummVM: "hanging")
constexpr std::uint16_t kNotUsable = 0x4ED;
constexpr int kRange = 0x80;
constexpr int kNothingSfx = 0xB0;

// getCX / getCY: the centre of the footprint.
std::pair<int, int> centre(const World& world, const Item& item) {
    const ShapeInfo* info = world.shape_info(item.shape);
    const int fx = info ? info->footpad_x : 0, fy = info ? info->footpad_y : 0;
    return {static_cast<std::uint16_t>(item.x - fx * 8), static_cast<std::uint16_t>(item.y - fy * 8)};
}

bool selectable(const World& world, const Item& item) {
    const ShapeInfo* info = world.shape_info(item.shape);
    return info && info->is(ShapeInfo::kSelectable);
}

// 13E8:3732: the mode ends (unless G), the item chosen is picked up; with
// G, on to the next.
void finish(World& world);

// 13E8:37B4.
// @orig 13E8:37B4
void next(World& world) {
    auto& sel = world.state.selection;
    if (sel.items.empty()) return;
    sel.chosen = 0;
    if (sel.current >= 0) sel.current = sel.current + 1 < static_cast<int>(sel.items.size()) ? sel.current + 1 : -1;
    if (sel.current < 0) {
        if (sel.pick_all) {
            sel.pick_all = false;
            finish(world);
            return;
        }
        sel.current = 0;
    }
}

// 13E8:388F.
// @orig 13E8:388F
void previous(World& world) {
    auto& sel = world.state.selection;
    sel.chosen = 0;
    if (sel.items.empty()) return;
    if (sel.current >= 0) --sel.current;
    if (sel.current < 0) sel.current = static_cast<int>(sel.items.size()) - 1;
}

// @orig 13E8:1710
void finish(World& world) {
    auto& sel = world.state.selection;
    const ObjId chosen = sel.chosen;
    if (!sel.pick_all) sel.active = false;  // flag 0x100, DS:5FF7; the frame goes
    if (chosen) add_item_cru(world, chosen, true);
    if (sel.pick_all) next(world);
}

// Enter, Space, Ins (13E8:1710): a selectable item is used, another chosen.
// @orig 13E8:1710
void take(World& world) {
    auto& sel = world.state.selection;
    const ObjId id = selected_item(world);
    if (id == 0) {
        sel.chosen = 0;
    } else if (selectable(world, *world.get(id))) {
        use_item(world, id);
        sel.chosen = 0;
    } else {
        sel.chosen = id;
    }
    finish(world);
}

}  // namespace

ObjId selected_item(const World& world) {
    const auto& sel = world.state.selection;
    if (!sel.active || sel.current < 0 || sel.current >= static_cast<int>(sel.items.size())) return 0;
    return world.get(sel.items[sel.current]) ? sel.items[sel.current] : 0;
}

// @orig 13E8:2B74; flag 0x100
void selection_click(World& world) {
    take(world);
}

namespace {

// The items the selection mode offers: nearest first (see the header).
std::vector<ObjId> nearby_items(const World& world) {
    std::vector<ObjId> items;
    const Item* player = world.get(kPlayer);
    if (!player) return items;
    const auto [px, py] = centre(world, *player);
    const int pz = player->z & 0xFF;
    struct Candidate {
        ObjId id;
        int distance;
    };
    std::vector<Candidate> found;
    for (ObjId id = kFirstItem; id <= kLastItem; ++id) {
        const Item* item = world.get(id);
        if (!item || item->sprite_only || (item->z & 0xFF) == 254 || (item->flags & kItemContained)) continue;
        const ShapeInfo* info = world.shape_info(item->shape);
        const int family = info ? info->family : 0;
        if (item->shape != kNotUsable && (family < 10 || family > 13) && !selectable(world, *item)) continue;
        const auto [cx, cy] = centre(world, *item);
        const int distance = std::max(std::abs(cx - px), std::abs(cy - py));
        const int z = item->z & 0xFF;
        if (z < pz + 0x50 && pz - 0x18 < z && distance < kRange && !(item->flags & kItemFlag1000))
            found.push_back({id, distance});
    }
    // A bubble sort: nearest first, in item order between equals.
    std::stable_sort(found.begin(), found.end(), [](const Candidate& a, const Candidate& b) { return a.distance < b.distance; });
    for (const Candidate& c : found) items.push_back(c.id);
    return items;
}

}  // namespace

// @orig 13E8:33D9
void start_selection(World& world, bool pick_all) {
    auto& sel = world.state.selection;
    sel.pick_all = pick_all;
    sel.items.clear();
    sel.chosen = 0;
    sel.current = -1;
    if (!world.get(kPlayer)) return;
    sel.items = nearby_items(world);
    if (sel.items.empty()) {
        sel.pick_all = false;
        finish(world);
        if (Audio* audio = world.audio()) audio->play_sfx(kNothingSfx);
        return;
    }
    sel.current = 0;
    sel.active = true;
}

// @orig 13E8:3796
void cancel_selection(World& world) {
    auto& sel = world.state.selection;
    sel.chosen = 0;
    sel.pick_all = false;
    finish(world);
}

// @orig 13E8:14B4
void selection_key(World& world, std::uint16_t code) {
    auto& sel = world.state.selection;
    switch (code) {
    case 'u': case 'U':
        if (const ObjId id = selected_item(world)) {
            if (dtable::inventory_item_index(world.get(id)->shape) == 0) use_item(world, id);
            sel.chosen = 0;
            finish(world);
        }
        return;
    case gump_key::kEnter: case ' ': case 0x152:
        take(world);
        return;
    case gump_key::kEscape: case gump_key::kUp: case gump_key::kLeft: case gump_key::kRight: case gump_key::kDown:
        sel.chosen = 0;
        finish(world);
        return;
    case ',': case '<':
        previous(world);
        return;
    case '.': case '>': case 's': case 'S':
        next(world);
        return;
    default:
        return;
    }
}

// @orig ?: the G modal loop (NEWGUMP_1368_0000), not reproduced as such
void selection_tick(World& world) {
    auto& sel = world.state.selection;
    if (!sel.active) return;
    if (sel.items.empty()) {
        cancel_selection(world);
        return;
    }
    if (const ObjId id = selected_item(world); id && sel.pick_all) {
        sel.chosen = id;
        finish(world);
    }
}

}  // namespace crusader

namespace crusader {

// @orig none: port-only click on an item (keyboard and mouse mode)
bool click_item(World& world, int screen_x, int screen_y) {
    constexpr int kHitRadius = 28;
    auto& sel = world.state.selection;
    if (sel.active) return false;
    const std::vector<ObjId> items = nearby_items(world);
    int best = -1, best_distance = kHitRadius * kHitRadius + 1;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Item* item = world.get(items[i]);
        if (!item) continue;
        const auto [cx, cy] = centre(world, *item);
        const auto ux = static_cast<std::int16_t>(cx), uy = static_cast<std::int16_t>(cy);
        const int sx = ((ux - uy) >> 1) - world.state.view_left;
        const int sy = (static_cast<std::uint16_t>(ux + uy) >> 2) - world.state.view_top - (item->z & 0xFF) - 8;
        const int d = (sx - screen_x) * (sx - screen_x) + (sy - screen_y) * (sy - screen_y);
        if (d < best_distance) best = static_cast<int>(i), best_distance = d;
    }
    if (best < 0) return false;
    sel.pick_all = false;
    sel.items = items;
    sel.chosen = 0;
    sel.current = best;
    sel.active = true;
    take(world);  // as Enter on the item: used if selectable, else picked up
    return true;
}

}  // namespace crusader

namespace crusader {

// @orig none: port-only hover target (keyboard and mouse mode)
ObjId hover_item(const World& world, int screen_x, int screen_y, bool* in_range) {
    constexpr int kHitRadius = 28;
    ObjId best = 0;
    int best_distance = kHitRadius * kHitRadius + 1;
    for (ObjId id = kFirstItem; id <= kLastItem; ++id) {
        const Item* item = world.get(id);
        if (!item || item->sprite_only || (item->z & 0xFF) == 254 || (item->flags & kItemContained)) continue;
        const ShapeInfo* info = world.shape_info(item->shape);
        const int family = info ? info->family : 0;
        if (item->shape != kNotUsable && (family < 10 || family > 13) && !selectable(world, *item)) continue;
        const auto [cx, cy] = centre(world, *item);
        const auto ux = static_cast<std::int16_t>(cx), uy = static_cast<std::int16_t>(cy);
        const int sx = ((ux - uy) >> 1) - world.state.view_left;
        const int sy = (static_cast<std::uint16_t>(ux + uy) >> 2) - world.state.view_top - (item->z & 0xFF) - 8;
        const int d = (sx - screen_x) * (sx - screen_x) + (sy - screen_y) * (sy - screen_y);
        if (d < best_distance) best = id, best_distance = d;
    }
    if (best != 0 && in_range) {
        const std::vector<ObjId> near = nearby_items(world);
        *in_range = std::find(near.begin(), near.end(), best) != near.end();
    }
    return best;
}

}  // namespace crusader
