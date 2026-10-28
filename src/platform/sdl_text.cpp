// Text drawn by the graphics card: the game's font as a texture of white glyphs, tinted when drawn.
#include "platform/sdl_presenter.hpp"

#include <SDL.h>

#include <algorithm>

namespace crusader {

// @orig none: port-only
void SdlPresenter::set_font(const FontAtlas& atlas) {
    if (!atlas.valid() || !renderer_) return;
    atlas_ = atlas;
    if (font_tex_) SDL_DestroyTexture(font_tex_);
    font_tex_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, atlas.width, atlas.height);
    if (!font_tex_) return;
    SDL_UpdateTexture(font_tex_, nullptr, atlas_.rgba.data(), atlas.width * 4);
    SDL_SetTextureBlendMode(font_tex_, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(font_tex_, SDL_ScaleModeNearest);
}

float SdlPresenter::text_width(const std::string& s, float scale) const {
    float w = 0;
    for (const unsigned char c : s) w += static_cast<float>(c < 128 ? atlas_.glyphs[c].advance : 0);
    return w * scale;
}

void SdlPresenter::text(float x, float y, const std::string& s, std::uint8_t r, std::uint8_t g, std::uint8_t b, bool right_aligned,
                        float scale) {
    if (!font_tex_ || s.empty()) return;
    if (right_aligned) x -= text_width(s, scale);
    texts_.push_back({x, y, scale, s, r, g, b});
}

// Every queued line, with a dark shadow so it can be read over anything.
void SdlPresenter::draw_texts() {
    if (!font_tex_) {
        texts_.clear();
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (const TextItem& t : texts_) {
        for (int pass = 0; pass < 2; ++pass) {
            const float shadow = pass == 0 ? t.scale : 0.0f;  // the shadow first, a pixel down and right
            SDL_SetTextureColorMod(font_tex_, pass == 0 ? 0 : t.r, pass == 0 ? 0 : t.g, pass == 0 ? 0 : t.b);
            SDL_SetTextureAlphaMod(font_tex_, pass == 0 ? 190 : 255);
            float pen = t.x;
            for (const unsigned char c : t.s) {
                if (c >= 128) continue;
                const FontAtlas::Glyph& g = atlas_.glyphs[c];
                if (g.w > 0) {
                    const SDL_Rect src{g.x, g.y, g.w, g.h};
                    const SDL_FRect dst{pen + static_cast<float>(g.dx) * t.scale + shadow, t.y + static_cast<float>(g.dy) * t.scale + shadow,
                                        static_cast<float>(g.w) * t.scale, static_cast<float>(g.h) * t.scale};
                    SDL_RenderCopyF(renderer_, font_tex_, &src, &dst);
                }
                pen += static_cast<float>(g.advance) * t.scale;
            }
        }
    }
    texts_.clear();
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
}

}  // namespace crusader
