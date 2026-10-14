#include "engine/world/world.hpp"

#include "core/bytes.hpp"
#include "engine/audio/audio.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <tuple>

namespace crusader {

namespace {
constexpr std::size_t kRecordSize = 16;
constexpr std::uint8_t kFamilyGlobEgg = 3;
}  // namespace

// @orig 10B0:01E7
std::vector<GlobItem> read_glob(const FlexArchive& globs, std::size_t index) {
    const auto d = globs.entry(index);
    std::vector<GlobItem> items;
    if (d.size() < 2) return items;
    const std::uint16_t count = read_u16(d, 0);
    items.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t pos = 2 + 6 * i;
        items.push_back({read_u8(d, pos), read_u8(d, pos + 1), read_u8(d, pos + 2), read_u16(d, pos + 3),
                         read_u8(d, pos + 5)});
    }
    return items;
}

// @orig 10B0:01E7
std::size_t World::expand_glob_egg(const FlexArchive& globs, ObjId egg_id) {
    // GLOB.C: each item is created, popped to its place and flagged 0x82.
    // Coordinates in usecode units: x = (egg.x & ~0x1FF) + 2 * gx + 1.
    const Item* egg = get(egg_id);
    if (!egg || family(*egg) != kFamilyGlobEgg || egg->quality == 0) return 0;
    const std::int32_t base_x = egg->x & ~0x1FF, base_y = egg->y & ~0x1FF, base_z = egg->z;
    std::size_t created = 0;
    expanding_glob_ = true;
    for (const GlobItem& g : read_glob(globs, egg->quality)) {
        const ObjId id = create(g.shape, g.frame);
        if (!id) continue;
        pop_to_coords(base_x + 2 * g.x + 1, base_y + 2 * g.y + 1, (base_z + g.z) & 0xFF);
        get(id)->flags |= kItemDisposable | 0x0002;
        ++created;
    }
    expanding_glob_ = false;
    return created;
}

// @orig 10B0:01E7
std::size_t World::expand_glob_eggs(const FlexArchive& globs) {
    std::vector<ObjId> eggs;
    for_each([&](const Item& i) {
        if (family(i) == kFamilyGlobEgg && i.parent == 0) eggs.push_back(i.id);
    });
    // Items already in the world, by position and look, to skip eggs whose
    // items a saved game already contains.
    std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t, std::uint16_t, std::uint16_t>> present;
    for_each([&](const Item& i) {
        if (i.parent == 0) present.emplace(i.x, i.y, i.z, i.shape, i.frame);
    });
    std::size_t created = 0;
    for (ObjId egg : eggs) {
        const Item& e = *get(egg);
        if (e.quality == 0) continue;
        bool saved = false;
        for (const GlobItem& g : read_glob(globs, e.quality)) {
            saved |= present.count({(e.x & ~0x1FF) + 2 * g.x + 1, (e.y & ~0x1FF) + 2 * g.y + 1, (e.z + g.z) & 0xFF,
                                    g.shape, g.frame}) != 0;
        }
        if (!saved) created += expand_glob_egg(globs, egg);
    }
    return created;
}

// @orig 10B8:03EF 10B0:05D6
std::vector<FixedRecord> read_fixed_map(const FlexArchive& fixed, std::size_t map) {
    const auto d = fixed.entry(map);
    std::vector<FixedRecord> records;
    records.reserve(d.size() / kRecordSize);
    for (std::size_t pos = 0; pos + kRecordSize <= d.size(); pos += kRecordSize) {
        FixedRecord r;
        r.x = read_u16(d, pos);
        r.y = read_u16(d, pos + 2);
        r.z = read_u8(d, pos + 4);
        r.shape = read_u16(d, pos + 5);
        r.frame = read_u8(d, pos + 7);
        r.flags = read_u16(d, pos + 8);
        r.quality = read_u16(d, pos + 10);
        r.npc_number = read_u8(d, pos + 12);
        r.map_number = read_u8(d, pos + 13);
        r.next = read_u16(d, pos + 14);
        records.push_back(r);
    }
    return records;
}

Item* World::get(ObjId id) {
    auto it = items_.find(id);
    return it == items_.end() ? nullptr : &it->second;
}

const Item* World::get(ObjId id) const {
    auto it = items_.find(id);
    return it == items_.end() ? nullptr : &it->second;
}

// @orig 10B0:262A
Item& World::add(Item item) {
    ObjId id = kFirstItemId;
    while (items_.contains(id)) {
        if (id == 0xFFFF) throw std::runtime_error("item table full");
        ++id;
    }
    item.id = id;
    if (id >= next_id_) next_id_ = static_cast<ObjId>(id + 1);
    Item& added = items_.emplace(id, std::move(item)).first->second;
    link_to_cell(added);
    return added;
}

