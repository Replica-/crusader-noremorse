#include "platform/sdl_presenter.hpp"

#include "core/png_reader.hpp"
#include "core/png_writer.hpp"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <tuple>

namespace crusader {

SdlPresenter::SdlPresenter(const std::string& title, int width, int height, int scale, bool fullscreen, bool headless)
    : headless_(headless), width_(width), height_(height) {
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
                               headless ? Uint32{SDL_WINDOW_HIDDEN}
                                        : SDL_WINDOW_RESIZABLE | (fullscreen ? Uint32{SDL_WINDOW_FULLSCREEN_DESKTOP} : 0u));
    if (!window_) {
        throw std::runtime_error(std::string("SDL window: ") + SDL_GetError());
    }
    // OpenGL by default (the SDL renderer's "opengl" driver); SDL picks its own
    // backend, e.g. Direct3D, if OpenGL is not available.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    const Uint32 flags = headless ? 0u : Uint32{SDL_RENDERER_PRESENTVSYNC};  // hidden: nothing to wait for
    renderer_ = SDL_CreateRenderer(window_, -1, flags);
    if (!renderer_) {
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "");
        renderer_ = SDL_CreateRenderer(window_, -1, flags);
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
    make_screen_target();
}

SdlPresenter::~SdlPresenter() {
    clear_scene_textures();
    clear_upscaled_textures();
    for (auto& entry : angle_textures_) {
        if (entry.second) SDL_DestroyTexture(entry.second);
    }
    if (font_tex_) SDL_DestroyTexture(font_tex_);
    if (jump_tex_) SDL_DestroyTexture(jump_tex_);
    if (colours_tex_) SDL_DestroyTexture(colours_tex_);
    if (player_tex_) SDL_DestroyTexture(player_tex_);
    for (SpriteTexture& t : sprite_tex_) {
        if (t.texture) SDL_DestroyTexture(t.texture);
    }
    if (occ_tex_) SDL_DestroyTexture(occ_tex_);
    for (IconTexture& i : wheel_icons_) {
        if (i.texture) SDL_DestroyTexture(i.texture);
    }
    for (SDL_Texture* t : {scene_target_, light_target_, light_spot_, screen_target_, room_small_[0], room_small_[1], fx_blob_}) {
        if (t) SDL_DestroyTexture(t);
    }
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
    make_screen_target();
}

// @orig 1360:0FD6
void SdlPresenter::warp_mouse(int x, int y) {
    if (headless_) return;  // the system pointer is someone else's
    int wx = 0, wy = 0;
    SDL_RenderLogicalToWindow(renderer_, static_cast<float>(x), static_cast<float>(y), &wx, &wy);
    SDL_WarpMouseInWindow(window_, wx, wy);
}

void SdlPresenter::set_gamma(double gamma) {
    ++gamma_version_;
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
    // With a hardware scene, the key colour of the frame is clear, so the scene shows through.
    if (scene_) lut[kSceneKey] = 0;
    SDL_SetTextureBlendMode(texture_, scene_ ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);

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

    use_screen_target();
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    if (scene_) {
        const bool lit = lighting_ && begin_lit_scene();
        draw_scene(palette);
        if (lit) end_lit_scene();
    }
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    draw_player_layer(palette);
    draw_player_sprite(palette);
    draw_jump();
    draw_laser();
    draw_debug_path();
    draw_walk_target();
    draw_light_debug();
    draw_blood();
    draw_hit_bars();
    draw_map();
    draw_wheel();
    draw_colours();
    draw_pointer();  // over the map too
    draw_texts();
    if (!shot_path_.empty()) {
        int w = 0, h = 0;
        if (screen_target_) SDL_QueryTexture(screen_target_, nullptr, nullptr, &w, &h);
        else SDL_GetRendererOutputSize(renderer_, &w, &h);
        if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888)) {
            if (SDL_RenderReadPixels(renderer_, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
                // A .png (the F12 screenshots) is written as one, anything else as a BMP (--shot).
                if (std::filesystem::path(shot_path_).extension() == ".png") {
                    try {
                        write_png_rgb(shot_path_, w, h, static_cast<const std::uint32_t*>(s->pixels), s->pitch);
                    } catch (const std::exception&) {
                    }
                } else {
                    SDL_SaveBMP(s, shot_path_.c_str());
                }
            }
            SDL_FreeSurface(s);
        }
        shot_path_.clear();
        shot_done_ = true;
    }
    if (screen_target_) {
        SDL_SetRenderTarget(renderer_, nullptr);  // the hidden window shows nothing: no copy to it
        return;
    }
    SDL_RenderPresent(renderer_);
}

// Headless: the picture everything is drawn into, of the frame's size (made again when it changes); with the upscaled
// sprites, of the size the frame takes in the hidden window, as a shown one would draw it.
// @orig none: port-only headless mode
void SdlPresenter::make_screen_target() {
    if (!headless_) return;
    if (screen_target_) SDL_DestroyTexture(screen_target_);
    screen_scale_ = upscaled_ ? window_scale() : 1.0f;
    const int w = static_cast<int>(std::lround(width_ * screen_scale_)), h = static_cast<int>(std::lround(height_ * screen_scale_));
    screen_target_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!screen_target_) throw std::runtime_error(std::string("SDL headless target: ") + SDL_GetError());
}

// The drawing pointed at the screen: the window, or headless its picture (in view pixels, whatever its size).
// @orig none: port-only headless mode
void SdlPresenter::use_screen_target() {
    SDL_SetRenderTarget(renderer_, screen_target_);
    if (screen_target_) SDL_RenderSetScale(renderer_, screen_scale_, screen_scale_);
}

