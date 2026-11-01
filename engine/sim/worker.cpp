



#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

namespace {

std::size_t CellIndex(const Map& m, Cell c) { return static_cast<std::size_t>(c.y) * m.width + c.x; }

bool InMap(const Map& m, Cell c) { return c.x >= 0 && c.y >= 0 && c.x < m.width && c.y < m.height; }

std::uint16_t ObjectAt(const Map& m, Cell c) { return InMap(m, c) ? m.objects[CellIndex(m, c)] : 0; }

bool IsField(std::uint16_t o) { return o >= 0x25de && o <= 0x25e3; }


bool CellFree(GameState& s, Cell c) {
    for (const UnitRef& r : s.map.cell_units[CellIndex(s.map, c)]) {
        const Unit* o = Unit_Find(s, r);
        if (o != nullptr && o->is_deleted == 0 && o->is_stopped != 0) return false;
    }
    return s.map.pass_land[CellIndex(s.map, c)] == 0;
}


bool AnyFreeNeighbour(const GameState& s, Cell c) {
    for (std::uint8_t d = 1; d <= 8; ++d) {
        const Cell n = Cell_Step(c, d);
        if (InMap(s.map, n) && s.map.pass_land[CellIndex(s.map, n)] == 0) return true;
    }
    return false;
}



bool Touches(const GameState& s, const Unit& u, std::uint16_t id, std::uint8_t n) {
    if (ObjectAt(s.map, Cell{u.map_x, u.map_y}) == id) return true;
    Cell row{u.map_x, u.map_y + 2 * n};
    for (int r = 0; r < 2 * n + 1; ++r) {
        Cell c = row;
        for (int k = 0; k < 2 * n + 1; ++k) {
            if (ObjectAt(s.map, c) == id) return true;
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    return false;
}

std::uint16_t HouseId(std::uint8_t player, std::uint16_t slot) {
    return static_cast<std::uint16_t>((player + 1) * 1000 + slot);
}

// I(MaxHP) > HP, as `fild; fcomp` compares it: the house still needs repair.
bool Damaged(const House& h) { return static_cast<double>(h.max_hp) > h.hp; }

}  // namespace

bool Unit_Touches(const GameState& s, const Unit& u, std::uint16_t id, std::uint8_t n) { return Touches(s, u, id, n); }


void Worker_GoToField(GameState& s, std::uint8_t player, Unit& u, float x, float y) {
    u.work = 10;
    u.field_0ca = 0;
    u.field_0bc = 0;
    TUnit_MoveTo(s, player, u, x, y, 0);
    u.field_086 = 0;
    u.field_093 = 0;
    u.player_unit_attack = 0xff;
    u.player_house_attack = 0xff;
}



void Worker_NextJob(GameState& s, std::uint8_t player, Unit& u) {
    if (u.work == 3) return;
    u.work = 0xff;
    if (Worker_JobSearch(s, player, u)) return;
    TUnit_StopMoving(s, player, u);
    u.is_stopped = 1;
    if (u.work != 3) u.work = 0xff;
}

namespace {


void GoToField(GameState& s, std::uint8_t player, Unit& u, Cell c) {
    const float x = static_cast<float>(static_cast<double>(static_cast<float>((c.x * 2 - (~c.y & 1)) * 48)) +
                                       static_cast<double>(45.0f));
    Worker_GoToField(s, player, u, x, static_cast<float>(c.y * 24));
}

// One cell of the ring scan: a free field cell, or a special object not yet built (a pontoon section) with a free
// neighbour. Our rules leave the sections to NearestSection (docs/bugs.md B95).
bool RingCheck(GameState& s, std::uint8_t player, Unit& u, Cell c) {
    const std::uint16_t o = ObjectAt(s.map, c);
    if (IsField(o) && CellFree(s, c)) {
        GoToField(s, player, u, c);
        return true;
    }
    if (s.original_rules && o >= 0x25e4 && o < 0x2af8) {
        const std::size_t k = o - 0x25e4u;
        const float amount = k < s.map.specials.size() ? s.map.specials[k].f_a : SpecialObject{}.f_a;
        if (!(amount < 0.0f) && AnyFreeNeighbour(s, c)) {
            Worker_SendToHouse(s, player, u, o, 0);  // the object id passed as the "house" (> 1000)
            return true;
        }
    }
    return false;
}

// Ours (docs/bugs.md B95): the nearest special object not yet built that the worker can reach. A breadth-first walk
// from the worker's cell over the cells free on g_PassLand (directions 1 to 8 in order, at most kReach steps, about
// the ring scan's reach); the first live section not yet built next to a reached cell is taken. A built section is
// free ground, so the workers build a line from the shore outward and never pick a section across the water.
bool NearestSection(GameState& s, std::uint8_t player, Unit& u) {
    constexpr std::int32_t kReach = 64, kW = 2 * kReach + 1, kH = 4 * kReach + 1;  // a step moves x 1, y 2 at most
    const Map& m = s.map;
    const Cell at{u.map_x, u.map_y};
    if (!InMap(m, at)) return false;
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(kW) * kH, 0);
    auto visit = [&](Cell c) {  // false: outside the window or seen before
        const std::int32_t wx = c.x - at.x + kReach, wy = c.y - at.y + 2 * kReach;
        if (wx < 0 || wy < 0 || wx >= kW || wy >= kH) return false;
        std::uint8_t& v = seen[static_cast<std::size_t>(wy) * kW + wx];
        if (v != 0) return false;
        v = 1;
        return true;
    };
    visit(at);
    std::vector<Cell> ring{at}, next;
    for (std::int32_t d = 0; d < kReach && !ring.empty(); ++d) {
        for (const Cell& c : ring) {
            for (std::uint8_t k = 1; k <= 8; ++k) {
                const Cell n = Cell_Step(c, k);
                if (!InMap(m, n) || !visit(n)) continue;
                const std::uint16_t o = ObjectAt(m, n);
                if (o >= 0x25e4 && o < 0x2af8) {
                    const std::size_t i = o - 0x25e4u;
                    if (i < m.specials.size() && m.specials[i].alive != 0 && !(m.specials[i].f_a < 0.0f)) {
                        Worker_SendToHouse(s, player, u, o, 0);  // the object id passed as the "house" (> 1000)
                        return true;
                    }
                }
                if (m.pass_land[CellIndex(m, n)] == 0) next.push_back(n);
            }
        }
        ring.swap(next);
        next.clear();
    }
    return false;
}

void Worker_JobDone(GameState& s, std::uint8_t player, Unit& u, const House& h);



void Worker_StartWork(GameState& s, std::uint8_t player, Unit& u, House& h) {
    if (h.is_destroyed != 0) {
        u.work = 0xff;
        Worker_JobDone(s, player, u, h);
        return;
    }
    if (!Touches(s, u, HouseId(player, h.num), 1) || !TUnit_IsSpotFree(s, player, u)) {
        u.work = 0xff;
        if (h.field_020 == 0 && !Damaged(h)) {
            Worker_JobDone(s, player, u, h);
            return;
        }
        const HouseFootprint& fp = House_Footprint(s, player, h);
        auto n = static_cast<std::uint16_t>((fp.b + fp.a) * 2 + 4);
        if (n == 0) n = 2;
        const auto spot = static_cast<std::uint16_t>(s.rng_table[s.rng_index] % n);
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 1);
        Worker_SendToHouse(s, player, u, u.field_0ca, spot);
        return;
    }
    u.num_work = 0xb;
    u.work = 1;
    u.num_sprite = 0;
    u.field_078 = 0xb;
    u.is_stopped = 1;
    u.field_0ca = h.num;
    TUnit_FaceTowards(s, player, u, static_cast<float>(Ftol(h.x)), static_cast<float>(Ftol(h.y)));
    if (u.target_orient == u.orient) u.field_078 = 100;
}



void Worker_StartWorkObject(GameState& s, std::uint8_t player, Unit& u) {
    const std::uint16_t id = u.field_0ca;
    const std::size_t k = static_cast<std::uint16_t>(id - 0x25e4);
    if (k >= s.map.specials.size()) {
        Unported(s, orig::kWorkerStartWork);  // an object id past the special list
        return;
    }
    const SpecialObject& r = s.map.specials[k];
    if (r.f_a < 0.0f) {
        Worker_NextJob(s, player, u);
        return;
    }
    if (!Touches(s, u, id, 1)) {
        u.work = 0xff;
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 1);  // Rng % 1
        Worker_SendToHouse(s, player, u, id, 0);
        return;
    }
    u.num_work = 0xb;
    u.work = 1;
    u.num_sprite = 0;
    u.field_078 = 0xb;
    u.is_stopped = 1;
    TUnit_FaceTowards(s, player, u, static_cast<float>((r.x * 2 - (~r.y & 1)) * 48), static_cast<float>(r.y * 24 - 10));
    if (u.target_orient == u.orient) u.field_078 = 100;
}