// @orig none: an item placed at its saved id
Item& World::add_with_id(Item item) {
    if (item.id == 0) throw std::runtime_error("item id 0 is reserved");
    if (item.id >= next_id_) next_id_ = static_cast<ObjId>(item.id + 1);
    if (const Item* old = get(item.id)) unlink_from_cell(*old);
    items_.erase(item.id);
    const ObjId id = item.id;
    Item& added = items_.emplace(id, std::move(item)).first->second;
    link_to_cell(added);
    return added;
}

std::vector<ObjId>* World::cell_of(const Item& item) {
    const int cx = (item.x & 0xFFFF) >> kCellShift, cy = (item.y & 0xFFFF) >> kCellShift;
    if (cx >= kGridSize || cy >= kGridSize) return nullptr;
    return &cells_[static_cast<std::size_t>(cx * kGridSize + cy)];
}

void World::link_to_cell(const Item& item) {
    if (!on_map(item)) return;
    if (auto* c = cell_of(item)) c->insert(c->begin(), item.id);
}

void World::unlink_from_cell(const Item& item) {
    if (auto* c = cell_of(item)) std::erase(*c, item.id);
}

std::span<const ObjId> World::cell(int cx, int cy) const {
    if (cx < 0 || cy < 0 || cx >= kGridSize || cy >= kGridSize) return {};
    return cells_[static_cast<std::size_t>(cx * kGridSize + cy)];
}

// @orig none: consistency check for the tests
bool World::cells_consistent() const {
    std::size_t linked = 0;
    for (int cx = 0; cx < kGridSize; ++cx) {
        for (int cy = 0; cy < kGridSize; ++cy) {
            for (ObjId id : cell(cx, cy)) {
                const Item* i = get(id);
                if (!i || !on_map(*i) || ((i->x & 0xFFFF) >> kCellShift) != cx ||
                    ((i->y & 0xFFFF) >> kCellShift) != cy) {
                    return false;
                }
                ++linked;
            }
        }
    }
    std::size_t expected = 0;
    for (const auto& [id, i] : items_) {
        if (on_map(i) && ((i.x & 0xFFFF) >> kCellShift) < kGridSize && ((i.y & 0xFFFF) >> kCellShift) < kGridSize)
            ++expected;
    }
    return linked == expected;
}

