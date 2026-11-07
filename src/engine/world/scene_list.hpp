#pragma once
// Port addition (GameConfig::hardware_scene): the scene handed to the graphics card instead of being drawn into the
// software frame. The game fills a list of these, in draw order, each frame; the presenter draws them under the frame
// (the frame's pixels of colour kSceneKey are left clear so the scene shows through) with one texture per picture.

#include "core/shape.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace crusader {

// The colour of the software frame that shows the hardware scene through. It is also what the frame is cleared to.
constexpr std::uint8_t kSceneKey = 255;

struct SceneCmd {
    const ShapeFrame* frame = nullptr;  // null: a splat of blood on the floor
    int x = 0, y = 0;                   // a picture: its top-left corner on the view; a splat: its centre
    bool mirrored = false, translucent = false;
    float rx = 0, ry = 0, alpha = 0;    // a splat: its radii and opacity
    float room = 0;                     // with the lighting: its room's ambient, times SceneLighting::ambient (0: dark)
};

// Port addition (GameConfig::lighting): a light over the hardware scene, a soft ellipse twice as wide as high (the floor
// seen in the isometric view). Lights do not add up: where they meet, the brightest wins.
struct SceneLight {
    float x = 0, y = 0;                 // its centre on the view
    float radius = 0;                   // half its width, view pixels
    float r = 1, g = 1, b = 1;          // its colour, 0..1
    float intensity = 1;                // multiplies the colour (the map saturates at 1)
};

// The light map the scene is multiplied by: each picture of the scene gives its pixels its room's ambient (SceneCmd::room
// times `ambient`, `dark` outside any lit room), then the lights over it. A value of 0.5 leaves the scene's own colours
// as drawn (the art has its light painted in), 1 doubles them.
struct SceneLighting {
    // By default the scene is shown at about 80 % of its painted light everywhere (0.4: 0.5 is the art as drawn), so the
    // lights show without the rest going dark; the lit rooms a little more (SceneCmd::room, the ambient times it).
    float dark_r = 0.40f, dark_g = 0.40f, dark_b = 0.41f;
    float ambient_r = 0.40f, ambient_g = 0.40f, ambient_b = 0.41f;
    std::vector<SceneLight> lights;
};


// Port addition: the light debug mode (F7 with the lighting on). The floor cells of the rooms the lights light, as
// diamonds on the view (one colour per room), the lights and where each room's search started, and a line of text.
struct LightDebugOverlay {
    bool on = false;
    struct Cell {
        float x[4], y[4];
        std::uint8_t r, g, b;
    };
    std::vector<Cell> cells;
    struct Mark {
        float x, y;
        bool seed;  // where a room's search started (else a light)
        bool picked;
    };
    std::vector<Mark> marks;
    struct Label {
        float x, y;
        std::string text;
    };
    std::vector<Label> labels;  // each room's name, at its north-west floor cell
    std::string text;
};

}  // namespace crusader
