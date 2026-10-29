#include "engine/world/jump.hpp"

#include "engine/world/actor_anim.hpp"
#include "engine/world/collision.hpp"
#include "engine/world/gravity.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace crusader {

namespace {

int chebyshev_xy(const World::Point& a, const World::Point& b) {
    return std::max(std::abs(a.x - b.x), std::abs(a.y - b.y));
}

// Whether `npc` can stand at (x, y, z): room for its box, and something under it.
bool can_stand(const World& world, ObjId npc, const Item& item, std::int32_t x, std::int32_t y, std::int32_t z) {
    if (z < 0 || z > 250) return false;
    const World::Point at{x, y, z};
    if (!position_free(world, item.shape, at, nullptr, npc)) return false;
    return (support_mask(world, item.shape, x, y, z, true, npc) & 0x10) != 0;
}

}  // namespace

// @orig none: port-only targeted jump
int jump_apex(int distance) { return std::clamp(20 + distance * 12 / 100, 20, 60); }

// @orig none: port-only targeted jump
int jump_ticks(int distance) { return std::clamp(18 + distance / 8, 18, 54); }

// @orig none: port-only targeted jump
World::Point jump_point(const World::Point& from, const World::Point& to, double t, int apex) {
    const double lift = 4.0 * apex * t * (1.0 - t);
    return {static_cast<std::int32_t>(std::lround(from.x + (to.x - from.x) * t)),
            static_cast<std::int32_t>(std::lround(from.y + (to.y - from.y) * t)),
            static_cast<std::int32_t>(std::lround(from.z + (to.z - from.z) * t + lift))};
}

// @orig none: port-only targeted jump
bool plan_jump(const World& world, ObjId npc, const World::Point& from, const World::Point& wanted, int max_range,
               World::Point& landing, std::vector<World::Point>* arc) {
    const Item* item = world.get(npc);
    if (!item) return false;
    const int dx = wanted.x - from.x, dy = wanted.y - from.y;
    const int distance = std::max(std::abs(dx), std::abs(dy));
    if (distance < 8) return false;
    const double scale = distance > max_range ? static_cast<double>(max_range) / distance : 1.0;
    // From the wanted place back towards the player, the first point that can be landed on.
    for (int back = 0; back <= static_cast<int>(distance * scale) - 16; back += 8) {
        const double along = (distance * scale - back) / distance;
        const std::int32_t x = from.x + static_cast<std::int32_t>(std::lround(dx * along));
        const std::int32_t y = from.y + static_cast<std::int32_t>(std::lround(dy * along));
        // The ground there: the first place that can be stood on going down from a little above the player, as far as two
        // storeys, so a jump can drop off a ledge onto the level below.
        for (std::int32_t zc = from.z + 24; zc >= std::max(0, from.z - 160); zc -= 4) {
            std::int32_t z = zc;
            if (!can_stand(world, npc, *item, x, y, z)) continue;
            for (int k = 0; k <= 9 && z - k >= 0; ++k) {  // down to the floor itself
                if (support_mask(world, item->shape, x, y, z - k, false, npc) & 0x10) {
                    z -= k;
                    break;
                }
            }
            const World::Point to{x, y, z};
            const int d = chebyshev_xy(from, to);
            const int apex = jump_apex(d);
            // The flight: no wall or object in the way, the whole arc.
            bool clear = true;
            World::Point previous = from;
            for (int i = 1; i <= 12 && clear; ++i) {
                const World::Point next = jump_point(from, to, i / 12.0, apex);
                const SweepResult r = sweep(world, item->shape, npc, previous, next, kSweepNoGround);
                clear = r.ok || (i == 12 && r.blocker == 0);
                previous = next;
            }
            if (!clear) continue;
            landing = to;
            if (arc) {
                arc->clear();
                for (int i = 0; i <= 28; ++i) arc->push_back(jump_point(from, to, i / 28.0, apex));
            }
            return true;
        }
    }
    return false;
}

// @orig none: port-only targeted jump
JumpProcess::JumpProcess(World& world, ObjId npc, World::Point from, World::Point to, int ticks, int apex)
    : world_(world), from_(from), to_(to), ticks_(std::max(1, ticks)), apex_(apex) {
    set_owner(npc);
    set_type(kType);
}

// One step a tick along the arc (the swept box tested each time); a step into something ends the flight, and the player falls.
// @orig none: port-only targeted jump
void JumpProcess::run() {
    Item* item = world_.get(owner());
    if (!item || world_.is_dead(owner())) {
        terminate();
        return;
    }
    ++step_;
    const bool last = step_ >= ticks_;
    const World::Point here = world_.location(*item);
    const World::Point next = last ? to_ : jump_point(from_, to_, static_cast<double>(step_) / ticks_, apex_);
    SweepResult r;
    if (last) {
        r = try_move(world_, owner(), here, next, kSweepSupportOnly | kSweepStepUp);
        if (!r.ok) r = sweep(world_, item->shape, owner(), here, next, kSweepNoGround);
    } else {
        r = sweep(world_, item->shape, owner(), here, next, kSweepNoGround);
    }
    if (!r.ok) {  // stopped by something: down from here
        fall(kernel(), world_, owner());
        terminate();
        return;
    }
    leave_surface(kernel(), world_, owner());
    finish_move(world_, owner(), r);
    if (last) {
        const Item* landed = world_.get(owner());
        if (landed && !(support_mask(world_, landed->shape, landed->x, landed->y, landed->z & 0xFF, true, owner()) & 0x10)) {
            fall(kernel(), world_, owner());  // not on anything: it falls the rest of the way
        }
        terminate();
        return;
    }
    sleep(1);
}

// @orig none: port-only targeted jump
Pid start_targeted_jump(Kernel& kernel, World& world, const AnimDat& anims, ObjId npc, const World::Point& target) {
    const Item* item = world.get(npc);
    if (!item || world.is_dead(npc)) return 0;
    const World::Point from = world.location(*item);
    const int distance = chebyshev_xy(from, target);
    const int ticks = jump_ticks(distance);
    // The jump animation, in place and stretched over the flight, facing the way it goes.
    const int dir = direction_16(from.x, from.y, target.x, target.y);
    kernel.fail_processes(npc, ActorAnimProcess::kType, 1);
    const Pid animation = do_anim(kernel, world, anims, npc, 0x3A, dir, 0, true);
    if (animation == 0) return 0;
    if (auto* anim = dynamic_cast<ActorAnimProcess*>(kernel.get(animation))) {
        const AnimAction* a = anims.action(item->shape, 0x3A);
        const int frames = a ? static_cast<int>(a->frame_count()) : 1;
        anim->set_in_place(true);
        anim->set_frame_ticks(std::max(1, ticks / std::max(1, frames)));
    }
    return kernel.spawn<JumpProcess>(world, npc, from, target, ticks, jump_apex(distance)).pid();
}

}  // namespace crusader
