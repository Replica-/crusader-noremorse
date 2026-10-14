// Items, maps and item intrinsics. Synthetic checks always run; checks on the
// original data run when data/ is present.

#include "core/shape.hpp"
#include "core/typeflags.hpp"
#include "engine/kernel.hpp"
#include "engine/usecode/item_intrinsics.hpp"
#include "engine/usecode/uc_machine.hpp"
#include "engine/usecode/uc_process.hpp"
#include "engine/usecode/world_intrinsics.hpp"
#include "engine/world/collision.hpp"
#include "engine/world/gravity.hpp"
#include "engine/world/render.hpp"
#include "engine/world/world.hpp"

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <optional>
#include <set>
#include <tuple>
#include <filesystem>

using namespace crusader;

namespace {

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

void test_typeflag_decode() {
    // family 6 (container): byte1 high nibble 6, byte2 bit0 0; footpad 3x4x4;
    // weight 7, volume 9; NPC flag.
    const std::uint8_t rec[9] = {0x03, 0x61, 0x60, 0x10, 0x02, 0x00, 0x20, 7, 9};
    const ShapeInfo s = ShapeInfo::decode(rec);
    CHECK(s.is(ShapeInfo::kFixed) && s.is(ShapeInfo::kSolid) && s.is(ShapeInfo::kDraw));
    CHECK(s.family == 6);
    CHECK(s.footpad_x == 3 && s.footpad_y == 4 && s.footpad_z == 4);
    CHECK(s.is(ShapeInfo::kNpc));
    CHECK(s.weight == 7 && s.volume == 9);
}

void test_world_ids() {
    World w;
    auto& a = w.add(Item{});
    auto& b = w.add(Item{});
    CHECK(a.id == kFirstItemId && b.id == kFirstItemId + 1);
    w.destroy(kFirstItemId);
    CHECK(w.add(Item{}).id == kFirstItemId);
}

// Runs `code` as event 0 of class 0 on `item`, returns the process result.
std::uint32_t run_on_item(World& w, ObjId item, const std::vector<std::uint8_t>& code) {
    const auto file = UsecodeFile::from_classes({UsecodeFile::make_class({1}, code)});
    Kernel k;
    UCMachine m(k, file);
    register_item_intrinsics(m, w);
    std::uint32_t result = 0xDEADBEEF;
    struct Watcher : Process {
        std::uint32_t& out;
        explicit Watcher(std::uint32_t& o) : out(o) {}
        void run() override { out = wake_result(); terminate(); }
    };
    const Pid pid = m.start_event(0, 0, item);
    auto watcher = std::make_unique<Watcher>(result);
    Watcher& ref = *watcher;
    k.add(std::move(watcher));
    ref.wait_for(pid);
    for (int i = 0; i < 10 && k.process_count(); ++i) k.run_slice();
    CHECK(m.faults().empty());
    return result;
}

void test_item_intrinsics() {
    World w;
    Item it;
    it.shape = 0x123;
    it.frame = 4;
    it.quality = 0x1234;
    it.flags = 0x00F0;
    const ObjId id = w.add(it).id;

    // push_dword bp 6 (this); calli 4, 0x001 (getFrame); add_sp 4;
    // push_retval; word_to_dword; pop_res; ret
    CHECK(run_on_item(w, id, {0x40, 0x06, 0x0F, 0x04, 0x01, 0x00, 0x6E, 0xFC, 0x5E, 0x60, 0x08, 0x50}) == 4);

    // setFrame(this, 9): push 9; push this; calli 6, 0x002; add_sp 6; ret
    run_on_item(w, id, {0x0A, 0x09, 0x40, 0x06, 0x0F, 0x06, 0x02, 0x00, 0x6E, 0xFA, 0x50});
    CHECK(w.get(id)->frame == 9);

    // getQHi -> 0x12, getQLo -> 0x34
    CHECK(run_on_item(w, id, {0x40, 0x06, 0x0F, 0x04, 0x26, 0x00, 0x6E, 0xFC, 0x5E, 0x60, 0x08, 0x50}) == 0x12);
    CHECK(run_on_item(w, id, {0x40, 0x06, 0x0F, 0x04, 0x10, 0x00, 0x6E, 0xFC, 0x5E, 0x60, 0x08, 0x50}) == 0x34);

    // andStatus(this, 0x0030) then getStatus
    run_on_item(w, id, {0x0A, 0x30, 0x40, 0x06, 0x0F, 0x06, 0x1A, 0x00, 0x6E, 0xFA, 0x50});
    CHECK(w.get(id)->flags == 0x0030);

    // destroy(this)
    run_on_item(w, id, {0x40, 0x06, 0x0F, 0x04, 0x0A, 0x00, 0x6E, 0xFC, 0x50});
    CHECK(w.get(id) == nullptr);
}

// Typeflags with a container (shape 6, family 6, footpad 2x3x4) and a
// family-7 shape 7; other shapes are plain.
std::vector<ShapeInfo> test_typeflags() {
    std::vector<ShapeInfo> tf(16);
    tf[6].family = 6;
    tf[6].footpad_x = 2;
    tf[6].footpad_y = 3;
    tf[6].footpad_z = 4;
    tf[7].family = 7;
    return tf;
}

void test_pending_stack() {
    World w(test_typeflags());
    // DAMAGE.FLX layout: shape 3 has 50 hit points; absent entries give 1.
    std::vector<std::uint8_t> damage(16 * 6);
    damage[3 * 6] = 1;
    damage[3 * 6 + 5] = 50;
    w.set_damage_table(damage);

    CHECK(w.create(0x800, 0) == 0);
    const ObjId a = w.create(3, 2);
    CHECK(a != 0 && w.pending_top() == a);
    CHECK(w.get(a)->flags & kItemEthereal);
    CHECK(w.get(a)->hit_points == 50);
    CHECK(w.pop_to_coords(100, 200, 300) == a);
    CHECK(w.pending_count() == 0);
    CHECK(w.get(a)->x == 100 && w.get(a)->y == 200 && w.get(a)->z == 250);  // z capped
    CHECK(!(w.get(a)->flags & kItemEthereal));

    Item box;
    box.shape = 6;
    box.x = 1000;
    box.y = 2000;
    box.z = 16;
    const ObjId c = w.add(box).id;
    const ObjId b = w.create(4, 0);
    CHECK(w.get(b)->hit_points == 1);
    w.push(a);
    CHECK(w.pending_count() == 2 && w.pending_top() == a);
    CHECK(w.pop_to_container(c) == a);
    CHECK(w.pop_to_container(c) == b);  // goes first
    CHECK((w.get(c)->contents == std::vector<ObjId>{b, a}));
    CHECK(w.get(a)->parent == c && w.get(a)->x == c && (w.get(a)->flags & kItemContained));
    const auto loc = w.location(*w.get(a));
    CHECK(loc.x == 1000 && loc.y == 2000 && loc.z == 16);

    // push/pop puts a contained item back into its container.
    w.push(b);
    CHECK(w.get(c)->contents == std::vector<ObjId>{a});
    CHECK(w.pop() == b);
    CHECK((w.get(c)->contents == std::vector<ObjId>{b, a}));
    w.push(b);
    CHECK(w.pop_to_end(c) == b);
    CHECK((w.get(c)->contents == std::vector<ObjId>{a, b}));

    // Not a container: popped where it is.
    const ObjId d = w.create(5, 0);
    CHECK(w.pop_to_container(a) == d);
    CHECK(w.get(d)->parent == 0 && w.get(d)->x == 1000 && w.get(d)->z == 16);

    // Destroying a pending item removes it from the stack.
    const ObjId e = w.create(5, 0);
    w.destroy(e);
    CHECK(w.pending_count() == 0 && w.pop() == 0);
}

void test_position_intrinsics() {
    World w(test_typeflags());
    Item box;
    box.shape = 6;
    box.x = 1000;
    box.y = 2000;
    box.z = 16;
    const ObjId c = w.add(box).id;
    const ObjId a = w.create(5, 0);
    w.pop_to_container(c);
    // push this; calli 4, n; add_sp 4; push_retval; word_to_dword; pop_res; ret
    auto get = [&](ObjId id, std::uint16_t n) {
        return run_on_item(w, id, {0x40, 0x06, 0x0F, 0x04, static_cast<std::uint8_t>(n),
                                   static_cast<std::uint8_t>(n >> 8), 0x6E, 0xFC, 0x5E, 0x60, 0x08, 0x50});
    };
    CHECK(get(a, 0x013) == 1000 && get(a, 0x014) == 2000 && get(a, 0x009) == 16);  // outermost container
    CHECK(get(c, 0x120) == 1000 - 16);  // getCX: x - footpad_x * 8
    CHECK(get(c, 0x11E) == 2000 - 24);  // getCY
    CHECK(get(c, 0x11F) == 16 + 16);    // getCZ: z + footpad_z * 4
}

void test_fast_area() {
    // Camera in cell (10, 4): s = 14, d = 6.
    const FastArea area = FastArea::around(10 * 512 + 100, 4 * 512 + 7);
    CHECK((area == FastArea{9, 19, 3, 9}));
    CHECK(area.contains_cell(10, 4) && area.contains_cell(13, 4) && area.contains_cell(10, 1));
    CHECK(!area.contains_cell(16, 4) && !area.contains_cell(10, 0) && !area.contains_cell(-1, 0));

    std::vector<ShapeInfo> tf = test_typeflags();
    tf[8].flags = ShapeInfo::kNoisy;  // has fast-area usecode events
    World w(tf);
    std::vector<std::pair<ObjId, std::uint16_t>> events;
    std::vector<ObjId> failed;
    w.set_hooks({[&](ObjId id, std::uint16_t ev) { events.emplace_back(id, ev); },
                 [&](ObjId id) { failed.push_back(id); }});
    auto place = [&](std::uint16_t shape, std::int32_t cx, std::int32_t cy, std::uint16_t flags = 0) {
        Item i;
        i.shape = shape;
        i.x = cx * 512 + 10;
        i.y = cy * 512 + 10;
        i.flags = flags;
        return w.add(i).id;
    };
    const ObjId near_item = place(8, 10, 4);
    const ObjId plain = place(5, 11, 4);
    const ObjId far_item = place(8, 30, 4);
    const ObjId disposable = place(5, 10, 4, kItemDisposable);

    w.update_fast_area(10 * 512, 4 * 512);
    CHECK(w.get(near_item)->flags & kItemFastArea);
    CHECK(w.get(plain)->flags & kItemFastArea);
    CHECK(!(w.get(far_item)->flags & kItemFastArea));
    CHECK(!(w.get(disposable)->flags & kItemFastArea));  // glob items never enter
    CHECK((events == std::vector<std::pair<ObjId, std::uint16_t>>{{near_item, std::uint16_t{0x0F}}}));

    // Moving far away: items leave, disposable ones are destroyed.
    events.clear();
    w.update_fast_area(30 * 512, 4 * 512);
    CHECK(!(w.get(near_item)->flags & kItemFastArea) && !(w.get(plain)->flags & kItemFastArea));
    CHECK(w.get(disposable) == nullptr);
    CHECK(w.get(far_item)->flags & kItemFastArea);
    CHECK(std::ranges::count(failed, near_item) == 1 && std::ranges::count(failed, plain) == 1);
    CHECK(std::ranges::count(events, std::pair<ObjId, std::uint16_t>{near_item, std::uint16_t{0x10}}) == 1);
    CHECK(std::ranges::count(events, std::pair<ObjId, std::uint16_t>{far_item, std::uint16_t{0x0F}}) == 1);

    // Popping into the fast area enters it.
    const ObjId created = w.create(5, 0);
    w.pop_to_coords(30 * 512, 4 * 512, 0);
    CHECK(w.get(created)->flags & kItemFastArea);
}

void test_item_search() {
    std::vector<ShapeInfo> tf = test_typeflags();
    tf[9].family = 3;
    World w(tf);
    auto place = [&](std::uint16_t shape, std::int32_t x, std::int32_t y, std::uint8_t frame = 0) {
        const ObjId id = w.create(shape, frame);
        w.pop_to_coords(x, y, 0);
        return id;
    };
    // Cells are 512 units: (700, 700) is in cell (1, 1).
    const ObjId centre = place(5, 700, 700);
    const ObjId a = place(9, 800, 700, 2);  // same cell as the centre
    const ObjId b = place(5, 700, 750, 3);  // same cell, popped later: chain head
    const ObjId c = place(9, 1600, 700);    // cell (3, 1)
    const ObjId d = place(9, 600, 1600);    // cell (1, 3)
    place(9, 2000, 700);                    // out of range (x > 700 + 1000)
    CHECK(w.cells_consistent());
    CHECK((std::vector<ObjId>(w.cell(1, 1).begin(), w.cell(1, 1).end()) == std::vector<ObjId>{b, a, centre}));

    // "$": everything in range, cell by cell (x inner, y outer), chain order.
    const std::uint8_t all[] = {'$'};
    CHECK((w.area_search(centre, 1000, all) == std::vector<ObjId>{b, a, centre, c, d}));
    // "@ %9 = $": shape == 9.
    const std::uint8_t shape9[] = {'@', '%', 9, 0, '=', '$'};
    CHECK((w.area_search(centre, 1000, shape9) == std::vector<ObjId>{a, c, d}));
    // ": %3 = ` %2 = & $": family 3 and frame 2.
    const std::uint8_t fam_frame[] = {':', '%', 3, 0, '=', '`', '%', 2, 0, '=', '&', '$'};
    CHECK((w.area_search(centre, 1000, fam_frame) == std::vector<ObjId>{a}));
    // "B 5 9 $": shape in {5, 9}; "! $" on true gives ~1 (still true).
    const std::uint8_t list[] = {'B', 5, 0, 9, 0, '$'};
    CHECK(w.matches_loop_script(*w.get(a), list) && w.matches_loop_script(*w.get(b), list));
    const std::uint8_t not_true[] = {0x01, '!', '$'};
    CHECK(w.matches_loop_script(*w.get(a), not_true));
    const std::uint8_t less[] = {'`', '%', 3, 0, '<', '$'};  // frame < 3
    CHECK(w.matches_loop_script(*w.get(a), less) && !w.matches_loop_script(*w.get(b), less));

    // Moving items keeps the cells right.
    w.push(a);
    CHECK(w.cells_consistent());
    w.destroy(c);
    CHECK(w.cells_consistent());
    CHECK((w.area_search(centre, 1000, all) == std::vector<ObjId>{b, centre, d}));

    // Container search: contents in order.
    Item box;
    box.shape = 6;
    box.x = 1000;
    box.y = 1000;
    const ObjId container = w.add(box).id;
    w.pop_to_container(container);  // a
    CHECK((w.container_search(container, all) == std::vector<ObjId>{a}));
}

void test_surface_search() {
    std::vector<ShapeInfo> tf = test_typeflags();
    // Shape 10: 4 x 4 footprint, 2 high (16 z units). Shape 11: 2 x 2, flat, not fixed (height 1).
    tf[10].footpad_x = 4;
    tf[10].footpad_y = 4;
    tf[10].footpad_z = 2;
    tf[11].footpad_x = 2;
    tf[11].footpad_y = 2;
    World w(tf);
    auto place = [&](std::uint16_t shape, std::int32_t x, std::int32_t y, std::int32_t z) {
        const ObjId id = w.create(shape, 0);
        w.pop_to_coords(x, y, z);
        return id;
    };
    const ObjId table = place(10, 1000, 1000, 8);       // x 937..1000, z 8..24
    const ObjId on_top = place(11, 990, 990, 24);       // rests on it
    const ObjId beside = place(11, 1100, 1000, 24);     // same height, footprint apart
    const ObjId floor = place(11, 1000, 1000, 7);       // its top (7 + 1) is the table's bottom
    const ObjId far_below = place(11, 1000, 1000, 0);
    const std::uint8_t all[] = {'$'};
    const std::uint8_t any_shape11[] = {'@', '%', 11, 0, '=', '$', 0};  // 7 bytes: not a fast path
    CHECK((w.surface_search(table, 1, all) == std::vector<ObjId>{on_top}));
    CHECK((w.surface_search(table, 2, all) == std::vector<ObjId>{floor}));
    CHECK((w.surface_search(table, 3, any_shape11) == std::vector<ObjId>{floor, on_top}));
    (void)beside;
    (void)far_below;
}

// Intrinsic `n` on item `a` with the object id `b`: push b; push this;
// calli 6, n; add_sp 6; push_retval; word_to_dword; pop_res; ret
std::uint32_t pair_intrinsic(World& w, std::uint16_t n, ObjId a, ObjId b) {
    return run_on_item(w, a, {0x0B, static_cast<std::uint8_t>(b), static_cast<std::uint8_t>(b >> 8), 0x40, 0x06, 0x0F,
                              0x06, static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(n >> 8), 0x6E, 0xFA, 0x5E,
                              0x60, 0x08, 0x50});
}

// Footprint tests (Item::I_isOn and its aliases, isCompletelyOn, isCentreOn,
// overlaps): boxes x - fx*16 .. x, open intervals in x and y.
void test_footprint_intrinsics() {
    std::vector<ShapeInfo> tf = test_typeflags();
    tf[10].footpad_x = 4;  // 64 x 64, 16 high
    tf[10].footpad_y = 4;
    tf[10].footpad_z = 2;
    tf[11].footpad_x = 2;  // 32 x 32, flat
    tf[11].footpad_y = 2;
    tf[12].footpad_x = 4;  // 64 x 16, 8 high
    tf[12].footpad_y = 1;
    tf[12].footpad_z = 1;
    World w(tf);
    auto place = [&](std::uint16_t shape, std::int32_t x, std::int32_t y, std::int32_t z) {
        const ObjId id = w.create(shape, 0);
        w.pop_to_coords(x, y, z);
        return id;
    };
    auto test = [&](std::uint16_t n, ObjId a, ObjId b) { return pair_intrinsic(w, n, a, b); };
    const ObjId table = place(10, 1000, 1000, 8);    // x and y 936..1000, z 8..24
    const ObjId on_top = place(11, 990, 990, 24);    // inside, on top
    const ObjId edge = place(11, 1030, 1000, 24);    // x 998..1030: across the edge
    const ObjId touching = place(11, 1032, 1000, 24);  // x 1000..1032: only touches
    const ObjId inside = place(11, 980, 980, 10);    // within the table's height
    CHECK(test(0x046, on_top, table) == 1 && test(0x126, edge, table) == 1 && test(0x046, touching, table) == 0);
    CHECK(test(0x046, inside, table) == 0 && test(0x046, table, on_top) == 0);
    CHECK(test(0x02B, on_top, table) == 1 && test(0x02B, edge, table) == 0);
    CHECK(test(0x05A, on_top, table) == 1 && test(0x05A, edge, table) == 0);  // centre x 1014
    CHECK(test(0x0A5, table, inside) == 1 && test(0x0A5, inside, table) == 1 && test(0x0A5, table, on_top) == 0);
    // A flipped item swaps its x and y extents.
    const ObjId bar = place(12, 1000, 1300, 0);  // x 936..1000, y 1284..1300
    const ObjId flipped = place(12, 1000, 1100, 0);
    w.get(flipped)->flags |= kItemFlipped;       // x 984..1000, y 1036..1100
    CHECK(test(0x046, place(11, 980, 1290, 8), bar) == 1);
    CHECK(test(0x046, place(11, 980, 1090, 8), flipped) == 0);
    CHECK(test(0x046, place(11, 1010, 1090, 8), flipped) == 1);
}

void test_collisions() {
    std::vector<ShapeInfo> tf(32);
    auto shape = [&](int s, int fx, int fy, int fz, bool fixed) {
        tf[s].footpad_x = static_cast<std::uint8_t>(fx);
        tf[s].footpad_y = static_cast<std::uint8_t>(fy);
        tf[s].footpad_z = static_cast<std::uint8_t>(fz);
        tf[s].flags = ShapeInfo::kSolid | (fixed ? ShapeInfo::kFixed : 0u);
    };
    shape(20, 4, 4, 0, true);   // floor tile, 64 x 64, flat
    shape(21, 1, 1, 4, false);  // mover, 16 x 16 x 32
    shape(22, 1, 4, 8, true);   // wall, 16 x 64 x 64
    shape(23, 2, 2, 1, true);   // step, 32 x 32 x 8
    World w(tf);
    auto place = [&](std::uint16_t s, std::int32_t x, std::int32_t y, std::int32_t z) {
        const ObjId id = w.create(s, 0);
        w.pop_to_coords(x, y, z);
        return id;
    };
    for (int x = 63; x < 512; x += 64) {
        for (int y = 63; y < 512; y += 64) place(20, x, y, 0);
    }
    w.set_camera(300, 300, 0);

    // A wall stops the box right before it.
    const ObjId m = place(21, 100, 100, 0);
    const ObjId wall = place(22, 140, 100, 0);  // x 125..140
    CHECK(try_move(w, m, {120, 100, 0}, kSweepSupportOnly | kSweepStepUp).ok);
    SweepResult r = try_move(w, m, {140, 100, 0}, kSweepSupportOnly | kSweepStepUp);
    CHECK(!r.ok && r.blocker == wall && r.end.x == 124 && r.axes == 1);
    // Without ground check the same move is still blocked.
    CHECK(!sweep(w, 21, m, {100, 100, 0}, {140, 100, 0}, kSweepNoGround).ok);

    // A step of 8 is climbed.
    const ObjId m2 = place(21, 300, 100, 0);
    place(23, 340, 108, 0);  // x 309..340, top at 8
    r = try_move(w, m2, {320, 100, 0}, kSweepSupportOnly | kSweepStepUp);
    CHECK(r.ok && r.end.z == 8);
    // Without kSweepStepUp it blocks.
    CHECK(!try_move(w, m2, {320, 100, 0}, kSweepSupportOnly).ok);

    // No floor beyond x 511: no support, refused.
    const ObjId m3 = place(21, 500, 300, 0);
    CHECK(try_move(w, m3, {505, 300, 0}, kSweepSupportOnly | kSweepStepUp).ok);  // box still on the floor
    CHECK(!try_move(w, m3, {540, 300, 0}, kSweepSupportOnly | kSweepStepUp).ok);
    CHECK(sweep(w, 21, m3, {500, 300, 0}, {540, 300, 0}, kSweepNoGround).ok);

    // Walking off a step: the first move leaves the box at the step's height
    // (the drop sweep starts on the step and touches it, as in the original);
    // the next one, clear of the step, follows the ground down (up to 9).
    const ObjId m4 = place(21, 330, 200, 8);
    place(23, 330, 200, 0);  // under it, x 299..330
    r = try_move(w, m4, {380, 200, 8}, kSweepSupportOnly | kSweepStepUp);
    CHECK(r.ok && r.end.z == 8 && r.ground_gap == 8);
    w.move_item(m4, r.end.x, r.end.y, r.end.z);
    r = try_move(w, m4, {390, 200, 8}, kSweepSupportOnly | kSweepStepUp);
    CHECK(r.ok && r.end.z == 0);

    // Free positions (ITEMFIND.C 00D9): the wall's box blocks, the flat
    // floor never does, nor does the space above the wall; the item standing
    // there counts too, unless ignored; a box reaching below x 0 is never free.
    ObjId first = 0;
    CHECK(!position_free(w, 21, {130, 90, 0}, &first) && first == wall);
    CHECK(position_free(w, 21, {130, 90, 64}, &first) && first == 0);
    CHECK(position_free(w, 21, {200, 300, 0}));
    CHECK(!position_free(w, 21, {100, 100, 0}, &first) && first == m);
    CHECK(position_free(w, 21, {100, 100, 0}, nullptr, m));
    CHECK(!position_free(w, 21, {10, 300, 0}));
    CHECK(!box_free(w, 16, 16, 1, 0x8001, {140, 100, 63}) && box_free(w, 16, 16, 1, 0x8000, {140, 100, 63}));
}

void test_support_and_fall() {
    std::vector<ShapeInfo> tf(32);
    tf[20].footpad_x = 4;  // platform 64 x 64, flat, fixed, solid
    tf[20].footpad_y = 4;
    tf[20].flags = ShapeInfo::kSolid | ShapeInfo::kFixed;
    tf[21].footpad_x = 1;  // crate 16 x 16 x 16, solid, heavy
    tf[21].footpad_y = 1;
    tf[21].footpad_z = 2;
    tf[21].flags = ShapeInfo::kSolid;
    tf[21].weight = 10;
    World w(tf);
    auto place = [&](std::uint16_t s, std::int32_t x, std::int32_t y, std::int32_t z) {
        const ObjId id = w.create(s, 0);
        w.pop_to_coords(x, y, z);
        return id;
    };
    place(20, 100, 100, 40);  // x, y 37..100, top at 40
    w.set_camera(300, 300, 0);

    // Fully on the platform: all quarters, standing.
    CHECK(support_mask(w, 21, 90, 90, 40, true, 0) == 0x1F);
    // Half over the edge (x 93..108, middle at 100): only the low-x quarters.
    CHECK(support_mask(w, 21, 108, 90, 40, true, 0) == 0x0C);
    // Beside it: nothing; on the ground (z 0): everything.
    CHECK(support_mask(w, 21, 130, 90, 40, true, 0) == 0);
    CHECK(support_mask(w, 21, 130, 90, 0, true, 0) == 0x1F);
    // Within 9 above the platform with slack, but only if some support is exact.
    CHECK(support_mask(w, 21, 90, 90, 45, true, 0) == 0);

    // A crate dropped beside the platform at height 40 falls to the ground.
    Kernel k;
    const ObjId crate = place(21, 130, 90, 40);
    const Pid g = fall(k, w, crate);
    CHECK(g != 0);
    for (int i = 0; i < 200 && k.process_count(); ++i) {
        k.advance_time(1'000'000 / 60);
        k.run_slice();
    }
    CHECK(w.get(crate)->z == 0 && k.process_count() == 0);
    CHECK(w.cells_consistent());
}

void test_on_screen() {
    World w(test_typeflags());
    // 20 x 10 frames anchored at (10, 9).
    w.set_frame_lookup([](std::uint16_t, std::uint16_t) { return std::optional<FrameBox>{{20, 10, 10, 9}}; });
    w.set_camera(4000, 4000, 0);
    CHECK(w.state.view_left == -320 && w.state.view_top == 2000 - 300);
    Item i;
    i.shape = 5;
    i.x = 4000;
    i.y = 4000;
    const ObjId centre = w.add(i).id;
    CHECK(w.is_partly_on_screen(*w.get(centre)));  // drawn around (320, 300)
    i.x = 6000;
    const ObjId far_item = w.add(i).id;  // 1000 pixels to the right
    CHECK(!w.is_partly_on_screen(*w.get(far_item)));
    // Right edge: x - y = 2 * (639 + 10 - 320) puts the frame's left column on x = 639.
    i.x = 4000 + 2 * (639 + 10 - 320);
    const ObjId edge = w.add(i).id;
    CHECK(w.is_partly_on_screen(*w.get(edge)));
    w.get(edge)->x += 2;
    CHECK(!w.is_partly_on_screen(*w.get(edge)));
}

void test_directions() {
    // 16 directions: 0 towards -y, 4 towards +x, 8 towards +y, 12 towards -x.
    CHECK(direction_16(100, 100, 100, 100) == 16);
    CHECK(direction_16(100, 100, 100, 50) == 0);
    CHECK(direction_16(100, 100, 150, 100) == 4);
    CHECK(direction_16(100, 100, 100, 150) == 8);
    CHECK(direction_16(100, 100, 50, 100) == 12);
    CHECK(direction_16(100, 100, 150, 50) == 2);   // ratio 1
    CHECK(direction_16(100, 100, 150, 140) == 6);  // ratio 0.8 > 3/4
    CHECK(direction_16(100, 100, 150, 130) == 5);  // ratio 0.6
    CHECK(direction_16(100, 100, 150, 112) == 4);  // ratio 0.24 <= 1/4
    CHECK(direction_16(100, 100, 120, 50) == 1);   // ratio 2.5
    CHECK(direction_16(100, 100, 110, 30) == 0);   // ratio 7
    CHECK(direction_16(100, 100, 50, 20) == 15);   // ratio 1.6
    // Range: direction 4 (towards +x) clamped into [6, 10] gives 6; into
    // [12, 14] it is 14 (6 steps away through 0, against 8 for 12); inside, unchanged.
    CHECK(closest_direction_in_range(100, 100, 150, 100, 16, 6, 10) == 6);
    CHECK(closest_direction_in_range(100, 100, 150, 100, 16, 12, 14) == 14);  // 4 is 6 from 14 via 0, 8 from 12
    CHECK(closest_direction_in_range(100, 100, 150, 100, 16, 2, 6) == 4);
    // 8 directions, same scale.
    CHECK(direction_8(100, 100, 100, 100) == 16);
    CHECK(direction_8(100, 100, 100, 50) == 0);
    CHECK(direction_8(100, 100, 150, 100) == 4);
    CHECK(direction_8(100, 100, 150, 150) == 6);
    CHECK(direction_8(100, 100, 50, 50) == 14);
    CHECK(direction_8(100, 100, 150, 120) == 4);  // close to the x axis
}

void test_random_range() {
    std::uint32_t state = 12345;
    bool seen_lo = false, seen_hi = false;
    for (int i = 0; i < 1000; ++i) {
        const auto v = random_range(state, 10, 3);  // bounds in either order
        CHECK(v >= 3 && v <= 10);
        seen_lo |= v == 3;
        seen_hi |= v == 10;
    }
    CHECK(seen_lo && seen_hi);
    std::uint32_t a = 7, b = 7;
    CHECK(random_range(a, 0, 1000) == random_range(b, 0, 1000));  // deterministic
}

void test_original_maps(const std::filesystem::path& data) {
    const auto typeflags = load_typeflags(data / "STATIC/TYPEFLAG.DAT");
    CHECK(typeflags.size() == 2048);
    std::size_t containers = 0;
    for (const auto& s : typeflags) containers += s.family == 6;
    CHECK(containers == 46);

    const auto fixed = FlexArchive::open(data / "STATIC/FIXED.DAT");
    const auto globs = FlexArchive::open(data / "STATIC/GLOB.FLX");
    std::size_t maps = 0, items = 0, contained = 0, expanded = 0;
    for (std::size_t map = 0; map < fixed.count(); ++map) {
        if (fixed.entry(map).empty()) continue;
        World w(typeflags);
        w.load_fixed_map(fixed, map);
        ++maps;
        items += w.item_count();
        expanded += w.expand_glob_eggs(globs);
        w.for_each([&](const Item& i) { contained += i.parent != 0; });
    }
    std::printf("FIXED.DAT: %zu maps, %zu items, %zu inside containers, %zu items from glob eggs\n", maps, items,
                contained, expanded);
    CHECK(expanded > items);  // most of the scenery comes from globs
    CHECK(maps == 63);

    // Saved game: item table with the player in slot 1.
    const auto save = FlexArchive::open(data / "SAVEGAME/SAVEGAME.1");
    World sw(typeflags);
    const std::size_t saved = sw.load_saved_items(save);
    std::size_t bad = 0;
    const auto shapes_flex = FlexArchive::open(data / "STATIC/SHAPES.FLX");
    sw.for_each([&](const Item& i) {
        if (shapes_flex.entry(i.shape).empty()) { ++bad; return; }
        const auto shape = Shape::parse(shapes_flex.entry(i.shape));
        if (i.frame >= shape.frames().size()) ++bad;
    });
    const std::size_t from_globs = sw.expand_glob_eggs(globs);
    std::printf("SAVEGAME.1: %zu items (%zu invalid), +%zu glob items not already saved\n", saved, bad, from_globs);
    std::size_t in_containers = 0, linked = 0;
    sw.for_each([&](const Item& i) {
        if (!(i.flags & kItemContained)) return;
        ++in_containers;
        const Item* parent = sw.get(i.parent);
        if (parent && std::ranges::count(parent->contents, i.id) == 1) ++linked;
    });
    CHECK(saved == 8288);
    CHECK(in_containers == 12 && linked == 12);
    // Map grid from entry 1: every item on the map in its cell, once.
    CHECK(sw.cells_consistent());
    // The player stands on one of the items below him (Item::I_isOn).
    {
        const std::uint8_t all[] = {'$'};
        const std::vector<ObjId> below = sw.surface_search(1, 2, all);
        const auto on = std::ranges::count_if(below, [&](ObjId id) { return pair_intrinsic(sw, 0x046, 1, id) == 1; });
        std::printf("SAVEGAME.1: player on %td of the %zu items below\n", on, below.size());
        CHECK(on > 0);
    }

    // Saved fast area (entry 1), around the camera: 11 x 7 cells in s, d.
    // Glob items on the map are exactly those of the area; moving the camera
    // across the map replaces them with those of the new area.
    {
        World mw(typeflags);
        mw.load_saved_items(save);
        mw.set_globs(&globs);
        CHECK((mw.fast_area() == FastArea{69, 79, 13, 19}));
        auto glob_items = [&](std::size_t& outside) {
            std::size_t n = 0;
            outside = 0;
            mw.for_each([&](const Item& i) {
                if (!(i.flags & kItemDisposable) || i.parent || (i.flags & kItemEthereal)) return;
                ++n;
                outside += !mw.fast_area().contains(i.x, i.y);
            });
            return n;
        };
        std::size_t outside = 0;
        const std::size_t saved_globs = glob_items(outside);
        CHECK(saved_globs == 539 && outside == 0);
        mw.update_fast_area(27667, 24763);  // player position in SAVEGAME.2
        const std::size_t new_globs = glob_items(outside);
        std::printf("SAVEGAME.1 camera moved: %zu glob items around the camera, %zu after the move\n", saved_globs,
                    new_globs);
        CHECK(outside == 0 && new_globs > 300);

        // SAVEGAME.2 was saved with that fast area: the original made the same glob items.
        World ow(typeflags);
        ow.load_saved_items(FlexArchive::open(data / "SAVEGAME/SAVEGAME.2"));
        CHECK(ow.fast_area() == mw.fast_area());
        using Key = std::tuple<std::int32_t, std::int32_t, std::int32_t, std::uint16_t, std::uint16_t>;
        auto keys = [](const World& world) {
            std::multiset<Key> k;
            world.for_each([&](const Item& i) {
                // 0x82 = from a glob; other disposable items (0x81) come from elsewhere.
                if ((i.flags & 0x82) == 0x82 && !i.parent && !(i.flags & kItemEthereal))
                    k.emplace(i.x, i.y, i.z, i.shape, i.frame);
            });
            return k;
        };
        const auto ours = keys(mw), original = keys(ow);
        std::vector<Key> only_ours, only_original;
        std::ranges::set_difference(ours, original, std::back_inserter(only_ours));
        std::ranges::set_difference(original, ours, std::back_inserter(only_original));
        std::printf("glob items vs SAVEGAME.2: %zu ours, %zu original, %zu only ours, %zu only original\n",
                    ours.size(), original.size(), only_ours.size(), only_original.size());
        CHECK(only_ours.empty() && only_original.empty());
    }

    // NPC table (entry 10): the dead bit matches the NPCs put out of the world.
    std::size_t dead = 0, dead_ethereal = 0;
    for (ObjId id = 1; id < 256; ++id) {
        const Item* npc = sw.get(id);
        if (!npc || !sw.is_dead(id)) continue;
        ++dead;
        dead_ethereal += (npc->flags & kItemEthereal) != 0;
    }
    CHECK(dead == 2 && dead_ethereal == 2);
    CHECK(sw.npc_direction(1) < 16);

    const auto damage = FlexArchive::open(data / "STATIC/DAMAGE.FLX");
    CHECK(damage.entry(0).size() == 2048 * 6);
    CHECK(bad == 0);
    CHECK(sw.get(1) && sw.get(1)->shape == 1 && sw.get(1)->npc_number == 1);
    CHECK(items == 233326);
}

}  // namespace

int main(int argc, char** argv) {
    test_typeflag_decode();
    test_world_ids();
    test_item_intrinsics();
    test_pending_stack();
    test_position_intrinsics();
    test_fast_area();
    test_directions();
    test_on_screen();
    test_item_search();
    test_surface_search();
    test_footprint_intrinsics();
    test_collisions();
    test_support_and_fall();
    test_random_range();
    const std::filesystem::path data = argc > 1 ? argv[1] : "data";
    if (std::filesystem::exists(data / "STATIC/FIXED.DAT")) {
        test_original_maps(data);
    } else {
        std::printf("game data not found, skipping map checks\n");
    }
    std::printf(failures ? "%d failure(s)\n" : "all world checks passed\n", failures);
    return failures ? 1 : 0;
}
