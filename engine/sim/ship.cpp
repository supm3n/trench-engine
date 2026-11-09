// Ships (MoveClass 13, 14, 15: transport 0x11, destroyer 0x12, cruiser 0x13, battleship 0x14; docs/movement.md
// "MoveClass 13, 14, 15: ships", docs/combat.md "Naval combat"): the port (its side on the water, its door, the ship
// built at the quay, the release and the repair), the ship list, ...
// x87 values that stay on the stack are double (the game runs at 53-bit precision); every float store is a float
// cast.
#include <cmath>

#include "dmath.hpp"
#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

namespace {

constexpr float kPiOverFour = 0.7853982f;

}  // namespace


bool TUnit_SetOrient(Unit& u, std::uint8_t o) {
    if (o >= 8) return false;
    u.orient = o;
    u.angle = static_cast<float>(static_cast<double>(static_cast<std::int8_t>(o)) * static_cast<double>(kPiOverFour) +
                                 static_cast<double>(0.0001f));
    return true;
}






std::uint32_t Port_ShipSide(const GameState& s, std::int32_t x, std::int32_t y, HouseFootprint fp) {
    const Map& m = s.map;
    const std::int32_t a = fp.a, b = fp.b;
    std::array<bool, 4> f{true, true, true, true};
    std::uint8_t k = 0;
    const std::uint32_t n = static_cast<std::uint32_t>(m.width) * m.height;
    Cell row{x, y};
    for (std::int32_t i = 0; i < a; ++i) {
        Cell c = row;
        for (std::int32_t j = 0; j < b; ++j) {
            const bool edge = i == 0 || i == a - 1 || j == 0 || j == b - 1;
            const std::uint32_t idx =
                static_cast<std::uint32_t>(m.width) * static_cast<std::uint32_t>(c.y) + static_cast<std::uint32_t>(c.x);
            if (edge && idx < n) {
                if (i == 0) {
                    k = 0;
                } else if (i == a - 1) {
                    k = 1;
                } else if (j == 0) {
                    k = 2;
                } else if (j == b - 1) {
                    k = 3;
                }
                if (!(Obstacle_UnderCell(m, c.x, c.y) && Obstacle_UnderCell(m, c.x + 1, c.y))) {
                    if (i == 0 || i == a - 1) {
                        f[i == 0 ? 0 : 1] = false;
                        if (j == 0) {
                            k = 2;
                        } else if (j == b - 1) {
                            k = 3;
                        }
                    }
                    f[k] = false;
                }
            }
            c = Cell_Step(c, 4);
        }
        row = Cell_Step(row, 1);
    }
    if (f[0]) return 1;
    if (f[1]) return 2;
    if (f[2]) return 4;
    return f[3] ? 3 : 0;
}



void Port_SetDoor(GameState& s, std::uint8_t nation, House& h) {
    if (h.field_0ac == 0) return;
    const HouseFootprint fp = s.house_footprints[nation % 6][h.type % 25];
    const std::int32_t a = fp.a, b = fp.b;
    const std::int32_t x0 = (2 * static_cast<std::uint16_t>(h.map_x) - (~static_cast<std::uint16_t>(h.map_y) & 1)) * 48;
    const std::int32_t y0 = static_cast<std::uint16_t>(h.map_y) * 24;
    std::int32_t sx = 0, sy = 0;
    float v0 = 0, v1 = 0;
    switch (h.field_0ac) {
        case 1:
            sx = (x0 - 48 * b - 150) + (x0 - 150), sy = (y0 + 48 - 24 * b + 75) + (y0 + 48 + 75);
            v0 = -5.0f, v1 = 10.0f;
            break;
        case 2:
            sx = (x0 + 48 * a + 96 - 48 * b + 150) + (x0 + 48 * a + 96 + 150);
            sy = (y0 - 24 * a - 24 * b - 75) + (y0 - 24 * a - 75);
            v0 = 5.0f, v1 = -10.0f;
            break;
        case 3:
            sx = (x0 - 48 * b + 48 * a - 150) + (x0 - 48 * b - 150);
            sy = (y0 - 24 * b - 24 * a - 75) + (y0 - 24 * b - 75);
            v0 = -5.0f, v1 = -10.0f;
            break;
        case 4:
            sx = (x0 + 96 + 48 * a + 150) + (x0 + 96 + 150), sy = (y0 + 48 - 24 * a + 75) + (y0 + 48 + 75);
            v0 = 5.0f, v1 = 10.0f;
            break;
        default:
            // a side above 4: the original reads its jump table's default (no shipped case)
            Unported(s, orig::kPortSetDoor);
            return;
    }
    const double dx = static_cast<double>(sx) * static_cast<double>(0.5f);
    const double dy = static_cast<double>(sy) * static_cast<double>(0.5f);
    h.exit_x = static_cast<float>(dx), h.exit_y = static_cast<float>(dy);
    h.rally_x = static_cast<float>(dx), h.rally_y = static_cast<float>(dy);

    const double len = std::sqrt(static_cast<double>(v0) * v0 + static_cast<double>(v1) * v1 + 0.0 * 0.0);
    const double inv = 1.0 / len;
    const auto r1 = static_cast<float>(inv * v1);
    const auto rx = static_cast<float>(static_cast<double>(v0) * inv * 1000.0);
    const auto ry = static_cast<float>(static_cast<double>(r1) * 1000.0);
    h.rally_x = static_cast<float>(static_cast<double>(rx) + h.rally_x);
    h.rally_y = static_cast<float>(static_cast<double>(ry) + h.rally_y);
}