// The window's pixels per view pixel (the view fitted in it as SDL_RenderSetLogicalSize does), at least 1.
// @orig none: port-only upscaled sprites
float SdlPresenter::window_scale() const {
    int ow = 0, oh = 0;
    if (headless_) SDL_GetWindowSize(window_, &ow, &oh);
    else SDL_GetRendererOutputSize(renderer_, &ow, &oh);
    return std::max(1.0f, std::min(static_cast<float>(ow) / static_cast<float>(width_), static_cast<float>(oh) / static_cast<float>(height_)));
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
    // The beam: from the dot back towards the player for a few tiles, fading out; none when the player is that close.
    {
        constexpr float kLength = 55.0f;
        const float dx = laser_.x0 - laser_.x1, dy = laser_.y0 - laser_.y1, distance = std::sqrt(dx * dx + dy * dy);
        constexpr float kHideWithin = 2.0f * kLength;  // closer than this to the player, no beam
        if (distance > kHideWithin) {
            const float ux = dx / distance, uy = dy / distance, nx = -uy, ny = ux;
            constexpr int kSteps = 24;
            for (int pass = 0; pass < 2; ++pass) {
                const float half = pass == 0 ? 2.2f : 0.7f;  // a faint wide glow, then a thin bright core
                const float peak = pass == 0 ? 55.0f : 210.0f;
                SDL_Vertex v[2 * (kSteps + 1)];
                int idx[6 * kSteps];
                for (int i = 0; i <= kSteps; ++i) {
                    const float t = static_cast<float>(i) / kSteps;
                    const float px = laser_.x1 + ux * kLength * t, py = laser_.y1 + uy * kLength * t;
                    const float fade = (1.0f - t) * (1.0f - t * 0.4f);
                    const SDL_Color c = colour(255, pass == 0 ? 40 : 90, pass == 0 ? 30 : 70, peak * fade);
                    v[2 * i] = {{px + nx * half, py + ny * half}, c, {0, 0}};
                    v[2 * i + 1] = {{px - nx * half, py - ny * half}, c, {0, 0}};
                }
                for (int i = 0; i < kSteps; ++i) {
                    const int a = 2 * i, k = 6 * i;
                    idx[k] = a, idx[k + 1] = a + 1, idx[k + 2] = a + 2, idx[k + 3] = a + 2, idx[k + 4] = a + 1, idx[k + 5] = a + 3;
                }
                SDL_RenderGeometry(renderer_, nullptr, v, 2 * (kSteps + 1), idx, 6 * kSteps);
            }
        }
    }
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

// The click-to-walk target: a diamond lying on the floor (twice as wide as high, the view's floor), filled faintly and
// outlined, pulsing gently, until the walk ends.
// @orig none: port-only click-to-walk target marker
void SdlPresenter::draw_walk_target() {
    if (!walk_target_) return;
    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const float pulse = 0.5f + 0.5f * std::sin(t * 6.0f);
    const float hw = 15.0f + 3.0f * pulse, hh = hw * 0.5f;
    const float cx = walk_target_x_, cy = walk_target_y_;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const SDL_Color fill{90, 255, 130, static_cast<Uint8>(50 + 40 * pulse)};
    const SDL_Vertex v[4] = {{{cx, cy - hh}, fill, {0, 0}}, {{cx + hw, cy}, fill, {0, 0}},
                             {{cx, cy + hh}, fill, {0, 0}}, {{cx - hw, cy}, fill, {0, 0}}};
    const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(renderer_, nullptr, v, 4, idx, 6);
    SDL_SetRenderDrawColor(renderer_, 120, 255, 150, static_cast<Uint8>(170 + 85 * pulse));
    for (const float d : {0.0f, 1.0f}) {  // two pixels thick
        const SDL_FPoint p[5] = {{cx, cy - hh - d}, {cx + hw + d, cy}, {cx, cy + hh + d}, {cx - hw - d, cy}, {cx, cy - hh - d}};
        SDL_RenderDrawLinesF(renderer_, p, 5);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

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

namespace crusader {

void SdlPresenter::draw_pointer() {
    if (!pointer_.show) return;
    // The target: a ring with a dot in its middle and four short lines out from the ring, as big as the old target cursor.
    if (pointer_.ring > 4.0f) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        const float cx = pointer_.x, cy = pointer_.y;
        const float outer = pointer_.ring * 0.5f, ring = outer * 0.56f, tick_w = std::max(1.0f, outer * 0.055f);
        const SDL_Color red{255, 40, 40, 255}, shade{0, 0, 0, 170};
        auto annulus = [&](float r_in, float r_out, SDL_Color c) {
            constexpr int kSegments = 48;
            SDL_Vertex v[2 * kSegments];
            int idx[6 * kSegments];
            for (int i = 0; i < kSegments; ++i) {
                const float a = 6.2831853f * static_cast<float>(i) / kSegments;
                v[2 * i] = {{cx + std::cos(a) * r_in, cy + std::sin(a) * r_in}, c, {0, 0}};
                v[2 * i + 1] = {{cx + std::cos(a) * r_out, cy + std::sin(a) * r_out}, c, {0, 0}};
                const int next = (i + 1) % kSegments;
                const int k = 6 * i;
                idx[k] = 2 * i, idx[k + 1] = 2 * i + 1, idx[k + 2] = 2 * next;
                idx[k + 3] = 2 * next, idx[k + 4] = 2 * i + 1, idx[k + 5] = 2 * next + 1;
            }
            SDL_RenderGeometry(renderer_, nullptr, v, 2 * kSegments, idx, 6 * kSegments);
        };
        auto bar = [&](float x0, float y0, float x1, float y1, float w, SDL_Color c) {  // a straight bar of width w
            const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
            const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
            const SDL_Vertex v[4] = {{{x0 + nx, y0 + ny}, c, {0, 0}}, {{x0 - nx, y0 - ny}, c, {0, 0}},
                                     {{x1 - nx, y1 - ny}, c, {0, 0}}, {{x1 + nx, y1 + ny}, c, {0, 0}}};
            static const int idx[6] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer_, nullptr, v, 4, idx, 6);
        };
        const float thick = std::max(0.8f, outer * 0.04f);
        annulus(ring - thick - 0.7f, ring + thick + 0.7f, shade);  // a thin dark rim so it reads on any floor
        annulus(ring - thick, ring + thick, red);
        for (int i = 0; i < 4; ++i) {
            const float dx = i == 0 ? 1.0f : i == 1 ? -1.0f : 0.0f, dy = i == 2 ? 1.0f : i == 3 ? -1.0f : 0.0f;
            bar(cx + dx * (ring + thick), cy + dy * (ring + thick), cx + dx * outer, cy + dy * outer, tick_w + 1.4f, shade);
            bar(cx + dx * (ring + thick), cy + dy * (ring + thick), cx + dx * outer, cy + dy * outer, tick_w, red);
        }
        const float dot = std::max(1.5f, outer * 0.09f);
        auto disc = [&](float r, SDL_Color c) {
            constexpr int kSegments = 20;
            SDL_Vertex v[kSegments + 1];
            int idx[kSegments * 3];
            v[0] = {{cx, cy}, c, {0, 0}};
            for (int i = 0; i < kSegments; ++i) {
                const float a = 6.2831853f * static_cast<float>(i) / kSegments;
                v[i + 1] = {{cx + std::cos(a) * r, cy + std::sin(a) * r}, c, {0, 0}};
                idx[i * 3] = 0, idx[i * 3 + 1] = i + 1, idx[i * 3 + 2] = (i + 1) % kSegments + 1;
            }
            SDL_RenderGeometry(renderer_, nullptr, v, kSegments + 1, idx, kSegments * 3);
        };
        disc(dot + 0.7f, shade);
        disc(dot, red);
        if (pointer_.hover != 0) {
            // The indicator of the arrow: a dot beside the ring at the lower right, green in reach, red too far.
            const SDL_Color c = pointer_.hover == 1 ? SDL_Color{70, 230, 100, 255} : SDL_Color{235, 65, 55, 255};
            const float ix = cx + outer * 0.95f, iy = cy + outer * 0.95f, r = std::max(3.8f, outer * 0.2f);
            auto badge = [&](float rr, SDL_Color col) {
                constexpr int kSegments = 18;
                SDL_Vertex v[kSegments + 1];
                int idx[kSegments * 3];
                v[0] = {{ix, iy}, col, {0, 0}};
                for (int i = 0; i < kSegments; ++i) {
                    const float a = 6.2831853f * static_cast<float>(i) / kSegments;
                    v[i + 1] = {{ix + std::cos(a) * rr, iy + std::sin(a) * rr}, col, {0, 0}};
                    idx[i * 3] = 0, idx[i * 3 + 1] = i + 1, idx[i * 3 + 2] = (i + 1) % kSegments + 1;
                }
                SDL_RenderGeometry(renderer_, nullptr, v, kSegments + 1, idx, kSegments * 3);
            };
            badge(r + 1.4f, SDL_Color{0, 0, 0, 215});
            badge(r, c);
        }
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    constexpr float kScale = 1.15f;
    // The arrow, tip at the pointer: tip, left foot, inner notch, tail (two points), notch, right wing.
    const SDL_FPoint shape[7] = {{0, 0}, {0, 16}, {4.4f, 12.2f}, {7.4f, 18.6f}, {10, 17.4f}, {7, 11}, {12, 11}};
    static const int tri[5][3] = {{0, 1, 2}, {0, 2, 5}, {0, 5, 6}, {2, 3, 4}, {2, 4, 5}};
    auto polygon = [&](float ox, float oy, SDL_Color c) {
        SDL_Vertex v[7];
        for (int i = 0; i < 7; ++i) v[i] = {{pointer_.x + ox + shape[i].x * kScale, pointer_.y + oy + shape[i].y * kScale}, c, {0, 0}};
        int idx[15];
        for (int t = 0; t < 5; ++t)
            for (int k = 0; k < 3; ++k) idx[t * 3 + k] = tri[t][k];
        SDL_RenderGeometry(renderer_, nullptr, v, 7, idx, 15);
    };
    const SDL_Color outline{0, 0, 0, 215}, fill{250, 250, 250, 255};
    for (const SDL_FPoint& o : {SDL_FPoint{-1, 0}, SDL_FPoint{1, 0}, SDL_FPoint{0, -1}, SDL_FPoint{0, 1}, SDL_FPoint{1, 1}, SDL_FPoint{-1, -1}})
        polygon(o.x, o.y, outline);
    polygon(0, 0, fill);
    if (pointer_.hover != 0) {
        // The indicator: a dot with a dark rim at the lower right of the arrow, green when in reach, red when not.
        const SDL_Color c = pointer_.hover == 1 ? SDL_Color{70, 230, 100, 255} : SDL_Color{235, 65, 55, 255};
        auto disc = [&](float r, SDL_Color col) {
            constexpr int kSegments = 18;
            SDL_Vertex v[kSegments + 1];
            int idx[kSegments * 3];
            const float cx = pointer_.x + 17.0f, cy = pointer_.y + 18.0f;
            v[0] = {{cx, cy}, col, {0, 0}};
            for (int i = 0; i < kSegments; ++i) {
                const float a = 6.2831853f * static_cast<float>(i) / kSegments;
                v[i + 1] = {{cx + std::cos(a) * r, cy + std::sin(a) * r}, col, {0, 0}};
                idx[i * 3] = 0;
                idx[i * 3 + 1] = i + 1;
                idx[i * 3 + 2] = (i + 1) % kSegments + 1;
            }
            SDL_RenderGeometry(renderer_, nullptr, v, kSegments + 1, idx, kSegments * 3);
        };
        disc(5.2f, SDL_Color{0, 0, 0, 215});
        disc(3.8f, c);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

}  // namespace crusader

namespace crusader {

// Blood, on the graphics card: drops are thin streaks, mist is soft red puffs, and what has landed is a flat
// dark splat (an ellipse, the floor being seen at an angle) that fades away.
void SdlPresenter::draw_blood() {
    if (blood_.empty()) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    auto disc = [&](float cx, float cy, float rx, float ry, SDL_Color centre, SDL_Color edge) {
        constexpr int kSegments = 14;
        SDL_Vertex v[kSegments + 1];
        int idx[kSegments * 3];
        v[0] = {{cx, cy}, centre, {0, 0}};
        for (int i = 0; i < kSegments; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / kSegments;
            v[i + 1] = {{cx + std::cos(a) * rx, cy + std::sin(a) * ry}, edge, {0, 0}};
            idx[i * 3] = 0;
            idx[i * 3 + 1] = i + 1;
            idx[i * 3 + 2] = (i + 1) % kSegments + 1;
        }
        SDL_RenderGeometry(renderer_, nullptr, v, kSegments + 1, idx, kSegments * 3);
    };
    auto alpha = [](float a) { return static_cast<Uint8>(std::clamp(a, 0.0f, 1.0f) * 255.0f); };
    for (const BloodDot& d : blood_) {
        if (d.kind == 2) {  // a splat
            disc(d.x, d.y, d.size * 1.7f, d.size * 0.85f, SDL_Color{205, 14, 20, alpha(d.alpha)}, SDL_Color{150, 8, 14, alpha(d.alpha * 0.6f)});
        } else if (d.kind == 1) {  // mist
            disc(d.x, d.y, d.size, d.size, SDL_Color{240, 32, 32, alpha(d.alpha)}, SDL_Color{240, 32, 32, 0});
        } else {  // a drop in flight: a streak from where it was to where it is
            const float dx = d.x - d.x0, dy = d.y - d.y0;
            const float len = std::sqrt(dx * dx + dy * dy);
            const float nx = len > 0.01f ? -dy / len : 0.0f, ny = len > 0.01f ? dx / len : 1.0f;
            const float w = d.size * 0.5f;
            const SDL_Color c{230, 22, 26, alpha(d.alpha)}, tail{230, 22, 26, alpha(d.alpha * 0.2f)};
            const SDL_Vertex v[4] = {{{d.x0 + nx * w, d.y0 + ny * w}, tail, {0, 0}},
                                     {{d.x0 - nx * w, d.y0 - ny * w}, tail, {0, 0}},
                                     {{d.x - nx * w, d.y - ny * w}, c, {0, 0}},
                                     {{d.x + nx * w, d.y + ny * w}, c, {0, 0}}};
            static const int idx[6] = {0, 1, 2, 0, 2, 3};
            SDL_RenderGeometry(renderer_, nullptr, v, 4, idx, 6);
            disc(d.x, d.y, w * 1.1f, w * 1.1f, c, c);
        }
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

}  // namespace crusader

namespace crusader {

void SdlPresenter::clear_scene_textures() {
    for (auto& entry : scene_textures_) {
        if (entry.second.texture) SDL_DestroyTexture(entry.second.texture);
        if (entry.second.mask) SDL_DestroyTexture(entry.second.mask);
    }
    scene_textures_.clear();
}

// @orig none: port-only upscaled sprites
void SdlPresenter::set_upscaled_sprites(bool on, const std::filesystem::path& dir) {
    if (on == upscaled_ && dir == upscaled_dir_) return;
    clear_upscaled_textures();
    upscaled_ = on;
    upscaled_dir_ = dir;
    make_screen_target();  // (headless: its picture at the hidden window's size, or the view's)
}

// @orig none: port-only upscaled sprites
void SdlPresenter::clear_upscaled_textures() {
    for (auto& entry : upscaled_textures_) {
        if (entry.second) SDL_DestroyTexture(entry.second);
    }
    upscaled_textures_.clear();
}

// The upscaled picture of a shape's frame (dir/SSSS_FFF.png), loaded once; null when there is none.
// @orig none: port-only upscaled sprites
SDL_Texture* SdlPresenter::upscaled_texture(int shape, int frame) {
    const std::uint32_t key = static_cast<std::uint32_t>(shape) << 16 | static_cast<std::uint32_t>(frame & 0xFFFF);
    const auto found = upscaled_textures_.find(key);
    if (found != upscaled_textures_.end()) return found->second;
    char name[32];
    std::snprintf(name, sizeof name, "%04d_%03d.png", shape, frame);
    SDL_Texture* t = nullptr;
    if (const auto img = read_png(upscaled_dir_ / name)) {
        t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, img->width, img->height);
        if (t) {
            SDL_UpdateTexture(t, nullptr, img->pixels.data(), img->width * static_cast<int>(sizeof(Uint32)));
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        }
    }
    upscaled_textures_.emplace(key, t);
    return t;
}

// The extra angles' picture (assets/angles/<set>_sNN.png, tools/angles), loaded once; null when there is none.
// @orig none: port-only extra angles
SDL_Texture* SdlPresenter::angle_texture(const char* set, int step) {
    char name[32];
    std::snprintf(name, sizeof name, "%s_s%02d.png", set, step);
    const auto found = angle_textures_.find(name);
    if (found != angle_textures_.end()) return found->second;
    SDL_Texture* t = nullptr;
    if (const auto img = read_png(std::filesystem::current_path() / "assets" / "angles" / name)) {
        t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, img->width, img->height);
        if (t) {
            SDL_UpdateTexture(t, nullptr, img->pixels.data(), img->width * static_cast<int>(sizeof(Uint32)));
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
        }
    }
    angle_textures_.emplace(name, t);
    return t;
}

// The hardware scene: the pictures in order as textures made from the palette, and the splats of blood on the floor.
// A change of palette (a fade, a colour cycle) or of gamma makes the textures again as they are next needed.
// @orig none: port-only hardware scene
void SdlPresenter::draw_scene(const Palette& palette) {
    if (!scene_) return;
    if (scene_gamma_version_ != gamma_version_ || std::memcmp(scene_colours_.data(), palette.colors.data(), sizeof(Rgb) * 256) != 0) {
        clear_scene_textures();
        std::memcpy(scene_colours_.data(), palette.colors.data(), sizeof(Rgb) * 256);
        scene_gamma_version_ = gamma_version_;
    }
    if (scene_textures_.size() > 8000) clear_scene_textures();  // far more than a scene shows: start again
    if ((lighting_ != nullptr) != scene_masks_) {  // the lighting switched: the pictures again, with or without their masks
        clear_scene_textures();
        scene_masks_ = lighting_ != nullptr;
    }
    std::array<Uint32, 256> lut;
    for (std::size_t i = 0; i < lut.size(); ++i) {
        const Rgb& c = palette.colors[i];
        lut[i] = 0xFF000000u | Uint32{gamma_table_[c.r]} << 16 | Uint32{gamma_table_[c.g]} << 8 | gamma_table_[c.b];
    }
    std::vector<Uint32> pixels;
    for (const SceneCmd& cmd : *scene_) {
        if (!cmd.frame) {  // a splat of blood on the floor
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
            constexpr int kSegments = 14;
            SDL_Vertex v[kSegments + 1];
            int idx[kSegments * 3];
            const auto alpha = [](float a) { return static_cast<Uint8>(std::clamp(a, 0.0f, 1.0f) * 255.0f); };
            const float cx = static_cast<float>(cmd.x), cy = static_cast<float>(cmd.y);
            v[0] = {{cx, cy}, SDL_Color{205, 14, 20, alpha(cmd.alpha)}, {0, 0}};
            for (int i = 0; i < kSegments; ++i) {
                const float a = 6.2831853f * static_cast<float>(i) / kSegments;
                v[i + 1] = {{cx + std::cos(a) * cmd.rx, cy + std::sin(a) * cmd.ry}, SDL_Color{150, 8, 14, alpha(cmd.alpha * 0.6f)}, {0, 0}};
                idx[i * 3] = 0;
                idx[i * 3 + 1] = i + 1;
                idx[i * 3 + 2] = (i + 1) % kSegments + 1;
            }
            SDL_RenderGeometry(renderer_, nullptr, v, kSegments + 1, idx, kSegments * 3);
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
            continue;
        }
        const ShapeFrame& f = *cmd.frame;
        if (f.width <= 0 || f.height <= 0) continue;
        const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(cmd.frame) << 1 | (cmd.mirrored ? 1u : 0u);
        auto found = scene_textures_.find(key);
        if (found == scene_textures_.end()) {
            Surface8 scratch(f.width, f.height);
            scratch.clear(kSceneKey);
            f.draw(scratch, cmd.mirrored ? f.width - 1 - f.xoff : f.xoff, f.yoff, cmd.mirrored);
            pixels.resize(scratch.pixels.size());
            for (std::size_t i = 0; i < pixels.size(); ++i) {
                pixels[i] = scratch.pixels[i] == kSceneKey ? 0u : lut[scratch.pixels[i]];
            }
            SceneTexture t;
            t.w = f.width, t.h = f.height;
            t.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, f.width, f.height);
            if (t.texture) {
                SDL_UpdateTexture(t.texture, nullptr, pixels.data(), f.width * static_cast<int>(sizeof(Uint32)));
                SDL_SetTextureBlendMode(t.texture, SDL_BLENDMODE_BLEND);
                // (a fire or an explosion smoothly filtered at the window's size: draw_fx)
                SDL_SetTextureScaleMode(t.texture, cmd.fx ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
            }
            if (lighting_) {
                // Its shape in white, coloured by its room's ambient in the light map.
                for (std::size_t i = 0; i < pixels.size(); ++i) pixels[i] = pixels[i] ? 0xFFFFFFFFu : 0u;
                t.mask = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, f.width, f.height);
                if (t.mask) {
                    SDL_UpdateTexture(t.mask, nullptr, pixels.data(), f.width * static_cast<int>(sizeof(Uint32)));
                    SDL_SetTextureBlendMode(t.mask, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(t.mask, SDL_ScaleModeNearest);
                }
            }
            found = scene_textures_.emplace(key, t).first;
        }
        const SceneTexture& t = found->second;
        if (!t.texture) continue;
        if (cmd.angle_step >= 0) {  // the player between two drawn facings (the extra angles), his gun in the picture
            if (SDL_Texture* angled = angle_texture(cmd.angle_set, cmd.angle_step)) {
                SDL_SetTextureAlphaMod(angled, 255);
                const SDL_Rect cell{cmd.anchor_x - kAngleAnchorX, cmd.anchor_y - kAngleAnchorY, kAngleCell, kAngleCell};
                SDL_RenderCopy(renderer_, angled, nullptr, &cell);
                continue;
            }
        }
        if (cmd.fx) {  // a fire or an explosion: glowing particles instead of the picture
            draw_fx(cmd, t.texture, t.w, t.h);
            continue;
        }
        const SDL_Rect dst{cmd.x, cmd.y, t.w, t.h};
        // Its upscaled picture where there is one: larger, stretched over the same place (the window's pixels show it).
        if (SDL_Texture* big = upscaled_ && cmd.shape >= 0 ? upscaled_texture(cmd.shape, cmd.frame_no) : nullptr) {
            SDL_SetTextureAlphaMod(big, cmd.translucent ? 150 : 255);
            SDL_RenderCopyEx(renderer_, big, nullptr, &dst, 0, nullptr, cmd.mirrored ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
            continue;
        }
        // Windows, force fields: the original blends them through a table; here they are simply half see-through.
        SDL_SetTextureAlphaMod(t.texture, cmd.translucent ? 150 : 255);
        SDL_RenderCopy(renderer_, t.texture, nullptr, &dst);
    }
    draw_scene_flashes();
}

// @orig none: port-only hardware effects
bool SdlPresenter::make_fx_blob() {
    if (fx_blob_) return true;
    constexpr int kSize = 64;
    std::vector<Uint32> pixels(kSize * kSize);
    for (int y = 0; y < kSize; ++y) {
        for (int x = 0; x < kSize; ++x) {
            const float dx = (x + 0.5f) / (kSize / 2) - 1, dy = (y + 0.5f) / (kSize / 2) - 1;
            const float t = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
            const auto a = static_cast<Uint32>(std::lround(255.0f * t * t));  // soft, brighter in the middle
            pixels[static_cast<std::size_t>(y * kSize + x)] = a << 24 | 0xFFFFFFu;
        }
    }
    fx_blob_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, kSize, kSize);
    if (!fx_blob_) return false;
    SDL_UpdateTexture(fx_blob_, nullptr, pixels.data(), kSize * static_cast<int>(sizeof(Uint32)));
    SDL_SetTextureScaleMode(fx_blob_, SDL_ScaleModeLinear);
    return true;
}

// A fire or an explosion: its own picture (the original animation, frame by frame), smoothly filtered at the window's
// size, over a glow of the same shape - the picture again, larger and added on, flickering a little.
// @orig none: port-only hardware effects
void SdlPresenter::draw_fx(const SceneCmd& cmd, SDL_Texture* texture, int w, int h) {
    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    const SDL_RendererFlip flip = SDL_FLIP_NONE;  // (a mirrored picture's texture is drawn mirrored already)
    const float fw = static_cast<float>(w), fh = static_cast<float>(h);
    const float cx = static_cast<float>(cmd.x) + fw / 2, cy = static_cast<float>(cmd.y) + fh / 2;
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_ADD);
    for (const auto [grow, alpha] : {std::pair{1.35f, 70.0f}, std::pair{1.12f, 90.0f}}) {
        const float flicker = 1.0f + 0.04f * std::sin(t * 17.0f + grow * 5.0f);
        const float gw = fw * grow * flicker, gh = fh * grow * flicker;
        const SDL_FRect glow{cx - gw / 2, cy - gh / 2, gw, gh};
        SDL_SetTextureAlphaMod(texture, static_cast<Uint8>(alpha));
        SDL_RenderCopyExF(renderer_, texture, nullptr, &glow, 0, nullptr, flip);
    }
    // A translucent one (the jets of flame: the original blends them through a table that brightens what is under them)
    // is added on, light; the others are drawn over.
    SDL_SetTextureBlendMode(texture, cmd.translucent ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND);
    SDL_SetTextureAlphaMod(texture, 255);
    const SDL_FRect dst{static_cast<float>(cmd.x), static_cast<float>(cmd.y), fw, fh};
    SDL_RenderCopyExF(renderer_, texture, nullptr, &dst, 0, nullptr, flip);
}

// The muzzle flashes: a warm glow and a white core, bright as the shot leaves, gone in a moment.
// @orig none: port-only hardware effects
void SdlPresenter::draw_scene_flashes() {
    if (!scene_flashes_ || scene_flashes_->empty() || !make_fx_blob()) return;
    SDL_SetTextureBlendMode(fx_blob_, SDL_BLENDMODE_ADD);
    for (const SceneFlash& fl : *scene_flashes_) {
        const float k = std::clamp(fl.strength, 0.0f, 1.0f);
        for (const auto& [radius, r, g, b] : {std::tuple{22.0f, 255, 170, 70}, std::tuple{9.0f, 255, 250, 230}}) {
            SDL_SetTextureColorMod(fx_blob_, static_cast<Uint8>(r), static_cast<Uint8>(g), static_cast<Uint8>(b));
            SDL_SetTextureAlphaMod(fx_blob_, static_cast<Uint8>(255.0f * k));
            const float s = radius * (0.7f + 0.3f * k) * 2;
            const SDL_FRect dst{fl.x - s / 2, fl.y - s / 2, s, s};
            SDL_RenderCopyF(renderer_, fx_blob_, nullptr, &dst);
        }
    }
}

}  // namespace crusader

namespace crusader {

// Points the drawing at the scene's own target (made again at a new size), cleared to black. False when the renderer has
// no render targets: the scene is then drawn unlit.
// @orig none: port-only lighting
bool SdlPresenter::begin_lit_scene() {
    if (lighting_broken_) return false;
    if (!SDL_RenderTargetSupported(renderer_)) {
        lighting_broken_ = true;
        return false;
    }
    // The targets: the size the view takes in the window (the upscaled sprites' and the effects' detail kept).
    const float s = window_scale();
    const int want_w = static_cast<int>(std::lround(width_ * s)), want_h = static_cast<int>(std::lround(height_ * s));
    if (!scene_target_ || target_w_ != want_w || target_h_ != want_h || small_w_ != std::max(1, width_ / kRoomScale) ||
        small_h_ != std::max(1, height_ / kRoomScale)) {
        for (SDL_Texture** t : {&scene_target_, &light_target_}) {
            if (*t) SDL_DestroyTexture(*t);
            *t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, want_w, want_h);
        }
        target_w_ = want_w, target_h_ = want_h;
        target_scale_x_ = static_cast<float>(want_w) / static_cast<float>(width_);
        target_scale_y_ = static_cast<float>(want_h) / static_cast<float>(height_);
        small_w_ = std::max(1, width_ / kRoomScale), small_h_ = std::max(1, height_ / kRoomScale);
        for (SDL_Texture*& t : room_small_) {
            if (t) SDL_DestroyTexture(t);
            t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, small_w_, small_h_);
            if (t) SDL_SetTextureScaleMode(t, SDL_ScaleModeLinear);
        }
        if (!scene_target_ || !light_target_ || !room_small_[0] || !room_small_[1]) {
            lighting_broken_ = true;
            return false;
        }
        SDL_SetTextureScaleMode(scene_target_, SDL_ScaleModeNearest);
        SDL_SetTextureScaleMode(light_target_, SDL_ScaleModeLinear);
        // Twice the light map: 0.5 keeps the scene as drawn, so a light can make it brighter than its art.
        const SDL_BlendMode doubled = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_DST_COLOR, SDL_BLENDFACTOR_SRC_COLOR, SDL_BLENDOPERATION_ADD,
                                                                 SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
        light_doubled_ = SDL_SetTextureBlendMode(light_target_, doubled) == 0;
        if (!light_doubled_) SDL_SetTextureBlendMode(light_target_, SDL_BLENDMODE_MOD);
        SDL_SetTextureBlendMode(scene_target_, SDL_BLENDMODE_NONE);
    }
    if (!light_spot_) {
        // A soft round spot, white in the middle and black at its edge (a smooth curve, no visible rim). Spots meeting
        // keep the brightest, not their sum (added up only where the renderer has no such blend).
        constexpr int kSize = 128;
        std::vector<Uint32> pixels(kSize * kSize);
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                const float dx = (x + 0.5f) / (kSize / 2) - 1, dy = (y + 0.5f) / (kSize / 2) - 1;
                const float t = std::clamp(1.0f - std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
                const auto v = static_cast<Uint32>(std::lround(255.0f * t * t * (3 - 2 * t)));
                pixels[static_cast<std::size_t>(y * kSize + x)] = 0xFF000000u | v << 16 | v << 8 | v;
            }
        }
        light_spot_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, kSize, kSize);
        if (!light_spot_) {
            lighting_broken_ = true;
            return false;
        }
        SDL_UpdateTexture(light_spot_, nullptr, pixels.data(), kSize * static_cast<int>(sizeof(Uint32)));
        SDL_SetTextureScaleMode(light_spot_, SDL_ScaleModeLinear);
        const SDL_BlendMode brightest = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM,
                                                                   SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM);
        light_brightest_ = SDL_SetTextureBlendMode(light_spot_, brightest) == 0;
        if (!light_brightest_) SDL_SetTextureBlendMode(light_spot_, SDL_BLENDMODE_ADD);
    }
    SDL_SetRenderTarget(renderer_, scene_target_);
    SDL_RenderSetScale(renderer_, target_scale_x_, target_scale_y_);  // (the scene in view pixels, whatever its size)
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    return true;
}

