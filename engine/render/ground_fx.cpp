#include "render/ground_fx.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

#include "formats/image.hpp"
#include "formats/les.hpp"
#include "formats/sprites.hpp"

namespace antanta {
namespace {



constexpr int kAnfRecord[8] = {8, 9, 17, 18, 15, 16, 5, 6};
constexpr float kSpeed[8] = {0, 0, 0.01f, 0.01f, 0.01f, 0.01f, 0.005f, 0.005f};  // 0: the file's
constexpr int kRaise[8] = {0, 0, 0x1e, 0x1e, 0x12, 0x12, 0x1e, 0x1e};

int floor_div(int a, int b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }

float quad_at(const MapDecoration& d, int i) {
    return std::bit_cast<float>(static_cast<std::uint32_t>(d.rest[4 * i] | d.rest[4 * i + 1] << 8 |
                                                           d.rest[4 * i + 2] << 16 |
                                                           static_cast<std::uint32_t>(d.rest[4 * i + 3]) << 24));
}
void set_quad(MapDecoration& d, int i, float v) {
    const auto u = std::bit_cast<std::uint32_t>(v);
    for (int k = 0; k < 4; ++k) d.rest[static_cast<std::size_t>(4 * i + k)] = static_cast<std::uint8_t>(u >> (8 * k));
}

std::uint8_t height_at(const MapData& m, int cx, int row) {
    cx = std::clamp(cx, 0, m.width - 1);
    row = std::clamp(row, 0, m.height - 1);
    return m.cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(m.width) + cx].height;
}


bool crater_step(MapDecoration& d, int count) {
    if (d.f17 != 0) return false;
    if (d.f14 != 0) {
        --d.f14;
        return false;
    }
    float f = 0;
    if (d.f16 == 0) {
        f = d.f08 + d.f03;
        d.f08 = f;
        if (f < static_cast<float>(d.f12) && f < static_cast<float>(count)) return true;
        f = static_cast<float>(d.f10);
    } else {
        f = d.f08 - d.f03;
        d.f08 = f;
        if (static_cast<float>(d.f10) <= f && f >= 0.0f) return true;
        f = d.f12 == 0 ? static_cast<float>(count - 1) : static_cast<float>(d.f12);
    }
    d.f08 = f;
    if (d.f01 != 0) {
        --d.f01;
        if (d.f01 == 0) {
            d.f17 = 1;
            return false;
        }
    }
    return true;
}

// Ours (docs/bugs.md B7): the ground under a flat crater on an 8-pixel grid, so its depth follows the terrain between
// the corners (the original interpolates the depth of the four corners only, and a ridge or a hollow between them cuts
// the crater off or lets the ground show through).
void make_ground(GroundFx::Crater& c, const MapData& m) {
    const float x0 = quad_at(c.d, 0), y0 = quad_at(c.d, 1), x1 = quad_at(c.d, 9), y1 = quad_at(c.d, 10);
    c.gw = std::max(2, static_cast<int>(std::ceil((x1 - x0) / 8.0f)) + 1);
    c.gh = std::max(2, static_cast<int>(std::ceil((y1 - y0) / 8.0f)) + 1);
    c.ground.resize(static_cast<std::size_t>(c.gw) * static_cast<std::size_t>(c.gh));
    for (int j = 0; j < c.gh; ++j) {
        for (int i = 0; i < c.gw; ++i) {
            const double x = std::min(static_cast<double>(x1), x0 + 8.0 * i);
            const double y = std::min(static_cast<double>(y1), y0 + 8.0 * j);
            c.ground[static_cast<std::size_t>(j * c.gw + i)] = static_cast<float>(screen_ground_height(m, x, y));
        }
    }
}

void blend_px(Target& rt, std::size_t i, const std::uint8_t* texel) {
    static const std::array<float, 16> nib = [] {
        std::array<float, 16> t{};
        for (int k = 0; k < 16; ++k) t[static_cast<std::size_t>(k)] = static_cast<float>(k) / 15.0f;
        return t;
    }();
    const float a = nib[texel[3]];
    float* dst = &rt.rgb[i * 3];
    for (int ch = 0; ch < 3; ++ch) dst[ch] = nib[texel[ch]] * a + dst[ch] * (1.0f - a);
}



void draw_flat(GroundFx::Crater& c, const Texture& t, const MapData& m, int vx, int vy, Target& rt) {
    if (c.ground.empty()) make_ground(c, m);
    const float qx0 = quad_at(c.d, 0), qy0 = quad_at(c.d, 1);
    const int x0 = static_cast<int>(std::floor(qx0)) - vx, y0 = static_cast<int>(std::floor(qy0)) - vy;
    for (int ty = 0; ty < t.height; ++ty) {
        const int y = y0 + ty;
        if (y < 0 || y >= rt.height) continue;
        const float gy = std::min(static_cast<float>(c.gh - 1), static_cast<float>(ty) / 8.0f);
        const int j = std::min(c.gh - 2, static_cast<int>(gy));
        const float fy = gy - static_cast<float>(j);
        for (int tx = 0; tx < t.width; ++tx) {
            const int x = x0 + tx;
            if (x < 0 || x >= rt.width) continue;
            const std::uint8_t* texel = &t.rgba[(static_cast<std::size_t>(ty) * t.width + tx) * 4];
            if (texel[3] == 0) continue;
            const float gx = std::min(static_cast<float>(c.gw - 1), static_cast<float>(tx) / 8.0f);
            const int i = std::min(c.gw - 2, static_cast<int>(gx));
            const float fx = gx - static_cast<float>(i);
            auto g = [&c](int a, int b) { return c.ground[static_cast<std::size_t>(b * c.gw + a)]; };
            const float h = (1 - fx) * (1 - fy) * g(i, j) + fx * (1 - fy) * g(i + 1, j) + (1 - fx) * fy * g(i, j + 1) +
                            fx * fy * g(i + 1, j + 1);
            const std::size_t px = static_cast<std::size_t>(y) * rt.width + x;
            const float key = static_cast<float>(y) + 2.0f * (h + 5.0f);
            if (!(key >= rt.key[px] - 0.001f)) continue;
            blend_px(rt, px, texel);
        }
    }
}




void draw_standing(const GroundFx::Crater& c, const Texture& t, int vx, int vy, Target& rt) {
    const float qx0 = quad_at(c.d, 0), qy0 = quad_at(c.d, 1), qx1 = quad_at(c.d, 3), qy1 = quad_at(c.d, 7);
    const float hl = quad_at(c.d, 8), hr = quad_at(c.d, 11);
    const int x0 = static_cast<int>(std::floor(qx0)) - vx, y0 = static_cast<int>(std::floor(qy0)) - vy;
    const float bottom = qy1 - static_cast<float>(vy), width = std::max(1.0f, qx1 - qx0);
    for (int ty = 0; ty < t.height; ++ty) {
        const int y = y0 + ty;
        if (y < 0 || y >= rt.height) continue;
        for (int tx = 0; tx < t.width; ++tx) {
            const int x = x0 + tx;
            if (x < 0 || x >= rt.width) continue;
            const std::uint8_t* texel = &t.rgba[(static_cast<std::size_t>(ty) * t.width + tx) * 4];
            if (texel[3] == 0) continue;
            const float f = static_cast<float>(tx) / width;
            const float key = bottom + 2.0f * (hl + (hr - hl) * f);
            const std::size_t px = static_cast<std::size_t>(y) * rt.width + x;
            if (!(key >= rt.key[px] - 0.001f)) continue;
            blend_px(rt, px, texel);
            rt.key[px] = key;
            rt.water[px] = 0;
        }
    }
}

bool point_in(double px, double py, double ax, double ay, double bx, double by, double cx, double cy, double* wa,
              double* wb, double* wc) {
    const double den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
    if (std::fabs(den) < 1e-9) return false;
    *wa = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den;
    *wb = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den;
    *wc = 1 - *wa - *wb;
    return *wa >= -1e-7 && *wb >= -1e-7 && *wc >= -1e-7;
}

}  // namespace

