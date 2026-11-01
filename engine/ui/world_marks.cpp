#include "ui/world_marks.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace antanta::ui {
namespace {

constexpr std::uint32_t kGrey = 0xff808080;
constexpr std::uint32_t kReload = 0xff40b040;
constexpr std::uint32_t kBlack = 0xff000000;   // the box select rectangle (the DC's default pen, low)

// Direct3D's coverage: a quad from x to x + w covers the pixels ceil(x) .. ceil(x + w) - 1.
float snap(float v) { return std::ceil(v); }

void bar(std::vector<MarkQuad>* out, float x0, float y0, float x1, float y1, std::uint32_t argb) {
    const float a = snap(x0), b = snap(x1), c = snap(y0), d = snap(y1);
    if (!(a < b && c < d)) return;
    out->push_back(
        {a, c, b, d, kMarksWhiteX + 1.0f, kMarksWhiteY + 1.0f, kMarksWhiteX + 3.0f, kMarksWhiteY + 3.0f, argb});
}

std::uint32_t player_argb(const WorldGraphics& g, const sim::GameState& s, std::size_t player) {
    const std::size_t c = s.players[player].colour;
    if (c >= g.player_rgb.size()) return 0xffffffff;
    const auto& rgb = g.player_rgb[c];
    return 0xff000000u | std::uint32_t{rgb[0]} << 16 | std::uint32_t{rgb[1]} << 8 | rgb[2];
}

// The four corner brackets on the rectangle (x, y, w, h): quads centred on its corners, Select.444 flipped so the L
// of each points into the rectangle.
void corners(std::vector<MarkQuad>* out, float x, float y, float w, float h) {
    constexpr float sx = kMarksSelectX, sy = kMarksSelectY;
    for (int k = 0; k < 4; ++k) {
        const bool right = (k & 1) != 0, bottom = (k & 2) != 0;
        const float qx = snap(x - 2 + (right ? w : 0)), qy = snap(y - 2 + (bottom ? h : 0));
        out->push_back({qx, qy, qx + 4, qy + 4, right ? sx + 4 : sx, bottom ? sy + 4 : sy, right ? sx : sx + 4,
                        bottom ? sy : sy + 4, 0xffffffff});
    }
}



void digits(std::vector<MarkQuad>* out, int x, int y, unsigned n) {
    const std::string text = std::to_string(n);
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto d = static_cast<float>(text[i] - '0');
        const auto qx = static_cast<float>(x + 5 * static_cast<int>(i));
        out->push_back(
            {qx, static_cast<float>(y), qx + 5, static_cast<float>(y + 7), d * 5, 0, d * 5 + 5, 8, 0xffffffff});
    }
}

struct UnitMark {
    float order_y, order_x;
    float x, y, w, h;  // the mark frame in view pixels
    std::uint8_t player;
    const sim::Unit* u;
};



void set_keys(std::vector<MarkQuad>* out, std::size_t from, const UnitMark& m, bool corners) {
    const double c4 = static_cast<double>(m.u->z) + static_cast<double>(m.u->field_024);
    const double b = static_cast<double>(m.y) + m.h;
    const auto top = static_cast<float>(b + 2 * c4 + m.h - 2), bottom = static_cast<float>(b - 2 + 2 * c4 + 10);
    for (std::size_t i = from; i < out->size(); ++i) {
        MarkQuad& q = (*out)[i];
        q.unit = m.u->serial;
        q.before = corners;
        q.key = corners && i - from >= 2 ? bottom : top;  // corners() makes top left, top right, then the bottom two
    }
}

}  // namespace

