#pragma once
// The character colour screen (port addition), opened by the options screen's "couleurs" button. It lets the player pick
// the three colours of the main character (the reds of the armour, the gold of the chest logo, the browns of the backpack)
// with a colour square and a hue bar, from a row of quick colours, or as a whole from presets, and shows the character
// turning round in them. The colours are written to GameConfig::player_colours (CRUSADER.CFG "playercolors") as they change.
//
// Everything is drawn on the graphics card: draw() only clears the screen, overlay() describes the panels, swatches and
// text for the presenter (640 x 480 screen coordinates), and the game adds the picture of the character.
//
// Keys: up / down change feature, Enter and Escape close. Mouse: pick a feature, click or drag in the square and the hue bar,
// click a quick colour or a preset, "reset" restores the game's own colours, "done" closes.

#include "core/game_config.hpp"
#include "core/surface.hpp"
#include "engine/world/colours_overlay.hpp"

#include <array>
#include <cstdint>

namespace crusader {

class ColoursScreen {
public:
    explicit ColoursScreen(GameConfig& config);

    void draw(Surface8& screen) const { screen.clear(0); }
    void key(std::uint16_t code);
    void mouse_down(int x, int y);
    void mouse_move(int x, int y);
    void mouse_up(int, int) { drag_ = 0; }
    void update(std::uint32_t ticks) { now_ = ticks; }

    bool closed() const { return closed_; }
    // The panels, swatches and text (without the picture of the character).
    ColoursOverlay overlay() const;
    // The direction the character is shown facing (0..15).
    int direction() const;

private:
    struct Hsv {
        float h = 0, s = 1, v = 1;
    };
    static std::uint32_t to_rgb(const Hsv& c);
    static Hsv from_rgb(std::uint32_t rgb);
    void set_colour(int feature, std::uint32_t rgb, bool keep_hsv);
    void set_from_hsv();
    void apply_preset(int preset);
    void reset();
    void pick(int x, int y);
    void sync_hsv();

    GameConfig* config_;
    int feature_ = 0;
    Hsv hsv_{};
    int drag_ = 0;  // 1 colour square, 2 hue bar
    bool closed_ = false;
    std::uint32_t now_ = 0;
};

}  // namespace crusader