int anim_index(int object) {
    if (object == 0x2581) return 0;
    if (object == 0x2582) return 1;
    if (object >= 0x25de && object <= 0x25e3) return object - 0x25de + 2;
    return -1;
}

void load_ground_fx_graphics(GroundFxGraphics& g, const GroundFxLoader& load) {
    auto open = [&](const std::string& path, Bytes* file, LesTable* table) {
        std::string err;
        auto f = load(path, &err);
        if (!f) {
            g.warnings.push_back(path + ": " + err);
            return false;
        }
        auto t = parse_les(*f, &err);
        if (!t) {
            g.warnings.push_back(path + ": " + err);
            return false;
        }
        *file = std::move(*f);
        *table = std::move(*t);
        return true;
    };
    auto tex444 = [&](const Bytes& file, const LesEntry& e, const std::string& path) -> std::optional<Texture> {
        std::string err;
        auto b = read_entry(file, e, &err);
        auto img = b ? decode_444(*b, &err) : std::nullopt;
        if (!img) {
            g.warnings.push_back(path + ": " + err);
            return std::nullopt;
        }
        return texture_from_image(*img);
    };




    const std::string dir = "Data/Objects/Animation/Craters/";
    Bytes flat_file, vert_file, burst_file;
    LesTable flat_t, vert_t, burst_t;
    if (open(dir + "Craters.les", &flat_file, &flat_t) && open(dir + "Vertical.les", &vert_file, &vert_t)) {
        const std::size_t n1 = flat_t.entries.size(), n2 = vert_t.entries.size();
        g.craters.resize(n1 + n2 + 1);
        for (std::size_t t = 0; t < n1 + n2; ++t) {
            const bool flat = t < n1;
            const LesTable& table = flat ? flat_t : vert_t;
            const auto id = static_cast<std::uint16_t>(flat ? t : t - n1);
            GroundFxGraphics::CraterSet& set = g.craters[t];
            set.kind = flat ? 1 : 2;
            if (const LesEntry* e = find_entry_id(table, id)) {
                if (auto tex = tex444(flat ? flat_file : vert_file, *e, dir + (flat ? "Craters.les" : "Vertical.les")))
                    set.frames.push_back(std::move(*tex));
            }
        }
        GroundFxGraphics::CraterSet& burst = g.craters.back();
        burst.kind = 2;
        if (open(dir + "burst.les", &burst_file, &burst_t)) {
            for (const LesEntry& e : burst_t.entries) {
                if (auto tex = tex444(burst_file, e, dir + "burst.les")) burst.frames.push_back(std::move(*tex));
            }
        }
        for (GroundFxGraphics::CraterSet& set : g.craters) {
            if (!set.frames.empty()) set.w = set.frames[0].width, set.h = set.frames[0].height;
        }
    }



    std::string err;
    auto anf = load("Data/Animation.anf", &err);
    if (!anf) {
        g.warnings.push_back("Data/Animation.anf: " + err);
        return;
    }
    const std::size_t n_rec = anf->size() >= 4 ? read_u32(*anf, 0) : 0;
    for (int a = 0; a < 8; ++a) {
        const std::size_t r = 4 + static_cast<std::size_t>(kAnfRecord[a]) * 0x30;
        if (static_cast<std::size_t>(kAnfRecord[a]) >= n_rec || r + 0x30 > anf->size()) {
            g.warnings.push_back("Data/Animation.anf: record " + std::to_string(kAnfRecord[a]) + " missing");
            continue;
        }
        const ByteSpan rec = ByteSpan(*anf).subspan(r, 0x30);
        std::string name(reinterpret_cast<const char*>(rec.data()), 30);
        name = name.substr(0, name.find('\0'));
        GroundFxGraphics::Anim& an = g.anims[static_cast<std::size_t>(a)];
        an.speed = kSpeed[a] != 0 ? kSpeed[a] : std::bit_cast<float>(read_u32(rec, 0x20));
        an.frames = rec[0x24];
        an.w = read_u16(rec, 0x26);
        an.h = read_u16(rec, 0x28);
        an.raise = kRaise[a];
        const std::string path = "Data/Objects/Animation/" + name + ".les";
        Bytes file;
        LesTable table;
        if (!open(path, &file, &table)) continue;
        for (int f = 0; f < an.frames; ++f) {
            const LesEntry* e = find_entry_id(table, static_cast<std::uint16_t>(f + 1));
            std::optional<Bytes> b = e ? read_entry(file, *e, &err) : std::nullopt;
            auto img = b ? decode_bmp(*b, &err) : std::nullopt;
            if (!img) {
                g.warnings.push_back(path + ": frame " + std::to_string(f + 1) + ": " + (e ? err : "missing"));
                an.pics.clear();
                break;
            }
            GroundFxGraphics::AnimFrame pic;
            pic.w = img->width, pic.h = img->height;
            const std::size_t n = static_cast<std::size_t>(pic.w) * static_cast<std::size_t>(pic.h);
            pic.rgb.resize(n);
            pic.opaque.resize(n);
            for (std::size_t i = 0; i < n; ++i) {


                const int r5 = img->rgba[i * 4] >> 3, g6 = img->rgba[i * 4 + 1] >> 2, b5 = img->rgba[i * 4 + 2] >> 3;
                pic.opaque[i] = !(r5 == 31 && g6 == 0 && b5 == 31);
                pic.rgb[i] = {static_cast<float>(r5) / 31.0f, static_cast<float>(g6) / 63.0f,
                              static_cast<float>(b5) / 31.0f};
            }
            an.pics.push_back(std::move(pic));
        }
    }
}

