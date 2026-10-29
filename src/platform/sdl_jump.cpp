// The targeted jump's preview (J held), drawn on the graphics card over the game: a dashed curve for the flight, a ring on the
// ground where the player lands and a translucent copy of the player standing there.
#include "platform/sdl_presenter.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace crusader {

// @orig none: port-only targeted jump
void SdlPresenter::draw_jump() {
    if (!jump_ || !jump_->on || jump_->arc.size() < 2) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);

    // A thick line segment as two triangles.
    auto bar = [&](float x0, float y0, float x1, float y1, float w, SDL_Color c) {
        const float dx = x1 - x0, dy = y1 - y0, len = std::sqrt(dx * dx + dy * dy);
        if (len < 0.01f) return;
        const float nx = -dy / len * w * 0.5f, ny = dx / len * w * 0.5f;
        const SDL_Vertex v[4] = {{{x0 + nx, y0 + ny}, c, {0, 0}}, {{x0 - nx, y0 - ny}, c, {0, 0}},
                                 {{x1 + nx, y1 + ny}, c, {0, 0}}, {{x1 - nx, y1 - ny}, c, {0, 0}}};
        const int idx[6] = {0, 1, 2, 2, 1, 3};
        SDL_RenderGeometry(renderer_, nullptr, v, 4, idx, 6);
    };

    // The landing ring: the footprint circle seen from the iso view (twice as wide as high).
    {
        constexpr int kSegments = 28;
        const float rx = 15.0f, ry = 7.5f;
        for (int i = 0; i < kSegments; ++i) {
            const float a0 = 6.2831853f * static_cast<float>(i) / kSegments, a1 = 6.2831853f * static_cast<float>(i + 1) / kSegments;
            bar(jump_->x + std::cos(a0) * rx, jump_->y + std::sin(a0) * ry, jump_->x + std::cos(a1) * rx, jump_->y + std::sin(a1) * ry, 1.6f,
                SDL_Color{120, 230, 255, 200});
        }
    }

    // The dashed flight: the dashes run along the curve, 7 pixels drawn then 5 left out.
    {
        constexpr float kDash = 7.0f, kGap = 5.0f;
        float into = 0.0f;  // distance into the current dash / gap
        bool drawing = true;
        for (std::size_t i = 1; i < jump_->arc.size(); ++i) {
            float x0 = jump_->arc[i - 1].first, y0 = jump_->arc[i - 1].second;
            const float x1 = jump_->arc[i].first, y1 = jump_->arc[i].second;
            float left = std::sqrt((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
            if (left < 0.01f) continue;
            const float ux = (x1 - x0) / left, uy = (y1 - y0) / left;
            while (left > 0.0f) {
                const float limit = drawing ? kDash : kGap;
                const float run = std::min(left, limit - into);
                const float ex = x0 + ux * run, ey = y0 + uy * run;
                if (drawing) {
                    bar(x0, y0 + 1.0f, ex, ey + 1.0f, 3.0f, SDL_Color{0, 0, 0, 90});  // a shadow under
                    bar(x0, y0, ex, ey, 2.2f, SDL_Color{150, 240, 255, 235});
                }
                x0 = ex, y0 = ey, left -= run, into += run;
                if (into >= limit - 0.001f) into = 0.0f, drawing = !drawing;
            }
        }
    }

    // The ghost: the picture of the player at the landing, half see-through.
    if (jump_->width > 0 && jump_->height > 0 && jump_->rgba.size() >= static_cast<std::size_t>(jump_->width) * static_cast<std::size_t>(jump_->height)) {
        if (jump_key_ != jump_->key || !jump_tex_) {
            if (jump_tex_) SDL_DestroyTexture(jump_tex_);
            jump_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, jump_->width, jump_->height);
            if (jump_tex_) {
                SDL_UpdateTexture(jump_tex_, nullptr, jump_->rgba.data(), jump_->width * 4);
                SDL_SetTextureBlendMode(jump_tex_, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(jump_tex_, SDL_ScaleModeNearest);
            }
            jump_key_ = jump_->key;
        }
        if (jump_tex_) {
            const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
            SDL_SetTextureAlphaMod(jump_tex_, static_cast<Uint8>(120 + 25 * std::sin(t * 6.0f)));
            const float w = static_cast<float>(jump_->width), h = static_cast<float>(jump_->height);
            const float left = jump_->mirrored ? jump_->x + static_cast<float>(jump_->xoff) - (w - 1.0f) : jump_->x - static_cast<float>(jump_->xoff);
            const SDL_FRect dst{left, jump_->y - static_cast<float>(jump_->yoff), w, h};
            SDL_RenderCopyExF(renderer_, jump_tex_, nullptr, &dst, 0.0, nullptr, jump_->mirrored ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
        }
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

}  // namespace crusader
