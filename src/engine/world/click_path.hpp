#pragma once
// Port addition: a path to a point for the controlled NPC (the keyboard and mouse
// mode's click to walk). The original's PathFinder (pathfind.hpp) only steers
// greedily and follows walls, which gets stuck on anything but a straight wall;
// this is an A* over a 16-unit grid, each step tested with the same swept-box
// move the walk uses, so it goes round what really blocks the NPC.

#include "engine/world/world.hpp"

#include <vector>

namespace crusader {

struct PlannedPath {
    std::vector<World::Point> points;  // the grid nodes to walk through, the start excluded
    bool reaches_goal = false;         // false: the closest it could get
    int expanded = 0;                  // nodes searched (debug)
};

// A path of `mover` from `from` to near `goal` (within one cell).
PlannedPath plan_path(const World& world, ObjId mover, const World::Point& from, const World::Point& goal);

// Whether `mover` can walk straight from `from` to `to` (the swept move reaches it).
bool straight_walk_ok(const World& world, ObjId mover, const World::Point& from, const World::Point& to);

}  // namespace crusader
