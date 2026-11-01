#include "ui/hud.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_set>

#include "formats/sprites.hpp"
#include "render/live.hpp"
#include "sim/fog.hpp"
#include "sim/movement.hpp"
#include "ui/select_keys.hpp"

namespace antanta::ui {
namespace {

using sim::Command;
using sim::OrderArg;

OrderArg raw(std::uint32_t v) {
    OrderArg a;
    a.raw = v;
    return a;
}

OrderArg unit_arg(const sim::UnitRef& r) {
    OrderArg a;
    a.tag = OrderArg::Tag::Unit;
    a.unit = r;
    return a;
}

OrderArg house_arg(const sim::HouseRef& r) {
    OrderArg a;
    a.tag = OrderArg::Tag::House;
    a.house = r;
    return a;
}

std::uint32_t fbits(float f) { return std::bit_cast<std::uint32_t>(f); }

// A unit the HUD can see and order: alive and not inside a house or a transport.
bool listed(const sim::Unit& u) { return u.is_deleted == 0 && u.work != 3 && u.work != 0x14 && !u.in_transport; }

int height_at(const sim::GameState& s, int cx, int row) {
    cx = std::clamp(cx, 0, static_cast<int>(s.map.width) - 1);
    row = std::clamp(row, 0, static_cast<int>(s.map.height) - 1);
    return s.map.heights[static_cast<std::size_t>(row) * s.map.width + static_cast<std::size_t>(cx)];
}

// The cell (as in the map's object grid) that holds house `slot` of player p: its own cell if the object there is
// the house, else the nearest footprint cell.
bool house_cell(const sim::GameState& s, std::uint8_t p, std::uint16_t slot, std::int32_t* cx, std::int32_t* cy) {
    const sim::House& h = s.players[p].houses[slot];
    const std::uint16_t id = static_cast<std::uint16_t>((p + 1) * 1000 + slot);
    const std::int32_t W = s.map.width, H = s.map.height;
    std::int32_t best = 1 << 30;
    for (std::int32_t y = h.map_y - 12; y <= h.map_y + 12; ++y) {
        for (std::int32_t x = h.map_x - 6; x <= h.map_x + 6; ++x) {
            if (x < 0 || y < 0 || x >= W || y >= H) continue;
            if (s.map.objects[static_cast<std::size_t>(y) * W + x] != id) continue;
            const std::int32_t d = 2 * std::abs(x - h.map_x) + std::abs(y - h.map_y);
            if (d < best) best = d, *cx = x, *cy = y;
        }
    }
    return best != 1 << 30;
}

// The debug HUD's names (a developer screen, English on purpose: docs/languages.md 6); the game's own come from the
// string table.
const char* const kUnitNames[] = {
    "Worker",          "Worker (T)",   "Soldier",       "Officer",        "Horseman",   "Mortar",     "Machine gun",
    "Scout plane",     "Fighter",      "Bomber",        "Heavy bomber",   "Light tank", "Heavy tank", "AA gun",
    "Special soldier", "Flamethrower", "Special horse", "Transport ship", "Destroyer",  "Cruiser",    "Battleship",
    "Howitzer",        "Cannon",       "SP howitzer",   "Armoured car",   "General 1",  "General 2",  "General 3"};
const char* const kHouseNames[] = {
    "Power plant", "Bunker (4)",  "Science academy", "Building academy", "Barracks",   "War academy", "Town centre",
    "Bunker (6)",  "Market",      "House",           "Iron mine",        "Gold mine",  "Sawmill",     "Oil well",
    "Mint",        "Barn (farm)", "Gun depot",       "Hospital",         "Medic post", "Tank depot",  "Hangar",
    "Watch tower", "Port",        "Stable",          "SP gun depot"};

}  // namespace

WorldPoint screen_to_world(ViewPos pos, int sx, int sy) { return {sx + view_x(pos), sy + view_y(pos)}; }

bool pick_cell(const sim::GameState& s, std::int32_t x, std::int32_t y, std::int32_t* cx, std::int32_t* cy) {
    const std::int32_t W = s.map.width, H = s.map.height;
    if (W == 0 || H == 0) return false;

    const float fx = static_cast<float>(static_cast<std::uint32_t>(x));
    const float fy = static_cast<float>(static_cast<std::uint32_t>(y));
    const sim::Cell c = sim::PickCell(s.map, fx, fy);
    if (c.x < 0 || c.y < 0 || c.x >= W || c.y >= H) return false;
    *cx = c.x, *cy = c.y;
    return true;
}

namespace {


std::array<int, 2> unit_canvas(const sim::GameState& s, const WorldGraphics* g, std::uint8_t player,
                               const sim::Unit& u) {
    if (g != nullptr && player < s.players.size()) {
        const auto& params = g->params[static_cast<std::size_t>(sim::Unit_Nation(s, player, u) % 6)];
        if (params && u.type < params->units.size()) return {params->units[u.type].width, params->units[u.type].height};
    }
    return {128, 128};
}

bool in_box(const UnitBox& b, std::int32_t x, std::int32_t y) { return b.x0 < x && x < b.x1 && b.y0 < y && y < b.y1; }

// How low a unit is on the screen, the click searches' order: Y - height - altitude (float, as there).
float screen_depth(const sim::Unit& u) { return (u.y - u.z) - u.field_024; }


sim::Cell pick(const sim::GameState& s, std::int32_t x, std::int32_t y, float dy) {
    const float fx = static_cast<float>(static_cast<std::uint32_t>(x));
    const float fy = static_cast<float>(static_cast<std::uint32_t>(y));
    return sim::PickCell(s.map, fx, fy + dy);
}





template <class F>
std::optional<sim::UnitRef> cell_search(const sim::GameState& s, const WorldGraphics* g, WorldPoint w, F ok) {
    const sim::Map& m = s.map;
    for (const float dy : {0.0f, 400.0f}) {
        const sim::Cell c = pick(s, w.x, w.y, dy);
        for (std::uint8_t k = 0; k < 9; ++k) {
            const sim::Cell d = sim::Cell_Step(sim::Cell_Step(c, k), k);
            std::optional<sim::UnitRef> best;
            float best_y = -100.0f;
            for (std::uint8_t j = 0; j < 9; ++j) {
                const sim::Cell e = sim::Cell_Step(d, j);
                if (e.x < 0 || e.y < 0 || e.x >= m.width || e.y >= m.height) continue;
                const std::vector<sim::UnitRef>& list = m.cell_units[static_cast<std::size_t>(e.y) * m.width + e.x];
                for (std::size_t i = 0; i < list.size() && i < 40; ++i) {
                    const sim::Unit* u = sim::Unit_Find(s, list[i]);
                    if (u == nullptr || u->is_deleted != 0 || !ok(list[i].player)) continue;
                    if (!in_box(unit_click_box(s, g, list[i].player, *u), w.x, w.y)) continue;
                    if (best_y < screen_depth(*u)) best_y = screen_depth(*u), best = list[i];
                }
            }
            if (best) return best;
        }
    }
    return std::nullopt;
}

}  // namespace

UnitBox unit_click_box(const sim::GameState& s, const WorldGraphics* g, std::uint8_t player, const sim::Unit& u) {
    const auto [w, h] = unit_canvas(s, g, player, u);
    const double qw = w >> 2, qh = h >> 2;
    const double x = u.x, y = u.y, z = u.z, alt = u.field_024;
    return {sim::Ftol(x - qw), sim::Ftol(y - qh - z - alt), sim::Ftol(qw + x), sim::Ftol(qh + y - z - alt)};
}

std::optional<sim::UnitRef> unit_at(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, int sx, int sy,
                                    int player) {
    const WorldPoint w = screen_to_world(pos, sx, sy);
    std::optional<sim::UnitRef> best;
    float best_y = -100.0f;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        if (player >= 0 && static_cast<int>(p) != player) continue;
        const auto pp = static_cast<std::uint8_t>(p);
        const sim::Player& pl = s.players[p];
        for (std::uint32_t k = pl.first_unit; k != sim::kNoSlot; k = pl.unit_slots[k].next) {
            const sim::Unit& u = pl.unit_slots[k];
            if (!listed(u) || !key_selectable(s, pp, u) || !in_box(unit_click_box(s, g, pp, u), w.x, w.y)) continue;
            if (best_y < screen_depth(u)) best_y = screen_depth(u), best = sim::UnitRef{pp, u.id, u.serial};
        }
    }
    return best;
}

