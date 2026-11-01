




#include <cmath>

#include "dmath.hpp"
#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

namespace {




constexpr std::uint8_t kOrientDir[8] = {5, 4, 8, 3, 6, 2, 7, 1};
constexpr std::uint8_t kOrient218[8] = {6, 7, 0, 5, 6, 7, 0, 5};
constexpr std::uint8_t kOrientBack[8] = {4, 5, 6, 7, 0, 1, 2, 3};

std::uint8_t OrientDir(std::uint8_t o) { return o < 8 ? kOrientDir[o] : 1; }
std::uint8_t Orient218(std::uint8_t o) { return o < 8 ? kOrient218[o] : 1; }
std::uint8_t OrientBack(std::uint8_t o) { return o < 8 ? kOrientBack[o] : 1; }


void SideDirs(std::uint8_t d, std::uint8_t& a, std::uint8_t& b) {
    switch (d) {
        case 1: a = 5, b = 7; break;
        case 2: a = 6, b = 7; break;
        case 3: a = 6, b = 8; break;
        case 4: a = 8, b = 5; break;
        case 5: a = 4, b = 1; break;
        case 6: a = 3, b = 2; break;
        case 7: a = 2, b = 1; break;
        case 8: a = 4, b = 3; break;
        default: break;
    }
}

bool SameCell(Cell a, std::int32_t x, std::int32_t y) { return a.x == x && a.y == y; }



bool NotBehind(const Unit& o, const Unit& u) {
    const Cell uc{u.map_x, u.map_y};
    if (o.map_x == u.map_x && o.map_y == u.map_y) return false;
    const std::uint8_t back = OrientDir(OrientBack(u.orient));
    if (SameCell(Cell_Step(uc, back), o.map_x, o.map_y)) return false;
    std::uint8_t a = 0, b = 0;
    SideDirs(back, a, b);
    if (SameCell(Cell_Step(uc, a), o.map_x, o.map_y)) return false;
    if (SameCell(Cell_Step(uc, b), o.map_x, o.map_y)) return false;
    return true;
}


bool Within100(float d) { return d <= 100.0f; }


double Length3(const float v[3]) {
    const double x = v[0], y = v[1], z = v[2];
    return std::sqrt((x * x + y * y) + z * z);
}



double AngleTo(const float a[3], const float b[3]) {
    const double dot =
        (static_cast<double>(b[1]) * a[1] + static_cast<double>(a[2]) * b[2]) + static_cast<double>(b[0]) * a[0];
    const double lb = std::sqrt((static_cast<double>(b[1]) * b[1] + static_cast<double>(b[2]) * b[2]) +
                                static_cast<double>(b[0]) * b[0]);
    const double la = std::sqrt((static_cast<double>(a[1]) * a[1] + static_cast<double>(a[2]) * a[2]) +
                                static_cast<double>(a[0]) * a[0]);
    double r = dmath::acos(dot / (lb * la));
    if (static_cast<double>(a[1]) * b[0] + static_cast<double>(b[1]) * a[0] < 0.0) {
        r = static_cast<double>(6.2831855f) - r;
    }
    return r;
}


void Rotate(const float v[3], float out[3], float angle) {
    const double sn = dmath::sin(static_cast<double>(angle)), cs = dmath::cos(static_cast<double>(angle));
    out[0] = static_cast<float>(cs * v[0] - sn * v[1]);
    out[1] = static_cast<float>(sn * v[0] + cs * v[1]);
    out[2] = v[2];
}


std::uint16_t CellObject(const GameState& s, Cell c) {
    if (c.x < 0 || c.y < 0 || c.x >= s.map.width || c.y >= s.map.height) return 0;
    return s.map.objects[static_cast<std::size_t>(c.y) * s.map.width + static_cast<std::size_t>(c.x)];
}



std::uint16_t CrushedObject(const GameState& s, std::uint16_t o, std::uint8_t k) {
    static constexpr std::uint16_t kTable[9] = {0x25b8, 0x25b5, 0x25b2, 0x25a9, 0x25a3,
                                                0x25a0, 0x25af, 0x25ac, 0x25a4};
    if (o > 0x2587 && o < 0x2591) return static_cast<std::uint16_t>(kTable[o - 0x2588] + k);
    if (o == 0x2587) return static_cast<std::uint16_t>(0x259d + k);
    if (o == 0x2584) return static_cast<std::uint16_t>(0x259a + k);
    if (o == 0x2596) return static_cast<std::uint16_t>(0x2597 + k);
    if (o > 0x2596 && o < 0x25bb) {
        const int r = (o - 0x2597) % 3;
        if (r != 0 && k == 0) return static_cast<std::uint16_t>(o - r);
        return o;
    }
    if (o < 0x25de) {
        const std::size_t i = static_cast<std::size_t>(o) - 0x2580u;
        const std::vector<std::uint8_t>& t = s.object_blocking[2];
        if (i >= t.size()) return o;  // past the table: the original reads past its end
        if (t[i] == 2) return 0;
        if (t[i] == 3) return 0x251c;
    }
    return o;
}




void CrushObjects(GameState& s, const Unit& u) {
    const Cell c{u.map_x, u.map_y};
    const std::uint16_t o = CellObject(s, c);
    if (o >= 0x25de && o <= 0x25e3) {
        Terrain_SetObject(s, c.x, c.y, 0);
        return;
    }
    if (o < 9000) return;
    if (o < 0x251c) {
        const std::size_t i = static_cast<std::size_t>(o) - 9000u;
        const std::vector<std::uint8_t>& t = s.object_blocking[0];
        if (i >= t.size()) {
            // a tree record objects.les does not have: the original reads past the table
            Unported(s, orig::kVehicleCrushObjects);
            return;
        }
        if (t[i] < 2) return;
        Terrain_SetObject(s, c.x, c.y, 0);
        return;
    }
    if (o < 0x2580 || o >= 0x25e4) return;
    const std::uint16_t n0 = CrushedObject(s, o, 0);
    if (n0 != o) Terrain_SetObject(s, c.x, c.y, n0);
    const Cell c1 = Cell_Step(c, OrientDir(Orient218(u.orient)));
    const std::uint16_t o1 = CellObject(s, c1);
    if (o1 >= 0x2580 && o1 < 0x25e4) {
        const std::uint16_t n1 = CrushedObject(s, o1, 1);
        if (n1 != o1) Terrain_SetObject(s, c1.x, c1.y, n1);
    }
    const Cell c2 = Cell_Step(c, OrientDir(OrientBack(Orient218(u.orient))));
    const std::uint16_t o2 = CellObject(s, c2);
    if (o2 < 0x2580 || o2 >= 0x25e4) return;
    const std::uint16_t n2 = CrushedObject(s, o2, 2);
    if (n2 != o2) Terrain_SetObject(s, c2.x, c2.y, n2);
}



bool LargeCellsInside(const Map& m, std::int32_t x, std::int32_t y) {
    return x >= 2 && y >= 3 && y < m.height - 3 && x < m.width - 2;
}



void LargeCells_Clear(GameState& s, std::int32_t x, std::int32_t y) {
    Map& m = s.map;
    if (!LargeCellsInside(m, x, y)) return;
    const std::int64_t w = m.width, i = static_cast<std::int64_t>(y) * w + x;
    auto clr = [&m](std::int64_t k) { m.flags[static_cast<std::size_t>(k)] &= 0xfd; };
    clr(i), clr(i + 1), clr(i - 1), clr(i - 2 * w), clr(i + 2 * w);
    for (const std::uint8_t d : {std::uint8_t{4}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}}) {
        const Cell n = Cell_Step(Cell{x, y}, d);
        clr(static_cast<std::int64_t>(n.y) * w + n.x);
    }
    const std::uint32_t gen = m.pass_gen;  // only the large-unit marks changed, which the route fields leave out
    PathGrid_UpdateRegion(s, x, y, 3);
    m.pass_gen = gen;
}