// @orig ?: loading of the saved item table (SAVEGAME entries 0 and 1), not located
std::size_t World::load_saved_items(const FlexArchive& save) {
    // Item table, saved as one array per field (ITEMCACH.C save routine,
    // 10240 slots; slot number = object id). Field order and widths:
    constexpr std::size_t kSlots = 10240;
    const auto d = save.entry(0);
    const std::size_t widths[] = {2, 2, 1, 2, 1, 2, 2, 1, 1, 2, 2, 1};
    std::size_t offsets[12];
    std::size_t pos = 0;
    for (std::size_t f = 0; f < 12; ++f) {
        offsets[f] = pos;
        pos += kSlots * widths[f];
    }
    check_range(d, 0, pos);
    auto field = [&](std::size_t f, std::size_t slot) -> std::uint16_t {
        const std::size_t at = offsets[f] + slot * widths[f];
        return widths[f] == 2 ? read_u16(d, at) : read_u8(d, at);
    };

    items_.clear();
    clear_tracked();
    for (auto& c : cells_) c.clear();
    next_id_ = 1;
    std::size_t count = 0;
    for (std::size_t slot = 1; slot < kSlots; ++slot) {
        const std::uint16_t shape = field(3, slot);
        if (shape == 0) continue;
        Item item;
        item.id = static_cast<ObjId>(slot);
        item.x = field(0, slot);
        item.y = field(1, slot);
        item.z = field(2, slot);
        item.shape = shape;
        item.frame = static_cast<std::uint8_t>(field(4, slot));
        item.flags = field(5, slot);
        item.quality = field(6, slot);
        item.npc_number = static_cast<std::uint8_t>(field(7, slot));
        item.map_number = static_cast<std::uint8_t>(field(8, slot));
        item.save_u2 = field(10, slot);
        item.hit_points = static_cast<std::uint8_t>(field(11, slot));
        add_with_id(std::move(item));
        ++count;
    }
    pending_.clear();

    // Entry 1 starts with the u16 map number, then the fast area (s0, s1, d0, d1).
    const auto lists = save.entry(1);
    if (lists.size() >= 10) {
        auto word = [&](std::size_t at) { return static_cast<std::int16_t>(read_u16(lists, at)); };
        current_map_ = word(0);
        fast_area_ = {word(2), word(4), word(6), word(8)};
    }

    // Map grid: cell heads follow the header (64 x 64 words, x-major), and
    // each chain continues through array 9. Rebuilt in that order.
    if (lists.size() >= 10 + kGridSize * kGridSize * 2) {
        for (auto& c : cells_) c.clear();
        for (int cx = 0; cx < kGridSize; ++cx) {
            for (int cy = 0; cy < kGridSize; ++cy) {
                auto& c = cells_[static_cast<std::size_t>(cx * kGridSize + cy)];
                ObjId id = read_u16(lists, 10 + 2 * static_cast<std::size_t>(cx * kGridSize + cy));
                while (id != 0 && id < kSlots && c.size() < kSlots) {
                    const Item* i = get(id);
                    if (!i || !on_map(*i)) break;
                    c.push_back(id);
                    id = field(9, id);
                }
            }
        }
    }

    // Entry 10: NPC table.
    const auto npcs = save.entry(10);
    if (npcs.size() == npcs_.size()) {
        std::ranges::copy(npcs, npcs_.begin());
        // High byte of the NPCs' frames.
        for (ObjId id = 1; id < kNpcSlots; ++id) {
            if (Item* npc = get(id)) npc->frame |= static_cast<std::uint16_t>(npcs_[id * kNpcRecordSize + 0x0F] << 8);
        }
    }

    // Entry 20 (NPC_10f0_390d): the 21-byte block at DS:7E0E (player id,
    // ...); +0xB is the difficulty (DS:7E19, World::I_gameDifficulty), +0xD
    // the keycards (DS:7E1B), +0x11 the battery (DS:7E1F, MainActor::I_getMaxEnergy).
    const auto player = save.entry(20);
    if (player.size() >= 0x0D) state.difficulty = static_cast<std::uint16_t>(player[0x0B] | player[0x0C] << 8);
    if (player.size() >= 0x11) state.keycards = read_u32(player, 0x0D);
    if (player.size() >= 0x13) state.battery = static_cast<std::uint16_t>(player[0x11] | player[0x12] << 8);

    // Array 9 links items in lists: map grid cells (heads in entry 1), the
    // pending stack, and container contents, which start at the container's
    // quality field (families 6 and 7) and hold the container id in x.
    std::set<ObjId> linked;
    for (auto& [id, item] : items_) {
        const std::uint8_t fam = family(item);
        if (fam != kFamilyContainer && fam != kFamilyActorContainer) continue;
        for (ObjId c = item.quality; c != 0 && c < kSlots && linked.size() < kSlots; c = field(9, c)) {
            Item* child = get(c);
            if (!child || !(child->flags & kItemContained) || child->x != id || !linked.insert(c).second) break;
            child->parent = id;
            item.contents.push_back(c);
        }
    }
    // Contained items missing from the chains (in SAVEGAME.13, 16 of 65,
    // probably carried by NPCs): attached by their x field.
    for (auto& [id, item] : items_) {
        if (!(item.flags & kItemContained) || linked.count(id)) continue;
        if (Item* parent = get(static_cast<ObjId>(item.x))) {
            item.parent = parent->id;
            parent->contents.push_back(id);
        }
    }
    return count;
}

// @orig 10C8:18C0
std::vector<ObjId> World::rect_search(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) const {
    // 16-bit coordinates; negative bounds become 0 (low) or 0x7FFF (high).
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 < 0) x1 = 0x7FFF;
    if (y1 < 0) y1 = 0x7FFF;
    std::vector<ObjId> found;
    for (int cy = y0 >> kCellShift; cy <= (y1 >> kCellShift) && cy < kGridSize; ++cy) {
        for (int cx = x0 >> kCellShift; cx <= (x1 >> kCellShift) && cx < kGridSize; ++cx) {
            for (ObjId id : cell(cx, cy)) {
                const Item* i = get(id);
                if (i && !i->sprite_only && i->x >= x0 && i->x <= x1 && i->y >= y0 && i->y <= y1) found.push_back(id);
            }
        }
    }
    return found;
}

std::uint8_t World::initial_hit_points(std::uint16_t shape) const {
    const std::size_t at = std::size_t{shape} * 6;
    if (at + 6 > damage_.size() || !(damage_[at] & 1)) return 1;
    return damage_[at + 5];
}

// @orig ?: outermost container position, not located
World::Point World::location(const Item& item) const {
    const Item* top = &item;
    while (top->flags & kItemContained) {
        const Item* parent = get(top->parent);
        if (!parent) break;
        top = parent;
    }
    return {top->x, top->y, top->z};
}

