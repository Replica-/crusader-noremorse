#include "engine/world/loadout.hpp"

#include "core/dtable.hpp"
#include "engine/world/actor_anim.hpp"
#include "engine/world/actor_create.hpp"
#include "engine/world/inventory.hpp"
#include "engine/world/player_commands.hpp"

#include <algorithm>

namespace crusader {

namespace {

constexpr ObjId kPlayer = 1;
constexpr int kFirstWeaponSlot = 1, kLastWeaponSlot = 5;
constexpr int kFirstAmmoSlot = 6, kLastAmmoSlot = 10;
constexpr int kFirstItemSlot = 11, kLastItemSlot = 25;
constexpr std::uint16_t kCredits = 0x4ED;

bool equipped(const Item& item) { return item.flags & kItemEquipped; }

// The items in the player's contents that are in no slot.
std::vector<ObjId> stash(const World& world) {
    std::vector<ObjId> out;
    if (const Item* me = world.get(kPlayer)) {
        for (const ObjId id : me->contents) {
            const Item* item = world.get(id);
            if (item && !equipped(*item)) out.push_back(id);
        }
    }
    return out;
}

// An equipped item out of its slot and into the stash.
void to_stash(World& world, ObjId id) {
    if (Item* item = world.get(id)) {
        world.unequip(kPlayer, id);
        item->flags &= static_cast<std::uint16_t>(~kItemEquipped);
        item->z = 0;
    }
}

int free_slot(const World& world, int first, int last) {
    for (int slot = first; slot <= last; ++slot) {
        if (world.npc_equip(kPlayer, slot) == 0) return slot;
    }
    return 0;
}

// A stashed item into a slot of `first`..`last`: a free one, else the slot of `swap_slot` (its item goes to the stash).
// Returns the slot, 0 if it could not.
int equip_from_stash(World& world, ObjId id, int first, int last, int swap_slot) {
    int slot = free_slot(world, first, last);
    if (slot == 0 && swap_slot >= first && swap_slot <= last) {
        if (const ObjId old = world.npc_equip(kPlayer, swap_slot)) to_stash(world, old);
        slot = swap_slot;
    }
    if (slot == 0) return 0;
    return set_equip(world, kPlayer, slot, id) ? slot : 0;
}

// The ammunition of a weapon brought in: from the stash into an ammunition slot, sending away one no weapon in a slot uses.
void ensure_ammo(World& world, std::uint16_t weapon_shape) {
    const std::uint16_t ammo = dtable::ammo_shape(weapon_shape);
    if (ammo == 0 || carried(world, ammo) != 0) return;
    ObjId stashed = 0;
    for (const ObjId id : stash(world)) {
        if (world.get(id)->shape == ammo) stashed = id;
    }
    if (stashed == 0) return;
    int slot = free_slot(world, kFirstAmmoSlot, kLastAmmoSlot);
    if (slot == 0) {
        // Not needed by any weapon in a slot.
        for (int s = kFirstAmmoSlot; s <= kLastAmmoSlot && slot == 0; ++s) {
            const Item* held = world.get(world.npc_equip(kPlayer, s));
            if (!held) continue;
            bool used = false;
            for (int w = kFirstWeaponSlot; w <= kLastWeaponSlot; ++w) {
                const Item* gun = world.get(world.npc_equip(kPlayer, w));
                if (gun && dtable::ammo_shape(gun->shape) == held->shape) used = true;
            }
            if (!used) slot = s;
        }
        if (slot == 0) slot = kFirstAmmoSlot;
        if (const ObjId old = world.npc_equip(kPlayer, slot)) to_stash(world, old);
    }
    set_equip(world, kPlayer, slot, stashed);
}

}  // namespace

// @orig none: port-only (the weapon wheel)
std::vector<LoadoutEntry> carried_weapons(const World& world) {
    std::vector<LoadoutEntry> out;
    const int active = world.npc_word(kPlayer, npc::kWeapon);
    for (int slot = kFirstWeaponSlot; slot <= kLastWeaponSlot; ++slot) {
        const Item* item = world.get(world.npc_equip(kPlayer, slot));
        if (!item || world.family_of(item->shape) != 10) continue;
        out.push_back({item->id, item->shape, slot, slot == active, item->quality});
    }
    for (const ObjId id : stash(world)) {
        const Item* item = world.get(id);
        if (world.family_of(item->shape) == 10) out.push_back({id, item->shape, 0, false, item->quality});
    }
    return out;
}

// @orig none: port-only (the item wheel)
std::vector<LoadoutEntry> carried_items(const World& world) {
    std::vector<LoadoutEntry> out;
    const int active = world.npc_word(kPlayer, npc::kActiveItem);
    auto usable = [&](const Item& item) {
        const std::uint8_t family = world.family_of(item.shape);
        return (family == 12 || family == 13) && item.shape != kCredits;
    };
    for (int slot = kFirstItemSlot; slot <= kLastItemSlot; ++slot) {
        const Item* item = world.get(world.npc_equip(kPlayer, slot));
        if (item && usable(*item)) out.push_back({item->id, item->shape, slot, slot == active, item->quality});
    }
    for (const ObjId id : stash(world)) {
        const Item* item = world.get(id);
        if (usable(*item)) out.push_back({id, item->shape, 0, false, item->quality});
    }
    return out;
}

// @orig none: port-only (the weapon wheel)
void choose_weapon(Kernel& kernel, World& world, const AnimDat& anims, ObjId id) {
    Item* item = world.get(id);
    if (!item || world.family_of(item->shape) != 10) return;
    int slot = equipped(*item) ? static_cast<int>(item->z & 0xFF) : 0;
    if (slot == 0) {
        const std::uint16_t shape = item->shape;
        slot = equip_from_stash(world, id, kFirstWeaponSlot, kLastWeaponSlot, world.npc_word(kPlayer, npc::kWeapon));
        if (slot == 0) return;
        ensure_ammo(world, shape);
    }
    select_weapon_slot(kernel, world, anims, slot);
}

// @orig none: port-only (the item wheel)
void choose_item(World& world, ObjId id) {
    Item* item = world.get(id);
    if (!item) return;
    int slot = equipped(*item) ? static_cast<int>(item->z & 0xFF) : 0;
    if (slot == 0) slot = equip_from_stash(world, id, kFirstItemSlot, kLastItemSlot, world.npc_word(kPlayer, npc::kActiveItem));
    if (slot != 0) select_item_slot(world, slot);
}

// @orig none: port-only (test missions)
ObjId stash_new_item(World& world, std::uint16_t quality) {
    const ObjId id = world.pop_to_container(kPlayer);
    if (Item* item = world.get(id)) item->quality = quality;
    return id;
}

}  // namespace crusader
