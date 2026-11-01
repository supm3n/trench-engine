// Map cells: world to cell, direction tables, the per-cell unit lists and ground height (docs/movement.md 4.1, 5.8).
#include <cassert>
#include <cmath>

#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

const std::int32_t kDirCellEven[9][2] = {{0, 0}, {0, -1}, {0, 1}, {-1, 1}, {-1, -1}, {0, -2}, {0, 2}, {1, 0}, {-1, 0}};
const std::int32_t kDirCellOdd[9][2] = {{0, 0}, {1, -1}, {1, 1}, {0, 1}, {0, -1}, {0, -2}, {0, 2}, {1, 0}, {-1, 0}};
const std::int32_t kDirWorld[9][2] = {{0, 0},   {48, -24}, {48, 24}, {-48, 24}, {-48, -24},
                                      {0, -48}, {0, 48},   {96, 0},  {-96, 0}};
const std::uint8_t kOpposite[9] = {0, 3, 4, 1, 2, 6, 5, 8, 7};
const std::uint16_t kStepCost[9] = {0, 9, 9, 9, 9, 9, 9, 16, 16};


std::int32_t Ftol(double v) {
    if (!(v > -2147483649.0 && v < 2147483648.0)) return INT32_MIN;
    return static_cast<std::int32_t>(v);
}

// RngTable[RngIndex++] (the inlined read used everywhere, docs/movement.md 6).
std::uint16_t Rng_Next(GameState& s) { return s.rng_table[s.rng_index++]; }


Cell WorldToCell(float x, float y) {
    const auto ix = static_cast<std::int16_t>(Ftol(static_cast<double>(x) * static_cast<double>(0.010416667f)));
    const auto iy = static_cast<std::int16_t>(Ftol(static_cast<double>(y) * static_cast<double>(0.020833334f)));
    const double fx = static_cast<double>(x) - static_cast<double>(ix * 96);
    const double fy = static_cast<double>(y) - static_cast<double>(iy * 48);
    const double f2 = fy + fy;
    if (fx >= 48.0) {
        if (fy >= 24.0) return f2 + fx - 96.0 >= 48.0 ? Cell{ix + 1, 2 * iy + 2} : Cell{ix, 2 * iy + 1};
        return f2 - fx + 96.0 < 48.0 ? Cell{ix + 1, 2 * iy} : Cell{ix, 2 * iy + 1};
    }
    if (fy >= 24.0) return f2 - fx >= 48.0 ? Cell{ix, 2 * iy + 2} : Cell{ix, 2 * iy + 1};
    return f2 + fx < 48.0 ? Cell{ix, 2 * iy} : Cell{ix, 2 * iy + 1};
}

namespace {





bool Cell_HitTest(const Map& m, std::int16_t cx, std::int16_t cy, float px, float py) {
    const std::int32_t W = m.width, H = m.height;
    if (cx >= W - 1 || cx <= 1 || cy <= 1 || cy >= H - 1) return false;
    const std::int32_t odd = cy & 1;
    const std::size_t i = static_cast<std::size_t>(cy) * static_cast<std::size_t>(W) + static_cast<std::size_t>(cx);
    const double lx = static_cast<double>(px) - static_cast<double>((cx * 2 - (odd == 0 ? 1 : 0)) * 48);
    const double ly = static_cast<double>(py) - static_cast<double>(cy * 24 - 24);
    const std::int32_t L = 24 - m.heights[i];
    const std::int32_t T = -static_cast<std::int32_t>(m.heights[i - static_cast<std::size_t>(W) + odd]);
    const std::int32_t B = 48 - m.heights[i + static_cast<std::size_t>(W) + odd];
    const std::int32_t R = 24 - m.heights[i + 1];
    bool in = false;
    if ((ly <= L) != (ly <= T) && lx < (ly - L) * 48.0 / (T - L)) in = true;
    if ((ly <= T) != (ly <= R) && (ly - T) * 48.0 / (R - T) > lx - 48.0) in = !in;
    if ((ly <= R) != (ly <= B) && (ly - R) * -48.0 / (B - R) > lx - 96.0) in = !in;
    if ((ly <= B) != (ly <= L) && (ly - B) * -48.0 / (L - B) > lx - 48.0) in = !in;
    return in;
}

}  // namespace