void Worker_JobDone(GameState& s, std::uint8_t player, Unit& u, const House& h) {
    if (h.is_destroyed == 0 && h.type >= 10 && h.type <= 12) {
        Worker_SendToHouse(s, player, u, h.num, 0);
        return;
    }
    Worker_NextJob(s, player, u);
}



bool FieldCellFree(GameState& s, const Unit* self, float x, float y, Cell c) {
    if (!InMap(s.map, c)) return false;
    const std::size_t i = CellIndex(s.map, c);
    for (const UnitRef& r : s.map.cell_units[i]) {
        const Unit* o = Unit_Find(s, r);
        if (o == nullptr || o->is_deleted != 0 || o == self || o->is_stopped == 0) continue;
        const double size = Unit_Type(s, r.player, *o).size;
        if (std::abs(static_cast<double>(o->x) - x) < size && std::abs(static_cast<double>(o->y) - y) < size) {
            return false;
        }
    }
    return s.map.pass_land[i] == 0 && IsField(s.map.objects[i]);
}

}  // namespace


bool Worker_OnField(GameState& s, std::uint8_t /*player*/, const Unit& u) {
    return FieldCellFree(s, &u, u.x, u.y, Cell{u.map_x, u.map_y});
}



void Worker_FindField(GameState& s, std::uint8_t player, Unit& u) {
    ++u.field_145;
    const std::int32_t w = s.map.width, h = s.map.height;
    auto clamp = [w, h](std::int32_t& x, std::int32_t& y) {
        if (x < 4) x = 4;
        if (y < 10) y = 10;
        if (w - 3 <= x) x = w - 4;
        if (h - 9 <= y) y = h - 10;
    };
    std::int32_t x0 = u.map_x < 6 ? 1 : u.map_x - 5, y0 = u.map_y < 11 ? 1 : u.map_y - 10;
    std::int32_t x1 = u.map_x + 5, y1 = u.map_y + 10;
    clamp(x0, y0);
    clamp(x1, y1);
    std::vector<std::array<float, 2>> pts;
    for (std::int32_t x = x0; x < x1 && pts.size() < 100; ++x) {
        for (std::int32_t y = y0; y < y1 && pts.size() < 100; ++y) {
            if (x >= w || y >= h || !IsField(s.map.objects[static_cast<std::size_t>(y) * w + x])) continue;
            const auto wx = static_cast<float>((x * 2 - (~y & 1)) * 48);
            const auto py = static_cast<float>(y * 24);
            for (const float px : {wx + 15.0f, wx + 75.0f}) {
                if (pts.size() < 100 && FieldCellFree(s, nullptr, px, py, WorldToCell(px, py))) pts.push_back({px, py});
            }
        }
    }
    if (pts.empty()) {
        u.work = 0xff;
        TUnit_OrderEnd(s, player, u, 1);
        return;
    }
    const std::size_t k = s.rng_table[s.rng_index] % pts.size();
    s.rng_index = static_cast<std::uint16_t>(s.rng_index + 1);
    TUnit_OrderMove(s, player, u, pts[k][0], pts[k][1], 1);
    u.work = 10;
}




