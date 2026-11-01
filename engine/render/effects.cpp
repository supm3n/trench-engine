#include "render/effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "formats/les.hpp"
#include "render/raster.hpp"

namespace antanta {

namespace {




constexpr const char* kFiles[kEffectTypes] = {
    "Bomba.les",
    "Bomba1.les",
    "Bomba2.les",
    "Bomba3.les",
    "BuildingSmallDeath.les",
    "ShootRight.les",
    "CannonShot.les",
    "FireManDeath.les",
    "GaubicaShot.les",
    "SM1.les",
    "PlaneDeath.les",
    "PlaneDeath1.les",
    "PlaneDeath2.les",
    "PlaneDeath3.les",
    "Tank-Hit.les",
    "GranadeLouncher.les",
    "exFG.les",
    "exFG2.les",
    "SmallDB.les",
    "flack.les",
    "SmallDBSand.les",
    "flack.les",
    "flack2.les",
    "flack3.les",
    "granade1.les",
    "granade2.les",
    "granade3.les",
    "Tank1.les",
    "tank2.les",
    "tank3.les",
    "WEN.les",
    "WEM.les",
    "WES.les",
    "WEP.les",
    "smallfire.les",
    "ShootLeft.les",
    "gas.les",
    "Arrows.les",
    "torpedo.les",
    "gunplane.les",
    "HouseShootFull.les",
    "new_dust6.les",
    "new_water1.les",
    "new_explo1.les",
    "new_explo2.les",
    "new_explo3.les",
    "new_explo4.les",
    "new_explo5.les",
    "new_dust7.les",
    "new_dust2.les",
};

// _ftol: truncation toward zero.
int ftol(float v) { return static_cast<int>(v); }

// The MSVC rand() formula on the view's generator (render/effects_emit effect_rand draws from the same state).
int crt_rand(std::uint32_t& state) {
    state = state * 214013u + 2531011u;
    return static_cast<int>(state >> 16 & 0x7fff);
}


bool tree_has_leaves(int r) {
    return !(r == 0x49 || r == 0x4c || r == 0x59 || (r >= 0x4e && r <= 0x56) || (r >= 0x5a && r <= 0x73) ||
             (r >= 0x75 && r <= 0x77) || (r >= 0x79 && r <= 0x85) || (r >= 0x87 && r <= 0x8b) ||
             (r >= 0xc5 && r <= 0xc7) || r == 0xda || r == 0xdb);
}

float f32_of(ByteSpan b, std::size_t pos) {
    const std::uint32_t v = read_u32(b, pos);
    float f = 0;
    std::memcpy(&f, &v, sizeof f);
    return f;
}

}  // namespace

const char* effect_file(int type) { return type >= 0 && type < kEffectTypes ? kFiles[type] : ""; }

std::optional<EffectGraphics::Type> decode_effect_type(ByteSpan entry, std::string* error) {
    if (entry.size() < 0xd) {
        if (error) *error = "entry 0 shorter than its header";
        return std::nullopt;
    }
    EffectGraphics::Type t;
    t.tried = true;
    const int count = entry[0];
    t.canvas_w = read_u16(entry, 1), t.canvas_h = read_u16(entry, 5);
    t.anchor_x = read_u16(entry, 9), t.anchor_y = read_u16(entry, 0xb);
    if (count >= 0x32) {
        if (error) *error = "more than 49 frames";
        return std::nullopt;
    }

    for (int i = 0; i < count; ++i) {
        const std::size_t r = 0xd + static_cast<std::size_t>(i) * 0x14;
        if (r + 0x14 > entry.size()) {
            if (error) *error = "frame record past the entry";
            return std::nullopt;
        }
        if (read_u16(entry, r) == 0xffff) break;
        EffectGraphics::Frame f;
        f.x = read_u16(entry, r), f.y = read_u16(entry, r + 2);
        f.w = read_u16(entry, r + 4), f.h = read_u16(entry, r + 6);
        f.tw = read_u16(entry, r + 8), f.th = read_u16(entry, r + 10);
        const std::size_t off = read_u32(entry, r + 0x10);
        f.tex_w = f.tw >> 1, f.tex_h = f.th >> 1;
        f.px.assign(static_cast<std::size_t>(f.tex_w) * static_cast<std::size_t>(f.tex_h), 0);
        // Every second pixel of every second row: texel (c, j) is picture pixel (2 c, 2 j).
        const int cols = std::min(f.w >> 1, f.tex_w), rows = std::min(f.h >> 1, f.tex_h);
        for (int j = 0; j < rows; ++j) {
            for (int c = 0; c < cols; ++c) {
                const std::size_t at =
                    off + (static_cast<std::size_t>(2 * j) * f.w + 2 * static_cast<std::size_t>(c)) * 2;
                if (at + 2 > entry.size()) {
                    if (error) *error = "frame pixels past the entry";
                    return std::nullopt;
                }
                f.px[static_cast<std::size_t>(j) * static_cast<std::size_t>(f.tex_w) + c] = read_u16(entry, at);
            }
        }
        t.frames.push_back(std::move(f));
    }
    t.count = static_cast<int>(t.frames.size());
    return t;
}

void effect_type_load(EffectGraphics& g, int type, const GameFileLoader& load) {
    if (type < 0 || type >= kEffectTypes || g.types[static_cast<std::size_t>(type)].tried) return;
    EffectGraphics::Type& t = g.types[static_cast<std::size_t>(type)];
    t.tried = true;
    const std::string path = std::string("Data/Misc/explo/") + kFiles[type];
    std::string err;
    auto file = load(path, &err);
    auto table = file ? parse_les(*file, &err) : std::nullopt;
    const LesEntry* e = table ? find_entry_id(*table, 0) : nullptr;
    if (table && e == nullptr) err = "no entry 0";
    auto bytes = e ? read_entry(*file, *e, &err) : std::nullopt;
    auto decoded = bytes ? decode_effect_type(*bytes, &err) : std::nullopt;
    if (!decoded) {
        g.warnings.push_back(path + ": " + err);
        return;
    }
    t = std::move(*decoded);
}

void effects_add(Effects& fx, EffectGraphics& g, const GameFileLoader& load, const EffectSpawn& e) {
    if (e.type >= kEffectTypes) return;
    effect_type_load(g, e.type, load);
    Effects::Item it;
    it.e = e;
    it.end = g.types[e.type].count;
    fx.list.push_back(it);
}

void effects_step(Effects& fx, const EffectGraphics& g, std::uint32_t tick) {
    if (tick == fx.stepped_tick) return;
    fx.stepped_tick = tick;

    for (std::size_t k = fx.list.size(); k-- > 0;) {
        Effects::Item& it = fx.list[k];
        it.shown = false;
        if (it.e.delay != 0) {
            --it.e.delay;
            continue;
        }
        const auto f = static_cast<std::uint8_t>(ftol(it.e.frame));
        const int count = g.types[it.e.type].count;
        if (f == static_cast<std::uint8_t>(count) || f == it.end) {
            fx.list.erase(fx.list.begin() + static_cast<std::ptrdiff_t>(k));
            continue;
        }

        it.shown = true;
        it.shown_frame = f;
        it.shown_height = static_cast<std::int16_t>(it.e.rise + it.e.z);
        it.e.frame = it.e.speed + it.e.frame;
        it.e.rise += it.e.rise_step;
    }
}

void effects_quads(const Effects& fx, const EffectGraphics& g, ViewPos pos, int w, int h,
                   std::vector<EffectQuad>* out) {
    const int vx = view_x(pos), vy = view_y(pos);
    struct Item {
        EffectQuad q;
        int row, col;
    };
    std::vector<Item> items;
    for (std::size_t k = fx.list.size(); k-- > 0;) {
        const Effects::Item& it = fx.list[k];
        if (!it.shown) continue;
        const EffectGraphics::Type& t = g.types[it.e.type];

        if (it.shown_frame >= t.count) continue;
        const EffectGraphics::Frame& f = t.frames[static_cast<std::size_t>(it.shown_frame)];
        if (f.tw == 0 || f.th == 0 || f.w == 0 || f.h == 0 || f.tw == 0xffff || f.th == 0xffff || f.w == 0xffff ||
            f.h == 0xffff || f.y == 0xffff || f.px.empty()) {
            continue;
        }

        const int ix = t.canvas_w - t.anchor_x + it.e.x - vx;
        const int iy = t.canvas_h - t.anchor_y + it.e.y - vy;
        const int z = it.shown_height;
        EffectQuad q;
        q.frame = &f;
        q.x0 = f.x + ix - t.canvas_w;
        q.y0 = f.y + iy - z - t.canvas_h;
        q.w = f.tw, q.h = f.th;
        // DepthFromScreenY(iy, iy - y1) at the bottom corners and (iy, iy - y1 + H) at the top ones.
        q.key_bottom = 2.0 * iy - (q.y0 + q.h);
        q.key_top = q.key_bottom + t.canvas_h;
        q.ground = it.e.ground != 0;
        if (q.x0 >= w || q.y0 >= h || q.x0 + q.w <= 0 || q.y0 + q.h <= 0) continue;
        items.push_back({q, iy, ix});
    }

    std::stable_sort(items.begin(), items.end(),
                     [](const Item& a, const Item& b) { return a.row < b.row || (a.row == b.row && a.col < b.col); });
    for (const Item& it : items) out->push_back(it.q);
}

namespace {




void leaf_draw(const EffectQuad& q, Target& rt, int band_top, int band_bottom) {
    const Texture& tex = *q.leaf;
    if (tex.width <= 0 || tex.height <= 0) return;
    constexpr float kTint = 0xe5 / 255.0f, kAlpha = 0xaa / 255.0f;
    const int xa = static_cast<int>(std::ceil(q.lx)), xb = static_cast<int>(std::ceil(q.lx + q.w));
    for (int y = std::max({0, band_top, q.y0}); y < std::min({rt.height, band_bottom, q.y0 + q.h}); ++y) {
        const double ty = static_cast<double>(y - q.y0) / q.h;
        const auto key = static_cast<float>(q.key_top + (q.key_bottom - q.key_top) * ty);
        const double v = q.v0 + (q.v1 - q.v0) * ty;
        const int tv = std::clamp(static_cast<int>(std::floor(v * tex.height)), 0, tex.height - 1);
        for (int x = std::max(0, xa); x < std::min(rt.width, xb); ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(rt.width) + x;
            if (!(key >= rt.key[i] - 0.001f)) continue;
            const double u = q.u0 + (q.u1 - q.u0) * ((x - q.lx) / q.w);
            const int tu = std::clamp(static_cast<int>(std::floor(u * tex.width)), 0, tex.width - 1);
            const std::uint8_t* tx =
                &tex.rgba[(static_cast<std::size_t>(tv) * static_cast<std::size_t>(tex.width) + tu) * 4];
            const float a = static_cast<float>(tx[3]) / 15.0f * kAlpha;
            if (a <= 0) continue;  // the colour key (texel 0) too
            float* dst = &rt.rgb[i * 3];
            for (int ch = 0; ch < 3; ++ch)
                dst[ch] = static_cast<float>(tx[ch]) / 15.0f * kTint * a + dst[ch] * (1.0f - a);
        }
    }
}

}  // namespace

void effect_draw(const EffectQuad& q, Target& rt, int band_top, int band_bottom) {
    if (q.leaf != nullptr) {
        leaf_draw(q, rt, band_top, band_bottom);
        return;
    }
    if (q.frame == nullptr || q.w <= 0 || q.h <= 0 || q.frame->tex_w <= 0 || q.frame->tex_h <= 0) return;
    const EffectGraphics::Frame& f = *q.frame;


    const float tint[3] = {q.ground ? 0xee / 255.0f : 0xe5 / 255.0f, q.ground ? 0xdd / 255.0f : 0xe5 / 255.0f,
                           q.ground ? 0xee / 255.0f : 0xe5 / 255.0f};
    const float alpha = q.ground ? 0x60 / 255.0f : 0xbe / 255.0f;
    const int tw = f.tex_w, th = f.tex_h;
    auto texel = [&](int i, int j) {
        i = ((i % tw) + tw) % tw, j = ((j % th) + th) % th;  // wrap addressing
        return f.px[static_cast<std::size_t>(j) * static_cast<std::size_t>(tw) + i];
    };
    for (int y = std::max({0, band_top, q.y0}); y < std::min({rt.height, band_bottom, q.y0 + q.h}); ++y) {
        const double ty = static_cast<double>(y - q.y0) / q.h;
        const auto key = static_cast<float>(q.key_top + (q.key_bottom - q.key_top) * ty);

        const double sv = ty * 0.99999 * th - 0.5;
        const int j0 = static_cast<int>(std::floor(sv));
        const float fv = static_cast<float>(sv - j0);
        for (int x = std::max(0, q.x0); x < std::min(rt.width, q.x0 + q.w); ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(rt.width) + x;
            if (!(key >= rt.key[i] - 0.001f)) continue;
            const double su = static_cast<double>(x - q.x0) / q.w * 0.99999 * tw - 0.5;
            const int i0 = static_cast<int>(std::floor(su));
            const float fu = static_cast<float>(su - i0);
            const std::uint16_t t00 = texel(i0, j0), t10 = texel(i0 + 1, j0), t01 = texel(i0, j0 + 1),
                                t11 = texel(i0 + 1, j0 + 1);
            if ((t00 | t10 | t01 | t11) >> 12 == 0) continue;  // all four transparent
            float c[4];
            for (int ch = 0; ch < 4; ++ch) {
                const int sh = ch == 3 ? 12 : 8 - 4 * ch;
                auto v = [sh](std::uint16_t t) { return static_cast<float>(t >> sh & 15) / 15.0f; };
                const float top = v(t00) + (v(t10) - v(t00)) * fu, bottom = v(t01) + (v(t11) - v(t01)) * fu;
                c[ch] = top + (bottom - top) * fv;
            }
            const float a = c[3] * alpha;
            if (a <= 0) continue;
            float* dst = &rt.rgb[i * 3];
            for (int ch = 0; ch < 3; ++ch) dst[ch] = c[ch] * tint[ch] * a + dst[ch] * (1.0f - a);
        }
    }
}

void leaves_add(Effects& fx, const ObjectAtlas& trees, const MapData& map, int x, int y, int record) {
    if (x < 0 || y < 0 || x >= map.width || y >= map.height || !tree_has_leaves(record)) return;
    if (record < 0 || static_cast<std::size_t>(record) >= trees.entries.size() / 0x38) return;
    const int cell = y * map.width + x;
    for (const TreeLeaves& l : fx.leaves) {
        if (l.cell == cell) return;
    }
    const ByteSpan r = ByteSpan(trees.entries).subspan(static_cast<std::size_t>(record) * 0x38, 0x38);
    TreeLeaves t;
    t.cell = cell;
    t.w = r[0x14], t.h = r[0x15], t.page = r[1];
    if (t.w == 0 || t.h == 0) return;  // the original divides by them

    t.ground = map.cells[static_cast<std::size_t>(cell)].height;
    t.x = (2 * x - (~y & 1)) * 48;
    t.y = y * 24 - t.ground;
    const float u0 = f32_of(r, 4), u1 = f32_of(r, 8), v0 = f32_of(r, 0xc), v1 = f32_of(r, 0x10);
    const float du = u1 - u0, dv = v1 - v0;
    t.leaves.resize(50);  // 0x32
    for (TreeLeaves::Leaf& l : t.leaves) {
        const int rx = crt_rand(fx.rand_state) % t.w;
        const int ry = crt_rand(fx.rand_state) % t.h;
        l.x = static_cast<float>(rx);
        l.y = static_cast<float>(ry - t.h);
        l.fall = static_cast<float>(crt_rand(fx.rand_state) % 100) * 0.02f;
        l.swing = static_cast<float>(crt_rand(fx.rand_state) % 20);
        l.phase = static_cast<float>(crt_rand(fx.rand_state) % 100) * 0.01f * 3.14f;
        l.phase_step = static_cast<float>(crt_rand(fx.rand_state) % 10) * 0.0125f;
        l.u0 = static_cast<float>(crt_rand(fx.rand_state) % 100) * 0.01f * du + u0;
        l.v0 = static_cast<float>(crt_rand(fx.rand_state) % 100) * 0.01f * dv + v0;
        l.u1 = du * 0.05f + l.u0;
        l.v1 = dv * 0.05f + l.v0;
    }
    fx.leaves.push_back(std::move(t));
}

void leaves_step(Effects& fx) {
    for (std::size_t k = 0; k < fx.leaves.size();) {
        TreeLeaves& t = fx.leaves[k];
        bool done = true;
        for (TreeLeaves::Leaf& l : t.leaves) {
            if (static_cast<float>(t.h / 2) - l.y > 0.0f) done = false;  // above the ground in the frame shown
            l.y = static_cast<float>((std::cos(static_cast<double>(l.phase)) + 1.0) * l.fall + l.y + 1.0);
            l.phase = l.phase_step + l.phase;
        }
        --t.life;
        if (done || t.life <= 0) {
            fx.leaves.erase(fx.leaves.begin() + static_cast<std::ptrdiff_t>(k));
        } else {
            ++k;
        }
    }
}

void leaves_quads(const Effects& fx, const ObjectAtlas& trees, ViewPos pos, int w, int h,
                  std::vector<EffectQuad>* out) {
    const auto ox0 = static_cast<float>(view_x(pos)) + 1.5f, oy0 = static_cast<float>(view_y(pos)) + 1.5f;
    for (const TreeLeaves& t : fx.leaves) {
        if (static_cast<std::size_t>(t.page) >= trees.pages.size()) continue;
        const float ox = static_cast<float>(t.x) - ox0, oy = static_cast<float>(t.y) - oy0;
        for (const TreeLeaves::Leaf& l : t.leaves) {
            const int sx = ftol(ox + l.x), sy = ftol(oy + l.y);
            const double lx = sx + std::sin(static_cast<double>(l.phase)) * l.swing;
            if (lx + 3 <= 0 || lx >= w || sy + 3 <= 0 || sy >= h) continue;
            // Ours adds twice the cell's height: the original's key leaves it out, so on raised ground the leaves
            // sank into the hill before they reached the foot of the tree (docs/bugs.md B52).
            const double hh = static_cast<double>(static_cast<float>(t.h / 2) - l.y) + t.ground;
            EffectQuad q;
            q.leaf = &trees.pages[static_cast<std::size_t>(t.page)];
            q.lx = lx, q.x0 = static_cast<int>(std::floor(lx)), q.y0 = sy, q.w = 3, q.h = 3;
            q.key_top = sy + 2 * hh, q.key_bottom = q.key_top + 3;
            q.ground = true;
            q.u0 = l.u0, q.u1 = l.u1, q.v0 = l.v0, q.v1 = l.v1;
            out->push_back(q);
        }
    }
}

}  // namespace antanta
