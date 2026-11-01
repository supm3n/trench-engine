#include "render/bridges.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace antanta {
namespace {

constexpr float kStep = 0.2f;
constexpr float kWobble = 3.3333332538f;
constexpr float kTint = 0xe5 / 255.0f;
constexpr float kAlpha = 0x5a / 255.0f;

// A texel of the reflection picture (8-bit RGBA, point sampled) at u, v in 0..1.
std::array<float, 4> picture_texel(const Image& pic, double u, double v) {
    const int tx = std::clamp(static_cast<int>(u * pic.width), 0, pic.width - 1);
    const int ty = std::clamp(static_cast<int>(v * pic.height), 0, pic.height - 1);
    const std::uint8_t* t = &pic.rgba[(static_cast<std::size_t>(ty) * pic.width + tx) * 4];
    return {t[0] / 255.0f, t[1] / 255.0f, t[2] / 255.0f, t[3] / 255.0f};
}

}  // namespace

void bridge_reflections_draw(const WorldGraphics& g, const WorldView& view, std::uint32_t tick, ViewPos pos,
                             Target& rt) {
    const int vx = view_x(pos), vy = view_y(pos);
    struct Piece {
        const Image* pic;
        std::array<WaterVertex, 36> v;
    };
    std::vector<Piece> pieces;
    for (std::size_t hi = 0; hi < view.houses.size(); ++hi) {
        const MapHouse& h = view.houses[hi];


        const bool bridge = h.player == 8 && h.type <= 1;
        if (!bridge && h.type != 0x16) continue;
        int nation = 5, graphic = 0;
        if (!bridge) {
            if (h.player < 0 || h.player >= static_cast<int>(view.nations.size())) continue;
            nation = view_house_nation(view, hi);
            graphic = hi < view.house_graphic.size() ? view.house_graphic[hi] : (h.build_state == 0 ? 0 : 10);
        }
        const auto it = g.houses.find({nation, h.type, graphic});


        if (it == g.houses.end() || !it->second.shadow || it->second.shadow->image.width == 0) continue;
        const HouseGraphics& hg = it->second;
        const int dx = !bridge ? 0 : h.type == 0 ? -0x11 : 0xd, dy = !bridge ? 0x9c : h.type == 0 ? 0x37 : 0xe;


        const G4m& r = *hg.shadow;
        const double x0 = h.cell_x * 96 - (h.cell_y % 2 == 0 ? 48 : 0) - vx + 0x30 - (hg.anchor_x - r.off_x) + dx;
        const double y0 = (h.cell_y - 1) * 24 - vy - (hg.anchor_y - r.off_y - dy - 0x15);
        const double w = r.image.width, ht = r.image.height;
        if (x0 + w + 8 < 0 || x0 - 8 > rt.width || y0 + ht + 8 < 0 || y0 - 8 > rt.height) continue;
        Piece p{&r.image, {}};
        // Vertex (i, j), i along x, j along y: x wobbles by sin(6 tick + 16 j), y by cos(6 tick + 16 i) (radians), y
        // whole pixels (ftol), the picture's coordinates in fifths.
        const double phase = 6.0 * static_cast<double>(tick);
        for (int i = 0; i < 6; ++i) {
            const double cy = std::cos(phase + 16.0 * i) * kWobble;
            for (int j = 0; j < 6; ++j) {
                WaterVertex& gv = p.v[static_cast<std::size_t>(j * 6 + i)];
                gv.x = static_cast<float>(std::sin(phase + 16.0 * j) * kWobble + i * (w * kStep) + x0);
                gv.y = static_cast<double>(static_cast<std::int64_t>(j * (ht * kStep) + cy + y0));
                gv.u = i * static_cast<double>(kStep);
                gv.v = j * static_cast<double>(kStep);
            }
        }
        pieces.push_back(p);
    }
    if (pieces.empty()) return;
    const std::array<float, 3> tint{kTint, kTint, kTint};
    parallel_bands(rt.height, [&](int top, int bottom) {
        for (const Piece& p : pieces) {
            const Image& pic = *p.pic;
            auto sample = [&pic](double u, double v) { return picture_texel(pic, u, v); };
            for (int j = 0; j < 5; ++j) {
                for (int i = 0; i < 5; ++i) {


                    const WaterVertex& a = p.v[static_cast<std::size_t>(j * 6 + i)];
                    const WaterVertex& b = p.v[static_cast<std::size_t>(j * 6 + i + 1)];
                    const WaterVertex& c = p.v[static_cast<std::size_t>((j + 1) * 6 + i)];
                    const WaterVertex& d = p.v[static_cast<std::size_t>((j + 1) * 6 + i + 1)];
                    water_triangle(rt, sample, tint, kAlpha, c, a, b, top, bottom);
                    water_triangle(rt, sample, tint, kAlpha, c, b, d, top, bottom);
                }
            }
        }
    });
}

}  // namespace antanta
