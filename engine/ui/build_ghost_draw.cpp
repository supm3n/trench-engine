#include "ui/build_ghost_draw.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <string>
#include <vector>

namespace antanta::ui {

struct GhostGraphics {
    SDL_Renderer* renderer = nullptr;
    std::map<std::array<int, 2>, SDL_Texture*> textures;  // (nation, type); nullptr: tried, no picture
    std::map<int, SDL_Texture*> fields;                   // by render/ground_fx anim_index; nullptr: no picture
    std::map<int, SDL_Texture*> specials;                 // by TrenchAndPonton entry id; nullptr: no picture
    ~GhostGraphics() {
        for (auto& [k, t] : specials) {
            if (t) SDL_DestroyTexture(t);
        }
        for (auto& [k, t] : textures) {
            if (t) SDL_DestroyTexture(t);
        }
        for (auto& [k, t] : fields) {
            if (t) SDL_DestroyTexture(t);
        }
    }
};

void GhostGraphicsFree::operator()(GhostGraphics* g) const { delete g; }

GhostGraphicsPtr ghost_graphics_open(SDL_Renderer* renderer) {
    GhostGraphicsPtr g(new GhostGraphics);
    g->renderer = renderer;
    return g;
}

namespace {


SDL_Texture* ghost_texture(GhostGraphics& gg, WorldGraphics& world, const GameFileLoader& load, int nation,
                           std::uint16_t type, int w, int h) {
    const std::array<int, 2> key{nation, type};
    if (auto it = gg.textures.find(key); it != gg.textures.end()) return it->second;
    SDL_Texture*& tex = gg.textures[key];
    // The finished house's graphic (phase 0) through the static layer's loader, as one house of player 0.
    MapHouse one;
    one.type = static_cast<std::uint8_t>(type);
    WorldView view;
    view.houses = std::span<const MapHouse>(&one, 1);
    view.nations = {nation};
    view.house_graphic = {0};
    std::vector<std::string> warnings;
    load_view_graphics(world, view, load, &warnings);
    for (const std::string& wn : warnings) SDL_Log("build ghost: %s", wn.c_str());
    const auto it = world.houses.find({nation, type, 0});
    if (it == world.houses.end() || it->second.picture.width == 0 || w <= 0 || h <= 0) return tex;
    const Image& pic = it->second.picture;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    const int cw = std::min(w, pic.width), ch = std::min(h, pic.height);
    for (int y = 0; y < ch; ++y) {
        std::copy_n(pic.rgba.begin() + static_cast<std::ptrdiff_t>(y) * pic.width * 4, static_cast<std::size_t>(cw) * 4,
                    rgba.begin() + static_cast<std::ptrdiff_t>(y) * w * 4);
    }
    tex = SDL_CreateTexture(gg.renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!tex) return tex;
    SDL_UpdateTexture(tex, nullptr, rgba.data(), w * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    return tex;
}



SDL_Texture* field_texture(GhostGraphics& gg, const WorldGraphics& world, int anim) {
    if (auto it = gg.fields.find(anim); it != gg.fields.end()) return it->second;
    SDL_Texture*& tex = gg.fields[anim];
    if (anim < 0 || static_cast<std::size_t>(anim) >= world.fx.anims.size()) return tex;
    const GroundFxGraphics::Anim& an = world.fx.anims[static_cast<std::size_t>(anim)];
    if (an.pics.empty() || an.pics[0].w <= 0 || an.pics[0].h <= 0) return tex;
    const GroundFxGraphics::AnimFrame& f = an.pics[0];
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(f.w) * static_cast<std::size_t>(f.h) * 4, 0);
    for (std::size_t i = 0; i < f.rgb.size() && i < f.opaque.size(); ++i) {
        for (std::size_t c = 0; c < 3; ++c) {
            rgba[i * 4 + c] = static_cast<std::uint8_t>(std::clamp(f.rgb[i][c], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        rgba[i * 4 + 3] = f.opaque[i] ? 255 : 0;
    }
    tex = SDL_CreateTexture(gg.renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, f.w, f.h);
    if (!tex) return tex;
    SDL_UpdateTexture(tex, nullptr, rgba.data(), f.w * 4);
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    return tex;
}

}  // namespace

void pontoon_preview_draw(GhostGraphics& gg, const WorldGraphics& world, std::span<const sim::PontoonCell> cells,
                          int vx, int vy, bool red) {
    for (const sim::PontoonCell& c : cells) {
        const int id = 10 * c.kind;  // the finished picture
        auto [it, fresh] = gg.specials.try_emplace(id, nullptr);
        if (fresh) {
            const auto pic = world.specials.find(id);
            if (pic == world.specials.end()) continue;
            const Texture& t = pic->second;
            std::vector<std::uint8_t> rgba(t.rgba.size());
            for (std::size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<std::uint8_t>(t.rgba[i] * 17);
            it->second =
                SDL_CreateTexture(gg.renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, t.width, t.height);
            if (!it->second) continue;
            SDL_UpdateTexture(it->second, nullptr, rgba.data(), t.width * 4);
            SDL_SetTextureBlendMode(it->second, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(it->second, SDL_SCALEMODE_NEAREST);
        }
        SDL_Texture* tex = it->second;
        if (!tex) continue;
        SDL_SetTextureColorMod(tex, 0xff, red ? 0 : 0xff, red ? 0 : 0xff);  // as ghost_colour of a refused house
        float w = 0, h = 0;
        SDL_GetTextureSize(tex, &w, &h);
        const int cx = (c.x * 2 - (~c.y & 1)) * 48 + (c.kind == 0x15 ? -0x4f : -0x30) + 0x70, cy = c.y * 24 - 0x44;
        const SDL_FRect dst{static_cast<float>(cx - static_cast<int>(w) / 2 - vx),
                            static_cast<float>(cy - static_cast<int>(h) / 2 - vy), w, h};
        SDL_RenderTexture(gg.renderer, tex, nullptr, &dst);
    }
}

void ghost_draw(GhostGraphics& gg, WorldGraphics& world, const GameFileLoader& load, const BuildGhost& g, int nation,
                std::uint16_t type, std::uint32_t rgb, int vx, int vy) {
    SDL_Renderer* r = gg.renderer;
    const auto cr = static_cast<Uint8>(rgb >> 16), cg = static_cast<Uint8>(rgb >> 8), cb = static_cast<Uint8>(rgb);

    const int anim = anim_index(g.field_object);
    if (!g.fields.empty() && anim >= 0) {
        const GroundFxGraphics::Anim& an = world.fx.anims[static_cast<std::size_t>(anim)];
        if (SDL_Texture* ft = field_texture(gg, world, anim)) {
            SDL_SetTextureColorMod(ft, cr, cg, cb);
            for (const BuildGhost::Field& f : g.fields) {
                const SDL_FRect dst{static_cast<float>(f.x - (an.w >> 1) - vx), static_cast<float>(f.y - an.h - vy),
                                    static_cast<float>(an.w >> 1 << 1), static_cast<float>(an.h)};
                SDL_RenderTexture(r, ft, nullptr, &dst);
            }
        }
    }
    SDL_Texture* tex = ghost_texture(gg, world, load, nation, type, g.w, g.h);
    if (!tex) return;
    SDL_SetTextureColorMod(tex, cr, cg, cb);
    const SDL_FRect dst{static_cast<float>(g.x - vx), static_cast<float>(g.y - vy), static_cast<float>(g.w),
                        static_cast<float>(g.h)};
    SDL_RenderTexture(r, tex, nullptr, &dst);
}

}  // namespace antanta::ui