std::optional<sim::UnitRef> enemy_at(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, int sx, int sy,
                                     std::uint8_t player) {
    return cell_search(s, g, screen_to_world(pos, sx, sy),
                       [&](std::uint8_t owner) { return sim::IsEnemy(s, player, owner); });
}

std::optional<sim::UnitRef> info_unit_at(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, int sx, int sy,
                                         std::uint8_t player) {
    const sim::Map& m = s.map;
    const WorldPoint w = screen_to_world(pos, sx, sy);
    const auto cells = static_cast<std::int64_t>(m.width) * m.height;
    for (const float dy : {0.0f, 400.0f}) {  // the second time only aircraft (the type's +5)
        const sim::Cell c = pick(s, w.x, w.y, dy);
        if (c.x < 0 || c.y < 0 || c.x >= m.width || c.y >= m.height) break;
        for (std::uint8_t k = 0; k < 9; ++k) {
            const sim::Cell e = sim::Cell_Step(c, k);
            const std::int64_t i = std::int64_t{m.width} * e.y + e.x;  // as there: no test of x alone
            if (i < 0 || i >= cells) continue;
            const std::vector<sim::UnitRef>& list = m.cell_units[static_cast<std::size_t>(i)];
            for (std::size_t j = 0; j < list.size() && j < 40; ++j) {
                const sim::UnitRef r = list[j];
                const sim::Unit* u = sim::Unit_Find(s, r);
                if (u == nullptr || r.player == player || u->is_deleted != 0) continue;
                if (dy != 0 && sim::Unit_Type(s, r.player, *u).is_air == 0) continue;
                if (!in_box(unit_click_box(s, g, r.player, *u), w.x, w.y)) continue;
                if (sim::Fog_CellMask(s.fog, u->map_x, u->map_y, u->map_x + 1, u->map_y + 1).at(0) == 0) continue;
                return r;
            }
        }
    }
    // Ours (docs/bugs.md B312): those cells reach an aircraft only near 400 pixels up and only the upper part of its
    // box, so a plane climbing, diving or circling low is never named. None found: the aircraft search below.
    return other_aircraft_at(s, g, pos, sx, sy, player);
}