void Worker_HarvestStep(GameState& s, std::uint8_t player, Unit& u) {
    u.field_0ca = static_cast<std::uint16_t>(u.field_0ca + 10);
    if (u.field_0ca <= 199) return;
    TUnit_StopMoving(s, player, u);
    const House* best = nullptr;
    float d_best = 9999999.0f;
    for (const House& h : s.players[player].houses) {
        if (h.is_destroyed != 0 || (h.type != 0xf && h.type != 6) || h.field_020 != 0) continue;
        const double dx = static_cast<double>(h.exit_x) - u.x, dy = static_cast<double>(h.exit_y) - u.y;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d < d_best) d_best = static_cast<float>(d), best = &h;
    }
    if (best != nullptr) {
        TUnit_OrderMove(s, player, u, best->exit_x, best->exit_y, 1);
        u.work = 0xb;
    } else {
        TUnit_OrderEnd(s, player, u, 1);
    }
    u.field_094 = u.x, u.field_098 = u.y;
}



void PutInHouse(GameState& s, std::uint8_t player, Unit& u, std::uint16_t slot) {
    House& h = s.players[player].houses[slot];
    if (u.work != 3 && u.work != 0x14 && h.field_020 == 0 && h.units_inside < h.capacity && h.is_destroyed == 0) {
        if (Player_IsYou(s, player)) s.flag_8ef4d5 = 1;
        h.inside.push_back(Unit_Ref(player, u));         // the selection drop (DeleteSelection) is interface state
        ++h.units_inside;
        u.in_house = true, u.house_player = player, u.house_slot = slot;
        u.work = 3;
        u.field_006 = h.field_030 != 0 ? 1 : 0;
        u.field_142 = 0;
        if (u.in_group) {
            if (Group* g = Unit_Group(s, player, u)) {
                Group_RemoveMember(s, *g, u);
            } else {
                Unported(s, orig::kGroupRemoveMember);
            }
        }
        u.work = 3;
        TUnit_StopMoving(s, player, u);
        u.field_07c = 0;
        u.is_stopped = 1;
        return;
    }
    if (Unit_Type(s, player, u).is_air != 0) {
        Air_TakeOff(s, player, u, player, h);
        return;
    }
    u.work = 9;
    u.final_x = h.exit_x, u.final_y = h.exit_y;
    u.field_050 = h.x, u.field_054 = h.y;
    u.dest_x = u.final_x, u.dest_y = u.final_y;
    u.field_0f8 = 1;
    u.in_house = true, u.house_player = player, u.house_slot = slot;
    TUnit_ComputeVelocity(s, player, u);
    TUnit_SetMoveAnimation(s, player, u);
    u.field_078 = u.num_work;
    TUnit_SetOrientToDest(s, player, u);
    if (u.target_orient == u.orient) u.field_078 = 100;
}



