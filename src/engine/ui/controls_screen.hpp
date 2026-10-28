#pragma once
// The keyboard and mouse controls screen (port addition), opened from the enhancements window's "controles" row. A picture of
// the keyboard, with the keys the port uses lit, a picture of the mouse with its buttons, and the list of what each does.
//
// Look: the screen cleared, a red frame (colour 0x57) with the title in green, the lit keys outlined and labelled in green
// (0x0C), the others dim (0x75). Any key or click closes it.

#include "core/font.hpp"
#include "core/surface.hpp"

#include <cstdint>

namespace crusader {

class ControlsScreen {
public:
    explicit ControlsScreen(const FontSet& fonts) : fonts_(&fonts) {}

    void draw(Surface8& screen) const;
    void key(std::uint16_t) { closed_ = true; }
    void mouse_down(int, int) { closed_ = true; }
    bool closed() const { return closed_; }

private:
    const FontSet* fonts_;
    bool closed_ = false;
};

}  // namespace crusader