Cell PickCell(const Map& m, float x, float y) {
    const auto ix = static_cast<std::uint16_t>(Ftol(static_cast<double>(x) * static_cast<double>(0.010416667f)));
    const auto iy = static_cast<std::uint16_t>(Ftol(static_cast<double>(y) * static_cast<double>(0.020833334f)));
    const double fx = static_cast<double>(x) - static_cast<double>(ix * 96);
    const double fy = static_cast<double>(y) - static_cast<double>(iy * 48);
    const double f2 = fy + fy;
    Cell c{ix, 2 * iy + 1};
    if (fx >= 48.0) {
        if (fy >= 24.0) {
            if (f2 + fx - 96.0 >= 48.0) c = Cell{ix + 1, 2 * iy + 2};
        } else if (f2 - fx + 96.0 < 48.0) {
            c = Cell{ix + 1, 2 * iy};
        }
    } else if (fy >= 24.0) {
        if (f2 - fx >= 48.0) c = Cell{ix, 2 * iy + 2};
    } else if (f2 + fx < 48.0) {
        c = Cell{ix, 2 * iy};
    }
    const auto hit = [&](std::uint32_t cx, std::uint32_t cy) {
        return Cell_HitTest(m, static_cast<std::int16_t>(cx & 0xffff), static_cast<std::int16_t>(cy & 0xffff), x, y);
    };
    std::int32_t r = static_cast<std::uint16_t>(c.y) + 14;
    std::int32_t r1 = static_cast<std::uint16_t>(c.y) + 15;
    const auto x16 = static_cast<std::uint32_t>(static_cast<std::uint16_t>(c.x));
    for (int k = 7; k >= 0; --k, r -= 2, r1 -= 2) {
        const std::int32_t rs = static_cast<std::int16_t>(r & 0xffff);
        if (hit(x16, static_cast<std::uint32_t>(r))) return Cell{c.x, rs};
        if ((r & 1) != 0) {
            if (hit(x16, static_cast<std::uint32_t>(r1))) return Cell{c.x, rs + 1};
            if (hit((x16 + 1) & 0xffff, static_cast<std::uint32_t>(r1))) return Cell{c.x + 1, rs + 1};
        } else {
            if (hit(x16 - 1, static_cast<std::uint32_t>(r1))) return Cell{c.x - 1, rs + 1};
            if (hit(x16, static_cast<std::uint32_t>(r1))) return Cell{c.x, rs + 1};
        }
    }
    return c;
}



Cell CellToWorldPoint(const Map& m, Cell c) {
    if (c.x < 0 || c.y < 0 || c.x >= m.width || c.y >= m.height) return c;
    const std::uint8_t h = m.heights[static_cast<std::size_t>(c.y) * m.width + static_cast<std::size_t>(c.x)];
    return Cell{(c.x * 2 - (~c.y & 1)) * 48, c.y * 24 - h};
}


Cell Cell_Step(Cell c, std::uint8_t dir) {
    const std::int32_t (*t)[2] = (c.y & 1) ? kDirCellOdd : kDirCellEven;
    return Cell{c.x + t[dir][0], c.y + t[dir][1]};
}



Unit* Unit_FindById(GameState& s, std::uint8_t player, std::uint16_t id) {
    if (player >= s.players.size()) return nullptr;
    Player& p = s.players[player];
    if (id >= p.id_slots.size() || p.id_slots[id] == kNoSlot) return nullptr;
    return &p.unit_slots[p.id_slots[id]];
}

std::uint32_t Unit_Add(GameState& s, std::uint8_t player, const Unit& u, bool at_head) {
    Player& p = s.players[player];
    std::uint32_t slot = 0;
    if (!p.free_slots.empty()) {
        slot = p.free_slots.back();
        p.free_slots.pop_back();
        p.unit_slots[slot] = u;
    } else {
        slot = static_cast<std::uint32_t>(p.unit_slots.size());
        p.unit_slots.push_back(u);
    }
    Unit& v = p.unit_slots[slot];
    v.next = kNoSlot, v.prev = kNoSlot;
    if (at_head || p.first_unit == kNoSlot) {
        v.next = p.first_unit;
        if (p.first_unit != kNoSlot) p.unit_slots[p.first_unit].prev = slot;
        p.first_unit = slot;
    } else {
        std::uint32_t last = p.first_unit;
        while (p.unit_slots[last].next != kNoSlot) last = p.unit_slots[last].next;
        p.unit_slots[last].next = slot;
        v.prev = last;
    }
    if (p.id_slots.size() <= u.id) p.id_slots.resize(static_cast<std::size_t>(u.id) + 1, kNoSlot);
    assert(p.id_slots[u.id] == kNoSlot);
    p.id_slots[u.id] = slot;
    ++p.num_units;
    return slot;
}

