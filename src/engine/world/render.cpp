#include "engine/world/render.hpp"

#include <algorithm>
#include <optional>

namespace crusader {

// @orig none: shape cache
const Shape* ShapeCache::get(std::uint16_t shape) {
    auto it = cache_.find(shape);
    if (it == cache_.end()) {
        std::optional<Shape> parsed;
        const auto data = archive_.entry(shape);
        if (!data.empty()) {
            try {
                parsed = Shape::parse(data, layout_);
            } catch (const std::exception&) {
                parsed.reset();
            }
        }
        it = cache_.emplace(shape, std::move(parsed)).first;
    }
    return it->second ? &*it->second : nullptr;
}

namespace {

// World units (2 x usecode). Same rounding as the original (SHAPHAND.C,
// usecode units): the difference or sum is shifted, not each term.
int project_x(std::int32_t x, std::int32_t y) { return (x - y) >> 2; }
int project_y(std::int32_t x, std::int32_t y, std::int32_t z) { return ((x + y) >> 3) - z; }

bool screen_overlap(const DrawItem& a, const DrawItem& b) {
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

// Whether the screen hexagons of the two world boxes overlap (ScummVM,
// SortItem::overlap). Sprites whose rectangles meet but whose boxes do not
// cover each other get no ordering constraint: such constraints chained
// floors, steps and crates into cycles.
// @orig ?: display list sort (DLIST.C), not located
bool box_overlap(const DrawItem& a, const DrawItem& b) {
    struct Hex {
        int left, right;      // x of the near-left and far-right corners
        int top_x, top_y;     // far-left corner, top face
        int bot_x, bot_y;     // near-right corner, bottom face
    };
    auto hex = [](const DrawItem& d) {
        return Hex{project_x(d.x_left, d.y), project_x(d.x, d.y_far),
                   project_x(d.x_left, d.y_far), project_y(d.x_left, d.y_far, d.z_top),
                   project_x(d.x, d.y), project_y(d.x, d.y, d.z)};
    };
    const Hex p = hex(a), q = hex(b);
    if (p.right <= q.left || p.left >= q.right) return false;
    // Each top edge of a against the bottom corner of b, and the reverse
    // (screen slopes 1/2).
    const int tx = p.top_x - q.bot_x, ty = p.top_y - q.bot_y;
    const int bx = p.bot_x - q.top_x, by = p.bot_y - q.top_y;
    return tx + 2 * ty < 0 && -tx + 2 * ty < 0 && bx - 2 * by < 0 && -bx - 2 * by < 0;
}

// Draw-order comparison between two items whose screen rectangles overlap.
// Decision order (after ScummVM's reverse engineering of the original sorter,
// SortItem::below): clear separation in z, then y, then x; z with an 8-unit
// tolerance; axes where one box has zero thickness, compared on the 32-unit
// footpad grid; then tie-breaks on shape properties, screen position, depth
// and identity so that the relation is total and deterministic.
struct SortKey {
    const DrawItem& d;
    bool flat() const { return d.z == d.z_top; }
    bool x_flat() const { return d.x_left == d.x; }
    bool y_flat() const { return d.y_far == d.y; }
    bool has(ShapeInfo::Flag f) const { return d.shape_flags & f; }
    // ScummVM's _fbigsq: flat, as wide as deep, 128 world units or more.
    bool big_square_flat() const { return flat() && d.x - d.x_left == d.y - d.y_far && d.x - d.x_left >= 128; }
};

// Compares a < b on a predicate; returns nullopt when they tie.
template <class T>
std::optional<bool> prefer(T a, T b, bool smaller_first) {
    if (a == b) return std::nullopt;
    return smaller_first ? a < b : b < a;
}

// @orig ?: display list sort (DLIST.C), not located
bool behind(const DrawItem& da, const DrawItem& db) {
    const SortKey a{da}, b{db};

    // Clearly separated vertically.
    if (a.flat() && b.flat()) {
        if (da.z != db.z) return da.z < db.z;
    } else {
        if (da.z_top <= db.z) return true;
        if (da.z >= db.z_top) return false;
    }
    // Clearly separated in y, then x (zero-thickness boxes on the 32 grid).
    if (a.y_flat() && b.y_flat()) {
        if (da.y / 32 != db.y / 32) return da.y < db.y;
    } else {
        if (da.y <= db.y_far) return true;
        if (da.y_far >= db.y) return false;
    }
    if (a.x_flat() && b.x_flat()) {
        if (da.x / 32 != db.x / 32) return da.x < db.x;
    } else {
        if (da.x <= db.x_left) return true;
        if (da.x_left >= db.x) return false;
    }
    // Vertical overlap of at most 8 units counts as "on top of".
    if (da.z_top - 8 <= db.z && da.z < db.z_top - 8) return true;
    if (da.z >= db.z_top - 8 && da.z_top - 8 > db.z) return false;

    // One box flat on an axis, the other not: coarse grid, then centres.
    auto axis = [](std::int32_t lo1, std::int32_t hi1, std::int32_t lo2, std::int32_t hi2) -> std::optional<bool> {
        if (hi1 / 32 <= lo2 / 32) return true;
        if (lo1 / 32 >= hi2 / 32) return false;
        return prefer((lo1 / 32 + hi1 / 32) / 2, (lo2 / 32 + hi2 / 32) / 2, true);
    };
    if (a.y_flat() != b.y_flat()) {
        if (auto r = axis(da.y_far, da.y, db.y_far, db.y)) return *r;
    }
    if (a.x_flat() != b.x_flat()) {
        if (auto r = axis(da.x_left, da.x, db.x_left, db.x)) return *r;
    }

    if (a.flat() || b.flat()) {
        if (auto r = prefer(da.z, db.z, true)) return *r;                     // lower first
        if (auto r = prefer(a.flat(), b.flat(), false)) return *r;            // flat first
        if (auto r = prefer(a.has(ShapeInfo::kTranslucent), b.has(ShapeInfo::kTranslucent), true)) return *r;
        if (auto r = prefer(a.has(ShapeInfo::kDraw), b.has(ShapeInfo::kDraw), false)) return *r;
        if (auto r = prefer(a.has(ShapeInfo::kSolid), b.has(ShapeInfo::kSolid), false)) return *r;
        if (auto r = prefer(a.has(ShapeInfo::kOccludes), b.has(ShapeInfo::kOccludes), false)) return *r;
        // Large square flats (floor tiles) first, so that the markings laid
        // on them (hazard stripes 0x10C) are not cut by the next tile.
        if (auto r = prefer(a.big_square_flat(), b.big_square_flat(), false)) return *r;
        // Both large squares, one laid on the other (the force field floor
        // FFFLOOR 0x135, 64 x 64, on 256 x 256 tiles): the larger first, or
        // the tile hides the outline of the smaller one.
        if (a.big_square_flat() && b.big_square_flat()) {
            if (auto r = prefer(da.x - da.x_left, db.x - db.x_left, false)) return *r;
        }
    }
    if (da.x == db.x && da.y == db.y) {
        if (auto r = prefer(a.has(ShapeInfo::kTranslucent), b.has(ShapeInfo::kTranslucent), true)) return *r;
    }
    if (a.has(ShapeInfo::kLand) && b.has(ShapeInfo::kLand)) {
        if (auto r = prefer(a.has(ShapeInfo::kRoof), b.has(ShapeInfo::kRoof), true)) return *r;
    }
    if (auto r = prefer(a.has(ShapeInfo::kRoof), b.has(ShapeInfo::kRoof), false)) return *r;  // roof first
    if (auto r = prefer(da.z, db.z, true)) return *r;
    if (a.x_flat() || b.x_flat() || a.y_flat() || b.y_flat()) {
        if (auto r = prefer(da.sx_left, db.sx_left, false)) return *r;  // further right on screen first
        if (auto r = prefer(da.sy, db.sy, true)) return *r;
    }
    if (auto r = prefer(da.x + da.y, db.x + db.y, true)) return *r;
    if (auto r = prefer(da.x_left + da.y_far, db.x_left + db.y_far, true)) return *r;
    if (auto r = prefer(da.y, db.y, true)) return *r;
    if (auto r = prefer(da.x, db.x, true)) return *r;
    if (auto r = prefer(da.item->shape, db.item->shape, true)) return *r;
    if (auto r = prefer(da.item->frame, db.item->frame, true)) return *r;
    return da.item->id < db.item->id;
}

}  // namespace

std::optional<bool> draw_before(const DrawItem& a, const DrawItem& b) {
    if (!screen_overlap(a, b) || !box_overlap(a, b)) return std::nullopt;
    return behind(a, b);
}

std::vector<DrawItem> build_display_list(const World& world, ShapeCache& shapes, const Camera& camera, int w,
                                         int h, RenderStats* stats) {
    return build_display_list(world, shapes, View{project_x(camera.x, camera.y) - w / 2,
                                                  project_y(camera.x, camera.y, camera.z) - h / 2},
                              w, h, stats);
}

// @orig ?: display list (DLIST.C), not located
std::vector<DrawItem> build_display_list(const World& world, ShapeCache& shapes, const View& view, int w, int h,
                                         RenderStats* stats) {
    RenderStats local;
    RenderStats& st = stats ? *stats : local;
    st = {};
    // Screen = projection - view corner.
    const int cam_sx = view.left + w / 2, cam_sy = view.top + h / 2;

    std::vector<DrawItem> items;
    world.for_each([&](const Item& item) {
        if (item.off_map || item.parent != 0 || (item.flags & (kItemInvisible | kItemContained | kItemEthereal))) return;
        const ShapeInfo* info = world.shape_info(item.shape);
        if (info && info->is(ShapeInfo::kEditor)) return;  // editor-only markers
        const Shape* shape = shapes.get(item.shape);
        if (!shape || item.frame >= shape->frames().size()) {
            ++st.missing_shape;
            return;
        }
        DrawItem d{};
        d.item = &item;
        d.frame = &shape->frames()[item.frame];
        d.mirrored = item.flags & kItemFlipped;
        d.x = world_from_usecode(item.x);
        d.y = world_from_usecode(item.y);
        d.z = item.z;
        const int fx = info ? info->footpad_x : 0;
        const int fy = info ? info->footpad_y : 0;
        // A mirrored item has its footpad x/y swapped.
        d.x_left = d.x - (d.mirrored ? fy : fx) * kFootpadXY;
        d.y_far = d.y - (d.mirrored ? fx : fy) * kFootpadXY;
        d.z_top = d.z + (info ? info->footpad_z : 0) * kFootpadZ;
        d.sx = project_x(d.x, d.y) - cam_sx + w / 2;
        d.sy = project_y(d.x, d.y, d.z) - cam_sy + h / 2;
        d.sx_left = project_x(d.x_left, d.y) - cam_sx + w / 2;
        d.shape_flags = info ? info->flags : 0;
        const int fw = d.frame->width, fh = d.frame->height;
        d.left = d.mirrored ? d.sx + d.frame->xoff - (fw - 1) : d.sx - d.frame->xoff;
        d.top = d.sy - d.frame->yoff;
        d.right = d.left + fw;
        d.bottom = d.top + fh;
        if (d.right <= 0 || d.bottom <= 0 || d.left >= w || d.top >= h) return;
        items.push_back(d);
    });
    st.candidates = items.size();

    // Dependency graph between screen-overlapping items, then DFS topological
    // order (cycles, which the original also had to break, are cut by the DFS).
    // Ties are broken by id: the order of unconstrained items follows from the
    // order of this list, and must not change as the camera moves.
    std::sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) {
        if (a.left != b.left) return a.left < b.left;
        return a.item->id < b.item->id;
    });
    const std::size_t n = items.size();
    std::vector<std::vector<std::uint32_t>> before(n);  // before[i]: items to draw before i
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n && items[j].left < items[i].right; ++j) {
            if (!screen_overlap(items[i], items[j]) || !box_overlap(items[i], items[j])) continue;
            if (behind(items[i], items[j])) before[j].push_back(static_cast<std::uint32_t>(i));
            else before[i].push_back(static_cast<std::uint32_t>(j));
        }
    }
    std::vector<std::uint8_t> state(n, 0);  // 0 new, 1 visiting, 2 done
    std::vector<DrawItem> ordered;
    ordered.reserve(n);
    std::vector<std::pair<std::uint32_t, std::size_t>> stack;
    // Visit in depth order so unconstrained items keep a sensible order: low z
    // first, flat before thick (the original's list order, ScummVM's
    // SortItem::listLessThan), then by depth and, for a total order, by id.
    std::vector<std::uint32_t> roots(n);
    for (std::uint32_t i = 0; i < n; ++i) roots[i] = i;
    std::sort(roots.begin(), roots.end(), [&](std::uint32_t a, std::uint32_t b) {
        const auto& A = items[a];
        const auto& B = items[b];
        if (A.z != B.z) return A.z < B.z;
        const bool flat_a = A.z == A.z_top, flat_b = B.z == B.z_top;
        if (flat_a != flat_b) return flat_a;
        if (A.x + A.y != B.x + B.y) return A.x + A.y < B.x + B.y;
        return A.item->id < B.item->id;
    });
    for (std::uint32_t root : roots) {
        if (state[root]) continue;
        stack.emplace_back(root, 0);
        state[root] = 1;
        while (!stack.empty()) {
            auto& [node, next] = stack.back();
            if (next < before[node].size()) {
                const std::uint32_t dep = before[node][next++];
                if (state[dep] == 0) {
                    state[dep] = 1;
                    stack.emplace_back(dep, 0);
                } else if (state[dep] == 1) {
                    ++st.cycle_edges;  // dep is an ancestor: constraint cannot hold
                    if (st.cycle_samples.size() < 12) st.cycle_samples.emplace_back(items[node].item, items[dep].item);
                }
            } else {
                state[node] = 2;
                ordered.push_back(items[node]);
                stack.pop_back();
            }
        }
    }
    st.drawn = ordered.size();
    return ordered;
}

// @orig ?: item under the mouse, not located
std::vector<std::size_t> pick(const std::vector<DrawItem>& list, int x, int y) {
    std::vector<std::size_t> hits;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const DrawItem& d = list[i];
        if (x < d.left || x >= d.right || y < d.top || y >= d.bottom) continue;
        const int fy = y - d.top;
        const int fx = d.mirrored ? (d.right - 1 - x) : (x - d.left);
        if (d.frame->pixel(fx, fy) >= 0) hits.push_back(i);
    }
    return hits;
}

// @orig 11A0:02E4 (blitter choice; the walk over the list is not located)
void draw_display_list(Surface8& screen, const std::vector<DrawItem>& list, const XformTable* xform,
                       const std::function<void(Surface8&, const DrawItem&)>& after) {
    for (const DrawItem& d : list) {
        // TYPEFLAG translucent shapes (windows, force fields...) go through
        // the translucent blitter (asm_blit_4, mirrored asm_blit_5).
        const bool translucent = d.shape_flags & ShapeInfo::kTranslucent;
        d.frame->draw(screen, d.sx, d.sy, d.mirrored, translucent ? xform : nullptr);
        if (after) after(screen, d);
    }
}

}  // namespace crusader
