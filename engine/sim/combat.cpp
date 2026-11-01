



#include <bit>
#include <cmath>

#include "control_group.hpp"
#include "dmath.hpp"
#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

namespace {

constexpr float kGridInvW = 0.0020833334f;
constexpr float kGridInvH = 0.004166667f;

bool CrawlSet(std::uint8_t nw) { return nw == 1 || nw == 2 || nw == 0x14 || nw == 8 || nw == 0xe || nw == 0x15; }


bool FireSet(std::uint8_t nw) { return nw == 6 || nw == 0x12 || nw == 2 || nw == 0x14 || nw == 0x15; }

double Dist(const Unit& a, const Unit& b) {
    const double dx = static_cast<double>(a.x) - b.x, dy = static_cast<double>(a.y) - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

double DistPt(float a, float b, float c, float d) {
    const double dx = static_cast<double>(a) - c, dy = static_cast<double>(b) - d;
    return std::sqrt(dx * dx + dy * dy);
}

std::int32_t AliveUnits(const GameState& s) {
    std::int32_t n = 0;
    for (const Player& pl : s.players) {
        if (pl.is_lost == 0) n += pl.units_alive;
    }
    return n;
}


bool CanAttackHouses(const UnitType& t, const Unit& u) {
    return t.field_007 != 0 && (t.field_008 == 0 || !(u.field_118 <= 1.0f));
}


bool CanMelee(const UnitType& t, const Unit& u) { return t.anim_size[7] != 0 && u.work != 3 && u.work != 0x14; }


bool CanGrenade(const UnitType& t, const Unit& u) {
    return t.anim_size[0x16] != 0 && u.work != 3 && u.work != 0x14 && !(u.field_118 < 1.0f);
}


bool KeepsDistance(const Unit& u) {
    if (u.field_116 == 1 || u.field_116 == 3) return true;
    if (u.player_unit_attack != 0xff || u.player_house_attack != 0xff || u.field_142 != 0) return false;
    if (u.work == 0x14 || u.work == 3) return false;
    return u.field_116 != 4;
}

std::size_t GridCellOf(const GameState& s, float x, float y) {
    return static_cast<std::size_t>(Ftol(static_cast<double>(kGridInvH) * y) * s.grid_w +
                                    Ftol(static_cast<double>(kGridInvW) * x));
}


std::int32_t GridCellClamped(const GameState& s, float x, float y) {
    const auto pw = static_cast<float>(static_cast<std::uint32_t>(s.map.width) * 96);
    const auto ph = static_cast<float>(static_cast<std::uint32_t>(s.map.height) * 24);
    if (x < 1.0f) x = 1.0f;
    if (y < 1.0f) y = 1.0f;
    if (x >= pw) x = static_cast<float>(static_cast<std::uint32_t>(s.map.width) * 96 - 1);
    if (y >= ph) y = static_cast<float>(static_cast<std::uint32_t>(s.map.height) * 24 - 1);
    return static_cast<std::int32_t>(GridCellOf(s, x, y));
}


void SetOrient(Unit& u, std::uint8_t o) {
    if (o > 7) return;
    u.orient = o;
    u.angle = static_cast<float>(static_cast<double>(static_cast<std::int8_t>(o)) * static_cast<double>(0.7853982f) +
                                 static_cast<double>(0.0001f));
}

std::uint8_t OrientFromSlope(bool left, double slope) {
    if (left) {
        if (slope < -2.41) return 0;
        if (slope < -0.41) return 1;
        if (slope < 0.42) return 2;
        if (slope < 2.42) return 3;
        return 4;
    }
    if (slope < -2.41) return 0;
    if (slope < -0.41) return 7;
    if (slope < 0.42) return 6;
    if (slope < 2.42) return 5;
    return 4;
}


void StartMelee(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    const std::uint8_t nw = u.num_work;
    if (nw == 7 || nw == 0xd) return;
    if (nw == 6 && t.anim_size[0x13] != 0) {
        u.num_work = 0x13, u.field_078 = 0xd;
        return;
    }
    if (CrawlSet(nw)) {
        if ((nw == 2 || nw == 0x14 || nw == 0x15) && t.anim_size[0x15] != 0) {
            u.num_work = 0x15, u.num_sprite = 0;
            return;
        }
        u.num_work = 0xe, u.num_sprite = 0, u.field_078 = 0xd;
        return;
    }
    if (nw == 0x13 || nw == 9) {
        u.field_078 = 0xd;
        return;
    }
    u.num_work = 0xd;
    if (t.anim_size[0xd] == 0) {
        u.num_work = 7;
        if (t.anim_size[7] == 0) TUnit_StopMoving(s, player, u);
    }
    u.num_sprite = 0;
}


void StartGrenade(GameState& s, std::uint8_t player, Unit& u, float px, float py) {
    const UnitType& t = Unit_Type(s, player, u);
    if (t.anim_size[0x16] == 0) return;
    u.field_0d4 = px, u.field_0d8 = py;
    const std::uint8_t nw = u.num_work;
    if (nw == 0x16) return;
    if (nw == 7 && t.anim_size[9] != 0) {
        u.field_078 = 0x16, u.num_work = 9;
    } else if (CrawlSet(nw)) {
        if ((nw == 2 || nw == 0x14 || nw == 0x15) && t.anim_size[0x15] != 0) {
            u.num_work = 0x15, u.num_sprite = 0, u.field_078 = 0x16;
        } else {
            u.field_078 = 0x16, u.num_work = 0xe, u.num_sprite = 0;
        }
    } else if (nw == 0x13 || nw == 9) {
        u.field_078 = 0x16;
    } else {
        u.num_work = 0x16, u.num_sprite = 0;
    }
}

}  // namespace

bool TUnit_CanGrenade(const GameState& s, std::uint8_t player, const Unit& u) {
    return CanGrenade(Unit_Type(s, player, u), u);
}

void TUnit_StartGrenade(GameState& s, std::uint8_t player, Unit& u, float px, float py) {
    StartGrenade(s, player, u, px, py);
}

void TUnit_StartMelee(GameState& s, std::uint8_t player, Unit& u) { StartMelee(s, player, u); }

// The targets the original keeps as pointers.
Unit* AttackTarget(GameState& s, const Unit& u) { return u.has_attack_unit ? Unit_Find(s, u.attack_ref) : nullptr; }

House* HouseTarget(GameState& s, const Unit& u) {
    if (!u.has_house_attack || u.house_attack_player >= s.players.size()) return nullptr;
    std::vector<House>& hs = s.players[u.house_attack_player].houses;
    return u.house_attack_slot < hs.size() ? &hs[u.house_attack_slot] : nullptr;
}

void SetAttackUnit(Unit& u, std::uint8_t tp, const Unit& t) {
    u.has_attack_unit = true;
    u.attack_ref = UnitRef{tp, t.id, t.serial};
}


void MarkZone(GameState& s, std::uint8_t p, float x, float y, std::uint8_t v) {
    if (p >= s.players.size()) return;
    Player& pl = s.players[p];
    const std::int32_t w = pl.zone_width, size = static_cast<std::int32_t>(pl.zone_grid.size());
    const std::int32_t i = (Ftol(y) / 300) * w + Ftol(x) / 300;
    if (i < 0 || i >= size) return;
    if (v < 100) {
        for (const std::int32_t j : {i - 1, i + 1, i - w, i + w}) {
            if (j >= 0 && j < size) pl.zone_grid[static_cast<std::size_t>(j)] = v;
        }
    }
    pl.zone_grid[static_cast<std::size_t>(i)] = v;
}


void Grid_Init(GameState& s) {
    s.grid_w = s.map.width * 96 / 480, s.grid_h = s.map.height * 24 / 240;
    s.unit_cells.assign(static_cast<std::size_t>(s.grid_w * s.grid_h + 200), {});
    s.house_cells.assign(static_cast<std::size_t>(s.grid_w * s.grid_h + 200), {});
}


void Grid_PushUnit(GameState& s, std::uint8_t player, const Unit& u) {
    if (s.unit_cells.empty()) return;
    if (!(u.x > 0.0f) || !(u.y > 0.0f)) return;
    if (!(u.x <= static_cast<float>(static_cast<std::uint32_t>(s.map.width) * 96))) return;
    if (!(u.y <= static_cast<float>(static_cast<std::uint32_t>(s.map.height) * 24))) return;
    const std::size_t c = GridCellOf(s, u.x, u.y);
    if (c >= s.unit_cells.size()) {
        Unported(s, orig::kZoneInsertUnit);  // past the grid's spare cells
        return;
    }
    s.unit_cells[c].push_back(Unit_Ref(player, u));
}


void Grid_PushHouse(GameState& s, std::uint8_t player, std::uint16_t slot) {
    const House& h = s.players[player].houses[slot];
    if (s.house_cells.empty() || h.is_destroyed != 0) return;
    if (static_cast<std::uint32_t>(h.map_x) >= s.map.width || static_cast<std::uint32_t>(h.map_y) >= s.map.height) {
        return;
    }
    const std::size_t c = GridCellOf(s, h.x, h.y);
    if (c >= s.house_cells.size()) {
        Unported(s, orig::kUpdateUnits);
        return;
    }
    s.house_cells[c].push_back(HouseRef{player, slot, 0});
}


void Grid_TickReset(GameState& s) {
    if (s.tick % 33 == 0) {
        for (auto& c : s.unit_cells) c.clear();
    }
    if (s.tick % 62 == 0) {
        for (auto& c : s.house_cells) c.clear();
    }
}


void SaveOrder(Unit& u) {
    if (u.field_0bc != 0) return;
    u.waypoints[0] = Unit::Waypoint{u.final_x, u.final_y, u.work, u.field_0ca};
    u.field_0bc = 1;
}


void FaceToPoint(GameState& s, std::uint8_t player, Unit& u, float px, float py) {
    const double dx = static_cast<double>(px) - u.x;
    const double dy = static_cast<double>(py) - u.y;
    float slope;
    if (dx > 0.0) {
        slope = static_cast<float>(dy / dx);
    } else if (dx < 0.0) {
        slope = static_cast<float>(dy / (static_cast<double>(u.x) - px));
    } else {
        slope = static_cast<float>(dy * static_cast<double>(10000.0f));
    }
    const std::uint8_t o = OrientFromSlope(dx < 0.0, slope);
    u.target_orient = o;
    if (o == u.orient) return;
    const std::uint8_t nw = u.num_work;
    if (!CrawlSet(nw)) {
        const UnitType& t = Unit_Type(s, player, u);
        if (t.field_006 == 0) {
            TUnit_StartTurn(s, player, u);
            return;
        }
        const int diff = o < u.orient ? u.orient - o : o - u.orient;
        if (diff < 2 || (nw != 7 && nw != 0xd && !FireSet(nw))) {
            TUnit_StartTurn(s, player, u);
            return;
        }
    }
    u.orient = o;
    u.angle = static_cast<float>(static_cast<double>(static_cast<std::int8_t>(o)) * static_cast<double>(0.7853982f) +
                                 static_cast<double>(0.02f));
}


void FaceAttackTarget(GameState& s, std::uint8_t player, Unit& u) {
    float px = u.field_0d4, py = u.field_0d8;
    if (u.has_attack_unit) {
        const Unit* t = AttackTarget(s, u);
        if (t == nullptr) {
            Unported(s, orig::kFaceAttackTarget);  // a dangling target
            return;
        }
        px = t->x, py = t->y;
    }
    const auto dxf = static_cast<float>(static_cast<double>(px) - u.x);
    double slope;
    if (dxf > 0.0f) {
        slope = (static_cast<double>(py) - u.y) / dxf;
    } else if (dxf < 0.0f) {
        slope = (static_cast<double>(py) - u.y) / (static_cast<double>(u.x) - px);
    } else {
        slope = (static_cast<double>(py) - u.y) * static_cast<double>(10000.0f);
    }
    const std::uint8_t o = OrientFromSlope(dxf < 0.0f, slope);
    u.target_orient = o;
    const UnitType& t = Unit_Type(s, player, u);
    if (t.move_class == 4) {
        SetOrient(u, o);
        return;
    }
    const std::uint8_t nw = u.num_work;
    bool snap = CrawlSet(nw);
    if (!snap && t.field_006 != 0) {
        const int diff = o < u.orient ? u.orient - o : o - u.orient;
        snap = diff < 2 && (nw == 7 || nw == 0xd || FireSet(nw));
    }
    if (snap) {
        SetOrient(u, o);
        return;
    }
    if (u.target_orient != u.orient) TUnit_StartTurn(s, player, u);
}


void StartFireAnim(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    const std::uint8_t nw = u.num_work;
    if (!FireSet(nw)) {
        if (nw == 7 && t.anim_size[9] != 0) {
            u.num_work = 9, u.field_078 = 0x12;
        } else if (nw == 1 || nw == 8 || nw == 0xe) {
            u.num_work = t.anim_size[0x14] != 0 ? 0x14 : t.anim_size[2] != 0 ? 2 : 1;
            u.num_sprite = 0;
        } else if (nw == 0x13 || nw == 9) {
            u.field_078 = 0x12;
        } else if (u.type == 0x17 && nw == 3) {
            u.num_work = 6, u.num_sprite = 0;
        } else {
            u.num_work = 0x12;
            if (t.anim_size[0x12] == 0) {
                u.num_work = 6;
                if (t.anim_size[6] == 0) TUnit_StopMoving(s, player, u);
            }
            u.num_sprite = 0;
        }
    }
    if (u.field_078 == 100) {
        u.field_078 = u.num_work;
        FaceAttackTarget(s, player, u);
        if (u.target_orient == u.orient) u.field_078 = 100;
    } else {
        FaceAttackTarget(s, player, u);
    }
}


bool TargetClear(GameState& s, std::uint8_t p, const Unit& t) {
    if (t.work == 3 || t.work == 0x14) return true;
    if (!CellInside(s, Cell{t.map_x, t.map_y})) return true;
    for (std::uint8_t d = 0; d <= 8; ++d) {
        const Cell c = Cell_Step(Cell{t.map_x, t.map_y}, d);
        const auto i = static_cast<std::size_t>(c.y) * s.map.width + c.x;
        for (const UnitRef& r : s.map.cell_units[i]) {
            const Unit* w = Unit_Find(s, r);
            if (w == nullptr) continue;
            if (!(w->field_024 > 1.0f) && w->is_deleted == 0 && !IsEnemy(s, p, r.player)) return false;
        }
    }
    return true;
}


double ExpFactor(GameState& s, std::uint8_t player, const Unit& u) {
    if (u.in_group) {
        const Group* g = Unit_Group(s, player, u);
        if (g == nullptr) {
            Unported(s, orig::kExpFactor);
            return 1.0;
        }
        return g->has_mc7 != 0 ? 1.5 : 1.0;
    }
    const double x = static_cast<double>(u.field_08c) * static_cast<double>(0.002f) + 1.0;
    return x > 1.5 ? 1.5 : x;
}



double CalcHitDamage(GameState& s, std::uint8_t ap, const Unit& a, std::uint8_t tp, const Unit& t) {
    const UnitType& at = Unit_Type(s, ap, a);
    const UnitType& tt = Unit_Type(s, tp, t);
    if (t.type != 0x18 && tt.move_class == 3) return 0.0;
    double att = at.attack[ap][1], def = tt.defense[tp][1];
    if (at.move_class == 9) {
        att = at.attack[ap][4], def = tt.defense[tp][4];
    } else if (at.is_air != 0 && tt.is_air == 0) {
        att = att * static_cast<double>(0.5f);
    }
    const int r = Rng_Next(s) % 100 + 1;
    double x = (static_cast<double>(r) * static_cast<double>(0.01f)) * (att - def);
    x = x * ExpFactor(s, ap, a);
    x = x / ExpFactor(s, tp, t);
    if (t.work == 3) {
        const House* h =
            t.in_house && t.house_player < s.players.size() && t.house_slot < s.players[t.house_player].houses.size()
                ? &s.players[t.house_player].houses[t.house_slot]
                : nullptr;
        const std::uint16_t ht = h != nullptr ? h->type : 0;
        x = x * static_cast<double>(ht == 0x15 || ht == 7 || ht == 1 ? 0.01f : 0.05f);
    } else if (CrawlSet(t.num_work) || (tt.move_class == 16 && (t.num_work == 6 || t.num_work == 0x12))) {
        x = x * static_cast<double>(0.4f);
    }
    if (x < 0.5) x = 0.5;
    return x;
}

namespace {


bool CannotShoot(GameState& s, std::uint8_t ap, Unit& a, std::uint8_t tp, const Unit& t) {
    const auto d = static_cast<float>(Dist(a, t));
    const UnitType& tt = Unit_Type(s, tp, t);
    if (tt.move_class == 3 && t.type != 0x18) return true;
    if (static_cast<double>(Unit_Type(s, ap, a).range[ap]) < d) return true;
    if (tt.is_air != 0 || a.work == 3) {
        FaceToPoint(s, ap, a, t.x, t.y);
        return false;
    }
    if (d < 150.0f) return true;
    if (TargetClear(s, ap, t)) {
        FaceToPoint(s, ap, a, t.x, t.y);
        return false;
    }
    return true;
}

// True when a slot of pl, live or freed, may refer to the dead unit (its target or its transporter). A scan of the
// slots in memory order, much faster on long unit lists than the list walk (docs/bugs.md B59); false: the walk in
// ClearReferences would change nothing in this player.
bool AnySlotRefersTo(const Player& pl, const UnitRef& dead) {
    for (const Unit& v : pl.unit_slots) {
        if (v.has_attack_unit && v.attack_ref.player == dead.player && v.attack_ref.id == dead.id &&
            v.attack_ref.serial == dead.serial) {
            return true;
        }
        if (v.has_future_transporter && v.future_transporter.player == dead.player &&
            v.future_transporter.serial == dead.serial) {
            return true;
        }
        if (v.in_transport && v.transporter.player == dead.player && v.transporter.serial == dead.serial) return true;
    }
    return false;
}



void ClearReferences(GameState& s, const UnitRef& dead) {
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        Player& pl = s.players[p];
        if (pl.is_lost != 0) continue;
        if (!AnySlotRefersTo(pl, dead)) continue;
        for (const std::uint32_t k : Player_UnitSlots(pl)) {
            Unit& v = pl.unit_slots[k];
            if (v.has_attack_unit && v.attack_ref.player == dead.player && v.attack_ref.id == dead.id &&
                v.attack_ref.serial == dead.serial) {
                TUnit_ClearTargets(v);
                TUnit_AttackEnd(s, static_cast<std::uint8_t>(p), v);
            }
            Transport_ClearLinks(v, dead);
        }
    }
}

}  // namespace