// @orig 10F0:236B 10B0:268D
ObjId World::create_npc(std::uint16_t shape, std::uint16_t frame) {
    ObjId slot = 0;
    for (ObjId id = 2; id < kNpcSlots && slot == 0; ++id) {
        if (!get(id)) slot = id;
    }
    for (ObjId id = 2; id < kNpcSlots && slot == 0; ++id) {
        const Item* i = get(id);
        const std::uint16_t s = i->shape;
        if ((i->z & 0xFF) != 0xFE && s != 0x3AC && s != 0x4DC && s != 0x524 && s != 0x4D9 && s != 0x4E0 &&
            (i->flags & 0x0002) && !(i->flags & kItemFastArea)) {
            destroy(id);
            slot = id;
        }
    }
    if (slot == 0) return 0;
    Item item;
    item.id = slot;
    item.shape = shape;
    item.npc_number = static_cast<std::uint8_t>(slot);
    add_with_id(std::move(item));
    // NPC_10f0_2610: the record starts cleared.
    auto r = npc_record(slot);
    std::fill(r.begin(), r.end(), std::uint8_t{0});
    set_frame(*get(slot), frame);
    push(slot);
    return slot;
}

// @orig 10A8:11E7 10B0:262A
ObjId World::create(std::uint16_t shape, std::uint8_t frame) {
    // Item::I_create (ITEM.C). The original reuses a free slot, keeping its
    // stale x/y/z; flags, quality, NPC and map numbers are cleared.
    if (shape > 0x7FF) return 0;
    Item item;
    item.shape = shape;
    item.frame = frame;
    item.hit_points = initial_hit_points(shape);
    const std::uint8_t fam = family(item);
    // Family 10 takes its quality from the DTABLE.C tables (not decoded yet).
    // Families 11 and 12 get quality 1, then the item's usecode event 0x0C
    // ("combine") is started.
    if (fam == 11 || fam == 12) item.quality = 1;
    const ObjId id = add(std::move(item)).id;
    push(id);
    if ((fam == 11 || fam == 12) && hooks_.start_event) hooks_.start_event(id, 0x0C);
    return id;
}

void World::detach(Item& item) {
    if (Item* parent = get(item.parent)) std::erase(parent->contents, item.id);
    item.parent = 0;
}

// @orig 10B0:1C21
void World::push(ObjId id) {
    // ITEMCACH.C: already out of the world -> nothing. Otherwise the item
    // leaves its container or map cell and goes on top of the stack. An
    // equipped item leaves its NPC's equipment slots first (10B0:1C21).
    Item* item = get(id);
    if (!item || (item->flags & kItemEthereal)) return;
    if (item->flags & kItemEquipped) {
        if (item->flags & kItemContained) unequip(item->parent, id);
        item->flags &= static_cast<std::uint16_t>(~kItemEquipped);
    }
    if (on_map(*item)) unlink_from_cell(*item);
    detach(*item);
    item->flags |= kItemEthereal;
    pending_.push_back(id);
}

// @orig 10B0:1D29
ObjId World::pop_to_coords(std::int32_t x, std::int32_t y, std::int32_t z) {
    // Not reproduced yet: display list, camera follow when the item is the
    // controlled NPC.
    if (pending_.empty()) return 0;
    const ObjId id = pending_.back();
    pending_.pop_back();
    Item& item = *get(id);
    if (id == state.controlled_npc && id == state.camera_target) track_camera_target(x, y, z);
    item.x = x;
    item.y = y;
    item.z = std::min<std::int32_t>(z, 250);
    if (fast_area_.contains(x, y)) {
        if (!(item.flags & kItemFastArea) && !expanding_glob_) enter_fast_area(item);
    } else if (item.flags & kItemFastArea) {
        leave_fast_area(item);
    }
    item.flags &= static_cast<std::uint16_t>(~(kItemContained | kItemEthereal));
    link_to_cell(item);
    return id;
}

FastArea FastArea::around(std::int32_t x, std::int32_t y) {
    const std::int32_t cx = (x & 0xFFFF) >> 9, cy = (y & 0xFFFF) >> 9;
    return {cx + cy - 5, cx + cy + 5, cx - cy - 3, cx - cy + 3};
}

bool FastArea::contains_cell(std::int32_t cx, std::int32_t cy) const {
    if (cx < 0 || cx >= 64 || cy < 0 || cy >= 64) return false;
    return s0 <= cx + cy && cx + cy <= s1 && d0 <= cx - cy && cx - cy <= d1;
}

bool World::has_fast_area_events(const Item& item) const {
    // NPCs, and shapes with TYPEFLAG byte 0 bit 7.
    const ShapeInfo* info = shape_info(item.shape);
    return is_npc_id(item.id) || (info && info->is(ShapeInfo::kNoisy));
}

