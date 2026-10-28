#pragma once
// Port addition: the weapon and item wheels. What the player carries, for a list to choose from, and the choosing.
//
// The player's equipment slots are few (five weapons, five ammunitions, fifteen other items, see inventory.hpp). Anything
// else the player holds is "stashed": in his contents, in no slot (the test missions give every weapon and item this way). A
// stashed weapon or item is chosen by swapping it into the slot of the active one (or a free slot), and the ammunition of a
// weapon brought in is moved into an ammunition slot the same way.

#include "core/anim_dat.hpp"
#include "engine/kernel.hpp"
#include "engine/world/world.hpp"

#include <cstdint>
#include <vector>

namespace crusader {

struct LoadoutEntry {
    ObjId id = 0;
    std::uint16_t shape = 0;
    int slot = 0;          // the equipment slot, 0 when stashed
    bool active = false;   // the weapon in hand, or the active item
    int quality = 0;       // rounds loaded in a weapon, how many of an item
};

// The weapons the player carries (the five slots first, then the stash), and the other items he uses.
std::vector<LoadoutEntry> carried_weapons(const World& world);
std::vector<LoadoutEntry> carried_items(const World& world);

// Makes the weapon / item the active one; a stashed one first swaps into a slot.
void choose_weapon(Kernel& kernel, World& world, const AnimDat& anims, ObjId id);
void choose_item(World& world, ObjId id);

// Puts a new item (just created, waiting in the pending stack) into the player's stash.
ObjId stash_new_item(World& world, std::uint16_t quality);

}  // namespace crusader