void Port_RepairShips(GameState& s, std::uint8_t p, const House& h) {
    if (s.tick % 20 != 0) return;
    for (std::size_t i = 0; i < s.ships.size(); ++i) {
        const UnitRef r = s.ships[i];
        Unit* u = Unit_Find(s, r);
        if (u == nullptr) {
            Unported(s, orig::kPortRepairShips);  // a stale list entry: the original reads freed memory
            continue;
        }
        if (IsEnemy(s, p, r.player)) continue;
        const double dx = static_cast<double>(h.x) - u->x, dy = static_cast<double>(h.y) - u->y;
        if (std::sqrt(dy * dy + dx * dx) > static_cast<double>(h.field_080)) continue;
        const std::uint16_t mh = Unit_Type(s, r.player, *u).max_hp[r.player];
        if (!(static_cast<double>(mh) > static_cast<double>(u->hp))) continue;
        const double v = static_cast<double>(u->hp) + static_cast<double>(100.0f);
        u->hp = static_cast<float>(v);
        if (v > static_cast<double>(static_cast<float>(mh))) u->hp = static_cast<float>(mh);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The coarse water grid (docs/movement.md 15.1).

namespace {

constexpr float kF18 = 0.010416667f;
constexpr float kF1c = 0.020833334f;

std::int32_t CoarseI(float x) { return Ftol(static_cast<double>(x) * static_cast<double>(kF18)); }
std::int32_t CoarseJ(float y) { return Ftol(static_cast<double>(y) * static_cast<double>(kF1c)); }


std::uint16_t CellObject(const Map& m, std::int32_t x, std::int32_t y) {
    if (static_cast<std::uint32_t>(x) >= m.width || static_cast<std::uint32_t>(y) >= m.height) return 0;
    return m.objects[static_cast<std::size_t>(y) * m.width + static_cast<std::size_t>(x)];
}

// No object, trees, stones, miscs and everything from 0x2af8 on let ships pass; houses (0x65 .. 0x2327) and special
// objects (0x25e4 .. 0x2af7) do not.
bool ObjOk(std::uint16_t o) { return o < 0x65 || o >= 0x2af8 || (o >= 0x2328 && o < 0x25e4); }


bool CellNavigable(const Map& m, Cell c) { return Obstacle_UnderCell(m, c.x, c.y) && ObjOk(CellObject(m, c.x, c.y)); }

Cell CoarseCentreCell(std::int32_t i, std::int32_t j) {
    return WorldToCell(static_cast<float>(96 * i + 48), static_cast<float>(48 * j + 24));
}

std::uint8_t& G(CoarseWater& c, std::int32_t i, std::int32_t j) {
    return c.g[static_cast<std::size_t>(j) * static_cast<std::size_t>(c.w) + static_cast<std::size_t>(i)];
}


std::uint8_t Ship_OrientClass(const Unit& u) {
    const float a = u.angle;
    if (a < 0.7853982f) return 0;
    if (a < 2.3561945f) return 2;
    if (a < 3.926991f) return 4;
    if (a < 5.4977875f) return 6;
    return 0;
}




void SetBox(GameState& s, const ShipRec& r, std::uint8_t d, std::uint8_t newv, std::uint8_t oldv) {
    CoarseWater& c = s.map.water;
    const Unit* u = Unit_Find(s, r.unit);
    const std::uint16_t t = u != nullptr ? u->type : 0;
    // a record of a freed unit: the original reads freed memory
    if (u == nullptr) Unported(s, orig::kCoarseWaterSetBox);
    const std::int32_t a = t == 0x12 ? 2 : t == 0x14 ? 4 : 3;
    const std::int32_t b = t == 0x12 ? 3 : t == 0x14 ? 5 : 4;
    std::int32_t x0 = r.i - a, x1 = r.i + a, y0 = r.j - b, y1 = r.j + b;
    if (d == 0 || d == 4) {
        y0 -= 2, y1 += 2;
    } else if (d == 2 || d == 6) {
        x0 -= 1, x1 += 1;
    } else if (d == 0xff && oldv == 0xf) {
        y0 -= 2, y1 += 2, x0 -= 1, x1 += 1;
    }
    auto clamp = [](std::int32_t& v, std::int32_t n) {
        if (v < 0) v = 0;
        if (v >= n) v = n - 1;
    };
    clamp(x0, c.w), clamp(x1, c.w), clamp(y0, c.h), clamp(y1, c.h);
    for (std::int32_t x = x0; x <= x1; ++x) {
        for (std::int32_t y = y0; y <= y1; ++y) {
            std::uint8_t& v = G(c, x, y);
            if (v == oldv) v = newv;
        }
    }
}

ShipRec* FindRec(GameState& s, const Unit& u) {
    for (ShipRec& r : s.map.water.recs) {
        if (r.unit.serial == u.serial && Unit_Find(s, r.unit) == &u) return &r;
    }
    return nullptr;
}

}  // namespace





void CoarseWater_Rebuild(GameState& s, const std::int32_t* rect) {
    CoarseWater& c = s.map.water;
    if (c.g.empty()) return;  // "water not set yet"
    std::int32_t i0 = 0, j0 = 0, i1 = c.w - 1, j1 = c.h - 1;
    if (rect != nullptr) {
        auto ci = [](std::int32_t v) { return Ftol(static_cast<double>(v * 96) * static_cast<double>(kF18)); };
        auto cj = [](std::int32_t v) { return Ftol(static_cast<double>(v * 24) * static_cast<double>(kF1c)); };
        i0 = ci(rect[0]), j0 = cj(rect[1]), i1 = ci(rect[2]), j1 = cj(rect[3]);
        auto clamp = [](std::int32_t& v, std::int32_t n) {
            if (v < 0) v = 0;
            if (v >= n) v = n - 1;
        };
        clamp(i0, c.w), clamp(i1, c.w), clamp(j0, c.h), clamp(j1, c.h);
    }
    const Map& m = s.map;
    auto border = [&](std::int32_t i, std::int32_t j) { return j == 0 || i == 0 || i == c.w - 1 || j == c.h - 1; };
    for (std::int32_t j = j0; j <= j1; ++j) {
        for (std::int32_t i = i0; i <= i1; ++i) {
            std::uint8_t& g = G(c, i, j);
            if (border(i, j)) {
                g = 10;
                continue;
            }
            const Cell cc = CoarseCentreCell(i, j);
            if (!Obstacle_UnderCell(m, cc.x, cc.y) || !ObjOk(CellObject(m, cc.x, cc.y)) || g == 10) {
                g = 10;
                continue;
            }
            std::uint8_t v = 0;
            for (std::int32_t a = -2; a <= 2 && v == 0; ++a) {
                for (std::int32_t k = -2; k <= 3; ++k) {
                    const std::int32_t x = i + a, y = j + k;
                    if (x < 0 || x >= c.w || y < 0 || y >= c.h) continue;
                    if (!CellNavigable(m, CoarseCentreCell(x, y))) {
                        v = 10;
                        break;
                    }
                }
            }
            g = v;
        }
    }
    for (std::int32_t j = j0; j <= j1; ++j) {
        for (std::int32_t i = i0; i <= i1; ++i) {
            std::uint8_t& g = G(c, i, j);
            if (border(i, j)) {
                g = 10;
                continue;
            }
            if (g != 0) continue;
            std::uint8_t v = 0;
            for (std::int32_t di = -1; di <= 1 && v == 0; ++di) {
                for (std::int32_t dj = -1; dj <= 1; ++dj) {
                    const std::int32_t x = i + di, y = j + dj;
                    if (x >= 0 && x < c.w && y >= 0 && y < c.h && G(c, x, y) == 10) {
                        v = 11;
                        break;
                    }
                }
            }
            g = v;
        }
    }
}



void CoarseWater_Init(GameState& s) {
    CoarseWater& c = s.map.water;
    c.w = s.map.width, c.h = s.map.height / 2;
    c.g.assign(static_cast<std::size_t>(c.w) * static_cast<std::size_t>(c.h), 0);
    c.recs.clear();
    CoarseWater_Rebuild(s, nullptr);


    s.ships.clear();
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        for (const std::uint32_t k : Player_UnitSlots(s.players[p])) {
            Unit& u = s.players[p].unit_slots[k];
            if (Unit_Type(s, static_cast<std::uint8_t>(p), u).is_ship != 0) {
                Ship_Created(s, static_cast<std::uint8_t>(p), u);
            }
        }
    }
}


void CoarseWater_RebuildAroundHouse(GameState& s, std::int32_t x, std::int32_t y, HouseFootprint fp) {
    const std::int32_t r[4] = {x - fp.a - 4, y - fp.b - 12, x + fp.a + 4, y + fp.b + 12};
    CoarseWater_Rebuild(s, r);
}




bool CoarseWater_FindWaterNear(GameState& s, float& x, float& y) {
    CoarseWater& c = s.map.water;
    if (c.g.empty()) return false;
    std::int32_t i = static_cast<std::int16_t>(CoarseI(x)), j = static_cast<std::int16_t>(CoarseJ(y));
    auto clamp = [&] {
        if (i < 0) i = 0;
        if (i >= c.w) i = c.w - 1;
        if (j < 0) j = 0;
        if (j >= c.h) j = c.h - 1;
    };
    clamp();
    if (G(c, i, j) != 0) {
        for (std::int32_t n = 0;;) {
            i += (s.rng_table[s.rng_index] & 1) - 1;
            j += (s.rng_table[static_cast<std::uint16_t>(s.rng_index + 1)] & 1) - 1;
            s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
            i = static_cast<std::int16_t>(i), j = static_cast<std::int16_t>(j);
            clamp();
            if (++n >= 5000) return false;
            if (G(c, i, j) == 0) break;
        }
    }
    x = static_cast<float>(i * 96 + 48);
    y = static_cast<float>(j * 48 + 24);
    return true;
}


void CoarseWater_AddShip(GameState& s, std::uint8_t player, const Unit& u) {
    s.map.water.recs.push_back(ShipRec{Unit_Ref(player, u), static_cast<std::uint16_t>(CoarseI(u.x)),
                                       static_cast<std::uint16_t>(CoarseJ(u.y))});
}






void CoarseWater_RemoveShip(GameState& s, const UnitRef& r) {
    auto& v = s.map.water.recs;
    auto same = [&](const ShipRec& q) { return q.unit.player == r.player && q.unit.serial == r.serial; };
    std::size_t k = 0;
    while (k < v.size() && !same(v[k])) ++k;
    if (k == v.size()) return;
    if (!s.original_rules) {  // docs/bugs.md B43
        if (!s.map.water.g.empty()) SetBox(s, v[k], 0xff, 0, 15);
        std::erase_if(v, same);
        return;
    }
    std::size_t dst = k;
    for (std::size_t i = k + 1; i < v.size(); ++i) {
        if (!same(v[i])) v[dst++] = v[i];
    }
}




void CoarseWater_RefreshMarks(GameState& s) {
    CoarseWater& c = s.map.water;
    if (c.recs.empty() || c.g.empty()) return;
    for (const ShipRec& r : c.recs) SetBox(s, r, 0xff, 0, 15);
    for (ShipRec& r : c.recs) {
        const Unit* u = Unit_Find(s, r.unit);
        if (u == nullptr) {
            Unported(s, orig::kCoarseWaterRefreshMarks);  // a record of a freed unit: the original reads freed memory
            continue;
        }
        if (u->is_deleted != 0) continue;
        r.j = static_cast<std::uint16_t>(CoarseJ(u->y));
        r.i = static_cast<std::uint16_t>(CoarseI(u->x));
        SetBox(s, r, Ship_OrientClass(*u), 15, 0);
    }
}



void CoarseWater_UnmarkShip(GameState& s, const Unit& u) {
    ShipRec* r = FindRec(s, u);
    if (r == nullptr) return;
    const ShipRec me = *r;
    SetBox(s, me, 0xff, 0, 15);
    for (const ShipRec& q : s.map.water.recs) {
        if (q.unit.serial == me.unit.serial && q.unit.player == me.unit.player) continue;
        const Unit* o = Unit_Find(s, q.unit);
        if (o == nullptr || o->is_deleted != 0) continue;
        const std::int32_t di = static_cast<std::int16_t>(q.i) - static_cast<std::int16_t>(me.i);
        const std::int32_t dj = static_cast<std::int16_t>(q.j) - static_cast<std::int16_t>(me.j);
        if (std::abs(di) < 5 && std::abs(dj) < 6) SetBox(s, q, Ship_OrientClass(*o), 15, 0);
    }
}


void CoarseWater_MarkShip(GameState& s, const Unit& u) {
    if (u.is_deleted != 0) return;
    ShipRec* r = FindRec(s, u);
    if (r != nullptr) SetBox(s, *r, Ship_OrientClass(u), 15, 0);
}



bool Water_IsFreeForShip(GameState& s, std::uint8_t k, float x, float y) {
    CoarseWater& c = s.map.water;
    const auto i = static_cast<std::uint16_t>(CoarseI(x)), j = static_cast<std::uint16_t>(CoarseJ(y));
    if (i >= c.w || j >= c.h) {
        Unported(s, orig::kWaterIsFreeForShip);  // off the grid: the original reads outside it
        return false;
    }
    const std::uint8_t v = G(c, i, j);
    return k > 1 ? v == 0 : v != 10;
}


std::uint8_t Water_CellValueAt(const GameState& s, float x, float y) {
    const CoarseWater& c = s.map.water;
    const auto j = static_cast<std::uint16_t>(CoarseJ(y)), i = static_cast<std::uint16_t>(CoarseI(x));
    if (i >= c.w || j >= c.h) return 10;
    return c.g[static_cast<std::size_t>(j) * static_cast<std::size_t>(c.w) + i];
}


bool Water_FreePixelCoordForShip(GameState& s, std::uint8_t k, float x, float y) {
    const std::uint8_t v = Water_CellValueAt(s, x, y);
    return k > 1 ? v == 0 : (v == 0 || v == 11);
}


bool Ship_CanMoveTo(GameState& s, const Unit& u, float x, float y) {
    CoarseWater_UnmarkShip(s, u);
    const bool r = Water_FreePixelCoordForShip(s, 1, x, y);
    CoarseWater_MarkShip(s, u);
    return r;
}


bool CoarseWater_PointFree(GameState& s, float x, float y, bool strict) {
    CoarseWater& c = s.map.water;
    const std::int32_t i = static_cast<std::int16_t>(CoarseI(x)), j = static_cast<std::int16_t>(CoarseJ(y));
    if (i < 0 || i >= c.w || j < 0 || j >= c.h) return false;
    const std::uint8_t v = G(c, i, j);
    return v == 0 || (!strict && v == 15);
}

namespace {



bool ShipPath_LineFree(const std::vector<std::uint8_t>& g, std::int32_t W, Cell a, Cell b, std::int32_t s0) {
    const std::int32_t dx = std::abs(b.x - a.x), dy = std::abs(b.y - a.y);
    const std::int32_t sx = b.x >= a.x ? 1 : -1, sy = b.y >= a.y ? 1 : -1;
    std::int32_t st = s0 + 1;
    auto blocked = [&](std::int64_t idx) {
        const std::uint8_t v = idx >= 0 && static_cast<std::size_t>(idx) < g.size() ? g[static_cast<std::size_t>(idx)]
                                                                                    : static_cast<std::uint8_t>(10);
        if (st >= 3 && st <= 14) return v > 9;
        return v > 9 && v != 15;
    };
    if (dy <= dx) {
        std::int32_t err = 2 * dy - dx, x = a.x + sx;
        std::int64_t row = static_cast<std::int64_t>(a.y) * W;
        for (std::int32_t i = 1; i <= dx; ++i) {
            if (err > 0) {
                err += 2 * (dy - dx);
                row += static_cast<std::int64_t>(sy) * W;
            } else {
                err += 2 * dy;
            }
            if (blocked(row + x)) return false;
            x += sx, ++st;
        }
    } else {
        std::int32_t err = 2 * dx - dy, y = a.y + sy, x = a.x;
        for (std::int32_t i = 1; i <= dy; ++i) {
            if (err > 0) {
                err += 2 * (dx - dy);
                x += sx;
            } else {
                err += 2 * dx;
            }
            if (blocked(static_cast<std::int64_t>(y) * W + x)) return false;
            y += sy, ++st;
        }
    }
    return true;
}




bool ShipPath_Corner(GameState& s, const std::vector<std::uint8_t>& g, std::int32_t W, Cell st, Cell& out) {
    static constexpr std::int32_t kBack[8][2] = {{0, 1}, {-1, 0}, {0, -1}, {1, 0}, {-1, 1}, {-1, -1}, {1, -1}, {1, 1}};
    auto at = [&](Cell c) -> std::int32_t {
        const std::int64_t k = static_cast<std::int64_t>(c.y) * W + c.x;
        if (k < 0 || static_cast<std::size_t>(k) >= g.size()) {
            Unported(s, orig::kShipPathCorner);  // the walk left the grid
            return 0;
        }
        return g[static_cast<std::size_t>(k)];
    };
    Cell p = st, q = st;
    std::int32_t c = 0, d = at(st) - 1;
    for (int guard = 0; guard < 1 << 22; ++guard) {
        std::int32_t e = 0;
        do {
            if (d < 0 || d > 7) {
                Unported(s, orig::kShipPathCorner);  // a cell without a label on the chain
                return false;
            }
            p.x += kBack[d][0], p.y += kBack[d][1];
            e = at(p) - 1;
        } while (e == d);
        ++c;
        if (c > 1 && !ShipPath_LineFree(g, W, st, p, 0)) {
            const Cell m{(q.x + p.x) / 2, (q.y + p.y) / 2};
            out = ShipPath_LineFree(g, W, st, m, 0) ? m : q;
            return true;
        }
        q = p;
        if (e == 8) {
            // The walk reached the target with every run end in sight: the original leaves out unwritten (Q3). Ours
            // (docs/ai.md 10.17): the target itself is the waypoint.
            if (s.original_rules) return false;
            out = p;
            return true;
        }
        d = e;
    }
    return false;
}






bool ShipPath_Search(GameState& s, Cell st, Cell t, std::int32_t W, std::int32_t H, Cell& out, bool& ok) {
    ok = true;
    const std::vector<std::uint8_t>& grid = s.map.water.g;
    if (st.x == t.x && st.y == t.y) {
        out = st;
        return true;
    }
    if (ShipPath_LineFree(grid, W, st, t, 0)) {
        out = t;
        return true;
    }
    std::vector<std::uint8_t> g = grid;
    const std::int32_t si = st.y * W + st.x, ti = t.y * W + t.x;
    g[static_cast<std::size_t>(ti)] = 1;
    g[static_cast<std::size_t>(si)] = 0;
    const std::int32_t off[8] = {-W, 1, W, -1, 1 - W, W + 1, W - 1, -1 - W};
    std::vector<std::int32_t> q(0x800, 0);
    q[0] = ti;
    std::uint32_t head = 0, tail = 1;
    for (;;) {
        for (std::int32_t k = 0; k < 8; ++k) {
            const std::int32_t n = q[head] + off[k];
            if (n == si) {
                g[static_cast<std::size_t>(si)] = static_cast<std::uint8_t>(k + 1);
                g[static_cast<std::size_t>(ti)] = 9;
                ok = ShipPath_Corner(s, g, W, st, out);
                return true;
            }
            if (n < 0 || static_cast<std::size_t>(n) >= g.size()) {
                Unported(s, orig::kShipPathSearch);  // past the grid: the original reads outside it
                continue;
            }
            const std::uint8_t v = g[static_cast<std::size_t>(n)];
            bool take = v == 0;
            if (v == 15) {
                const std::int32_t dy = std::abs(n / W - st.y), dx = std::abs(n % H - st.x);
                take = (dx < 2 && dy < 2) || dx > 15 || dy > 15 || dx + dy > 15;
            }
            if (!take) continue;
            g[static_cast<std::size_t>(n)] = static_cast<std::uint8_t>(k + 1);
            q[tail] = n;
            tail = (tail + 1) & 0x7ff;
        }
        head = (head + 1) & 0x7ff;
        if (head == tail) return false;
    }
}

}  // namespace




bool Ship_FindWaypoint(GameState& s, Unit& u, float& px, float& py) {
    CoarseWater& c = s.map.water;
    const std::int32_t ti = CoarseI(px), tj = CoarseJ(py), si = CoarseI(u.x), sj = CoarseJ(u.y);
    if (static_cast<std::uint16_t>(ti) >= c.w || static_cast<std::uint16_t>(tj) >= c.h ||
        static_cast<std::uint16_t>(si) >= c.w || static_cast<std::uint16_t>(sj) >= c.h) {
        return false;
    }
    CoarseWater_UnmarkShip(s, u);
    Cell out{sj, 0};
    bool ok = true;
    if (!ShipPath_Search(s, Cell{static_cast<std::uint16_t>(si), static_cast<std::uint16_t>(sj)},
                         Cell{static_cast<std::uint16_t>(ti), static_cast<std::uint16_t>(tj)}, c.w, c.h, out, ok)) {
        CoarseWater_MarkShip(s, u);
        return false;
    }
    if (!ok) {
        // out is left unwritten: the original goes on with the ship's row and a stack leftover
        Unported(s, orig::kShipPathCorner);
        return false;
    }
    const std::int32_t ox = out.x, oy = out.y;
    if (!(ox >= 0 && ox < c.w && oy >= 0 && oy < c.h && ox >= 3 && ox <= c.w - 3 && oy >= 3 && oy <= c.h - 3)) {
        return false;  // the marks stay cleared
    }
    if (ox != static_cast<std::uint16_t>(ti) || oy != static_cast<std::uint16_t>(tj)) {
        px = static_cast<float>(ox * 96 + 48);
        py = static_cast<float>(oy * 48 + 24);
    }
    CoarseWater_MarkShip(s, u);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Sailing (docs/movement.md 15.3 .. 13.6).

namespace {

constexpr float kTwoPiF = 6.2831855f;
constexpr float kHalfPi = 1.5707964f;
constexpr float kFourOverPi = 1.2732395f;
constexpr float kPi = 3.1415927f;


void WrapAngleF(float& a) {
    if (a < 0.0f) {
        a = a + kTwoPiF;
        return;
    }
    if (!(a < kTwoPiF)) a = a - kTwoPiF;
    if (!(a < 6.2821856f)) a = 0.0f;
}

double DistU(const Unit& a, const Unit& b) {
    const double dx = static_cast<double>(a.x) - b.x, dy = static_cast<double>(a.y) - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

double DistP(float a, float b, float c, float d) {
    const double dx = static_cast<double>(a) - c, dy = static_cast<double>(b) - d;
    return std::sqrt(dx * dx + dy * dy);
}

struct V3 {
    float x = 0, y = 0, z = 0;
};


void Vec3_RotateZ(V3& v, float a) {
    const double sn = dmath::sin(static_cast<double>(a)), cs = dmath::cos(static_cast<double>(a));
    const double x = v.x, y = v.y;
    v.x = static_cast<float>(cs * x - sn * y);
    v.y = static_cast<float>(x * sn + cs * y);
}


double Vec3_Length(const V3& v) {
    return std::sqrt((static_cast<double>(v.x) * v.x + static_cast<double>(v.y) * v.y) +
                     static_cast<double>(v.z) * v.z);
}



double Vec3_AngleTo(const V3& a, const V3& b) {
    const double dot =
        (static_cast<double>(b.y) * a.y + static_cast<double>(a.z) * b.z) + static_cast<double>(b.x) * a.x;
    const double nb =
        std::sqrt((static_cast<double>(b.y) * b.y + static_cast<double>(b.z) * b.z) + static_cast<double>(b.x) * b.x);
    const double na =
        std::sqrt((static_cast<double>(a.y) * a.y + static_cast<double>(a.z) * a.z) + static_cast<double>(a.x) * a.x);
    double r = dmath::acos(dot / (nb * na));
    if (static_cast<double>(a.y) * b.x + static_cast<double>(b.y) * a.x < 0.0) r = static_cast<double>(kTwoPiF) - r;
    return r;
}


double Vec3_Heading(const V3& v) { return Vec3_AngleTo(v, V3{100.0f, 0.0f, 0.0f}); }



std::size_t SpriteIndex(const GameState& s, std::uint8_t player, const Unit& u, int kind, int dir) {
    const std::uint16_t g = Unit_Type(s, player, u).graphic;
    const std::size_t nation = g / 40, type = g % 40;
    if (nation >= 6 || type < 0x11 || type > 0x14 || dir < 0 || dir > 7) return SIZE_MAX;
    std::size_t k = 0;
    if (kind == 0) {
        k = static_cast<std::size_t>(dir);
    } else if (kind == 1) {
        if (dir != 5 && dir != 7) return SIZE_MAX;
        k = dir == 5 ? 8 : 9;
    } else {
        k = 10 + static_cast<std::size_t>(dir);
    }
    return (nation * 4 + (type - 0x11)) * 18 + k;
}

GameState::ShipSprite* Sprite(GameState& s, std::uint8_t player, const Unit& u, int kind, int dir,
                              std::uint32_t caller) {
    const std::size_t i = SpriteIndex(s, player, u, kind, dir);
    if (i == SIZE_MAX) {
        Unported(s, caller);  // no slot: the original uses a null pointer
        return nullptr;
    }
    GameState::ShipSprite& e = s.ship_sprites[i];
    if (e.state == 1 && s.tick >= e.ready_tick) e.state = 2;
    return &e;
}


std::uint8_t ShipGfx_State(GameState& s, std::uint8_t player, const Unit& u, int kind, int dir) {
    const GameState::ShipSprite* e = Sprite(s, player, u, kind, dir, orig::kShipGfxSlot);
    return e != nullptr ? e->state : 0;
}


void ShipGfx_Request(GameState& s, std::uint8_t player, const Unit& u, int kind, int dir) {
    GameState::ShipSprite* e = Sprite(s, player, u, kind, dir, orig::kShipGfxRequest);
    if (e == nullptr) return;
    ++e->refs;
    if (e->state == 0) {
        e->state = 1;
        e->ready_tick = s.tick + 1;
    }
}


void ShipGfx_Release(GameState& s, std::uint8_t player, const Unit& u, int kind, int dir) {
    GameState::ShipSprite* e = Sprite(s, player, u, kind, dir, orig::kShipGfxRelease);
    if (e == nullptr || e->refs == 0) return;
    --e->refs;
    if (e->state == 2 && e->refs == 0) e->state = 0;
}


void ShipGfx_WaitLoaded(GameState& s, std::uint8_t player, const Unit& u, int kind, int dir) {
    if (GameState::ShipSprite* e = Sprite(s, player, u, kind, dir, orig::kShipGfxSlot)) e->state = 2;
}



void ShipGfx_LoadNow(GameState& s, std::uint8_t player, const Unit& u, int kind, int dir) {
    GameState::ShipSprite* e = Sprite(s, player, u, kind, dir, orig::kShipGfxLoad);
    if (e == nullptr) return;
    ++e->refs;
    if (e->refs > 1 && e->state != 1) return;
    e->state = 2;
}

}  // namespace






void Ship_SetupSprites(GameState& s, std::uint8_t player, Unit& u) {
    u.field_121.fill(0), u.field_129.fill(0), u.field_131.fill(0);
    auto loaded = [&](int kind, int dir) { return ShipGfx_State(s, player, u, kind, dir) == 2; };
    if (u.work == 0xe && u.field_0ca < 2) {
        const House* h = u.in_house ? House_Find(s, HouseRef{u.house_player, u.house_slot, 0}) : nullptr;
        if (h == nullptr) {
            Unported(s, orig::kShipSetupSprites);
            return;
        }
        const int dir = h->field_0ac == 1 || h->field_0ac == 2 ? 5 : 7;
        if (loaded(1, dir)) {
            ShipGfx_Request(s, player, u, 1, dir);
        } else {
            ShipGfx_LoadNow(s, player, u, 1, dir);
        }
        u.field_121[static_cast<std::size_t>(dir)] = 1;
        return;
    }
    const int kind = u.is_deleted != 0 ? 2 : 0;
    const int dir = u.orient & 7;
    if (loaded(kind, dir)) {
        ShipGfx_Request(s, player, u, kind, dir);
        return;
    }
    ShipGfx_LoadNow(s, player, u, kind, dir);
    (kind == 2 ? u.field_131 : u.field_129)[static_cast<std::size_t>(dir)] = 1;
}

namespace {


bool Ship_NearDest(const GameState& s, std::uint8_t player, const Unit& u) {
    const double dx = static_cast<double>(u.x) - u.dest_x, dy = static_cast<double>(u.y) - u.dest_y;
    const double sp = Unit_Type(s, player, u).speed[player];
    return sp + sp > std::sqrt(dy * dy + dx * dx);
}


bool Ship_IsTurning(const GameState& s, std::uint8_t player, const Unit& u) {
    const double two_pi = kTwoPiF;
    auto wrap = [&](double h) {
        if (h < 0.0) return h + two_pi;
        if (h >= two_pi) {
            h -= two_pi;
            if (h >= static_cast<double>(6.2821856f)) h = 0.0;
        }
        return h;
    };
    double h = dmath::atan2(static_cast<double>(u.vel_y), -static_cast<double>(u.vel_x));
    if (h < 0.0) h += two_pi;
    h = wrap(h + static_cast<double>(kHalfPi));
    const double e = wrap(h - static_cast<double>(u.angle));
    return !(std::fabs(e) < static_cast<double>(Unit_Type(s, player, u).turn_rate));
}



bool Ship_TurnStep(GameState& s, std::uint8_t player, Unit& u) {
    if (std::fabs(u.vel_x) < 0.01f && std::fabs(u.vel_y) < 0.01f) return false;
    const UnitType& t = Unit_Type(s, player, u);
    double h = dmath::atan2(static_cast<double>(u.vel_y), -static_cast<double>(u.vel_x));
    if (h < 0.0) h += static_cast<double>(kTwoPiF);
    h += static_cast<double>(kHalfPi);
    float T = static_cast<float>(h);
    WrapAngleF(T);
    float A = u.angle;
    const float e0 = static_cast<float>(static_cast<double>(T) - A);
    float e = e0;
    WrapAngleF(e);
    if (!(std::fabs(e) >= t.turn_rate)) return false;
    const std::uint8_t o0 = u.orient;
    int step = 0;
    if (T > A) {
        step = e0 <= kPi ? 1 : -1;
    } else {
        step = !(static_cast<double>(A) - T < static_cast<double>(kPi)) ? 1 : -1;
    }
    A = static_cast<float>(step > 0 ? static_cast<double>(A) + t.turn_rate : static_cast<double>(A) - t.turn_rate);
    auto o = static_cast<std::uint8_t>(o0 + step);
    if (static_cast<std::int8_t>(o) < 0) {
        o = 7;
    } else if (o == 8) {
        o = 0;
    }
    if (u.field_129[o] == 0) {
        ShipGfx_Request(s, player, u, 0, o);
        u.field_129[o] = 1;
    }
    WrapAngleF(A);
    if (!(A < kTwoPiF)) WrapAngleF(A);
    const auto n = static_cast<std::uint8_t>(Ftol(static_cast<double>(A) * static_cast<double>(kFourOverPi)));
    if (ShipGfx_State(s, player, u, 0, n) != 2) return true;
    const int k = t.ship.frames;
    const double a = static_cast<double>(A) - static_cast<double>(n) * static_cast<double>(kPiOverFour);
    u.num_sprite = static_cast<float>(static_cast<std::uint8_t>(Ftol(a * k * static_cast<double>(kFourOverPi))));
    u.angle = A, u.orient = n;
    for (std::uint8_t b = 0; b < 8; ++b) {
        if (u.field_129[b] != 0 && b != n && b != o) {
            ShipGfx_Release(s, player, u, 0, b);
            u.field_129[b] = 0;
        }
    }
    return true;
}


bool Ship_WillCollide(const Unit& u, const Unit& c) {
    if (c.work == 0xe && c.field_108 == 0) return false;
    V3 a{100.0f, 0.0f, 0.0f};
    Vec3_RotateZ(a, static_cast<float>(static_cast<double>(c.angle) + kHalfPi));
    const V3 v{u.vel_x, -u.vel_y, 0.0f};
    const V3 d{c.x - u.x, u.y - c.y, 0.0f};
    const double ang = Vec3_AngleTo(v, d);
    const auto angf = static_cast<float>(ang);
    if (!(ang < static_cast<double>(1.0471976f))) {
        if (!(angf > 5.7595868f)) return false;
    }
    const double dist = DistU(u, c);
    const auto df = static_cast<float>(dist);
    if (dist < 250.0) return true;
    if (angf < 0.7853982f || angf > 5.4977875f) {
        if (c.work == 0xe && df < 400.0f) return true;
        if (df < 700.0f) return true;
    }
    if ((u.type >= 0x13 || c.type > 0x13) && df < 400.0f) return true;
    const double ang2 = Vec3_AngleTo(v, a);
    const auto a2f = static_cast<float>(ang2);
    if (ang2 < static_cast<double>(0.3926991f)) return false;
    if (a2f > 5.1050882f) return false;
    if (!(a2f < 3.5342917f)) return true;
    if (a2f <= 2.7488937f) return true;
    return false;
}



bool Ship_CheckCollision(GameState& s, const Unit& u) {
    if (s.ships.size() < 2) return false;
    std::vector<const Unit*> cand;
    for (const UnitRef& r : s.ships) {
        const Unit* o = Unit_Find(s, r);
        if (o == nullptr) {
            Unported(s, orig::kShipCheckCollision);  // a stale list entry
            continue;
        }
        if (o == &u || o->is_deleted != 0) continue;
        const double d = DistU(u, *o);
        const auto df = static_cast<float>(d);
        if (!(d < 900.0)) continue;
        if (u.type >= 0x13 || o->type >= 0x13) {
            cand.push_back(o);
            continue;
        }
        if (df > 600.0f) continue;
        if (u.type == 0x12 && o->type == 0x12 && df > 500.0f) continue;
        cand.push_back(o);
    }
    for (const Unit* c : cand) {
        if (Ship_WillCollide(u, *c)) return true;
    }
    return false;
}

}  // namespace


void Ship_Arrive(GameState& s, std::uint8_t player, Unit& u) {
    Event_ShipWake(s, player, u, false);
    u.work = 0xff;
    u.field_174 = 1;
    EventUnitEndMove(s, player, u);
}


void Ship_BeginMove(GameState& s, std::uint8_t player, Unit& u) {
    if (std::fabs(static_cast<double>(u.vel_y)) + std::fabs(static_cast<double>(u.vel_x)) < 1.0) {
        TUnit_OrderEnd(s, player, u, 1);
        return;
    }
    if (u.work == 0x32) return;
    if (u.field_174 != 2) Event_ShipWake(s, player, u, true);
    u.field_0ca = 20;
    u.work = 0xff;
}



void Ship_Replan(GameState& s, std::uint8_t player, Unit& u) {
    u.field_158 = 0;
    if (u.field_154 == 0 && !Ship_FindWaypoint(s, u, u.dest_x, u.dest_y)) {
        const double dx = static_cast<double>(u.dest_x) - u.final_x, dy = static_cast<double>(u.dest_y) - u.final_y;
        if (std::sqrt(dy * dy + dx * dx) > 1.0) {
            TUnit_OrderMove(s, player, u, u.final_x, u.final_y, 1);
            return;
        }
        if (u.field_145 < 10) {
            ++u.field_145;
            Event_ShipWake(s, player, u, false);
            u.field_07c = 40;
            return;
        }
        u.field_145 = 0;
        TUnit_ClearPath(u);
        return;
    }
    u.field_145 = 0;
    TUnit_ComputeVelocity(s, player, u);
    Ship_BeginMove(s, player, u);
    u.field_09f = 0;
}




void Ship_FixTarget(GameState& s, Unit& u) {
    CoarseWater& c = s.map.water;
    const std::int32_t cx = static_cast<std::int16_t>(CoarseI(u.final_x)),
                       cy = static_cast<std::int16_t>(CoarseJ(u.final_y));
    if (cx >= 0 && cx < c.w && cy >= 0 && cy < c.h && G(c, cx, cy) == 0) return;
    const bool tr = u.type == 0x11;
    const auto dx = static_cast<float>(static_cast<double>(u.x) - u.final_x);
    const double dyx = static_cast<double>(u.y) - u.final_y;
    const auto dy = static_cast<float>(dyx);
    const double len = std::sqrt(dyx * dy + static_cast<double>(dx) * dx);
    const auto n = static_cast<float>(len * static_cast<double>(tr ? 0.2f : 0.01f));
    if (n < 2.0f) {
        TUnit_ClearPath(u);
        if (tr) {
            u.field_16c = static_cast<float>(static_cast<double>(u.x) - static_cast<double>(dx) * 0.1f);
            u.field_170 = static_cast<float>(static_cast<double>(u.y) - static_cast<double>(dy) * 0.1f);
        }
        return;
    }
    const double inv = 1.0 / n;
    const auto sx = static_cast<float>(dx * inv), sy = static_cast<float>(inv * dy);
    float px = u.final_x, py = u.final_y, i = 0.0f;
    if (0.0f < n) {
        do {
            px = px + sx, py = py + sy;
            const double ex = static_cast<double>(px) - u.x, ey = static_cast<double>(py) - u.y;
            const bool far = std::sqrt(ey * ey + ex * ex) > 200.0;
            if (CoarseWater_PointFree(s, px, py, far)) {
                if (tr) u.field_16c = px - sx, u.field_170 = py - sy;
                u.final_x = px, u.final_y = py, u.dest_x = px, u.dest_y = py;
                return;
            }
            i = i + 1.0f;
        } while (i < n);
    }
    TUnit_ClearPath(u);
    if (tr) u.field_16c = u.x - sx, u.field_170 = u.y - sy;
}


void Ship_OrderMove(GameState& s, std::uint8_t player, Unit& u, float x, float y) {
    const std::uint8_t w = u.work;
    if (w == 0xe || u.field_154 != 0 || w == 5 || w == 0xc || w == 0xd || w == 6 || w == 4) return;
    u.dest_x = x, u.dest_y = y, u.final_x = x, u.final_y = y;
    u.num_work = 0;
    u.is_stopped = 0;
    u.field_174 = 1;
    Ship_FixTarget(s, u);
    if (DistP(u.x, u.y, u.final_x, u.final_y) < 1.0) {
        Ship_Arrive(s, player, u);
    } else {
        Ship_Replan(s, player, u);
    }
}


void Ship_MoveTo(GameState& s, std::uint8_t player, Unit& u, float x, float y) {
    u.dest_x = x, u.dest_y = y, u.final_x = x, u.final_y = y;
    TUnit_ComputeVelocity(s, player, u);
    u.is_stopped = 0;
    Ship_BeginMove(s, player, u);
}


void Ship_OrderEnd(GameState& s, std::uint8_t player, Unit& u) {
    TUnit_ClearPath(u);
    Ship_Arrive(s, player, u);
}


void Ship_OrderQueued(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t work,
                      std::uint16_t arg) {
    u.dest_x = x, u.dest_y = y, u.final_x = x, u.final_y = y;
    u.work = work;
    u.field_0ca = arg;
    u.num_work = 0;
    u.is_stopped = 0;
    u.field_174 = 1;
    if (!Water_IsFreeForShip(s, 10, x, y)) {
        Ship_FixTarget(s, u);
        if (DistP(u.x, u.y, u.final_x, u.final_y) < 1.0) {
            Ship_Arrive(s, player, u);
            return;
        }
        u.final_x = u.dest_x, u.final_y = u.dest_y;
    }
    if (Ship_FindWaypoint(s, u, u.dest_x, u.dest_y)) {
        TUnit_ComputeVelocity(s, player, u);
        Ship_BeginMove(s, player, u);
        return;
    }
    TUnit_ClearPath(u);
    Ship_Arrive(s, player, u);
}



void Ship_EndMove(GameState& s, std::uint8_t player, Unit& u) {
    u.field_09f = 0;
    const double dx = static_cast<double>(u.x) - u.final_x, dy = static_cast<double>(u.y) - u.final_y;
    if (std::sqrt(dy * dy + dx * dx) > 100.0) {
        TUnit_OrderMove(s, player, u, u.final_x, u.final_y, 1);
        return;
    }
    TUnit_StopMoving(s, player, u);
    u.is_stopped = 1;
    u.work = 0xff;
    if (u.type == 0x11) {
        const double lx = static_cast<double>(u.x) - u.field_16c, ly = static_cast<double>(u.y) - u.field_170;
        if (std::sqrt(ly * ly + lx * lx) < 100.0) {
            u.vel_x = u.field_16c - u.x;
            u.vel_y = u.field_170 - u.y;
        }
    }
}



void Ship_LeavePort(GameState& s, std::uint8_t player, Unit& u, const House& h) {
    const float vx = 400.0f, vy = h.field_0ac == 1 || h.field_0ac == 2 ? 200.0f : -200.0f;
    u.field_154 = 1;
    const std::uint8_t modes[2] = {10, 1};
    for (const std::uint8_t mode : modes) {
        for (int sign = 1; sign >= -1; sign -= 2) {
            const float px = static_cast<float>(sign) * vx + u.x, py = static_cast<float>(sign) * vy + u.y;
            if (Water_IsFreeForShip(s, mode, px, py)) {
                u.dest_x = px, u.dest_y = py;
                TUnit_ComputeVelocity(s, player, u);
                Ship_BeginMove(s, player, u);
                u.final_x = h.rally_x, u.final_y = h.rally_y;
                if (sign < 0) Event_ShipWake(s, player, u, false);
                return;
            }
        }
    }
    TUnit_OrderMove(s, player, u, -vx + u.x, -vy + u.y, 0);
    u.final_x = h.rally_x, u.final_y = h.rally_y;
}

namespace {




void Ship_AimAt(GameState& s, std::uint8_t player, Unit& u, float tx, float ty, bool near, bool house) {
    if (!near) {
        const double dx = static_cast<double>(u.x) - tx, dy = static_cast<double>(u.y) - ty;
        if (!house && u.type == 0x12 && std::sqrt(dy * dy + dx * dx) > 900.0) return;
        TUnit_ClearPath(u);
        Ship_Arrive(s, player, u);
    }
    V3 a{100.0f, 100.0f, 0.0f};
    Vec3_RotateZ(a, static_cast<float>(static_cast<double>(u.angle) + kHalfPi));
    a.y = -a.y;
    V3 b{10.0f, 0.0f, 0.0f};
    const V3 d{tx - u.x, ty - u.y, 0.0f};
    const auto ha = static_cast<float>(Vec3_Heading(a));
    const double tx87 = Vec3_Heading(d) + static_cast<double>(kHalfPi);
    auto T = static_cast<float>(tx87);
    if (tx87 < 0.0) {
        T = T + kTwoPiF;
    } else {
        if (T >= kTwoPiF) T = T - kTwoPiF;
        if (T >= 6.2821856f) T = 0.0f;
    }
    if (std::fabs(static_cast<double>(ha) - T) > static_cast<double>(0.2356195f)) {
        Vec3_RotateZ(b, T);
        u.vel_x = b.x, u.vel_y = b.y;
    } else {
        u.vel_x = 0.0f, u.vel_y = 0.0f;
    }
}

}  // namespace


void MakeMoveShip(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    bool near = true;
    if (u.work == 0xe) goto tail;
    Ship_UpdateWeapons(s, player, u);
    if (u.field_07c != 0) {
        --u.field_07c;
        if (u.field_07c != 0) goto tail;
        Event_ShipWake(s, player, u, true);
        if (u.field_145 != 0) Ship_Replan(s, player, u);
        goto tail;
    }
    {
        const double dx = static_cast<double>(u.x) - u.final_x, dy = static_cast<double>(u.y) - u.final_y;
        near = std::sqrt(dy * dy + dx * dx) < 1.0;
        float vx = u.vel_x, vy = u.vel_y;
        if (u.field_174 == 2) vx = -vx, vy = -vy;
        if (u.work == 0x32 || u.work == 0x33) Unported(s, orig::kShipGetStep);  // no writer of these Works in the game
        if (u.field_154 != 0) {
            if (Ship_CheckCollision(s, u)) return;
            if (u.field_174 == 2) {
                u.x = u.x - vx, u.y = u.y - vy;
            } else {
                u.x = vx + u.x, u.y = vy + u.y;
            }
            const auto d2 = static_cast<float>(DistP(u.x, u.y, u.dest_x, u.dest_y));
            if (TUnit_Speed(s, player, u, orig::kMoveShip) < d2) goto tail;
            u.field_154 = 0;
        }
        if (!near) {
            const float nx = vx + u.x, ny = vy + u.y;
            if (Ship_CanMoveTo(s, u, nx, ny) || Ship_IsTurning(s, player, u)) goto move;
            if (Water_CellValueAt(s, nx, ny) == 10) {
                u.dest_x = u.final_x, u.dest_y = u.final_y;
                Ship_Replan(s, player, u);
                goto tail;
            }
            if (!Ship_CheckCollision(s, u)) goto move;
            const auto fx = static_cast<float>(static_cast<double>(vx) * 10.0 + u.x);
            const auto fy = static_cast<float>(static_cast<double>(vy) * 10.0 + u.y);
            if (Ship_CanMoveTo(s, u, fx, fy)) goto move;
            if (Rng_Next(s) < 5000) {
                u.dest_x = u.final_x, u.dest_y = u.final_y;
                Ship_Replan(s, player, u);
                goto tail;
            }
            Event_ShipWake(s, player, u, false);
            u.field_158 = 0;
            goto tail;
        }
    move:
        if (u.type == 0x11 && !u.passengers.empty()) {
            if (Rng_Next(s) % 5 == 1) {
                const Cell c{u.map_x, u.map_y};
                for (std::size_t i = 0; i < u.passengers.size(); ++i) {
                    Unit* p = Unit_Find(s, u.passengers[i]);
                    if (p != nullptr) TUnit_SetCell(s, u.passengers[i].player, *p, c);
                }
            }
        }
        if (near) {
            Ship_TurnStep(s, player, u);
            Event_ShipWake(s, player, u, false);
            goto tail;
        }
        if (Ship_NearDest(s, player, u)) {
            if (DistP(u.dest_x, u.dest_y, u.final_x, u.final_y) > 100.0) {
                u.dest_x = u.final_x, u.dest_y = u.final_y;
                Ship_Replan(s, player, u);
            } else {
                TUnit_ClearPath(u);
                Ship_Arrive(s, player, u);
            }
            goto tail;
        }
        if (u.field_174 == 2) {
            u.x = u.x - vx, u.y = u.y - vy;
            goto tail;
        }
        if (Ship_TurnStep(s, player, u)) goto tail;
        u.x = vx + u.x, u.y = vy + u.y;
    }
tail:
    if (u.player_unit_attack != 0xff) {
        Unit* tg = AttackTarget(s, u);
        if (tg == nullptr) {
            Unported(s, orig::kMoveShip);  // a target player without a target: the original reads a null pointer
        } else if (tg->is_deleted != 0) {
            TUnit_AttackEnd(s, player, u);
        } else {
            const double d = DistU(*tg, u);
            if (!(static_cast<double>(t.range[player]) < d)) {
                Ship_AimAt(s, player, u, tg->x, tg->y, near, false);
            } else {
                TUnit_OnAttackedBy(s, player, u, u.attack_ref.player, *tg, 1);
            }
        }
    }
    if (u.player_house_attack != 0xff) {
        House* h = HouseTarget(s, u);
        if (h == nullptr) {
            Unported(s, orig::kMoveShip);
        } else if (h->is_destroyed != 0) {
            TUnit_AttackEnd(s, player, u);
        } else {
            const double dx = static_cast<double>(u.x) - h->x, dy = static_cast<double>(u.y) - h->y;
            if (!(static_cast<double>(t.range[player]) < std::sqrt(dy * dy + dx * dx))) {
                Ship_AimAt(s, player, u, h->x, h->y, near, true);
            } else {
                TUnit_AttackHouse(s, player, u, u, u.house_attack_player, u.house_attack_slot, 0, 1);
            }
        }
    }
    const Cell c = WorldToCell(u.x, u.y);
    if (c.x != u.map_x || c.y != u.map_y) TUnit_SetCell(s, player, u, c);
    TUnit_UpdateGroundHeight(s, player, u);
}



void Ship_ReleaseSprites(GameState& s, std::uint8_t player, Unit& u) {
    for (std::uint8_t d = 0; d < 8; ++d) {
        if (u.field_121[d] != 0 && (u.is_deleted != 0 || (u.work == 0xe && u.field_0ca > 1 &&
                                                          ShipGfx_State(s, player, u, 0, u.orient) == 2))) {
            ShipGfx_Release(s, player, u, 1, d);
            u.field_121[d] = 0;
        }
        if (u.field_129[d] != 0 && u.field_139 == 2) {
            ShipGfx_Release(s, player, u, 0, d);
            u.field_129[d] = 0;
        }
        if (u.field_131[d] != 0 && 0xffff - static_cast<std::int32_t>(u.field_0ca) >= 2400) {
            ShipGfx_Release(s, player, u, 2, d);
            u.field_131[d] = 0;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Creation, the port's production, deletion (docs/movement.md 15.2).




void Ship_Created(GameState& s, std::uint8_t p, Unit& u) {
    s.ships.push_back(Unit_Ref(p, u));
    CoarseWater_AddShip(s, p, u);
    const ShipGraphic& g = Unit_Type(s, p, u).ship;
    u.weapon_count = static_cast<std::uint8_t>(g.weapons.size());
    u.guns.clear();
    for (const ShipWeaponType& w : g.weapons) {
        const float a = w.arc_count == 1 ? w.arcs[2] : w.f28;
        u.guns.push_back(Unit::ShipGun{a, a, 0});
    }
}


void Ship_Deleted(GameState& s, std::uint8_t p, const Unit& u) {
    for (std::size_t i = 0; i < s.ships.size(); ++i) {
        if (s.ships[i].player == p && s.ships[i].serial == u.serial) {
            s.ships.erase(s.ships.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    CoarseWater_RemoveShip(s, Unit_Ref(p, u));
}






std::uint16_t AddUnit(GameState& s, float x, float y, std::uint8_t p, std::uint16_t type, bool force,
                      std::uint8_t own) {
    if (type == 1) type = 0;
    if (p >= s.players.size() || s.players[p].is_lost != 0) return 0xffff;
    const std::uint8_t nation = own != 0xff ? own : s.players[p].nation;
    if (type >= 40 || !s.unit_types[nation][type].present) return 0xffff;
    if (!force) {
        auto to_float = [](std::int32_t v) {
            double d = static_cast<double>(v);
            if (v < 0) d += static_cast<double>(4294967296.0f);
            return static_cast<float>(d);
        };
        const Cell c = WorldToCell(to_float(Ftol(static_cast<double>(x))), to_float(Ftol(static_cast<double>(y))));
        if (!(c.x > 3 && c.y > 9 && c.x < s.map.width - 3 && c.y < s.map.height - 9)) return 0xffff;
        if (s.unit_types[s.players[0].nation][type].is_ship != 0) {
            if (!Cell_Obstacle(s.map, c.x, c.y)) return 0xffff;
        } else {
            Unported(s, orig::kSpotCheckLand);  // the land rule of the spot check
            return 0xffff;
        }
    }
    Unit u = CreateUnit(s, x, y, type, nation, p);
    if (nation != s.players[p].nation) u.nation = nation;
    if (!TUnit_IsSpotFree(s, p, u)) {
        // The unit is destroyed without DeleteUnit: a ship stays in the ship list and the ShipRecs, dangling.
        if (s.unit_types[nation][type].is_ship != 0) Unported(s, orig::kAddUnit);
        return 0xffff;
    }
    return AddUnitToPlayer(s, p, u).id;
}

namespace {


std::uint8_t Port_LaunchOrient(const House& h) { return h.field_0ac == 1 || h.field_0ac == 2 ? 5 : 7; }

}  // namespace




void Port_StartShip(GameState& s, std::uint8_t p, std::uint16_t k) {
    {
        const House& h = s.players[p].houses[k];
        if (h.field_0ac == 0) return;
    }
    const std::uint16_t id = AddUnit(s, s.players[p].houses[k].exit_x, s.players[p].houses[k].exit_y, p,
                                     s.players[p].houses[k].queue[0], false, House_TrainNation(s, p, k));
    House& h = s.players[p].houses[k];
    Unit* u = id == 0xffff ? nullptr : Unit_FindById(s, p, id);
    h.has_field_0b0 = u != nullptr;
    h.field_0b0 = u != nullptr ? Unit_Ref(p, *u) : UnitRef{};
    if (u == nullptr) return;
    u->field_108 = 0;
    u->in_house = true, u->house_player = p, u->house_slot = k;
    u->work = 0xe;
    const std::uint8_t o = Port_LaunchOrient(h);
    ShipGfx_Request(s, p, *u, 1, o);
    TUnit_SetOrient(*u, o);
    u->field_0ca = 0;
    u->field_139 = 1;
    u->field_121[o] = 1;
}

namespace {

using Quad = std::array<float, 8>;



Quad Ship_Footprint(const GameState& s, std::uint8_t player, const Unit& u) {
    const ShipGraphic& g = Unit_Type(s, player, u).ship;
    Quad q{};
    const auto yz = static_cast<float>(static_cast<double>(u.y) - u.z);
    const double n = g.frames;
    const std::uint8_t o = u.orient;
    const double fa = ((static_cast<double>(u.angle) - static_cast<double>(o) * static_cast<double>(kPiOverFour)) * n) *
                      static_cast<double>(kFourOverPi);
    const std::int32_t f = static_cast<std::uint8_t>(Ftol(fa)) + static_cast<std::int32_t>(g.frames) * o;
    const auto a = static_cast<float>((static_cast<double>(f) / n + 1.0) * static_cast<double>(kPiOverFour));
    if (g.hull.size() < 2) return q;  // the callers check (no hull: the original reads past the array)
    auto corner = [&](const std::array<float, 3>& pt, float dy, float& ox, float& oy) {
        V3 v{pt[0], static_cast<float>(static_cast<double>(pt[1]) + dy), pt[2]};
        Vec3_RotateZ(v, a);
        ox = static_cast<float>((static_cast<double>(v.x) - v.y) * static_cast<double>(0.8660254f) + u.x);
        const double t = (static_cast<double>(v.y) + v.x) * 0.5 + v.z;
        const auto sy = static_cast<float>(static_cast<double>(yz) + -t);
        oy = static_cast<float>(static_cast<double>(sy) + 55.0);
    };
    corner(g.hull[0], 40.0f, q[0], q[1]);
    corner(g.hull[0], -40.0f, q[2], q[3]);
    corner(g.hull[1], 40.0f, q[6], q[7]);
    corner(g.hull[1], -40.0f, q[4], q[5]);
    return q;
}


bool Quad_ContainsPoint(const Quad& q, float px, float py) {
    float sum = 0.0f;
    for (int i = 0; i < 4; ++i) {
        const int j = (i + 1) % 4;
        const V3 a{q[2 * i] - px, q[2 * i + 1] - py, 0.0f};
        const V3 b{q[2 * j] - px, q[2 * j + 1] - py, 0.0f};
        const auto dot = static_cast<float>((0.0 + static_cast<double>(b.y) * a.y) + static_cast<double>(b.x) * a.x);
        const auto nb = static_cast<float>(Vec3_Length(b));
        const double na = Vec3_Length(a);
        sum = static_cast<float>(dmath::acos(static_cast<double>(dot) / (na * nb)) + sum);
    }
    return std::fabs(static_cast<double>(sum) - kTwoPiF) < 1.0;
}


bool Quad_Overlap(const Quad& q1, const Quad& q2) {
    for (int i = 0; i < 4; ++i) {
        if (Quad_ContainsPoint(q1, q2[2 * i], q2[2 * i + 1])) return true;
    }
    for (int i = 0; i < 4; ++i) {
        if (Quad_ContainsPoint(q2, q1[2 * i], q1[2 * i + 1])) return true;
    }
    return false;
}

bool HasHull(GameState& s, std::uint8_t player, const Unit& u, std::uint32_t caller) {
    const ShipGraphic& g = Unit_Type(s, player, u).ship;
    if (g.hull.size() >= 2 && g.frames != 0) return true;
    Unported(s, caller);  // no hull data (the nation 0 cruiser's graphic file is missing: a division by zero)
    return false;
}



const Unit* Ship_FindOverlapping(GameState& s, std::uint8_t player, const Unit& u) {
    if (s.ships.size() < 2) return nullptr;
    std::vector<std::pair<std::uint8_t, const Unit*>> cand;
    for (const UnitRef& r : s.ships) {
        const Unit* o = Unit_Find(s, r);
        if (o == nullptr) {
            Unported(s, orig::kShipFindOverlapping);  // a stale list entry
            continue;
        }
        const double d = DistU(*o, u);
        if (o == &u || o->is_deleted != 0) continue;
        if (d < 100.0) return o;
        if (d < 1000.0) cand.emplace_back(r.player, o);
    }
    if (cand.empty() || !HasHull(s, player, u, orig::kShipGfxFootprint)) return nullptr;
    const Quad q0 = Ship_Footprint(s, player, u);
    for (std::size_t i = cand.size(); i-- > 0;) {
        if (!HasHull(s, cand[i].first, *cand[i].second, orig::kShipGfxFootprint)) continue;
        if (Quad_Overlap(q0, Ship_Footprint(s, cand[i].first, *cand[i].second))) return cand[i].second;
    }
    return nullptr;
}

}  // namespace





void Port_ShipStage(GameState& s, std::uint8_t p, std::uint16_t k) {
    House& h = s.players[p].houses[k];
    Unit* u = Unit_Find(s, h.field_0b0);
    if (u == nullptr) {
        Unported(s, orig::kUpdateUnits);  // a ship the port cannot find
        return;
    }
    const std::uint8_t up = h.field_0b0.player;
    if (Ship_FindOverlapping(s, up, *u) != nullptr && u->field_108 == 0) {
        h.field_044 = static_cast<float>(Unit_Type(s, up, *u).build_time[up]);
        return;
    }
    const UnitType& qt = s.unit_types[House_TrainNation(s, p, k)][h.queue[0]];  // B263
    u->field_108 = 1;
    const double v = 4.0 - static_cast<double>(h.field_044) * 4.0 / static_cast<double>(qt.build_time[up]);
    const auto stage = static_cast<std::uint16_t>(Ftol(v));
    u->field_0ca = stage;
    if (stage != 2) return;
    const std::uint8_t o = u->orient;
    if (u->field_129[o & 7] != 0) return;
    TUnit_SetOrient(*u, o);
    ShipGfx_Request(s, up, *u, 0, u->orient);
    u->field_129[u->orient & 7] = 1;
}



void Port_ReleaseShip(GameState& s, std::uint8_t p, House& h) {
    (void)p;
    Unit* u = Unit_Find(s, h.field_0b0);
    if (u == nullptr) {
        Unported(s, orig::kUpdateUnits);  // a ship the port cannot find
        return;
    }
    u->work = 0xff;
    u->in_house = false;
    if (Unit_Type(s, h.field_0b0.player, *u).is_ship != 0) Ship_LeavePort(s, h.field_0b0.player, *u, h);


    if (!s.original_rules && Player_IsYou(s, h.field_0b0.player)) u->field_006 = 0, u->field_142 = 300;
}



void Port_Destroyed(GameState& s, std::uint8_t p, House& h) {
    (void)p;
    if (!h.has_field_0b0) return;
    Unit* u = Unit_Find(s, h.field_0b0);
    if (u == nullptr) {
        Unported(s, orig::kPortDestroyed);  // a ship the port cannot find
    } else {
        OnUnitKilled(s, h.field_0b0.player, *u, 0xff);
    }
    for (auto& c : s.unit_cells) c.clear();
}






void Port_CancelShip(GameState& s, House& h, bool clear_grid) {
    Unit* u = Unit_Find(s, h.field_0b0);
    if (u == nullptr) {
        Unported(s, orig::kHouseCancelTrain);  // a ship the port cannot find
        h.has_field_0b0 = false, h.field_0b0 = UnitRef{};
        return;
    }
    const std::uint8_t up = h.field_0b0.player;
    for (std::uint8_t d = 0; d < 8; ++d) {
        std::uint8_t* flags[3] = {&u->field_121[d], &u->field_129[d], &u->field_131[d]};
        const int kinds[3] = {1, 0, 2};
        for (int i = 0; i < 3; ++i) {
            if (*flags[i] == 0 || ShipGfx_State(s, up, *u, kinds[i], d) == 0) continue;
            ShipGfx_WaitLoaded(s, up, *u, kinds[i], d);
            ShipGfx_Release(s, up, *u, kinds[i], d);
            *flags[i] = 0;
        }
    }
    u->in_house = false;
    u->field_0ca = 0xb;
    if (clear_grid) {
        for (auto& c : s.unit_cells) c.clear();
    }
    Unit_ClearReferences(s, Unit_Ref(up, *u));
    Ship_Deleted(s, up, *u);
    if (u->in_cell_list) Cell_RemoveUnit(s, up, *u);
    if (!s.original_rules) {  // docs/bugs.md B42
        Player& pl = s.players[up];
        --pl.unit_count[u->type % 40];
        --pl.units_alive;
    }
    Unit_Delete(s, up, s.players[up].id_slots[u->id]);
    h.has_field_0b0 = false, h.field_0b0 = UnitRef{};
}

// ---------------------------------------------------------------------------------------------------------------
// Turrets and naval combat (docs/combat.md 16 .. 17).

namespace {


double AddAngle(double a, double b) {
    double v = a + b;
    if (v < 0.0) {
        v += static_cast<double>(kTwoPiF);
    } else if (v >= static_cast<double>(kTwoPiF)) {
        v -= static_cast<double>(kTwoPiF);
    }
    if (!(v < static_cast<double>(6.2821856f))) return 0.0;
    if (v < 0.0) return static_cast<double>(6.2811856f);
    return v;
}


bool InArc(const ShipWeaponType& w, double rel) {
    double r = rel - w.arcs[2];
    if (r > static_cast<double>(kPi)) r = static_cast<double>(kTwoPiF) - r;
    if (w.arcs[1] <= r && r <= w.arcs[0]) return true;
    if (w.arc_count == 2) {
        r = rel - w.arcs[5];
        if (r > static_cast<double>(kPi)) r = static_cast<double>(kTwoPiF) - r;
        if (w.arcs[4] <= r && r <= w.arcs[3]) return true;
    }
    return false;
}


float HullAngle(const ShipGraphic& g, const Unit& u) {
    const double step = static_cast<double>(kPiOverFour) / static_cast<double>(g.frames);
    return static_cast<float>(static_cast<double>(Ftol(static_cast<double>(u.angle) / step)) * step);
}



V3 WeaponPoint(const ShipWeaponType& w, float angle, float elev, std::size_t idx) {
    const std::array<float, 3> pa = idx < w.points.size() ? w.points[idx] : std::array<float, 3>{};
    V3 p{pa[0], pa[1], pa[2]};
    if (w.slot != 0) {
        V3 d{pa[0] - w.pos[0], pa[1] - w.pos[1], pa[2] - w.pos[2]};
        Vec3_RotateZ(d, elev);
        p = V3{d.x + w.pos[0], d.y + w.pos[1], d.z + w.pos[2]};
    }
    Vec3_RotateZ(p, angle);
    return p;
}


bool TurnTurret(const ShipWeaponType& w, Unit::ShipGun& g) {
    if (w.slot == 0) return false;
    const float cur = g.angle, tgt = g.target, step = w.turn_step;
    if (std::fabs(static_cast<double>(tgt) - cur) < static_cast<double>(step)) {
        g.angle = tgt;
        return false;
    }
    const bool down = tgt > cur ? static_cast<double>(tgt) - cur > static_cast<double>(kPi)
                                : static_cast<double>(cur) - tgt <= static_cast<double>(kPi);
    if (down) {
        g.angle = static_cast<float>(static_cast<double>(cur) - step);
    } else {
        g.angle = static_cast<float>(static_cast<double>(cur) + step + static_cast<double>(0.01f));
    }
    WrapAngleF(g.angle);
    if (g.angle >= kTwoPiF) WrapAngleF(g.angle);
    return true;
}


void SnapTurret(const ShipWeaponType& w, Unit::ShipGun& g) {
    auto k = static_cast<std::uint8_t>(Ftol(static_cast<double>(g.target) * static_cast<double>(kFourOverPi)));
    if (k > 7) k = 7;
    const std::uint8_t n = w.steps;
    const double base = static_cast<double>(k) * static_cast<double>(kPiOverFour);
    const double step = static_cast<double>(kPiOverFour) / static_cast<double>(n);
    auto j = static_cast<std::uint8_t>(Ftol((static_cast<double>(g.target) - base) / step));
    if (n != 0 && j > n - 1) j = static_cast<std::uint8_t>(n - 1);
    g.target = static_cast<float>(static_cast<double>(j) * step + base + static_cast<double>(0.001f));
}


bool AimFixed(const ShipGraphic& sg, const ShipWeaponType& w, const Unit::ShipGun& g, const Unit& u, float x, float y) {
    const float aq = HullAngle(sg, u);
    const V3 p = WeaponPoint(w, aq, g.angle, 0);
    const double uy = static_cast<double>(u.y) - u.z;
    const double dx = static_cast<double>(u.x) - x, dy = uy - y;
    if (std::sqrt(dx * dx + dy * dy) > static_cast<double>(w.fire_range)) return false;
    const auto mx = static_cast<float>((static_cast<double>(p.x) - p.y) * static_cast<double>(0.8660254f) + u.x);
    const auto my = static_cast<float>(-((static_cast<double>(p.y) + p.x) * 0.5 + p.z) + uy);
    const double abs = AddAngle(aq, g.angle);
    double t = -dmath::atan2(static_cast<double>(y) - my, static_cast<double>(x) - mx) - static_cast<double>(kHalfPi);
    if (t < 0.0) t += static_cast<double>(kTwoPiF);
    double diff = std::fabs(t - abs);
    if (diff > static_cast<double>(kPi)) diff = static_cast<double>(kTwoPiF) - diff;
    return diff <= static_cast<double>(0.2617994f);
}



bool AimTurret(const ShipGraphic& sg, const ShipWeaponType& w, const Unit::ShipGun& g, const Unit& u, float x, float y,
               float& out) {
    const float aq = HullAngle(sg, u);
    V3 r{w.pos[0], w.pos[1], w.pos[2]};
    Vec3_RotateZ(r, aq);
    const auto oa = static_cast<float>((static_cast<double>(r.x) - r.y) * static_cast<double>(0.8660254f));
    const auto ob = static_cast<float>(-((static_cast<double>(r.y) + r.x) * 0.5 + r.z));
    const auto px = static_cast<float>(static_cast<double>(oa) + u.x);
    const auto py = static_cast<float>((static_cast<double>(u.y) - u.z) + ob);
    out = 0.0f;
    const double ex = static_cast<double>(px) - x, ey = static_cast<double>(py) - y;
    if (std::sqrt(ex * ex + ey * ey) > static_cast<double>(w.fire_range)) return false;
    const double abs = AddAngle(aq, g.angle);
    const double t =
        -dmath::atan2(static_cast<double>(y) - py, static_cast<double>(x) - px) - static_cast<double>(kHalfPi);
    out = static_cast<float>(t);
    if (t < 0.0) out = static_cast<float>(t + static_cast<double>(kTwoPiF));
    float diff = abs < static_cast<double>(out) ? static_cast<float>(static_cast<double>(out) - abs)
                                                : static_cast<float>(abs - static_cast<double>(out));
    if (diff > kPi) diff = static_cast<float>(static_cast<double>(kTwoPiF) - diff);
    float rel = static_cast<float>(static_cast<double>(out) - aq);
    WrapAngleF(rel);
    if (!InArc(w, rel)) {
        out = static_cast<float>(static_cast<double>(aq) - (w.arc_count == 1 ? w.arcs[2] : w.f28));
        WrapAngleF(out);
        return false;
    }
    return diff <= w.turn_step;
}


void ShipMuzzle(const ShipGraphic& sg, const ShipWeaponType& w, const Unit::ShipGun& g, const Unit& u, float m[3]) {
    const V3 p = WeaponPoint(w, HullAngle(sg, u), g.angle, 0);
    m[0] = static_cast<float>((static_cast<double>(p.x) - p.y) * static_cast<double>(0.8660254f) + u.x);
    const auto b = static_cast<float>(-((static_cast<double>(p.y) + p.x) * 0.5 + p.z));
    m[1] = static_cast<float>((static_cast<double>(b) + u.y) + p.z);
    m[2] = static_cast<float>((static_cast<double>(u.z) + u.field_024) + p.z);
}




void Weapon_Update(GameState& s, std::uint8_t player, Unit& u, std::size_t i) {
    const UnitType& t = Unit_Type(s, player, u);
    const ShipWeaponType& w = t.ship.weapons[i];
    Unit::ShipGun& g = u.guns[i];
    if (TurnTurret(w, g)) return;
    if (g.reload != 0) {
        if (!Unit_ReloadHeld(s, player, t)) --g.reload;  // Plus, E1: an unpaid ship reloads at half speed
        return;
    }
    if (w.kind == 7) {
        if (u.player_house_attack != 0xff) return;
        if (u.player_unit_attack != 0xff) {
            const Unit* tg = AttackTarget(s, u);
            if (tg == nullptr || Unit_Type(s, u.attack_ref.player, *tg).is_ship == 0) return;
        }
        if (u.work == 8) {
            const Cell c = WorldToCell(u.field_0d4, u.field_0d8);
            if (!Obstacle_UnderCell(s.map, c.x, c.y)) return;
        }
    }
    float x = 0, y = 0;
    if (u.player_unit_attack != 0xff) {
        const Unit* tg = AttackTarget(s, u);
        if (tg == nullptr) {
            Unported(s, orig::kWeaponUpdate);  // a target player without a target
            return;
        }
        x = tg->x, y = tg->y;
    } else if (u.player_house_attack != 0xff) {
        const House* h = HouseTarget(s, u);
        if (h == nullptr) {
            Unported(s, orig::kWeaponUpdate);
            return;
        }
        x = h->x, y = h->y;
    } else if (u.work == 8) {
        x = u.field_0d4, y = u.field_0d8;
    } else {
        return;  // (the weapon's own targets would be cleared: never set for ships)
    }
    if (w.slot == 0) {
        if (!AimFixed(t.ship, w, g, u, x, y)) return;
    } else {
        float ang = 0;
        if (!AimTurret(t.ship, w, g, u, x, y, ang)) {
            g.target = static_cast<float>(static_cast<double>(ang) - u.angle);
            WrapAngleF(g.target);
            SnapTurret(w, g);
            return;
        }
    }
    if (g.reload != 0) return;
    for (std::uint8_t b = 0; b < w.barrels; ++b) {
        float m[3];
        ShipMuzzle(t.ship, w, g, u, m);
        Weapon_FireShip(s, player, u, w, m, static_cast<std::uint8_t>(i), b, g.angle);
    }
    g.reload = w.reload;
    if (g.reload == 0) {
        g.reload = t.reload[player];
        if (w.slot == 0) g.reload = static_cast<std::uint16_t>(g.reload >> 1);
    }
}

}  // namespace


void Ship_UpdateWeapons(GameState& s, std::uint8_t player, Unit& u) {
    const std::uint8_t n = u.weapon_count;
    for (std::uint8_t i = 0; i < n; ++i) {
        if (i >= u.guns.size() || i >= Unit_Type(s, player, u).ship.weapons.size()) {
            Unported(s, orig::kUpdateWeapons);  // a weapon count without weapon data
            return;
        }
        Weapon_Update(s, player, u, i);
    }
}


void Ship_OnAttackedBy(GameState& s, std::uint8_t player, Unit& u, std::uint8_t tp, Unit& t) {
    if (Unit_Type(s, tp, t).is_air != 0) return;
    if (u.field_154 != 0) return;
    TUnit_OrderMove(s, player, u, t.x, t.y, 1);
    u.has_attack_unit = true;
    u.attack_ref = UnitRef{tp, t.id, t.serial};
    u.player_unit_attack = tp;
    u.player_house_attack = 0xff, u.has_house_attack = false;
}


void Ship_AttackHouse(GameState& s, std::uint8_t player, Unit& u, const House& h) {
    u.work = 0xff;
    u.final_x = h.x, u.final_y = h.y;
    TUnit_OrderMove(s, player, u, h.x, h.y, 1);
}



bool Ship_FindTarget(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);

    std::int32_t bp = static_cast<std::uint8_t>(t.field_588);
    if (s.fog_553ae0 == 0) bp = static_cast<std::int16_t>((static_cast<std::int32_t>(t.range[player]) - 200) / 96);
    const auto ry = static_cast<std::int16_t>(2 * bp);
    std::vector<std::pair<std::uint8_t, Unit*>> list;
    for (const UnitRef& r : s.ships) {
        Unit* o = Unit_Find(s, r);
        if (o == nullptr) {
            Unported(s, orig::kShipFindTarget);  // a stale list entry
            continue;
        }
        if (o->is_deleted != 0 || o->field_108 == 0) continue;
        if (!IsEnemy(s, player, r.player)) continue;
        const std::int32_t dx = std::abs(static_cast<std::int16_t>(o->map_x) - static_cast<std::int16_t>(u.map_x));
        const std::int32_t dy = std::abs(static_cast<std::int16_t>(o->map_y) - static_cast<std::int16_t>(u.map_y));
        if (dx < bp && dy < ry) list.emplace_back(r.player, o);
    }
    if (list.empty()) return false;
    // the introsort path: its front element with ties is not decoded
    if (list.size() > 32) Unported(s, orig::kShipTargetSort);
    std::size_t first = 0;
    for (std::size_t i = 1; i < list.size(); ++i) {
        if (static_cast<double>(static_cast<float>(DistU(u, *list[i].second))) < DistU(u, *list[first].second)) {
            first = i;
        }
    }
    TUnit_OnAttackedBy(s, player, u, list[first].first, *list[first].second, 1);
    return true;
}




void Ship_Killed(GameState& s, std::uint8_t player, Unit& u, std::uint8_t killer) {
    (void)killer;
    const ShipGraphic& g = Unit_Type(s, player, u).ship;
    if (!g.has_text) return;

    const std::int32_t count = g.sink_count / 3, r = g.sink_count;  // r > 0 whenever count > 0
    std::vector<SimEvent::SinkArgs::Bubble> bubbles;
    for (std::int32_t i = 0; i < count; ++i) {
        SimEvent::SinkArgs::Bubble b;
        b.delay = static_cast<std::int16_t>(Crt_rand(s) % 10);
        for (;;) {
            const std::int32_t x = Crt_rand(s) % (2 * r) - r;
            const std::int32_t y = Crt_rand(s) % (2 * r) - r;
            if (std::sqrt(static_cast<double>(x * x + y * y)) < static_cast<double>(r)) {
                b.a = static_cast<std::int16_t>(x), b.b = static_cast<std::int16_t>(y >> 1);
                break;
            }
        }
        bubbles.push_back(b);
    }
    Event_ShipSink(s, player, u, std::move(bubbles));
}




bool Ship_MoveForDead(GameState& s, std::uint8_t player, std::uint32_t slot) {
    {
        const Unit& u = s.players[player].unit_slots[slot];
        if (u.field_108 != 0 && u.in_house) House_ProduceUnit(s, u.house_player, u.house_slot);
    }
    Unit& u = s.players[player].unit_slots[slot];
    const UnitType& t = Unit_Type(s, player, u);
    if (u.field_108 != 0) {
        const std::int32_t k = 0xffff - static_cast<std::int32_t>(u.field_0ca);
        if (k >= 2400) {
            u.field_108 = 0;
        } else if (u.field_139 != 1 || (u.work == 0xe && u.field_0ca > 1)) {
            const std::uint8_t o = u.orient & 7;
            if (u.field_131[o] == 0) {
                ShipGfx_Request(s, player, u, 2, o);
                u.field_131[o] = 1;
            }
        }
    }
    if (u.num_work != 0xf && u.num_work != 0x10 && u.num_work != 0x11) {
        u.num_work = 0xf;
        u.num_sprite = 0;
    }
    u.num_sprite = t.anim_speed[u.num_work] + u.num_sprite;
    if (!(static_cast<float>(t.anim_size[u.num_work]) <= u.num_sprite)) return false;
    u.field_0ca = static_cast<std::uint16_t>(u.field_0ca - 30);
    if (u.field_0ca > 62000 && u.field_0ca < 63000) {
        Cell_RemoveUnit(s, player, u);
        u.field_0ca = 61999;
    }
    if (u.field_0ca < 20001) {
        u.field_0ca = 0xb;
        Unit_ClearReferences(s, Unit_Ref(player, u));
        Ship_Deleted(s, player, u);
        if (u.in_cell_list) Cell_RemoveUnit(s, player, u);
        if (u.work == 3) Unported(s, orig::kDeleteUnit);     // leaving the house
        if (u.work == 0x14) Transport_GetOut(s, player, u);
        Unit_Delete(s, player, slot);
        return true;
    }
    u.num_sprite = u.num_sprite - t.anim_speed[u.num_work];
    return false;
}

namespace {



Unit* Ship_FindAt(GameState& s, float x, float y, std::uint8_t p, std::uint8_t* tp) {
    std::vector<std::pair<std::uint8_t, Unit*>> list;
    for (const UnitRef& r : s.ships) {
        Unit* o = Unit_Find(s, r);
        if (o == nullptr) {
            Unported(s, orig::kShipFindAt);  // a stale list entry
            continue;
        }
        if (o->is_deleted != 0) continue;
        const double dx = static_cast<double>(x) - o->x, dy = static_cast<double>(y) - o->y;
        if (std::sqrt(dx * dx + dy * dy) < 600.0 && (p == 0xff || IsEnemy(s, p, r.player))) {
            list.emplace_back(r.player, o);
        }
    }
    for (std::size_t i = list.size(); i-- > 0;) {
        if (!HasHull(s, list[i].first, *list[i].second, orig::kShipGfxFootprint)) continue;
        if (Quad_ContainsPoint(Ship_Footprint(s, list[i].first, *list[i].second), x, y)) {
            *tp = list[i].first;
            return list[i].second;
        }
    }
    return nullptr;
}

}  // namespace

namespace {




std::array<float, 2> Ship_HitPoint(const GameState& s, std::uint8_t player, const Unit& u, float px, float py) {
    const ShipGraphic& g = Unit_Type(s, player, u).ship;
    if (g.hull.size() < 2 || g.frames == 0) return {px, py};
    const double n = g.frames;
    const auto o = static_cast<std::int8_t>(u.orient);
    const double fa = ((static_cast<double>(u.angle) - static_cast<double>(o) * static_cast<double>(kPiOverFour)) * n) *
                      static_cast<double>(kFourOverPi);
    const std::int32_t f = static_cast<std::uint8_t>(Ftol(fa)) + static_cast<std::int32_t>(g.frames) * o;
    const auto a = static_cast<float>((static_cast<double>(f) / n + 1.0) * static_cast<double>(kPiOverFour));
    auto end = [&](const std::array<float, 3>& pt) {
        V3 v{pt[0], pt[1], pt[2]};
        Vec3_RotateZ(v, a);
        const auto x = static_cast<float>((static_cast<double>(v.x) - v.y) * static_cast<double>(0.8660254f) + u.x);
        const auto y = static_cast<float>(static_cast<double>(u.y) + -((static_cast<double>(v.y) + v.x) * 0.5 + v.z));
        return V3{x, y, 0.0f};
    };
    V3 c = end(g.hull[0]);
    const V3 b = end(g.hull[1]);
    const V3 d{b.x - c.x, b.y - c.y, 0.0f - c.z};
    const double len = Vec3_Length(d);
    const double inv = 1.0 / Vec3_Length(d);
    const auto step = static_cast<float>(len * static_cast<double>(0.05f));
    float best = 1e6f;
    std::array<float, 2> r{0, 0};
    for (int i = 0; i < 20; ++i) {
        const auto dist =
            static_cast<float>(std::sqrt((static_cast<double>(c.x) - px) * (static_cast<double>(c.x) - px) +
                                         (static_cast<double>(c.y) - py) * (static_cast<double>(c.y) - py)));
        if (dist < best) r = {c.x, c.y}, best = dist;
        c.x = static_cast<float>(static_cast<double>(d.x) * inv * step + c.x);
        c.y = static_cast<float>(static_cast<double>(d.y) * inv * step + c.y);
        c.z = static_cast<float>(static_cast<double>(d.z) * inv * step + c.z);
    }
    return r;
}

}  // namespace




bool Shell_HitShip(GameState& s, float x, float y, float z, Shell& v) {
    (void)z;
    std::uint8_t tp = 0;
    Unit* t = Ship_FindAt(s, x, y, 0xff, &tp);
    if (t == nullptr || t->field_108 == 0) return false;
    if (v.owner.player == tp && v.owner.serial == t->serial) return false;
    if (v.kind != 9 && !IsEnemy(s, v.player, tp)) return false;
    const double hp =
        static_cast<double>(t->hp) - static_cast<double>(v.damage) / static_cast<double>(s.players[tp].factor_9b32d8);
    t->hp = static_cast<float>(hp);
    if (hp < 0.0) OnUnitKilled(s, tp, *t, v.player);

    if (t->field_154 == 0 && t->player_unit_attack == 0xff && t->player_house_attack == 0xff && t->work != 8 &&
        t->field_006 != 0 && Unit_Type(s, tp, *t).anim_size[6] != 0) {
        Unported(s, orig::kShellHitShip);
    }
    v.cz = 55.0f;
    if (v.fragments != 0 || v.kind == 9) {
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 1);

        const std::array<float, 2> at = Ship_HitPoint(s, tp, *t, x, y);
        Event_Effect(s, 3, true, Ftol(at[0]), Ftol(at[1]), Ftol(v.cz), 8);
    }
    return true;
}

}  // namespace antanta::sim