void Unit_Delete(GameState& s, std::uint8_t player, std::uint32_t slot) {
    Player& p = s.players[player];
    const Unit& u = p.unit_slots[slot];
    if (u.prev != kNoSlot) {
        p.unit_slots[u.prev].next = u.next;
    } else {
        p.first_unit = u.next;
    }
    if (u.next != kNoSlot) p.unit_slots[u.next].prev = u.prev;
    if (u.id < p.id_slots.size() && p.id_slots[u.id] == slot) p.id_slots[u.id] = kNoSlot;
    if (u.id < p.next_id) p.free_ids.push_back(u.id);



    for (auto& [name, g] : s.script_groups) {
        const auto it = g.members.find(u.id);
        if (it != g.members.end() && (s.original_rules || it->second.player == player)) g.members.erase(it);
    }
    --p.num_units;
    p.freed_slots.push_back(slot);
    if (p.first_unit == kNoSlot) p.is_lost = 1;
}

std::vector<std::uint32_t> Player_UnitSlots(const Player& p) {
    std::vector<std::uint32_t> out;
    out.reserve(p.num_units);
    for (std::uint32_t k = p.first_unit; k != kNoSlot; k = p.unit_slots[k].next) out.push_back(k);
    return out;
}

House* House_Find(GameState& s, const HouseRef& r) {
    if (r.player >= s.players.size() || r.slot >= s.players[r.player].houses.size()) return nullptr;
    House& h = s.players[r.player].houses[r.slot];
    return r.generation == 0 || h.generation == r.generation ? &h : nullptr;
}

UnitRef Unit_Ref(std::uint8_t player, const Unit& u) { return UnitRef{player, u.id, u.serial}; }

namespace {


void Cell_RemoveFromList(GameState& s, std::uint8_t player, Unit& u) {
    const std::int64_t i = static_cast<std::int64_t>(u.map_y) * s.map.width + u.map_x;
    if (i < 0 || i >= static_cast<std::int64_t>(s.map.cell_units.size())) return;
    std::vector<UnitRef>& list = s.map.cell_units[static_cast<std::size_t>(i)];
    const UnitRef r = Unit_Ref(player, u);
    for (std::size_t k = 0; k < list.size(); ++k) {
        if (list[k].player == r.player && list[k].id == r.id && list[k].serial == r.serial) {
            list.erase(list.begin() + static_cast<std::ptrdiff_t>(k));
            break;
        }
    }
    u.in_cell_list = false;
}

}  // namespace

void Cell_ListRemove(GameState& s, std::uint8_t player, Unit& u) { Cell_RemoveFromList(s, player, u); }


void Cell_RemoveUnit(GameState& s, std::uint8_t player, Unit& u) {
    if (Unit_Type(s, player, u).field_468 != 0) LargeCells_Leave(s, u);
    Cell_RemoveFromList(s, player, u);
}