// @orig 10A8:2B30
void World::enter_fast_area(Item& item) {
    // Not reproduced: sound sources and family 4/7/8 special cases.
    item.flags |= kItemFastArea;
    if (item.sprite_only) return;  // port-only sprite items: no events, not tracked
    if (is_trackable(item.shape) && !(item.flags & kItemBroken)) track(item.id);
    // Dead NPCs stop here, except four shapes.
    if (is_npc_id(item.id) && is_dead(item.id) && item.shape != 0x576 && item.shape != 0x596 &&
        item.shape != 0x59C && item.shape != 0x58F) {
        return;
    }
    // Monster eggs (family 7) make their NPC (ITEMFIND_10e0_0247) and stop
    // here. Trigger eggs (family 4, teleport eggs of family 8 at frame 0)
    // get an EggHatcher process (SHAPHAND_1098_068b).
    const std::uint8_t fam = family(item);
    if (fam == 7 && hooks_.monster_egg) {
        hooks_.monster_egg(item.id);
        return;
    }
    if ((fam == 4 || (fam == 8 && item.frame == 0)) && hooks_.egg_hatcher) hooks_.egg_hatcher(item.id);
    if (is_npc_id(item.id) && hooks_.npc_entered_fast_area) hooks_.npc_entered_fast_area(item.id);
    if (has_fast_area_events(item) && hooks_.start_event) hooks_.start_event(item.id, 0x0F);
}

// @orig 10A8:2DAD
void World::leave_fast_area(Item& item) {
    item.flags &= static_cast<std::uint16_t>(~kItemFastArea);
    if (item.sprite_only) return;
    if (is_trackable(item.shape) && !(item.flags & kItemBroken)) untrack(item.id);
    if (hooks_.fail_processes) hooks_.fail_processes(item.id);
    if (is_npc_id(item.id) && is_dead(item.id)) return;
    if (has_fast_area_events(item) && hooks_.start_event) hooks_.start_event(item.id, 0x10);
}

bool World::is_trackable(std::uint16_t shape) const {
    // ITEM_10a8_30a8 / 30d2.
    const ShapeInfo* info = shape_info(shape);
    return info && (info->is(ShapeInfo::kOccludes) || info->is(ShapeInfo::kTargetable));
}

// @orig 1128:0098
void World::track(ObjId id) {
    // First free slot; nothing happens when the list is full. An item can be
    // listed twice, as in the original.
    for (ObjId& slot : tracked_) {
        if (slot == 0) {
            slot = id;
            ++tracked_count_;
            return;
        }
    }
}

// @orig 1128:00FE
void World::untrack(ObjId id) {
    // First slot holding the id.
    for (ObjId& slot : tracked_) {
        if (slot == id) {
            slot = 0;
            --tracked_count_;
            return;
        }
    }
}

void World::clear_tracked() {
    tracked_.fill(0);
    tracked_count_ = 0;
}

// @orig 10A8:06A5
void World::set_shape(Item& item, std::uint16_t shape) {
    // Item_I_setShape (10A8:06A5). The fast area is not checked.
    const bool was = is_trackable(item.shape), will = is_trackable(shape);
    const bool broken = item.flags & kItemBroken;
    if (was && !broken && !will) untrack(item.id);
    else if (will && !was && !broken) track(item.id);
    item.shape = shape;
}

void World::set_broken(Item& item) {
    if (item.flags & kItemBroken) return;
    untrack(item.id);
    item.flags |= kItemBroken;
}

// @orig 1188:0CBA
void World::set_camera(std::int32_t x, std::int32_t y, std::int32_t z) {
    // 16-bit arithmetic of ANIMCACH.C: halves and quarters are taken before
    // the sum or difference.
    const std::uint16_t ux = static_cast<std::uint16_t>(x), uy = static_cast<std::uint16_t>(y);
    state.camera_x = ux;
    state.camera_y = uy;
    state.camera_z = z & 0xFF;
    state.view_left = static_cast<std::int16_t>((ux >> 1) - (uy >> 1) - 0x140);
    state.view_top = static_cast<std::int16_t>((ux >> 2) + (uy >> 2) - state.camera_z - 300);
    update_fast_area(ux, uy);
    if (audio_) audio_->sources.update_volumes(*this);
}

// @orig 10D8:01C7
std::uint8_t direction_16(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2) {
    // 16-bit differences; dy is positive towards -y.
    const auto dx = static_cast<std::int16_t>(x2 - x1), dy = static_cast<std::int16_t>(y1 - y2);
    if (dx == 0 && dy == 0) return 16;
    if (dy == 0) return dx > 0 ? 4 : 12;
    if (dx == 0) return dy > 0 ? 0 : 8;
    // PFLOAT.C 16.16 division: integer part in 16 bits, then the remainder
    // scaled by 65536. Thresholds 4, 4/3, 3/4 and 1/4 computed the same way.
    auto fdiv = [](std::uint32_t a, std::uint32_t b) {  // a, b: 16-bit integers
        const std::uint32_t q = a / b, r = a % b;
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(static_cast<std::int16_t>(q)) << 16 |
                                         ((r << 16) / b));
    };
    const std::int32_t ratio = fdiv(static_cast<std::uint16_t>(dy < 0 ? -dy : dy),
                                    static_cast<std::uint16_t>(dx < 0 ? -dx : dx));
    const std::int32_t four_thirds = fdiv(4, 3), three_quarters = fdiv(3, 4), quarter = fdiv(1, 4);
    // Per quadrant: steep, 4/3, 3/4, else, flat (ratio <= 1/4).
    auto pick = [&](std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d, std::uint8_t flat) {
        if (ratio > 0x40000) return a;
        if (ratio > four_thirds) return b;
        if (ratio > three_quarters) return c;
        if (ratio <= quarter) return flat;
        return d;
    };
    if (dy > 0 && dx > 0) return pick(0, 1, 2, 3, 4);
    if (dy > 0) return pick(0, 15, 14, 13, 12);
    if (dx < 0) return pick(8, 9, 10, 11, 12);
    return pick(8, 7, 6, 5, 4);
}

