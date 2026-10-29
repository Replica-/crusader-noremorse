#include "engine/ui/controls_screen.hpp"

#include "engine/ui/enhancements_screen.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>
#include <string>
#include <vector>

namespace crusader {

namespace {

constexpr std::uint8_t kFrameColour = 0x57, kGreen = 0x0C, kDim = 0x75;
constexpr int kTitleFont = 14, kBodyFont = 4;
constexpr int kUnit = 24;  // a key is this wide

// Text in one colour: drawn on a scratch surface, copied in `tint`.
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

// One key of the keyboard picture: a framed square with its label, lit when the port uses it.
void draw_key(Surface8& screen, const FontSet& fonts, int x, int y, int w, int h, const char* label, bool lit) {
    draw_rounded_box(screen, x, y, x + w - 1, y + h - 1, -1, lit ? kGreen : kDim);
    if (lit) outline(screen, x + 2, y + 2, x + w - 3, y + h - 3, kDim);
    TextWriter writer(fonts, kBodyFont);
    const int tw = writer.measure(label).width;
    print_tinted(screen, fonts, kBodyFont, x + (w - tw) / 2, y + h / 2 + 3, label, lit ? kGreen : kDim);
}

}  // namespace

// @orig none: port-only controls screen
void ControlsScreen::draw(Surface8& screen) const {
    screen.clear(0);
    draw_rounded_box(screen, 14, 12, 625, 467, -1, kFrameColour);
    draw_rounded_box(screen, 17, 15, 622, 464, -1, kFrameColour);
    const std::string title = "KEYBOARD + MOUSE CONTROLS";
    const int title_width = TextWriter(*fonts_, kTitleFont).measure(title).width;
    print_tinted(screen, *fonts_, kTitleFont, (screen.width - title_width) / 2, 46, title, kGreen);

    // The keyboard: the rows of the left hand, with the keys the port uses lit.
    constexpr int kx = 30, ky = 62, h = 22, gap = 3;
    auto row = [&](int y, int x, std::initializer_list<std::pair<const char*, bool>> keys, int width = kUnit) {
        for (const auto& [label, lit] : keys) {
            draw_key(screen, *fonts_, x, y, width, h, label, lit);
            x += width + gap;
        }
    };
    draw_key(screen, *fonts_, kx, ky, 40, h, "ESC", true);
    row(ky + 26, kx, {{"TAB", true}}, 38);
    row(ky + 26, kx + 41, {{"Q", true}, {"W", true}, {"E", true}, {"R", false}, {"T", false}, {"Y", false}, {"U", false}, {"I", true}, {"O", false}});
    row(ky + 52, kx + 8, {{"A", true}, {"S", true}, {"D", true}, {"F", false}, {"G", false}, {"H", false}, {"J", false}, {"K", false}});
    draw_key(screen, *fonts_, kx - 6, ky + 78, 52, h, "SHIFT", true);
    row(ky + 78, kx + 49, {{"Z", false}, {"X", false}, {"C", true}, {"V", false}, {"B", false}, {"N", false}, {"M", true}});
    draw_key(screen, *fonts_, kx + 22, ky + 104, 190, h, "SPACE", true);
    draw_key(screen, *fonts_, kx - 6, ky + 104, 26, h, "CTRL", false);

    // The mouse: its body with the buttons and the wheel.
    const int mx = 470, my = 66, mw = 76, mh = 112;
    draw_rounded_box(screen, mx, my, mx + mw - 1, my + mh - 1, -1, kDim);
    draw_rounded_box(screen, mx + 3, my + 3, mx + mw / 2 - 2, my + 44, -1, kGreen);   // left button
    draw_rounded_box(screen, mx + mw / 2 + 2, my + 3, mx + mw - 4, my + 44, -1, kGreen);  // right button
    draw_rounded_box(screen, mx + mw / 2 - 5, my + 12, mx + mw / 2 + 4, my + 32, -1, kGreen);  // wheel
    TextWriter small(*fonts_, kBodyFont);
    print_tinted(screen, *fonts_, kBodyFont, mx + 14, my + 28, "L", kGreen);
    print_tinted(screen, *fonts_, kBodyFont, mx + mw - 24, my + 28, "R", kGreen);
    print_tinted(screen, *fonts_, kBodyFont, mx + 8, my + 76, "MOUSE", kDim);

    // What each does, in two columns: the key (or button) in green, then what it does.
    struct Line {
        const char* keys;
        const char* what;
    };
    const std::vector<Line> left = {
        {"W", "walk to cursor (hold)"},
        {"SHIFT + W", "run to cursor"},
        {"A / D", "step left / right"},
        {"S", "step back"},
        {"Q / E", "roll left / right"},
        {"C", "kneel / stand up"},
        {"SPACE", "jump (hold: aim it)"},
        {"ESC", "menu"},
    };
    const std::vector<Line> right = {
        {"LEFT CLICK", "walk / use item"},
        {"DOUBLE CLICK", "run there"},
        {"LEFT, GUN OUT", "holster, then go"},
        {"RIGHT CLICK", "draw / holster gun"},
        {"HOLD RIGHT", "fire at the cursor"},
        {"TAB", "weapon wheel"},
        {"I", "item wheel"},
        {"M", "mission map"},
    };
    const int top = 220, step = 15;
    print_tinted(screen, *fonts_, kBodyFont, 30, top - 8, "MOVEMENT", kDim);
    print_tinted(screen, *fonts_, kBodyFont, 330, top - 8, "MOUSE AND MENUS", kDim);
    for (std::size_t i = 0; i < left.size(); ++i) {
        const int y = top + 10 + static_cast<int>(i) * step;
        print_tinted(screen, *fonts_, kBodyFont, 30, y, left[i].keys, kGreen);
        print_tinted(screen, *fonts_, kBodyFont, 118, y, left[i].what, kFrameColour);
    }
    for (std::size_t i = 0; i < right.size(); ++i) {
        const int y = top + 10 + static_cast<int>(i) * step;
        print_tinted(screen, *fonts_, kBodyFont, 330, y, right[i].keys, kGreen);
        print_tinted(screen, *fonts_, kBodyFont, 446, y, right[i].what, kFrameColour);
    }
    print_tinted(screen, *fonts_, kBodyFont, 330, top + 10 + 8 * step, "WHEEL ZOOMS THE MAP, DRAG PANS IT", kDim);
    print_tinted(screen, *fonts_, kBodyFont, 30, 444, "ANY KEY OR CLICK CLOSES THIS SCREEN", kDim);
}

}  // namespace crusader

