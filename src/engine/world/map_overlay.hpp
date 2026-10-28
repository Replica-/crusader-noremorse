#pragma once
// Port addition: what the mission map (M) shows, in "map pixels": the isometric projection of the level seen from above,
// without the height. Built by the game, drawn by the presenter.

#include <cstdint>
#include <vector>

namespace crusader {

struct MapPoly {
    float x[4], y[4];  // the corners of a footprint
    std::uint8_t r, g, b, a;
};

struct MapMarker {
    float x, y;
    int kind;  // 0 you, 1 enemy, 2 key item, 3 weapon or ammunition, 4 something to use, 5 trigger
};

struct MapOverlay {
    bool on = false;
    std::vector<MapPoly> polys;  // floors first, then walls
    std::vector<MapMarker> markers;
    float heading_x = 0, heading_y = 0;  // the way you face on the map
    // The view: the map pixel at the middle of the panel, and screen pixels per map pixel (the wheel zooms, a drag pans).
    float center_x = 0, center_y = 0, zoom = 0.5f;
    bool follow = true;  // the view stays on you until it is panned
};

}  // namespace crusader

namespace crusader {

// The panel the map is drawn in, for a view of w x h pixels (the legend and hints are drawn inside it).
struct MapPanel {
    float x, y, w, h;
};
inline MapPanel map_panel(int w, int h) { return {24.0f, 24.0f, static_cast<float>(w) - 48.0f, static_cast<float>(h) - 24.0f - 56.0f}; }

}  // namespace crusader