std::optional<sim::UnitRef> other_aircraft_at(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, int sx,
                                              int sy, std::uint8_t player) {
    const WorldPoint w = screen_to_world(pos, sx, sy);
    std::optional<sim::UnitRef> best;
    float best_y = -100.0f;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        if (p == player) continue;
        const auto pp = static_cast<std::uint8_t>(p);
        const sim::Player& pl = s.players[p];
        for (std::uint32_t k = pl.first_unit; k != sim::kNoSlot; k = pl.unit_slots[k].next) {
            const sim::Unit& u = pl.unit_slots[k];
            if (!listed(u) || sim::Unit_Type(s, pp, u).is_air == 0) continue;
            if (!in_box(unit_click_box(s, g, pp, u), w.x, w.y) || !(best_y < screen_depth(u))) continue;
            if (sim::Fog_CellMask(s.fog, u.map_x, u.map_y, u.map_x + 1, u.map_y + 1).at(0) == 0) continue;
            best_y = screen_depth(u), best = sim::UnitRef{pp, u.id, u.serial};
        }
    }
    return best;
}

bool point_in_fog(const sim::GameState& s, ViewPos pos, int sx, int sy) {
    const WorldPoint w = screen_to_world(pos, sx, sy);
    std::int32_t cx = 0, cy = 0;
    return s.fog.built && s.fog.on && pick_cell(s, w.x, w.y, &cx, &cy) &&
           sim::Fog_CellMask(s.fog, cx, cy, cx + 1, cy + 1)[0] == 0;
}



static std::optional<sim::UnitRef> transport_at(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, int sx,
                                                int sy, std::uint8_t player) {
    const auto r = cell_search(s, g, screen_to_world(pos, sx, sy), [&](std::uint8_t owner) { return owner == player; });
    if (!r) return std::nullopt;
    const sim::Unit* u = sim::Unit_Find(s, *r);
    if (u == nullptr || (u->type != 0x11 && u->type != 0x18)) return std::nullopt;
    return r;
}

std::vector<sim::UnitRef> units_in_rect(const sim::GameState& s, ViewPos pos, int x0, int y0, int x1, int y1,
                                        std::uint8_t player) {
    std::vector<sim::UnitRef> out;
    if (player >= s.players.size()) return out;
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    const sim::Player& pl = s.players[player];
    for (std::uint32_t k = pl.first_unit; k != sim::kNoSlot; k = pl.unit_slots[k].next) {
        const sim::Unit& u = pl.unit_slots[k];
        if (!listed(u)) continue;
        const auto [fx, fy] = unit_screen_point(u, pos);
        if (fx >= x0 && fx <= x1 && fy - 18 >= y0 && fy - 18 <= y1) out.push_back({player, u.id, u.serial});
    }
    return out;
}

namespace {

// The house's picture as the renderer draws it (render/iso static_band), or null before it is loaded.
const HouseGraphics* house_picture(const sim::GameState& s, const WorldGraphics* g, const sim::House& h,
                                   std::uint8_t player) {
    if (g == nullptr) return nullptr;
    const auto it =
        g->houses.find({sim::House_Nation(s, player, h), h.type, house_les_id(h.graphic_phase, h.graphic_stage, 0)});
    return it == g->houses.end() || it->second.picture.width == 0 ? nullptr : &it->second;
}

}  // namespace

