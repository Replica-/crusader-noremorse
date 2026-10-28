#include "engine/world/click_path.hpp"

#include "engine/world/collision.hpp"

#include <algorithm>
#include <cstdlib>
#include <functional>
#include <queue>

namespace crusader {

namespace {

constexpr int kCell = 16;
constexpr int kRadius = 60;               // cells each way from the start
constexpr int kSide = 2 * kRadius + 1;
constexpr int kMaxExpanded = 1500;
constexpr std::uint8_t kWalkFlags = kSweepSupportOnly | kSweepStepUp;

struct Node {
    int g = 1 << 30;
    int parent = -1;
    std::int32_t z = 0;
    bool closed = false;
};

// Octile distance in tenths of a cell.
int octile(int dx, int dy) {
    dx = std::abs(dx), dy = std::abs(dy);
    return 10 * std::max(dx, dy) + 4 * std::min(dx, dy);
}

}  // namespace

bool straight_walk_ok(const World& world, ObjId mover, const World::Point& from, const World::Point& to) {
    const SweepResult r = try_move(world, mover, from, to, kWalkFlags);
    return r.ok && r.end.x == to.x && r.end.y == to.y;
}

// @orig none: port-only click-to-walk path
PlannedPath plan_path(const World& world, ObjId mover, const World::Point& from, const World::Point& goal) {
    PlannedPath out;
    std::vector<Node> nodes(static_cast<std::size_t>(kSide) * kSide);
    auto index = [](int cx, int cy) { return (cy + kRadius) * kSide + (cx + kRadius); };
    auto position = [&](int cx, int cy, std::int32_t z) { return World::Point{from.x + cx * kCell, from.y + cy * kCell, z}; };
    // The goal in cells (rounded), to rank nodes by what is left to walk.
    const int gx = (goal.x - from.x) / kCell, gy = (goal.y - from.y) / kCell;
    auto heuristic = [&](int cx, int cy) { return octile(gx - cx, gy - cy) * 12 / 10; };

    using Entry = std::pair<int, int>;  // f, node index
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
    nodes[static_cast<std::size_t>(index(0, 0))] = {0, -1, from.z, false};
    open.push({heuristic(0, 0), index(0, 0)});

    int best = index(0, 0), best_h = heuristic(0, 0);
    int found = -1;
    while (!open.empty() && out.expanded < kMaxExpanded) {
        const int current = open.top().second;
        open.pop();
        Node& node = nodes[static_cast<std::size_t>(current)];
        if (node.closed) continue;
        node.closed = true;
        ++out.expanded;
        const int cx = current % kSide - kRadius, cy = current / kSide - kRadius;
        const int h = heuristic(cx, cy);
        if (h < best_h) best = current, best_h = h;
        if (std::max(std::abs(gx - cx), std::abs(gy - cy)) <= 1) {
            found = current;
            break;
        }
        const World::Point here = position(cx, cy, node.z);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int nx = cx + dx, ny = cy + dy;
                if (std::abs(nx) > kRadius || std::abs(ny) > kRadius) continue;
                Node& next = nodes[static_cast<std::size_t>(index(nx, ny))];
                const int g = node.g + (dx && dy ? 14 : 10);
                if (next.closed || g >= next.g) continue;
                const World::Point to = position(nx, ny, node.z);
                const SweepResult r = try_move(world, mover, here, to, kWalkFlags);
                if (!r.ok || r.end.x != to.x || r.end.y != to.y) continue;
                next.g = g;
                next.parent = current;
                next.z = r.end.z;
                open.push({g + heuristic(nx, ny), index(nx, ny)});
            }
        }
    }
    out.reaches_goal = found >= 0;
    int at = found >= 0 ? found : best;
    while (at > 0 && nodes[static_cast<std::size_t>(at)].parent >= 0) {
        const int cx = at % kSide - kRadius, cy = at / kSide - kRadius;
        out.points.push_back(position(cx, cy, nodes[static_cast<std::size_t>(at)].z));
        at = nodes[static_cast<std::size_t>(at)].parent;
    }
    std::reverse(out.points.begin(), out.points.end());
    return out;
}

}  // namespace crusader