void Unit_ClearReferences(GameState& s, const UnitRef& dead) { ClearReferences(s, dead); }


void OnUnitKilled(GameState& s, std::uint8_t player, Unit& u, std::uint8_t killer) {
    if (u.is_deleted != 0) return;
    u.has_attack_unit = false, u.player_unit_attack = 0xff;
    u.player_house_attack = 0xff, u.has_house_attack = false;
    Event_UnitSound(s, player, u, 4, 4);
    if (u.work == 3) House_FreeDeadUnit(s, u);  // a unit killed inside a house leaves the list
    Transport_OnKilled(s, player, u, killer);   // a passenger gets out first; a transport's passengers die with it
    if (u.in_group) {
        if (Group* g = Unit_Group(s, player, u)) {
            Group_RemoveMember(s, *g, u);
        } else {
            Unported(s, orig::kGroupRemoveMember);
        }
    }
    Player& pl = s.players[player];
    ControlGroup_RemoveUnit(pl, u);
    --pl.unit_count[u.type % 40];
    --pl.units_alive;
    ++pl.units_lost;
    u.is_deleted = 1;
    u.work = 0xff;
    u.field_0ca = 0xffff;
    const UnitType& t = Unit_Type(s, player, u);
    if ((t.move_class == 10 || t.move_class == 16) && FireSet(u.num_work)) {
        u.num_work = 0x11;
    } else if (CrawlSet(u.num_work)) {
        u.num_work = 0x11;
    } else if (u.hp <= -1000.0f) {
        u.num_work = 0x10;
    } else {
        u.num_work = 0xf;
    }
    if (t.anim_size[u.num_work] == 0) u.num_work = 0xf;
    if (t.is_air != 0) {
        Air_OnKilled(s, player, u);
    } else {
        u.num_sprite = 0;
    }
    if (t.is_ship != 0) Ship_Killed(s, player, u, killer);
    if (killer < 8 && killer < s.players.size()) ++s.players[killer].units_killed;
}