// unit whose old cell is outside the playable area (x < 3, y < 8, x > W - 3, y > H - 8) leaves its cell list and is
// put on the cell clamped from (x, y) by its old cell (x <= 3: 4, y <= 9: 10, x >= W - 3: W - 4, y >= H - 9:
// H - 10): position, Dest and Final at the cell's point (x + 48.0f), targets cleared, SetCell there, OrderEnd(0).
// Then, if that cell (or (x, y)) is outside the area: the unit leaves its list again; an aircraft (B+5) just stays
// out of the lists (true); any other unit gets the same teleport to (x, y) clamped by itself (true). false: SetCell
// goes on with (x, y). No RNG.
bool ClampToMapBorder(GameState& s, std::uint8_t player, Unit& u, std::int32_t x, std::int32_t y) {
    const std::int32_t w = s.map.width, h = s.map.height;
    auto teleport = [&](std::int32_t cx, std::int32_t cy) {
        const std::int32_t px = (static_cast<std::int32_t>(static_cast<std::uint16_t>(cx)) * 2 -
                                 (~static_cast<std::int32_t>(static_cast<std::uint16_t>(cy)) & 1)) *
                                48;
        const double fx = static_cast<double>(px) + static_cast<double>(48.0f);
        const double fy = static_cast<double>(static_cast<std::uint16_t>(cy) * 24);
        u.map_x = static_cast<std::uint16_t>(cx), u.map_y = static_cast<std::uint16_t>(cy);
        u.x = static_cast<float>(fx), u.y = static_cast<float>(fy);
        u.player_unit_attack = 0xff, u.has_attack_unit = false;
        u.player_house_attack = 0xff, u.has_house_attack = false;
        u.final_x = static_cast<float>(fx), u.final_y = static_cast<float>(fy);
        u.dest_x = static_cast<float>(fx), u.dest_y = static_cast<float>(fy);
        TUnit_SetCell(s, player, u, Cell{cx, cy});
        TUnit_OrderEnd(s, player, u, 0);
    };
    const bool old_out = u.map_x < 3 || u.map_y < 8 || u.map_x > w - 3 || u.map_y > h - 8;
    if ((old_out || x < 3 || y < 8 || x > w - 3 || y > h - 8) && (w < 7 || h < 18)) {
        // A map too small for the clamped cell to be inside (only synthetic test maps): the original would recurse
        // without end; counted and the unit left where it is.
        Unported(s, orig::kClampToMapBorder);
        return true;
    }
    if (old_out) {
        Cell_RemoveUnit(s, player, u);
        const std::int32_t ox = u.map_x, oy = u.map_y;
        x = ox <= 3 ? 4 : x;
        y = oy <= 9 ? 10 : y;
        if (ox >= w - 3) x = w - 4;
        if (oy >= h - 9) y = h - 10;
        teleport(x, y);
    }
    if (x >= 3 && y >= 8 && x <= w - 3 && y <= h - 8) return false;
    Cell_RemoveUnit(s, player, u);
    if (Unit_Type(s, player, u).is_air != 0) return true;
    if (x <= 3) x = 4;
    if (y <= 9) y = 10;
    if (x >= w - 3) x = w - 4;
    if (y >= h - 9) y = h - 10;
    teleport(x, y);
    return true;
}



void TUnit_SetCell(GameState& s, std::uint8_t player, Unit& u, Cell c) {
    const std::int32_t w = s.map.width;
    if (ClampToMapBorder(s, player, u, c.x, c.y)) return;
    if (Unit_Type(s, player, u).field_468 != 0) LargeCells_Move(s, u, c);
    if (u.in_cell_list) Cell_RemoveFromList(s, player, u);
    u.map_x = static_cast<std::uint16_t>(c.x), u.map_y = static_cast<std::uint16_t>(c.y);
    std::vector<UnitRef>& list = s.map.cell_units[static_cast<std::size_t>(c.y * w + c.x)];
    if (list.size() < 40) {
        list.push_back(Unit_Ref(player, u));
        u.in_cell_list = true;
    }
}



bool IsLowBridgeCell(const GameState& s, Cell c) {
    if (c.x < 0 || c.y < 0 || c.x >= s.map.width || c.y >= s.map.height) return false;  // the original logs an error
    const std::uint16_t v = s.map.objects[static_cast<std::size_t>(c.y * s.map.width + c.x)];
    if (v < 0x65 || v >= 0x3e8) return false;
    const std::size_t k = v - 0x65u;
    return k < s.map.neutral_houses.size() && s.map.neutral_houses[k].type <= 1;
}

