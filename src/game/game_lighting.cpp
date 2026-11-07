// The lighting (GameConfig::lighting): the hardware scene is multiplied by a light map, about 80 % of the art everywhere,
// a little more in the rooms with lamps, plus soft lights. The art has its light painted in, so the lights come from
// what the pictures show:
// - shapes picked by hand (kShapeLights): explosions, shots, lamps; a shape listed for some frames is dark in the others;
// - glowing pixels: a picture with enough pixels of the palette's light colours gives a light of that colour. Those
//   are the colours outside the shading ramps: 1..7 (yellows to red, rotated by the palette cycle), 8..14 (the
//   translucent glows, cycled), and the lone bright entries (lamps, LEDs, screens);
// - liquids: a flat picture that moves (a pool, a spill, a leak) glows in its own colour and lights its room (kLiquidRoom);
// - the muzzle flash of each shot (GameState::muzzle_flashes), fading in a moment;
// - a soft light around the controlled NPC, so that the dark places stay readable.
// Lights do not add up: where they meet, the brightest wins.
//
// Rooms: each light of the map (lamps and glows, on screen or not) lights the room it faces. From the floor in front of
// it, the room is every floor cell joined to it with no wall between (crates, machines, railings and doors do not part
// rooms), found once and kept. A room is as bright as the strongest light in it (kLampRoom
// for the lamps, kLiquidRoom, kGlowRoom for a screen or sign), and every picture in it (floor, walls, what stands there)
// gets that ambient; the rest of the map has the base one. The presenter blurs it, so it fades from room to room.
// The light debug mode (F7) shows the rooms and, for a clicked light, its own room and what it gives.
#include "game/game.hpp"

#include "engine/world/collision.hpp"
#include "engine/world/projectile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <initializer_list>