void AlertNearby(GameState& s, std::uint8_t p, std::uint16_t mx, std::uint16_t my, std::uint8_t ap, Unit& a) {
    if (a.work == 3 || a.is_deleted != 0) return;
    const std::uint32_t cells = static_cast<std::uint32_t>(s.map.width) * s.map.height;
    int count = 0;
    for (int xo = -2; xo <= 2; ++xo) {
        if (count > 9) return;
        for (int yo = -4; yo <= 4; ++yo) {
            if (count > 9) break;
            const std::uint32_t c = static_cast<std::uint32_t>(mx + (my + yo) * static_cast<int>(s.map.width) + xo);
            if (c >= cells) continue;
            const std::vector<UnitRef> list = s.map.cell_units[c];
            for (const UnitRef& r : list) {
                Unit* v = Unit_Find(s, r);
                if (v == nullptr) continue;
                if (v->field_142 != 0 || r.player == p) continue;
                const UnitType& vt = Unit_Type(s, r.player, *v);
                if (v->is_deleted == 0 && v->field_006 != 0 && v->work == 0xff && IsEnemy(s, r.player, p) &&
                    vt.is_air == 0 && !FireSet(v->num_work) && v->num_work != 7 && v->num_work != 0xd) {
                    if (!v->in_group) {
                        if ((v->type != 0 || (v->map_x == mx && v->map_y == my)) &&
                            (vt.is_ship == 0 || (v->player_unit_attack == 0xff && v->player_house_attack == 0xff))) {
                            TUnit_OnAttackedBy(s, r.player, *v, ap, a, 1);
                            ++count;
                        }
                        continue;
                    }
                } else if (!(IsEnemy(s, r.player, p) && v->in_group)) {
                    continue;
                }
                Group* g = Unit_Group(s, r.player, *v);
                if (g == nullptr) {
                    Unported(s, orig::kGroupAlert);
                    continue;
                }
                Group_Alert(s, *g, ap, &a);
                count += 5;
            }
        }
    }
}


void Group_Alert(GameState& s, Group& g, std::uint8_t ap, Unit* a) {
    if (g.field_048 != 0) return;
    g.field_048 = 0x28;
    if (a != nullptr && !IsEnemy(s, g.player, ap)) return;
    if (g.live == 0) return;
    bool any = false;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        Unit* m = g.members[i].player == 0xff ? nullptr : Unit_Find(s, g.members[i]);
        if (m == nullptr || m->field_142 != 0 || m->player_unit_attack != 0xff || m->player_house_attack != 0xff) {
            continue;
        }
        a->field_141 = 0xc;
        if (m->field_006 == 0) continue;
        TUnit_OnAttackedBy(s, g.player, *m, ap, *a, 1);
        a->field_141 = 0xc;
        any = true;
    }
    Event_GroupAlert(s, g, any);
    g.blocked = 100;
}





void House_MuzzleFlash(GameState& s, std::uint8_t hp, const House& h) {
    const HouseType& ht = House_Type(s, hp, h);
    const std::uint8_t n = ht.field_26e;
    if (n == 0) return;
    MarkZone(s, hp, h.x, h.y, 0x1e);
    auto r = [&s](int ahead) {
        return s.rng_table.empty() ? 0 : s.rng_table[static_cast<std::uint16_t>(s.rng_index + ahead)];
    };
    const bool sides = h.type == 0x15 || h.type == 7 || h.type == 1;
    std::uint32_t k = 0;
    if (h.units_inside < n && h.units_inside != 0 && sides) {
        k = r(1) % n;
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
    } else {
        k = h.units_inside < n && h.units_inside != 0 ? r(0) % h.units_inside : r(0) % n;
        ++s.rng_index;
    }


    k &= 0xff;
    if (k >= 10) return;
    const Map& m = s.map;
    const std::int32_t hc = static_cast<std::uint32_t>(h.map_x) < static_cast<std::uint32_t>(m.width) &&
                                    static_cast<std::uint32_t>(h.map_y) < static_cast<std::uint32_t>(m.height)
                                ? m.heights[static_cast<std::size_t>(h.map_y) * m.width + h.map_x]
                                : 0;
    const float x = static_cast<float>(ht.flash_x[k]) + (h.x - static_cast<float>(ht.field_140));
    const float y =
        static_cast<float>(ht.flash_y[k]) + ((h.y - static_cast<float>(ht.field_142)) - static_cast<float>(hc));
    const std::uint8_t type = !sides ? 0x28 : (k < static_cast<std::uint32_t>(n >> 1) ? 0x23 : 5);
    if (SimEvent::EffectArgs* e = Event_Effect(s, 3, false, Ftol(x), Ftol(y) + 200, 200, type)) {
        e->rise_step = 0;
        e->on_screen = true, e->view_x = x, e->view_y = y;
    }
}


void RifleShot(GameState& s, std::uint8_t ap, Unit& a, std::uint8_t tp, Unit& t) {
    const UnitType& at = Unit_Type(s, ap, a);
    a.field_09c = 1;  // never cleared by the sim (TUnit_Draw clears it): kept for the hash and the saves, B67
    Event_RifleFlash(s, ap, a);
    a.field_0ee = at.reload[ap];
    if (a.work == 3) {
        if (!a.in_house) {
            a.work = 0xff;
            return;
        }
        if (a.house_player < s.players.size() && a.house_slot < s.players[a.house_player].houses.size()) {
            House_MuzzleFlash(s, a.house_player, s.players[a.house_player].houses[a.house_slot]);
        }
    }
    if (t.is_deleted != 0 || CannotShoot(s, ap, a, tp, t)) {
        TUnit_AttackEnd(s, ap, a);
        return;
    }
    Event_UnitSound(s, ap, a, 2, 4);
    if (t.in_group) {
        if (Group* g = Unit_Group(s, tp, t)) {
            Group_Alert(s, *g, ap, &a);
        } else {
            Unported(s, orig::kGroupAlert);
        }
    } else if (t.player_unit_attack == 0xff && t.player_house_attack == 0xff) {
        AlertNearby(s, ap, t.map_x, t.map_y, ap, a);
    }
    a.field_08c = static_cast<float>(static_cast<double>(a.field_08c) + 1.0);
    const std::uint8_t r = static_cast<std::uint8_t>(Rng_Next(s) % 101);
    const auto d = static_cast<float>(Dist(a, t));
    const double q = static_cast<double>(d) / at.range[ap];
    const double lhs = q * q * r;
    const double rhs = static_cast<double>(at.accuracy[ap]) * static_cast<double>(s.players[ap].accuracy_factor);
    if (rhs < lhs) return;
    const double hp = static_cast<double>(t.hp) - CalcHitDamage(s, ap, a, tp, t);
    t.hp = static_cast<float>(hp);
    if (hp < 1.0) {
        a.field_08c = static_cast<float>(static_cast<double>(a.field_08c) + 10.0);
        OnUnitKilled(s, tp, t, ap);
        TUnit_AttackEnd(s, ap, a);
        return;
    }
    if ((at.move_class == 4 || (a.type == 0xe && s.players[ap].nation == 2)) && a.player_unit_attack != 0xff &&
        a.field_116 != 1) {
        TUnit_OnAttackedBy(s, ap, a, tp, t, 1);
    }
}




