#include "ui/world_marks_draw.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "formats/image.hpp"
#include "formats/sprites.hpp"

namespace antanta::ui {

struct MarksGraphics {
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* atlas = nullptr;
    std::vector<SDL_Vertex> vertices;  // kept between frames
    std::vector<int> indices;
    ~MarksGraphics() {
        if (atlas) SDL_DestroyTexture(atlas);
    }
};

void MarksGraphicsFree::operator()(MarksGraphics* g) const { delete g; }

namespace {

// Copies `img` (RGBA8) into the atlas at (x, y), clipped to it.
void blit(std::vector<std::uint8_t>& atlas, const Image& img, int x, int y) {
    for (int r = 0; r < img.height && y + r < kMarksAtlasH; ++r) {
        for (int c = 0; c < img.width && x + c < kMarksAtlasW; ++c) {
            for (int k = 0; k < 4; ++k) {
                atlas[(static_cast<std::size_t>(y + r) * kMarksAtlasW + (x + c)) * 4 + k] =
                    img.rgba[(static_cast<std::size_t>(r) * img.width + c) * 4 + k];
            }
        }
    }
}

std::optional<Image> read(const GameFileLoader& load, const std::string& path) {
    std::string err;
    const auto bytes = load(path, &err);
    std::optional<Image> img;
    if (bytes && path.ends_with(".bmp")) {
        img = decode_bmp(*bytes, &err);
        if (img) {  // magenta is transparent
            for (std::size_t i = 0; i + 3 < img->rgba.size(); i += 4) {
                if (img->rgba[i] == 255 && img->rgba[i + 1] == 0 && img->rgba[i + 2] == 255) img->rgba[i + 3] = 0;
            }
        }
    } else if (bytes) {
        img = decode_444(*bytes, &err);
    }
    if (!img) SDL_Log("world marks: %s: %s", path.c_str(), err.c_str());
    return img;
}

}  // namespace

std::vector<std::uint8_t> marks_atlas(const GameFileLoader& load) {
    std::vector<std::uint8_t> atlas(static_cast<std::size_t>(kMarksAtlasW) * kMarksAtlasH * 4, 0);
    if (const auto n = read(load, "Data/Misc/Numbers2.bmp")) blit(atlas, *n, 0, 0);
    if (const auto s = read(load, "Data/Misc/Select.444")) blit(atlas, *s, kMarksSelectX, kMarksSelectY);
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            for (int k = 0; k < 4; ++k) {
                atlas[(static_cast<std::size_t>(kMarksWhiteY + r) * kMarksAtlasW + kMarksWhiteX + c) * 4 + k] = 255;
            }
        }
    }
    return atlas;
}

MarksGraphicsPtr marks_graphics_open(SDL_Renderer* renderer, const GameFileLoader& load) {
    MarksGraphicsPtr g(new MarksGraphics);
    g->renderer = renderer;
    const std::vector<std::uint8_t> atlas = marks_atlas(load);
    g->atlas =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, kMarksAtlasW, kMarksAtlasH);
    if (g->atlas) {
        SDL_UpdateTexture(g->atlas, nullptr, atlas.data(), kMarksAtlasW * 4);
        SDL_SetTextureBlendMode(g->atlas, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(g->atlas, SDL_SCALEMODE_NEAREST);
    }
    return g;
}

void marks_draw(MarksGraphics& g, const std::vector<MarkQuad>& quads) {
    if (g.atlas == nullptr || quads.empty()) return;
    g.vertices.resize(quads.size() * 4);
    g.indices.resize(quads.size() * 6);
    constexpr float iw = 1.0f / kMarksAtlasW, ih = 1.0f / kMarksAtlasH;
    for (std::size_t i = 0; i < quads.size(); ++i) {
        const MarkQuad& q = quads[i];
        const SDL_FColor c{static_cast<float>(q.argb >> 16 & 0xff) / 255.0f,
                           static_cast<float>(q.argb >> 8 & 0xff) / 255.0f, static_cast<float>(q.argb & 0xff) / 255.0f,
                           static_cast<float>(q.argb >> 24) / 255.0f};
        SDL_Vertex* v = &g.vertices[i * 4];
        v[0] = {{q.x0, q.y0}, c, {q.u0 * iw, q.v0 * ih}};
        v[1] = {{q.x1, q.y0}, c, {q.u1 * iw, q.v0 * ih}};
        v[2] = {{q.x0, q.y1}, c, {q.u0 * iw, q.v1 * ih}};
        v[3] = {{q.x1, q.y1}, c, {q.u1 * iw, q.v1 * ih}};
        const int b = static_cast<int>(i * 4);
        const std::array<int, 6> idx{b, b + 1, b + 2, b + 1, b + 3, b + 2};
        std::copy(idx.begin(), idx.end(), g.indices.begin() + static_cast<std::ptrdiff_t>(i * 6));
    }
    SDL_RenderGeometry(g.renderer, g.atlas, g.vertices.data(), static_cast<int>(g.vertices.size()), g.indices.data(),
                       static_cast<int>(g.indices.size()));
}

}  // namespace antanta::ui