void world_marks_units(const WorldGraphics& g, UnitSprites& sprites, const GameFileLoader& load,
                       const sim::GameState& s, const MarksInput& in, std::vector<MarkQuad>* out) {
    // Where render_live_units draws the unit: its screen point moved by the blend offset in whole pixels.
    auto place = [&](std::uint8_t player, const sim::Unit& u, UnitMark* m) {
        const auto [bx, by] = draw_blend_offset(in.blend, u);
        auto [sx, sy] = unit_screen_point(u, in.pos);
        sx += static_cast<float>(std::lround(bx)), sy += static_cast<float>(std::lround(by));
        if (!(-200 < sx && sx < static_cast<float>(in.view_w) + 200 && -200 < sy &&
              sy < static_cast<float>(in.view_h) + 300)) {
            return false;
        }
        const auto f = unit_mark_frame(g, sprites, load, s, player, u);
        if (!f) return false;
        *m = UnitMark{sy + (*f)[1] + (*f)[3], sx, sx + (*f)[0], sy + (*f)[1], (*f)[2], (*f)[3], player, &u};
        return true;
    };

    if (in.selected != nullptr) {
        for (const sim::UnitRef& r : *in.selected) {
            if (r.player != s.my_player) continue;
            const sim::Unit* u = sim::Unit_Find(s, r);
            UnitMark m{};
            if (u != nullptr && place(r.player, *u, &m)) {
                const std::size_t from = out->size();
                corners(out, m.x, m.y, m.w, m.h);
                set_keys(out, from, m, true);
            }
        }
    }
    if (!in.show_health) return;
    std::vector<UnitMark> marks;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        const sim::Player& pl = s.players[p];
        for (std::uint32_t k = pl.first_unit; k != sim::kNoSlot; k = pl.unit_slots[k].next) {
            const sim::Unit& u = pl.unit_slots[k];
            UnitMark m{};
            if (u.is_deleted == 0 && place(static_cast<std::uint8_t>(p), u, &m)) marks.push_back(m);
        }
    }
    std::stable_sort(marks.begin(), marks.end(), [](const UnitMark& a, const UnitMark& b) {
        return a.order_y < b.order_y || (a.order_y == b.order_y && a.order_x < b.order_x);
    });
    for (const UnitMark& m : marks) {
        const std::size_t from = out->size();
        const sim::Unit& u = *m.u;
        const sim::UnitType& t = s.unit_types[sim::Unit_Nation(s, m.player, u) % 6][u.type % 40];
        const float top = m.y - 2;

        const std::uint16_t max_hp = t.max_hp[m.player % 8];
        const float fill = max_hp != 0 ? std::clamp(m.w * u.hp / static_cast<float>(max_hp), 0.0f, m.w) : 0.0f;
        bar(out, m.x + fill, top, m.x + m.w, top + 3, kGrey);
        bar(out, m.x, top, m.x + fill, top + 3, player_argb(g, s, m.player));
        // Reload, 4 px higher: w - cooldown * w / ReloadTime[player] in whole pixels.
        const bool mine = m.player == s.my_player && (t.anim_size[6] != 0 || t.is_air != 0);

        if (mine || (u.weapon_count != 0 && u.type != 0x18 && t.is_ship == 0)) {
            const int w = static_cast<int>(m.w);
            const std::uint16_t reload = t.reload[m.player % 8];
            const int rw = reload != 0 ? w - static_cast<int>(u.field_0ee) * w / reload : w;
            bar(out, m.x, top - 4, m.x + static_cast<float>(std::max(0, rw)), top - 1, kReload);
        }

        if (m.player == s.my_player && u.field_0ea != 0xff) {
            const float c4 = u.z + u.field_024;
            const int y = static_cast<int>(top - 8 + m.h + c4) - static_cast<int>(c4 + m.h);
            digits(out, static_cast<int>(m.x), y, u.field_0ea);
        }
        set_keys(out, from, m, false);
    }
}