void LargeCells_Set(GameState& s, std::int32_t x, std::int32_t y) {
    Map& m = s.map;
    if (!LargeCellsInside(m, x, y)) return;
    const std::int64_t w = m.width, i = static_cast<std::int64_t>(y) * w + x;
    auto set = [&m](std::int64_t k, bool always) {
        if (always || m.pass_large[static_cast<std::size_t>(k)] == 0) m.flags[static_cast<std::size_t>(k)] |= 2;
    };
    set(i, false), set(i + 1, false), set(i - 1, false), set(i - 2 * w, false), set(i + 2 * w, true);
    for (const std::uint8_t d : {std::uint8_t{4}, std::uint8_t{1}, std::uint8_t{2}, std::uint8_t{3}}) {
        const Cell n = Cell_Step(Cell{x, y}, d);
        set(static_cast<std::int64_t>(n.y) * w + n.x, false);
    }
    const std::uint32_t gen = m.pass_gen;  // only the large-unit marks changed, which the route fields leave out
    PathGrid_UpdateRegion(s, x, y, 3);
    m.pass_gen = gen;
}


bool NearWaypoint(const GameState& s, std::uint8_t player, const Unit& u) {
    const double dx = static_cast<double>(u.x) - u.dest_x, dy = static_cast<double>(u.y) - u.dest_y;
    return std::sqrt(dy * dy + dx * dx) < Unit_Type(s, player, u).waypoint_radius[player];
}