namespace crusader {

namespace {

// A light given to a shape by hand: its colour, radius (half its width, view pixels), intensity, and its centre's
// height above the middle of the picture (view pixels, up is positive), for the frames in `frames` (bit n: frame n),
// and the ambient of the room it lights (0: none, a shot or an explosion lights only itself).
// A shape listed here is never lit by its glowing pixels: its other frames stay dark (a light switched off or shot out).
struct ShapeLight {
    std::uint16_t shape;
    float r, g, b, radius, intensity, rise;
    std::uint32_t frames = 0xFFFFFFFFu;  // every frame
    float room = 0;
    const char* name = nullptr;  // for the light debug mode, when the game has none for it
};

constexpr std::uint32_t frame_bits(std::initializer_list<int> frames) {
    std::uint32_t bits = 0;
    for (int f : frames) bits |= 1u << f;
    return bits;
}

constexpr float kAlertR = 1.0f, kAlertG = 0.16f, kAlertB = 0.1f;  // the red of the alert and alarm lamps
// A room with a real lamp in it (flood, alert, alarm) is a little brighter than the rest (SceneLighting: about 80 %), one
// with a glowing pool a little less; a screen or a sign only glows.
constexpr float kLampRoom = 1.3f;  // about 105 % of the art
constexpr float kGlowRoom = 1.0f;  // the same as elsewhere (80 %): its own glow shows
constexpr float kLiquidRoom = 1.15f;  // a glowing pool or spill: between a screen and a lamp
constexpr int kLiquidMinPixels = 400;

constexpr std::array kShapeLights = {
    // Explosions and flashes (sprite_process.cpp, the ones drawn over the scene).
    ShapeLight{0x31C, 1.0f, 0.62f, 0.28f, 170, 1.0f, 0},
    ShapeLight{0x31F, 1.0f, 0.62f, 0.28f, 150, 1.0f, 0},
    ShapeLight{0x320, 1.0f, 0.62f, 0.28f, 190, 1.0f, 0},
    ShapeLight{0x321, 1.0f, 0.62f, 0.28f, 190, 1.0f, 0},
    ShapeLight{0x322, 1.0f, 0.62f, 0.28f, 200, 1.0f, 0},
    ShapeLight{0x323, 1.0f, 0.62f, 0.28f, 190, 1.0f, 0},
    ShapeLight{0x324, 1.0f, 0.62f, 0.28f, 150, 0.9f, 0},
    ShapeLight{0x325, 1.0f, 0.62f, 0.28f, 150, 0.9f, 0},
    ShapeLight{0x326, 1.0f, 0.62f, 0.28f, 220, 1.0f, 0},
    ShapeLight{0x327, 1.0f, 0.62f, 0.28f, 190, 1.0f, 0},
    // A shot in flight.
    ShapeLight{kProjectileShape, 1.0f, 0.85f, 0.55f, 46, 0.7f, 0},
    // Wall lamps (shape 0xBC): in threes, a flood light, a round alert lamp, a caged alert lamp, in two facings
    // (frames 0..5); frames 6..11 are the same shot out, dark. The flood light shines down onto the floor below it.
    ShapeLight{0xBC, 1.0f, 0.96f, 0.86f, 150, 0.85f, -34, frame_bits({0, 3}), kLampRoom, "FLOOD LIGHT"},
    ShapeLight{0xBC, kAlertR, kAlertG, kAlertB, 80, 0.85f, 0, frame_bits({1, 2, 4, 5}), kLampRoom, "ALERT LAMP"},
    // The alarm lights (world_intrinsics.hpp, set_alert): frame 1 while the alarm is on, dark otherwise.
    ShapeLight{0x21, kAlertR, kAlertG, kAlertB, 100, 0.9f, 0, frame_bits({1}), kLampRoom, "ALARM LIGHT (ON)"},
    ShapeLight{0x49, kAlertR, kAlertG, kAlertB, 100, 0.9f, 0, frame_bits({1}), kLampRoom, "ALARM LIGHT (ON)"},
    ShapeLight{0x174, kAlertR, kAlertG, kAlertB, 100, 0.9f, 0, frame_bits({1}), kLampRoom, "ALARM LIGHT (ON)"},
    ShapeLight{0x271, kAlertR, kAlertG, kAlertB, 100, 0.9f, 0, frame_bits({1}), kLampRoom, "ALARM LIGHT (ON)"},
};

// The light of `shape` in `frame`: its entry, a dark one when the shape is listed for other frames only, null when
// the shape is not listed.
const ShapeLight* shape_light(std::uint16_t shape, std::uint16_t frame) {
    static constexpr ShapeLight kDark{0, 0, 0, 0, 0, 0, 0};
    const ShapeLight* found = nullptr;
    for (const ShapeLight& l : kShapeLights) {
        if (l.shape != shape) continue;
        if (frame < 32 && (l.frames >> frame & 1u)) return &l;
        found = &kDark;
    }
    return found;
}

// The palette's light colours (see above): 1 a light colour, 2 a translucent glow (its entry is only a tint).
constexpr std::array<std::uint8_t, 256> kLightColours = [] {
    std::array<std::uint8_t, 256> t{};
    for (int i = 1; i <= 7; ++i) t[static_cast<std::size_t>(i)] = 1;
    for (int i = 8; i <= 14; ++i) t[static_cast<std::size_t>(i)] = 2;
    // (65, 67, 69, the dim halves of the blue, red and green pairs, shade railings and pipes: not lights)
    for (int i : {15, 63, 64, 66, 68, 70, 71, 95, 160, 175, 223, 251, 252, 254, 255}) t[static_cast<std::size_t>(i)] = 1;
    return t;
}();

constexpr int kGlowMinPixels = 6;  // fewer: a stray pixel of the art, no light
constexpr float kFlashSeconds = 0.09f;

constexpr std::int32_t kRoomCell = 32;     // usecode units: the step of the room search
constexpr int kMaxRoomCells = 20000;       // a room bigger than this is cut there
constexpr int kRoomSearchesPerPass = 6;    // new rooms searched per refresh (the rest wait for the next)
constexpr int kRefreshFrames = 30;         // frames between refreshes of the lights' rooms and levels
constexpr std::int32_t kLightRange = 3000;   // usecode units from the controlled NPC (x and y) where lights count
constexpr std::int32_t kProbe = 8, kProbeHeight = 32;  // the room search's probe: what fits in a doorway, not a crate's gap
constexpr std::int32_t kStair = 10;        // the most the floor rises or drops between two cells of a room
constexpr std::int32_t kWallReach = 160;   // how far above a room's floor a tall picture still belongs to it (stacked walls)
constexpr std::int32_t kWallHeight = 80;   // a wall: solid, thin, at least this tall, nameless (is_wall)
constexpr std::int32_t kWallLook = 32;     // the height above the floor where walls are looked for
constexpr std::uint16_t kSolidMask = 1;    // solid shapes (collision.hpp)

std::int32_t floor_div(std::int32_t a, std::int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

std::int64_t cell_key(std::int32_t x, std::int32_t y) {
    return static_cast<std::int64_t>(floor_div(x, kRoomCell)) << 32 ^ static_cast<std::uint32_t>(floor_div(y, kRoomCell));
}

}  // namespace

// @orig none: port-only lighting
const Game::Glow& Game::glow_of(const ShapeFrame& frame) {
    const auto found = glows_.find(&frame);
    if (found != glows_.end()) return found->second;
    Glow g;
    std::array<int, 256> counts{}, all{};
    double sx = 0, sy = 0;
    Surface8 scratch(std::max(frame.width, 1), std::max(frame.height, 1));
    scratch.clear(0);  // colour 0 is not a light one
    frame.draw(scratch, frame.xoff, frame.yoff);
    for (int y = 0; y < frame.height; ++y) {
        const std::uint8_t* row = scratch.row(y);
        for (int x = 0; x < frame.width; ++x) {
            const std::uint8_t c = row[x];
            ++all[c];
            if (!kLightColours[c]) continue;
            ++counts[c];
            ++g.count;
            sx += x;
            sy += y;
        }
    }
    if (g.count > 0) {
        g.cx = static_cast<float>(sx / g.count);
        g.cy = static_cast<float>(sy / g.count);
        g.colour = static_cast<std::uint8_t>(std::max_element(counts.begin(), counts.end()) - counts.begin());
    }
    all[0] = 0;  // (the clear colour)
    for (std::size_t i = 1; i < all.size(); ++i) g.pixels += all[i];
    g.main = static_cast<std::uint8_t>(std::max_element(all.begin(), all.end()) - all.begin());
    return glows_.emplace(&frame, g).first->second;
}

// Whether `shape` is a wall, the only thing that parts two rooms: solid, thin (one footpad unit), full height (kWallHeight
// and more) and nameless (doors, machines, railings, crates are named or low), the posts that join wall pieces included.
// @orig none: port-only lighting
bool Game::is_wall(std::uint16_t shape) {
    const auto found = wall_shapes_.find(shape);
    if (found != wall_shapes_.end()) return found->second;
    const ShapeInfo* info = world_->shape_info(shape);
    const bool wall = info && info->is(ShapeInfo::kSolid) && info->footpad_z * 8 >= kWallHeight &&
                      std::min(info->footpad_x, info->footpad_y) <= 1 && object_name(shape).empty();
    return wall_shapes_.emplace(shape, wall).first->second;
}

// Whether a wall (is_wall) stands at (x, y) across the height z + kWallLook, kept per 8 units (wall_at_uncached).
// @orig none: port-only lighting
bool Game::wall_at(std::int32_t x, std::int32_t y, std::int32_t z) {
    const std::int64_t key = (static_cast<std::int64_t>(floor_div(x, 8)) << 36) ^ (static_cast<std::int64_t>(floor_div(y, 8)) << 12) ^ (z >> 4);
    if (const auto found = wall_cache_.find(key); found != wall_cache_.end()) return found->second;
    return wall_cache_.emplace(key, wall_at_uncached(x, y, z)).first->second;
}

// What wall_at keeps (walls the game hides are not there).
// @orig none: port-only lighting
bool Game::wall_at_uncached(std::int32_t x, std::int32_t y, std::int32_t z) {
    if (x < 0 || y < 0) return false;
    const std::int32_t look = z + kWallLook;
    for (int gy = y >> World::kCellShift; gy <= (y + 0x100) >> World::kCellShift; ++gy) {
        for (int gx = x >> World::kCellShift; gx <= (x + 0x100) >> World::kCellShift; ++gx) {
            for (const ObjId id : world_->cell(gx, gy)) {
                const Item* item = world_->get(id);
                if (!item || item->sprite_only || (item->flags & (kItemInvisible | kItemEthereal)) || !is_wall(item->shape)) continue;
                const ShapeInfo* wi = world_->shape_info(item->shape);
                if (item->z > look || item->z + wi->footpad_z * 8 < look) continue;
                const bool mirrored = item->flags & kItemFlipped;
                const std::int32_t w = (mirrored ? wi->footpad_y : wi->footpad_x) * 16, d = (mirrored ? wi->footpad_x : wi->footpad_y) * 16;
                if (x <= item->x && x > item->x - w && y <= item->y && y > item->y - d) return true;
            }
        }
    }
    return false;
}

// Where a small probe (kProbe across, kProbeHeight high) stands at (x, y): on the top of a solid item under it (a floor
// tile has no height: its own z), the one nearest `z` within `reach` where the probe is free. As the walk's support
// (collision.cpp), not box_free, which never meets a floor of no height. Characters are neither floor nor obstacle.
// @orig none: port-only lighting
std::optional<std::int32_t> Game::probe_stand(std::int32_t x, std::int32_t y, std::int32_t z, std::int32_t reach, bool need_room) const {
    const std::int32_t x0 = x - kProbe + 1, y0 = y - kProbe + 1;
    if (x0 < 0 || y0 < 0) return std::nullopt;
    // The solid items over the probe's column (characters are not obstacles: they move), their tops near `z` to stand
    // on and their heights that block it.
    std::array<std::int32_t, 32> tops{}, low{}, high{};
    int n_tops = 0, n_solids = 0;
    for (int gy = y0 >> World::kCellShift; gy <= (y + 0x100) >> World::kCellShift; ++gy) {
        for (int gx = x0 >> World::kCellShift; gx <= (x + 0x100) >> World::kCellShift; ++gx) {
            for (const ObjId id : world_->cell(gx, gy)) {
                const Item* c = world_->get(id);
                if (!c || c->sprite_only || is_npc_id(id) || (c->flags & (kItemInvisible | kItemEthereal))) continue;  // (hidden: not there)
                const ShapeInfo* info = world_->shape_info(c->shape);
                if (!info || !info->is(ShapeInfo::kSolid)) continue;
                const std::int32_t cx = static_cast<std::uint16_t>(c->x), cy = static_cast<std::uint16_t>(c->y);
                if (cx < x0 || cy < y0 || cx - info->footpad_x * 16 + 1 > x || cy - info->footpad_y * 16 + 1 > y) continue;
                const std::int32_t bottom = c->z & 0xFF, top = bottom + info->footpad_z * 8;
                if (std::abs(top - z) <= reach && n_tops < static_cast<int>(tops.size())) tops[static_cast<std::size_t>(n_tops++)] = top;
                if (top > bottom && n_solids < static_cast<int>(low.size())) {
                    low[static_cast<std::size_t>(n_solids)] = bottom, high[static_cast<std::size_t>(n_solids)] = top - 1;
                    ++n_solids;
                }
            }
        }
    }
    std::optional<std::int32_t> best;
    for (int t = 0; t < n_tops; ++t) {
        const std::int32_t top = tops[static_cast<std::size_t>(t)];
        if (best && std::abs(top - z) >= std::abs(*best - z)) continue;
        bool free = true;
        if (!need_room) {
            best = top;
            continue;
        }
        for (int s = 0; s < n_solids && free; ++s) {
            free = high[static_cast<std::size_t>(s)] < top || low[static_cast<std::size_t>(s)] > top + kProbeHeight - 1;
        }
        if (free) best = top;
    }
    return best;
}

// The room `light` lights: the floor around it, searched when no room holds that floor yet; -1 when there is none.
// The start is the lowest floor near the light (not the top of the truss, desk or console it is on): under it, then
// around it, nearest first, the facing of its frame first (a wall lamp faces +x or +y, the camera's side of its wall).
// The room is then every cell reached a step of kRoomCell at a time where the probe stands within a stair's height of
// the last cell, there and halfway (a thin wall between two cells parts them). Crates and machines do not cut it.
// @orig none: port-only lighting
int Game::find_room(const Item& light) {
    const World::Point at = world_->location(light);
    const bool second_facing = light.frame % 6 >= 3;
    const std::array<std::array<int, 2>, 4> sides = second_facing
        ? std::array<std::array<int, 2>, 4>{{{0, 1}, {1, 0}, {0, -1}, {-1, 0}}}
        : std::array<std::array<int, 2>, 4>{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
    const auto lowest = [&](std::int32_t x, std::int32_t y) -> std::optional<World::Point> {
        for (std::int32_t z = std::max(at.z - 160, 0); z <= std::min(at.z + 8, 240); z += 8) {  // never above it
            if (const auto stand = probe_stand(x, y, z, 4)) return World::Point{x, y, *stand};
        }
        return std::nullopt;
    };
    // The camera's side first (+x, +y: a lamp is drawn on the face of its wall that is seen); under the light and the
    // other sides only when nothing stands there.
    std::optional<World::Point> seed;
    const auto lowest_of = [&](std::initializer_list<std::array<int, 2>> dirs) {
        for (int dist : {16, 32, 48, 64, 96, 128}) {
            for (const auto& side : dirs) {
                const auto p = lowest(at.x + side[0] * dist, at.y + side[1] * dist);
                if (p && (!seed || p->z < seed->z)) seed = p;
            }
        }
    };
    lowest_of({sides[0], sides[1], {1, 1}});
    if (!seed) seed = lowest(at.x, at.y);
    if (!seed) lowest_of({{1, -1}, {-1, 1}, {-1, 0}, {0, -1}, {-1, -1}});
    if (!seed) return -1;
    return room_from(*seed);
}

// The room of the floor at `seed`: the one that holds it already, else a new one: every floor cell joined to it a step of
// kRoomCell at a time, with floor within a stair's height and no wall (is_wall) on the way. Only walls part rooms: what
// stands on the floor (crates, pipes, railings, machines) is in the room.
// @orig none: port-only lighting
int Game::room_from(const World::Point& seed) {
    if (const auto known = room_cells_.find(cell_key(seed.x, seed.y)); known != room_cells_.end()) {
        if (std::abs(known->second.z - seed.z) <= kStair * 2) return known->second.room;
    }
    constexpr std::array<std::array<int, 2>, 4> kSides{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};
    const int index = static_cast<int>(rooms_.size());
    Room room;
    room.seed = seed;
    std::deque<World::Point> open{seed};
    room_cells_[cell_key(seed.x, seed.y)] = {index, seed.z};
    room.cells.push_back(cell_key(seed.x, seed.y));
    std::vector<int> touching;  // rooms found before that this one reaches (joined to it at the end)
    while (!open.empty() && static_cast<int>(room.cells.size()) < kMaxRoomCells) {
        const World::Point here = open.front();
        open.pop_front();
        for (const auto& side : kSides) {
            const std::int32_t x = here.x + side[0] * kRoomCell, y = here.y + side[1] * kRoomCell;
            const std::int64_t key = cell_key(x, y);
            const auto owner = room_cells_.find(key);
            if (owner != room_cells_.end() && owner->second.room == index) continue;
            // Floor there (whatever stands on it: crates, pipes, railings, machines are in the room), within a stair's
            // height, and no wall on the way (looked for every 8 units: a wall is 16 thick).
            const auto z = owner != room_cells_.end() ? std::optional<std::int32_t>{owner->second.z}
                                                      : probe_stand(x, y, here.z, kStair, false);
            if (!z || std::abs(*z - here.z) > kStair) continue;
            bool walled = false;
            for (std::int32_t t = 8; t <= kRoomCell && !walled; t += 8) {
                walled = wall_at(here.x + side[0] * t, here.y + side[1] * t, std::min(here.z, *z));
            }
            if (walled) continue;
            if (owner != room_cells_.end()) {
                touching.push_back(owner->second.room);  // the same floor as a room found before: one room
                continue;
            }
            room_cells_[key] = {index, *z};
            room.cells.push_back(key);
            open.push_back({x, y, *z});
        }
    }
    // The rooms it reached: theirs are its cells now (and their lights its lights: live_room).
    for (int other : touching) {
        other = live_room(other);
        if (other == index || other < 0) continue;
        Room& o = rooms_[static_cast<std::size_t>(other)];
        for (const std::int64_t key : o.cells) room_cells_[key].room = index;
        room.cells.insert(room.cells.end(), o.cells.begin(), o.cells.end());
        o.cells.clear();
        o.merged = index;
    }
    // Its name: its north-west floor cell (the smallest x, then y), whatever order the rooms were found in.
    {
        const auto cell_x = [](std::int64_t key) { return static_cast<std::int32_t>(key >> 32); };
        const auto cell_y = [](std::int64_t key) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(key)); };
        const auto first = std::min_element(room.cells.begin(), room.cells.end(), [&](std::int64_t a, std::int64_t b) {
            return cell_x(a) != cell_x(b) ? cell_x(a) < cell_x(b) : cell_y(a) < cell_y(b);
        });
        room.label_x = cell_x(*first), room.label_y = cell_y(*first);
    }
    rooms_.push_back(std::move(room));
    room_levels_.push_back(0);
    return index;
}

// A room's name for the light debug mode: "ROOM x.y", its north-west floor cell in cells of kRoomCell (stable).
std::string Game::room_name(int room) const {
    if (room < 0 || room >= static_cast<int>(rooms_.size())) return "NO ROOM";
    const Room& r = rooms_[static_cast<std::size_t>(room)];
    return "ROOM " + std::to_string(r.label_x) + "." + std::to_string(r.label_y);
}

// A room, or the one it was joined to (a room search that reached it).
int Game::live_room(int room) const {
    while (room >= 0 && room < static_cast<int>(rooms_.size()) && rooms_[static_cast<std::size_t>(room)].merged >= 0) {
        room = rooms_[static_cast<std::size_t>(room)].merged;
    }
    return room;
}

// The room someone standing at `at` is in (searched when no room holds it yet); -1 when there is no floor there.
// @orig none: port-only lighting
int Game::room_at(const World::Point& at, std::int32_t reach) {
    const auto stand = probe_stand(at.x, at.y, at.z, reach < 0 ? kStair : reach, false);
    return stand ? room_from(World::Point{at.x, at.y, *stand}) : -1;
}

// The room the cell at (x, y) belongs to, -1 for none, and its floor's z.
int Game::room_cell(std::int32_t x, std::int32_t y, std::int32_t* z) const {
    const auto found = room_cells_.find(cell_key(x, y));
    if (found == room_cells_.end()) return -1;
    if (z) *z = found->second.z;
    return found->second.room;
}

// What a liquid is: a flat picture that moves (a pool, a spill, a leak), big enough not to be a stain.
// @orig none: port-only lighting
bool Game::is_liquid(const Item& item, const Glow& g) const {
    const ShapeInfo* info = world_->shape_info(item.shape);
    return info && info->footpad_z == 0 && info->anim_type != 0 && g.pixels >= kLiquidMinPixels;
}

// The name the game gives a shape: the one shown when it is picked up, else its usecode class's (Y_POOL, ALARMBOX...);
// empty when it has none. Kept per shape.
// @orig none: port-only lighting
const std::string& Game::object_name(std::uint16_t shape) {
    const auto found = object_names_.find(shape);
    if (found != object_names_.end()) return found->second;
    std::string name;
    if (dtable::weapon_index(shape) > 0) name = dtable::weapon_name(shape);
    else if (dtable::inventory_item_index(shape) > 0) name = dtable::item_name(shape);
    else if (dtable::ammo_index(shape) > 0) name = dtable::ammo_name(shape);
    else if (dtable::family12_index(shape) > 0) name = dtable::family12_name(shape);
    else if (std::string c = usecode_.class_name(shape); c.rfind("class_", 0) != 0) name = std::move(c);
    return object_names_.emplace(shape, std::move(name)).first->second;
}

// The ambient `item` gives the room it is in, as a light: its entry in kShapeLights, a liquid's, a glow's; 0 for none.
// @orig none: port-only lighting
float Game::room_light_level(const Item& item) {
    if (const ShapeLight* l = shape_light(item.shape, item.frame)) return l->room;
    const Shape* shape = shapes_.get(item.shape);
    if (!shape || item.frame >= shape->frames().size()) return 0;
    const Glow& g = glow_of(shape->frames()[item.frame]);
    if (is_liquid(item, g)) return kLiquidRoom;
    // A glow counts only on something the game names (a screen, a sign, a console): unnamed scenery with a few bright
    // pixels painted in does not light a room.
    return g.count >= kGlowMinPixels && !object_name(item.shape).empty() ? kGlowRoom : 0;
}

// Every few frames: the rooms of the lights of the map (a few new searches at a time), and how bright each room is.
// @orig none: port-only lighting
void Game::refresh_rooms() {
    // The rooms again from nothing on a new map, and when the game shows or hides an area (its items' invisible flags:
    // a hidden floor is no floor, a hidden wall no wall).
    const auto forget_rooms = [this] {
        room_cells_.clear();
        rooms_.clear();
        room_levels_.clear();
        light_rooms_.clear();
        wall_cache_.clear();
        player_room_ = -1;
        light_debug_room_ = -1;
        rooms_age_ = kRefreshFrames;
    };
    if (rooms_map_ != world_->current_map()) {
        rooms_map_ = world_->current_map();
        forget_rooms();
    }
    if (++rooms_age_ < kRefreshFrames) return;
    rooms_age_ = 0;
    std::size_t hidden = 0;
    world_->for_each([&](const Item& item) {
        if (!is_npc_id(item.id) && (item.flags & (kItemInvisible | kItemEthereal))) ++hidden;  // (scenery: not a cloaked NPC)
    });
    if (hidden != rooms_hidden_) {
        rooms_hidden_ = hidden;
        forget_rooms();
        rooms_age_ = 0;
    }
    std::fill(room_levels_.begin(), room_levels_.end(), 0.0f);
    int searches = 0;
    // The lights near the controlled NPC: the rest of the map waits until it comes near (and the scripts keep spare items
    // far off the level, which light nothing).
    const Item* npc = world_->get(world_->state.controlled_npc);
    if (!npc) return;
    const World::Point centre = world_->location(*npc);
    world_->for_each([&](const Item& item) {
        if (item.parent != 0 || item.sprite_only || item.off_map || is_npc_id(item.id)) return;
        if (item.flags & (kItemInvisible | kItemEthereal)) return;  // a light the game hides lights nothing
        if (std::abs(item.x - centre.x) > kLightRange || std::abs(item.y - centre.y) > kLightRange) return;
        const float level = room_light_level(item);
        if (level <= 0) return;
        auto known = light_rooms_.find(item.id);
        if (known == light_rooms_.end()) {
            if (searches >= kRoomSearchesPerPass) return;
            ++searches;
            known = light_rooms_.emplace(item.id, find_room(item)).first;
        }
        if (live_room(known->second) >= 0) {
            float& room = room_levels_[static_cast<std::size_t>(live_room(known->second))];
            room = std::max(room, level);
        }
    });
    if (searches >= kRoomSearchesPerPass) rooms_age_ = kRefreshFrames;  // more to search: again next frame
}

// The ambient of the room a picture stands in: the brightest lit room among the cells under it and next to it (walls
// stand on a room's edge). A flat picture (a floor tile) belongs to the rooms whose floor it is (at its height); a tall
// one (a wall, a machine, the upper pieces of a high wall) to the rooms it rises from or above.
// @orig none: port-only lighting
float Game::room_level(const DrawItem& d) const {
    if (room_cells_.empty()) return 0;
    const Item& item = *d.item;
    const ShapeInfo* info = world_->shape_info(item.shape);
    const bool mirrored = item.flags & kItemFlipped;
    const std::int32_t w = info ? (mirrored ? info->footpad_y : info->footpad_x) * 16 : 0;
    const std::int32_t h = info ? (mirrored ? info->footpad_x : info->footpad_y) * 16 : 0;
    const std::int32_t height = info ? info->footpad_z * 8 : 0;
    const std::int32_t top = item.z + height;
    float level = 0;
    for (std::int32_t x = item.x - w - kRoomCell; x <= item.x + kRoomCell; x += kRoomCell) {
        for (std::int32_t y = item.y - h - kRoomCell; y <= item.y + kRoomCell; y += kRoomCell) {
            const auto found = room_cells_.find(cell_key(x, y));
            if (found == room_cells_.end()) continue;
            const RoomCell& cell = found->second;
            const bool in_room = height <= 8 ? std::abs(top - cell.z) <= 16                       // its floor
                                             : item.z >= cell.z - 32 && item.z <= cell.z + kWallReach;  // rising from it
            if (in_room) level = std::max(level, room_levels_[static_cast<std::size_t>(cell.room)]);
        }
    }
    return level;
}

// The muzzle flashes of the shots fired since the last pass, then their fading.
// @orig none: port-only lighting
void Game::update_flashes(float seconds) {
    if (!world_) return;
    for (const GameState::MuzzleFlash& m : world_->state.muzzle_flashes) {
        if (flashes_.size() < 32) {
            flashes_.push_back({static_cast<float>(m.x), static_cast<float>(m.y), static_cast<float>(m.z), kFlashSeconds,
                                m.fire_type});
        }
    }
    world_->state.muzzle_flashes.clear();
    for (Flash& f : flashes_) f.left -= std::min(seconds, 0.05f);
    std::erase_if(flashes_, [](const Flash& f) { return f.left <= 0; });
}

// The lights of the scene of this frame, from its display list.
// @orig none: port-only lighting
void Game::build_lights(const std::vector<DrawItem>& list) {
    lighting_.lights.clear();
    light_spots_.clear();
    if (!config_.lighting) return;
    const GameState& s = world_->state;
    const Palette& palette = s.screen_palette;
    for (const DrawItem& d : list) {
        if (!d.frame) continue;
        if (!is_npc_id(d.item->id) && room_light_level(*d.item) > 0) {
            light_spots_.push_back({d.item->id, d.left, d.top, d.right, d.bottom});
        }
        const float cx = static_cast<float>(d.left + d.right) * 0.5f, cy = static_cast<float>(d.top + d.bottom) * 0.5f;
        if (const ShapeLight* l = shape_light(d.item->shape, d.item->frame)) {
            if (l->radius > 0) lighting_.lights.push_back({cx, cy - l->rise, l->radius, l->r, l->g, l->b, l->intensity});
            continue;
        }
        if (d.item->id == s.controlled_npc) {
            // Around the controlled NPC: from about the chest, not too strong (it lights what it is near, not a torch).
            lighting_.lights.push_back({static_cast<float>(d.sx), static_cast<float>(d.sy) - 20, 130, 0.5f, 0.52f, 0.56f, 1.0f});
            continue;
        }
        if (d.shape_flags & ShapeInfo::kNpc) continue;  // what a character wears or carries does not light the room
        const Glow& g = glow_of(*d.frame);
        if (is_liquid(*d.item, g)) {
            // A pool glows in its own colour, over all of it.
            const Rgb& c = palette.colors[g.main];
            const float r = c.r / 255.0f, gr = c.g / 255.0f, b = c.b / 255.0f;
            const float bright = std::max({r, gr, b, 0.01f});
            const float radius = std::clamp(static_cast<float>(d.right - d.left) * 0.7f, 60.0f, 220.0f);
            lighting_.lights.push_back({cx, cy, radius, r / bright, gr / bright, b / bright, 0.75f});
            continue;
        }
        if (g.count < kGlowMinPixels) continue;
        const Rgb& c = palette.colors[g.colour];
        float r = c.r / 255.0f, gr = c.g / 255.0f, b = c.b / 255.0f;
        const float bright = std::max({r, gr, b});
        if (bright < 0.06f) continue;  // the cycle has taken it dark for now
        if (kLightColours[g.colour] == 2) r /= bright, gr /= bright, b /= bright;  // a glow's tint, at full strength
        const float size = std::sqrt(static_cast<float>(g.count));
        const float x = static_cast<float>(d.left) + (d.mirrored ? static_cast<float>(d.frame->width - 1) - g.cx : g.cx);
        const float y = static_cast<float>(d.top) + g.cy;
        lighting_.lights.push_back({x, y, std::clamp(28.0f + 7.0f * size, 36.0f, 160.0f), r, gr, b,
                                    std::clamp(0.45f + 0.04f * size, 0.5f, 0.8f)});
    }
    for (const Flash& f : flashes_) {
        const auto ux = static_cast<std::int16_t>(f.x), uy = static_cast<std::int16_t>(f.y);
        const float x = static_cast<float>(((ux - uy) >> 1) - s.view_left);
        const float y = static_cast<float>((static_cast<std::uint16_t>(ux + uy) >> 2) - static_cast<int>(f.z) - s.view_top);
        lighting_.lights.push_back({x, y, 120, 1.0f, 0.82f, 0.5f, 1.1f * f.left / kFlashSeconds});
    }
    build_light_debug();
}

// The light debug mode's overlay: the floor cells of the rooms near the view (all of them, or the clicked light's), the
// lights on screen, where each room's search started, and a line on what is shown.
// @orig none: port-only lighting
void Game::build_light_debug() {
    LightDebugOverlay& o = light_debug_overlay_;
    o.on = light_debug_ && config_.lighting;
    o.cells.clear();
    o.marks.clear();
    o.labels.clear();
    o.text.clear();
    if (!o.on) return;
    const GameState& s = world_->state;
    // The room the controlled NPC stands in (the middle of its footprint).
    if (const Item* npc = world_->get(s.controlled_npc)) {
        const ShapeInfo* info = world_->shape_info(npc->shape);
        World::Point at = world_->location(*npc);
        at.x -= info ? info->footpad_x * 8 : 0;
        at.y -= info ? info->footpad_y * 8 : 0;
        player_room_ = room_at(at);
    }
    const auto project = [&](std::int32_t x, std::int32_t y, std::int32_t z, float& sx, float& sy) {
        sx = static_cast<float>((x - y) / 2 - s.view_left);
        sy = static_cast<float>((x + y) / 4 - z - s.view_top);
    };
    int picked_room = -1;  // -1: all rooms shown
    if (light_debug_pick_) {
        const auto found = light_rooms_.find(light_debug_pick_);
        picked_room = found != light_rooms_.end() ? live_room(found->second) : -2;  // -2: not searched yet
        if (picked_room == -1) picked_room = -3;                        // -3: searched, no floor
    } else if (light_debug_room_ != -1) {
        picked_room = light_debug_room_ >= 0 ? live_room(light_debug_room_) : -3;
    } else if (light_debug_own_room_ && player_room_ >= 0) {
        picked_room = live_room(player_room_);
    }
    if (picked_room >= static_cast<int>(rooms_.size())) picked_room = -2;  // (from before the rooms were searched again)
    const int w = s.screen_right + 1, h = s.screen_bottom + 1;
    for (std::size_t r = 0; r < rooms_.size(); ++r) {
        if (rooms_[r].merged >= 0 || (picked_room != -1 && static_cast<int>(r) != picked_room)) continue;
        const float level = room_levels_[r];
        // A colour per room by its name (the same in every run), brighter for a brighter room (dim: no light in it now).
        const std::uint32_t hash = (static_cast<std::uint32_t>(rooms_[r].label_x) * 73856093u) ^ (static_cast<std::uint32_t>(rooms_[r].label_y) * 19349663u);
        const float k = level > 0 ? 0.6f + 0.4f * std::min(level / kLampRoom, 1.0f) : 0.35f;
        const auto channel = [&](int shift) { return static_cast<std::uint8_t>(static_cast<float>(90 + ((hash >> shift) & 0x7F)) * k); };
        float lx = 0, ly = 0;  // the middle of its cells on screen, for its name
        int shown = 0;
        for (const std::int64_t key : rooms_[r].cells) {
            const auto cell = room_cells_.find(key);
            if (cell == room_cells_.end()) continue;
            const auto x0 = static_cast<std::int32_t>(key >> 32) * kRoomCell;
            const auto y0 = static_cast<std::int32_t>(static_cast<std::uint32_t>(key)) * kRoomCell;
            const std::int32_t cx[4] = {x0, x0 + kRoomCell, x0 + kRoomCell, x0}, cy[4] = {y0, y0, y0 + kRoomCell, y0 + kRoomCell};
            LightDebugOverlay::Cell c{};
            bool near = false;
            for (int i = 0; i < 4; ++i) {
                project(cx[i], cy[i], cell->second.z, c.x[i], c.y[i]);
                near = near || (c.x[i] > -64 && c.x[i] < w + 64 && c.y[i] > -64 && c.y[i] < h + 64);
            }
            if (!near) continue;
            c.r = channel(0), c.g = channel(8), c.b = channel(16);
            lx += (c.x[0] + c.x[2]) * 0.5f, ly += (c.y[0] + c.y[2]) * 0.5f, ++shown;
            o.cells.push_back(c);
        }
        float sx, sy;
        project(rooms_[r].seed.x, rooms_[r].seed.y, rooms_[r].seed.z, sx, sy);
        o.marks.push_back({sx, sy, true, picked_room == static_cast<int>(r)});
        if (shown > 0) o.labels.push_back({lx / static_cast<float>(shown), ly / static_cast<float>(shown), room_name(static_cast<int>(r))});
    }
    for (const LightSpot& l : light_spots_) {
        o.marks.push_back({(l.left + l.right) * 0.5f, (l.top + l.bottom) * 0.5f, false, l.id == light_debug_pick_});
    }
    char line[256];
    if (light_debug_pick_) {
        const Item* item = world_->get(light_debug_pick_);
        const float level = item ? room_light_level(*item) : 0;
        const char* kind = !item ? "" : shape_light(item->shape, item->frame) ? "LAMP" : [&] {
            const Shape* shape = shapes_.get(item->shape);
            return shape && item->frame < shape->frames().size() && is_liquid(*item, glow_of(shape->frames()[item->frame])) ? "LIQUID" : "GLOW";
        }();
        // Its name: the game's (object_name), else the one kShapeLights gives it.
        std::string name = item ? object_name(item->shape) : std::string{};
        if (const ShapeLight* l = item ? shape_light(item->shape, item->frame) : nullptr; name.empty() && l && l->name) name = l->name;
        if (name.empty()) name = "(NO NAME)";
        if (picked_room >= 0) {
            const auto room = static_cast<std::size_t>(picked_room);
            std::snprintf(line, sizeof line, "%s %s (0x%X FRAME %d) GIVES %.1f - %s: %zu CELLS, AMBIENT %.1f (CLICK A FLOOR: ITS ROOM; F7 TWICE: ALL)", kind,
                          name.c_str(), item ? item->shape : 0, item ? item->frame : 0, static_cast<double>(level), room_name(picked_room).c_str(),
                          rooms_[room].cells.size(), static_cast<double>(room_levels_[room]));
        } else {
            std::snprintf(line, sizeof line, "%s %s (0x%X FRAME %d) GIVES %.1f - %s", kind, name.c_str(), item ? item->shape : 0,
                          item ? item->frame : 0, static_cast<double>(level), picked_room == -3 ? "NO FLOOR FOUND NEAR IT" : "NOT SEARCHED YET");
        }
    } else if (picked_room >= 0 || light_debug_room_ == -2) {
        // A room by its floor: the one clicked, or the NPC's own.
        const bool clicked = light_debug_room_ != -1;
        if (clicked) {
            float sx, sy;
            project(light_debug_tile_.x, light_debug_tile_.y, light_debug_tile_.z, sx, sy);
            o.marks.push_back({sx, sy, false, true});
        }
        if (picked_room >= 0) {
            const auto room = static_cast<std::size_t>(picked_room);
            std::snprintf(line, sizeof line, "%s %s: %zu CELLS, AMBIENT %.1f%s - CLICK A FLOOR OR A LIGHT", clicked ? "CLICKED" : "YOUR",
                          room_name(picked_room).c_str(), rooms_[room].cells.size(), static_cast<double>(room_levels_[room]),
                          picked_room == live_room(player_room_) ? " (YOU ARE IN IT)" : "");
        } else {
            std::snprintf(line, sizeof line, "NO FLOOR THERE (AT YOUR FLOOR'S HEIGHT) - CLICK A FLOOR OR A LIGHT");
        }
    } else {
        std::snprintf(line, sizeof line, "LIGHT DEBUG (F7): %zu ROOMS, %zu LIGHTS ON SCREEN - CLICK A LIGHT OR A FLOOR", rooms_.size(),
                      light_spots_.size());
    }
    o.text = line;
}

// A click in the light debug mode: the light under the pointer (the smallest picture there), or none (all rooms again).
// @orig none: port-only lighting
void Game::light_debug_click(int screen_x, int screen_y) {
    light_debug_pick_ = 0;
    int best = 1 << 30;
    for (const LightSpot& l : light_spots_) {
        if (screen_x < l.left || screen_x > l.right || screen_y < l.top || screen_y > l.bottom) continue;
        const int area = (l.right - l.left) * (l.bottom - l.top);
        if (area < best) best = area, light_debug_pick_ = l.id;
    }
    light_debug_room_ = -1;
    if (light_debug_pick_ || !world_) return;
    // No light there: the floor under the pointer, at the controlled NPC's floor height (the view's projection undone).
    const GameState& s = world_->state;
    const Item* npc = world_->get(s.controlled_npc);
    if (!npc) return;
    const std::int32_t z = world_->location(*npc).z;
    const std::int32_t a = 2 * (screen_x + s.view_left), b = 4 * (screen_y + s.view_top + z);  // x - y, x + y
    light_debug_tile_ = World::Point{(a + b) / 2, (b - a) / 2, z};
    const int room = room_at(light_debug_tile_, 64);  // (a floor a little up or down from the NPC's too)
    light_debug_room_ = room >= 0 ? room : -2;
}

}  // namespace crusader
