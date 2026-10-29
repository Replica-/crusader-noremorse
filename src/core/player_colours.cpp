#include "core/player_colours.hpp"

#include <algorithm>
#include <cmath>

namespace crusader {

namespace {

// The brightest channel of the colour that takes a chosen colour as it is, for each feature.
constexpr double kReference[kFeatureCount] = {138.0, 255.0, 89.0};

}  // namespace

// Which feature a palette colour belongs to, from its colour alone: the blackish and the greys stay as they are; reds, pinks
// and rust are the main colour; bright oranges and golds the logo; the duller browns, olives and tans the backpack; the
// cold colours (greens, blues, purples) are accents of the armour and go with the main colour.
// @orig none: port-only player colours
int player_feature_of(const Rgb& c) {
    const double r = c.r, g = c.g, b = c.b;
    const double hi = std::max({r, g, b}), lo = std::min({r, g, b});
    const double v = hi / 255.0, s = hi <= 0 ? 0.0 : (hi - lo) / hi;
    if (v < 0.16 || s < 0.2) return -1;  // blackish or grey: unchanged
    const double d = hi - lo;
    double h = 0;
    if (hi == r) h = 60.0 * std::fmod((g - b) / d, 6.0);
    else if (hi == g) h = 60.0 * ((b - r) / d + 2.0);
    else h = 60.0 * ((r - g) / d + 4.0);
    if (h < 0) h += 360.0;
    if (h >= 335.0 || h < 22.0) return kFeatureMain;
    if (h < 70.0) return (v > 0.6 && s > 0.55) ? kFeatureLogo : kFeaturePack;
    return kFeatureMain;
}

// @orig none: port-only player colours
std::array<std::uint32_t, 256> player_recolour(const Palette& base, const PlayerColours& colours) {
    std::array<std::uint32_t, 256> out{};
    if (!colours.custom) return out;
    for (std::size_t i = 0; i < 256; ++i) {
        const Rgb& c = base.colors[i];
        const int f = player_feature_of(c);
        if (f < 0) continue;
        // How bright this shade is against the one that takes the chosen colour as it is.
        const double s = std::max({c.r, c.g, c.b}) / kReference[f];
        const std::uint32_t t = colours.rgb[static_cast<std::size_t>(f)];
        const double tr = (t >> 16) & 0xFF, tg = (t >> 8) & 0xFF, tb = t & 0xFF;
        auto shade = [&](double v) {
            // Darker than the reference: scaled down; lighter: towards white.
            const double out_v = s <= 1.0 ? v * s : v + (255.0 - v) * std::min(1.0, (s - 1.0) * 0.6);
            return static_cast<std::uint32_t>(std::clamp(out_v, 0.0, 255.0) + 0.5);
        };
        out[i] = 0xFF000000u | shade(tb) << 16 | shade(tg) << 8 | shade(tr);
    }
    return out;
}

// @orig none: port-only player colours
const PlayerPreset* player_presets(int& count) {
    static const PlayerPreset presets[] = {
        {"ORIGINAL", {0x8A0404, 0xFF9E20, 0x594928}},
        {"STEALTH", {0x2C2F36, 0x9AA0AA, 0x1E2024}},
        {"DESERT", {0xC2A25A, 0x6B4A18, 0x7A5A32}},
        {"ARCTIC", {0xE6EDF5, 0x3C86D6, 0x8A9BB0}},
        {"TOXIC", {0x37C837, 0xF0F028, 0x2E4A22}},
        {"ROYAL", {0x3A3AD0, 0xFFD23C, 0x2A1E5A}},
        {"NIGHT", {0x16204A, 0x5ADCF0, 0x1A1A2A}},
        {"SUNSET", {0xE8601C, 0xFFE08A, 0x6B2A18}},
        {"ORCHID", {0xC83C9C, 0xF0E6FF, 0x4A2A5A}},
        {"GOLD", {0xD4AF37, 0x20202A, 0x5C4A1A}},
        {"OCEAN", {0x0E8C8C, 0xF4F0D0, 0x1C3A4A}},
        {"BONE", {0xDCD2B8, 0x8A0404, 0x6A5A44}},
    };
    count = static_cast<int>(sizeof(presets) / sizeof(presets[0]));
    return presets;
}

}  // namespace crusader
