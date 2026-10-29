#pragma once
// Small drawing helpers shared by the port's own screens (options panel, controls screen, controller screen).

#include "core/font.hpp"
#include "core/surface.hpp"

#include <cstdint>
#include <string>

namespace crusader {

// A frame with its corners cut like the original buttons', filled with `fill` (a palette index) unless it is negative.
void draw_rounded_box(Surface8& screen, int x0, int y0, int x1, int y1, int fill, std::uint8_t colour);

// A "+" in a square frame of `size` pixels, top-left at (x, y).
void draw_plus_icon(Surface8& screen, int x, int y, int size, std::uint8_t colour);

// Text of font `font` in one colour (the font has its own): drawn on a scratch surface, copied in `tint`. `y` is the baseline.
void print_tinted(Surface8& screen, const FontSet& fonts, int font, int x, int y, const std::string& text, int tint);

}  // namespace crusader
