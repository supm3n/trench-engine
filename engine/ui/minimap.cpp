#include "ui/minimap.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

#include "sim/movement.hpp"

namespace antanta::ui {

MinimapLayout minimap_layout(int w, int h, int k, bool shown) {
    MinimapLayout l;
    l.scale = k = std::max(1, k);
    l.shown = shown;
    l.width = w, l.height = h;
    l.size = kMinimapSize * k;
    l.x = w - 213 * k;
    l.y = shown ? h - 213 * k : h - 13 * k;
    l.plate = {l.x - 8 * k, l.y - 36 * k, 208 * k, 40 * k};
    l.left = {l.x - 10 * k, l.y - 8 * k, 11 * k, 212 * k};
    l.arrow = {l.x + 168 * k, l.y - 36 * k, 33 * k, 30 * k};
    return l;
}

bool minimap_contains(const MinimapLayout& l, int x, int y) {
    return l.shown && x > l.x && x < l.width - 12 * l.scale && y > l.y && y < l.height - 12 * l.scale;
}

bool minimap_hud_contains(const MinimapLayout& l, int x, int y) {
    if (l.shown) return x > l.x && x < l.width - 12 * l.scale && y > l.y - 44 * l.scale && y < l.height - 12 * l.scale;
    const IRect& p = l.plate;
    return x >= p.x && x < p.x + p.w && y >= p.y && y < p.y + p.h;
}

bool minimap_arrow_contains(const MinimapLayout& l, int x, int y) {
    const IRect& a = l.arrow;
    return x > a.x && x < a.x + a.w && y > a.y && y < a.y + a.h;
}

namespace {

struct HudRect {
    int x, y, w, h;  // from (mmx, mmy) in the original's pixels
};


constexpr HudRect kHudRects[kHudButtons] = {{-7, -34, 28, 29},  {24, -34, 26, 23},  {62, -34, 26, 23},
                                            {100, -34, 26, 23}, {138, -34, 26, 23}, {168, -36, 33, 30}};


double cells_x(const MinimapLayout& l, int map_w) {
    return static_cast<double>(static_cast<float>(map_w * static_cast<double>(0.005f))) / l.scale;
}
double cells_y(const MinimapLayout& l, int map_h) {
    return static_cast<double>(static_cast<float>(map_h * static_cast<double>(0.005f))) / l.scale;
}



ViewCells view_size(int view_w, int view_h) {
    static constexpr int kModes[5][4] = {
        {1024, 768, 11, 34}, {1152, 864, 12, 38}, {1280, 960, 14, 42}, {1280, 1024, 14, 44}, {1600, 1200, 17, 51}};
    for (const auto& m : kModes) {
        if (view_w == m[0] && view_h == m[1]) return ViewCells{m[2], m[3]};
    }
    return view_cells(view_w, view_h);
}

}  // namespace

IRect hud_button_rect(const MinimapLayout& l, HudButton b) {
    const HudRect& r = kHudRects[static_cast<int>(b)];
    return {l.x + r.x * l.scale, l.y + r.y * l.scale, r.w * l.scale, r.h * l.scale};
}

int hud_button_hit(const MinimapLayout& l, int x, int y) {
    for (int i = 0; i < kHudButtons; ++i) {
        const IRect r = hud_button_rect(l, static_cast<HudButton>(i));
        if (x > r.x && x < r.x + r.w && y > r.y && y < r.y + r.h) return i;
    }
    return -1;
}

std::string hud_button_tooltip(const StringTable& strings, HudButton b, bool minimap_shown) {
    static constexpr const char* kKeys[kHudButtons] = {"IDS_SHOW_ZADANIE", "IDS_ECONOM_ASSISTANT", "IDS_WAR_ASSISTANT",
                                                       "IDS_HELP",         "IDS_OPTIONS",          "IDS_MINIMAP_HIDE"};
    const std::string key = b == HudButton::Arrow && !minimap_shown ? "IDS_MINIMAP_SHOW" : kKeys[static_cast<int>(b)];
    const auto it = strings.find(key);
    return it != strings.end() ? it->second : key;
}

std::array<HudButtonDraw, kHudButtons> hud_buttons_draw(const MinimapLayout& l, const HudButtonsState& st) {
    auto make = [&](HudButton b, int frame) {
        HudButtonDraw d;
        d.button = b, d.frame = frame;
        d.dst = hud_button_rect(l, b);
        // The pictures are taller or wider than the strict hover rectangle; they start at its top left.
        d.dst.w = (b == HudButton::Objective || b == HudButton::Arrow ? 33 : 26) * l.scale;
        return d;
    };
    // Help and Play menu: held while hovered 1, hover 2, else 0.
    auto three = [&](HudButton b) {
        const bool over = st.hover == static_cast<int>(b);
        return make(b, over ? (st.left_down ? 1 : 2) : 0);
    };
    return {make(HudButton::Military, st.military ? 1 : 0),
            make(HudButton::Economic, st.economic ? 1 : 0),
            three(HudButton::Help),
            three(HudButton::PlayMenu),
            make(HudButton::Arrow, l.shown ? 0 : 1),
            make(HudButton::Objective, 0)};
}

HudTooltip hud_button_tooltip_line(const MinimapLayout& l, const StringTable& strings, const HudButtonsState& st) {
    HudTooltip t;
    if (st.hover < 0 || st.hover >= kHudButtons) return t;
    const auto b = static_cast<HudButton>(st.hover);
    if ((b == HudButton::Help || b == HudButton::PlayMenu) && st.left_down) return t;
    t.text = hud_button_tooltip(strings, b, l.shown);

    t.x = l.width / l.scale - 213 + 24;
    t.y = l.height / l.scale - (l.shown ? 213 : 13) - 54;
    return t;
}

sim::Command hud_advisor_order(std::uint8_t player, bool military) {
    sim::Command c;
    c.kind = sim::Command::Kind::ToggleAdvisor;
    c.player = player;
    sim::OrderArg a, b;
    a.raw = player, b.raw = military ? 1u : 0u;
    c.args = {a, b};
    return c;
}

std::vector<sim::Command> game_options_orders(std::uint8_t player, const GameAdvisors& a) {
    auto order = [player](sim::Command::Kind kind, const std::int32_t* v, std::size_t n) {
        sim::Command c;
        c.kind = kind;
        c.player = player;
        sim::OrderArg p, values;
        p.raw = player;
        values.tag = sim::OrderArg::Tag::Array;  // the recorder's a6: six ints (the military call reads four)
        values.values.assign(v, v + n);
        values.values.resize(6, 0);
        c.args = {p, values};
        return c;
    };
    const std::int32_t mil[] = {a.military[0], a.military[1], a.military[2], a.military[3]};
    const std::int32_t eco[] = {a.economic[0], a.economic[1], a.economic[2],
                                a.economic[3], a.economic[4], a.economic[5]};
    std::vector<sim::Command> out = {order(sim::Command::Kind::Act_a53f0, mil, 4),
                                     order(sim::Command::Kind::Act_a5380, eco, 6)};
    sim::OrderArg conduct;
    conduct.raw = a.conduct;
    out[0].args.push_back(conduct);
    return out;
}

std::array<int, 4> minimap_view_rect(const MinimapLayout& l, int map_w, int map_h, double cam_x, double cam_y,
                                     int view_w, int view_h) {
    const double sx = cells_x(l, map_w), sy = cells_y(l, map_h);
    const ViewCells v = view_size(view_w, view_h);
    const int k = l.scale;
    const int x0 = static_cast<int>(cam_x / 96.0 / sx + l.x - 2.0 * k);
    const int y0 = static_cast<int>(cam_y / 24.0 / sy + l.y + 2.0 * k);
    const int x1 = static_cast<int>(static_cast<float>(x0) + v.cols / sx + 3.0 * k);
    const int y1 = static_cast<int>(static_cast<float>(y0) + v.rows / sy - 1.0 * k);
    return {x0, y0, x1, y1};
}

ViewPos minimap_camera_cell(const MinimapLayout& l, int map_w, int map_h, int x, int y, int view_w, int view_h) {
    const double sx = cells_x(l, map_w), sy = cells_y(l, map_h);
    const ViewCells v = view_size(view_w, view_h);
    const double fx = ((x - l.x) - (v.cols / sx) * 0.5) * sx;
    const double fy = static_cast<float>(((y - l.y) - (v.rows / sy) * 0.5) * sy);
    int cx = fx <= 1.0 ? 2 : static_cast<int>(fx);  // so 1 < fx < 2 gives 1, as there
    int cy = fy <= 1.0 ? 2 : static_cast<int>(fy);
    cy = std::min(cy, map_h - v.rows - 10);
    cx = std::min(cx, map_w - v.cols - 2);
    return ViewPos{cx, cy};
}

std::array<double, 2> minimap_camera_point(const MinimapLayout& l, int map_w, int map_h, int x, int y, int view_w,
                                           int view_h) {
    const double sx = cells_x(l, map_w), sy = cells_y(l, map_h);
    return {(x - l.x) * sx * 96.0 - view_w * 0.5, (y - l.y) * sy * 24.0 - view_h * 0.5};
}

WorldPoint minimap_world_point(const MinimapLayout& l, int map_w, int map_h, int x, int y) {
    const double sx = cells_x(l, map_w), sy = cells_y(l, map_h);
    return {static_cast<int>((x - l.x) * sx * 96.0 + 48.0), static_cast<int>((y - l.y) * sy * 24.0 + 12.0)};
}

std::uint32_t rgb565(std::uint32_t rgb) {
    const std::uint32_t r = (rgb >> 19) & 31, g = (rgb >> 10) & 63, b = (rgb >> 3) & 31;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}

namespace {

constexpr std::uint32_t kObjectColour = 0xE4F6;
constexpr std::uint32_t kWaterColour = 0x264758;

std::uint32_t expand565(std::uint32_t v) {
    const std::uint32_t r = (v >> 11) & 31, g = (v >> 5) & 63, b = v & 31;
    return (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
}


double scale_of(int cells) { return static_cast<float>(cells * static_cast<double>(0.005f)); }


std::int64_t cell_of(int map_w, int map_h, double sx, double sy, int px, int py, int k) {
    std::int64_t i = 0;
    if (k == 1) {
        i = sim::Ftol(static_cast<double>(map_w) * static_cast<float>(py) * sy + static_cast<double>(px) * sx);
    } else {
        i = static_cast<std::int64_t>(map_w) * sim::Ftol(py * sy / k) + sim::Ftol(px * sx / k);
    }
    return i >= 0 && i < static_cast<std::int64_t>(map_w) * map_h ? i : -1;
}


bool water_at(const sim::Map& m, int px, int py, int k) {
    const int gw = 2 * m.width, gh = m.height;
    if (m.obstacle.size() != static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh)) return false;
    const double gsx = scale_of(gw), gsy = scale_of(gh);
    std::int64_t i = 0;
    if (k == 1) {
        const double fx = static_cast<float>(px * gsx), fy = py * gsy;
        if (!(fx < gw && fy < gh && fy >= 0 && fx >= 0)) return false;
        i = sim::Ftol(static_cast<double>(py * gw) * gsy + fx);
    } else {
        const std::int32_t x = sim::Ftol(px * gsx / k), y = sim::Ftol(py * gsy / k);
        if (x < 0 || y < 0 || x >= gw || y >= gh) return false;
        i = static_cast<std::int64_t>(y) * gw + x;
    }
    return i >= 0 && i < static_cast<std::int64_t>(m.obstacle.size()) && m.obstacle[static_cast<std::size_t>(i)] != 0;
}

}  // namespace

std::vector<std::uint32_t> minimap_static(const sim::GameState& s, const MinimapPalette& pal, int k, bool tile_slip,
                                          bool houses) {
    k = std::max(1, k);
    const int n = kMinimapSize * k;
    std::vector<std::uint32_t> px(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), 0);
    const sim::Map& m = s.map;
    const int W = m.width, H = m.height;
    if (W <= 0 || H <= 0) return px;
    const double sx = scale_of(W), sy = scale_of(H);
    auto tile = [&](std::size_t t) -> std::array<std::uint8_t, 3> {
        return t < pal.tiles.size() ? pal.tiles[t] : std::array<std::uint8_t, 3>{};
    };
    for (int py = 0; py < n; ++py) {
        for (int x = 0; x < n; ++x) {
            const std::int64_t i = cell_of(W, H, sx, sy, x, py, k);
            if (i < 0) continue;
            const auto ci = static_cast<std::size_t>(i);
            std::uint32_t& out =
                px[static_cast<std::size_t>(py) * static_cast<std::size_t>(n) + static_cast<std::size_t>(x)];
            const std::uint16_t o = ci < m.objects.size() ? m.objects[ci] : 0;
            if (o >= 0x65 && o < 1000) {
                out = expand565(kObjectColour);
                continue;
            }
            if (houses && o >= 1000 && o < 9000) {
                const std::size_t p = o / 1000 - 1, slot = o % 1000;
                if (p < s.players.size() && slot < s.players[p].houses.size() &&
                    s.players[p].houses[slot].is_destroyed == 0) {
                    out = rgb565(p < pal.players.size() ? pal.players[p] : 0);
                    continue;
                }
            }
            if (water_at(m, x, py, k)) {
                out = rgb565(kWaterColour);
                continue;
            }
            const sim::TerrainCell c = ci < m.terrain.size() ? m.terrain[ci] : sim::TerrainCell{};
            const std::size_t base = c.tile & 0x7fff;
            std::array<std::uint8_t, 3> b{};
            if (tile_slip) {
                b = tile(base);
            } else if (base > 0) {
                b = tile(base - 1);
            }
            int sum[3] = {b[0], b[1], b[2]}, cnt = 0;
            for (std::size_t j = 0; j < 4; ++j) {  // the first four of the nine sub-tiles, whatever bit 0x8000 says
                if (c.sub[j] == 0) continue;
                const auto v = tile(c.sub[j] - 1u);
                for (std::size_t ch = 0; ch < 3; ++ch) sum[ch] += v[ch];
                ++cnt;
            }
            const std::uint8_t h = ci < m.heights.size() ? m.heights[ci] : 0;
            const double f = (h * static_cast<double>(0.0019608f) + static_cast<double>(0.5f)) / (cnt + 1);
            std::uint32_t rgb = 0;
            for (int ch = 0; ch < 3; ++ch) {
                rgb = rgb << 8 | (static_cast<std::uint32_t>(sim::Ftol(sum[ch] * f)) & 0xff);
            }
            out = rgb565(rgb);
        }
    }

