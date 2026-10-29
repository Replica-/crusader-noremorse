#pragma once
// Port addition: a jump to a chosen place. The jump animation of the original has a fixed length and direction; this one
// plans where the player can land (plan_jump, for the preview shown while the J key is held) and carries him there along a
// smooth arc (JumpProcess), the jump animation playing in place meanwhile, so the jump can be any length and any way.

#include "core/anim_dat.hpp"
#include "engine/kernel.hpp"
#include "engine/process.hpp"
#include "engine/world/world.hpp"

#include <vector>

namespace crusader {

// Where `npc`, standing at `from`, can land when it wants to go to `wanted`: the farthest point on the way, within `max_range`,
// where it stands on something and has room, and the flight to it is clear. `landing` and, when given, `arc` (the points of the
// flight, in usecode units) are set; false when there is no such point.
bool plan_jump(const World& world, ObjId npc, const World::Point& from, const World::Point& wanted, int max_range,
               World::Point& landing, std::vector<World::Point>* arc);

// The height of the arc above the straight line to the landing, and its length in ticks, for a jump of `distance`.
int jump_apex(int distance);
int jump_ticks(int distance);

// The point of the arc at `t` (0..1).
World::Point jump_point(const World::Point& from, const World::Point& to, double t, int apex);

class JumpProcess : public Process {
public:
    static constexpr std::uint16_t kType = 0x205;

    JumpProcess(World& world, ObjId npc, World::Point from, World::Point to, int ticks, int apex);

protected:
    void run() override;

private:
    World& world_;
    World::Point from_, to_;
    int ticks_, apex_;
    int step_ = 0;
};

// Starts the jump of `npc` to `target`: its animations are cut, the jump animation plays in place (stretched over the flight)
// and the arc carries it. Returns the pid of the arc process, 0 if the actor cannot jump.
Pid start_targeted_jump(Kernel& kernel, World& world, const AnimDat& anims, ObjId npc, const World::Point& target);

}  // namespace crusader
