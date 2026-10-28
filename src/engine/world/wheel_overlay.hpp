#pragma once
// Port addition: what the weapon wheel (Tab) and the item wheel (I) show. Built by the game, drawn by the presenter.

#include <string>
#include <vector>

namespace crusader {

struct WheelEntry {
    std::string label;   // the name
    std::string detail;  // rounds loaded, how many, ...
    bool active = false;   // in hand / the active item
    bool stashed = false;  // carried but in no slot: chosen, it swaps into one
    int icon = -1;         // the icon (a texture id the presenter has been given), -1 for none
    unsigned id = 0;
};

struct WheelOverlay {
    bool on = false;
    int kind = 0;  // 0 weapons, 1 items
    std::string title;
    std::vector<WheelEntry> entries;
    int hover = -1;  // the entry under the pointer, -1 for none
    float cx = 0, cy = 0;  // the middle of the wheel, view pixels
    float inner = 70.0f, outer = 190.0f;
};

}  // namespace crusader

#include <cstdint>

namespace crusader {

// An icon of the game's own (GUMPS.FLX), as a picture for the presenter to keep.
struct WheelIcon {
    int id = 0;
    int width = 0, height = 0;
    std::vector<std::uint32_t> rgba;  // bytes R, G, B, A in memory
};

}  // namespace crusader