namespace crusader {

// @orig none: port-only help page
void draw_updated_help_page(Surface8& picture, const Palette& palette, const FontSet& fonts) {
    auto nearest = [&](int r, int g, int b) {
        int best = 1, best_distance = 1 << 30;
        for (int i = 1; i < 256; ++i) {
            const Rgb& c = palette.colors[static_cast<std::size_t>(i)];
            const int d = (c.r - r) * (c.r - r) + (c.g - g) * (c.g - g) + (c.b - b) * (c.b - b);
            if (d < best_distance) best = i, best_distance = d;
        }
        return best;
    };
    const int red = nearest(230, 30, 30), green = nearest(60, 230, 70);
    picture.clear(0);

    struct Line {
        const char* key;
        const char* what;
    };
    const Line game[] = {
        {"F1", "HELP SCREEN"},           {"F2", "OPTIONS SCREEN"},       {"F4", "QUICK LOAD"},
        {"F5", "QUICK SAVE"},            {"F6", "LOAD THE AUTOSAVE"},    {"F8", "LOAD SCREEN"},
        {"F9", "SAVE SCREEN"},           {"+", "INCREASE MUSIC VOLUME"}, {"-", "DECREASE MUSIC VOLUME"},
        {"ESC", "GAME OR MAIN MENU"},    {"ALT X", "EXIT TO DOS"},       {"ALT ENTER", "FULLSCREEN ON / OFF"},
    };
    const Line player[] = {
        {"W", "WALK TO THE CURSOR (HOLD)"},   {"SHIFT W", "RUN TO THE CURSOR"},      {"A  D", "STEP LEFT / RIGHT"},
        {"S", "STEP BACK"},                   {"Q  E", "ROLL LEFT / RIGHT"},         {"C", "KNEEL / STAND UP"},
        {"SPACE", "JUMP, HOLD TO AIM"},                  {"RIGHT CLICK", "DRAW / HOLSTER, HOLD FIRES"},
        {"LEFT CLICK", "WALK THERE / USE AN ITEM"}, {"TAB", "WEAPON WHEEL"},         {"I", "ITEM WHEEL"},
        {"M", "MISSION MAP"},                 {"U", "USE THE ACTIVE ITEM"},          {"B", "DETONATE BOMB"},
        {"Z", "CENTER CAMERA"},               {"G", "GRAB EXPOSED ITEMS"},
    };
    auto heading = [&](int x, const char* text) {
        print_tinted(picture, fonts, kBodyFont, x, 52, text, green);
        hline(picture, x, 56, x + 150, static_cast<std::uint8_t>(green));
    };
    heading(62, "GAME CONTROL");
    heading(330, "PLAYER INTERFACE");
    auto key_box = [&](int x, int y, const char* label) {
        const int width = TextWriter(fonts, kBodyFont).measure(label).width + 10;
        draw_rounded_box(picture, x, y - 8, x + width - 1, y + 7, -1, static_cast<std::uint8_t>(red));
        print_tinted(picture, fonts, kBodyFont, x + 5, y, label, red);
        return width;
    };
    int y = 82;
    for (const Line& l : game) {
        key_box(62, y, l.key);
        print_tinted(picture, fonts, kBodyFont, 150, y, l.what, green);
        y += 22;
    }
    y = 82;
    for (const Line& l : player) {
        key_box(330, y, l.key);
        print_tinted(picture, fonts, kBodyFont, 430, y, l.what, green);
        y += 22;
    }
    print_tinted(picture, fonts, kBodyFont, 62, 440, "THE FULL PICTURE OF THE KEYBOARD AND MOUSE IS ON THE PREVIOUS SCREEN", green);
}

}  // namespace crusader