double screen_ground_height(const MapData& m, double x, double y) {
    if (m.width <= 0 || m.height <= 0 || m.cells.empty()) return 0;
    double best_key = -1e18, best_h = 0;
    bool hit = false;
    const int r0 = std::max(0, static_cast<int>(std::floor(y / 24.0)) - 1);
    const int r1 = std::min(m.height - 1, static_cast<int>(std::floor((y + 24.0 + 256.0) / 24.0)) + 1);
    const int c_mid = floor_div(static_cast<int>(std::floor(x)) + 48, 96);
    for (int row = r0; row <= r1; ++row) {
        const double ry = row * 24.0;
        const int p = row & 1;
        for (int cx = std::max(0, c_mid - 1); cx <= std::min(m.width - 1, c_mid + 1); ++cx) {
            const double vx = cx * 96.0 - (row % 2 == 0 ? 48.0 : 0.0);
            const double hl = height_at(m, cx, row), ht = height_at(m, cx + p, row - 1),
                         hb = height_at(m, cx + p, row + 1), hr = height_at(m, cx + 1, row);
            // The terrain triangles L-T-B and T-B-R as the static layer draws them (iso.cpp step 3).
            const double lx = vx, ly = ry - hl, tx = vx + 48, ty = ry - 24 - ht, bx = vx + 48, by = ry + 24 - hb,
                         rx = vx + 96, rY = ry - hr;
            double a = 0, b = 0, c = 0;
            double h = 0;
            bool in = false;
            if (point_in(x, y, lx, ly, tx, ty, bx, by, &a, &b, &c)) {
                h = a * hl + b * ht + c * hb, in = true;
            } else if (point_in(x, y, tx, ty, bx, by, rx, rY, &a, &b, &c)) {
                h = a * ht + b * hb + c * hr, in = true;
            }
            if (in && y + 2 * h > best_key) best_key = y + 2 * h, best_h = h, hit = true;
        }
    }
    return hit ? best_h : 0;
}