    for (std::size_t j = 0; j < s.mine_site_count && j < s.mine_sites.size(); ++j) {
        const int cx = sim::Ftol(s.mine_sites[j][0] * k / sx), cy = sim::Ftol(s.mine_sites[j][1] * k / sy);
        for (int dy = 0; dy < k; ++dy) {
            for (int dx = 0; dx < k; ++dx) {
                if (cx + dx >= 0 && cx + dx < n && cy + dy >= 0 && cy + dy < n) {
                    px[static_cast<std::size_t>(cy + dy) * static_cast<std::size_t>(n) +
                       static_cast<std::size_t>(cx + dx)] = 0xffffff;
                }
            }
        }
    }
    return px;
}

Minimap save_preview(const sim::GameState& s, const MinimapPalette& pal, bool tile_slip) {
    const std::vector<std::uint32_t> px = minimap_static(s, pal, 1, tile_slip, false);
    Minimap m;
    m.rgb565.reserve(px.size());
    for (std::uint32_t v : px) {  // back to the 16 bits (the widening repeats the top bits)
        m.rgb565.push_back(
            static_cast<std::uint16_t>((v >> 19 & 0x1f) << 11 | (v >> 10 & 0x3f) << 5 | (v >> 3 & 0x1f)));
    }
    return m;
}

