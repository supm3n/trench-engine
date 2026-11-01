#include "render/house_fx.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

#include "formats/image.hpp"
#include "formats/les.hpp"
#include "formats/params.hpp"
#include "formats/sprites.hpp"
#include "render/iso.hpp"

namespace antanta {

// How far from a slot's point the depth map may be for the slot to borrow the nearest depth (about the height of a
// chimney's cap): Austria's artillery depot is 18 pixels off, France's war academy 33 and 55.
constexpr int kDepthReach = 24;

std::optional<HouseFxGraphics::Frame> decode_house_fx_frame(ByteSpan entry, std::string* error) {
    HouseFxGraphics::Frame f;

    if (entry.size() >= 4) {
        const int w = read_u16(entry, 0), h = read_u16(entry, 2);
        if (static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 2 + 4 == entry.size()) {
            auto img = decode_444(entry, error);
            if (!img) return std::nullopt;
            f.w = img->width, f.h = img->height, f.argb4444 = true;
            f.px.resize(static_cast<std::size_t>(f.w) * static_cast<std::size_t>(f.h));
            for (std::size_t i = 0; i < f.px.size(); ++i) {
                const std::uint8_t* q = &img->rgba[i * 4];
                f.px[i] =
                    static_cast<std::uint16_t>((q[3] >> 4) << 12 | (q[0] >> 4) << 8 | (q[1] >> 4) << 4 | q[2] >> 4);
            }
            return f;
        }
    }
    auto img = decode_bmp(entry, error);
    if (!img) return std::nullopt;
    f.w = img->width, f.h = img->height;
    f.px.resize(static_cast<std::size_t>(f.w) * static_cast<std::size_t>(f.h));
    for (std::size_t i = 0; i < f.px.size(); ++i) {

        const std::uint8_t* q = &img->rgba[i * 4];
        f.px[i] = static_cast<std::uint16_t>((q[0] >> 3) << 11 | (q[1] >> 2) << 5 | q[2] >> 3);
    }
    return f;
}

namespace {

bool has_depth(std::int16_t d) {
    return d != static_cast<std::int16_t>(0x7FFF) && d != static_cast<std::int16_t>(0x8000);
}

// The depth at (x, y) of the house picture, else that of the nearest point with one within kDepthReach pixels (the
// first in row order among equals).
bool nearest_depth(const HouseGraphics& gfx, int x, int y, std::int16_t* out) {
    const int w = gfx.picture.width, h = gfx.picture.height;
    int best = kDepthReach * kDepthReach + 1;
    for (int j = std::max(0, y - kDepthReach); j <= std::min(h - 1, y + kDepthReach); ++j) {
        for (int i = std::max(0, x - kDepthReach); i <= std::min(w - 1, x + kDepthReach); ++i) {
            const std::int16_t d = gfx.depth[static_cast<std::size_t>(j) * static_cast<std::size_t>(w) + i];
            const int dist = (i - x) * (i - x) + (j - y) * (j - y);
            if (has_depth(d) && dist < best) best = dist, *out = d;
        }
    }
    return best <= kDepthReach * kDepthReach;
}

}  // namespace

void load_house_fx_graphics(HouseFxGraphics& g, const HouseFxLoader& load) {



    std::string err;
    auto anf = load("Data/Animation.anf", &err);
    if (!anf) {
        g.warnings.push_back("Data/Animation.anf: " + err);
        return;
    }
    const std::size_t n = anf->size() >= 4 ? read_u32(*anf, 0) : 0;
    for (std::size_t i = 0; i < n && 4 + (i + 1) * 0x30 <= anf->size(); ++i) {
        const ByteSpan rec = ByteSpan(*anf).subspan(4 + i * 0x30, 0x30);
        HouseFxGraphics::Record r;
        r.name = std::string(reinterpret_cast<const char*>(rec.data()), 30);
        r.name = r.name.substr(0, r.name.find('\0'));
        r.speed = std::bit_cast<float>(read_u32(rec, 0x20));
        r.frames = rec[0x24];
        r.w = read_u16(rec, 0x26), r.h = read_u16(rec, 0x28);
        r.ox = read_u16(rec, 0x2a), r.oy = read_u16(rec, 0x2c);
        g.records.push_back(std::move(r));
    }
    for (HouseFxGraphics::Record& r : g.records) {
        const std::string path = "Data/Objects/Animation/" + r.name + ".les";
        auto file = load(path, &err);
        auto table = file ? parse_les(*file, &err) : std::nullopt;
        for (int f = 0; table && f < r.frames; ++f) {
            const LesEntry* e = find_entry_id(*table, static_cast<std::uint16_t>(f + 1));
            std::optional<Bytes> b = e ? read_entry(*file, *e, &err) : std::nullopt;
            auto pic = b ? decode_house_fx_frame(*b, &err) : std::nullopt;
            if (!pic) {
                if (!e) err = "missing";
                table.reset();
                break;
            }
            if (pic->argb4444) r.w = pic->w, r.h = pic->h;
            r.pics.push_back(std::move(*pic));
        }
        if (!table) {
            g.warnings.push_back(path + ": " + err);
            r.pics.clear();
            r.frames = 0;
        }
    }
}

void house_fx_quads(const WorldGraphics& g, const WorldView& view, ViewPos pos, int w, int h, HouseFx& fx,
                    std::vector<HouseFxQuad>* out) {
    if (view.map == nullptr || view.map->cells.empty()) return;
    const MapData& m = *view.map;
    const int vx = view_x(pos), vy = view_y(pos);
    const HouseFxGraphics& hg = g.house_fx;
    struct Item {
        HouseFxQuad q;
        int bottom, right;
    };
    std::vector<Item> items;
    for (std::size_t i = 0; i < view.houses.size(); ++i) {
        const MapHouse& h0 = view.houses[i];


        const int graphic = i < view.house_graphic.size() ? view.house_graphic[i] : (h0.build_state == 0 ? 0 : 10);
        if (h0.build_state != 0 || graphic != 0) continue;
        const int nation = h0.player >= 0 && h0.player < static_cast<int>(view.nations.size())
                               ? view_house_nation(view, i)
                           : h0.player == 8 ? 5  // a neutral house: BasicParams5.dat
                                            : (h0.player < 0 ? 0 : h0.player % 5);
        if (nation < 0 || nation >= static_cast<int>(g.params.size()) || !g.params[static_cast<std::size_t>(nation)])
            continue;
        const BasicParams& bp = *g.params[static_cast<std::size_t>(nation)];
        if (h0.type >= bp.houses.size()) continue;
        const HouseParams& hp = bp.houses[h0.type];
        if (hp.anim_count == 0) continue;
        const auto it = g.houses.find({nation, h0.type, graphic});
        const HouseGraphics* gfx = it != g.houses.end() && it->second.picture.width > 0 ? &it->second : nullptr;
        if (gfx == nullptr) continue;
        // The house's corner as static_band draws it: cell left x - (anchorX - 0x30), row y - 24 - (anchorY + hgt -
        // 0x30), hgt = house_draw_height (55 for a bridge and a port, else the cell's height + 1).
        const int hgt = house_draw_height(m, h0);
        const int cell_x = h0.cell_x * 96 - (h0.cell_y % 2 == 0 ? 48 : 0) - vx;
        const int left = cell_x - (hp.anchor_x - 0x30);
        const int top = h0.cell_y * 24 - vy - 24 - (static_cast<std::uint16_t>(hp.anchor_y + hgt) - 0x30);
        for (int k = 0; k < std::min<int>(hp.anim_count, 10); ++k) {
            const std::uint16_t kind = hp.anim_kind[static_cast<std::size_t>(k)];
            if (kind >= hg.records.size()) continue;
            const HouseFxGraphics::Record& r = hg.records[kind];
            if (r.pics.empty()) continue;
            const int sx = hp.anim_x[static_cast<std::size_t>(k)], sy = hp.anim_y[static_cast<std::size_t>(k)];
            const int x0 = left - r.ox + sx, y0 = top - r.oy + sy;
            if (x0 >= w || y0 >= h || x0 + r.w <= 0 || y0 + r.h <= 0) continue;
            // The depth at the slot's point: the .hzb there + 2 hgt as a short (DepthFromScreenY(bottom + K, K) and
            // (bottom
            // + K, K + h): the keys bottom + 2 K, the top h more); no depth map: 0. A point with no depth (0x7fff or
            // 0x8000) wraps K below 0 in the original and the animation never shows. Ours (docs/bugs.md B18): a point
            // just off the picture's depth (within kDepthReach, Austria's artillery depot) takes the depth of the
            // nearest point that has one; a point far from any (France's war academy, which carries the power
            // station's smoke points) is left out on purpose.
            int K = 0;
            if (!gfx->depth.empty()) {
                std::int16_t d = static_cast<std::int16_t>(kNoDepth);
                if (!nearest_depth(*gfx, sx, sy, &d)) continue;
                K = static_cast<std::int16_t>(static_cast<std::uint16_t>(d + 2 * house_key_height(m, h0)));
            }
            HouseFx::Slot& slot = fx.slots[{h0.player, h0.cell_x, h0.cell_y, h0.type, k}];
            slot.record = kind;
            slot.drawn = true;
            const int fi = std::clamp(static_cast<int>(static_cast<std::uint8_t>(static_cast<int>(slot.frame))), 0,
                                      static_cast<int>(r.pics.size()) - 1);
            HouseFxQuad q;
            q.pic = &r.pics[static_cast<std::size_t>(fi)];
            q.x0 = x0, q.y0 = y0, q.w = r.w, q.h = r.h;
            q.key = static_cast<double>(y0 + r.h) + 2.0 * K;
            items.push_back({q, y0 + r.h, x0 + r.w});
        }
    }

    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.bottom < b.bottom || (a.bottom == b.bottom && a.right < b.right);
    });
    for (const Item& it : items) out->push_back(it.q);
}

