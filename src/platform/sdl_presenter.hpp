#pragma once
// Presents an 8-bit framebuffer through SDL. Replaces the original VESA mode
// setup (SUPERVGA), banked VRAM blits (VPORT asm blitters) and DAC writes
// (palette_write_hw): the game draws into a Surface8, this converts it with the
// current palette and scales it to the window.

#include "core/palette.hpp"
#include "core/surface.hpp"

#include <string>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace crusader {

class SdlPresenter {
public:
    SdlPresenter(const std::string& title, int width, int height, int scale);
    ~SdlPresenter();
    SdlPresenter(const SdlPresenter&) = delete;
    SdlPresenter& operator=(const SdlPresenter&) = delete;

    void set_title(const std::string& title);
    void present(const Surface8& frame, const Palette& palette);
    // Puts the system pointer on a point of the game screen (mouse_warp).
    void warp_mouse(int x, int y);

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    int width_;
    int height_;
};

}  // namespace crusader
