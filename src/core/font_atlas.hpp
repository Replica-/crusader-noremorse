#pragma once
// Port addition: the game's bitmap font as an RGBA atlas (white glyphs on transparent), so the graphics card can draw text
// in any colour without the game's palette.

#include "core/font.hpp"

#include <cstdint>
#include <vector>

namespace crusader {

struct FontAtlas {
    struct Glyph {
        int x = 0, y = 0, w = 0, h = 0;  // in the atlas
        int dx = 0, dy = 0;              // from the pen position on the baseline to the glyph's top left
        int advance = 0;
    };
    int width = 0, height = 0;
    std::vector<std::uint32_t> rgba;  // bytes R, G, B, A (white, alpha 0 or 255)
    Glyph glyphs[128];
    int line_height = 0;
    bool valid() const { return width > 0 && !rgba.empty(); }
};

// The printable ASCII characters of `font` (see TextWriter), packed in rows.
FontAtlas build_font_atlas(const FontSet& fonts, int font);

}  // namespace crusader
