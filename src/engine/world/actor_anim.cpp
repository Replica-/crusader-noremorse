#include "engine/world/actor_anim.hpp"

#include "core/dtable.hpp"
#include "engine/audio/audio.hpp"
#include "engine/world/aim_mode.hpp"
#include "engine/world/collision.hpp"
#include "engine/world/gravity.hpp"
#include "engine/world/item_selection.hpp"

#include <algorithm>

namespace crusader {

ActorAnimProcess::ActorAnimProcess(World& world, const AnimDat& anims, ObjId actor, std::uint16_t action, int dir,
                                   Pid previous)
    : world_(world), anims_(anims), action_(action), dir_(dir), previous_(previous) {
    set_owner(actor);
    set_type(kType);
}

void ActorAnimProcess::on_added() {
    if (previous_ != 0) wait_for(previous_);
}

// @orig 1158:31DA
void ActorAnimProcess::set_direction(int dir) {
    // ANIMPRIM.C 31DA: the codes 0x10..0x12 start from the actor's direction;
    // 8-direction actions use even directions and turn by 2.
    const Item* item = world_.get(owner());
    const AnimAction* a = item ? anims_.action(item->shape, action_) : nullptr;
    const bool sixteen = a && a->sixteen_directions();
    int d = (dir >= kDirCurrent) ? world_.npc_direction(owner()) : dir;
    if (!sixteen) {
        if (d & 1) d = (d - 1) & 0xF;
        if (dir == kDirTurnRight) d = (d + 2) & 0xF;
        else if (dir == kDirTurnLeft) d = (d - 2) & 0xF;
    } else if (dir == kDirTurnRight) {
        d = (d + 1) & 0xF;
    } else if (dir == kDirTurnLeft) {
        d = (d - 1) & 0xF;
    }
    dir_ = d;
    if (auto record = world_.npc_record(owner()); !record.empty()) record[npc::kDirection] = static_cast<std::uint8_t>(d);
}

// @orig 1158:016F
void ActorAnimProcess::start(const AnimAction& a) {
    // ANIMPRIM.C 016F / 17B2.
    set_direction(dir_);
    auto record = world_.npc_record(owner());
    if (record.empty()) return;
    std::uint8_t& index = record[npc::kAnimFrame];
    const std::uint8_t flags = a.flags();
    const auto count = static_cast<std::uint8_t>(a.frame_count());
    const std::uint16_t last = static_cast<std::uint16_t>(record[npc::kLastAnim] | record[npc::kLastAnim + 1] << 8);
    if (flags & 0x01) {
        // Looping: start mid-cycle, or where the previous half stride ended.
        if (!(record[npc::kAnimFlags] & 0x04)) index = count >> 1;
        else index = (flags & 0x04) ? static_cast<std::uint8_t>(count - 1) : 0;
    } else if ((flags & 0x04) && last == action_) {
        index = 1;  // 2 when off screen (frame skipping, not reproduced)
    } else {
        index = 0;
    }
    record[npc::kLastAnim] = static_cast<std::uint8_t>(action_);
    record[npc::kLastAnim + 1] = static_cast<std::uint8_t>(action_ >> 8);
}

// @orig 11D8:17DB
const ActorAnimProcess* ActorAnimProcess::next_queued() const {
    const ActorAnimProcess* next = nullptr;
    kernel().for_each_process([&](const Process& p) {
        auto* anim = dynamic_cast<const ActorAnimProcess*>(&p);
        if (anim && anim->owner() == owner() && anim->previous() == pid()) next = anim;
    });
    return next;
}

// @orig 1158:1908
void ActorAnimProcess::run() {
    // ANIMPRIM.C 1908: nothing happens while the actor falls.
    if (auto r = world_.npc_record(owner()); !r.empty() && (r[npc::kAnimFlags] & 0x02)) {
        sleep(1);
        return;
    }
    if (!step()) {
        terminate();
        return;
    }
    const Item* item = world_.get(owner());
    const AnimAction* a = item ? anims_.action(item->shape, action_) : nullptr;
    sleep(a ? std::max<std::uint8_t>(a->ticks_per_frame(), 1) : 1);
}

// @orig 1158:1908
bool ActorAnimProcess::step() {
    Item* item = world_.get(owner());
    if (!item) return false;
    const AnimAction* a = anims_.action(item->shape, action_);
    if (!a || a->frame_count() == 0) return false;
    auto record = world_.npc_record(owner());
    if (record.empty()) return false;

    const bool first = !started_;
    if (first) {
        started_ = true;
        start(*a);
    }
    const std::uint8_t flags = a->flags();
    const int count = static_cast<int>(a->frame_count());
    int index = record[npc::kAnimFrame] + (first ? 0 : 1);

    // ANIMPRIM.C 1908: half strides of looping actions.
    if (flags & 0x01) {
        std::uint8_t& anim_flags = record[npc::kAnimFlags];
        if (!(anim_flags & 0x04)) {
            if ((flags & 0x04) && index == count - 1) {
                const ActorAnimProcess* next = next_queued();
                if (!next || next->action() != action_) {
                    anim_flags |= 0x04;
                    return false;
                }
            }
        } else {
            if ((index == count / 2 && count != 3) || (index == 2 && count == 3)) {
                anim_flags &= ~0x04;
                return false;
            }
            if (index == count) index = (flags & 0x04) ? 1 : 0;
        }
    }
    if (index >= count) {
        if (count != 1) record[npc::kAnimFlags] |= 0x04;
        return false;
    }
    record[npc::kAnimFrame] = static_cast<std::uint8_t>(index);

    const AnimFrame* f = a->frame(dir_, static_cast<std::size_t>(index));
    if (!f) return false;
    world_.set_frame(*item, f->frame);
    if (f->mirrored) item->flags |= 0x0020;
    else item->flags &= static_cast<std::uint16_t>(~0x0020);
    // The frame's sound, restarted on the actor's voice (I_playSFXCru 0x045).
    if (f->sfx != 0) {
        if (Audio* audio = world_.audio()) audio->sources.retrigger(world_, f->sfx, owner());
    }
    if (f->fires_weapon() && world_.hooks().fire_weapon) {
        world_.hooks().fire_weapon(owner(), f->muzzle_a, f->muzzle_b, f->muzzle_z, dir_);
    }

    const int move_dir = a->turned() ? (dir_ + 4) & 0xF : dir_;
    const int d = f->distance * 2;
    const std::int32_t z = std::max(0, item->z + f->delta_z);
    if (d != 0 || f->delta_z != 0) {
        // Frames with the ground flag need support and may climb steps
        // (sweep flags 0x11); others only check for obstacles (4).
        const World::Point target{item->x + kDirectionDx[move_dir] * d, item->y + kDirectionDy[move_dir] * d, z};
        const std::uint8_t sweep_flags = f->ground_check ? (kSweepSupportOnly | kSweepStepUp) : kSweepNoGround;
        const World::Point start{item->x, item->y, item->z};
        const SweepResult r = try_move(world_, owner(), target, sweep_flags);
        if (r.ok) {
            world_.move_item(owner(), r.end.x, r.end.y, r.end.z);
            if (f->ground_check && fall_if_unsupported(start, r.end)) return false;
        } else if (f->ground_check && r.blocker == 0) {
            // Refused for lack of ground: over the edge.
            if (fall_if_unsupported(start, target)) return false;
            blocked_ = true;
            return false;
        } else {
            blocked_ = true;
            return false;
        }
    }
    return true;
}

// @orig 1158:3091
bool ActorAnimProcess::fall_if_unsupported(const World::Point& from, const World::Point& to) {
    // ANIMPRIM.C 3091: when no two opposite quarters of the footprint rest on
    // something (ground down to 9 below), the actor stumbles (action 0x3B,
    // replacing its animations, unless rolling or kneeling) and falls with
    // the step as speed.
    const Item* item = world_.get(owner());
    if (!item) return false;
    if (support_mask(world_, item->shape, to.x, to.y, to.z, true, owner()) & 0x10) return false;
    auto record = world_.npc_record(owner());
    const std::uint16_t last =
        record.empty() ? 0 : static_cast<std::uint16_t>(record[npc::kLastAnim] | record[npc::kLastAnim + 1] << 8);
    if (last != 0x3B && last != 0x3A && last != 0x3D && last != 0x3E && last != 0x17 && last != 0x18) {
        do_anim(kernel(), world_, anims_, owner(), 0x3B, kDirCurrent, 10000, true);
    }
    hurl(kernel(), world_, owner(), to.x - from.x, to.y - from.y, to.z - from.z, 2);
    return true;
}

// @orig 1158:2B4D 1158:1908
SimulatedStep simulate_step(const World& world, const AnimDat& anims, ObjId actor, std::uint16_t action, int dir,
                            World::Point& pos, ObjId goal) {
    const Item* item = world.get(actor);
    const auto record = world.npc_record(actor);
    const AnimAction* a = item ? anims.action(item->shape, action) : nullptr;
    if (!a || a->frame_count() == 0 || record.empty()) return {};
    // First frame, as ActorAnimProcess::start would pick it.
    const std::uint8_t flags = a->flags();
    const int count = static_cast<int>(a->frame_count());
    const std::uint16_t last = static_cast<std::uint16_t>(record[npc::kLastAnim] | record[npc::kLastAnim + 1] << 8);
    int index = 0;
    if (flags & 0x01) index = !(record[npc::kAnimFlags] & 0x04) ? count >> 1 : ((flags & 0x04) ? count - 1 : 0);
    else if ((flags & 0x04) && last == action) index = 1;
    index = std::min(index, count - 1);
    dir &= 0xF;
    if (!a->sixteen_directions() && (dir & 1)) dir = (dir - 1) & 0xF;
    const AnimFrame* f = a->frame(dir, static_cast<std::size_t>(index));
    if (!f) return {};
    const int move_dir = a->turned() ? (dir + 4) & 0xF : dir;
    const int d = f->distance * 2;
    if (d == 0 && f->delta_z == 0) return {true, false};
    const World::Point target{pos.x + kDirectionDx[move_dir] * d, pos.y + kDirectionDy[move_dir] * d,
                              std::max(0, pos.z + f->delta_z)};
    const std::uint8_t sweep_flags = f->ground_check ? (kSweepSupportOnly | kSweepStepUp) : kSweepNoGround;
    const SweepResult r = try_move(world, actor, pos, target, sweep_flags);
    bool touched = goal != 0 && r.blocker == goal;
    for (const SweepHit& h : r.hits) touched = touched || (goal != 0 && h.item == goal);
    if (r.ok) {
        pos = r.end;
        return {true, touched};
    }
    return {touched, touched};
}

// CRUSADER_1138_32a8: shows the first frame of `action` in the NPC's direction.
// @orig 1138:32A8
void set_pose(World& w, const AnimDat& anims, ObjId id, std::uint16_t action) {
    Item* item = w.get(id);
    auto r = w.npc_record(id);
    if (!item || r.empty()) return;
    if (const AnimAction* a = anims.action(item->shape, action)) {
        if (const AnimFrame* f = a->frame(w.npc_direction(id), 0)) {
            w.set_frame(*item, f->frame);
            if (f->mirrored) item->flags |= 0x0020;
            else item->flags &= static_cast<std::uint16_t>(~0x0020);
        }
    }
    r[npc::kLastAnim] = static_cast<std::uint8_t>(action);
    r[npc::kLastAnim + 1] = static_cast<std::uint8_t>(action >> 8);
    r[npc::kAnimFrame] = 0;
}

bool actor_busy(const Kernel& kernel, ObjId actor) {
    return kernel.count_processes(actor, ActorAnimProcess::kType) != 0;
}

// @orig 11D8:151D
ActorAnimProcess* current_anim(const Kernel& kernel, ObjId actor) {
    ActorAnimProcess* current = nullptr;
    kernel.for_each_process([&](const Process& p) {
        auto* anim = dynamic_cast<const ActorAnimProcess*>(&p);
        if (anim && anim->owner() == actor && !anim->waiting() && !current) {
            current = const_cast<ActorAnimProcess*>(anim);
        }
    });
    return current;
}

// @orig 1130:2105
std::uint16_t remap_action(const World& world, ObjId actor, std::uint16_t action) {
    struct Pair {
        std::uint16_t from, to;
    };
    static constexpr Pair kPairs[] = {{4, 0x25},    {0xB, 0x10},  {7, 0xC},     {8, 0xD},     {0x2E, 0x2F},
                                      {0x2A, 0x2B}, {0xA, 0xF},   {0x30, 0x31}, {3, 0x31},    {0x22, 0x23},
                                      {0x27, 0x19}, {0x24, 0x2C}, {2, 0x2D}};
    static constexpr std::size_t kMap40Pairs = 6;  // entries of DS:277F
    static constexpr std::uint16_t kNpcKept[] = {4, 0xB, 7, 8, 0x24, 2};

    if (actor != 1) {
        if (std::find(std::begin(kNpcKept), std::end(kNpcKept), action) != std::end(kNpcKept)) return action;
        if (action == 0x30) return 3;
    }
    const Item* item = world.get(actor);
    if (item && item->map_number == 40) {
        for (std::size_t i = 0; i < kMap40Pairs; ++i) {
            if (kPairs[i].from == action) return kPairs[i].to;
        }
        return action;
    }
    const Item* weapon = world.get(world.active_weapon(actor));
    if (!weapon || dtable::weapon(weapon->shape).keeps_actions()) return action;
    for (const Pair& p : kPairs) {
        if (p.from != action) continue;
        if (action != 8 && action != 0x2A) return p.to;
        const std::uint16_t overlay = dtable::overlay_shape(weapon->shape);
        const bool big = actor == 1 && (overlay == 0x36E || overlay == 0x33B);
        if (action == 8) return big ? 0x36 : 0xD;
        return big ? 0x32 : 0x2B;
    }
    return action;
}

namespace {

// Actor::I_doAnim (NPC.C 10F0:0245), for the controlled NPC: moving actions
// make NPC shots less accurate for a while (see GameState::player_moved_at).
// @orig 10F0:0245
void note_player_movement(World& world, std::uint16_t action, std::uint32_t now) {
    std::uint32_t window = 0;
    std::uint16_t accuracy = 0;
    switch (action) {
    case 0x01: case 0x02: window = 0x78; accuracy = 3; break;
    case 0x1C: case 0x1D: window = 0x3C; accuracy = 3; break;
    case 0x28: case 0x29: window = 0x4B; accuracy = 3; break;
    case 0x03: case 0x17: case 0x18: case 0x19: case 0x22: case 0x23: case 0x26: case 0x27:
    case 0x38: case 0x3A: case 0x3D: case 0x3E:
        window = 0x78;
        accuracy = 2;
        break;
    default:
        return;
    }
    world.state.player_moved_at = now;
    world.state.player_move_window = window;
    world.state.player_move_accuracy = accuracy;
}

}  // namespace

// @orig 10F0:0245
Pid do_anim(Kernel& kernel, World& world, const AnimDat& anims, ObjId actor, std::uint16_t action, int dir,
            std::uint32_t max_queued, bool replace) {
    const Item* item = world.get(actor);
    if (!item || (item->z & 0xFF) == 254) return 0;
    action = remap_action(world, actor, action);
    // NPC.C 10F0:0245: the controlled NPC ends the item selection mode.
    if (actor == world.state.controlled_npc && world.state.selection.active) cancel_selection(world);
    if (world.is_dead(actor)) return 0;
    // NPC_10f0_2315: record +0x59 bit 0x40 (set by NPC_10f0_2335, which
    // nothing calls in this version).
    if (auto r = world.npc_record(actor); !r.empty() && (r[npc::kAnimFlags] & 0x40)) return 0;
    if (!anims.action(item->shape, action)) return 0;
    const std::size_t queued = max_queued == 0 ? (actor_busy(kernel, actor) ? 1 : 0)
                                               : kernel.count_processes(actor, ActorAnimProcess::kType);
    if (max_queued < queued) return 0;
    if (replace) kernel.fail_processes(actor, ActorAnimProcess::kType, 1);

    // The new animation waits for the last one queued (the one no other waits for).
    Pid last = 0;
    std::vector<Pid> queue, followed;
    kernel.for_each_process([&](const Process& p) {
        auto* anim = dynamic_cast<const ActorAnimProcess*>(&p);
        if (!anim || anim->owner() != actor) return;
        queue.push_back(anim->pid());
        if (anim->previous()) followed.push_back(anim->previous());
    });
    for (Pid p : queue) {
        if (std::find(followed.begin(), followed.end(), p) == followed.end()) last = p;
    }
    if (actor == world.state.controlled_npc) note_player_movement(world, action, kernel.ticks());
    // Weapon drawn (record +0x5A bit 1): set by actions 7 and 0xC, cleared by
    // 0xB and 0x10.
    if (auto record = world.npc_record(actor); !record.empty()) {
        if (action == 7 || action == 0xC) record[0x5A] |= 0x02;
        else if (action == 0xB || action == 0x10) record[0x5A] &= static_cast<std::uint8_t>(~0x02);
    }
    const Pid pid = kernel.add(std::make_unique<ActorAnimProcess>(world, anims, actor, action, dir, last));
    aim_after_do_anim(kernel, world, anims, actor, action, pid);
    return pid;
}

void ActorAnimProcess::mark_turning() {
    turning_ = true;
    ++world_.state.aim.turns;
}

// @orig 1158:2E66
void ActorAnimProcess::on_terminate() {
    if (!turning_) return;
    if (world_.state.aim.turns != 0) --world_.state.aim.turns;
    const Item* item = world_.get(owner());
    if (item && (item->z & 0xFF) != 254 && owner() == world_.state.controlled_npc) {
        enter_aim_mode(kernel(), world_, anims_);
    }
}

}  // namespace crusader
