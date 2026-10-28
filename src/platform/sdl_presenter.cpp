#include "platform/sdl_presenter.hpp"

#include <SDL.h>

#include <array>
#include <cmath>
#include <stdexcept>

namespace crusader {

SdlPresenter::SdlPresenter(const std::string& title, int width, int height, int scale, bool fullscreen)
    : width_(width), height_(height) {
    set_gamma(1.0);
    // Without this, Windows scales a window by the display's zoom (125 %, 150 %...)
    // and SDL reports a smaller size than the screen has, so the view would
    // be computed for fewer pixels than are shown.
    // Literal name: the SDL_HINT_WINDOWS_DPI_AWARENESS macro only exists in SDL >= 2.24
    // (older SDL, e.g. on CI, just ignores the unknown hint).
    SDL_SetHint("SDL_WINDOWS_DPI_AWARENESS", "permonitorv2");
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        throw std::runtime_error(std::string("SDL video: ") + SDL_GetError());
    }
    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               width * scale, height * scale,
                               SDL_WINDOW_RESIZABLE | (fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    if (!window_) {
        throw std::runtime_error(std::string("SDL window: ") + SDL_GetError());
    }
    // OpenGL by default (the SDL renderer's "opengl" driver); SDL picks its own
    // backend, e.g. Direct3D, if OpenGL is not available.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_PRESENTVSYNC);
    }
    if (!renderer_) {
        throw std::runtime_error(std::string("SDL renderer: ") + SDL_GetError());
    }
    SDL_RenderSetLogicalSize(renderer_, width, height);
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                 width, height);
    if (!texture_) {
        throw std::runtime_error(std::string("SDL texture: ") + SDL_GetError());
    }
}

SdlPresenter::~SdlPresenter() {
    if (texture_) SDL_DestroyTexture(texture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void SdlPresenter::toggle_fullscreen() {
    const bool on = SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN_DESKTOP;
    SDL_SetWindowFullscreen(window_, on ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
}

void SdlPresenter::maximize() { SDL_MaximizeWindow(window_); }

void SdlPresenter::output_size(int* w, int* h) const {
    SDL_GetRendererOutputSize(renderer_, w, h);
}

void SdlPresenter::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    SDL_Texture* texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                             width, height);
    if (!texture) {
        throw std::runtime_error(std::string("SDL texture: ") + SDL_GetError());
    }
    SDL_DestroyTexture(texture_);
    texture_ = texture;
    width_ = width;
    height_ = height;
    SDL_RenderSetLogicalSize(renderer_, width, height);
}

// @orig 1360:0FD6
void SdlPresenter::warp_mouse(int x, int y) {
    int wx = 0, wy = 0;
    SDL_RenderLogicalToWindow(renderer_, static_cast<float>(x), static_cast<float>(y), &wx, &wy);
    SDL_WarpMouseInWindow(window_, wx, wy);
}

void SdlPresenter::set_gamma(double gamma) {
    if (!(gamma > 0)) gamma = 1.0;
    for (std::size_t i = 0; i < gamma_table_.size(); ++i) {
        gamma_table_[i] = static_cast<std::uint8_t>(std::lround(255.0 * std::pow(i / 255.0, 1.0 / gamma)));
    }
}

void SdlPresenter::set_title(const std::string& title) {
    SDL_SetWindowTitle(window_, title.c_str());
}

void SdlPresenter::present(const Surface8& frame, const Palette& palette) {
    std::array<Uint32, 256> lut;
    for (std::size_t i = 0; i < lut.size(); ++i) {
        const Rgb& c = palette.colors[i];
        lut[i] = 0xFF000000u | Uint32{gamma_table_[c.r]} << 16 | Uint32{gamma_table_[c.g]} << 8 | gamma_table_[c.b];
    }

    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(texture_, nullptr, &pixels, &pitch) != 0) {
        return;
    }
    for (int y = 0; y < height_ && y < frame.height; ++y) {
        auto* dst = reinterpret_cast<Uint32*>(static_cast<Uint8*>(pixels) + y * pitch);
        const std::uint8_t* src = frame.row(y);
        for (int x = 0; x < width_ && x < frame.width; ++x) {
            dst[x] = lut[src[x]];
        }
    }
    SDL_UnlockTexture(texture_);

    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    draw_laser();
    draw_debug_path();
    draw_hit_bars();
    SDL_RenderPresent(renderer_);
}

}  // namespace crusader

namespace crusader {

std::string SdlPresenter::renderer_name() const {
    SDL_RendererInfo info;
    return SDL_GetRendererInfo(renderer_, &info) == 0 ? info.name : "?";
}

bool SdlPresenter::software_renderer() const {
    SDL_RendererInfo info;
    return SDL_GetRendererInfo(renderer_, &info) != 0 || (info.flags & SDL_RENDERER_SOFTWARE);
}

}  // namespace crusader

#include <algorithm>