double DistTo(const Unit& u, float px, float py) {
    const double dx = static_cast<double>(u.x) - px, dy = static_cast<double>(u.y) - py;
    return std::sqrt(dy * dy + dx * dx);
}


double UnitDist(const Unit& a, const Unit& b) {
    const double dx = static_cast<double>(a.x) - b.x, dy = static_cast<double>(a.y) - b.y;
    return std::sqrt(dx * dx + dy * dy);
}



std::uint8_t OrientToPoint(float x1, float y1, float x2, float y2) {
    const double dx = static_cast<double>(x2) - x1;
    const double dy = static_cast<double>(y2) - y1;
    float sl = 0;
    if (dx > 0.0) {
        sl = static_cast<float>(dy / dx);
    } else if (dx < 0.0) {
        sl = static_cast<float>(dy / (static_cast<double>(x1) - x2));
    } else {
        sl = static_cast<float>(dy * static_cast<double>(10000.0f));
    }
    const bool pos = !(dx < 0.0);
    if (sl < -2.41) return 0;
    if (sl < -0.41) return pos ? 7 : 1;
    if (sl < 0.42) return pos ? 6 : 2;
    if (sl < 2.42) return pos ? 5 : 3;
    return 4;
}


void StopAndHold(GameState& s, std::uint8_t player, Unit& u) {
    TUnit_StopMoving(s, player, u);
    u.is_stopped = 1;
}


void SetCellIfMoved(GameState& s, std::uint8_t player, Unit& u) {
    const Cell c = WorldToCell(u.x, u.y);
    if (c.y != u.map_y || c.x != u.map_x) TUnit_SetCell(s, player, u, c);
}





void MgPickTarget(GameState& s, std::uint8_t player, Unit& u, const UnitType& t) {
    if (u.field_006 == 0 && u.field_142 == 0) return;
    const auto rx =
        static_cast<std::int16_t>(Ftol(static_cast<double>(t.field_5dc) * static_cast<double>(0.010416667f)));
    const auto ry =
        static_cast<std::int16_t>(Ftol(static_cast<double>(t.field_5dc) * static_cast<double>(0.020833334f)));
    const std::int32_t w = s.map.width, wh = s.map.width * s.map.height;
    float lim = t.field_5dc;
    UnitRef best[3];
    std::uint8_t cnt = 0;
    for (std::int32_t xo = -rx; xo <= rx; ++xo) {
        for (std::int32_t yo = -ry; yo <= ry; ++yo) {
            const std::int32_t c = (u.map_y - yo) * w + u.map_x - xo;
            if (c < 0 || c >= wh) break;
            const std::vector<UnitRef>& list = s.map.cell_units[static_cast<std::size_t>(c)];
            for (std::size_t k = 0; k < 40 && k < list.size(); ++k) {
                const UnitRef ref = list[k];
                Unit* v = Unit_Find(s, ref);
                if (v == nullptr) {
                    Unported(s, orig::kMgPickTarget);  // a stale cell entry
                    continue;
                }
                if (v->is_deleted != 0 || v->work == 3 || v->work == 0x14 || !IsEnemy(s, player, ref.player)) continue;
                const double d = UnitDist(*v, u);
                const auto df = static_cast<float>(d);
                if (!(d < static_cast<double>(lim))) continue;
                const UnitType& vt = Unit_Type(s, ref.player, *v);
                if (vt.is_air != 0 || vt.is_ship != 0) continue;
                if (vt.move_class == 3 && v->type != 0x17) continue;
                if (cnt < 3) {
                    best[cnt++] = ref;
                    continue;
                }
                for (int j = 0; j < 3; ++j) {
                    const Unit* bj = Unit_Find(s, best[j]);
                    if (bj == nullptr) {
                        Unported(s, orig::kMgPickTarget);
                        return;
                    }
                    if (UnitDist(*bj, u) > static_cast<double>(df)) {  // test ah, 0x41: unordered keeps it
                        for (int m = 2; m > j; --m) best[m] = best[m - 1];
                        best[j] = ref;
                    }
                }
                const Unit* b2 = Unit_Find(s, best[2]);
                if (b2 == nullptr) {
                    Unported(s, orig::kMgPickTarget);
                    return;
                }
                lim = static_cast<float>(UnitDist(*b2, u));
            }
        }
    }
    if (cnt == 0) {
        u.has_mg_target = false;
        return;
    }
    u.mg_target = best[Rng_Next(s) % cnt];
    u.has_mg_target = true;
}





