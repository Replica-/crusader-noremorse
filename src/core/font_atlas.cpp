#include "core/font_atlas.hpp"

#include <algorithm>
#include <string>

namespace crusader {

// @orig none: port-only text drawn by the graphics card
FontAtlas build_font_atlas(const FontSet& fonts, int font) {
    FontAtlas atlas;
    constexpr int kScratch = 96, kPenX = 24, kPenY = 64;  // room around a glyph: it hangs from its baseline
    const TextWriter writer(fonts, font);
    struct Cut {
        int w = 0, h = 0;
        std::vector<std::uint8_t> alpha;
    };
    Cut cuts[128];
    for (int c = 32; c < 127; ++c) {
        Surface8 scratch(kScratch, kScratch);
        scratch.clear(0);
        TextWriter w(fonts, font);
        const std::string s(1, static_cast<char>(c));
        w.print(scratch, kPenX, kPenY, s);
        int minx = kScratch, miny = kScratch, maxx = -1, maxy = -1;
        for (int y = 0; y < kScratch; ++y) {
            for (int x = 0; x < kScratch; ++x) {
                if (!scratch.row(y)[x]) continue;
                minx = std::min(minx, x), maxx = std::max(maxx, x);
                miny = std::min(miny, y), maxy = std::max(maxy, y);
            }
        }
        FontAtlas::Glyph& g = atlas.glyphs[c];
        g.advance = writer.char_width(static_cast<unsigned char>(c));
        atlas.line_height = std::max(atlas.line_height, writer.char_height(static_cast<unsigned char>(c)));
        if (maxx < 0) continue;  // a space
        Cut& cut = cuts[c];
        cut.w = maxx - minx + 1;
        cut.h = maxy - miny + 1;
        cut.alpha.assign(static_cast<std::size_t>(cut.w) * static_cast<std::size_t>(cut.h), 0);
        for (int y = 0; y < cut.h; ++y) {
            for (int x = 0; x < cut.w; ++x) {
                cut.alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(cut.w) + static_cast<std::size_t>(x)] =
                    scratch.row(miny + y)[minx + x] ? 255 : 0;
            }
        }
        g.w = cut.w, g.h = cut.h;
        g.dx = minx - kPenX;
        g.dy = miny - kPenY;
    }
    // Pack in rows of 256 pixels, a pixel apart.
    atlas.width = 256;
    int x = 1, y = 1, row_h = 0;
    for (int c = 32; c < 127; ++c) {
        FontAtlas::Glyph& g = atlas.glyphs[c];
        if (g.w == 0) continue;
        if (x + g.w + 1 > atlas.width) x = 1, y += row_h + 1, row_h = 0;
        g.x = x, g.y = y;
        x += g.w + 1;
        row_h = std::max(row_h, g.h);
    }
    atlas.height = y + row_h + 1;
    atlas.rgba.assign(static_cast<std::size_t>(atlas.width) * static_cast<std::size_t>(atlas.height), 0);
    for (int c = 32; c < 127; ++c) {
        const FontAtlas::Glyph& g = atlas.glyphs[c];
        const Cut& cut = cuts[c];
        for (int gy = 0; gy < g.h; ++gy) {
            for (int gx = 0; gx < g.w; ++gx) {
                if (!cut.alpha[static_cast<std::size_t>(gy) * static_cast<std::size_t>(g.w) + static_cast<std::size_t>(gx)]) continue;
                atlas.rgba[static_cast<std::size_t>(g.y + gy) * static_cast<std::size_t>(atlas.width) + static_cast<std::size_t>(g.x + gx)] =
                    0xFFFFFFFFu;
            }
        }
    }
    return atlas;
}

}  // namespace crusader