namespace {

std::uint32_t selected_colour(const sim::GameState& s) { return rgb565(s.climate == 3 ? 0xff8000 : 0x8c8c8c); }

MinimapDot dot_at(const sim::GameState& s, double x, double y, int k, std::uint32_t rgb) {
    const double sx = scale_of(s.map.width), sy = scale_of(s.map.height);
    return MinimapDot{sim::Ftol(x * k / sx), sim::Ftol(y * k / sy), k, rgb};
}

}  // namespace

std::vector<MinimapDot> minimap_dots(const sim::GameState& s, const MinimapPalette& pal, const Selection& sel, int k) {
    std::vector<MinimapDot> out;
    k = std::max(1, k);
    const int n = kMinimapSize * k;
    if (s.map.width <= 0 || s.map.height <= 0) return out;
    const std::uint32_t chosen = selected_colour(s);
    // A player whose dots are all one colour (no selected unit of it can be among them) is read from its slots in
    // memory order, each pixel once: the same picture, and far faster than walking a list of tens of thousands of
    // units (docs/bugs.md B59). A slot is in the list when its id points back to it.
    std::vector<std::uint8_t> seen;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        const sim::Player& pl = s.players[p];
        if (pl.is_lost != 0) continue;
        const std::uint32_t own = rgb565(p < pal.players.size() ? pal.players[p] : 0);
        const bool picking = p == s.my_player && p == sel.player && !sel.units.empty();
        if (!picking) {
            seen.assign(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), 0);
            for (std::uint32_t slot = 0; slot < pl.unit_slots.size(); ++slot) {
                const sim::Unit& u = pl.unit_slots[slot];
                if (u.is_deleted != 0 || u.field_108 == 0) continue;
                if (u.id >= pl.id_slots.size() || pl.id_slots[u.id] != slot) continue;
                const MinimapDot d = dot_at(s, u.map_x, u.map_y, k, own);
                if (d.x < 0 || d.y < 0 || d.x >= n || d.y >= n) continue;  // the original has no bounds test
                std::uint8_t& mark =
                    seen[static_cast<std::size_t>(d.y) * static_cast<std::size_t>(n) + static_cast<std::size_t>(d.x)];
                if (mark == 0) mark = 1, out.push_back(d);
            }
            continue;
        }
        // The selection's id and serial pairs in a set, so a unit's test is one lookup and not a walk of the whole
        // selection (Ctrl+A on an army of thousands: thousands of walks a frame).
        std::unordered_set<std::uint64_t> picks;
        picks.reserve(sel.units.size());
        for (const sim::UnitRef& r : sel.units) picks.insert(std::uint64_t{r.id} << 32 | r.serial);
        for (std::uint32_t slot = pl.first_unit; slot != sim::kNoSlot; slot = pl.unit_slots[slot].next) {
            const sim::Unit& u = pl.unit_slots[slot];
            if (u.is_deleted != 0 || u.field_108 == 0) continue;
            const bool picked = picks.count(std::uint64_t{u.id} << 32 | u.serial) != 0;
            const MinimapDot d = dot_at(s, u.map_x, u.map_y, k, picked ? chosen : own);
            if (d.x >= 0 && d.y >= 0 && d.x < n && d.y < n) out.push_back(d);  // the original has no bounds test
        }
    }
    return out;
}

