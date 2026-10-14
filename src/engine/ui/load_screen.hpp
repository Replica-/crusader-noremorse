#pragma once
// The load / save screen (segment 1008, FUN_1008_0088), opened by the main
// menu's "load" (mode 1) and "save" (mode 0) buttons. Only loading is
// reproduced: saving needs the save writer (milestone 7).
//
// Layout: the screen is cleared; background GUMPS.FLX shapes 64 + 5 * mode ..
// 67 + 5 * mode (load: 69..72, 208 x 174 each) at (111, 66), (319, 66),
// (111, 240), (319, 240) (FUN_1008_05a6: a 417 x 348 area centred on the
// screen). Eleven slots (FUN_1008_0758), slot i at (183, 109 + 27 i): shape 68
// frame 0 (314 x 17); the selected one (DS:0098, 0 at start-up, kept between
// screens) gets an outline of colour 0xFD around its rectangle; the slot name
// is written in font 14 at (+4, +12) (FUN_1008_0c9a). Palette: MISC.PAL.
//
// Slot i is SAVEGAME/SAVEGAME.<i + 1>; its name is the 32-byte entry 9 of
// that file, read at start-up into DS:7964 (CRUSYS_1058_000d, 13 names; an
// absent file gives an empty name). The original writes the name with printf
// as the format string; the port writes it as is.
//
// Keys (FUN_1008_0312): up / '8' select the previous slot (not before 0), down
// / '2' the next (not after 10), Enter chooses the selected slot, Escape closes
// with result 0. Mouse (FUN_1008_0aea): a button press on a slot selects and
// chooses it. Choosing a slot with an empty name does nothing (FUN_1008_0b63);
// otherwise the slot goes to DS:0C94 and the screen closes with result 1.

#include "core/font.hpp"
#include "core/surface.hpp"
#include "engine/world/render.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace crusader {

class LoadScreen {
public:
    static constexpr int kSlots = 11;
    static constexpr int kNames = 13;  // DS:7964 .. DS:7998
    static constexpr std::uint16_t kSlotShape = 68;
    static constexpr std::uint8_t kSelectedColour = 0xFD;
    static constexpr int kFont = 14;

    using Names = std::array<std::string, kNames>;

    struct Rect {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // inclusive
        bool contains(int x, int y) const { return x >= x0 && x <= x1 && y >= y0 && y <= y1; }
    };

    // CRUSYS_1058_000d: the names of SAVEGAME.1 .. SAVEGAME.13.
    static Names read_names(const std::filesystem::path& savegame_dir);

    // `selected` is DS:0098, kept by the caller.
    LoadScreen(ShapeCache& gumps, const FontSet& fonts, const Names& names, int& selected);

    void draw(Surface8& screen) const;
    void key(std::uint16_t code);
    void mouse_down(int x, int y);

    // Set once closed: 0 (cancelled) or 1 (slot chosen, see slot()).
    std::optional<int> result() const { return result_; }
    int slot() const { return *selected_; }  // DS:0C94 when result() is 1
    const Rect& slot_rect(int i) const { return slots_[i]; }

private:
    void choose(int i);

    ShapeCache* gumps_;
    const FontSet* fonts_;
    const Names* names_;
    int* selected_;
    std::array<Rect, kSlots> slots_{};
    std::optional<int> result_;
};

}  // namespace crusader