void TUnit_EnterHouseStep(GameState& s, std::uint8_t player, Unit& u) {
    std::vector<House>& houses = s.players[player].houses;
    if (u.field_0ca >= houses.size()) {
        Unported(s, orig::kWorkerArrive);  // a resource object
        return;
    }
    House& h = houses[u.field_0ca];
    if (Ftol(u.dest_x) == Ftol(h.exit_x) && Ftol(h.exit_y) == Ftol(u.dest_y)) {
        if (h.field_020 == 0 && h.is_destroyed == 0 && h.units_inside < h.capacity) {
            u.field_174 = 1;
            s.deselected.push_back(Unit_Ref(player, u));
            u.final_x = h.x, u.final_y = h.y;
            u.dest_x = h.x, u.dest_y = h.y;
            TUnit_ComputeVelocity(s, player, u);
            TUnit_SetMoveAnimation(s, player, u);
            u.field_078 = u.num_work;
            TUnit_SetOrientToDest(s, player, u);
            if (u.target_orient == u.orient) u.field_078 = 100;
            u.field_0f8 = 1;
            u.path.clear();
            u.is_stopped = 0;
            u.field_050 = u.x, u.field_054 = u.y;
        } else {
            EventUnitEndMove(s, player, u);
        }
        return;
    }
    if (Ftol(u.dest_x) == Ftol(h.x) && Ftol(u.dest_y) == Ftol(h.y)) {
        PutInHouse(s, player, u, u.field_0ca);
        return;
    }
    u.final_x = h.exit_x, u.final_y = h.exit_y;
    u.dest_x = u.final_x, u.dest_y = u.final_y;
    u.field_0f8 = 1;
    TUnit_Repath(s, player, u);
}

namespace {



bool GoToBarn(GameState& s, std::uint8_t player, Unit& u) {
    const std::vector<House>& houses = s.players[player].houses;
    std::uint32_t best = 0xffff;
    std::size_t pick = 0;
    for (std::size_t i = 0; i < houses.size(); ++i) {
        const House& h = houses[i];
        if (h.is_destroyed != 0 || h.field_020 != 0 || h.type != 0xf) continue;
        const std::int32_t dx = static_cast<std::int16_t>(u.map_x) - static_cast<std::int16_t>(h.map_x);
        const std::int32_t dy = static_cast<std::int16_t>(u.map_y) - static_cast<std::int16_t>(h.map_y);
        const auto d = static_cast<std::uint32_t>(std::abs(dy) + 2 * std::abs(dx));
        if (static_cast<std::uint16_t>(d) < static_cast<std::uint16_t>(best)) best = d, pick = i;
    }
    if (static_cast<std::int16_t>(best) == -1) return false;
    TUnit_OrderMove(s, player, u, houses[pick].exit_x, houses[pick].exit_y, 1);
    return true;
}

}  // namespace