namespace crusader {

void SdlPresenter::set_laser(bool on, bool hit, float x0, float y0, float x1, float y1, float spread_x, float spread_y) {
    laser_ = {on, hit, x0, y0, x1, y1, spread_x, spread_y};
}

// The laser pointer: a red dot where the beam would end (stopped by what is in the way) inside a
// crosshair whose four lines open up with the shot's spread, drawn as additive OpenGL geometry.
void SdlPresenter::draw_laser() {
    if (!laser_.on) return;
    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const float pulse = 0.82f + 0.18f * std::sin(t * 21.0f) + 0.05f * std::sin(t * 57.0f);

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_ADD);
    auto colour = [](int r, int g, int b, float a) {
        return SDL_Color{static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b),
                         static_cast<Uint8>(std::clamp(a, 0.0f, 255.0f))};
    };

    // A soft round flare of radius r at (x, y).
    auto flare = [&](float x, float y, float r, SDL_Color c) {
        constexpr int kSegments = 20;
        SDL_Vertex v[kSegments + 1];
        int idx[kSegments * 3];
        SDL_Color edge = c;
        edge.a = 0;
        v[0] = {{x, y}, c, {0, 0}};
        for (int i = 0; i < kSegments; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / kSegments;
            v[i + 1] = {{x + std::cos(a) * r, y + std::sin(a) * r}, edge, {0, 0}};
            idx[i * 3] = 0;
            idx[i * 3 + 1] = i + 1;
            idx[i * 3 + 2] = (i + 1) % kSegments + 1;
        }
        SDL_RenderGeometry(renderer_, nullptr, v, kSegments + 1, idx, kSegments * 3);
    };
    // The crosshair: a dot in the centre and four short lines (up, down, left, right) that move
    // away from it as the shot gets less accurate, so it shows where the bullets can land.
    {
        const float gap = 3.0f + laser_.spread_x * 0.7f, length = 7.0f;
        const float cx = laser_.x1, cy = laser_.y1;
        auto line = [&](float x0, float y0, float x1, float y1) {
            // A soft line: a faint wide copy under a brighter thin one.
            const bool horizontal = y0 == y1;
            for (int k = -1; k <= 1; ++k) {
                SDL_SetRenderDrawColor(renderer_, 255, k == 0 ? 120 : 50, k == 0 ? 100 : 40, k == 0 ? 235 : 70);
                const float dx = horizontal ? 0.0f : static_cast<float>(k), dy = horizontal ? static_cast<float>(k) : 0.0f;
                SDL_RenderDrawLineF(renderer_, x0 + dx, y0 + dy, x1 + dx, y1 + dy);
            }
        };
        line(cx - gap - length, cy, cx - gap, cy);
        line(cx + gap, cy, cx + gap + length, cy);
        line(cx, cy - gap - length, cx, cy - gap);
        line(cx, cy + gap, cx, cy + gap + length);
    }
    flare(laser_.x1, laser_.y1, 6.0f * pulse, colour(255, 30, 20, 80));    // halo
    flare(laser_.x1, laser_.y1, 3.6f * pulse, colour(255, 60, 40, 230));   // the dot
    flare(laser_.x1, laser_.y1, 1.6f, colour(255, 235, 225, 255));         // its hot centre
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

}  // namespace crusader

namespace crusader {

// The planned click-to-walk path: a cyan line through its nodes, a dot on each, and a
// magenta cross on the goal.
void SdlPresenter::draw_debug_path() {
    if (path_.size() < 2) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 0, 255, 255, 230);
    for (std::size_t i = 1; i < path_.size(); ++i) {
        SDL_RenderDrawLineF(renderer_, path_[i - 1].first, path_[i - 1].second, path_[i].first, path_[i].second);
        SDL_RenderDrawLineF(renderer_, path_[i - 1].first, path_[i - 1].second + 1, path_[i].first, path_[i].second + 1);
    }
    for (std::size_t i = 1; i + 1 < path_.size(); ++i) {
        const SDL_FRect dot{path_[i].first - 1.5f, path_[i].second - 1.5f, 3.0f, 3.0f};
        SDL_RenderFillRectF(renderer_, &dot);
    }
    const auto [gx, gy] = path_.back();
    SDL_SetRenderDrawColor(renderer_, 255, 0, 255, 255);
    SDL_RenderDrawLineF(renderer_, gx - 6, gy - 6, gx + 6, gy + 6);
    SDL_RenderDrawLineF(renderer_, gx - 6, gy + 6, gx + 6, gy - 6);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

}  // namespace crusader

namespace crusader {

// Slim health bars: a faint dark track with a fill from green to red.
void SdlPresenter::draw_hit_bars() {
    if (hit_bars_.empty()) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    constexpr float kWidth = 22.0f, kHeight = 2.0f;
    for (const HitBar& b : hit_bars_) {
        const float x = b.x - kWidth / 2, y = b.y;
        const SDL_FRect track{x - 1, y - 1, kWidth + 2, kHeight + 2};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, static_cast<Uint8>(120 * b.alpha));
        SDL_RenderFillRectF(renderer_, &track);
        const float f = b.fraction;
        const Uint8 red = static_cast<Uint8>(std::clamp(2.0f * (1.0f - f), 0.0f, 1.0f) * 225), green = static_cast<Uint8>(std::clamp(2.0f * f, 0.0f, 1.0f) * 200 + 25);
        SDL_SetRenderDrawColor(renderer_, red, green, 60, static_cast<Uint8>(215 * b.alpha));
        const SDL_FRect fill{x, y, kWidth * f, kHeight};
        SDL_RenderFillRectF(renderer_, &fill);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

}  // namespace crusader