void UnitHit(GameState& s, std::uint8_t player, Unit& u, std::uint16_t v, std::uint8_t killer, std::uint8_t flag) {
    if (u.is_deleted != 0) return;
    const UnitType& t = Unit_Type(s, player, u);
    const Player& pl = s.players[player];
    const double a0 = static_cast<double>(v) - static_cast<double>(t.defense[player][2]);
    const float a = a0 < static_cast<double>(0.1f) ? 0.1f : static_cast<float>(a0);        // fst, then the test
    double d = static_cast<double>(a) / ExpFactor(s, player, u);
    if (u.work == 3) {
        double k = static_cast<double>(pl.factor_9b3338);
        if (k > static_cast<double>(1.5f)) k = k + k;
        const House* h =
            u.in_house && u.house_player < s.players.size() && u.house_slot < s.players[u.house_player].houses.size()
                ? &s.players[u.house_player].houses[u.house_slot]
                : nullptr;
        if (h == nullptr) {
            Unported(s, orig::kUnitHit);  // a unit in a house the port does not know
            return;
        }
        if (flag == 0) {
            d = static_cast<double>(0.01f) * d;
        } else if (h->type == 0x15 || h->type == 7 || h->type == 1) {
            d = static_cast<double>(0.3f) / k * d;
        } else {
            d = static_cast<double>(0.5f) / k * d;
        }
    } else if (u.num_work == 1 || u.num_work == 2 || u.num_work == 0x14 || u.num_work == 8 || u.num_work == 0xe ||
               u.num_work == 0x15) {
        d = d * static_cast<double>(0.6f);
    }
    // Entente Engine Plus, F2 (docs/plus-balance.md 2, docs/bugs.md B301): the gas protection research also protects
    // a unit in the open from a gas puff (flag 2): the damage divided by the factor (x1.5 and x1.2, so 1.8 with
    // both). Inside a house the original's rule above stays.
    if (s.plus_rules && flag == 2 && u.work != 3) d = d / static_cast<double>(pl.factor_9b3338);
    if (u.type == 0xb || u.type == 0xc) {
        if (flag != 0) return;
        d = d * static_cast<double>(pl.factor_9e28fc);
    }
    if (t.is_ship != 0) d = d / static_cast<double>(pl.factor_9b32d8);
    if (flag != 0) {
        if (d < 1.0) d = 1.0;
        if (u.type >= 0x15 && u.type <= 0x17 && d < 2.0) d = 2.0;
    }
    const double hp = static_cast<double>(u.hp) - d;
    u.hp = static_cast<float>(hp);
    if (hp < 0.0) {
        OnUnitKilled(s, player, u, killer);
    } else {
        u.field_144 = 10;
    }

}


void MeleeHitUnit(GameState& s, std::uint8_t ap, Unit& a, std::uint8_t tp, Unit& t) {
    const UnitType& tt = Unit_Type(s, tp, t);

    if (t.work == 0x14 || t.work == 3 || t.is_deleted != 0 || (tt.move_class == 3 && t.type != 0x18) ||
        Dist(a, t) > 100.0) {
        TUnit_AttackEnd(s, ap, a);
        return;
    }
    a.field_08c = a.field_08c + 1.0f;
    Event_UnitSound(s, ap, a, 3, 4);
    const UnitType& at = Unit_Type(s, ap, a);
    auto d = static_cast<float>(static_cast<double>(at.attack[ap][0]) - tt.defense[tp][0]);
    if (d < 1.0f) d = 1.0f;
    const auto m = static_cast<float>(ExpFactor(s, ap, a) * d);
    const double hp = static_cast<double>(t.hp) - m / ExpFactor(s, tp, t);
    t.hp = static_cast<float>(hp);
    if (hp < 0.0) {
        a.field_08c = a.field_08c + 10.0f;
        OnUnitKilled(s, tp, t, ap);
        TUnit_AttackEnd(s, ap, a);
    } else {
        t.field_144 = 10;
    }

}






void MeleeHitHouse(GameState& s, std::uint8_t ap, Unit& a) {
    House* h = HouseTarget(s, a);
    if (h == nullptr || h->is_destroyed != 0) {
        TUnit_AttackEnd(s, ap, a);
        return;
    }
    const std::uint8_t owner = a.house_attack_player;
    Event_UnitSound(s, ap, a, 3, 4);
    a.field_08c = a.field_08c + 1.0f;
    const UnitType& at = Unit_Type(s, ap, a);
    const double armour =
        static_cast<double>(House_Durability(s, owner, *h)) * static_cast<double>(s.players[owner].factor_724);
    double x = (static_cast<double>(at.attack[ap][0]) * static_cast<double>(0.5f) - armour) * ExpFactor(s, ap, a);
    if (a.in_group) {
        const Group* g = Unit_Group(s, ap, a);
        if (g != nullptr && g->ai_owned == 0) x *= static_cast<double>(1.5f);
    }
    if (x < 1.0) x = static_cast<double>(0.3f);
    bool down = false;
    if (h->field_020 != 0) {
        h->field_020 = static_cast<std::uint16_t>(Ftol(x + 1.0) + h->field_020);
        down = h->field_020 >= House_Type(s, owner, *h).work_rate;
    } else {
        h->hp = static_cast<float>(static_cast<double>(h->hp) - x);
        down = h->hp < 0.0f;
    }
    if (ap != owner) {

        if (h->field_0a8 > 0 || h->is_destroyed != 0 || s.players[owner].is_lost != 0) {
            --h->field_0a8;
        } else {
            AI_HouseAttacked(s, owner, *h);
        }
    }
    if (down) {
        a.field_08c = a.field_08c + 10.0f;
        House_OnDestroyed(s, owner, *h, ap);
        TUnit_AttackEnd(s, ap, a);
    }
}




void BulletToPoint(GameState& s, float x, float y, std::uint8_t ap, Unit& a, bool any, float r) {
    const Cell c0 = WorldToCell(x, y);
    for (std::uint8_t dir = 0; dir < 9; ++dir) {
        const Cell c = Cell_Step(c0, dir);
        if (c.x < 0 || c.y < 0 || c.x >= s.map.width || c.y >= s.map.height) continue;
        const auto idx = static_cast<std::size_t>(c.y) * s.map.width + static_cast<std::size_t>(c.x);
        for (std::size_t k = 0; k < 40 && k < s.map.cell_units[idx].size(); ++k) {
            const UnitRef ref = s.map.cell_units[idx][k];
            Unit* v = Unit_Find(s, ref);
            if (v == nullptr) {
                Unported(s, orig::kBulletToPoint);  // a stale cell entry
                continue;
            }
            if (v->hp < 0.0f) continue;
            if (!any && ref.player == ap) continue;
            const double dx = static_cast<double>(v->x) - x, dy = static_cast<double>(v->y) - y;
            const double z = v->field_024;
            const double d3 = std::sqrt(z * z + dy * dy + dx * dx);  // compared unrounded
            if (!(d3 < static_cast<double>(r))) continue;
            const double hp = static_cast<double>(v->hp) - CalcHitDamage(s, ap, a, ref.player, *v);
            v->hp = static_cast<float>(hp);
            if (hp < 0.0) {
                a.field_08c = a.field_08c + 10.0f;
                OnUnitKilled(s, ref.player, *v, ap);
            } else {
                v->field_144 = 10;
            }
        }
    }
}



