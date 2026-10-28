// The weapon wheel (Tab) and the item wheel (I), drawn on the graphics card over the game: a ring of sectors, one for each thing
// carried, the one under the pointer lit, the one in hand outlined in gold, the names round the outside.
#include "platform/sdl_presenter.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace crusader {

// @orig none: port-only wheels
void SdlPresenter::draw_wheel() {
    if (!wheel_ || !wheel_->on) return;
    constexpr float kPi = 3.14159265f;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const float W = static_cast<float>(width_), H = static_cast<float>(height_);
    // The game dims behind it.
    const SDL_FRect all{0, 0, W, H};
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 125);
    SDL_RenderFillRectF(renderer_, &all);

    const float cx = wheel_->cx, cy = wheel_->cy;
    // Room for the names: a smaller wheel in a small view.
    const float scale = std::min(1.0f, std::min(W, H) / 560.0f);
    const float inner = wheel_->inner * scale, outer = wheel_->outer * scale;
    const std::size_t n = wheel_->entries.size();

    // A ring sector from angle a0 to a1 (0 at the top, clockwise) between two radii.
    auto sector = [&](float a0, float a1, float r0, float r1, SDL_Color c) {
        const int steps = std::max(2, static_cast<int>((a1 - a0) / 0.07f));
        std::vector<SDL_Vertex> v;
        std::vector<int> idx;
        for (int i = 0; i <= steps; ++i) {
            const float a = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(steps);
            const float s = std::sin(a), co = -std::cos(a);
            v.push_back({{cx + s * r0, cy + co * r0}, c, {0, 0}});
            v.push_back({{cx + s * r1, cy + co * r1}, c, {0, 0}});
            if (i > 0) {
                const int k = 2 * i;
                idx.insert(idx.end(), {k - 2, k - 1, k, k, k - 1, k + 1});
            }
        }
        SDL_RenderGeometry(renderer_, nullptr, v.data(), static_cast<int>(v.size()), idx.data(), static_cast<int>(idx.size()));
    };
    auto disc = [&](float r, SDL_Color c) {
        sector(0.0f, 2.0f * kPi - 0.001f, 0.0f, r, c);
    };

    disc(inner - 4.0f, SDL_Color{8, 12, 20, 235});
    if (n > 0) {
        const float span = 2.0f * kPi / static_cast<float>(n);
        const float gap = std::min(0.03f, span * 0.12f);
        for (std::size_t i = 0; i < n; ++i) {
            const WheelEntry& e = wheel_->entries[i];
            const float a0 = span * static_cast<float>(i) + gap, a1 = span * static_cast<float>(i + 1) - gap;
            const bool hover = static_cast<int>(i) == wheel_->hover;
            SDL_Color base = e.stashed ? SDL_Color{34, 26, 48, 215} : SDL_Color{18, 30, 46, 215};
            if (hover) base = SDL_Color{52, 118, 205, 240};
            sector(a0, a1, inner, outer, base);
            if (e.active) sector(a0, a1, outer - 4.0f, outer, SDL_Color{255, 205, 70, 255});  // the one in hand
        }
        // The pictures, in the sectors, as big as the sector lets them be.
        for (std::size_t i = 0; i < n; ++i) {
            const WheelEntry& e = wheel_->entries[i];
            if (e.icon < 0 || static_cast<std::size_t>(e.icon) >= wheel_icons_.size() || !wheel_icons_[static_cast<std::size_t>(e.icon)].texture) continue;
            const IconTexture& it = wheel_icons_[static_cast<std::size_t>(e.icon)];
            const float mid = span * (static_cast<float>(i) + 0.5f);
            const float radius = (inner + outer) * 0.5f;
            const float arc = radius * span * 0.85f;  // room along the ring
            const float room = std::min(outer - inner - 12.0f, std::max(arc, 24.0f));
            const float k = std::min(room / static_cast<float>(std::max(it.width, it.height)), 2.5f);
            const float w = static_cast<float>(it.width) * k, h = static_cast<float>(it.height) * k;
            const float px = cx + std::sin(mid) * radius, py = cy - std::cos(mid) * radius;
            const SDL_FRect dst{px - w * 0.5f, py - h * 0.5f, w, h};
            SDL_SetTextureAlphaMod(it.texture, e.stashed ? 190 : 255);
            SDL_RenderCopyF(renderer_, it.texture, nullptr, &dst);
        }
        // The names, round the outside: left of the wheel they end at it, right of it they start from it.
        for (std::size_t i = 0; i < n; ++i) {
            const WheelEntry& e = wheel_->entries[i];
            const float mid = span * (static_cast<float>(i) + 0.5f);
            const float s = std::sin(mid), co = -std::cos(mid);
            const float px = cx + s * (outer + 12.0f), py = cy + co * (outer + 12.0f) + 4.0f;
            const bool hover = static_cast<int>(i) == wheel_->hover;
            const std::string label = e.stashed ? e.label + " *" : e.label;
            const std::uint8_t r = hover ? 255 : e.active ? 255 : e.stashed ? 175 : 205;
            const std::uint8_t g = hover ? 255 : e.active ? 215 : e.stashed ? 170 : 220;
            const std::uint8_t b = hover ? 255 : e.active ? 110 : e.stashed ? 205 : 240;
            if (std::abs(s) < 0.2f) text(px - text_width(label) * 0.5f, py + (co > 0 ? 8.0f : -4.0f), label, r, g, b);
            else if (s >= 0) text(px, py, label, r, g, b);
            else text(px, py, label, r, g, b, true);
        }
    }

    // The middle: what the wheel is, and the entry under the pointer.
    auto centred = [&](float y, const std::string& s, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        text(cx - text_width(s) * 0.5f, y, s, r, g, b);
    };
    centred(cy - 10.0f, wheel_->title, 150, 190, 235);
    if (n == 0) {
        centred(cy + 10.0f, "NOTHING CARRIED", 255, 190, 120);
    } else if (wheel_->hover >= 0 && wheel_->hover < static_cast<int>(n)) {
        const WheelEntry& e = wheel_->entries[static_cast<std::size_t>(wheel_->hover)];
        centred(cy + 10.0f, e.label, 255, 255, 255);
        if (!e.detail.empty()) centred(cy + 26.0f, e.detail, 210, 225, 245);
        if (e.stashed) centred(cy + 42.0f, "SWAPS IN", 175, 170, 205);
    }
    centred(H - 24.0f, "CLICK OR ENTER TO CHOOSE   ARROWS MOVE   TAB  I  ESC CLOSE", 190, 205, 225);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

}  // namespace crusader

namespace crusader {

// @orig none: port-only wheels
void SdlPresenter::add_wheel_icons(std::vector<WheelIcon> icons) {
    for (const WheelIcon& icon : icons) {
        if (icon.id < 0 || icon.width <= 0 || icon.height <= 0) continue;
        if (wheel_icons_.size() <= static_cast<std::size_t>(icon.id)) wheel_icons_.resize(static_cast<std::size_t>(icon.id) + 1);
        SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, icon.width, icon.height);
        if (!t) continue;
        SDL_UpdateTexture(t, nullptr, icon.rgba.data(), icon.width * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_ScaleModeNearest);
        wheel_icons_[static_cast<std::size_t>(icon.id)] = {t, icon.width, icon.height};
    }
}

}  // namespace crusader
