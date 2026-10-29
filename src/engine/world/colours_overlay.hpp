#pragma once
// Port addition: what the character colour screen and the recoloured player are drawn from on the graphics card. Built by
// the game, drawn by the presenter.

#include "core/palette.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace crusader {

// A rectangle in screen pixels; its corners (top-left, top-right, bottom-left, bottom-right) are RGBA, bytes R, G, B, A in memory.
struct ColourRect {
    float x = 0, y = 0, w = 0, h = 0;
    std::array<std::uint32_t, 4> corner{};
    bool outline = false;  // only its border, 2 pixels wide
};

struct ColourLabel {
    float x = 0, y = 0;  // y is the baseline
    std::string text;
    std::uint8_t r = 255, g = 255, b = 255;
    int align = 0;  // 0 left, 1 centred, 2 right of x
    float scale = 1.0f;
};

struct ColoursOverlay {
    bool on = false;
    std::vector<ColourRect> rects;
    std::vector<ColourLabel> labels;
    // The character, as a picture (bytes R, G, B, A in memory) shown `scale` times as big with its top-left at (ix, iy).
    int iw = 0, ih = 0, ax = 0, ay = 0;  // its size and the anchor (the feet) in it
    float ix = 0, iy = 0, iscale = 1;
    std::uint32_t image_key = 0;  // changes with the picture
    std::vector<std::uint32_t> image;
};

// The player's own sprite, drawn again by the graphics card in its chosen colours over the frame where it is not hidden.
struct PlayerLayer {
    bool on = false;
    int x = 0, y = 0, w = 0, h = 0;      // where on the frame
    std::vector<std::uint8_t> index;     // w * h palette indices, 255 for none
    std::array<std::uint32_t, 256> recolour{};  // the colour for each index (alpha 0: leave the frame's own)
    Palette base;                        // the unfaded palette, to follow the fades
};

}  // namespace crusader

namespace crusader {

// One of the pictures of the turning player, drawn through a transform: turned by `rotation` degrees (clockwise) about its
// pivot, sheared sideways by `shear` (sideways shift per pixel of height) and squeezed across by `scale_x`.
struct SpriteLayer {
    int x = 0, y = 0, w = 0, h = 0;     // where on the frame
    float pivot_x = 0, pivot_y = 0;     // the point it turns about, in the picture
    float rotation = 0, shear = 0, scale_x = 1, alpha = 1, shift = 0;
    std::vector<std::uint8_t> index;    // w * h palette indices, 255 for none
};

// The player between two of his 16 pictures (GameConfig::turn_rotation): the nearest one and its neighbour towards the pointer,
// each transformed for the exact angle, the neighbour faded over the nearest one by how far the turn is. Kept out of the frame,
// drawn by the graphics card, with what was in front of him drawn again over.
struct PlayerSprite {
    bool on = false;
    int count = 0;
    std::array<SpriteLayer, 2> layer;
    // What was drawn over the player afterwards, as the frame has it (palette indices, 255 for none), to draw again over him.
    int ox = 0, oy = 0, ow = 0, oh = 0;
    std::vector<std::uint8_t> occluder;
    std::array<std::uint32_t, 256> recolour{};  // the player's chosen colours (alpha 0: the palette's own)
    Palette base;                       // the unfaded palette, to follow the fades
};

}  // namespace crusader