void MgBurst(GameState& s, std::uint8_t ap, Unit& a, std::uint8_t tp, Unit& t) {
    const UnitType& at = Unit_Type(s, ap, a);
    a.field_0ee = at.reload[ap];
    if (a.work == 3) {
        if (!a.in_house) {
            a.work = 0xff;
            return;
        }
        if (a.house_player < s.players.size() && a.house_slot < s.players[a.house_player].houses.size()) {
            House_MuzzleFlash(s, a.house_player, s.players[a.house_player].houses[a.house_slot]);
        }
    }
    if (t.field_024 > 1.0f) {  // an aircraft: an aimed shot instead
        RifleShot(s, ap, a, tp, t);
        return;
    }
    if (t.is_deleted != 0 || CannotShoot(s, ap, a, tp, t)) {
        TUnit_AttackEnd(s, ap, a);
        return;
    }
    Event_UnitSound(s, ap, a, 2, 4);
    if (t.in_group) {  // the target is passed as the attacker (original bug): only the group's cooldown is set
        if (Group* g = Unit_Group(s, tp, t)) {
            Group_Alert(s, *g, tp, &t);
        } else {
            Unported(s, orig::kGroupAlert);
        }
    } else if (t.player_unit_attack == 0xff && t.player_house_attack == 0xff) {
        AlertNearby(s, ap, t.map_x, t.map_y, ap, a);
    }
    const auto r = static_cast<std::uint8_t>(Rng_Next(s) % 101);
    const double acc = static_cast<double>(at.accuracy[ap]) * static_cast<double>(s.players[ap].accuracy_factor);
    if (acc < static_cast<double>(r)) return;
    const auto dxf = static_cast<float>(static_cast<double>(t.x) - a.x);
    const auto dyf = static_cast<float>(static_cast<double>(t.y) - a.y);
    const std::int32_t k = Ftol(Dist(a, t)) / 40 + 1;
    const double inv = static_cast<double>(1.0f) / static_cast<double>(k);
    const auto stepx = static_cast<float>(static_cast<double>(dxf) * inv);
    const double stepy_d = inv * static_cast<double>(dyf);
    const auto stepy = static_cast<float>(stepy_d);
    const auto len = static_cast<float>(
        std::sqrt(stepy_d * static_cast<double>(stepy) + static_cast<double>(stepx) * static_cast<double>(stepx)));
    const float base = k < 5 ? 20.0f : 10.0f;
    const auto tanv = static_cast<float>(dmath::tan(0.39269909262657166));
    float acc_len = 0.0f, cx = a.x, cy = a.y;
    for (std::int32_t i = 0; i < k; ++i) {
        acc_len = static_cast<float>(static_cast<double>(acc_len) + len);
        cx = static_cast<float>(static_cast<double>(cx) + stepx);
        cy = static_cast<float>(static_cast<double>(cy) + stepy);
        const auto h = static_cast<float>(static_cast<double>(tanv) * acc_len + base);
        BulletToPoint(s, cx, cy, ap, a, i > k / 2, h);
    }
    a.field_08c = static_cast<float>(static_cast<double>(a.field_08c) + static_cast<double>(0.3f));
}





void FlameToPoint(GameState& s, std::uint8_t ap, Unit& a, float x, float y, float r, bool any) {
    const Cell c0 = WorldToCell(x, y);
    const UnitType& at = Unit_Type(s, ap, a);
    for (std::uint8_t dir = 0; dir < 9; ++dir) {
        const Cell c = Cell_Step(c0, dir);
        if (c.x < 0 || c.y < 0 || c.x >= s.map.width || c.y >= s.map.height) continue;
        const auto idx = static_cast<std::size_t>(c.y) * s.map.width + static_cast<std::size_t>(c.x);
        const std::uint16_t obj = s.map.objects[idx];
        if (obj >= 1000 && obj < 9000) {  // a house: g_pHouse[obj / 1000 - 1][obj % 1000]
            const std::size_t hp = obj / 1000u - 1u, slot = obj % 1000u;
            if (hp >= s.players.size() || slot >= s.players[hp].houses.size()) {
                Unported(s, orig::kFlameToPoint);  // a house slot the port does not have
            } else if (House& h = s.players[hp].houses[slot]; h.is_destroyed == 0) {
                const auto owner = static_cast<std::uint8_t>(hp);
                const double att = at.attack[ap][4];
                const double def = House_Durability(s, owner, h);  // Plus F7: x2 by the Building Academy
                double d = 1.0;
                if (def < att) d = (static_cast<double>(Rng_Next(s) % 0x65) * static_cast<double>(0.01f)) * (att - def);
                h.hp = static_cast<float>(static_cast<double>(h.hp) - d);
                if (ap != owner) {

                    if (h.field_0a8 > 0 || h.is_destroyed != 0 || s.players[owner].is_lost != 0) {
                        --h.field_0a8;
                    } else {
                        AI_HouseAttacked(s, owner, h);
                    }
                }
                if (h.hp < 1.0f) {
                    h.hp = -10.0f;
                    House_OnDestroyed(s, owner, h, ap);
                }

            }
        }
        for (std::size_t k = 0; k < 40 && k < s.map.cell_units[idx].size(); ++k) {
            const UnitRef ref = s.map.cell_units[idx][k];
            Unit* v = Unit_Find(s, ref);
            if (v == nullptr) {
                Unported(s, orig::kFlameToPoint);  // a stale cell entry
                continue;
            }
            if (v->hp < 0.0f || v == &a) continue;
            if (!any && ref.player == ap) continue;
            const double dx = static_cast<double>(v->x) - x, dy = static_cast<double>(v->y) - y;
            const double z = v->field_024;
            const double d3 = std::sqrt(z * z + dy * dy + dx * dx);  // compared unrounded
            if (!(d3 < static_cast<double>(r))) continue;
            const double hp = static_cast<double>(v->hp) - CalcHitDamage(s, ap, a, ref.player, *v);
            v->hp = static_cast<float>(hp);
            if (hp < 0.0) {
                a.field_08c = a.field_08c + 10.0f;
                OnUnitKilled(s, ref.player, *v, ap);
            }

        }
    }
}







void CombatFireFlameThrower(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    u.field_0ee = t.reload[player];
    if (u.work == 3) {  // "Att->Work==STAY_IN_HOUSE" (a debug message)
        u.work = 0xff;
        TUnit_AttackEnd(s, player, u);
        return;
    }
    float tx = u.field_0d4, ty = u.field_0d8;
    Unit* tg = nullptr;
    if (u.player_unit_attack != 0xff && u.has_attack_unit) {
        tg = AttackTarget(s, u);
        if (tg == nullptr) {
            Unported(s, orig::kFireFlameThrower);  // a dangling target: the original reads a stale pointer
            return;
        }
        if (tg->is_deleted != 0) {
            TUnit_AttackEnd(s, player, u);
            return;
        }

        const double d = Dist(u, *tg);
        if (d > static_cast<double>(t.range[player]) || d < static_cast<double>(40.0f)) {
            TUnit_AttackEnd(s, player, u);
            return;
        }
        FaceToPoint(s, player, u, tg->x, tg->y);
        if (tg->player_unit_attack == 0xff && tg->player_house_attack == 0xff) {
            AlertNearby(s, player, tg->map_x, tg->map_y, player, u);
        }
        tx = tg->x, ty = tg->y;
    }
    if (u.player_house_attack != 0xff && u.has_house_attack) {
        const House* h = HouseTarget(s, u);
        if (h == nullptr) {
            Unported(s, orig::kFireFlameThrower);  // a stale house pointer
            return;
        }
        tx = h->x, ty = h->y;
        if (h->is_destroyed != 0) {
            TUnit_AttackEnd(s, player, u);
            return;
        }
    }
    u.field_08c = u.field_08c + 1.0f;
    // The distance with dy stored as a float, dx kept on the x87 stack; the steps are floats, the step length
    // sqrt(stepy * (float)stepy + stepx * stepx) with stepy unrounded in its first factor (as MgBurst).
    const auto dyf = static_cast<float>(static_cast<double>(u.y) - ty);
    const double dx = static_cast<double>(u.x) - tx;
    const std::int32_t n = Ftol(std::sqrt(dx * dx + static_cast<double>(dyf) * dyf)) / 40;
    const auto k = static_cast<float>(static_cast<double>(1.0f) / static_cast<double>(n + 1));
    const auto stepx = static_cast<float>((static_cast<double>(tx) - u.x) * static_cast<double>(k));
    const double stepy_d = (static_cast<double>(ty) - u.y) * static_cast<double>(k);
    const auto stepy = static_cast<float>(stepy_d);
    const auto len = static_cast<float>(
        std::sqrt(stepy_d * static_cast<double>(stepy) + static_cast<double>(stepx) * static_cast<double>(stepx)));
    constexpr float kTan = 0.55785173f;
    static_assert(std::bit_cast<std::uint32_t>(kTan) == 0x3f0ecf5fu);
    float way = 0.0f, cx = u.x, cy = u.y;
    for (std::int32_t i = 0; i < n + 2; ++i) {
        way = static_cast<float>(static_cast<double>(way) + len);
        FlameToPoint(s, player, u, cx, cy, static_cast<float>(static_cast<double>(kTan) * way), i > 1);
        cx = static_cast<float>(static_cast<double>(cx) + stepx);
        cy = static_cast<float>(static_cast<double>(cy) + stepy);
    }
    if (tg != nullptr && tg->is_deleted != 0) {
        TUnit_AttackEnd(s, player, u);
        return;
    }
    if (u.player_house_attack != 0xff && u.has_house_attack) {
        const House* h = HouseTarget(s, u);
        if (h != nullptr && h->is_destroyed != 0) TUnit_AttackEnd(s, player, u);
    }
}


void TUnit_AttackEnd(GameState& s, std::uint8_t player, Unit& u) {
    if (Air_ResumeAfterFight(s, player, u)) return;  // our rules (docs/bugs.md B277)
    const UnitType& t = Unit_Type(s, player, u);
    if (t.is_air == 0 && u.work != 0x14 && u.work != 3 && u.field_0bc != 0) TUnit_OrderEnd(s, player, u, 1);
    if (TUnit_FindTarget(s, player, u) == 0) {
        u.has_attack_unit = false, u.player_unit_attack = 0xff;
        u.player_house_attack = 0xff, u.has_house_attack = false;
        TUnit_OrderEnd(s, player, u, 1);
    }
}