// @orig 10D8:0120
std::uint8_t direction_8(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2) {
    // 16-bit arithmetic, as in the original.
    const auto ux = static_cast<std::int16_t>(x1 - x2), uy = static_cast<std::int16_t>(y1 - y2);
    if (ux == 0 && uy == 0) return 16;
    const auto ax = static_cast<std::int16_t>(ux < 0 ? -ux : ux), ay = static_cast<std::int16_t>(uy < 0 ? -uy : uy);
    const auto ax2 = static_cast<std::int16_t>(ax * 2);
    if (ay < ax / 2 || ax2 < ay) {  // close to an axis
        if (ay < ax2) return ux >= 0 ? 12 : 4;
        return uy >= 0 ? 0 : 8;
    }
    if (ux > 0 && uy > 0) return 14;
    if (ux < 0 && uy < 0) return 6;
    if (ux > 0 && uy < 0) return 10;
    return 2;
}

// @orig 10D8:0000
int closest_direction_in_range(std::int32_t x1, std::int32_t y1, std::int32_t x2, std::int32_t y2, int steps, int lo,
                               int hi) {
    const int dir = steps == 8 ? direction_8(x1, y1, x2, y2) : direction_16(x1, y1, x2, y2);
    if (dir >= lo && dir <= hi) return dir;
    auto wrap = [steps](int d) { return d < 0 ? d + steps : d; };
    const int to_lo = std::min(wrap(dir - lo), wrap(lo - dir));
    const int to_hi = std::min(wrap(dir - hi), wrap(hi - dir));
    return to_lo < to_hi ? lo : hi;
}

// @orig 10A8:0810
void World::set_frame(Item& item, std::uint16_t frame) {
    if (is_npc_id(item.id)) {
        item.frame = frame;
        npcs_[item.id * kNpcRecordSize + 0x0F] = static_cast<std::uint8_t>(frame >> 8);
    } else {
        item.frame = frame & 0xFF;
    }
}

std::span<std::uint8_t> World::npc_record(ObjId id) {
    if (id >= kNpcSlots) return {};
    return std::span(npcs_).subspan(id * kNpcRecordSize, kNpcRecordSize);
}

std::span<const std::uint8_t> World::npc_record(ObjId id) const {
    if (id >= kNpcSlots) return {};
    return std::span(npcs_).subspan(id * kNpcRecordSize, kNpcRecordSize);
}

bool World::is_dead(ObjId id) const {
    if (!is_npc_id(id)) return true;
    return npcs_[id * kNpcRecordSize + 0x13] & 1;
}

std::uint8_t World::npc_direction(ObjId id) const {
    return id < kNpcSlots ? npcs_[id * kNpcRecordSize + 0x0C] : 0;
}

// @orig ?: camera follow of CameraProcess (segment 1188), not located
void World::track_camera_target(std::int32_t x, std::int32_t y, std::int32_t z) {
    // Screen position of the target (SHAPHAND.C projection), feet at z.
    const auto ux = static_cast<std::int16_t>(x), uy = static_cast<std::int16_t>(y);
    const int sx = static_cast<std::int16_t>(((ux - uy) >> 1) - state.view_left);
    const int sy = static_cast<std::int16_t>((static_cast<std::uint16_t>(ux + uy) >> 2) - state.view_top - (z & 0xFF));
    const int w = state.screen_right, h = state.screen_bottom;  // x1 - x0, y1 - y0 with x0 = y0 = 0
    if (sy >= 0x8C && sy <= h - 0x46 && sx <= w - 100 && sx >= 100) return;

    // Where to put the target: away from the edge it came near.
    int target_x = (w + 1) / 2;
    if (sy < 0xA0) {
        if (sx < 0x78) target_x = w - 0x77;
        else if (w - 0x77 < sx) target_x = 0x78;
    } else if (h - 0x59 < sy) {
        if (w - 0x77 < sx) target_x = 0x78;
    } else if (sx < 0x78) {
        target_x = w - 0x77;
    } else if (w - 0x77 < sx) {
        target_x = 0x78;
    }
    const int dx = sx - target_x;
    const int dy = sy - ((h + 1) / 2 + 0x28);

    // Move the camera point by the same amount on screen (ANIMCACH.C 0467:
    // x = sx + 2 sy, y = 2 sy - sx in absolute screen coordinates).
    const auto cx = static_cast<std::int16_t>(state.camera_x), cy = static_cast<std::int16_t>(state.camera_y);
    const int cam_sx = ((cx - cy) >> 1) - state.view_left;
    const int cam_sy = (static_cast<std::uint16_t>(cx + cy) >> 2) - state.view_top;
    const int abs_x = cam_sx + dx + state.view_left, abs_y = cam_sy + dy + state.view_top;
    set_camera(abs_x + 2 * abs_y, 2 * abs_y - abs_x, state.camera_z);
}

