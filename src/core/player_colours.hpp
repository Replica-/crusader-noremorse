#pragma once
// Port addition: the colours of the main character, chosen in the options (GameConfig::player_colours). The Silencer's
// sprites are drawn with three ramps of the game's palette: the reds of the armour, the gold of the chest logo and the
// browns of the backpack. A custom colour replaces a whole ramp, keeping its light and shade.

#include "core/palette.hpp"

#include <array>
#include <cstdint>

namespace crusader {

enum PlayerFeature : int { kFeatureMain = 0, kFeatureLogo = 1, kFeaturePack = 2, kFeatureCount = 3 };

struct PlayerColours {
    // 0xRRGGBB for the main colour (the reds), the chest logo (the golds) and the backpack (the browns).
    std::array<std::uint32_t, kFeatureCount> rgb{0x8A0404, 0xFF9E20, 0x594928};
    bool custom = false;  // false: the game's own colours, whatever rgb says

    static PlayerColours original() { return {}; }
    bool operator==(const PlayerColours& o) const { return rgb == o.rgb && custom == o.custom; }
};

// The feature a palette colour belongs to, judged from the colour itself (-1 for the blackish and the greys, which never change).
int player_feature_of(const Rgb& colour);

// For every palette index, the colour that replaces it for the player (bytes R, G, B, A in memory, A = 0 where nothing
// changes), from the unfaded `base` palette.
std::array<std::uint32_t, 256> player_recolour(const Palette& base, const PlayerColours& colours);

struct PlayerPreset {
    const char* name;
    std::array<std::uint32_t, kFeatureCount> rgb;
};
// The ready-made looks, the game's own first.
const PlayerPreset* player_presets(int& count);

}  // namespace crusader
