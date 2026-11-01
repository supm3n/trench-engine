#include "ui/cursor_draw.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include "formats/image.hpp"

namespace antanta::ui {

struct CursorGraphics {
    SDL_Renderer* renderer = nullptr;
    GameFileLoader load;
    std::array<bool, kCursorCount> tried{};
    std::array<std::optional<Image>, kCursorCount> pic;
    std::array<SDL_Texture*, kCursorCount> tex{};
    std::array<SDL_Cursor*, kCursorCount> cursor{};
    std::array<int, kCursorCount> cursor_scale{};
    int shown = -2;  // the cursor set last: -1 the system's, -2 none yet
    int shown_scale = 0;
    ~CursorGraphics() {
        for (SDL_Texture* t : tex) {
            if (t) SDL_DestroyTexture(t);
        }
        for (SDL_Cursor* c : cursor) {
            if (c) SDL_DestroyCursor(c);
        }
    }
};

void CursorGraphicsFree::operator()(CursorGraphics* g) const { delete g; }

CursorGraphicsPtr cursor_graphics_open(SDL_Renderer* renderer, GameFileLoader load) {
    CursorGraphicsPtr g(new CursorGraphics);
    g->renderer = renderer;
    g->load = std::move(load);
    return g;
}

namespace {


const Image* picture(CursorGraphics& g, Cursor c) {
    const auto k = static_cast<std::size_t>(c);
    if (!g.tried[k]) {
        g.tried[k] = true;
        const std::string path = std::string("Data/Misc/Cursors/") + cursor_file(c);
        std::string err;
        auto bytes = g.load(path, &err);
        if (bytes) g.pic[k] = decode_bmp(*bytes, &err);
        if (!g.pic[k]) {
            SDL_Log("cursor: %s: %s", path.c_str(), err.c_str());
        } else {
            std::vector<std::uint8_t>& px = g.pic[k]->rgba;
            for (std::size_t i = 0; i + 3 < px.size(); i += 4) {
                if (px[i] == 255 && px[i + 1] == 0 && px[i + 2] == 255) px[i + 3] = 0;
            }
        }
    }
    return g.pic[k] ? &*g.pic[k] : nullptr;
}

}  // namespace

void cursor_show(CursorGraphics& g, Cursor c, int scale) {
    scale = std::clamp(scale, 1, 4);
    const auto k = static_cast<std::size_t>(c);
    if (g.shown == static_cast<int>(k) && g.shown_scale == scale) return;
    if (g.cursor[k] && g.cursor_scale[k] != scale) {
        if (g.shown == static_cast<int>(k)) SDL_SetCursor(SDL_GetDefaultCursor());
        SDL_DestroyCursor(g.cursor[k]);
        g.cursor[k] = nullptr;
    }
    if (!g.cursor[k]) {
        const Image* img = picture(g, c);
        if (img == nullptr) {
            cursor_show_system(g);
            return;
        }
        SDL_Surface* sf = SDL_CreateSurface(img->width * scale, img->height * scale, SDL_PIXELFORMAT_ABGR8888);
        if (sf == nullptr) {
            cursor_show_system(g);
            return;
        }
        for (int y = 0; y < sf->h; ++y) {
            auto* row = static_cast<std::uint8_t*>(sf->pixels) + static_cast<std::ptrdiff_t>(y) * sf->pitch;
            for (int x = 0; x < sf->w; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y / scale) * img->width + x / scale) * 4;
                std::copy_n(img->rgba.data() + i, 4, row + static_cast<std::ptrdiff_t>(x) * 4);
            }
        }
        g.cursor[k] = SDL_CreateColorCursor(sf, 0, 0);  // hot spot (0, 0), as every cursor of the original (D.1)
        g.cursor_scale[k] = scale;
        SDL_DestroySurface(sf);
        if (!g.cursor[k]) {
            cursor_show_system(g);
            return;
        }
    }
    SDL_SetCursor(g.cursor[k]);
    g.shown = static_cast<int>(k), g.shown_scale = scale;
}

void cursor_show_system(CursorGraphics& g) {
    if (g.shown == -1) return;
    SDL_SetCursor(SDL_GetDefaultCursor());
    g.shown = -1;
}

void cursor_draw(CursorGraphics& g, Cursor c, float x, float y) {
    const auto k = static_cast<std::size_t>(c);
    if (!g.tex[k]) {
        const Image* img = picture(g, c);
        if (img == nullptr) return;
        SDL_Texture* t =
            SDL_CreateTexture(g.renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, img->width, img->height);
        if (!t) return;
        SDL_UpdateTexture(t, nullptr, img->rgba.data(), img->width * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
        g.tex[k] = t;
    }
    float w = 0, h = 0;
    SDL_GetTextureSize(g.tex[k], &w, &h);
    const SDL_FRect dst{x, y, w, h};
    SDL_RenderTexture(g.renderer, g.tex[k], nullptr, &dst);
}

}  // namespace antanta::ui