void MgBurstVehicle(GameState& s, std::uint8_t ap, Unit& a, std::uint8_t tp, Unit& t) {
    if (!(t.field_024 <= 1.0f)) {
        RifleShot(s, ap, a, tp, t);
        return;
    }
    Event_UnitSound(s, ap, a, 3, 4);
    if (t.in_group) {
        if (Group* g = Unit_Group(s, tp, t)) {
            Group_Alert(s, *g, tp, &t);
        } else {
            Unported(s, orig::kGroupAlert);
        }
    } else if (t.player_unit_attack == 0xff && t.player_house_attack == 0xff) {
        AlertNearby(s, ap, t.map_x, t.map_y, ap, a);
    }
    const UnitType& at = Unit_Type(s, ap, a);
    const auto r = static_cast<std::uint8_t>(Rng_Next(s) % 101);
    const double acc = static_cast<double>(at.accuracy[ap]) * static_cast<double>(s.players[ap].accuracy_factor);
    if (acc < static_cast<double>(r)) return;
    const auto dxf = static_cast<float>(static_cast<double>(t.x) - a.x);
    const auto dyf = static_cast<float>(static_cast<double>(t.y) - a.y);
    const std::int32_t k = Ftol(UnitDist(a, t)) / 40 + 1;
    const float exp0 = a.field_08c;
    const double inv = static_cast<double>(1.0f) / static_cast<double>(k);
    const auto stepx = static_cast<float>(static_cast<double>(dxf) * inv);
    const double stepy_d = inv * static_cast<double>(dyf);
    const auto stepy = static_cast<float>(stepy_d);
    const auto len = static_cast<float>(
        std::sqrt(stepy_d * static_cast<double>(stepy) + static_cast<double>(stepx) * static_cast<double>(stepx)));
    const auto tanv = static_cast<float>(dmath::tan(0.39269909262657166));
    float acc_len = 0.0f, cx = a.x, cy = a.y;
    for (std::int32_t i = 0; i < k; ++i) {
        acc_len = static_cast<float>(static_cast<double>(acc_len) + len);
        cx = static_cast<float>(static_cast<double>(cx) + stepx);
        cy = static_cast<float>(static_cast<double>(cy) + stepy);
        const auto h = static_cast<float>(static_cast<double>(tanv) * acc_len + static_cast<double>(10.0f));
        BulletToPoint(s, cx, cy, ap, a, i > k / 2, h);
    }
    a.field_08c = static_cast<float>((static_cast<double>(a.field_08c) - exp0) * static_cast<double>(0.1f) + exp0);
}




void MachineGuns(GameState& s, std::uint8_t player, Unit& u, const UnitType& t) {
    if (t.field_5d8 == 0) return;
    if (!u.has_mg_target) {
        if (u.mg_delay != 0) {
            --u.mg_delay;
            return;
        }
        MgPickTarget(s, player, u, t);
        if (!u.has_mg_target) {
            u.mg_delay = 30;
            return;
        }
    }
    for (std::uint8_t g = 0; g < t.field_5d8; ++g) {
        if (Rng_Next(s) < 25000) continue;
        Unit* tg = Unit_Find(s, u.mg_target);
        if (tg == nullptr) {
            // a target deleted and its slot reused: the original keeps a stale pointer
            Unported(s, orig::kVehicleMachineGuns);
            u.has_mg_target = false;
            return;
        }
        MgBurstVehicle(s, player, u, u.mg_target.player, *tg);
        if (tg->is_deleted != 0) u.has_mg_target = false;
        MgPickTarget(s, player, u, t);
        if (!u.has_mg_target) break;
    }
}



void VehicleArrive(GameState& s, std::uint8_t player, Unit& u) {
    u.field_0f8 = 0;
    SetCellIfMoved(s, player, u);
    const UnitType& t = Unit_Type(s, player, u);
    if (u.player_unit_attack != 0xff) {
        Unit* tg = AttackTarget(s, u);
        if (tg == nullptr) {
            Unported(s, orig::kVehicleArrive);  // a target player without a target: the original reads a null pointer
            return;
        }
        if (static_cast<double>(t.range[player]) > UnitDist(u, *tg) && t.anim_size[6] != 0) {
            StopAndHold(s, player, u);
            TUnit_OnAttackedBy(s, player, u, u.attack_ref.player, *tg, 1);
            return;
        }
    } else if (u.work == 8) {
        if (!(static_cast<double>(t.range[player]) < DistTo(u, u.field_0d4, u.field_0d8))) {
            TUnit_ClearPath(u);
            FaceToPoint(s, player, u, u.field_0d4, u.field_0d8);
            return;
        }
    }
    EventUnitEndMove(s, player, u);
}





