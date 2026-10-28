#pragma once
// Presents an 8-bit framebuffer through SDL. Replaces the original VESA mode
// setup (SUPERVGA), banked VRAM blits (VPORT asm blitters) and DAC writes
// (palette_write_hw): the game draws into a Surface8, this converts it with the
// current palette and scales it to the window.

#include "core/palette.hpp"
#include "core/surface.hpp"

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
    void set_pointer(bool show, float x, float y, int hover) { pointer_ = {show, x, y, hover}; }
    // Blood (view pixels): a streak while it flies (kind 0), a puff of mist (1), a flat splat on the floor (2).
    struct BloodDot {
        float x, y, x0, y0, size, alpha;
        int kind;
    };
    void set_blood(std::vector<BloodDot> dots) { blood_ = std::move(dots); }
    // The first person inset: a square of `size` view pixels at (x, y), with one column per entry (height as a fraction of
    // the square, 0 for an empty column, and its colour); an empty list hides it.
    struct FpvColumn {
        float height;
        std::uint8_t r, g, b;
    };
    void set_fpv(float x, float y, float size, std::vector<FpvColumn> columns) {
        fpv_x_ = x, fpv_y_ = y, fpv_size_ = size;
        fpv_ = std::move(columns);
    }
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
    void draw_fpv();
    std::vector<FpvColumn> fpv_;
    float fpv_x_ = 0, fpv_y_ = 0, fpv_size_ = 0;
    std::vector<BloodDot> blood_;
    struct Pointer {
        bool show = false;
        float x = 0, y = 0;
        int hover = 0;
    } pointer_;
};

}  // namespace crusader
