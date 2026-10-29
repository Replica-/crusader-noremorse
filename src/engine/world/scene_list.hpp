#pragma once
// Port addition (GameConfig::hardware_scene): the scene handed to the graphics card instead of being drawn into the
// software frame. The game fills a list of these, in draw order, each frame; the presenter draws them under the frame
// (the frame's pixels of colour kSceneKey are left clear so the scene shows through) with one texture per picture.

#include "core/shape.hpp"

#include <cstdint>
#include <vector>

namespace crusader {

// The colour of the software frame that shows the hardware scene through. It is also what the frame is cleared to.
constexpr std::uint8_t kSceneKey = 255;

struct SceneCmd {
    const ShapeFrame* frame = nullptr;  // null: a splat of blood on the floor
    int x = 0, y = 0;                   // a picture: its top-left corner on the view; a splat: its centre
    bool mirrored = false, translucent = false;
    float rx = 0, ry = 0, alpha = 0;    // a splat: its radii and opacity
};

}  // namespace crusader