void VehiclePathStep(GameState& s, std::uint8_t player, Unit& u) {
    SetCellIfMoved(s, player, u);
    const UnitType& t = Unit_Type(s, player, u);
    if (u.player_unit_attack != 0xff) {
        Unit* tg = AttackTarget(s, u);
        if (tg == nullptr) {
            Unported(s, orig::kVehiclePathStep);  // a target player without a target: the original reads a null pointer
            return;
        }
        if (static_cast<double>(t.range[player]) > UnitDist(u, *tg) && t.anim_size[6] != 0) {
            TUnit_ClearPath(u);
            StopAndHold(s, player, u);
            TUnit_OnAttackedBy(s, player, u, u.attack_ref.player, *tg, 1);
            return;
        }
    } else if (u.player_house_attack != 0xff) {
        const House* h = HouseTarget(s, u);
        if (h == nullptr) {
            Unported(s, orig::kVehiclePathStep);  // a stale house pointer
            return;
        }
        if ((Rng_Next(s) & 1) != 0) {
            TUnit_AttackHouse(s, player, u, u, u.player_house_attack, u.house_attack_slot, 0, 1);
            return;
        }
        if (h->is_destroyed != 0) {
            TUnit_AttackEnd(s, player, u);
            return;
        }
    } else if (u.work == 8) {
        if (!(static_cast<double>(t.range[player]) < DistTo(u, u.field_0d4, u.field_0d8))) {
            TUnit_ClearPath(u);
            StartFireAnim(s, player, u);
            FaceToPoint(s, player, u, u.field_0d4, u.field_0d8);
            return;
        }
    }
    if (u.path_index < u.path.size()) {
        const std::uint8_t dir = u.path[u.path_index];
        if (!TUnit_CanStepDir(s, player, u, dir)) {
            TUnit_Repath(s, player, u);
            return;
        }
        u.dest_x = static_cast<float>(kDirWorld[dir][0]) + u.dest_x;
        u.dest_y = static_cast<float>(kDirWorld[dir][1]) + u.dest_y;
        TUnit_ComputeVelocity(s, player, u);
        TUnit_SetMoveAnimation(s, player, u);
        ++u.path_index;
        u.x = u.vel_x + u.x;
        u.y = u.vel_y + u.y;
        return;
    }
    TUnit_ClearPath(u);
    VehicleArrive(s, player, u);
}

}  // namespace


std::uint8_t Orient_ToPoint(float x1, float y1, float x2, float y2) { return OrientToPoint(x1, y1, x2, y2); }



void LargeCells_Move(GameState& s, const Unit& u, Cell c) {
    s.large_cells_clear.push_back({u.map_x, u.map_y});
    s.large_cells_set.push_back({c.x, c.y});
}


void LargeCells_Leave(GameState& s, const Unit& u) { s.large_cells_clear.push_back({u.map_x, u.map_y}); }



void LargeCells_Flush(GameState& s) {
    const std::vector<std::array<std::int32_t, 2>> clear = std::move(s.large_cells_clear);
    const std::vector<std::array<std::int32_t, 2>> set = std::move(s.large_cells_set);
    s.large_cells_clear.clear();
    s.large_cells_set.clear();
    for (const auto& c : clear) LargeCells_Clear(s, c[0], c[1]);
    for (const auto& c : set) LargeCells_Set(s, c[0], c[1]);
}





