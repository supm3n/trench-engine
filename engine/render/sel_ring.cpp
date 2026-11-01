#include "render/sel_ring.hpp"

#include <algorithm>
#include <cmath>

#include "formats/image.hpp"

namespace antanta {
namespace {

constexpr float kTint = 0xe5 / 255.0f;

// fn(pixel index, texel index) for every pixel of the dot that passes the depth test of rt.
template <class F>
void each_pixel(const RingTexture& tex, const RingDot& d, const Target& rt, F fn) {
    const int x0 = std::max(0, static_cast<int>(std::ceil(d.x))), y0 = std::max(0, static_cast<int>(std::ceil(d.y)));
    const int x1 = std::min(rt.width, static_cast<int>(std::ceil(d.x + 4.0f)));
    const int y1 = std::min(rt.height, static_cast<int>(std::ceil(d.y + 4.0f)));
    for (int py = y0; py < y1; ++py) {
        const int tv = std::min(tex.height - 1, static_cast<int>((static_cast<float>(py) - d.y) / 4.0f * 0.99999f *
                                                                 static_cast<float>(tex.height)));
        for (int px = x0; px < x1; ++px) {
            const int tu = std::min(tex.width - 1, static_cast<int>((static_cast<float>(px) - d.x) / 4.0f * 0.99999f *
                                                                    static_cast<float>(tex.width)));
            const std::size_t t = static_cast<std::size_t>(tv) * static_cast<std::size_t>(tex.width) + tu;
            const std::size_t i = static_cast<std::size_t>(py) * static_cast<std::size_t>(rt.width) + px;
            if (!tex.opaque[t] || !(d.key >= rt.key[i] - 0.001f)) continue;
            fn(i, t);
        }
    }
}

}  // namespace

std::optional<RingTexture> ring_texture(const GameFileLoader& load, std::string* error) {
    const std::string path = "Data/Misc/HouseSelTexture.bmp";
    const std::optional<Bytes> file = load(path, error);
    if (!file) return std::nullopt;
    const std::optional<Image> img = decode_bmp(*file, error);
    if (!img || img->width <= 0 || img->height <= 0) {
        if (img && error) *error = path + ": empty picture";
        return std::nullopt;
    }
    RingTexture t;
    t.width = img->width, t.height = img->height;
    const std::size_t n = static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height);
    t.rgb.resize(n), t.opaque.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const int r5 = img->rgba[i * 4] >> 3, g6 = img->rgba[i * 4 + 1] >> 2, b5 = img->rgba[i * 4 + 2] >> 3;
        t.opaque[i] = !(r5 == 31 && g6 == 0 && b5 == 31);
        t.rgb[i] = {static_cast<float>(r5) / 31.0f, static_cast<float>(g6) / 63.0f, static_cast<float>(b5) / 31.0f};
    }
    return t;
}

void ring_dots_draw(const RingTexture& tex, std::span<const RingDot> dots, Target& rt) {
    if (tex.width <= 0 || tex.height <= 0) return;
    for (const RingDot& d : dots) {
        each_pixel(tex, d, rt, [&](std::size_t i, std::size_t t) {
            for (int ch = 0; ch < 3; ++ch) rt.rgb[i * 3 + ch] = tex.rgb[t][ch] * kTint;
        });
    }
}

void ring_dots_draw_rgba(const RingTexture& tex, std::span<const RingDot> dots, const Target& rt,
                         std::vector<std::uint8_t>& rgba) {
    if (tex.width <= 0 || tex.height <= 0) return;
    for (const RingDot& d : dots) {
        each_pixel(tex, d, rt, [&](std::size_t i, std::size_t t) {
            for (int ch = 0; ch < 3; ++ch) {
                rgba[i * 4 + ch] = static_cast<std::uint8_t>(tex.rgb[t][ch] * 255.0f * kTint + 0.5f);
            }
        });
    }
}

}  // namespace antanta
