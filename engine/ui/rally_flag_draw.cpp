#include "ui/rally_flag_draw.hpp"

#include <optional>
#include <string>

#include "formats/image.hpp"

namespace antanta::ui {

struct RallyGraphics {
    SDL_Renderer* renderer = nullptr;
    GameFileLoader load;
    bool loaded = false;
    std::array<SDL_Texture*, 8> cloth{}, pole{};
    ~RallyGraphics() {
        for (SDL_Texture* t : cloth) {
            if (t) SDL_DestroyTexture(t);
        }
        for (SDL_Texture* t : pole) {
            if (t) SDL_DestroyTexture(t);
        }
    }
};

void RallyGraphicsFree::operator()(RallyGraphics* g) const { delete g; }

RallyGraphicsPtr rally_graphics_open(SDL_Renderer* renderer, GameFileLoader load) {
    RallyGraphicsPtr g(new RallyGraphics);
    g->renderer = renderer;
    g->load = std::move(load);
    return g;
}

namespace {

// A BMP of Data/Misc/Cursors with magenta transparent, as RGBA (empty: not read, logged).
std::optional<Image> read_picture(RallyGraphics& g, const std::string& path) {
    std::string err;
    auto bytes = g.load(path, &err);
    std::optional<Image> img;
    if (bytes) img = decode_bmp(*bytes, &err);
    if (!img) {
        SDL_Log("rally flag: %s: %s", path.c_str(), err.c_str());
        return img;
    }
    for (std::size_t i = 0; i + 3 < img->rgba.size(); i += 4) {
        if (img->rgba[i] == 255 && img->rgba[i + 1] == 0 && img->rgba[i + 2] == 255) img->rgba[i + 3] = 0;
    }
    return img;
}

SDL_Texture* make_texture(RallyGraphics& g, const std::string& path) {
    const auto img = read_picture(g, path);
    if (!img) return nullptr;
    SDL_Texture* t =
        SDL_CreateTexture(g.renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, img->width, img->height);
    if (!t) return nullptr;
    SDL_UpdateTexture(t, nullptr, img->rgba.data(), img->width * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(t, SDL_SCALEMODE_NEAREST);
    return t;
}

void load_all(RallyGraphics& g) {
    if (g.loaded) return;
    g.loaded = true;
    for (int i = 0; i < 8; ++i) {
        const std::string n = "0" + std::to_string(i + 1) + ".bmp";
        g.cloth[static_cast<std::size_t>(i)] = make_texture(g, "Data/Misc/Cursors/Polotnishe/" + n);
        g.pole[static_cast<std::size_t>(i)] = make_texture(g, "Data/Misc/Cursors/Flagshtok/" + n);
    }
}

}  // namespace

void rally_flag_draw(RallyGraphics& g, float x, float y, int frame, std::uint32_t rgb) {
    load_all(g);
    const auto k = static_cast<std::size_t>(frame & 7);
    const SDL_FRect dst{x - 16, y - 16, 32, 32};
    if (SDL_Texture* t = g.cloth[k]) {
        SDL_SetTextureColorMod(t, static_cast<Uint8>(rgb >> 16), static_cast<Uint8>(rgb >> 8), static_cast<Uint8>(rgb));
        SDL_RenderTexture(g.renderer, t, nullptr, &dst);
    }
    if (SDL_Texture* t = g.pole[k]) SDL_RenderTexture(g.renderer, t, nullptr, &dst);
}

}  // namespace antanta::ui