namespace {


int Cell_Height(const GameState& s, std::int64_t idx) {
    if (idx < 0 || idx >= static_cast<std::int64_t>(s.map.heights.size())) return 0;
    return s.map.heights[static_cast<std::size_t>(idx)];
}

int CellToWorldX(Cell c) { return (c.x * 2 - (~c.y & 1)) * 48; }


bool IsSpecialObj20or21(const GameState& s, Cell c) {
    if (c.x < 0 || c.y < 0 || c.x >= s.map.width || c.y >= s.map.height) return false;
    return Special_IsPontoon(s, s.map.objects[static_cast<std::size_t>(c.y * s.map.width + c.x)]);
}


double Terrain_PlaneFrom3Cells(const GameState& s, float out[3], Cell b, Cell c1, Cell c2) {
    const std::int32_t w = s.map.width;
    const double wbx = CellToWorldX(b);
    const double hb = Cell_Height(s, static_cast<std::int64_t>(b.y) * w + b.x);
    const double h1 = Cell_Height(s, static_cast<std::int64_t>(c1.y) * w + c1.x);
    const double d1 = CellToWorldX(c1) - wbx;
    const double h2 = Cell_Height(s, static_cast<std::int64_t>(c2.y) * w + c2.x);
    const double dx2 = CellToWorldX(c2) - wbx;
    const float nx = static_cast<float>(((h2 + hb) + (h1 + hb)) * 24.0 - (h2 + h1) * 48.0);
    const float ny = static_cast<float>(((h1 - h2) * (dx2 + d1) + (h2 - hb) * dx2) + (hb - h1) * d1);
    const double nz = ((d1 - dx2) * 0.0 - (-d1) * 24.0) + dx2 * 24.0;
    const float nzf = static_cast<float>(nz);
    const double d = -(((h2 * nz) + static_cast<double>(nx) * dx2) + static_cast<double>(ny) * 24.0);
    if (d == 0.0) {
        out[0] = nx, out[1] = ny, out[2] = nzf;
        return d;
    }
    out[0] = static_cast<float>(static_cast<double>(nx) / d);
    out[1] = static_cast<float>(static_cast<double>(ny) / d);
    out[2] = static_cast<float>(static_cast<double>(nzf) / d);
    return 1.0;
}


std::int32_t Terrain_PlaneHeightAt(float rx, float ry, double pd, const float n[3]) {
    const double den = (2000.0 * n[2] + 0.0 * static_cast<double>(n[0])) + 0.0 * static_cast<double>(n[1]);
    const double inv = -1.0 / den;
    const double num = ((static_cast<double>(rx) * n[0] + static_cast<double>(ry) * n[1]) + n[2] * -1000.0) + pd;
    const double t = inv * num;
    const float z1 = static_cast<float>(t * 2000.0);
    const float z2 = static_cast<float>(static_cast<double>(z1) - 1000.0);
    return Ftol(z2);
}

}  // namespace



void TUnit_UpdateGroundHeight(GameState& s, std::uint8_t player, Unit& u) {
    if (Unit_Type(s, player, u).is_ship != 0) return;
    const Cell c{u.map_x, u.map_y};
    if (u.field_024 < 1.0f) {
        if (IsSpecialObj20or21(s, c)) {
            u.z = 65.0f;
            return;
        }
        if (IsLowBridgeCell(s, c)) {
            u.z = 114.0f;
            return;
        }
    }
    const std::int32_t w = s.map.width, h = s.map.height;
    if (!(c.x > 2 && c.x < w - 2 && c.y > 3 && c.y < h - 3)) return;
    int wx = CellToWorldX(c), wy = c.y * 24;
    const Cell c1 = Cell_Step(c, 1), c2 = Cell_Step(c, 2);
    Cell base = c;
    if (static_cast<double>(u.x) - wx >= 48.0) {
        base = Cell_Step(c, 7);
        wx = CellToWorldX(base), wy = base.y * 24;
    }
    float plane[3] = {0, 0, 0};
    const double pd = Terrain_PlaneFrom3Cells(s, plane, base, c1, c2);
    const float rx = static_cast<float>(static_cast<double>(u.x) - wx);
    const float ry = static_cast<float>(static_cast<double>(u.y) - wy);
    u.z = static_cast<float>(Terrain_PlaneHeightAt(rx, ry, pd, plane));
}



std::int32_t GroundHeightAt(const GameState& s, float x, float y) {
    const std::int32_t w = s.map.width, h = s.map.height;
    auto on_map = [w, h](Cell c) { return c.x >= 0 && c.y >= 0 && c.x < w && c.y < h; };
    const Cell c = WorldToCell(x, y);
    if (!on_map(c)) return 50;
    const Cell c1 = Cell_Step(c, 1), c2 = Cell_Step(c, 2);
    if (!on_map(c1) || !on_map(c2)) return 50;
    Cell base = c;
    if (static_cast<double>(x) - CellToWorldX(c) >= 48.0) base = Cell_Step(c, 7);
    if (!on_map(base)) return 50;
    float plane[3] = {0, 0, 0};
    const double pd = Terrain_PlaneFrom3Cells(s, plane, base, c1, c2);
    const float rx = static_cast<float>(static_cast<double>(x) - CellToWorldX(base));
    const float ry = static_cast<float>(static_cast<double>(y) - base.y * 24);
    return Terrain_PlaneHeightAt(rx, ry, pd, plane);
}