void TUnit_CheckTargets(GameState& s, std::uint8_t player, Unit& u) {
    if (u.player_unit_attack == 0xff) {
        u.has_attack_unit = false;
    } else if (!u.has_attack_unit) {
        u.player_unit_attack = 0xff;
    } else {
        const Unit* t = AttackTarget(s, u);
        if (t == nullptr || t->is_deleted != 0) {
            TUnit_AttackEnd(s, player, u);
            return;
        }
    }
    if (u.player_house_attack == 0xff) {
        u.has_house_attack = false;
        return;
    }
    if (!u.has_house_attack) {
        u.player_house_attack = 0xff;
        return;
    }
    const House* h = HouseTarget(s, u);
    if (h == nullptr || h->is_destroyed != 0) TUnit_AttackEnd(s, player, u);
}

namespace {




bool CanThrowAt(const GameState& s, std::uint8_t player, const Unit& u, float px, float py) {
    if (u.work == 8) return true;
    const Cell c = WorldToCell(px, py);
    if (c.x < 0 || c.y < 0 || c.x >= s.map.width || c.y >= s.map.height) return false;
    const auto cells = static_cast<std::int64_t>(s.map.objects.size());
    for (std::int32_t x = c.x - 2; x < c.x + 3; ++x) {
        for (std::int32_t y = c.y - 5; y < c.y + 6; ++y) {
            const std::int64_t i = static_cast<std::int64_t>(s.map.width) * y + x;
            if (i < 0 || i >= cells) continue;
            const auto idx = static_cast<std::size_t>(i);
            if (s.map.objects[idx] / 1000 - 1 == player) return false;
            const std::vector<UnitRef>& list = s.map.cell_units[idx];
            for (std::size_t k = 0; k < list.size() && k < 40; ++k) {
                const Unit* w = Unit_Find(s, list[k]);
                if (w == nullptr || w->is_deleted != 0 || IsEnemy(s, player, list[k].player)) continue;
                const double dx = static_cast<double>(w->x) - px, dy = static_cast<double>(w->y) - py;
                if (std::sqrt(dy * dy + dx * dx) < static_cast<double>(400.0f)) return false;
            }
        }
    }
    return true;
}

}  // namespace


bool TUnit_TryAttack(GameState& s, std::uint8_t player, Unit& u, std::uint8_t tp, Unit& t, float d) {
    const UnitType& ut = Unit_Type(s, player, u);
    const UnitType& tt = Unit_Type(s, tp, t);
    const std::uint8_t m = u.field_116;
    bool melee_ok = !(m == 1 || t.work == 3 || t.work == 0x14);
    bool ranged_ok = m != 2;
    bool grenade = (m == 0xff || m == 3) && CanGrenade(ut, u);
    bool no_melee = false;
    if (ut.anim_size[6] != 0) {
        const bool meleeable = (tt.field_468 == 0 || t.type == 0x17 || tt.move_class == 3) && t.work != 0x14 &&
                               t.work != 3 && ut.anim_size[7] != 0 && u.work != 3 && u.work != 0x14;
        if (!meleeable) no_melee = true, ranged_ok = true;
    }
    if (tt.is_air != 0) melee_ok = false, grenade = false;
    bool try_grenade = false;
    if (tt.move_class == 3) {
        if (ut.move_class == 10 || ut.move_class == 3) {
            try_grenade = grenade;
        } else {
            ranged_ok = false, melee_ok = false, no_melee = false;
            if (!grenade && !CanGrenade(ut, u)) return false;
            try_grenade = true;
        }
    } else {
        try_grenade = grenade;
    }

    if (try_grenade && CanGrenade(ut, u) && d < 270.0f && !(d <= 140.0f) &&
        (tt.field_468 != 0 || CanThrowAt(s, player, u, t.x, t.y))) {
        StartGrenade(s, player, u, t.x, t.y);
        u.field_078 = u.num_work;
        TUnit_FaceTowards(s, player, u, t.x, t.y);
        if (u.target_orient == u.orient) u.field_078 = 100;
        u.vel_x = 0, u.vel_y = 0, u.is_stopped = 1;
        return true;
    }
    if (melee_ok && CanMelee(ut, u) && !no_melee) {
        if (d <= 50.0f) {
            FaceAttackTarget(s, player, u);
            StartMelee(s, player, u);
            u.vel_x = 0, u.vel_y = 0, u.is_stopped = 1;
            return true;
        }
        if (static_cast<float>(ut.range[player] >> 2) > d) return false;
        if (ut.move_class == 4 && !TargetClear(s, player, t)) return false;
    }
    if (!ranged_ok) return false;
    if (!(static_cast<float>(ut.range[player]) > d) || ut.anim_size[6] == 0) return false;
    const bool ok = TargetClear(s, player, t);
    if (tt.is_air == 0 && !ok) {
        if (CanMelee(ut, u)) return false;
        TUnit_ClearTargets(u);
        ++u.field_145;
        TUnit_OrderEnd(s, player, u, 1);
        return true;
    }
    if (ut.move_class == 4 && u.field_0ee != 0) return false;
    StartFireAnim(s, player, u);
    u.is_stopped = 1;
    return true;
}

namespace {


void ApproachPoint(GameState& s, std::uint8_t player, Unit& u, std::uint8_t tp, const Unit& t) {
    const double dx = static_cast<double>(t.x) - u.x;
    const double dy = static_cast<double>(t.y) - u.y;
    const auto dyf = static_cast<float>(dy);
    const auto k = static_cast<float>(dy / dx);
    float sx, sy;
    if (std::fabs(dx) < 1.0) {
        sx = 0;
        sy = static_cast<float>(dyf > 0.0f ? static_cast<double>(dyf) - 25.0 : static_cast<double>(dyf) + 25.0);
    } else {
        const double q = static_cast<double>(25.0f) / std::sqrt(static_cast<double>(k) * k + 1.0);
        sx = static_cast<float>(dx > 0.0 ? dx - q : q + dx);
        sy = static_cast<float>(static_cast<double>(sx) * k);
    }
    u.dest_x = static_cast<float>(static_cast<double>(sx) + u.x);
    u.dest_y = static_cast<float>(static_cast<double>(sy) + u.y);
    if (Unit_Type(s, tp, t).field_468 != 0) ApproachPoint_LargeTarget(s, player, u, sx, sy);
}

}  // namespace


void TUnit_OnAttackedBy(GameState& s, std::uint8_t player, Unit& u, std::uint8_t tp, Unit& t, std::uint8_t flag) {
    const UnitType& ut = Unit_Type(s, player, u);
    if (ut.move_class == 3 && ut.anim_size[6] == 0) return;
    u.field_15c = 0;
    MarkZone(s, player, u.x, u.y, 30);
    MarkZone(s, tp, t.x, t.y, 30);
    if (ut.is_ship != 0) {
        Ship_OnAttackedBy(s, player, u, tp, t);
        return;
    }
    if (u.type == 0x18) return;
    const std::uint8_t w = u.work;
    u.field_09f = 0;
    if (w == 0x14 || w == 9 || w == 2 || u.field_142 != 0 || u.field_006 == 0) return;
    if (t.work == 3 && ut.anim_size[6] == 0) return;
    if (w == 0xe || u.field_154 != 0 || w == 5 || w == 0xc || w == 0xd || w == 6 || w == 4) return;
    const bool same =
        u.has_attack_unit && u.attack_ref.player == tp && u.attack_ref.id == t.id && u.attack_ref.serial == t.serial;
    if (!(w == 3 || t.field_141 < 0xd || same)) return;
    if (t.is_deleted != 0) return;
    const auto d = static_cast<float>(Dist(u, t));
    if (ut.move_class == 7 && u.work != 3 && u.in_group) {
        if (flag != 0) SaveOrder(u);
        SetAttackUnit(u, tp, t);
        u.player_unit_attack = tp;
        u.field_093 = 0, u.work = 7, u.field_0ca = 0xffff, u.player_house_attack = 0xff;
        u.attack_unit_id = t.id, u.field_086 = 0;
        if (t.field_141 < 0xe) t.field_141 = static_cast<std::uint8_t>(t.field_141 + 2);
        if (!TUnit_TryAttack(s, player, u, tp, t, d)) TUnit_ClearTargets(u);
        return;
    }
    if (flag != 0) SaveOrder(u);
    if (ut.is_air != 0) {
        Air_OnAttackedBy(s, player, u, tp, t);
        return;
    }
    const UnitType& tt = Unit_Type(s, tp, t);
    if (tt.move_class == 3 && !CanGrenade(ut, u) && u.weapon_count == 0 && ut.move_class != 0xc) {
        TUnit_OrderEnd(s, player, u, 1);
        return;
    }
    if (u.type == 0xd && (static_cast<float>(ut.range[player] - 100) < d || tt.is_air == 0)) {
        TUnit_ClearTargets(u);
        return;
    }
    if (u.work != 3 && (u.field_116 == 1 || u.field_116 == 3 || (ut.field_5e0 != 0 && KeepsDistance(u))) &&
        static_cast<double>(d) <
            static_cast<double>(static_cast<float>(ut.range[player])) * static_cast<double>(0.2f)) {
        float mx, my;
        if (d <= 10.0f) {
            mx = 400.0f, my = 0;
        } else {
            const double k = static_cast<double>(1.0f) / (static_cast<double>(d) * static_cast<double>(0.0025f));
            mx = static_cast<float>((static_cast<double>(u.x) - t.x) * k);
            my = static_cast<float>((static_cast<double>(u.y) - t.y) * k);
        }
        TUnit_ClearTargets(u);
        TUnit_OrderMove(s, player, u, static_cast<float>(static_cast<double>(mx) + u.x),
                        static_cast<float>(static_cast<double>(my) + u.y), 1);
        return;
    }
    SetAttackUnit(u, tp, t);
    u.player_unit_attack = tp;
    if (t.field_141 < 0xe) t.field_141 = static_cast<std::uint8_t>(t.field_141 + 2);
    if (!(t.field_024 <= 1.0f) && ut.field_006 == 0 && u.type != 0xd) {
        TUnit_ClearTargets(u);
        return;
    }
    if (u.work == 3) {
        if (d < static_cast<float>(ut.range[player] + 200) && ut.anim_size[6] != 0) {
            StartFireAnim(s, player, u);
            SetAttackUnit(u, tp, t);
            u.player_unit_attack = tp, u.player_house_attack = 0xff, u.has_house_attack = false;
            return;
        }
        TUnit_ClearTargets(u);
        return;
    }
    u.field_093 = 0;
    u.work = (u.type == 0 || ut.is_ship != 0) ? 0xff : 7;
    u.field_0ca = 0xffff;
    SetAttackUnit(u, tp, t);
    u.player_unit_attack = tp, u.player_house_attack = 0xff, u.attack_unit_id = t.id, u.field_086 = 0;
    if (TUnit_TryAttack(s, player, u, tp, t, d)) return;
    if (u.field_116 == 4) {
        TUnit_ClearTargets(u);
        TUnit_StopMoving(s, player, u);
        return;
    }
    if (u.in_group) u.field_106 = 1;
    if (ut.anim_size[6] == 0 && !CanMelee(ut, u)) {
        if (u.weapon_count != 0) u.is_stopped = 0;
        return;
    }
    const float sx = u.dest_x, sy = u.dest_y;
    ApproachPoint(s, player, u, tp, t);
    const float px = u.dest_x, py = u.dest_y;
    u.dest_x = sx, u.dest_y = sy;
    TUnit_MoveTo(s, player, u, px, py, 0);
}



