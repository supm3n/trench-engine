#include "render/topdown.hpp"

#include <algorithm>

namespace antanta {
namespace {

constexpr std::array<std::array<std::uint8_t, 3>, 8> kPlayerColours = {{
    {255, 0, 0},
    {0, 128, 255},
    {255, 255, 0},
    {0, 255, 0},
    {255, 128, 0},
    {160, 64, 255},
    {0, 255, 255},
    {255, 128, 192},
}};

struct Canvas {
    Image& img;
    void dot(int x, int y, std::array<std::uint8_t, 3> c, int r) {
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                const int px = x + dx, py = y + dy;
                if (px < 0 || py < 0 || px >= img.width || py >= img.height) continue;
                std::uint8_t* p = &img.rgba[(static_cast<std::size_t>(py) * img.width + px) * 4];
                p[0] = c[0], p[1] = c[1], p[2] = c[2], p[3] = 255;
            }
        }
    }
};

Rgb fallback_colour(int id) {
    const unsigned h = static_cast<unsigned>(id) * 2654435761u;
    return {double(64 + (h >> 8 & 127)), double(64 + (h >> 16 & 127)), double(64 + (h >> 24 & 127))};
}

// Cell column on the staggered grid: odd rows sit half a cell to the right in the overview.
int cell_px(int x, int y) { return x * kTopDownCellW + ((y & 1) ? kTopDownCellW / 2 : 0); }

}  // namespace

std::vector<Rgb> tile_colours(const LandTextures& textures) {
    std::vector<Rgb> out;
    for (const Image& t : textures.textures) {
        Rgb sum{};
        const std::size_t n = static_cast<std::size_t>(t.width) * t.height;
        for (std::size_t i = 0; i < n; ++i) {
            for (int c = 0; c < 3; ++c) sum[c] += t.rgba[i * 4 + c];
        }
        for (double& v : sum) v /= double(n ? n : 1);
        out.push_back(sum);
    }
    return out;
}

Image render_top_down(const MapData& map, const std::vector<Rgb>& colours) {
    Image img;
    img.width = map.width * kTopDownCellW + kTopDownCellW;
    img.height = (map.height + 1) * kTopDownRowH;
    img.rgba.assign(static_cast<std::size_t>(img.width) * img.height * 4, 0);
    for (std::size_t i = 3; i < img.rgba.size(); i += 4) img.rgba[i] = 255;

    int lo = 255, hi = 0;
    for (const MapCell& c : map.cells) lo = std::min<int>(lo, c.height), hi = std::max<int>(hi, c.height);
    const int n = static_cast<int>(colours.size());
    auto colour_of = [&](int id) { return n ? colours[static_cast<std::size_t>(id)] : fallback_colour(id); };
    for (int y = 0; y < map.height; ++y) {
        for (int x = 0; x < map.width; ++x) {
            const MapCell& c = map.cells[static_cast<std::size_t>(y) * map.width + x];
            std::vector<int> ids;
            if (c.tile & 0x8000) {
                for (std::uint16_t s : c.subtile) {
                    if (s > 0 && (s <= n || n == 0)) ids.push_back(s - 1);
                }
            } else {
                const int id = (c.tile & 0x7FFF) - 1;
                if (id >= 0 && (id < n || n == 0)) ids.push_back(id);
            }
            if (ids.empty()) ids.push_back(0);
            Rgb col{};
            for (int id : ids) {
                const Rgb t = colour_of(id);
                for (int k = 0; k < 3; ++k) col[k] += t[k] / double(ids.size());
            }
            const double shade = 0.6 + 0.4 * (c.height - lo) / double(std::max(1, hi - lo));
            std::array<std::uint8_t, 3> px{};
            for (int k = 0; k < 3; ++k) px[k] = static_cast<std::uint8_t>(std::clamp(col[k] * shade, 0.0, 255.0));
            for (int dy = 0; dy < kTopDownRowH; ++dy) {
                for (int dx = 0; dx < kTopDownCellW; ++dx) {
                    Canvas{img}.dot(cell_px(x, y) + dx, y * kTopDownRowH + dy, px, 0);
                }
            }
        }
    }

    Canvas canvas{img};
    const int grid_w = 2 * map.width;
    for (std::size_t i = 0; i < map.obstacle_grid.size(); ++i) {  // one entry per 48 x 24 world pixels
        const std::uint16_t g = map.obstacle_grid[i];
        if (!g) continue;
        const int gx = static_cast<int>(i % grid_w), gy = static_cast<int>(i / grid_w);
        canvas.dot(gx * kTopDownCellW / 2, gy * kTopDownRowH,
                   (g & 0x8000) ? std::array<std::uint8_t, 3>{255, 0, 255} : std::array<std::uint8_t, 3>{160, 0, 160},
                   0);
    }
    auto cell_dot = [&](std::int64_t x, std::int64_t y, std::array<std::uint8_t, 3> c, int r) {
        if (x < 0 || y < 0 || x >= map.width || y >= map.height) return;
        canvas.dot(cell_px(static_cast<int>(x), static_cast<int>(y)), static_cast<int>(y) * kTopDownRowH, c, r);
    };
    for (std::uint32_t e : map.port_sites) cell_dot(e & 0xFFFF, e >> 16, {0, 255, 255}, 3);
    auto world_dot = [&](float wx, float wy, std::array<std::uint8_t, 3> c, int r) {
        if (!(wx > -1e6f && wx < 1e6f && wy > -1e6f && wy < 1e6f)) return;  // garbage or NaN in a record
        canvas.dot(static_cast<int>(wx / 96.0f * kTopDownCellW), static_cast<int>(wy / 24.0f * kTopDownRowH), c, r);
    };
    for (const MapDecoration& d : map.decorations) world_dot(d.world_x, d.world_y, {0, 200, 0}, 1);
    for (const MapHouse& h : map.neutral_houses) cell_dot(h.cell_x, h.cell_y, {255, 255, 255}, 3);
    for (const MapHouse& h : map.houses)
        cell_dot(h.cell_x, h.cell_y, kPlayerColours[static_cast<std::size_t>(h.player) & 7], 3);
    for (const MapUnit& u : map.units)
        world_dot(u.world_x, u.world_y, kPlayerColours[static_cast<std::size_t>(u.player) & 7], 1);
    return img;
}

}  // namespace antanta