// The light map, then the scene multiplied by it onto the screen. The map is the scene again, each picture as its shape
// in its room's ambient (dark outside the lit rooms), in the same order (what is in front hides what is behind), drawn
// small and blurred so that the light changes smoothly between rooms; then the lights over it, the brightest winning.
// @orig none: port-only lighting
void SdlPresenter::end_lit_scene() {
    const float scale = light_doubled_ ? 1.0f : 2.0f;  // multiplied in once: the map holds the whole factor
    const auto byte = [scale](float v) { return static_cast<Uint8>(std::clamp(v * scale, 0.0f, 1.0f) * 255.0f + 0.5f); };
    const SceneLighting& lit = *lighting_;
    SDL_SetRenderTarget(renderer_, room_small_[0]);
    SDL_SetRenderDrawColor(renderer_, byte(lit.dark_r), byte(lit.dark_g), byte(lit.dark_b), 255);
    SDL_RenderClear(renderer_);
    constexpr float kSmall = 1.0f / kRoomScale;
    for (const SceneCmd& cmd : *scene_) {
        if (!cmd.frame || (cmd.translucent && !cmd.fx)) continue;  // a splat or a window: the light of what is under it
        const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(cmd.frame) << 1 | (cmd.mirrored ? 1u : 0u);
        const auto found = scene_textures_.find(key);
        if (found == scene_textures_.end() || !found->second.mask) continue;
        const SceneTexture& t = found->second;
        if (cmd.fx) {  // a fire or an explosion gives light: shown as drawn, whatever lights the room (a red alarm)
            SDL_SetTextureColorMod(t.mask, byte(0.5f), byte(0.5f), byte(0.5f));
        } else if (cmd.ambient_r >= 0) {  // an area with its own ambient (the light editor)
            SDL_SetTextureColorMod(t.mask, byte(cmd.ambient_r), byte(cmd.ambient_g), byte(cmd.ambient_b));
        } else {
            SDL_SetTextureColorMod(t.mask, byte(lit.dark_r), byte(lit.dark_g), byte(lit.dark_b));
        }
        const SDL_FRect dst{cmd.x * kSmall, cmd.y * kSmall, t.w * kSmall, t.h * kSmall};
        SDL_RenderCopyF(renderer_, t.mask, nullptr, &dst);
    }
    // The blur: each pass the average of the picture and four copies moved by `step` (1, 2, 3 small pixels: about
    // 6 x kRoomScale view pixels of soft edge in all). Returns the small picture that holds the result.
    const auto blur = [this]() {
        int from = 0;
        for (const int step : {1, 2, 3}) {
            SDL_Texture* src = room_small_[from];
            SDL_SetRenderTarget(renderer_, room_small_[1 - from]);
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
            SDL_RenderClear(renderer_);
            SDL_SetTextureBlendMode(src, SDL_BLENDMODE_ADD);
            SDL_SetTextureColorMod(src, 51, 51, 51);
            for (const auto [dx, dy] : {std::pair{0, 0}, std::pair{step, 0}, std::pair{-step, 0}, std::pair{0, step}, std::pair{0, -step}}) {
                const SDL_Rect dst{dx, dy, small_w_, small_h_};
                SDL_RenderCopy(renderer_, src, nullptr, &dst);
            }
            from = 1 - from;
        }
        SDL_SetTextureColorMod(room_small_[from], 255, 255, 255);
        return room_small_[from];
    };
    SDL_Texture* rooms = blur();
    SDL_SetRenderTarget(renderer_, light_target_);
    SDL_RenderSetScale(renderer_, target_scale_x_, target_scale_y_);
    SDL_SetTextureBlendMode(rooms, SDL_BLENDMODE_NONE);
    SDL_RenderCopy(renderer_, rooms, nullptr, nullptr);
    for (const SceneLight& l : lit.lights) {
        if (l.radius <= 0 || l.intensity <= 0) continue;
        const Uint8 r = byte(l.r * l.intensity), g = byte(l.g * l.intensity), b = byte(l.b * l.intensity);
        if (l.beam) {
            // A cone: a beam from the source down to the edges of its spot, fading as it goes (geometry with the lights'
            // blend: the brightest wins, else added).
            SDL_BlendMode blend = SDL_BLENDMODE_ADD;
            SDL_GetTextureBlendMode(light_spot_, &blend);
            SDL_SetRenderDrawBlendMode(renderer_, blend);
            const auto half = [](Uint8 v) { return static_cast<Uint8>(v / 2); };
            const SDL_Color top{half(r), half(g), half(b), 255}, bottom{static_cast<Uint8>(r / 5), static_cast<Uint8>(g / 5),
                                                                        static_cast<Uint8>(b / 5), 255};
            const SDL_Vertex v[3] = {{{l.bx, l.by}, top, {0, 0}},
                                     {{l.x - l.radius * 0.7f, l.y}, bottom, {0, 0}},
                                     {{l.x + l.radius * 0.7f, l.y}, bottom, {0, 0}}};
            SDL_RenderGeometry(renderer_, nullptr, v, 3, nullptr, 0);
            SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
        }
        SDL_SetTextureColorMod(light_spot_, r, g, b);
        const SDL_FRect dst{l.x - l.radius, l.y - l.radius * 0.5f, l.radius * 2, l.radius};
        SDL_RenderCopyF(renderer_, light_spot_, nullptr, &dst);
        if (l.core > 0) {  // a sphere's bright core: the same light again, smaller (a flatter, harder light)
            const float c = l.radius * std::min(l.core, 0.95f);
            const SDL_FRect in{l.x - c, l.y - c * 0.5f, c * 2, c};
            SDL_RenderCopyF(renderer_, light_spot_, nullptr, &in);
        }
    }
    use_screen_target();
    SDL_RenderCopy(renderer_, scene_target_, nullptr, nullptr);
    SDL_RenderCopy(renderer_, light_target_, nullptr, nullptr);
}