bool Worker_JobSearch(GameState& s, std::uint8_t player, Unit& u) {
    if (u.in_group || u.work != 0xff) return false;
    if (s.players[player].control_type == 1) {
        const std::uint16_t r = s.rng_table[s.rng_index];
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 1);
        if (r < 2000 && GoToBarn(s, player, u)) return true;
    }
    std::uint16_t best = 40, pick = 0xffff;
    const std::vector<House>& houses = s.players[player].houses;
    for (std::size_t i = 0; i < houses.size(); ++i) {
        const House& h = houses[i];
        if (h.is_destroyed != 0) continue;
        if (h.field_020 == 0 && !Damaged(h)) continue;
        const auto dx = static_cast<std::int16_t>(u.map_x) - static_cast<std::int16_t>(h.map_x);
        const auto dy = static_cast<std::int16_t>(u.map_y) - static_cast<std::int16_t>(h.map_y);
        const auto d = static_cast<std::uint16_t>(2 * std::abs(dx) + std::abs(dy));
        if (d < best) best = d, pick = static_cast<std::uint16_t>(i);
    }
    if (pick != 0xffff) {
        Worker_SendToHouse(s, player, u, pick, 0);
        return true;
    }
    const Cell at{u.map_x, u.map_y};
    for (std::uint8_t k = 0; k <= 8; ++k) {
        const Cell c = Cell_Step(at, k);
        if (CellInside(s, c) && IsField(ObjectAt(s.map, c)) && CellFree(s, c)) {
            GoToField(s, player, u, c);
            return true;
        }
    }
    if (!s.original_rules && NearestSection(s, player, u)) return true;
    // Ring scan: ring r starts r diamonds below a point 5 diamonds above the worker.
    Cell start{u.map_x, u.map_y > 14 ? u.map_y - 10 : u.map_y};
    for (int r = 0; r < 10; ++r, start = Cell_Step(start, 6)) {
        if (!CellInside(s, start)) continue;
        Cell c = start;
        for (int i = 0; i < 2 * r + 1; ++i) {
            if (CellInside(s, c) && RingCheck(s, player, u, c)) return true;
            c = Cell_Step(c, 4);
        }
        for (const auto& [dir, n] : {std::pair{1, 2 * r}, std::pair{2, 2 * r}, std::pair{3, 2 * r - 1}}) {
            for (int i = 0; i < n; ++i) {
                c = Cell_Step(c, static_cast<std::uint8_t>(dir));
                if (!CellInside(s, c)) {
                    c = Cell_Step(c, static_cast<std::uint8_t>(dir));
                } else if (RingCheck(s, player, u, c)) {
                    return true;
                }
            }
        }
    }
    return false;
}