void crater_add(GroundFx& fx, const GroundFxGraphics& g, const MapData& m, std::int32_t x, std::int32_t y,
                std::uint8_t type) {
    if (type >= g.craters.size()) return;
    // Unsigned compares, so a negative point is out too.
    const auto ux = static_cast<std::uint32_t>(x), uy = static_cast<std::uint32_t>(y);
    if (ux >= static_cast<std::uint32_t>((m.width - 2) * 96) || uy >= static_cast<std::uint32_t>((m.height - 3) * 24) ||
        ux <= 0xc0 || uy <= 0x48) {
        return;
    }
    for (const GroundFx::Crater& c : fx.craters) {
        if (c.d.type == type && static_cast<std::int32_t>(c.d.world_x) == x &&
            static_cast<std::int32_t>(c.d.world_y) == y)
            return;
    }
    const GroundFxGraphics::CraterSet& set = g.craters[type];
    if (set.frames.empty()) return;


    GroundFx::Crater c;
    c.d.type = type;
    c.d.f03 = 1.0f;
    c.d.f0c = 1.0f;
    c.d.f12 = static_cast<std::uint16_t>(set.frames.size());
    c.d.world_x = static_cast<float>(x), c.d.world_y = static_cast<float>(y);
    float q[12];
    if (set.kind == 1) {
        const float x0 = static_cast<float>(x) - static_cast<float>(set.w / 2);
        const float y0 = static_cast<float>(y) - static_cast<float>(set.h / 2);
        const float x1 = static_cast<float>(set.w) + x0, y1 = static_cast<float>(set.h) + y0;
        auto h = [&m](float px, float py) {
            return static_cast<float>(static_cast<int>(screen_ground_height(m, px, py))) + 5.0f;
        };
        const float v[12] = {x0, y0, h(x0, y0), x1, y0, h(x1, y0), x0, y1, h(x0, y1), x1, y1, h(x1, y1)};
        std::copy(v, v + 12, q);
    } else {
        const auto x0 = static_cast<float>(x - (set.w >> 1)), y0 = static_cast<float>(y - (set.h >> 1));
        const float x1 = static_cast<float>(set.w) + x0, y1 = static_cast<float>(set.h) + y0;
        const auto hl = static_cast<float>(static_cast<int>(screen_ground_height(m, x0, y1)));
        const auto hr = static_cast<float>(static_cast<int>(screen_ground_height(m, x1, y1)));
        const float v[12] = {x0, y0, hl, x1, y0, hr, x0, y1, hl, x1, y1, hr};
        std::copy(v, v + 12, q);
    }
    for (int i = 0; i < 12; ++i) set_quad(c.d, i, q[i]);
    // 5000 cycles for a flat set, forever for a standing one; type 2 two cycles.
    c.d.f01 = set.kind == 2 ? 0 : 5000;
    if (type == 2) c.d.f01 = 2;
    fx.craters.push_back(std::move(c));
}