// The light debug mode: the rooms' floor cells as see-through diamonds, the lights (rings) and where each room's search
// started (crosses), the clicked one in white, and its line of text at the top.
// @orig none: port-only lighting
void SdlPresenter::draw_light_debug() {
    if (!light_debug_) return;
    const LightDebugOverlay& o = *light_debug_;
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    v.reserve(o.cells.size() * 4);
    idx.reserve(o.cells.size() * 6);
    for (const LightDebugOverlay::Cell& c : o.cells) {
        const int base = static_cast<int>(v.size());
        for (int i = 0; i < 4; ++i) v.push_back({{c.x[i], c.y[i]}, SDL_Color{c.r, c.g, c.b, 90}, {0, 0}});
        for (const int i : {0, 1, 2, 0, 2, 3}) idx.push_back(base + i);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    if (!v.empty()) SDL_RenderGeometry(renderer_, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
    for (const LightDebugOverlay::Mark& m : o.marks) {
        const Uint8 c = m.picked ? 255 : 200;
        if (m.seed) {
            SDL_SetRenderDrawColor(renderer_, c, m.picked ? 255 : 60, m.picked ? 255 : 220, 255);
            for (const float d : {-1.0f, 0.0f, 1.0f}) {
                SDL_RenderDrawLineF(renderer_, m.x - 6, m.y - 6 + d, m.x + 6, m.y + 6 + d);
                SDL_RenderDrawLineF(renderer_, m.x - 6, m.y + 6 + d, m.x + 6, m.y - 6 + d);
            }
        } else if (m.style != 0) {  // the light editor's ambient areas (a square) and cones (a triangle)
            SDL_SetRenderDrawColor(renderer_, c, m.picked ? 255 : (m.style == 1 ? 120 : 200), m.picked ? 255 : (m.style == 1 ? 255 : 60), 255);
            const float r = m.picked ? 11.0f : 8.0f;
            for (const float d : {0.0f, 1.0f}) {
                if (m.style == 1) {
                    const SDL_FRect sq{m.x - r - d, m.y - r - d, 2 * (r + d), 2 * (r + d)};
                    SDL_RenderDrawRectF(renderer_, &sq);
                } else {
                    SDL_RenderDrawLineF(renderer_, m.x, m.y - r - d, m.x - r - d, m.y + r);
                    SDL_RenderDrawLineF(renderer_, m.x - r - d, m.y + r + d, m.x + r + d, m.y + r + d);
                    SDL_RenderDrawLineF(renderer_, m.x + r + d, m.y + r, m.x, m.y - r - d);
                }
            }
        } else {
            SDL_SetRenderDrawColor(renderer_, c, c, m.picked ? 255 : 60, 255);
            const float r = m.picked ? 10.0f : 7.0f;
            constexpr int kSides = 16;
            for (int i = 0; i < kSides; ++i) {
                const float a0 = 6.2831853f * static_cast<float>(i) / kSides, a1 = 6.2831853f * static_cast<float>(i + 1) / kSides;
                SDL_RenderDrawLineF(renderer_, m.x + std::cos(a0) * r, m.y + std::sin(a0) * r, m.x + std::cos(a1) * r, m.y + std::sin(a1) * r);
            }
        }
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    for (const LightDebugOverlay::Label& l : o.labels) {
        const float k = o.text_scale, half = text_width(l.text, k) * 0.5f;
        text(l.x - half + k, l.y + k, l.text, 0, 0, 0, false, k);  // (a shadow, to read over any floor)
        text(l.x - half, l.y, l.text, 255, 255, 255, false, k);
    }
    // The light editor's mini map: a dark panel, the level, the view's outline in yellow.
    if (const LightDebugOverlay::MiniMap& m = o.minimap; m.polys && !m.polys->empty()) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 8, 10, 14, 220);
        const SDL_FRect panel{m.x, m.y, m.w, m.h};
        SDL_RenderFillRectF(renderer_, &panel);
        const SDL_Rect clip{static_cast<int>(m.x), static_cast<int>(m.y), static_cast<int>(m.w) + 1, static_cast<int>(m.h) + 1};
        SDL_RenderSetClipRect(renderer_, &clip);
        std::vector<SDL_Vertex> mv;
        std::vector<int> mi;
        mv.reserve(m.polys->size() * 4);
        mi.reserve(m.polys->size() * 6);
        for (const MapPoly& poly : *m.polys) {
            const int base = static_cast<int>(mv.size());
            for (int i = 0; i < 4; ++i) {
                mv.push_back({{m.ox + poly.x[i] * m.scale, m.oy + poly.y[i] * m.scale}, SDL_Color{poly.r, poly.g, poly.b, poly.a}, {0, 0}});
            }
            for (const int i : {0, 1, 2, 0, 2, 3}) mi.push_back(base + i);
        }
        SDL_RenderGeometry(renderer_, nullptr, mv.data(), static_cast<int>(mv.size()), mi.data(), static_cast<int>(mi.size()));
        SDL_SetRenderDrawColor(renderer_, 255, 220, 60, 255);
        for (int i = 0; i < 4; ++i) {
            const int j = (i + 1) % 4;
            SDL_RenderDrawLineF(renderer_, m.ox + m.view_x[i] * m.scale, m.oy + m.view_y[i] * m.scale, m.ox + m.view_x[j] * m.scale,
                                m.oy + m.view_y[j] * m.scale);
        }
        SDL_RenderSetClipRect(renderer_, nullptr);
        SDL_SetRenderDrawColor(renderer_, 120, 130, 150, 255);
        SDL_RenderDrawRectF(renderer_, &panel);
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    }
    const float k = o.text_scale;
    text(8.0f * k + k, 18.0f * k + k, o.text, 0, 0, 0, false, k);  // (a shadow, to read over any floor)
    text(8.0f * k, 18.0f * k, o.text, 255, 230, 120, false, k);
    for (std::size_t i = 0; i < o.lines.size(); ++i) {
        text(8.0f * k + k, (34.0f + 14.0f * static_cast<float>(i)) * k + k, o.lines[i], 0, 0, 0, false, k);
        text(8.0f * k, (34.0f + 14.0f * static_cast<float>(i)) * k, o.lines[i], 210, 235, 255, false, k);
    }
    // The light editor's panels, over all the rest.
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (const LightDebugOverlay::Box& b : o.boxes) {
        const SDL_FRect r{b.x, b.y, b.w, b.h};
        SDL_SetRenderDrawColor(renderer_, b.fill[0], b.fill[1], b.fill[2], b.fill[3]);
        SDL_RenderFillRectF(renderer_, &r);
        if (b.edge[3]) {
            SDL_SetRenderDrawColor(renderer_, b.edge[0], b.edge[1], b.edge[2], b.edge[3]);
            SDL_RenderDrawRectF(renderer_, &r);
        }
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    for (const LightDebugOverlay::Box& b : o.boxes) {
        const float baseline = b.y + b.h * 0.5f + 3 * b.text_scale;
        if (!b.text.empty()) text(b.x + 8 * b.text_scale, baseline, b.text, b.ink[0], b.ink[1], b.ink[2], false, b.text_scale);
        if (!b.right.empty()) text(b.x + b.w - 8 * b.text_scale, baseline, b.right, b.ink[0], b.ink[1], b.ink[2], true, b.text_scale);
    }
}

}  // namespace crusader