void TUnit_SlopeSpeedScale(GameState& s, Unit& u) {
    const Cell c = WorldToCell(u.dest_x, u.dest_y);
    const std::int32_t w = s.map.width, h = s.map.height;
    if (c.x < 2 || c.y < 5 || c.x >= w - 2 || c.y >= h - 4) return;
    const std::int64_t idx = static_cast<std::int64_t>(w) * c.y + c.x;
    const int par = c.y & 1;
    const int h0 = Cell_Height(s, idx), hn = Cell_Height(s, idx - w + par), he = Cell_Height(s, idx + 1),
              hs = Cell_Height(s, idx + w + par);
    const float cyw = static_cast<float>(c.y * 24);
    const float cxw = static_cast<float>((2 * c.x - (par == 0 ? 1 : 0)) * 48);
    double zd = 0;
    if (IsLowBridgeCell(s, c)) {
        zd = 114.0;
    } else if (IsSpecialObj20or21(s, c)) {
        zd = 65.0;
    } else {
        // The ray is vertical: its x/y direction is f32(Dest - Dest) = 0.
        const float dx = static_cast<float>(static_cast<double>(u.dest_x) - u.dest_x);
        const float dy = static_cast<float>(static_cast<double>(u.dest_y) - u.dest_y);
        const bool upper = static_cast<double>(u.dest_y) - cyw < 0.0;
        const double de = he - h0;
        float a = 0;
        double b = 0, k = 0, den = 0;
        if (upper) {
            const double dn = hn - h0;
            a = static_cast<float>(-24.0 * de);
            b = dn * 96.0 - de * 48.0;
            k = (h0 * 2304.0 + b * cyw) + static_cast<double>(a) * cxw;
            den = (static_cast<double>(dy) * b + static_cast<double>(dx) * a) + 1000.0 * 2304.0;
        } else {
            const double ds = hs - h0;
            a = static_cast<float>(24.0 * de);
            b = ds * 96.0 - de * 48.0;
            k = (static_cast<double>(cyw) * b - h0 * 2304.0) + static_cast<double>(a) * cxw;
            den = (static_cast<double>(dy) * b + static_cast<double>(dx) * a) - 1000.0 * 2304.0;
        }
        zd = u.z;
        if (den <= -1e-05 || !(den < 1e-05)) {
            const double t = ((b * u.dest_y + static_cast<double>(u.dest_x) * a) + -k) * (-1.0 / den);
            if (!(t < 0.0) && !(t > 1.0)) {
                const float px = static_cast<float>(static_cast<double>(static_cast<float>(dx * t)) + u.dest_x);
                const float py = static_cast<float>(static_cast<double>(static_cast<float>(dy * t)) + u.dest_y);
                const float zhit = static_cast<float>(t * 1000.0);
                const float xs[4] = {cxw, cxw + 48.0f, cxw + 96.0f, cxw};
                const float ys[4] = {cyw, upper ? cyw - 24.0f : cyw + 24.0f, cyw, cyw};
                bool in = false;
                for (int i = 0; i < 3; ++i) {
                    const bool e0 = !(py > ys[i]);
                    const bool e1 = !(py > ys[i + 1]);
                    if (e0 != e1) {
                        const double q =
                            ((static_cast<double>(xs[i + 1]) - xs[i]) * (static_cast<double>(py) - ys[i])) /
                            (static_cast<double>(ys[i + 1]) - ys[i]);
                        const double lhs = static_cast<double>(px) - xs[i];
                        if (q > lhs) in = !in;
                    }
                }
                if (in) zd = zhit;
            }
        }
    }
    double dz = zd - u.z;
    dz = dz > 0.0 ? dz * 6.0 : dz * 3.0;
    const double ddy = static_cast<double>(u.y) - u.dest_y;
    const double ddx = static_cast<double>(u.x) - u.dest_x;
    const double d = std::sqrt(ddy * ddy + ddx * ddx);
    const float df = static_cast<float>(d);
    if (d < 3.0) return;
    double f = std::sqrt(static_cast<double>(df) * df + dz * dz) / df;
    if (f > 4.0) f = 4.0;
    const double inv = 1.0 / f;
    u.vel_x = static_cast<float>(inv * u.vel_x);
    u.vel_y = static_cast<float>(inv * u.vel_y);
}


void TUnit_UpdateCellIfMoved(GameState& s, std::uint8_t player, Unit& u) {
    const Cell c = WorldToCell(u.x, u.y);
    if (c.x != u.map_x || c.y != u.map_y) TUnit_SetCell(s, player, u, c);
    TUnit_UpdateGroundHeight(s, player, u);
}


void TUnit_ResyncCell(GameState& s, std::uint8_t player, Unit& u) {
    TUnit_SetCell(s, player, u, WorldToCell(u.x, u.y));
    TUnit_UpdateGroundHeight(s, player, u);
}

}  // namespace antanta::sim