namespace {

void start(GroundFx& fx, const GroundFxGraphics& g, const MapData& m) {
    if (fx.started) return;
    fx.started = true;

    for (const MapDecoration& d : m.decorations) {
        GroundFx::Crater c;
        c.d = d;
        const int kind = d.type < g.craters.size() ? g.craters[d.type].kind : 0;
        c.d.f01 = kind == 2 ? 0 : 5000;
        fx.craters.push_back(std::move(c));
    }


    std::uint32_t r = 1;
    for (int a = 0; a < 8; ++a) {
        r = r * 214013u + 2531011u;
        const int n = g.anims[static_cast<std::size_t>(a)].frames;
        fx.frame[static_cast<std::size_t>(a)] = n > 0 ? static_cast<float>(static_cast<int>(r >> 16 & 0x7fff) % n) : 0;
    }
}

}  // namespace

void ground_fx_step(GroundFx& fx, const GroundFxGraphics& g, const MapData& m,
                    const std::vector<GroundFxEvent>& added) {
    start(fx, g, m);
    for (const GroundFxEvent& e : added) crater_add(fx, g, m, e.x, e.y, e.type);
    for (GroundFx::Crater& c : fx.craters) {
        const int count = c.d.type < g.craters.size() ? static_cast<int>(g.craters[c.d.type].frames.size()) : 0;
        c.show = crater_step(c.d, count);
    }
    std::erase_if(fx.craters, [](const GroundFx::Crater& c) { return c.d.f17 != 0; });



    for (int a = 0; a < 8; ++a) {
        const GroundFxGraphics::Anim& an = g.anims[static_cast<std::size_t>(a)];
        float& f = fx.frame[static_cast<std::size_t>(a)];
        for (int k = 0; k < kTypicalCells[static_cast<std::size_t>(a)]; ++k) {
            f += an.speed;
            if (static_cast<float>(an.frames) <= f) f = 0;
        }
    }
}