void TankRedirectAllArround(GameState& s, std::uint8_t player, Unit& u) {
    if (u.num_work == 6 || u.work == 9) return;
    if ((u.num_work == 3 || u.num_work == 4) && Rng_Next(s) < 25000) return;  // idle: three ticks in four
    const UnitType& t = Unit_Type(s, player, u);
    // near: within 300 of Final (dx on the x87, dy stored as float).
    bool near = false;
    {
        const float dyf = static_cast<float>(static_cast<double>(u.y) - u.final_y);
        const double dx = static_cast<double>(u.x) - u.final_x;
        near = std::sqrt(dx * dx + static_cast<double>(dyf) * dyf) < 300.0;
    }
    const std::uint8_t front = OrientDir(u.orient);
    std::uint8_t side_a = 1, side_b = 1;
    SideDirs(front, side_a, side_b);
    TUnit_UpdateCellIfMoved(s, player, u);

    const auto size = static_cast<std::int32_t>(t.size);
    const auto s1 = static_cast<float>(static_cast<double>(2.5f) * size);  // reach for pushing aside
    const auto s2 = static_cast<float>(size * static_cast<double>(1.5f));  // reach for running over
    float v[3] = {static_cast<float>(kDirWorld[OrientDir(u.orient)][0]),
                  static_cast<float>(kDirWorld[OrientDir(u.orient)][1]), 0.0f};
    {
        const double inv = static_cast<double>(1.0f) / Length3(v);
        const double vx = v[0] * inv;
        v[1] = static_cast<float>(inv * v[1]);
        v[2] = static_cast<float>(v[2] * inv);
        const double sx = s1 * vx;
        const double sy = static_cast<double>(s1) * v[1];
        const auto sz = static_cast<float>(static_cast<double>(v[2]) * s1);
        v[0] = static_cast<float>(sx + sx);
        v[1] = static_cast<float>(sy + sy);
        v[2] = static_cast<float>(static_cast<double>(sz) + sz);
    }
    const std::int32_t w = s.map.width, h = s.map.height;
    Cell row{u.map_x, u.map_y + 4};
    for (int i = 0; i < 5; ++i) {
        Cell c = row;
        for (int j = 0; j < 5; ++j) {
            if (c.x >= 0 && c.y >= 0 && c.x < w && c.y < h) {
                const std::size_t idx = static_cast<std::size_t>(c.y) * w + static_cast<std::size_t>(c.x);
                for (std::size_t k = 0; k < 40 && k < s.map.cell_units[idx].size();) {
                    const UnitRef ref = s.map.cell_units[idx][k];
                    Unit* op = Unit_Find(s, ref);
                    if (op == nullptr) {
                        Unported(s, orig::kTankRedirect);  // a stale cell entry
                        ++k;
                        continue;
                    }
                    Unit& o = *op;
                    if (&o == &u || o.type == 0x17) {
                        ++k;
                        continue;
                    }
                    const UnitType& ot = Unit_Type(s, ref.player, o);
                    const bool idle = o.num_work == 3 || o.num_work == 4;
                    const double ddx = static_cast<double>(o.x) - u.x, ddy = static_cast<double>(o.y) - u.y;
                    const float d[3] = {static_cast<float>(ddx), static_cast<float>(ddy), 0.0f};
                    const bool right = ddx * v[1] - ddy * v[0] > 0.0;
                    const double ex = static_cast<double>(u.x) - o.x, ey = static_cast<double>(u.y) - o.y;
                    const auto dist = static_cast<float>(std::sqrt(ex * ex + ey * ey));
                    const double ang = AngleTo(v, d);
                    // An idle unit behind u (pi/2 < angle < 3pi/2 from the heading) is left alone.
                    if (idle && ang > static_cast<double>(1.5707964f) && ang < static_cast<double>(4.712389f)) {
                        ++k;
                        continue;
                    }
                    const std::uint8_t odir = OrientDir(o.orient);
                    const bool olarge = ot.field_468 != 0;

                    const bool boarding_u = o.work == 0x15 && o.has_future_transporter &&
                                            o.future_transporter.player == player &&
                                            o.future_transporter.serial == u.serial;
                    // Run over: a small enemy ground unit, not boarding u, alive, not in a house or transport, in
                    // u's cell or within 1.5 * Size.
                    if (!olarge && IsEnemy(s, player, ref.player) && !boarding_u && o.field_024 < 1.0f &&
                        o.is_deleted == 0 && o.work != 3 && o.work != 0x14 &&
                        ((o.map_x == u.map_x && o.map_y == u.map_y) || dist <= s2)) {
                        o.hp = -100.0f;
                        OnUnitKilled(s, ref.player, o, player);
                        const std::vector<UnitRef>& list = s.map.cell_units[idx];
                        if (k >= list.size()) {
                            Unported(s, orig::kTankRedirect);
                            break;
                        }
                        ++k;
                        continue;
                    }
                    if (u.is_stopped != 0 || !(dist <= s1)) {
                        ++k;
                        continue;
                    }
                    const bool ok = olarge ? NotBehind(o, u) : true;  // bl
                    if (o.work == 9 || boarding_u || o.is_deleted != 0 || o.work == 3 || o.work == 0x14 ||
                        !(o.field_024 < 1.0f)) {
                        ++k;
                        continue;
                    }
                    if (o.field_09f != 0 || !ok) {
                        // L_e90: a large unit close ahead that is already giving way: u goes on now and then.
                        if (olarge && Within100(dist) && ok && !near && Rng_Next(s) <= 3000) {
                            TUnit_OrderMove(s, player, u, u.final_x, u.final_y, 1);
                        }
                        ++k;
                        continue;
                    }
                    bool wait = false;
                    if (olarge && Within100(dist)) {
                        const bool far = static_cast<float>(ot.size) < dist;
                        const bool facing = odir == front || odir == side_a || odir == side_b;
                        wait = (far && o.field_07c == 0) || (facing && !idle);
                    }
                    if (wait) {
                        if (near) {
                            TUnit_OrderEnd(s, player, u, 1);
                        } else if (Rng_Next(s) <= 3000) {
                            TUnit_OrderMove(s, player, u, u.final_x, u.final_y, 1);
                        }
                        ++k;
                        continue;
                    }
                    if (olarge && near) {
                        ++k;
                        continue;
                    }
                    // Push o aside: a point 90 degrees off u's heading (to o's side), from o.
                    o.field_09f = 1;
                    float p[3] = {0, 0, 0};
                    Rotate(v, p, right ? -1.5707964f : 1.5707964f);
                    std::int32_t tx = Ftol(static_cast<double>(p[0]) + o.x);
                    std::int32_t ty = Ftol(static_cast<double>(p[1]) + o.y);
                    const Cell tc = WorldToCell(static_cast<float>(tx), static_cast<float>(ty));
                    if (!TUnit_CanEnterCell(s, ref.player, o, tc.y * w + tc.x)) {
                        // RelocateTargetLand's cell is returned in edx:eax and dropped: only its RNG reads
                        // count; the unit is sent to the centre of the blocked cell.
                        (void)RelocateTargetLand(s, tc);
                        tx = (tc.x * 2 - (~tc.y & 1)) * 48 + 0x30;
                        ty = tc.y * 24;
                    }

                    (void)Rng_Next(s);
                    Event_Horn(s, player, u);
                    if (o.field_0bc == 0) {
                        const double fx = static_cast<double>(o.x) - o.final_x,
                                     fy = static_cast<double>(o.y) - o.final_y;
                        if (std::sqrt(fy * fy + fx * fx) > 40.0) {
                            o.waypoints[0] = Unit::Waypoint{o.final_x, o.final_y, o.work, o.field_0ca};
                            o.field_0bc = 1;
                        }
                    }
                    TUnit_OrderMove(s, ref.player, o, static_cast<float>(tx), static_cast<float>(ty), 1);
                    if (Unit_Type(s, ref.player, o).field_468 != 0) {  // a large unit gives way: u waits 10 ticks
                        u.is_stopped = 1;
                        u.field_07c = 10;
                        if (u.num_work == 5) {
                            u.field_078 = 3;
                        } else {
                            u.num_work = 3;
                        }
                        u.num_sprite = 0;
                    }
                    ++k;
                }
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    CrushObjects(s, u);
}



void UnitActionVehicle(GameState& s, std::uint8_t player, Unit& u) {
    if (!u.path.empty()) {
        if (NearWaypoint(s, player, u)) {
            VehiclePathStep(s, player, u);
            TUnit_UpdateCellIfMoved(s, player, u);
            return;
        }
    } else if (NearWaypoint(s, player, u)) {
        VehicleArrive(s, player, u);
        TUnit_UpdateCellIfMoved(s, player, u);
        return;
    }
    u.x = u.vel_x + u.x;
    u.y = u.vel_y + u.y;
    TUnit_UpdateCellIfMoved(s, player, u);
}





void MakeMoveVehicle(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    MachineGuns(s, player, u, t);
    // Turn first: away from the waypoint (and with a path or while driving), a vehicle not facing it turns.
    if (u.num_work != 5 && u.num_work != 6 && DistTo(u, u.dest_x, u.dest_y) > t.waypoint_radius[player] &&
        (!u.path.empty() || u.num_work == 0)) {
        const std::uint8_t o = OrientToPoint(u.x, u.y, u.dest_x, u.dest_y);
        u.target_orient = o;
        if (o != u.orient) {
            u.field_078 = u.num_work;
            u.num_work = 5;
            u.num_sprite = 0;
            return;
        }
    }
    if ((!u.path.empty() || DistTo(u, u.dest_x, u.dest_y) > t.waypoint_radius[player]) && u.field_07c == 0) {
        u.is_stopped = 0;
    }
    if (u.num_work == 0) Event_UnitDust(s, player, u, 0);
    if (u.num_work == 3) {  // standing: an attack order resumes; the original returns without ResyncCell
        u.field_09f = 0;
        if (u.player_unit_attack != 0xff) {
            Unit* tg = AttackTarget(s, u);
            if (tg != nullptr && tg->is_deleted == 0) {
                TUnit_OnAttackedBy(s, player, u, u.attack_ref.player, *tg, 1);
            } else {
                TUnit_AttackEnd(s, player, u);
            }
            return;
        }
        if (u.player_house_attack != 0xff) {
            const House* h = HouseTarget(s, u);
            if (h != nullptr && h->is_destroyed == 0) {
                TUnit_AttackHouse(s, player, u, u, u.player_house_attack, u.house_attack_slot, 0, 1);
            } else {
                TUnit_AttackEnd(s, player, u);
            }
            return;
        }
    }
    if (TUnit_ActionGate(s, player, u)) {
        TUnit_ResyncCell(s, player, u);
        return;
    }
    if (u.num_work == 4) {
        if (s.short_anims != 0) {
            u.num_work = 3;
        } else {
            const double half = static_cast<double>(t.anim_size[4]) * static_cast<double>(0.5f);
            if (!(half < u.num_sprite) && static_cast<double>(t.anim_speed[4]) + u.num_sprite > half) {
                u.num_work = 3;
                u.num_sprite = 0.001f;
            }
        }
    }
    if (u.num_work == 0) {
        const std::uint16_t o = CellObject(s, Cell{u.map_x, u.map_y});
        if (o > 9000 && o < 0x251c) Event_TreeLeaves(s, u.map_x, u.map_y, static_cast<std::uint16_t>(o - 9000));
    }
    if (u.num_work == 6) {  // firing: a standing house target or a unit target in range keeps it
        const House* h = u.player_house_attack != 0xff ? HouseTarget(s, u) : nullptr;
        if (h == nullptr || h->is_destroyed != 0) {
            Unit* tg = u.player_unit_attack != 0xff ? AttackTarget(s, u) : nullptr;
            if (tg != nullptr && tg->is_deleted == 0) {
                if (static_cast<double>(t.range[player]) < UnitDist(u, *tg)) {  // out of range
                    if (u.field_116 == 4) {
                        TUnit_AttackEnd(s, player, u);
                    } else {
                        TUnit_OnAttackedBy(s, player, u, u.attack_ref.player, *tg, 1);
                    }
                    TUnit_ResyncCell(s, player, u);
                    return;
                }
            } else if (u.work == 8) {
                TUnit_ClearTargets(u);
            } else {
                TUnit_AttackEnd(s, player, u);
            }
        }
    }
    if (u.field_0ee != 0) {
        if (!Unit_ReloadHeld(s, player, t)) --u.field_0ee;  // Plus, E1: an unpaid unit reloads at half speed
        if (u.num_work == 6 || u.num_work == 2) {
            TUnit_ResyncCell(s, player, u);
            return;
        }
    }
    const std::uint8_t nw = u.num_work;
    const double sum = static_cast<double>(t.anim_speed[nw]) + u.num_sprite;  // fst: compared unrounded
    u.num_sprite = static_cast<float>(sum);
    if (!(sum < static_cast<double>(t.anim_size[nw]))) {
        u.num_sprite = 0.01f;
        switch (nw) {
            case 3:
                if (s.short_anims != 0) {
                    u.num_work = 3;
                    break;
                }
                if (Rng_Next(s) >= 100 || t.anim_size[4] == 0) break;
                u.num_work = 4;
                if (Rng_Next(s) < 0x4000) {
                    u.num_sprite = static_cast<float>(static_cast<double>(t.anim_size[4]) * static_cast<double>(0.5f) +
                                                      static_cast<double>(1.01f));
                } else {
                    u.num_sprite = 0;
                }
                break;
            case 4:
                if (s.short_anims != 0) {
                    u.num_work = 3;
                } else {
                    TUnit_StopMoving(s, player, u);
                }
                break;
            case 6:
                if (u.field_0ee != 0) break;
                if (u.work == 8) u.has_attack_unit = false;
                if (u.weapon_count != 0) Weapon_Fire(s, player, u);
                u.field_0ee = t.reload[player];
                break;
            case 0x12: u.num_work = 6; break;
            case 0x13: u.num_work = 0; break;
            default: break;
        }
    }
    TUnit_ResyncCell(s, player, u);
}

}  // namespace antanta::sim
