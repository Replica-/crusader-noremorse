#pragma once
// Port addition: what the targeted jump shows while J is held. Built by the game, drawn by the presenter.

#include <cstdint>
#include <utility>
#include <vector>

namespace crusader {

struct JumpPreview {
    bool on = false;                                // J is held long enough and a landing place exists
    std::vector<std::pair<float, float>> arc;       // the flight, view pixels
    float x = 0, y = 0;                             // where the player's anchor lands, view pixels
    // The player's picture as it will land (a ghost): the frame it shows now, recoloured by the game's palette.
    std::uint32_t key = 0;                          // changes with the picture
    int width = 0, height = 0, xoff = 0, yoff = 0;  // the frame and its anchor
    bool mirrored = false;
    std::vector<std::uint32_t> rgba;                // bytes R, G, B, A in memory
};

}  // namespace crusader