std::array<int, 4> house_rect(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, const sim::House& h,
                              std::uint8_t player) {
    const sim::HouseType& ht = sim::House_Type(s, player, h);
    const int cxs = h.map_x * 96 - (h.map_y % 2 == 0 ? 48 : 0) + 48 - view_x(pos);
    const int cys = h.map_y * 24 - view_y(pos);
    const int hgt = height_at(s, h.map_x, h.map_y) + 1;
    if (const HouseGraphics* hg = house_picture(s, g, h, player)) {  // the opaque pixels' bounding box
        const Image& pic = hg->picture;
        int x0 = pic.width, y0 = pic.height, x1 = -1, y1 = -1;
        for (int y = 0; y < pic.height; ++y) {
            for (int x = 0; x < pic.width; ++x) {
                if (pic.rgba[(static_cast<std::size_t>(y) * pic.width + x) * 4 + 3] == 0) continue;
                x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
            }
        }
        if (x1 < 0) x0 = y0 = 0;
        return {cxs - hg->anchor_x + x0, cys + 24 - hgt - hg->anchor_y + y0, x1 - x0 + 1, y1 - y0 + 1};
    }
    return {cxs - ht.anchor_x, cys + 24 - hgt - ht.anchor_y, ht.sprite_w, ht.sprite_h};
}

std::optional<sim::HouseRef> house_at(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, int sx, int sy) {





    constexpr int R = kHousePickReach;
    std::optional<sim::HouseRef> exact, near;
    std::int64_t exact_order = -1, near_order = -1;
    int near_d2 = 1 << 30;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        for (std::size_t k = 0; k < s.players[p].houses.size(); ++k) {
            const sim::House& h = s.players[p].houses[k];
            if (!(h.hp >= -149000.0f) || h.map_x < 0 || h.map_y < 0 || h.map_x >= s.map.width ||
                h.map_y >= s.map.height) {
                continue;
            }
            const sim::HouseRef ref{static_cast<std::uint8_t>(p), static_cast<std::uint16_t>(k), 0};
            const std::int64_t order = std::int64_t{h.map_y} * 100 + h.map_x;  // painter order: later is in front
            if (const HouseGraphics* hg = house_picture(s, g, h, static_cast<std::uint8_t>(p))) {
                const Image& pic = hg->picture;
                const int px = sx - (h.map_x * 96 - (h.map_y % 2 == 0 ? 48 : 0) + 48 - view_x(pos) - hg->anchor_x);
                const int py =
                    sy - (h.map_y * 24 - view_y(pos) + 24 - (height_at(s, h.map_x, h.map_y) + 1) - hg->anchor_y);
                if (px < -R || py < -R || px >= pic.width + R || py >= pic.height + R) continue;
                auto opaque = [&](int x, int y) {
                    return x >= 0 && y >= 0 && x < pic.width && y < pic.height &&
                           pic.rgba[(static_cast<std::size_t>(y) * pic.width + x) * 4 + 3] != 0;
                };
                if (opaque(px, py)) {
                    if (order > exact_order) exact_order = order, exact = ref;
                    continue;
                }
                if (exact) continue;  // rule 1 already has its house: no need to look for rim pixels
                int d2 = 1 << 30;
                for (int dy = -R; dy <= R; ++dy) {
                    for (int dx = -R; dx <= R; ++dx) {
                        if (dx * dx + dy * dy < d2 && opaque(px + dx, py + dy)) d2 = dx * dx + dy * dy;
                    }
                }
                if (d2 == (1 << 30)) continue;
                if (d2 < near_d2 || (d2 == near_d2 && order > near_order)) near_d2 = d2, near_order = order, near = ref;
            } else {  // no picture loaded: the THouseBasic rectangle, its lower 85 %
                const auto r = house_rect(s, g, pos, h, static_cast<std::uint8_t>(p));
                if (sx < r[0] || sx >= r[0] + r[2] || sy < r[1] + r[3] * 15 / 100 || sy >= r[1] + r[3]) continue;
                if (order > exact_order) exact_order = order, exact = ref;
            }
        }
    }
    if (exact) return exact;
    if (near) return near;
    std::int32_t cx = 0, cy = 0;
    const WorldPoint w = screen_to_world(pos, sx, sy);
    if (!pick_cell(s, w.x, w.y, &cx, &cy)) return std::nullopt;
    const std::uint16_t obj = s.map.objects[static_cast<std::size_t>(cy) * s.map.width + static_cast<std::size_t>(cx)];
    if (obj < 1000 || obj > 8999 || obj / 1000 - 1u >= s.players.size()) return std::nullopt;
    const auto hp = static_cast<std::uint8_t>(obj / 1000 - 1);
    const auto slot = static_cast<std::uint16_t>(obj % 1000);
    if (slot >= s.players[hp].houses.size() || !(s.players[hp].houses[slot].hp >= -149000.0f)) return std::nullopt;
    return sim::HouseRef{hp, slot, 0};
}