void world_marks_houses(const WorldGraphics& g, const LiveView& lv, const sim::GameState& s, ViewPos pos, int view_w,
                        int view_h, HouseBoxCache& cache, std::vector<MarkQuad>* out) {
    const int vx = view_x(pos), vy = view_y(pos);
    const std::size_t n = std::min({lv.houses.size(), lv.house_refs.size(), lv.view.house_graphic.size()});
    for (std::size_t i = 0; i < n; ++i) {
        const MapHouse& mh = lv.houses[i];
        const sim::HouseRef& ref = lv.house_refs[i];

        if (ref.player >= s.players.size() || ref.player >= 8 || mh.player != ref.player) continue;
        const auto& houses = s.players[ref.player].houses;
        if (ref.slot >= houses.size()) continue;
        const sim::House& h = houses[ref.slot];
        if (h.is_destroyed != 0 || static_cast<std::size_t>(ref.player) >= lv.view.nations.size()) continue;
        const auto it = g.houses.find({lv.view.nations[ref.player], mh.type, lv.view.house_graphic[i]});
        if (it == g.houses.end() || it->second.picture.width == 0) continue;
        const HouseGraphics& hg = it->second;
        auto box = cache.find(&hg);
        if (box == cache.end()) {  // the opaque pixels' bounding box (x0, y0, x1, y1 inclusive)
            const Image& pic = hg.picture;
            std::array<int, 4> b{pic.width, pic.height, -1, -1};
            for (int y = 0; y < pic.height; ++y) {
                for (int x = 0; x < pic.width; ++x) {
                    if (pic.rgba[(static_cast<std::size_t>(y) * pic.width + x) * 4 + 3] == 0) continue;
                    b = {std::min(b[0], x), std::min(b[1], y), std::max(b[2], x), std::max(b[3], y)};
                }
            }
            box = cache.emplace(&hg, b).first;
        }
        const std::array<int, 4>& b = box->second;
        if (b[2] < 0) continue;

        const int cx = std::clamp(mh.cell_x, 0, std::max(0, lv.map.width - 1));
        const int cy = std::clamp(mh.cell_y, 0, std::max(0, lv.map.height - 1));
        const std::size_t ci = static_cast<std::size_t>(cy) * static_cast<std::size_t>(lv.map.width) + cx;
        const int hgt = (ci < lv.map.cells.size() ? lv.map.cells[ci].height : 0) + 1;
        const int x0 = mh.cell_x * 96 - (mh.cell_y % 2 == 0 ? 48 : 0) - vx + 48 - hg.anchor_x;
        const int y0 = mh.cell_y * 24 - vy + 24 - hgt - hg.anchor_y;


        const int left = x0 + b[0], top = y0 + b[1], right = x0 + b[2];
        if (!(0 < top && top - 3 < view_h && left < view_w && 0 < right)) continue;
        const sim::HouseType& ht = sim::House_Type(s, ref.player, h);
        float frac = h.max_hp != 0 ? h.hp / static_cast<float>(h.max_hp) : 0.0f;
        if (h.field_020 != 0) frac = ht.work_rate != 0 ? 1.0f - static_cast<float>(h.field_020) / ht.work_rate : 0.0f;
        frac = std::clamp(frac, 0.0f, 1.0f);
        const auto fill = static_cast<float>(left + static_cast<int>(static_cast<float>(right - left) * frac));
        bar(out, static_cast<float>(left), static_cast<float>(top - 3), static_cast<float>(right),
            static_cast<float>(top), kGrey);
        bar(out, static_cast<float>(left), static_cast<float>(top - 3), fill, static_cast<float>(top),
            player_argb(g, s, ref.player));
    }
}

void world_marks_box(float x0, float y0, float x1, float y1, std::vector<MarkQuad>* out) {
    const float l = std::floor(std::min(x0, x1)), r = std::floor(std::max(x0, x1));
    const float t = std::floor(std::min(y0, y1)), b = std::floor(std::max(y0, y1));
    bar(out, l, t, r + 1, t + 1, kBlack);
    bar(out, l, b, r + 1, b + 1, kBlack);
    bar(out, l, t + 1, l + 1, b, kBlack);
    bar(out, r, t + 1, r + 1, b, kBlack);
}

}  // namespace antanta::ui