void house_fx_step(HouseFx& fx, const HouseFxGraphics& g) {
    for (auto& [key, slot] : fx.slots) {
        if (!slot.drawn) continue;
        slot.drawn = false;
        if (slot.record < 0 || slot.record >= static_cast<int>(g.records.size())) continue;
        const HouseFxGraphics::Record& r = g.records[static_cast<std::size_t>(slot.record)];
        slot.frame += r.speed;
        if (static_cast<float>(r.frames) <= slot.frame) slot.frame = 0;
    }
}

void house_fx_draw(const HouseFxQuad& q, Target& rt, int band_top, int band_bottom) {
    if (q.pic == nullptr || q.w <= 0 || q.h <= 0 || q.pic->w <= 0 || q.pic->h <= 0) return;
    const HouseFxGraphics::Frame& pic = *q.pic;
    const double top_key = q.key + q.h;


    constexpr float kTint = 0xe5 / 255.0f;
    for (int y = std::max({0, band_top, q.y0}); y < std::min({rt.height, band_bottom, q.y0 + q.h}); ++y) {
        const int ty = (y - q.y0) * pic.h / q.h;
        // The key along the quad: the top edge's at y0, falling by one a row to the bottom edge's at y0 + h.
        const auto key = static_cast<float>(top_key - (y - q.y0));
        for (int x = std::max(0, q.x0); x < std::min(rt.width, q.x0 + q.w); ++x) {
            const int tx = (x - q.x0) * pic.w / q.w;
            const std::uint16_t v = pic.px[static_cast<std::size_t>(ty) * static_cast<std::size_t>(pic.w) + tx];
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(rt.width) + x;
            if (!(key >= rt.key[i] - 0.001f)) continue;
            float* dst = &rt.rgb[i * 3];
            if (pic.argb4444) {
                const float a = static_cast<float>(v >> 12) / 15.0f;
                if (a <= 0) continue;
                const float c[3] = {static_cast<float>(v >> 8 & 15) / 15.0f, static_cast<float>(v >> 4 & 15) / 15.0f,
                                    static_cast<float>(v & 15) / 15.0f};
                for (int ch = 0; ch < 3; ++ch) dst[ch] = c[ch] * kTint * a + dst[ch] * (1.0f - a);
            } else {
                if (v == 0xf81f) continue;
                dst[0] = static_cast<float>(v >> 11) / 31.0f * kTint;
                dst[1] = static_cast<float>(v >> 5 & 63) / 63.0f * kTint;
                dst[2] = static_cast<float>(v & 31) / 31.0f * kTint;
            }
        }
    }
}

}  // namespace antanta