void selection_prune(const sim::GameState& s, Selection& sel) {
    // The units the step took out, in a set: a big selection is not walked once for each of them.
    std::unordered_set<std::uint64_t> gone;
    for (const sim::UnitRef& d : s.deselected) gone.insert(std::uint64_t{d.id} << 32 | d.serial);
    std::erase_if(sel.units, [&](const sim::UnitRef& r) {
        const sim::Unit* u = sim::Unit_Find(s, r);
        if (u == nullptr || !listed(*u)) return true;
        return gone.count(std::uint64_t{r.id} << 32 | r.serial) != 0;
    });

    std::erase_if(sel.flagged, [&](const sim::HouseRef& r) {
        return r.player >= s.players.size() || r.slot >= s.players[r.player].houses.size() ||
               s.players[r.player].houses[r.slot].is_destroyed != 0;
    });
    if (sel.house) {
        const sim::HouseRef r = *sel.house;
        if (r.player >= s.players.size() || r.slot >= s.players[r.player].houses.size() ||
            !(s.players[r.player].houses[r.slot].hp > -10000.0f)) {
            sel.house.reset();
        }
    }
}

RightClickTarget right_click_target(const sim::GameState& s, const WorldGraphics* g, std::uint8_t p, ViewPos pos,
                                    int sx, int sy) {




    RightClickTarget t;
    t.w = screen_to_world(pos, sx, sy);
    t.on_map = pick_cell(s, t.w.x, t.w.y, &t.cx, &t.cy);
    const bool fogged =
        t.on_map && s.fog.built && s.fog.on && sim::Fog_CellMask(s.fog, t.cx, t.cy, t.cx + 1, t.cy + 1)[0] == 0;
    auto object_at = [&](std::int32_t x, std::int32_t y) -> std::uint16_t {
        return s.map.objects[static_cast<std::size_t>(y) * s.map.width + static_cast<std::size_t>(x)];
    };
    auto house_of = [&](std::uint16_t obj) -> std::optional<sim::HouseRef> {
        if (obj < 1000 || obj > 8999 || obj / 1000 - 1u >= s.players.size()) return std::nullopt;
        const auto hp = static_cast<std::uint8_t>(obj / 1000 - 1);
        const auto slot = static_cast<std::uint16_t>(obj % 1000);
        if (slot >= s.players[hp].houses.size()) return std::nullopt;
        return sim::HouseRef{hp, slot, 0};
    };
    if (t.on_map) t.house = house_of(object_at(t.cx, t.cy));
    if (!t.house) t.target = enemy_at(s, g, pos, sx, sy, p);
    if (!t.house && !t.target) {  // the picture of a house: a point of its footprint cell
        const auto pic = house_at(s, g, pos, sx, sy);
        std::int32_t hx = 0, hy = 0;
        if (pic && house_cell(s, pic->player, pic->slot, &hx, &hy)) {
            const sim::Cell wp = sim::CellToWorldPoint(s.map, sim::Cell{hx, hy});
            std::int32_t px = 0, py = 0;
            if (pick_cell(s, wp.x + 48, wp.y, &px, &py) && px == hx && py == hy) {
                t.w = WorldPoint{wp.x + 48, wp.y};
                t.cx = hx, t.cy = hy, t.on_map = true;
                t.house = house_of(object_at(t.cx, t.cy));
            }
        }
    }
    if (t.on_map) t.obj = object_at(t.cx, t.cy);
    // Fog (owner's decision 2026-10-16, bugs.md B24): the cursor's test, the cell under the point is out of every sight
    // circle. An enemy there is not a target; the click is a move (right_click adds "ready to fight").
    if ((t.house || t.target) && fogged) {
        const bool enemy_house = t.house && t.house->player != p && sim::IsEnemy(s, p, t.house->player);
        if (enemy_house || t.target) {
            t.house.reset(), t.target.reset();
            t.hidden_enemy = true;
        }
    }
    return t;
}