// @orig 10A8:40CE
bool World::is_partly_on_screen(const Item& item) const {
    if (!frame_lookup_) return false;
    const auto box = frame_lookup_(item.shape, item.frame);
    if (!box) return false;
    // SHAPHAND.C projection: signed half difference, unsigned quarter sum.
    const auto x = static_cast<std::int16_t>(item.x), y = static_cast<std::int16_t>(item.y);
    const int sx = static_cast<std::int16_t>(((x - y) >> 1) - state.view_left);
    const int sy = static_cast<std::int16_t>((static_cast<std::uint16_t>(x + y) >> 2) - state.view_top -
                                             (item.z & 0xFF));
    const int left = sx - box->xoff, top = sy - box->yoff;
    const int right = left + box->width - 1, bottom = top + box->height - 1;
    // rect_overlaps: inclusive bounds.
    return left <= state.screen_right && right >= 0 && top <= state.screen_bottom && bottom >= 0 &&
           right >= left && bottom >= top;
}

// @orig 10A8:41F6
bool World::is_mostly_on_screen(const Item& item) const {
    if (!frame_lookup_) return false;
    const auto box = frame_lookup_(item.shape, item.frame);
    if (!box) return false;
    const auto x = static_cast<std::int16_t>(item.x), y = static_cast<std::int16_t>(item.y);
    const int sx = static_cast<std::int16_t>(((x - y) >> 1) - state.view_left);
    const int sy = static_cast<std::int16_t>((static_cast<std::uint16_t>(x + y) >> 2) - state.view_top -
                                             (item.z & 0xFF));
    const int left = sx - box->xoff, top = sy - box->yoff;
    const int right = left + box->width - 1, bottom = top + box->height - 1;
    // rect_clip_to the screen.
    const int cl = std::max(left, 0), ct = std::max(top, 0);
    const int cr = std::min<int>(right, state.screen_right), cb = std::min<int>(bottom, state.screen_bottom);
    return (box->width >> 1) <= cr - cl + 1 && cb - ct + 1 == box->height;
}

// @orig ?: fast area emptied on arrival, not located
void World::clear_fast_area() {
    std::vector<ObjId> globs;
    for (auto& [id, item] : items_) {
        if ((item.flags & (kItemDisposable | 0x0002)) == (kItemDisposable | 0x0002) && item.parent == 0 &&
            !(item.flags & kItemEthereal)) {
            globs.push_back(id);
        }
        item.flags &= static_cast<std::uint16_t>(~kItemFastArea);
    }
    for (ObjId id : globs) destroy(id);
    fast_area_ = {};
    clear_tracked();
}

// @orig 10B0:13BE
void World::update_fast_area(std::int32_t x, std::int32_t y) {
    const FastArea before = fast_area_;
    const FastArea after = FastArea::around(x, y);
    if (after == before) return;
    auto on_map = [](const Item& i) { return i.parent == 0 && !(i.flags & (kItemContained | kItemEthereal)); };
    auto in = [](const FastArea& area, const Item& i) { return area.contains(i.x, i.y); };

    // Cells left behind (the original also kills type 0x203 processes there).
    std::vector<ObjId> leaving;
    for_each([&](const Item& i) {
        if (on_map(i) && in(before, i) && !in(after, i)) leaving.push_back(i.id);
    });
    for (ObjId id : leaving) {
        Item* item = get(id);
        if (!item) continue;
        if (!(item->flags & kItemDisposable)) {
            leave_fast_area(*item);
            continue;
        }
        const ShapeInfo* info = shape_info(item->shape);
        if (info && info->is(ShapeInfo::kNoisy) && (item->flags & kItemFastArea)) leave_fast_area(*item);
        destroy(id);
    }
    fast_area_ = after;

    // Cells reached: glob eggs first, then the items.
    std::vector<ObjId> entering;
    for_each([&](const Item& i) {
        if (on_map(i) && in(after, i) && !in(before, i)) entering.push_back(i.id);
    });
    if (globs_) {
        for (ObjId id : entering) {
            if (family(*get(id)) == kFamilyGlobEgg) expand_glob_egg(*globs_, id);
        }
    }
    for (ObjId id : entering) {
        Item* item = get(id);
        if (!item || item->z == 254 || (item->flags & kItemDisposable)) continue;
        if (is_npc_id(id) && (item->flags & kItemFastArea)) continue;
        enter_fast_area(*item);
    }
}