void TUnit_AttackHouse(GameState& s, std::uint8_t player, Unit& u, const Unit& src, std::uint8_t hp, std::uint16_t slot,
                       std::uint16_t k, std::uint8_t flag) {
    const UnitType& ut = Unit_Type(s, player, u);
    if (ut.move_class == 3 && ut.anim_size[6] == 0) return;
    if (u.field_154 != 0) return;
    House& h = s.players[hp].houses[slot];
    u.field_15c = 0;
    MarkZone(s, player, u.x, u.y, 30);
    MarkZone(s, player, h.x, h.y, 30);
    if (u.type == 0x18) return;
    u.field_09f = 0;
    if (u.work == 9 || u.work == 2) return;
    if (!CanAttackHouses(ut, u)) return;
    TUnit_ClearTargets(u);
    if (u.work == 3 || u.work == 0x14) return;
    std::uint8_t mc = ut.move_class;
    const auto d = static_cast<float>(DistPt(u.x, u.y, h.x, h.y));
    if (mc == 10 || mc == 3) {
        if (u.field_116 == 4 && !(static_cast<float>(ut.range[player]) - 50.0f > d)) return;
    } else if (u.field_116 == 4 && 400.0f < d) {
        return;
    }
    u.field_093 = 0;
    u.player_house_attack = hp;
    u.has_house_attack = true, u.house_attack_player = hp, u.house_attack_slot = slot;
    u.field_006 = 1;
    if (ut.is_air != 0) {
        Air_AttackHouse(s, player, u, h, flag);
        return;
    }
    if (flag != 0) SaveOrder(u);
    if (ut.is_ship == 0 && mc != 3 && mc != 5 && mc != 8 && mc != 10) mc = 6;
    float tx = 0, ty = 0;
    std::uint8_t move_flag = 0;
    switch (mc) {
        case 6: {
            const HouseFootprint& fp = House_Footprint(s, hp, h);
            const std::uint16_t hid = static_cast<std::uint16_t>((hp + 1) * 1000 + h.num);
            std::vector<float> dist;
            std::vector<std::array<std::int32_t, 2>> pt;
            Cell row{h.map_x, h.map_y + 2};
            for (int r = 0; r < fp.b + 2; ++r) {
                Cell c = row;
                for (int kk = 0; kk < fp.a + 2; ++kk) {
                    const bool on_map = c.x >= 0 && c.y >= 0 && c.x < s.map.width && c.y < s.map.height;
                    const std::size_t i = on_map ? static_cast<std::size_t>(c.y) * s.map.width + c.x : 0;
                    if (on_map && s.map.pass_land[i] == 0 && s.map.objects[i] != hid) {
                        std::int32_t px = (c.x * 2 - (~c.y & 1)) * 48, py = c.y * 24;
                        if (kk == 0) {
                            if (r == 0) {
                                px += 0x30, py -= 0x19;
                            } else {
                                px += 0x54;
                            }
                        } else if (kk != fp.a - 1) {
                            if (r == fp.b - 1) {
                                py += 0x13, px += 0x30;
                            } else if (r == 0) {
                                px += 1;
                            }
                        } else if (r == 0) {
                            px += 1;
                        } else if (r == fp.b - 1) {
                            px -= 2, py += 0x17;
                        } else {
                            py -= 10;
                        }
                        const double dx = static_cast<double>(src.x) - px, dy = static_cast<double>(src.y) - py;
                        dist.push_back(static_cast<float>(std::sqrt(dx * dx + dy * dy)));
                        pt.push_back({px, py});
                    }
                    c = Cell_Step(c, 1);
                }
                row = Cell_Step(row, 4);
            }
            if (dist.size() <= k) return;
            for (std::size_t i = 0; i + 1 < dist.size(); ++i) {
                std::size_t m = i;
                float best = dist[i];
                for (std::size_t j = i + 1; j < dist.size(); ++j) {
                    if (dist[j] < best) best = dist[j], m = j;
                }
                if (m != i) std::swap(dist[i], dist[m]), std::swap(pt[i], pt[m]);
                if (i == k) break;
            }
            tx = static_cast<float>(pt[k][0]), ty = static_cast<float>(pt[k][1]);
            move_flag = 0;
            break;
        }
        case 10:
            if (d < static_cast<float>(ut.range[player] - 100)) {
                StartFireAnim(s, player, u);
                FaceToPoint(s, player, u, h.x, h.y);
                return;
            }
            [[fallthrough]];
        default:
            if (u.weapon_count != 0 && DistPt(h.x, h.y, u.x, u.y) < static_cast<double>(ut.range[player])) {
                TUnit_StopMoving(s, player, u);
                StartFireAnim(s, player, u);
                FaceToPoint(s, player, u, h.x, h.y);
                return;
            }
            if (u.work == 3) return;
            tx = h.exit_x, ty = h.exit_y;
            move_flag = 1;
            break;
        case 1:
        case 11:
        case 12: Unported(s, orig::kAttackHouse); return;  // aircraft
        case 13: TUnit_ClearTargets(u); return;
        case 14:
        case 15: Ship_AttackHouse(s, player, u, h); return;
    }
    u.field_086 = 0;
    u.work = 0xff;
    TUnit_MoveTo(s, player, u, tx, ty, move_flag);
    if (u.field_142 < 0x3c) u.field_142 = 0x3c;
}



