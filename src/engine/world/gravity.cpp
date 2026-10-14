#include "engine/world/gravity.hpp"

#include "engine/world/actor_anim.hpp"
#include "engine/world/collision.hpp"

#include <algorithm>

namespace crusader {

namespace {

struct Footprint {
    int w, d, h;
};

// @orig 1040:0072
Footprint footprint(const ShapeInfo* info) {
    if (!info) return {0, 0, 0};
    int h = info->footpad_z * 8;
    if (h == 0 && !info->is(ShapeInfo::kFixed)) h = 1;
    return {info->footpad_x * 16, info->footpad_y * 16, h};
}

constexpr std::size_t kStartZ = 0x11;  // NPC record: height when the fall began
constexpr std::uint8_t kFalling = 0x02;  // NPC record +0x59

}  // namespace

// @orig 1040:0072
std::uint8_t support_mask(const World& world, std::uint16_t shape, std::int32_t x, std::int32_t y, std::int32_t z,
                          bool slack, ObjId ignore) {
    if (z == 0) return 0x1F;
    const ShapeInfo* info = world.shape_info(shape);
    const Footprint f = footprint(info);
    const std::uint16_t mask = collision_mask(info);
    const int x0 = x - f.w + 1, y0 = y - f.d + 1;
    const int x_lo_mid = (x0 + x) >> 1, x_hi_mid = (x0 + x + 1) >> 1;
    const int y_lo_mid = (y0 + y) >> 1, y_hi_mid = (y0 + y + 1) >> 1;

    std::uint8_t result = 0;
    bool exact = false;
    // Candidates whose x / y lie from the near corner to 0x1E0 beyond.
    const int sx0 = std::max(x0, 0), sy0 = std::max(y0, 0), sx1 = x + 0x1E0, sy1 = y + 0x1E0;
    for (int gy = sy0 >> World::kCellShift; gy <= (sy1 >> World::kCellShift); ++gy) {
        for (int gx = sx0 >> World::kCellShift; gx <= (sx1 >> World::kCellShift); ++gx) {
            for (ObjId id : world.cell(gx, gy)) {
                if (id == ignore) continue;
                const Item& c = *world.get(id);
                const int cx = static_cast<std::uint16_t>(c.x), cy = static_cast<std::uint16_t>(c.y);
                const int cz = static_cast<std::uint8_t>(c.z);
                if (cx < x0 || cy < y0 || cx > sx1 || cy > sy1 || cz > z) continue;
                const ShapeInfo* ci = world.shape_info(c.shape);
                const Footprint cf = footprint(ci);
                const int top = cz + cf.h;
                if (cx - cf.w + 1 > x || cy - cf.d + 1 > y) continue;
                if (top != z && !(slack && top <= z && z - 9 <= top)) continue;
                if (!(mask & collision_mask(ci) & 0x7FFF)) continue;
                if (top == z) exact = true;
                if (cx - cf.w + 1 <= x_lo_mid) {
                    if (cy - cf.d + 1 <= y_lo_mid) result |= 8;
                    if (y_hi_mid <= cy) result |= 4;
                }
                if (x_hi_mid <= cx) {
                    if (cy - cf.d + 1 <= y_lo_mid) result |= 1;
                    if (y_hi_mid <= cy) result |= 2;
                }
            }
        }
    }
    if (((result & 8) && (result & 2)) || ((result & 4) && (result & 1))) result |= 0x10;
    if (slack && !exact) result = 0;
    return result;
}

// @orig 1040:0395
GravityProcess::GravityProcess(World& world, ObjId item, int vx_, int vy_, int vz_, int gravity_)
    : vx(vx_), vy(vy_), vz(vz_), gravity(gravity_), world_(world) {
    set_owner(item);
    set_type(kType);
    if (is_npc_id(item)) {
        if (auto r = world_.npc_record(item); !r.empty()) {
            r[npc::kAnimFlags] |= kFalling;
            if (const Item* i = world_.get(item)) r[kStartZ] = static_cast<std::uint8_t>(i->z);
        }
    }
}

// @orig 1040:06D2
void GravityProcess::run() {
    Item* item = world_.get(owner());
    if (!item || (item->z & 0xFF) > 250 || (item->flags & kItemContained) || !(item->flags & kItemFastArea)) {
        terminate();
        return;
    }
    const World::Point from{item->x, item->y, item->z};
    vz -= gravity;
    const World::Point to{item->x + vx, item->y + vy, item->z + vz};
    const SweepResult r = sweep(world_, item->shape, owner(), from, to, kSweepNoGround);
    world_.move_item(owner(), r.end.x, r.end.y, r.end.z);
    item = world_.get(owner());

    if (r.axes != 0) {
        const bool npc = is_npc_id(owner());
        if (!(r.axes & 4)) vz = (vz < 0 ? vz + 1 : vz) >> 2;
        if (r.axes & 1) vx = npc ? 0 : -vx;
        if (r.axes & 2) vy = npc ? 0 : -vy;
        if (r.axes & 4) {
            // Landed (or hit a ceiling): items without the bounce flags 0x4400
            // stop here; bouncing ones would rebound (not reproduced: they stop too).
            terminate();
            return;
        }
    }
    if (--steps_left_ == 0) {
        terminate();
        return;
    }
    sleep(4);
}

// @orig 1040:04BC
void GravityProcess::on_terminate() {
    // 1040:04BC: the NPC no longer falls.
    const ObjId id = owner();
    const Item* item = world_.get(id);
    if (!is_npc_id(id) || !item || (item->z & 0xFF) >= 0xFB) return;
    auto r = world_.npc_record(id);
    if (r.empty()) return;
    r[npc::kAnimFlags] &= static_cast<std::uint8_t>(~kFalling);
    if (failed() || id != 1 || world_.is_dead(id)) return;
    const int steps = (r[kStartZ] - (item->z & 0xFF)) / 8;
    const int last = r[npc::kLastAnim] | r[npc::kLastAnim + 1] << 8;
    const bool running = last == 3 || last == 0x30 || last == 0x31 || last == 0x38;  // 10F0:21A0
    if (last == 0x3A || last == 0x3D || last == 0x3E || last == 0x17 || last == 0x18 || running || steps < 3) return;
    if (world_.hooks().hard_landing) world_.hooks().hard_landing(id);
}

// @orig 1040:12A6
Pid hurl(Kernel& kernel, World& world, ObjId item, int vx, int vy, int vz, int gravity) {
    GravityProcess* existing = nullptr;
    kernel.for_each_process([&](const Process& p) {
        auto* g = dynamic_cast<const GravityProcess*>(&p);
        if (g && g->owner() == item && !existing) existing = const_cast<GravityProcess*>(g);
    });
    if (existing) {
        existing->vx += vx;
        existing->vy += vy;
        existing->vz += vz;
        return existing->pid();
    }
    auto& g = kernel.spawn<GravityProcess>(world, item, vx, vy, vz, gravity);
    g.sleep(20);
    return g.pid();
}

// @orig 1040:1334
Pid fall(Kernel& kernel, World& world, ObjId item) {
    const Item* i = world.get(item);
    const ShapeInfo* info = i ? world.shape_info(i->shape) : nullptr;
    if (!info || info->is(ShapeInfo::kFixed) || info->weight == 0) return 0;
    return hurl(kernel, world, item, 0, 0, 0, 2);
}

}  // namespace crusader
