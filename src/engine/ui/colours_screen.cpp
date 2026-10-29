#include "engine/ui/colours_screen.hpp"

#include "engine/ui/gump_keys.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace crusader {

namespace {

// Layout, screen pixels.
constexpr float kPanelX = 40, kPanelY = 28, kPanelW = 560, kPanelH = 446;
constexpr float kPicX = 62, kPicY = 96, kPicW = 190, kPicH = 236;
constexpr float kTabX = 276, kTabY = 76, kTabW = 300, kTabH = 36, kTabStep = 40;
constexpr float kSvX = 276, kSvY = 204, kSvW = 190, kSvH = 126;
constexpr float kHueX = 476, kHueY = 204, kHueW = 22, kHueH = 126;
constexpr float kQuickX = 276, kQuickY = 354, kQuickSize = 21, kQuickGap = 4;
constexpr float kPresetX = 276, kPresetY = 384, kPresetW = 68, kPresetH = 24, kPresetGap = 4;
constexpr float kButtonY = 420, kButtonW = 88, kButtonH = 22;
constexpr float kResetX = 62, kDoneX = 160;

constexpr std::uint32_t rgba(int r, int g, int b, int a = 255) {
    return static_cast<std::uint32_t>(a) << 24 | static_cast<std::uint32_t>(b) << 16 | static_cast<std::uint32_t>(g) << 8 | static_cast<std::uint32_t>(r);
}
constexpr std::uint32_t from_rgb24(std::uint32_t c, int a = 255) { return rgba((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, a); }

const char* const kFeatureNames[kFeatureCount] = {"MAIN COLOUR", "CHEST LOGO", "BACKPACK"};
const char* const kFeatureHints[kFeatureCount] = {"THE ARMOUR", "THE EMBLEM", "THE PACK"};

// Quick colours for any feature.
constexpr std::uint32_t kQuick[] = {0xC01010, 0xE8601C, 0xF0C020, 0x37C837, 0x0E8C8C, 0x3A6AE0, 0x7A3AD0, 0xC83C9C,
                                    0xF4F4F4, 0x9AA0AA, 0x2C2F36, 0x6B4A18};
constexpr int kQuickCount = static_cast<int>(sizeof(kQuick) / sizeof(kQuick[0]));

bool inside(int x, int y, float rx, float ry, float rw, float rh) {
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    return fx >= rx && fx < rx + rw && fy >= ry && fy < ry + rh;
}

ColourRect box(float x, float y, float w, float h, std::uint32_t c, bool outline = false) {
    ColourRect r;
    r.x = x, r.y = y, r.w = w, r.h = h;
    r.corner = {c, c, c, c};
    r.outline = outline;
    return r;
}

}  // namespace

ColoursScreen::ColoursScreen(GameConfig& config) : config_(&config) { sync_hsv(); }

// @orig none: port-only player colours
std::uint32_t ColoursScreen::to_rgb(const Hsv& c) {
    const float h = std::fmod(std::max(0.0f, c.h), 360.0f) / 60.0f;
    const int i = static_cast<int>(h);
    const float f = h - static_cast<float>(i), p = c.v * (1 - c.s), q = c.v * (1 - c.s * f), t = c.v * (1 - c.s * (1 - f));
    float r = c.v, g = t, b = p;
    switch (i % 6) {
    case 1: r = q, g = c.v, b = p; break;
    case 2: r = p, g = c.v, b = t; break;
    case 3: r = p, g = q, b = c.v; break;
    case 4: r = t, g = p, b = c.v; break;
    case 5: r = c.v, g = p, b = q; break;
    default: break;
    }
    auto byte = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return byte(r) << 16 | byte(g) << 8 | byte(b);
}

// @orig none: port-only player colours
ColoursScreen::Hsv ColoursScreen::from_rgb(std::uint32_t rgb) {
    const float r = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, g = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
                b = static_cast<float>(rgb & 0xFF) / 255.0f;
    const float hi = std::max({r, g, b}), lo = std::min({r, g, b}), d = hi - lo;
    Hsv c;
    c.v = hi;
    c.s = hi <= 0 ? 0 : d / hi;
    if (d > 0) {
        if (hi == r) c.h = 60.0f * std::fmod((g - b) / d, 6.0f);
        else if (hi == g) c.h = 60.0f * ((b - r) / d + 2.0f);
        else c.h = 60.0f * ((r - g) / d + 4.0f);
        if (c.h < 0) c.h += 360.0f;
    }
    return c;
}

void ColoursScreen::sync_hsv() { hsv_ = from_rgb(config_->player_colours.rgb[static_cast<std::size_t>(feature_)]); }

// @orig none: port-only player colours
void ColoursScreen::set_colour(int feature, std::uint32_t rgb, bool keep_hsv) {
    PlayerColours& p = config_->player_colours;
    p.rgb[static_cast<std::size_t>(feature)] = rgb;
    p.custom = !(p.rgb == PlayerColours::original().rgb);
    if (!keep_hsv && feature == feature_) hsv_ = from_rgb(rgb);
}

void ColoursScreen::set_from_hsv() { set_colour(feature_, to_rgb(hsv_), true); }

// @orig none: port-only player colours
void ColoursScreen::apply_preset(int preset) {
    int count = 0;
    const PlayerPreset* presets = player_presets(count);
    if (preset < 0 || preset >= count) return;
    PlayerColours& p = config_->player_colours;
    p.rgb = presets[preset].rgb;
    p.custom = !(p.rgb == PlayerColours::original().rgb);
    sync_hsv();
}

void ColoursScreen::reset() {
    config_->player_colours = PlayerColours::original();
    sync_hsv();
}

// @orig none: port-only player colours
void ColoursScreen::key(std::uint16_t code) {
    switch (code) {
    case gump_key::kEscape:
    case gump_key::kEnter: closed_ = true; break;
    case gump_key::kUp: feature_ = (feature_ + kFeatureCount - 1) % kFeatureCount; sync_hsv(); break;
    case gump_key::kDown: feature_ = (feature_ + 1) % kFeatureCount; sync_hsv(); break;
    default: break;
    }
}

// The pointer at (x, y) while dragging in the colour square or the hue bar.
void ColoursScreen::pick(int x, int y) {
    if (drag_ == 1) {
        hsv_.s = std::clamp((static_cast<float>(x) - kSvX) / (kSvW - 1), 0.0f, 1.0f);
        hsv_.v = 1.0f - std::clamp((static_cast<float>(y) - kSvY) / (kSvH - 1), 0.0f, 1.0f);
        set_from_hsv();
    } else if (drag_ == 2) {
        hsv_.h = std::clamp((static_cast<float>(y) - kHueY) / (kHueH - 1), 0.0f, 1.0f) * 359.0f;
        set_from_hsv();
    }
}

// @orig none: port-only player colours
void ColoursScreen::mouse_down(int x, int y) {
    for (int f = 0; f < kFeatureCount; ++f) {
        if (inside(x, y, kTabX, kTabY + kTabStep * static_cast<float>(f), kTabW, kTabH)) {
            feature_ = f;
            sync_hsv();
            return;
        }
    }
    if (inside(x, y, kSvX, kSvY, kSvW, kSvH)) {
        drag_ = 1;
        pick(x, y);
        return;
    }
    if (inside(x, y, kHueX, kHueY, kHueW, kHueH)) {
        drag_ = 2;
        pick(x, y);
        return;
    }
    for (int i = 0; i < kQuickCount; ++i) {
        if (inside(x, y, kQuickX + (kQuickSize + kQuickGap) * static_cast<float>(i), kQuickY, kQuickSize, kQuickSize)) {
            set_colour(feature_, kQuick[i], false);
            return;
        }
    }
    int count = 0;
    player_presets(count);
    for (int i = 0; i < count; ++i) {
        const float px = kPresetX + (kPresetW + kPresetGap) * static_cast<float>(i % 4);
        const float py = kPresetY + (kPresetH + kPresetGap) * static_cast<float>(i / 4);
        if (inside(x, y, px, py, kPresetW, kPresetH)) {
            apply_preset(i);
            return;
        }
    }
    if (inside(x, y, kResetX, kButtonY, kButtonW, kButtonH)) reset();
    else if (inside(x, y, kDoneX, kButtonY, kButtonW, kButtonH)) closed_ = true;
}

void ColoursScreen::mouse_move(int x, int y) {
    if (drag_) pick(x, y);
}

// @orig none: port-only player colours
int ColoursScreen::direction() const { return static_cast<int>((now_ / 40) % 8) * 2; }

// @orig none: port-only player colours
ColoursOverlay ColoursScreen::overlay() const {
    ColoursOverlay o;
    o.on = true;
    const std::uint32_t kPanel = rgba(10, 14, 22, 240), kEdge = rgba(80, 110, 160, 255);
    o.rects.push_back(box(0, 0, 640, 480, rgba(0, 0, 0, 255)));
    o.rects.push_back(box(kPanelX, kPanelY, kPanelW, kPanelH, kPanel));
    o.rects.push_back(box(kPanelX, kPanelY, kPanelW, kPanelH, kEdge, true));
    auto label = [&](float x, float y, const std::string& s, int r, int g, int b, int align = 0) {
        ColourLabel l;
        l.x = x, l.y = y, l.text = s;
        l.r = static_cast<std::uint8_t>(r), l.g = static_cast<std::uint8_t>(g), l.b = static_cast<std::uint8_t>(b);
        l.align = align;
        o.labels.push_back(l);
    };
    label(320, kPanelY + 34, "CHARACTER COLOURS", 150, 200, 255, 1);

    // The character's picture, in a frame.
    o.rects.push_back(box(kPicX, kPicY, kPicW, kPicH, rgba(26, 32, 44, 255)));
    o.rects.push_back(box(kPicX, kPicY, kPicW, kPicH, kEdge, true));
    label(kPicX + kPicW / 2, kPicY + kPicH + 20, "PREVIEW", 150, 165, 190, 1);
    o.ix = kPicX + kPicW / 2, o.iy = kPicY + kPicH - 24;  // the picture's anchor (the feet) goes here
    o.iscale = 2.4f;

    // The three features: a swatch, the name and the colour's number.
    for (int f = 0; f < kFeatureCount; ++f) {
        const float y = kTabY + kTabStep * static_cast<float>(f);
        const bool on = f == feature_;
        const std::uint32_t c = config_->player_colours.rgb[static_cast<std::size_t>(f)];
        o.rects.push_back(box(kTabX, y, kTabW, kTabH, on ? rgba(46, 96, 170, 255) : rgba(24, 32, 46, 255)));
        o.rects.push_back(box(kTabX, y, kTabW, kTabH, on ? rgba(150, 200, 255, 255) : kEdge, true));
        o.rects.push_back(box(kTabX + 5, y + 7, 22, 22, from_rgb24(c)));
        o.rects.push_back(box(kTabX + 5, y + 7, 22, 22, rgba(0, 0, 0, 255), true));
        label(kTabX + 36, y + 16, kFeatureNames[f], on ? 255 : 205, on ? 255 : 215, on ? 255 : 235);
        label(kTabX + 36, y + 30, kFeatureHints[f], 140, 155, 180);
        char hex[8];
        std::snprintf(hex, sizeof hex, "#%06X", static_cast<unsigned>(c));
        label(kTabX + kTabW - 8, y + 22, hex, 200, 215, 240, 2);
    }

    // The colour square (saturation across, brightness down) and the hue bar.
    ColourRect sv;
    sv.x = kSvX, sv.y = kSvY, sv.w = kSvW, sv.h = kSvH;
    const std::uint32_t hue = from_rgb24(to_rgb(Hsv{hsv_.h, 1, 1}));
    sv.corner = {rgba(255, 255, 255), hue, rgba(0, 0, 0), rgba(0, 0, 0)};
    o.rects.push_back(sv);
    o.rects.push_back(box(kSvX, kSvY, kSvW, kSvH, kEdge, true));
    const float mx = kSvX + hsv_.s * (kSvW - 1), my = kSvY + (1.0f - hsv_.v) * (kSvH - 1);
    o.rects.push_back(box(mx - 5, my - 5, 10, 10, rgba(0, 0, 0, 255), true));
    o.rects.push_back(box(mx - 4, my - 4, 8, 8, rgba(255, 255, 255, 255), true));
    for (int i = 0; i < 6; ++i) {
        ColourRect seg;
        seg.x = kHueX, seg.w = kHueW, seg.y = kHueY + kHueH * static_cast<float>(i) / 6.0f, seg.h = kHueH / 6.0f;
        const std::uint32_t a = from_rgb24(to_rgb(Hsv{60.0f * static_cast<float>(i), 1, 1}));
        const std::uint32_t b = from_rgb24(to_rgb(Hsv{60.0f * static_cast<float>(i + 1), 1, 1}));
        seg.corner = {a, a, b, b};
        o.rects.push_back(seg);
    }
    o.rects.push_back(box(kHueX, kHueY, kHueW, kHueH, kEdge, true));
    const float hy = kHueY + hsv_.h / 359.0f * (kHueH - 1);
    o.rects.push_back(box(kHueX - 3, hy - 2, kHueW + 6, 5, rgba(0, 0, 0, 255), true));
    o.rects.push_back(box(kHueX - 2, hy - 1, kHueW + 4, 3, rgba(255, 255, 255, 255), true));

    // The chosen colour, large, with its numbers.
    const std::uint32_t current = config_->player_colours.rgb[static_cast<std::size_t>(feature_)];
    o.rects.push_back(box(508, 202, 68, 50, from_rgb24(current)));
    o.rects.push_back(box(508, 202, 68, 50, kEdge, true));
    char text[24];
    std::snprintf(text, sizeof text, "#%06X", static_cast<unsigned>(current));
    label(542, 270, text, 220, 232, 250, 1);
    std::snprintf(text, sizeof text, "R %d", static_cast<int>((current >> 16) & 0xFF));
    label(512, 292, text, 255, 140, 140);
    std::snprintf(text, sizeof text, "G %d", static_cast<int>((current >> 8) & 0xFF));
    label(512, 308, text, 140, 255, 150);
    std::snprintf(text, sizeof text, "B %d", static_cast<int>(current & 0xFF));
    label(512, 324, text, 150, 180, 255);

    // Quick colours.
    label(kQuickX, kQuickY - 6, "QUICK COLOURS FOR THE CHOSEN FEATURE", 150, 165, 190);
    for (int i = 0; i < kQuickCount; ++i) {
        const float x = kQuickX + (kQuickSize + kQuickGap) * static_cast<float>(i);
        o.rects.push_back(box(x, kQuickY, kQuickSize, kQuickSize, from_rgb24(kQuick[i])));
        o.rects.push_back(box(x, kQuickY, kQuickSize, kQuickSize, rgba(0, 0, 0, 255), true));
    }

    // Presets: three chips of their colours and the name.
    int count = 0;
    const PlayerPreset* presets = player_presets(count);
    for (int i = 0; i < count; ++i) {
        const float x = kPresetX + (kPresetW + kPresetGap) * static_cast<float>(i % 4);
        const float y = kPresetY + (kPresetH + kPresetGap) * static_cast<float>(i / 4);
        const bool current_preset = config_->player_colours.rgb == presets[i].rgb && (config_->player_colours.custom || i == 0);
        o.rects.push_back(box(x, y, kPresetW, kPresetH, current_preset ? rgba(46, 96, 170, 255) : rgba(24, 32, 46, 255)));
        o.rects.push_back(box(x, y, kPresetW, kPresetH, current_preset ? rgba(150, 200, 255, 255) : kEdge, true));
        for (int k = 0; k < kFeatureCount; ++k) {
            o.rects.push_back(box(x + 4 + 7.0f * static_cast<float>(k), y + 4, 6, 6, from_rgb24(presets[i].rgb[static_cast<std::size_t>(k)])));
        }
        label(x + kPresetW / 2, y + 20, presets[i].name, 215, 225, 245, 1);
    }

    // Buttons.
    for (int b = 0; b < 2; ++b) {
        const float x = b == 0 ? kResetX : kDoneX;
        o.rects.push_back(box(x, kButtonY, kButtonW, kButtonH, rgba(34, 44, 62, 255)));
        o.rects.push_back(box(x, kButtonY, kButtonW, kButtonH, kEdge, true));
        label(x + kButtonW / 2, kButtonY + 16, b == 0 ? "RESET" : "DONE", 235, 240, 255, 1);
    }
    return o;
}

}  // namespace crusader
