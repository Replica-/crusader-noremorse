// The character colour screen and the recoloured player, drawn on the graphics card (see engine/world/colours_overlay.hpp).
#include "platform/sdl_presenter.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace crusader {

namespace {

SDL_Color unpack(std::uint32_t c) {
    return SDL_Color{static_cast<Uint8>(c & 0xFF), static_cast<Uint8>((c >> 8) & 0xFF), static_cast<Uint8>((c >> 16) & 0xFF), static_cast<Uint8>(c >> 24)};
}

SDL_Color mix(const SDL_Color& a, const SDL_Color& b, float t) {
    auto lerp = [t](int x, int y) { return static_cast<Uint8>(static_cast<float>(x) + static_cast<float>(y - x) * t + 0.5f); };
    return SDL_Color{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), lerp(a.a, b.a)};
}

}  // namespace

// @orig none: port-only player colours
void SdlPresenter::draw_colours() {
    if (!colours_ || !colours_->on) return;
    // The screen is laid out for 640 x 480, centred in the view like the other menu screens.
    const float ox = std::floor(static_cast<float>(width_ - 640) * 0.5f), oy = std::floor(static_cast<float>(height_ - 480) * 0.5f);
    {
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
        const SDL_FRect all{0, 0, static_cast<float>(width_), static_cast<float>(height_)};
        SDL_RenderFillRectF(renderer_, &all);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (ColourRect r : colours_->rects) {
        r.x += ox, r.y += oy;
        const SDL_Color tl = unpack(r.corner[0]), tr = unpack(r.corner[1]), bl = unpack(r.corner[2]), br = unpack(r.corner[3]);
        if (r.outline) {
            SDL_SetRenderDrawColor(renderer_, tl.r, tl.g, tl.b, tl.a);
            const SDL_FRect bars[4] = {{r.x, r.y, r.w, 2}, {r.x, r.y + r.h - 2, r.w, 2}, {r.x, r.y, 2, r.h}, {r.x + r.w - 2, r.y, 2, r.h}};
            SDL_RenderFillRectsF(renderer_, bars, 4);
            continue;
        }
        if (r.corner[0] == r.corner[1] && r.corner[0] == r.corner[2] && r.corner[0] == r.corner[3]) {
            SDL_SetRenderDrawColor(renderer_, tl.r, tl.g, tl.b, tl.a);
            const SDL_FRect f{r.x, r.y, r.w, r.h};
            SDL_RenderFillRectF(renderer_, &f);
            continue;
        }
        // A gradient: a grid of cells whose corners take the bilinear mix of the four colours.
        constexpr int kN = 14;
        std::vector<SDL_Vertex> v;
        std::vector<int> idx;
        for (int j = 0; j <= kN; ++j) {
            for (int i = 0; i <= kN; ++i) {
                const float u = static_cast<float>(i) / kN, w = static_cast<float>(j) / kN;
                v.push_back({{r.x + r.w * u, r.y + r.h * w}, mix(mix(tl, tr, u), mix(bl, br, u), w), {0, 0}});
            }
        }
        for (int j = 0; j < kN; ++j) {
            for (int i = 0; i < kN; ++i) {
                const int a = j * (kN + 1) + i, b = a + 1, c = a + kN + 1, d = c + 1;
                idx.insert(idx.end(), {a, b, c, b, d, c});
            }
        }
        SDL_RenderGeometry(renderer_, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
    }
    // The character.
    if (colours_->iw > 0 && colours_->ih > 0 &&
        colours_->image.size() >= static_cast<std::size_t>(colours_->iw) * static_cast<std::size_t>(colours_->ih)) {
        if (colours_key_ != colours_->image_key || !colours_tex_) {
            if (colours_tex_) SDL_DestroyTexture(colours_tex_);
            colours_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, colours_->iw, colours_->ih);
            if (colours_tex_) {
                SDL_UpdateTexture(colours_tex_, nullptr, colours_->image.data(), colours_->iw * 4);
                SDL_SetTextureBlendMode(colours_tex_, SDL_BLENDMODE_BLEND);
                SDL_SetTextureScaleMode(colours_tex_, SDL_ScaleModeNearest);
            }
            colours_key_ = colours_->image_key;
        }
        if (colours_tex_) {
            const float s = colours_->iscale;
            const SDL_FRect dst{ox + colours_->ix - static_cast<float>(colours_->ax) * s, oy + colours_->iy - static_cast<float>(colours_->ay) * s,
                                static_cast<float>(colours_->iw) * s, static_cast<float>(colours_->ih) * s};
            SDL_RenderCopyF(renderer_, colours_tex_, nullptr, &dst);
        }
    }
    for (const ColourLabel& l : colours_->labels) {
        const float ly = oy + l.y - 9.0f * l.scale;  // the labels give a baseline, text() wants the top
        if (l.align == 1) text(ox + l.x - text_width(l.text, l.scale) * 0.5f, ly, l.text, l.r, l.g, l.b, false, l.scale);
        else text(ox + l.x, ly, l.text, l.r, l.g, l.b, l.align == 2, l.scale);
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

// The player in its chosen colours, over the frame, where nothing hides it. The colours follow the palette's fades.
// @orig none: port-only player colours
void SdlPresenter::draw_player_layer(const Palette& current) {
    if (!player_ || !player_->on || player_->w <= 0 || player_->h <= 0) return;
    const PlayerLayer& l = *player_;
    if (player_w_ != l.w || player_h_ != l.h || !player_tex_) {
        if (player_tex_) SDL_DestroyTexture(player_tex_);
        player_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, l.w, l.h);
        if (!player_tex_) return;
        SDL_SetTextureBlendMode(player_tex_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(player_tex_, SDL_ScaleModeNearest);
        player_w_ = l.w, player_h_ = l.h;
    }
    std::array<std::uint32_t, 256> lut{};
    for (std::size_t i = 0; i < 256; ++i) {
        const std::uint32_t c = l.recolour[i];
        if ((c >> 24) == 0) continue;
        const Rgb& now = current.colors[i];
        const Rgb& base = l.base.colors[i];
        const float f = std::clamp(static_cast<float>(now.r + now.g + now.b + 3) / static_cast<float>(base.r + base.g + base.b + 3), 0.0f, 1.3f);
        auto chan = [&](std::uint32_t v) { return gamma_table_[static_cast<std::size_t>(std::min(255.0f, static_cast<float>(v) * f + 0.5f))]; };
        // c holds bytes R, G, B, A: red in the low byte.
        lut[i] = 0xFF000000u | static_cast<std::uint32_t>(chan((c >> 16) & 0xFF)) << 16 | static_cast<std::uint32_t>(chan((c >> 8) & 0xFF)) << 8 |
                 chan(c & 0xFF);
    }
    void* pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(player_tex_, nullptr, &pixels, &pitch) != 0) return;
    for (int y = 0; y < l.h; ++y) {
        auto* dst = reinterpret_cast<Uint32*>(static_cast<Uint8*>(pixels) + y * pitch);
        for (int x = 0; x < l.w; ++x) {
            const std::uint8_t p = l.index[static_cast<std::size_t>(y) * static_cast<std::size_t>(l.w) + static_cast<std::size_t>(x)];
            dst[x] = p == 255 ? 0u : lut[p];
        }
    }
    SDL_UnlockTexture(player_tex_);
    const SDL_FRect dst{static_cast<float>(l.x), static_cast<float>(l.y), static_cast<float>(l.w), static_cast<float>(l.h)};
    SDL_RenderCopyF(renderer_, player_tex_, nullptr, &dst);
}

}  // namespace crusader