void ground_fx_draw(const GroundFxGraphics& g, GroundFx& fx, const MapData& m, int vx, int vy, Target& rt) {
    start(fx, g, m);

    for (GroundFx::Crater& c : fx.craters) {
        if (!c.show || c.d.type >= g.craters.size()) continue;
        const GroundFxGraphics::CraterSet& set = g.craters[c.d.type];
        if (set.frames.empty()) continue;
        const int fi = std::clamp(static_cast<int>(c.d.f08), 0, static_cast<int>(set.frames.size()) - 1);
        const Texture& t = set.frames[static_cast<std::size_t>(fi)];
        const float x0 = quad_at(c.d, 0) - static_cast<float>(vx), y0 = quad_at(c.d, 1) - static_cast<float>(vy);
        if (x0 > static_cast<float>(rt.width) || y0 > static_cast<float>(rt.height) ||
            x0 + static_cast<float>(t.width) < 0 || y0 + static_cast<float>(t.height) < 0) {
            continue;
        }
        if (set.kind == 1) {
            draw_flat(c, t, m, vx, vy, rt);
        } else {
            draw_standing(c, t, vx, vy, rt);
        }
    }






    constexpr float kCellTint = 0xe5 / 255.0f;
    struct Item {
        int a, x0, y0, x1, y1;
        float key;
    };
    std::vector<Item> items;
    const int c0 = floor_div(vx, 96) - 2, c1 = floor_div(vx + rt.width, 96) + 2;
    const int r0 = floor_div(vy, 24) - 2, r1 = floor_div(vy + rt.height, 24) + 20;
    for (int row = std::max(0, r0); row <= std::min(m.height - 1, r1); ++row) {
        for (int cx = std::max(0, c0); cx <= std::min(m.width - 1, c1); ++cx) {
            const MapCell& cell = m.cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(m.width) + cx];
            const int a = anim_index(cell.object & 0x7FFF);
            if (a < 0) continue;
            const GroundFxGraphics::Anim& an = g.anims[static_cast<std::size_t>(a)];
            if (an.pics.empty()) continue;
            const int h = cell.height, row_y = row * 24 - vy;
            const int x = cx * 96 - (row % 2 == 0 ? 48 : 0) - vx;
            const bool field = an.raise != 0;
            const int centre = x + (field ? 0x42 : 0x30), bottom = row_y - 24 - h + (field ? 0x32 : 0x2a);
            const float key = static_cast<float>(bottom + 2 * h + (field ? an.raise + 10 : 0));
            items.push_back({a, centre - (an.w >> 1), bottom - an.h, centre + (an.w >> 1), bottom, key});
        }
    }
    std::stable_sort(items.begin(), items.end(),
                     [](const Item& p, const Item& q) { return p.y1 < q.y1 || (p.y1 == q.y1 && p.x1 < q.x1); });
    std::array<float, 8> frame = fx.frame;
    for (const Item& it : items) {
        const GroundFxGraphics::Anim& an = g.anims[static_cast<std::size_t>(it.a)];
        float& f = frame[static_cast<std::size_t>(it.a)];
        const int fi = std::clamp(static_cast<int>(static_cast<std::uint8_t>(static_cast<int>(f))), 0,
                                  static_cast<int>(an.pics.size()) - 1);
        f += an.speed;  // the next cell of this type shows the frame after (one shared frame per type)
        if (static_cast<float>(an.frames) <= f) f = 0;
        const GroundFxGraphics::AnimFrame& pic = an.pics[static_cast<std::size_t>(fi)];
        const int qw = it.x1 - it.x0, qh = it.y1 - it.y0;
        if (qw <= 0 || qh <= 0 || pic.w <= 0 || pic.h <= 0) continue;
        for (int y = std::max(0, it.y0); y < std::min(rt.height, it.y1); ++y) {
            const int ty = (y - it.y0) * pic.h / qh;
            for (int x = std::max(0, it.x0); x < std::min(rt.width, it.x1); ++x) {
                const int tx = (x - it.x0) * pic.w / qw;
                const std::size_t ti = static_cast<std::size_t>(ty) * pic.w + tx;
                if (!pic.opaque[ti]) continue;
                const std::size_t px = static_cast<std::size_t>(y) * rt.width + x;
                if (!(it.key >= rt.key[px] - 0.001f)) continue;
                float* dst = &rt.rgb[px * 3];
                for (int ch = 0; ch < 3; ++ch) dst[ch] = pic.rgb[ti][static_cast<std::size_t>(ch)] * kCellTint;
            }
        }
    }
}

}  // namespace antanta
