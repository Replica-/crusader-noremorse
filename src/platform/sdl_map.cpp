// The mission map (M), drawn on the graphics card over the game: a dark panel, the seen part of the level as diamonds, round
// markers for what matters, and a legend and hints in the panel (text drawn by the presenter, not from the game's palette).
#include "platform/sdl_presenter.hpp"

#include <SDL.h>

#include <algorithm>
#include <cmath>

namespace crusader {

namespace {

SDL_Color marker_colour(int kind) {
    switch (kind) {
    case 0: return {90, 255, 130, 255};    // you
    case 1: return {255, 70, 55, 255};     // enemy
    case 2: return {70, 225, 255, 255};    // key item
    case 3: return {255, 175, 55, 255};    // weapon or ammunition
    case 4: return {110, 150, 255, 255};   // something to use
    default: return {235, 100, 235, 255};  // trigger
    }
}

}  // namespace

// @orig none: port-only mission map
void SdlPresenter::draw_map() {
    if (!map_ || !map_->on) return;
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    const MapPanel pn = map_panel(width_, height_);
    const SDL_FRect panel{pn.x, pn.y, pn.w, pn.h};
    SDL_SetRenderDrawColor(renderer_, 5, 9, 15, 232);
    SDL_RenderFillRectF(renderer_, &panel);

    // The view: the map pixel `center` at the middle of the panel, `zoom` screen pixels to a map pixel.
    const float zoom = map_->zoom;
    const float ox = pn.x + pn.w * 0.5f - map_->center_x * zoom, oy = pn.y + pn.h * 0.5f - map_->center_y * zoom;
    const SDL_Rect clip{static_cast<int>(pn.x) + 1, static_cast<int>(pn.y) + 1, static_cast<int>(pn.w) - 2, static_cast<int>(pn.h) - 2};
    SDL_RenderSetClipRect(renderer_, &clip);

    // The footprints that touch the panel, one batch.
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    for (const MapPoly& p : map_->polys) {
        float sx[4], sy[4];
        float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
        for (int i = 0; i < 4; ++i) {
            sx[i] = ox + p.x[i] * zoom, sy[i] = oy + p.y[i] * zoom;
            lo_x = std::min(lo_x, sx[i]), hi_x = std::max(hi_x, sx[i]);
            lo_y = std::min(lo_y, sy[i]), hi_y = std::max(hi_y, sy[i]);
        }
        if (hi_x < pn.x || lo_x > pn.x + pn.w || hi_y < pn.y || lo_y > pn.y + pn.h) continue;
        const int base = static_cast<int>(vertices.size());
        for (int i = 0; i < 4; ++i) vertices.push_back({{sx[i], sy[i]}, SDL_Color{p.r, p.g, p.b, p.a}, {0, 0}});
        for (const int k : {0, 1, 2, 0, 2, 3}) indices.push_back(base + k);
    }
    if (!vertices.empty()) {
        SDL_RenderGeometry(renderer_, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(),
                           static_cast<int>(indices.size()));
    }

    // The markers: triggers first (behind), you last.
    auto disc = [&](float cx, float cy, float r, SDL_Color c) {
        constexpr int kSegments = 16;
        SDL_Vertex v[kSegments + 1];
        int idx[kSegments * 3];
        v[0] = {{cx, cy}, c, {0, 0}};
        for (int i = 0; i < kSegments; ++i) {
            const float a = 6.2831853f * static_cast<float>(i) / kSegments;
            v[i + 1] = {{cx + std::cos(a) * r, cy + std::sin(a) * r}, c, {0, 0}};
            idx[i * 3] = 0;
            idx[i * 3 + 1] = i + 1;
            idx[i * 3 + 2] = (i + 1) % kSegments + 1;
        }
        SDL_RenderGeometry(renderer_, nullptr, v, kSegments + 1, idx, kSegments * 3);
    };
    for (const int pass : {5, 3, 4, 2, 1, 0}) {
        for (const MapMarker& m : map_->markers) {
            if (m.kind != pass) continue;
            const float cx = ox + m.x * zoom, cy = oy + m.y * zoom;
            if (cx < pn.x - 12 || cx > pn.x + pn.w + 12 || cy < pn.y - 12 || cy > pn.y + pn.h + 12) continue;
            const SDL_Color c = marker_colour(m.kind);
            const float r = m.kind == 0 ? 6.0f : m.kind == 5 ? 4.0f : m.kind == 3 ? 3.0f : 4.2f;
            disc(cx, cy, r + 1.5f, SDL_Color{0, 0, 0, 210});
            disc(cx, cy, r, c);
            if (m.kind == 0) {  // which way you face
                const float hl = std::sqrt(map_->heading_x * map_->heading_x + map_->heading_y * map_->heading_y);
                if (hl > 0.01f) {
                    const float dx = map_->heading_x / hl, dy = map_->heading_y / hl;
                    SDL_SetRenderDrawColor(renderer_, 90, 255, 130, 255);
                    SDL_RenderDrawLineF(renderer_, cx, cy, cx + dx * 20.0f, cy + dy * 20.0f);
                    SDL_RenderDrawLineF(renderer_, cx + 1, cy, cx + dx * 20.0f + 1, cy + dy * 20.0f);
                }
            }
        }
    }
    SDL_RenderSetClipRect(renderer_, nullptr);

    // The frame, the legend down the left (a dot and a label each) and the hints along the bottom.
    SDL_SetRenderDrawColor(renderer_, 120, 150, 180, 235);
    SDL_RenderDrawRectF(renderer_, &panel);
    static const char* const labels[6] = {"YOU", "ENEMY", "KEY ITEM", "WEAPON / AMMO", "USE", "TRIGGER"};
    const SDL_FRect legend_back{pn.x + 8.0f, pn.y + 8.0f, 168.0f, 8.0f * 20.0f + 12.0f};
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 140);
    SDL_RenderFillRectF(renderer_, &legend_back);
    for (int i = 0; i < 6; ++i) {
        const float y = pn.y + 26.0f + 20.0f * static_cast<float>(i);
        disc(pn.x + 22.0f, y + 3.0f, 4.5f, marker_colour(i));
        text(pn.x + 36.0f, y, labels[i], 255, 255, 255);
    }
    // Two more entries for the doors, drawn as squares in the colours the map uses for them.
    for (int i = 0; i < 2; ++i) {
        const float y = pn.y + 26.0f + 20.0f * static_cast<float>(6 + i);
        const SDL_FRect swatch{pn.x + 17.5f, y - 2.0f, 9.0f, 9.0f};
        SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 220);
        const SDL_FRect rim{swatch.x - 1.5f, swatch.y - 1.5f, 12.0f, 12.0f};
        SDL_RenderFillRectF(renderer_, &rim);
        if (i == 0) SDL_SetRenderDrawColor(renderer_, 255, 170, 40, 255);
        else SDL_SetRenderDrawColor(renderer_, 200, 95, 255, 255);
        SDL_RenderFillRectF(renderer_, &swatch);
        text(pn.x + 36.0f, y, i == 0 ? "DOOR" : "FORCE FIELD", 255, 255, 255);
    }
    text(pn.x + 10.0f, pn.y + pn.h - 12.0f, "WHEEL ZOOM   DRAG PAN   MIDDLE CLICK RECENTRE   M CLOSE", 200, 215, 235);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

}  // namespace crusader