void Worker_SendToHouse(GameState& s, std::uint8_t player, Unit& u, std::uint16_t id, std::uint16_t spot,
                        const Unit* mover) {
    if (mover == nullptr) mover = &u;
    if (u.field_154 != 0) return;
    const UnitType& t = Unit_Type(s, player, u);
    std::uint8_t mc = t.move_class;
    u.field_09f = 0;
    std::vector<House>& houses = s.players[player].houses;
    const bool obj = id > 1000;
    const SpecialObject* rec = nullptr;
    House* h = nullptr;
    if (obj) {
        const std::size_t k = static_cast<std::uint16_t>(id - 0x25e4);
        if (k >= s.map.specials.size()) {
            Unported(s, orig::kSendToHouse);  // an object id past the special list
            return;
        }
        rec = &s.map.specials[k];
    } else if (id == 1000 || id >= houses.size()) {
        Unported(s, orig::kSendToHouse);  // a slot past NumHouse
        return;
    } else {
        h = &houses[id];
    }
    if (h != nullptr && t.is_air != 0 && h->type == 0x14) return;
    if (t.field_006 == 0 && mc != 6) {
        EventUnitEndMove(s, player, u);
        return;
    }
    if (u.work == 0x14 || u.work == 9) return;
    u.has_attack_unit = false;
    u.player_unit_attack = 0xff;
    u.player_house_attack = 0xff;
    u.has_house_attack = false;
    u.field_093 = 0;
    const std::uint16_t hid = obj ? id : HouseId(player, id);
    if (obj ? !(rec->f_a >= 0.0f) && mc == 6 : h->field_020 == 0 && !Damaged(*h) && mc == 6) mc = 2;
    if (mc != 6) {
        if (mc == 1 || mc == 11 || mc == 12 || t.is_air != 0 || t.is_ship != 0) {
            Unported(s, orig::kSendToHouse);  // aircraft and ships
            return;
        }
        u.field_0ca = id;
        u.field_086 = 0;
        if (obj) {
            const std::int32_t x = (rec->x * 2 - (~rec->y & 1)) * 48;
            const std::int32_t y = rec->y * 24 - s.map.heights[CellIndex(s.map, Cell{rec->x, rec->y})];
            u.work = 0xff;
            u.in_house = false;
            TUnit_MoveTo(s, player, u, static_cast<float>(x), static_cast<float>(y), 0);
            return;
        }
        // Any other class walks to the door; Work 2 (enter) when the house is finished and standing.
        u.work = h->field_020 == 0 && h->is_destroyed == 0 ? 2 : 0xff;
        u.in_house = true, u.house_player = player, u.house_slot = id;
        TUnit_MoveTo(s, player, u, h->exit_x, h->exit_y, 0);
        return;
    }
    int cols = 3, rows = 3;
    Cell row{0, 0};
    if (obj) {
        row = Cell{rec->x, rec->y + 2};
    } else {
        const HouseFootprint& fp = House_Footprint(s, player, *h);
        cols = fp.a + 2, rows = fp.b + 2;
        row = Cell{h->map_x, h->map_y + 2};
    }
    std::vector<float> dist;
    std::vector<std::array<std::int32_t, 2>> pt;
    for (int r = 0; r < rows; ++r) {
        Cell c = row;
        for (int k = 0; k < cols; ++k) {
            if (CellInside(s, c) && s.map.pass_land[CellIndex(s.map, c)] == 0 &&
                s.map.objects[CellIndex(s.map, c)] != hid) {
                std::int32_t px = (c.x * 2 - (~c.y & 1)) * 48, py = c.y * 24;
                if (k == 0) {
                    if (r == 0) {
                        px += 48, py -= 20;
                    } else {
                        px += 74;
                    }
                } else if (k == cols - 1) {
                    if (r == 0 || r != rows - 1) {
                        px += 5;
                    } else {
                        px += 48, py += 20;
                    }
                } else if (r == rows - 1) {
                    px += 48, py += 19;
                } else if (r == 0) {
                    px += 4;
                }
                const double dx = static_cast<double>(mover->x) - px, dy = static_cast<double>(mover->y) - py;
                dist.push_back(static_cast<float>(std::sqrt(dx * dx + dy * dy)));
                pt.push_back({px, py});
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    if (dist.size() <= spot) {
        EventUnitEndMove(s, player, u);
        return;
    }
    for (std::size_t i = 0; i <= spot; ++i) {  // selection sort up to the wanted position
        std::size_t m = i;
        for (std::size_t j = i + 1; j < dist.size(); ++j) {
            if (dist[m] > dist[j]) m = j;
        }
        std::swap(dist[i], dist[m]);
        std::swap(pt[i], pt[m]);
    }
    u.work = 1;
    u.field_0ca = id;
    u.field_086 = 0;
    auto x = static_cast<float>(pt[spot][0]), y = static_cast<float>(pt[spot][1]);
    if (obj) {
        u.in_house = false;


        const Cell c = WorldToCell(x, y);
        if (c.x >= 0 && c.y >= 0 && c.x < s.map.width && c.y < s.map.height &&
            s.map.pass_land[CellIndex(s.map, c)] == 0) {
            const std::uint16_t a = s.rng_table[s.rng_index];
            const std::uint16_t b = s.rng_table[static_cast<std::uint16_t>(s.rng_index + 1)];
            s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
            x = static_cast<float>(a % 30 + 33 + (c.x * 2 - (~c.y & 1)) * 48);
            y = static_cast<float>(b % 30 - 15 + c.y * 24);
        }
    } else {
        u.in_house = true, u.house_player = player, u.house_slot = id;
    }
    TUnit_MoveTo(s, player, u, x, y, 0);
}


void Worker_Arrive(GameState& s, std::uint8_t player, Unit& u) {
    u.field_0f8 = 0;



    if (u.player_unit_attack != 0xff) {
        const Unit* t = AttackTarget(s, u);
        if (t == nullptr) {
            Unported(s, orig::kWorkerArrive);  // a target player without a target: the original reads a null pointer
            return;
        }
        const double dx = static_cast<double>(u.x) - t->x, dy = static_cast<double>(u.y) - t->y;
        if (std::sqrt(dx * dx + dy * dy) <= 50.0 && Unit_Type(s, player, u).anim_size[7] != 0 && u.work != 3 &&
            u.work != 0x14) {
            TUnit_ClearPath(u);
            TUnit_StartMelee(s, player, u);
            u.is_stopped = 1;
            return;
        }
    } else if (u.player_house_attack != 0xff) {
        const House* h = HouseTarget(s, u);
        if (h == nullptr) {
            Unported(s, orig::kWorkerArrive);  // a target player without a target: the original reads a null pointer
            return;
        }
        if (h->is_destroyed == 0 &&
            Unit_Touches(s, u, static_cast<std::uint16_t>((u.player_house_attack + 1) * 1000 + h->num), 1)) {
            const float hx = h->x, hy = h->y;
            TUnit_ClearPath(u);
            TUnit_StartMelee(s, player, u);
            u.field_078 = u.num_work;
            TUnit_FaceTowards(s, player, u, hx, hy);
            if (u.target_orient == u.orient) u.field_078 = 100;
            return;
        }
        TUnit_AttackEnd(s, player, u);
        return;
    }
    if (u.field_09f != 0) {
        u.field_09f = 0;
        u.is_stopped = 1;
        TUnit_StopMoving(s, player, u);
        u.field_07c = 150;
        return;
    }
    switch (u.work) {
        case 1: {
            std::vector<House>& houses = s.players[player].houses;
            if (u.field_0ca > 999) {
                Worker_StartWorkObject(s, player, u);
                return;
            }
            if (u.field_0ca >= houses.size()) {
                Unported(s, orig::kWorkerStartWork);  // a slot past NumHouse
                return;
            }
            Worker_StartWork(s, player, u, houses[u.field_0ca]);
            return;
        }
        case 2: TUnit_EnterHouseStep(s, player, u); return;
        case 10:
            if (Worker_OnField(s, player, u)) {
                u.num_work = 10;
                u.num_sprite = 0;
                u.field_0ca = 0;
                u.is_stopped = 1;
            } else {
                Worker_FindField(s, player, u);
            }
            return;
        case 11: {  // a delivery: food, then back to the field
            Player& pl = s.players[player];
            const double v = static_cast<double>(pl.factor_744) * static_cast<double>(80.0f);
            pl.food = v + pl.food;
            pl.food_stat = v + pl.food_stat;
            if (pl.food < 0.0) pl.food = 0.0;
            pl.mined[1] = v + pl.mined[1];  // FoodMined
            TUnit_OrderMove(s, player, u, u.field_094, u.field_098, 1);
            u.work = 10;
            return;
        }
        default: EventUnitEndMove(s, player, u); return;
    }
}



void TUnit_OnHouseBuilt(GameState& s, std::uint8_t player, Unit& u, House& h);

bool Worker_WorkStep(GameState& s, std::uint8_t player, Unit& u) {
    const std::uint16_t t = u.field_0ca;
    if (t >= 0x25e4 && t < 0x2af8) {
        Worker_BuildSpecial(s, player, u);  // a pontoon section
        return false;
    }
    if (t == 0xffff) {
        TUnit_StopMoving(s, player, u);
        EventUnitEndMove(s, player, u);
        return true;
    }
    std::vector<House>& houses = s.players[player].houses;
    if (t >= houses.size()) return false;
    House& h = houses[t];
    if (h.is_destroyed != 0) {
        Worker_JobDone(s, player, u, h);
        return false;
    }
    if (h.field_020 != 0) {
        const HouseType& ht = House_Type(s, player, h);
        const std::uint8_t before = House_BuildStage(ht, h.field_020);
        h.field_020 = static_cast<std::uint16_t>(h.field_020 - 1);
        if (h.field_020 == 0) {
            TUnit_OnHouseBuilt(s, player, u, h);
        } else if (House_BuildStage(ht, h.field_020) != before) {
            House_SetGraphic(h, 1, static_cast<std::uint8_t>(before + 1));
        }
        return false;
    }
    const double m = static_cast<double>(h.max_hp);
    if (!(m > h.hp)) {
        Worker_JobDone(s, player, u, h);
        return false;
    }
    const std::uint16_t rate = House_Type(s, player, h).work_rate;
    double d = m / static_cast<double>(rate) * static_cast<double>(0.5f);
    if (d < 1.0) d = 1.0;
    const double sum = d + h.hp;
    h.hp = static_cast<float>(sum);
    if (sum > m) h.hp = static_cast<float>(m);
    return false;
}

std::uint16_t UnitCap_Add(const GameState& s, std::uint16_t cap, std::uint16_t add) {
    if (s.original_rules) return static_cast<std::uint16_t>(cap + add);
    return static_cast<std::uint16_t>(std::min<std::int32_t>(std::int32_t{cap} + add, 0xffff));
}

std::uint16_t UnitCap_Sub(const GameState& s, std::uint16_t cap, std::uint16_t sub) {
    if (s.original_rules) return static_cast<std::uint16_t>(cap - sub);
    return static_cast<std::uint16_t>(std::max<std::int32_t>(std::int32_t{cap} - sub, 0));
}



void House_BuiltCallback(GameState& s, std::uint8_t player, House& h) {
    Player& pl = s.players[player];
    switch (h.type) {
        case 6:
        case 9:
            pl.unit_cap = UnitCap_Add(s, pl.unit_cap, static_cast<std::uint16_t>(Ftol(h.field_070)));
            break;
        case 0:
            pl.power_made =
                static_cast<float>(static_cast<double>(static_cast<std::uint16_t>(Ftol(h.field_070))) + pl.power_made);
            break;
        case 14:
            ++pl.mint_count;
            if (pl.mint_done != 0) pl.gold_factor = Mint_Factor(s);
            break;
        case 15: House_PlantFields(s, player, h); break;
        default: break;
    }
}




void TUnit_OnHouseBuilt(GameState& s, std::uint8_t player, Unit& u, House& h) {
    Player& pl = s.players[player];
    if (h.type == 0x18) {  // the site becomes a unit of type 0x17 (docs/combat.md 13.5); no counter, no callback
        AddUnitToPlayer(s, player, CreateUnit(s, h.x, h.y, 0x17, pl.nation, player));
        h.hp = -1000000.0f;
        h.is_destroyed = 1;
        u.work = 0xff;
        if (player == s.my_player) Event_HouseSound(s, player, h, 2, 5);
        House_OnDestroyed(s, player, h, 0xff);
        TUnit_StopMoving(s, player, u);
        u.is_stopped = 1;
        return;
    }
    if (h.type < 25) ++pl.houses_done[h.type];
    House_BuiltCallback(s, player, h);
    const HouseType& ht = House_Type(s, player, h);
    pl.power_used = static_cast<float>(static_cast<double>(ht.power_use) + pl.power_used);
    if (player == s.my_player) Event_HouseSound(s, player, h, 2, 5);
    House_SetGraphic(h, 0, 0);
    u.work = 0xff;
    if (h.is_destroyed == 0 && h.type > 9 && h.type < 0xd) {
        Worker_SendToHouse(s, player, u, h.num, 0);
        return;
    }
    if (!Worker_JobSearch(s, player, u)) {
        TUnit_StopMoving(s, player, u);
        u.is_stopped = 1;
        if (u.work == 3) {
            Unported(s, orig::kLogError);
            return;
        }
        u.work = 0xff;
    }
}




void Order_DeleteKey(GameState& s, std::uint8_t player, const UnitRef* first) {
    if (first != nullptr) {
        if (Unit* u = Unit_Find(s, *first)) {
            u->hp = -10.0f;
            OnUnitKilled(s, player, *u, player);
            s.flag_8ab825 = 1;
        } else {
            Unported(s, orig::kDeleteKey);  // a selected unit the sim does not know
        }
    }
    Player& p = s.players[player];
    for (House& h : p.houses) {
        if (h.is_destroyed != 0 || h.selected == 0) continue;
        const double dmg = static_cast<double>(House_TypeMaxHp(s, player, h)) * static_cast<double>(0.52f);
        h.hp = static_cast<float>(static_cast<double>(h.hp) - dmg);
        if (!(h.hp > 1.0f)) {
            s.flag_8ab824 = 1;
            House_OnDestroyed(s, player, h, player);
        }
    }
}

}  // namespace antanta::sim