std::vector<MinimapDot> minimap_house_marks(const sim::GameState& s, const Selection& sel, int k) {
    std::vector<MinimapDot> out;
    k = std::max(1, k);
    if (!sel.house || sel.house->player != s.my_player || sel.house->player >= s.players.size()) return out;
    const sim::Player& pl = s.players[sel.house->player];
    if (pl.is_lost != 0 || sel.house->slot >= pl.houses.size()) return out;
    const sim::House& h = pl.houses[sel.house->slot];
    const std::int32_t W = s.map.width, H = s.map.height;
    const std::uint32_t c = selected_colour(s);

    const sim::Cell r = sim::WorldToCell(h.rally_x, h.rally_y);
    if (s.tick % 6 < 3 && r.x > 2 && r.x < W - 2 && r.y > 3 && r.y < H - 3) {
        const std::int32_t cells[5][2] = {{r.x, r.y}, {r.x, r.y - 2}, {r.x - 1, r.y}, {r.x, r.y + 2}, {r.x + 1, r.y}};
        for (const auto& q : cells) out.push_back(dot_at(s, q[0], q[1], k, c));
    }


    const sim::HouseFootprint fp = sim::House_Footprint(s, sel.house->player, h);
    std::int32_t x0 = h.map_x, y0 = h.map_y;
    for (int a = 0; a < fp.a; ++a) {
        std::int32_t x = x0, y = y0;
        for (int b = 0; b < fp.b; ++b) {
            if (x >= 0 && x < W && y >= 0 && y < H) out.push_back(dot_at(s, x, y, k, c));
            if (y % 2 != 0) ++x;
            --y;
        }
        if (y0 % 2 == 0) --x0;
        --y0;
    }
    return out;
}