std::vector<Command> right_click(const sim::GameState& s, const WorldGraphics* g, const Selection& sel, ViewPos pos,
                                 int sx, int sy, std::string* what) {
    std::vector<Command> out;
    const std::uint8_t p = sel.player;
    if (p >= s.players.size()) return out;
    WorldPoint w = screen_to_world(pos, sx, sy);
    std::int32_t cx = -1, cy = -1;
    bool on_map = pick_cell(s, w.x, w.y, &cx, &cy);
    if (sel.units.empty()) {
        if (!sel.house || sel.house->player != p || !on_map) return out;

        Command c;
        c.kind = Command::Kind::Rally;
        c.player = p;
        const float y = static_cast<float>(w.y + height_at(s, cx, cy));
        c.args = {house_arg(*sel.house), raw(fbits(static_cast<float>(w.x))), raw(fbits(y))};
        out.push_back(std::move(c));
        *what = "rally point";
        return out;
    }
    const RightClickTarget t = right_click_target(s, g, p, pos, sx, sy);
    if (!t.on_map) return out;
    w = t.w, cx = t.cx, cy = t.cy;
    const std::optional<sim::HouseRef>& house = t.house;
    const std::optional<sim::UnitRef>& target = t.target;
    const std::uint16_t obj = t.obj;
    if (!house && !target && obj > 0x25e3 && obj <= 11000) {  // kind 1 of a special object (bytes 0x14, 0x15)
        *what = "map objects are not in the new engine yet";
        return out;
    }
    Command top;
    top.kind = Command::Kind::UiOrder;
    top.player = p;
    top.args = {raw(p), raw(static_cast<std::uint32_t>(w.x)), raw(static_cast<std::uint32_t>(w.y))};
    top.cell_x = cx;
    top.cell_y = cy;
    top.units = sel.units;
    out.push_back(top);
    // The children tell the sim the click kind (as the recorder writes them; the sim makes its own).
    auto child = [&](Command::Kind k, const sim::UnitRef& u, std::vector<OrderArg> args) {
        Command c;
        c.kind = k;
        c.player = p;
        c.args = std::move(args);
        c.units = {u};
        out.push_back(std::move(c));
    };
    if (house && house->player == p) {  // click kind 1
        for (const sim::UnitRef& u : sel.units) {
            child(Command::Kind::EnterHouse, u, {unit_arg(u), unit_arg(u), raw(house->slot), raw(0)});
        }
        *what = "to own " + house_name(s.players[p].houses[house->slot].type);
        return out;
    }
    if (house && sim::IsEnemy(s, p, house->player)) {  // click kind 2
        for (const sim::UnitRef& u : sel.units) {
            child(Command::Kind::AttackHouse, u, {unit_arg(u), unit_arg(u), house_arg(*house), raw(0), raw(0)});
        }
        *what = "attack " + house_name(s.players[house->player].houses[house->slot].type);
        return out;
    }
    if (target) {  // click kind 4
        for (const sim::UnitRef& u : sel.units)
            child(Command::Kind::Attack, u, {unit_arg(u), unit_arg(*target), raw(0)});
        *what = "attack " + unit_name(sim::Unit_Find(s, *target)->type);
        return out;
    }
    if (!(obj >= 0x2580 && obj <= 0x25e3)) {
        if (const auto tr = transport_at(s, g, pos, sx, sy, p)) {
            for (const sim::UnitRef& u : sel.units)
                child(Command::Kind::Board, u, {unit_arg(u), unit_arg(*tr), raw(1)});
            *what = "board " + unit_name(sim::Unit_Find(s, *tr)->type);
            return out;
        }
    }
    if (obj >= 0x2580 && obj <= 0x25e3) {
        const float fx = static_cast<float>(w.x), fy = static_cast<float>(w.y + height_at(s, cx, cy));
        for (const sim::UnitRef& u : sel.units) {
            child(Command::Kind::OrderXy, u, {unit_arg(u), raw(fbits(fx)), raw(fbits(fy)), raw(obj)});
        }
        *what = obj >= 0x25de ? "to the field" : "to the object";
        return out;
    }

    const float fx = static_cast<float>(w.x), fy = static_cast<float>(w.y + height_at(s, cx, cy));
    for (const sim::UnitRef& u : sel.units) {
        child(Command::Kind::OrderMove, u, {unit_arg(u), raw(fbits(fx)), raw(fbits(fy)), raw(1)});
    }
    *what = "move";
    if (t.hidden_enemy) {



        Command may;
        may.kind = Command::Kind::MayFire;
        may.player = p;
        may.args = {raw(p)};
        for (const sim::UnitRef& r : sel.units) {
            const sim::Unit* su = sim::Unit_Find(s, r);
            if (su != nullptr && sim::Unit_Type(s, p, *su).move_class != 6) may.units.push_back(r);
        }
        if (!may.units.empty()) {
            out.push_back(std::move(may));
            *what = "move, ready to fight";
        }
    }
    return out;
}

