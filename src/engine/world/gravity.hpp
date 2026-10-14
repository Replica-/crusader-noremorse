#pragma once
// Falling and thrown items (segment 1040, the original's gravity process:
// 0395 constructor, 06D2 step, 04BC end; Item::I_hurl 12A6, Item::I_fall
// 1334) and the support test under an actor (1040:0072).
//
// A gravity process (type 0x203) carries a velocity (vx, vy, vz) and a
// gravity. Every step (every 4 ticks) the item moves by the velocity through
// a swept-box test, then gravity is taken from vz. Hitting a wall stops
// (NPCs) or reverses (others) the horizontal speed; landing ends the fall of
// items without the bounce flags 0x4400. The process gives up after 200
// steps. While an NPC falls its record +0x59 bit 1 is set and +0x11 keeps
// the starting height.
//
// At the end of a fall that was not cut short (1040:04BC), a living player
// that dropped 3 steps of 8 or more lands hard (WorldHooks::hard_landing:
// actions 0x3B then 4 or 0, sound 0x8F), unless already in action 0x3A,
// 0x3D, 0x3E, 0x17, 0x18 or running.
//
// Not yet: bouncing and rolling items (flags 0x4000 / 0x400), sliding off
// edges, homing targets, a dead NPC's landing (it ends its process of type
// 0xF0), items resting on a falling one, the frame-time multiplier (one step
// per run).

#include "engine/kernel.hpp"
#include "engine/process.hpp"
#include "engine/world/world.hpp"

#include <cstdint>

namespace crusader {

class GravityProcess : public Process {
public:
    static constexpr std::uint16_t kType = 0x203;

    GravityProcess(World& world, ObjId item, int vx, int vy, int vz, int gravity);

    int vx = 0, vy = 0, vz = 0;
    int gravity = 0;

protected:
    void run() override;
    void on_terminate() override;

private:
    World& world_;
    int steps_left_ = 200;
};

// Item::I_hurl: starts the item's gravity process, or adds the velocity to
// the running one. A new process first waits 20 ticks. Returns its pid.
Pid hurl(Kernel& kernel, World& world, ObjId item, int vx, int vy, int vz, int gravity);
// Item::I_fall: hurl with no speed and gravity 2, for items that are not
// fixed and have a weight.
Pid fall(Kernel& kernel, World& world, ObjId item);

// Support under a box of `shape` at (x, y, z) (1040:0072): one bit per
// quarter of its footprint resting on a solid item whose top is at z (or up
// to 9 below with `slack`): 8 low x / low y, 4 low x / high y, 1 high x /
// low y, 2 high x / high y; 0x10 when two opposite quarters are supported
// (the item stands). At z 0 everything is supported (0x1F).
std::uint8_t support_mask(const World& world, std::uint16_t shape, std::int32_t x, std::int32_t y, std::int32_t z,
                          bool slack, ObjId ignore);

}  // namespace crusader
