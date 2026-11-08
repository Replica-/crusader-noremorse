#pragma once
// Port addition (GameConfig::hardware_scene): the scene handed to the graphics card instead of being drawn into the
// software frame. The game fills a list of these, in draw order, each frame; the presenter draws them under the frame
// (the frame's pixels of colour kSceneKey are left clear so the scene shows through) with one texture per picture.

#include "core/shape.hpp"
#include "engine/world/map_overlay.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace crusader {

// The colour of the software frame that shows the hardware scene through. It is also what the frame is cleared to.
constexpr std::uint8_t kSceneKey = 255;

struct SceneCmd {
    const ShapeFrame* frame = nullptr;  // null: a splat of blood on the floor
    int shape = -1, frame_no = 0;       // a map item's picture: its shape and frame (the upscaled sprites); -1 otherwise
    bool fx = false;                    // a fire or an explosion: drawn smooth and glowing (its own frames)
    // The player in his combat stance between two of his 16 facings (GameConfig::extra_angles): the picture
    // assets/angles/<angle_set>_s<angle_step>.png (set c0..c4 the gun class, cn none; step 0..63) with its anchor (the
    // cell's 80, 120) at (anchor_x, anchor_y); angle_step -1 otherwise.
    int angle_step = -1;
    const char* angle_set = "";
    int anchor_x = 0, anchor_y = 0;
    int x = 0, y = 0;                   // a picture: its top-left corner on the view; a splat: its centre
    bool mirrored = false, translucent = false;
    float rx = 0, ry = 0, alpha = 0;    // a splat: its radii and opacity
    float ambient_r = -1, ambient_g = 0, ambient_b = 0;  // with the lighting: its area's ambient (the light editor), <0 the map's
};

// Port addition: a muzzle flash over the hardware scene, a short glow where a shot left the gun (`strength` 1 when fired,
// fading to 0).
struct SceneFlash {
    float x = 0, y = 0;
    float strength = 1;
};

// Port addition (GameConfig::lighting): a light over the hardware scene, a soft ellipse twice as wide as high (the floor
// seen in the isometric view). Lights do not add up: where they meet, the brightest wins.
struct SceneLight {
    float x = 0, y = 0;                 // its centre on the view
    float radius = 0;                   // half its width, view pixels
    float r = 1, g = 1, b = 1;          // its colour, 0..1
    float intensity = 1;                // multiplies the colour (the map saturates at 1)
    float core = 0;                     // a bright core, this part of the radius (a sphere's size; 0: none)
    bool beam = false;                  // a cone: a beam from its source (bx, by) on the view down to the spot
    float bx = 0, by = 0;
};

// The light map the scene is multiplied by: each picture of the scene gives its pixels its area's ambient (SceneCmd, set
// with the light editor; `dark`, the map's, elsewhere), then the lights over it. A value of 0.5 leaves the scene's colours
// as drawn (the art has its light painted in), 1 doubles them.
struct SceneLighting {
    // The map's ambient, the same everywhere: by default about 85 % of its painted light (0.425: 0.5 is the art as drawn),
    // so the lights show without the rest going dark; the light editor sets it per map (MapLights::ambient).
    float dark_r = 0.425f, dark_g = 0.425f, dark_b = 0.435f;
    float ambient_r = 0.425f, ambient_g = 0.425f, ambient_b = 0.435f;
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
        std::uint8_t style = 0;  // 0 ring or cross (seed), 1 square (an ambient area), 2 triangle (a cone)
    };
    std::vector<Mark> marks;
    struct Label {
        float x, y;
        std::string text;
    };
    std::vector<Label> labels;  // each room's name, at its north-west floor cell
    std::string text;
    std::vector<std::string> lines;  // more lines under it (the light editor's keys)
    float text_scale = 1;            // its text's size (the light editor zoomed out: as large as at 1)
    // The light editor's panels (its toolbar, the menu of a light): boxes with a line of text, drawn over all the rest.
    struct Box {
        float x, y, w, h;
        std::uint8_t fill[4], edge[4], ink[3];
        std::string text;
        float text_scale = 1;
        std::string right;  // more text, at its right end
    };
    std::vector<Box> boxes;
    // The light editor's mini map of the whole level: `polys` (map pixels, MapOverlay's) drawn at map pixel * scale + (ox, oy)
    // in the panel (x, y, w, h) of the view, with the view's outline (corners, map pixels). No polys: none.
    struct MiniMap {
        const std::vector<MapPoly>* polys = nullptr;
        float x = 0, y = 0, w = 0, h = 0, ox = 0, oy = 0, scale = 1;
        float view_x[4]{}, view_y[4]{};
    } minimap;
};

}  // namespace crusader
