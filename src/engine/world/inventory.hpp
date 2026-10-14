#pragma once
// The player's inventory (WORLD.C 13E8:0330); equipping is set_equip
// (actor_create.hpp, NPC.C 10F0:27FB).
//
// An NPC's equipment is 30 slots of item ids in its record (+0x1B); the
// player's are used by kind: 1..5 weapons (family 10), 6..10 ammunition
// (family 11), 11..25 other items (families 12 and 13, and shape 0x4ED).
// The active weapon's slot is at +0x15, the active item's at +0x19. An
// equipped item is in the NPC's contents, with its slot as z and flag 0x200.
//
// MainActor::I_addItemCru(item, show) picks an item up; true if taken:
//   - 0x4ED: added to the quality of the one carried, else in a free item slot;
//   - weapons: refused when one of the shape is carried; else in a free
//     weapon slot, loaded with a full clip (0 rounds for a weapon without
//     ammunition), and active when none was;
//   - ammunition: one more clip on those carried (up to 20), else in a free
//     slot with quality 1;
//   - keycard 0x111: bit (quality low byte, below 32) of the keycards;
//   - batteries 0x3A2 / 0x3A4 / 0x3A3 raise the battery to 1 / 2 / 3 and
//     refill the energy, or when it is already that good give 2500 / 5000 /
//     10000 energy;
//   - other items: one more on those carried (up to 10, or 20 for 0x560;
//     shields 0x52E..0x530 carried: just destroyed); shields raise the
//     shield level (+0x66) to 1 / 2 / 3; else in a free item slot, quality 1,
//     made the active item.
// The picked-up item is destroyed when it only adds to another.
// When `show`, the item is listed at the top left of the screen
// (GameState::pickups, DISPLAY_1190_049c) before it is destroyed; never for
// the datalink 0x4D4 added as a new item or to those carried. The original
// lists a shield after destroying it (it reads the shape of a destroyed
// item); the port lists it before.
// Not reproduced: the status bar boxes marked for redrawing (the port redraws
// the whole bar).

#include "engine/world/world.hpp"

namespace crusader {

// Item::I_setQAndCombine: the quality, then usecode event 0x0C ("combine").
void set_quality_and_combine(World& world, ObjId item, std::uint16_t quality);

bool add_item_cru(World& world, ObjId item, bool show);

// Item::I_use (10A8:2667): usecode event 1 ("use") of the item's class, when
// the class has it. Refused for a dead NPC, and for an NPC in combat other
// than the player. may_use says whether the event may start.
bool may_use(const World& world, ObjId item);
void use_item(World& world, ObjId item);

// Player record fields.
constexpr std::size_t kNpcActiveWeapon = 0x15;
constexpr std::size_t kNpcActiveItem = 0x19;
constexpr std::size_t kNpcShieldLevel = 0x66;

}  // namespace crusader