std::vector<Command> stop_order(const Selection& sel) {
    if (sel.units.empty()) return {};
    Command c;
    c.kind = Command::Kind::Stop;
    c.player = sel.player;
    c.args = {raw(sel.player)};
    c.units = sel.units;
    return {c};
}

std::vector<Command> delete_key(const Selection& sel) {
    if (sel.units.empty() && sel.flagged.empty()) return {};
    Command c;
    c.kind = Command::Kind::Act_ea260;
    c.player = sel.player;
    c.args = {raw(sel.player)};
    c.units = sel.units;
    c.houses = sel.flagged;  // the houses it damages, as the recorder (version 3) lists them
    return {c};
}

std::vector<Command> build_order(const sim::GameState& s, const Selection& sel, std::int32_t wx, std::int32_t wy,
                                 std::uint16_t type) {
    std::int32_t cx = 0, cy = 0;
    if (sel.player >= s.players.size() || !pick_cell(s, wx, wy, &cx, &cy)) return {};
    const sim::Cell w = sim::CellToWorldPoint(s.map, sim::Cell{cx, cy});
    Command c;
    c.kind = Command::Kind::AddHouse;
    c.player = sel.player;
    c.args = {raw(sel.player), raw(fbits(static_cast<float>(w.x))), raw(fbits(static_cast<float>(w.y))), raw(type)};
    c.units = sel.units;
    return {c};
}

std::optional<Command> select_houses(const sim::GameState& s, const Selection& sel) {
    if (sel.player >= s.players.size()) return std::nullopt;
    const std::vector<sim::House>& hs = s.players[sel.player].houses;
    bool same = true;
    for (std::size_t k = 0; k < hs.size() && same; ++k) {
        const bool want =
            std::any_of(sel.flagged.begin(), sel.flagged.end(), [&](const sim::HouseRef& r) { return r.slot == k; });
        same = (hs[k].selected != 0) == want;
    }
    if (same) return std::nullopt;
    Command c;
    c.kind = Command::Kind::SelectHouses;
    c.player = sel.player;
    c.args = {raw(sel.player)};
    c.houses = sel.flagged;
    return c;
}

std::optional<Command> select_summary(const sim::GameState& s, const Selection& sel) {
    if (sel.player >= s.players.size()) return std::nullopt;
    bool blocks = false;  // only a .mis mission's blocks read it (conditions 13 and 14)
    for (const sim::Player& pl : s.players) blocks = blocks || !pl.blocks.empty();
    if (!blocks) return std::nullopt;
    std::uint32_t type = 0xffff, num = 0xffff;
    if (!sel.units.empty()) {
        if (const sim::Unit* u = sim::Unit_Find(s, sel.units.front())) type = u->type;
    }
    if (!sel.flagged.empty() && sel.flagged.front().slot < s.players[sel.player].houses.size()) {
        num = s.players[sel.player].houses[sel.flagged.front().slot].num;
    }
    if ((type == 0xffff || type == s.sel_8ab774) && (num == 0xffff || num == s.sel_8ab770)) return std::nullopt;
    Command c;
    c.kind = Command::Kind::SelectSummary;
    c.player = sel.player;
    c.args = {raw(sel.player), raw(type), raw(num)};
    return c;
}

void select_unit(Selection& sel, const sim::UnitRef& u) {
    if (std::none_of(sel.units.begin(), sel.units.end(),
                     [&](const sim::UnitRef& r) { return r.id == u.id && r.serial == u.serial; })) {
        sel.units.insert(sel.units.begin(), u);
    }
}

void select_units(Selection& sel, std::span<const sim::UnitRef> list) {
    auto key = [](const sim::UnitRef& r) { return std::uint64_t{r.id} << 32 | r.serial; };
    std::unordered_set<std::uint64_t> in;
    in.reserve(sel.units.size() + list.size());
    for (const sim::UnitRef& r : sel.units) in.insert(key(r));
    std::vector<sim::UnitRef> out;
    for (const sim::UnitRef& r : list) {
        if (in.insert(key(r)).second) out.push_back(r);
    }
    if (out.empty()) return;
    std::reverse(out.begin(), out.end());
    out.insert(out.end(), sel.units.begin(), sel.units.end());
    sel.units = std::move(out);
}

bool toggle_unit(Selection& sel, const sim::UnitRef& u) {
    const auto it = std::find_if(sel.units.begin(), sel.units.end(),
                                 [&](const sim::UnitRef& r) { return r.id == u.id && r.serial == u.serial; });
    if (it == sel.units.end()) {
        sel.units.insert(sel.units.begin(), u);
        return true;
    }
    sel.units.erase(it);
    return false;
}

