#include "engine/ui/ui_draw.hpp"

#include <algorithm>

namespace crusader {

// @orig none: port-only drawing helper
void print_tinted(Surface8& screen, const FontSet& fonts, int font, int x, int y, const std::string& text, int tint) {
    TextWriter writer(fonts, font);
    constexpr int kPad = 4, kAbove = 32, kHeight = 48;
    const int width = writer.measure(text).width + 2 * kPad;
    Surface8 scratch(width, kHeight);
    writer.print(scratch, kPad, kAbove, text);
    for (int sy = 0; sy < kHeight; ++sy) {
        for (int sx = 0; sx < width; ++sx) {
            const int dx = x - kPad + sx, dy = y - kAbove + sy;
            if (scratch.row(sy)[sx] && dx >= 0 && dy >= 0 && dx < screen.width && dy < screen.height) {
                screen.row(dy)[dx] = static_cast<std::uint8_t>(tint);
            }
        }
    }
}

// @orig none: port-only drawing helper
void draw_rounded_box(Surface8& screen, int x0, int y0, int x1, int y1, int fill, std::uint8_t colour) {
    // Corners cut over three pixels, like the frames of the original buttons.
    constexpr int kInset[3] = {3, 2, 1};
    for (int y = y0; y <= y1; ++y) {
        const int d = std::min(y - y0, y1 - y);
        const int in = d < 3 ? kInset[d] : 0;
        if (fill >= 0) fill_rect(screen, x0 + in, y, x1 - in, y, static_cast<std::uint8_t>(fill));
        if (d == 0) {
            hline(screen, x0 + in, y, x1 - in, colour);
        } else {
            outline(screen, x0 + in, y, x0 + in, y, colour);
            outline(screen, x1 - in, y, x1 - in, y, colour);
        }
    }
}

// @orig none: port-only drawing helper
void draw_plus_icon(Surface8& screen, int x, int y, int size, std::uint8_t colour) {
    outline(screen, x, y, x + size - 1, y + size - 1, colour);
    const int mid = x + size / 2, midy = y + size / 2;
    const int arm = size / 2 - 2;
    fill_rect(screen, mid - arm, midy - 1, mid + arm, midy, colour);
    fill_rect(screen, mid - 1, midy - arm, mid, midy + arm, colour);
}

}  // namespace crusader