int TUnit_FindTarget(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& ut = Unit_Type(s, player, u);
    if (u.field_006 == 0) return 0;
    if (ut.move_class == 3 && ut.anim_size[6] == 0) return 0;
    switch (u.work) {
        case 0xe:
        case 0x14:
        case 8:
        case 9:
        case 5:
        case 0xc:
        case 0xd:
        case 6:
        case 4:
        case 2: return 0;
        default: break;
    }
    if (u.field_154 != 0 || u.field_142 != 0) return 0;
    if (ut.move_class == 11) return Air_FindTarget(s, player, u);
    if (ut.is_ship != 0) {
        const bool r = Ship_FindTarget(s, player, u);
        MarkZone(s, player, u.x, u.y, 30);
        if (r) return 1;
    }
    if (s.unit_cells.empty()) return 0;
    int maxc = 5;
    const std::int32_t n = AliveUnits(s);
    if (n > 5000) {
        maxc = 1;
    } else if (n > 2000) {
        maxc = (5000 - n) / 1000 + 1;
    }
    const bool is13 = u.type == 13;
    bool air_ok = true;
    if (u.work == 3 && u.in_house && u.house_player < s.players.size() &&
        u.house_slot < s.players[u.house_player].houses.size()) {
        const std::uint16_t ht = s.players[u.house_player].houses[u.house_slot].type;
        if (ht == 1 || ht == 7) air_ok = false;
    }
    if (u.type == 15 || u.type == 0 || ut.move_class == 4 || ut.move_class == 10 || ut.move_class == 3 ||
        ut.is_ship != 0) {
        air_ok = false;
    }
    if (is13) air_ok = true;
    bool chk_house = is13 ? false : CanAttackHouses(ut, u);
    if (u.work == 3) chk_house = false;
    const auto range = static_cast<float>(ut.range[player]);
    const std::int32_t gw = s.grid_w;
    std::int32_t a = GridCellClamped(s, static_cast<float>(u.x - range), static_cast<float>(u.y - range));
    std::int32_t b = GridCellClamped(s, static_cast<float>(u.x + range), static_cast<float>(u.y + range));
    std::int32_t row_a = a / gw, col_a = a % gw, row_b = b / gw, col_b = b % gw;
    float min_d = 0;
    if (ut.move_class == 10) {
        if (u.weapon_count != 0 && !ut.weapons.empty()) {
            min_d = ut.weapons[0].min_range;
        } else {
            min_d = static_cast<float>(static_cast<double>(range) * static_cast<double>(0.2f));
        }
    }
    float lim = range;
    if (ut.move_class == 6) {
        const std::int32_t c = GridCellClamped(s, u.x, u.y);
        row_a = row_b = c / gw, col_a = col_b = c % gw;
        maxc = 2;
        lim = 150.0f;
    }
    float h_best = lim;
    if (u.player_unit_attack != 0xff && u.has_attack_unit) {
        const Unit* t = AttackTarget(s, u);
        if (t == nullptr) {
            Unported(s, orig::kFindTarget);  // a dangling target
            return 0;
        }
        const std::int32_t c = GridCellClamped(s, t->x, t->y);
        row_a = row_b = c / gw, col_a = col_b = c % gw;
        maxc = 2;
        lim = 1000000.0f;
    }
    // Entente Engine Plus, A1 counter-battery (docs/plus-balance.md 4): a shooter of MoveClass 9, 10 or 12 without a
    // target also keeps the nearest enemy gun (MoveClass 10: mortar man, howitzer, cannon, fixed howitzer) in its
    // range and beyond its minimum range, and takes it before houses and other units, with no random pick. A5 (stage
    // 5): cavalry (MoveClass 4) and tanks (MoveClass 3 but the anti-aircraft vehicle) hunt guns the same way, within
    // their own range.
    const std::uint8_t mc = ut.move_class;
    const bool hunt = s.plus_rules && !is13 && (mc == 9 || mc == 10 || mc == 12 || mc == 4 || mc == 3) && lim == range;
    UnitRef gun{};
    double gun_d = 0;
    bool has_gun = false;
    std::array<UnitRef, 10> list{};
    int cnt = 0;
    const House* house = nullptr;
    std::uint8_t house_player = 0;
    std::uint16_t house_slot = 0;
    const std::int32_t cells = gw * s.grid_h;
    for (std::int32_t col = col_a; col <= col_b; ++col) {
        for (std::int32_t row = row_a; row < row_b; ++row) {
            const std::int32_t c = row * gw + col;
            if (chk_house && c >= 0 && c <= cells) {
                for (const HouseRef& hr : s.house_cells[static_cast<std::size_t>(c)]) {
                    const House& h = s.players[hr.player].houses[hr.slot];
                    if (h.is_destroyed != 0 || !IsEnemy(s, player, hr.player)) continue;
                    const double d = DistPt(h.x, h.y, u.x, u.y);
                    if (d < h_best && !(d < min_d)) {
                        h_best = static_cast<float>(d), house = &h, house_player = hr.player, house_slot = hr.slot;
                    }
                }
            }
            if (c < 0 || c > cells) continue;
            for (const UnitRef& r : s.unit_cells[static_cast<std::size_t>(c)]) {
                if (!IsEnemy(s, player, r.player)) continue;
                const Unit* t = Unit_Find(s, r);
                if (t == nullptr) {
                    Unported(s, orig::kFindTargetCellScan);  // a unit freed since the grid was built
                    continue;
                }
                const double d = Dist(*t, u);
                const auto df = static_cast<float>(d);
                if (hunt && d < static_cast<double>(range) && !(df < min_d) && (!has_gun || d < gun_d) &&
                    t->field_141 < 13 && t->is_deleted == 0 && t->work != 3 && t->work != 0x14 &&
                    Unit_Type(s, r.player, *t).move_class == 10) {
                    gun = r, gun_d = d, has_gun = true;
                }
                if (!(d < lim) || df < min_d) continue;
                if (t->field_141 >= 13 || t->is_deleted != 0 || t->work == 3) continue;
                const UnitType& tt = Unit_Type(s, r.player, *t);
                if (is13 && tt.is_air == 0) continue;
                if (!air_ok && tt.is_air != 0) continue;
                if (t->work == 0x14) continue;
                if (df < 50.0f && t->field_024 < 1.0f) {
                    cnt = 1;
                    list[0] = r;
                    goto post;
                }
                if (cnt < maxc) {
                    list[static_cast<std::size_t>(cnt++)] = r;
                    continue;
                }
                for (int j = 0; j < maxc; ++j) {  // no break: the original inserts again and duplicates entries
                    const Unit* lj = Unit_Find(s, list[static_cast<std::size_t>(j)]);
                    if (lj != nullptr && Dist(*lj, u) > df) {
                        for (int q = maxc - 1; q > j; --q)
                            list[static_cast<std::size_t>(q)] = list[static_cast<std::size_t>(q - 1)];
                        list[static_cast<std::size_t>(j)] = r;
                    }
                }
                const Unit* last = Unit_Find(s, list[static_cast<std::size_t>(maxc - 1)]);
                if (last != nullptr) lim = static_cast<float>(Dist(*last, u));
            }
        }
    }
post:
    int idx13 = -1;
    if (u.type == 9) {
        for (int i = 0; i < cnt; ++i) {
            const Unit* t = Unit_Find(s, list[static_cast<std::size_t>(i)]);
            if (t != nullptr && t->type == 13) {
                idx13 = i;
                break;
            }
        }
    }
    auto attack_unit = [&](const UnitRef& r) {
        Unit* t = Unit_Find(s, r);
        if (t != nullptr) TUnit_OnAttackedBy(s, player, u, r.player, *t, 1);
    };
    if (has_gun) {  // A1, Plus only
        attack_unit(gun);
        return 1;
    }
    if (cnt == 0 && house == nullptr) return 0;
    if (mc < 9 || (mc > 10 && mc != 12)) {
        if (cnt != 0) {
            const int r = Rng_Next(s) % cnt;
            attack_unit(list[static_cast<std::size_t>(r)]);
            return 1;
        }
        const HouseFootprint& fp = House_Footprint(s, house_player, *house);
        const int r = Rng_Next(s) % (fp.b + fp.a + 3);
        TUnit_AttackHouse(s, player, u, u, house_player, house_slot, static_cast<std::uint16_t>(r), 1);
        return 1;
    }
    if (idx13 >= 0) {
        attack_unit(list[static_cast<std::size_t>(idx13)]);
        return 1;
    }
    if (house != nullptr) {
        TUnit_AttackHouse(s, player, u, u, house_player, house_slot, 0, 1);
        return 1;
    }
    std::array<UnitRef, 10> ground{};
    int k = 0;
    for (int i = 0; i < cnt; ++i) {
        const Unit* t = Unit_Find(s, list[static_cast<std::size_t>(i)]);
        if (t != nullptr && t->field_024 < 1.0f)
            ground[static_cast<std::size_t>(k++)] = list[static_cast<std::size_t>(i)];
    }
    if (k == 0) return 0;
    const int r = Rng_Next(s) % k;
    attack_unit(ground[static_cast<std::size_t>(r)]);
    return 1;
}


void TStrateg_AutoTarget(GameState& s, std::uint8_t player, Unit& u) {
    Player& pl = s.players[player];
    const std::int32_t i = (Ftol(u.y) / 300) * pl.zone_width + Ftol(u.x) / 300;
    if (i < 0 || i >= static_cast<std::int32_t>(pl.zone_grid.size())) return;
    if (u.type == 13) {
        TUnit_FindTarget(s, player, u);
        return;
    }
    const std::int32_t n = AliveUnits(s);
    std::uint8_t al = 30, bl = 30;
    if (n > 10000) {
        bl = 100, al = 5;
    } else if (n > 1000) {
        const std::int32_t k = n / 1000;
        bl = static_cast<std::uint8_t>(7 * k + 30);
        al = static_cast<std::uint8_t>(
            Ftol(static_cast<double>(30.0f) - static_cast<double>(k) * static_cast<double>(2.5f)));
    }
    std::uint8_t& c = pl.zone_grid[static_cast<std::size_t>(i)];
    if (u.player_unit_attack != 0xff || u.player_house_attack != 0xff) {
        c = al;
        return;
    }
    const std::uint8_t v = c;
    if (v > 100) {
        c = static_cast<std::uint8_t>(v - 1);
        return;
    }
    if (v == 0 || v == 100) {
        c = static_cast<std::uint8_t>(bl + 100);
    } else {
        c = static_cast<std::uint8_t>(v - 1);
    }
    TUnit_FindTarget(s, player, u);
}

}  // namespace antanta::sim