void clear_selection(Selection& sel) {
    sel.units.clear();
    sel.flagged.clear();
    sel.house.reset();
    ++sel.ring_resets;
}

std::vector<Command> train_order(const sim::GameState& s, const Selection& sel, std::uint16_t unit_type) {
    if (!sel.house || sel.house->player != sel.player || sel.player >= s.players.size()) return {};
    Command c;
    c.kind = Command::Kind::Act_b9730;
    c.player = sel.player;
    c.args = {raw(sel.player), raw(sel.house->slot), raw(unit_type), raw(1)};
    return {c};
}

std::vector<Command> take_tick_orders(const sim::GameState& s, std::deque<std::vector<Command>>& queue) {
    std::vector<Command> out;
    while (!queue.empty()) {
        std::vector<Command>& order = queue.front();
        if (order.empty()) {
            queue.pop_front();
            continue;
        }
        const bool ui = order[0].kind == Command::Kind::UiOrder;
        if (ui && !out.empty()) break;  // its number would not be the sim's next one
        const std::uint32_t top = ui ? s.order_seq + 1 : 0;
        for (std::size_t i = 0; i < order.size(); ++i) {
            Command c = order[i];
            c.tick = s.tick + 1;
            c.oseq = i == 0 ? top : top + static_cast<std::uint32_t>(i);
            // The May fire order that follows a move into fog is a button order of its own (right_click), not a child.
            c.parent = i == 0 || c.kind == Command::Kind::MayFire ? 0 : top;
            out.push_back(std::move(c));
        }
        queue.pop_front();
    }
    return out;
}

bool house_point(const sim::GameState& s, ViewPos pos, std::uint8_t player, std::uint16_t slot, int* sx, int* sy) {
    if (player >= s.players.size() || slot >= s.players[player].houses.size()) return false;
    std::int32_t hx = 0, hy = 0;
    if (!house_cell(s, player, slot, &hx, &hy)) return false;
    const sim::Cell wp = sim::CellToWorldPoint(s.map, sim::Cell{hx, hy});
    std::int32_t px = 0, py = 0;
    if (!pick_cell(s, wp.x + 48, wp.y, &px, &py) || px != hx || py != hy) return false;
    *sx = wp.x + 48 - view_x(pos), *sy = wp.y - view_y(pos);
    return *sx >= 0 && *sy >= 0 && *sx < 1024 && *sy < 768;
}

std::string unit_name(int type) {
    if (type >= 0 && type < static_cast<int>(std::size(kUnitNames))) return kUnitNames[type];
    return "unit type " + std::to_string(type);
}

std::string house_name(int type) {
    if (type >= 0 && type < static_cast<int>(std::size(kHouseNames))) return kHouseNames[type];
    return "house type " + std::to_string(type);
}

std::string work_name(int work) {
    switch (work) {
        case 0xff: return "idle";
        case 1: return "going to work";
        case 2: return "entering a house";
        case 3: return "in a house";
        case 7: return "attacking";
        case 9: return "leaving a house";
        case 10: return "farming";
        case 11: return "carrying the harvest";
        case 0x14: return "carried";
        default: return "work " + std::to_string(work);
    }
}

std::vector<std::uint16_t> buildable_houses(const sim::GameState& s, std::uint8_t player) {
    std::vector<std::uint16_t> out;
    if (player >= s.players.size()) return out;
    const auto& types = s.house_types[s.players[player].nation % 6];
    for (std::uint16_t t = 0; t < 25; ++t) {
        if (types[t].present) out.push_back(t);
    }
    return out;
}

bool can_place(const sim::GameState& s, std::uint8_t player, std::int32_t cx, std::int32_t cy, std::uint16_t type) {
    return sim::House_CanPlace(s, player, cx, cy, type);
}

std::vector<std::array<std::int32_t, 2>> site_cells(const sim::GameState& s, std::uint8_t player, std::int32_t cx,
                                                    std::int32_t cy, std::uint16_t type) {
    std::vector<std::array<std::int32_t, 2>> out;
    if (player >= s.players.size()) return out;
    const sim::HouseFootprint fp = s.house_footprints[s.players[player].nation % 6][type % 25];
    sim::Cell row = sim::Cell_Step(sim::Cell{cx, cy}, 6);  // CanPlaceHouse's first box, without its border
    row = sim::Cell_Step(sim::Cell_Step(row, 4), 1);
    for (int r = 0; r < fp.b; ++r) {
        sim::Cell c = row;
        for (int k = 0; k < fp.a; ++k) {
            out.push_back({c.x, c.y});
            c = sim::Cell_Step(c, 1);
        }
        row = sim::Cell_Step(row, 4);
    }
    return out;
}

}  // namespace antanta::ui