void minimap_mark_add(std::vector<MinimapMark>& marks, int map_w, int map_h, std::int32_t cell_x, std::int32_t cell_y) {
    if (map_w <= 0 || map_h <= 0) return;
    MinimapMark m;
    m.x = static_cast<float>(cell_x / scale_of(map_w)), m.y = static_cast<float>(cell_y / scale_of(map_h));
    m.loops = 20 / kBattleFrames + 1;
    marks.push_back(m);
}

void minimap_marks_advance(std::vector<MinimapMark>& marks) {
    for (MinimapMark& m : marks) {
        m.frame += 0.5f;
        if (m.frame >= static_cast<float>(kBattleFrames)) m.frame = 0, --m.loops;
    }
    std::erase_if(marks, [](const MinimapMark& m) { return m.loops <= 0; });
}

std::vector<MinimapMarkDraw> minimap_marks_draw(const std::vector<MinimapMark>& marks, int k) {
    std::vector<MinimapMarkDraw> out;
    k = std::max(1, k);
    for (const MinimapMark& m : marks) {
        // A Direct3D quad at the float point less 8: the first pixel it covers is the next whole one (pixel centres on
        // whole coordinates), so (66.67, 66.67) draws from 59 (minimap_view_moved.png).
        out.push_back(MinimapMarkDraw{static_cast<int>(std::ceil((m.x - 8.0) * k)),
                                      static_cast<int>(std::ceil((m.y - 8.0) * k)), sim::Ftol(m.frame) & 0xff});
    }
    return out;
}

}  // namespace antanta::ui
