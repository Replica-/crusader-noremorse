#pragma once
// Presents an 8-bit framebuffer through SDL. Replaces the original VESA mode
// setup (SUPERVGA), banked VRAM blits (VPORT asm blitters) and DAC writes
// (palette_write_hw): the game draws into a Surface8, this converts it with the
// current palette and scales it to the window.

#include "core/font_atlas.hpp"
#include "core/palette.hpp"
#include "core/surface.hpp"
#include "engine/world/colours_overlay.hpp"
#include "engine/world/jump_overlay.hpp"
#include "engine/world/map_overlay.hpp"
#include "engine/world/wheel_overlay.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace crusader {

class SdlPresenter {
public:
    SdlPresenter(const std::string& title, int width, int height, int scale, bool fullscreen = false);
    ~SdlPresenter();
    SdlPresenter(const SdlPresenter&) = delete;
    SdlPresenter& operator=(const SdlPresenter&) = delete;

    void set_title(const std::string& title);
    // Brightens (> 1) or darkens (< 1) the picture; 1 is the original palette.
    void set_gamma(double gamma);
    void present(const Surface8& frame, const Palette& palette);
    void maximize();
    // "opengl", "direct3d11", "software"... and whether it is the software renderer.
    std::string renderer_name() const;
    bool software_renderer() const;
    // The laser sight drawn over the next frames (view pixels), cleared with on = false.
    void set_laser(bool on, bool hit, float x0, float y0, float x1, float y1, float spread_x = 0, float spread_y = 0);
    // The click-to-walk debug path (view pixels, goal last), empty to hide it.
    void set_debug_path(const std::vector<std::pair<float, float>>& points) { path_ = points; }
    // Health bars (centre x, y, fraction left 0..1, opacity 0..1), drawn over the frame.
    struct HitBar {
        float x, y, fraction, alpha;
    };
    void set_hit_bars(std::vector<HitBar> bars) { hit_bars_ = std::move(bars); }
    // The mouse arrow, drawn on the graphics card (not from the game's palette), with a small green / red dot
    // beside it when it is over something that can be used (hover 1 in reach, 2 too far).
    // With `ring` above 0 it is the target cursor of a drawn weapon instead: a ring, a dot and four short lines, `ring` pixels across.
    void set_pointer(bool show, float x, float y, int hover, float ring = 0) { pointer_ = {show, x, y, hover, ring}; }
    // Blood (view pixels): a streak while it flies (kind 0), a puff of mist (1), a flat splat on the floor (2).
    struct BloodDot {
        float x, y, x0, y0, size, alpha;
        int kind;
    };
    void set_blood(std::vector<BloodDot> dots) { blood_ = std::move(dots); }
    // Text drawn by the graphics card over everything, in any colour, with the game's font (`y` is the baseline).
    // `text()` queues it for the next present().
    // Saves what the next present() draws (the whole window, real pixels) as a BMP, for the README's pictures; done() once saved.
    void request_screenshot(const std::string& path) { shot_path_ = path; shot_done_ = false; }
    bool screenshot_done() const { return shot_done_; }
    void set_font(const FontAtlas& atlas);
    void text(float x, float y, const std::string& s, std::uint8_t r, std::uint8_t g, std::uint8_t b, bool right_aligned = false,
              float scale = 1.0f);
    float text_width(const std::string& s, float scale = 1.0f) const;
    // The mission map over everything (null or off hides it).
    void set_map(const MapOverlay* map) { map_ = map; }
    // The weapon / item wheel over the game (null or off hides it).
    void set_wheel(const WheelOverlay* wheel) { wheel_ = wheel; }
    // The pictures of the wheel entries (each handed over once, by id).
    void add_wheel_icons(std::vector<WheelIcon> icons);
    // The targeted jump's preview (a dashed arc and a ghost of the player), null or off hides it.
    void set_jump(const JumpPreview* jump) { jump_ = jump; }
    // The character colour screen over everything, and the player drawn again in his chosen colours (null hides them).
    void set_colours(const ColoursOverlay* colours) { colours_ = colours; }
    void set_player_layer(const PlayerLayer* player) { player_ = player; }
    // Desktop fullscreen on / off (Alt+Enter).
    void toggle_fullscreen();
    // Size of the window's drawing area in real pixels.
    void output_size(int* w, int* h) const;
    // Changes the size of the frames given to present (the window is unchanged).
    void resize(int width, int height);
    // Puts the system pointer on a point of the game screen (mouse_warp).
    void warp_mouse(int x, int y);

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    std::array<std::uint8_t, 256> gamma_table_;
    int width_;
    int height_;
    struct Laser {
        bool on = false, hit = false;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0, spread_x = 0, spread_y = 0;
    } laser_;
    void draw_laser();
    void draw_debug_path();
    std::vector<std::pair<float, float>> path_;
    std::vector<HitBar> hit_bars_;
    void draw_hit_bars();
    void draw_pointer();
    void draw_blood();
    const MapOverlay* map_ = nullptr;
    void draw_map();
    void draw_wheel();
    const WheelOverlay* wheel_ = nullptr;
    void draw_jump();
    void draw_colours();
    void draw_player_layer(const Palette& current);
    const ColoursOverlay* colours_ = nullptr;
    const PlayerLayer* player_ = nullptr;
    SDL_Texture* colours_tex_ = nullptr;
    std::uint32_t colours_key_ = 0;
    SDL_Texture* player_tex_ = nullptr;
    int player_w_ = 0, player_h_ = 0;
    const JumpPreview* jump_ = nullptr;
    SDL_Texture* jump_tex_ = nullptr;
    std::uint32_t jump_key_ = 0;
    struct IconTexture {
        SDL_Texture* texture = nullptr;
        int width = 0, height = 0;
    };
    std::vector<IconTexture> wheel_icons_;
    void draw_texts();
    std::string shot_path_;
    bool shot_done_ = false;
    struct TextItem {
        float x, y, scale;
        std::string s;
        std::uint8_t r, g, b;
    };
    std::vector<TextItem> texts_;
    FontAtlas atlas_;
    SDL_Texture* font_tex_ = nullptr;
    std::vector<BloodDot> blood_;
    struct Pointer {
        bool show = false;
        float x = 0, y = 0;
        int hover = 0;
        float ring = 0;
    } pointer_;
};

}  // namespace crusader