// @orig 10B0:229D 10B0:2467
ObjId World::pop_into(ObjId container_id, bool at_end) {
    Item* container = get(container_id);
    if (pending_.empty() || !container) return 0;
    const std::uint8_t fam = family(*container);
    // popToContainer only accepts containers (family 6), and never puts an
    // NPC inside; popToEnd also accepts family 7. Otherwise the item is
    // placed where the container is.
    const bool into = at_end ? (fam == kFamilyContainer || fam == kFamilyActorContainer)
                             : (fam == kFamilyContainer && !is_npc_id(pending_.back()));
    if (!into) {
        const Point p = location(*container);
        return pop_to_coords(p.x, p.y, p.z);
    }
    const ObjId id = pending_.back();
    pending_.pop_back();
    Item& item = *get(id);
    item.parent = container_id;
    item.x = container_id;
    item.z = 0;
    item.flags = static_cast<std::uint16_t>((item.flags & ~kItemEthereal) | kItemContained);
    auto& c = get(container_id)->contents;
    c.insert(at_end ? c.end() : c.begin(), id);
    return id;
}

ObjId World::pop_to_container(ObjId container) { return pop_into(container, false); }
ObjId World::pop_to_end(ObjId container) { return pop_into(container, true); }

// @orig 10B0:1C21 10B0:1D29
void World::move_item(ObjId id, std::int32_t x, std::int32_t y, std::int32_t z) {
    // An item already pushed (ethereal) is not pushed again; the original
    // would then pop whichever item is on top of the stack.
    Item* item = get(id);
    if (!item || (item->flags & kItemEthereal)) return;
    push(id);
    pop_to_coords(x, y, z);
    // Item_I_move: the marker follows the designated target.
    if (state.target_designation && id == state.aim.target && hooks_.designated_target_moved)
        hooks_.designated_target_moved(id, x, y, z);
}

const TrigTables& World::trig() const {
    static const TrigTables computed = TrigTables::computed();
    return trig_ ? *trig_ : computed;
}

// @orig 10B0:23DC
ObjId World::pop() {
    if (pending_.empty()) return 0;
    const Item& item = *get(pending_.back());
    // A pushed contained item keeps its container id in x.
    if (item.flags & kItemContained) return pop_to_container(static_cast<ObjId>(item.x));
    return pop_to_coords(item.x, item.y, item.z);
}

// @orig 10A8:1365 10B0:1AB3
void World::destroy(ObjId id) {
    Item* item = get(id);
    if (!item) return;
    // Item_I_destroy -> ITEMCACH_10b0_1ab3 (not for the player).
    if (id != 1 && is_trackable(item->shape) && !(item->flags & kItemBroken)) untrack(id);
    if (id != 1 && state.target_designation && id == state.aim.target && hooks_.designated_target_forgotten)
        hooks_.designated_target_forgotten(id);
    item = get(id);
    if (!item) return;
    for (ObjId child : std::vector<ObjId>(item->contents)) destroy(child);
    if (Item* parent = get(item->parent)) std::erase(parent->contents, id);
    unlink_from_cell(*item);
    std::erase(pending_, id);
    items_.erase(id);
}

// @orig 10B0:05D6
std::vector<ObjId> World::load_fixed_map(const FlexArchive& fixed, std::size_t map) {
    std::vector<ObjId> top_level;
    std::vector<ObjId> containers;  // open containers, innermost last
    for (const FixedRecord& r : read_fixed_map(fixed, map)) {
        // Inside a container the x field is the nesting depth.
        while (!containers.empty() && r.x != containers.size()) containers.pop_back();

        Item item;
        item.shape = r.shape;
        item.frame = r.frame;
        item.flags = r.flags;
        item.quality = r.quality;
        item.npc_number = r.npc_number;
        item.map_number = r.map_number;
        if (containers.empty()) {
            item.x = r.x;
            item.y = r.y;
            item.z = r.z;
        } else {
            item.parent = containers.back();
            item.x = item.parent;
            item.flags |= kItemContained;
        }
        const ObjId id = add(std::move(item)).id;
        if (Item* parent = get(get(id)->parent)) {
            parent->contents.push_back(id);
        } else {
            top_level.push_back(id);
        }
        const ShapeInfo* info = shape_info(r.shape);
        if (info && info->family == kFamilyContainer) containers.push_back(id);
    }
    return top_level;
}

}  // namespace crusader
