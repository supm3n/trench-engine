



#include "plus_army.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "market.hpp"
#include "movement.hpp"
#include "plus_brain.hpp"

namespace antanta::sim {

namespace {

constexpr double kPrice[5] = {1.0, 3.0, 7.5, 15.0, 7.5};  // the market's selling price in food (FeUnit, no index)
constexpr double kHome = 1500.0;                          // a unit this near an own standing house is at home
constexpr double kIncoming = 2000.0;                      // an enemy group this near our or an ally's house: a threat
constexpr double kCluster = 1500.0;                       // one threat: enemies within this of its first unit
constexpr double kAnswered = 2000.0;                      // our busy units this near a threat answer it
constexpr double kNextTarget = 3000.0;                    // a field group takes an enemy house this near
constexpr std::size_t kWave = 4 * 126;                    // four groups of AI_FormGroups' 126
constexpr std::size_t kMinWave = 60;                      // a wave: at least this many, or a tenth of the army
constexpr std::size_t kMinField = 30;                     // a crowd in the field goes on alone from this size
constexpr double kStaged = 3000.0;                        // a unit this near the staging point waits there
constexpr double kBaseEdge = 5000.0;                      // the staging point's house: this near the base
constexpr double kMerge = 4000.0;                         // a small crowd joins a field army this near
constexpr std::size_t kThreats = 16;

double D2(double ax, double ay, double bx, double by) {
    const double dx = ax - bx, dy = ay - by;
    return dx * dx + dy * dy;
}

bool Standing(const House& h) { return h.is_destroyed == 0; }

// A unit of player q's cost in food-equivalent at the market's base prices (both sides measured alike).
double Fe(const GameState& s, std::uint8_t q, const Unit& u) {
    const UnitType& t = Unit_Type(s, q, u);
    double v = 0;
    for (int r = 0; r < 5; ++r) v += static_cast<double>(t.cost[static_cast<std::size_t>(r)]) * kPrice[r];
    return v;
}

// A fighting land unit: it may join a group (B+9), not an aircraft or a ship.
bool Soldier(const GameState& s, std::uint8_t p, const Unit& u) {
    const UnitType& t = Unit_Type(s, p, u);
    return u.is_deleted == 0 && t.field_009 != 0 && t.is_air == 0 && t.is_ship == 0;
}

// AI_Think's idle test (sim/ai.cpp Idle), and the unit stands on a free cell: one on a closed cell (a house's
// footprint, a vehicle's mark) cannot walk and would hold its group's march (docs/ai.md 11).
bool Idle(const GameState& s, std::uint8_t p, const Unit& u) {
    if (u.work != 0xff || u.in_group || u.player_unit_attack != 0xff || u.player_house_attack != 0xff ||
        u.is_deleted != 0 || !(Unit_Type(s, p, u).speed[p] > 0.5f) || u.field_154 != 0) {
        return false;
    }
    const std::size_t k = static_cast<std::size_t>(u.map_y) * static_cast<std::size_t>(s.map.width) + u.map_x;
    return k < s.map.pass_land.size() && s.map.pass_land[k] == 0;
}



bool Inside(const Unit& u) { return u.work == 3 && u.in_house; }

// Player t is p or a living ally of p.
bool Ours(const GameState& s, std::uint8_t p, std::uint8_t t) {
    return t == p || (t < s.players.size() && s.players[t].is_lost == 0 && !IsEnemy(s, p, t));
}

// Points bucketed in squares of `cell` world units, for "within r" tests with r <= cell.
struct Near {
    double cell;
    std::int32_t gw, gh;
    std::vector<std::vector<std::pair<double, double>>> b;
    Near(const GameState& s, double c)
        : cell(c),
          gw(static_cast<std::int32_t>(s.map.width * 96.0 / c) + 2),
          gh(static_cast<std::int32_t>(s.map.height * 24.0 / c) + 2),
          b(static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh)) {}
    std::int32_t Gx(double x) const { return std::clamp(static_cast<std::int32_t>(x / cell), 0, gw - 1); }
    std::int32_t Gy(double y) const { return std::clamp(static_cast<std::int32_t>(y / cell), 0, gh - 1); }
    void Add(double x, double y) { b[static_cast<std::size_t>(Gy(y) * gw + Gx(x))].emplace_back(x, y); }
    bool Within(double x, double y, double r) const {
        const std::int32_t cx = Gx(x), cy = Gy(y);
        for (std::int32_t j = std::max(cy - 1, 0); j <= std::min(cy + 1, gh - 1); ++j) {
            for (std::int32_t i = std::max(cx - 1, 0); i <= std::min(cx + 1, gw - 1); ++i) {
                for (const auto& [px, py] : b[static_cast<std::size_t>(j * gw + i)]) {
                    if (D2(px, py, x, y) <= r * r) return true;
                }
            }
        }
        return false;
    }
};

// The idle soldiers of p in unit list order.
std::vector<Unit*> IdleSoldiers(GameState& s, std::uint8_t p) {
    std::vector<Unit*> v;
    Player& pl = s.players[p];
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        Unit& u = pl.unit_slots[k];
        if (Soldier(s, p, u) && Idle(s, p, u)) v.push_back(&u);
    }
    return v;
}

// v ordered by the squared distance to (x, y); ties keep the list order.
void ByDistance(std::vector<Unit*>& v, double x, double y) {
    std::vector<std::pair<double, std::size_t>> d(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) d[i] = {D2(v[i]->x, v[i]->y, x, y), i};
    std::sort(d.begin(), d.end());
    std::vector<Unit*> out(v.size());
    for (std::size_t i = 0; i < d.size(); ++i) out[i] = v[d[i].second];
    v.swap(out);
}

struct Threat {
    std::uint8_t player = 0xff;  // its first unit: the one the defenders attack
    std::uint16_t id = 0;
    double ax = 0, ay = 0;  // that unit's point
    double fe = 0;          // the threat's cost
    double answered = 0;    // ours already answering it
};

// The enemy units attacking our or an ally's houses or units, or marching in a group within 2,000 of such a house,
// as threats of the units within 1,500 of each threat's first unit (at most 16), with what already answers each.
std::vector<Threat> Threats(const GameState& s, std::uint8_t p) {
    std::vector<Threat> out;
    Near homes(s, kIncoming);
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        if (!Ours(s, p, static_cast<std::uint8_t>(q))) continue;
        for (const House& h : s.players[q].houses) {
            if (Standing(h)) homes.Add(h.x, h.y);
        }
    }
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        const auto qp = static_cast<std::uint8_t>(q);
        const Player& e = s.players[q];
        if (e.is_lost != 0 || !IsEnemy(s, p, qp)) continue;
        for (std::uint32_t k = e.first_unit; k != kNoSlot; k = e.unit_slots[k].next) {
            const Unit& u = e.unit_slots[k];
            if (!Soldier(s, qp, u) || Inside(u) || u.in_transport) continue;
            const bool hits = (u.player_unit_attack != 0xff && Ours(s, p, u.player_unit_attack)) ||
                              (u.player_house_attack != 0xff && Ours(s, p, u.player_house_attack));
            if (!hits && !(u.in_group && homes.Within(u.x, u.y, kIncoming))) continue;
            Threat* t = nullptr;
            for (Threat& c : out) {
                if (D2(c.ax, c.ay, u.x, u.y) <= kCluster * kCluster) {
                    t = &c;
                    break;
                }
            }
            if (t == nullptr) {
                if (out.size() == kThreats) continue;
                out.push_back(Threat{qp, u.id, u.x, u.y});
                t = &out.back();
            }
            t->fe += Fe(s, qp, u);
        }
    }
    if (out.empty()) return out;
    // Already answering: our groups heading to the threat, and our other busy soldiers near it (fighting there).
    const Player& pl = s.players[p];
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        const Unit& u = pl.unit_slots[k];
        if (!Soldier(s, p, u) || u.in_group || Inside(u) || u.in_transport ||
            (u.work == 0xff && u.player_unit_attack == 0xff && u.player_house_attack == 0xff)) {
            continue;
        }
        for (Threat& t : out) {
            if (D2(t.ax, t.ay, u.x, u.y) <= kAnswered * kAnswered) {
                t.answered += Fe(s, p, u);
                break;
            }
        }
    }
    for (const Group& g : pl.groups) {
        if (g.dead != 0) continue;
        for (Threat& t : out) {
            if (D2(t.ax, t.ay, g.tx, g.ty) > kAnswered * kAnswered) continue;
            for (const UnitRef& r : g.members) {
                if (const Unit* u = Unit_Find(s, r)) t.answered += Fe(s, p, *u);
            }
            break;
        }
    }
    return out;
}

double Weight(const GameState& s, std::uint8_t q, const House& h) {
    if (h.type == 6) return 2.0;                                   // a town centre
    if (h.type == 10 || h.type == 11 || h.type == 13) return 1.5;  // iron and gold mines, oil wells
    const std::uint16_t first = House_Type(s, q, h).produce[0];
    return first != 0xffff && first != 0 ? 1.5 : 1.0;  // a house that trains soldiers or vehicles
}

// The enemy house with the least distance / weight from (x, y), within `limit` of it when limit > 0.
PlusTarget BestHouse(const GameState& s, std::uint8_t p, double x, double y, double limit) {
    PlusTarget t;
    double best = 0;
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        const auto qp = static_cast<std::uint8_t>(q);
        if (s.players[q].is_lost != 0 || !IsEnemy(s, p, qp)) continue;
        const std::vector<House>& hs = s.players[q].houses;
        for (std::size_t k = 0; k < hs.size(); ++k) {
            const House& h = hs[k];
            if (!Standing(h)) continue;
            const double d2 = D2(h.x, h.y, x, y);
            if (limit > 0.0 && d2 > limit * limit) continue;
            const double v = std::sqrt(d2) / Weight(s, qp, h);
            if (t.kind == 0 || v < best) {
                best = v;
                t.kind = 2, t.player = qp, t.id = static_cast<std::uint16_t>(k);
                t.x = Ftol(h.x), t.y = Ftol(h.y);
            }
        }
    }
    return t;
}

// The groups for list (kind 2 or 3: attack the target; 1: walk to the point), the order of AI_Think's own call:
// a3 4 runs the staging probe and gives the target as the follow-up order.
void Send(GameState& s, std::uint8_t p, std::vector<Unit*> list, std::uint8_t a3, std::uint8_t kind,
          const PlusTarget& t) {
    if (list.empty()) return;
    std::int32_t x = t.x, y = t.y;
    AI_FormGroups(s, p, std::move(list), a3, 2, 3, kind, x, y, kind == 1 ? 0 : t.player, kind == 1 ? 0 : t.id, 1, false,
                  true);
}

// What of list (its first unit leads) AI_FormGroups would put in groups of 30 or more: the crowds within 2,000 of
// their first unit, the cavalry apart (its groups take cavalry only, 31 at most), each cut to whole groups (126, the
// cavalry's 31) when the last one would be smaller than 30. The rest stays where it is.
std::vector<Unit*> Compact(const GameState& s, std::uint8_t p, std::vector<Unit*> list) {
    std::vector<Unit*> out;
    while (!list.empty()) {
        const Unit& first = *list[0];
        std::vector<Unit*> crowd, rest;
        for (Unit* u : list) (D2(first.x, first.y, u->x, u->y) < 2000.0 * 2000.0 ? crowd : rest).push_back(u);
        list.swap(rest);
        for (const bool cavalry : {false, true}) {
            std::vector<Unit*> part;
            for (Unit* u : crowd) {
                if ((Unit_Type(s, p, *u).move_class == 4) == cavalry) part.push_back(u);
            }
            const std::size_t g = cavalry ? 31 : 126;
            std::size_t n = part.size();
            if (n > g && n % g < kMinField) n -= n % g;
            if (n >= kMinField) out.insert(out.end(), part.begin(), part.begin() + static_cast<std::ptrdiff_t>(n));
        }
    }
    return out;
}

// The own standing houses of p, for the "at home" test (within 1,500 of one).
Near Homes(const GameState& s, std::uint8_t p) {
    Near home(s, kHome);
    for (const House& h : s.players[p].houses) {
        if (Standing(h)) home.Add(h.x, h.y);
    }
    return home;
}

// The minimum wave: the larger of 60 and a tenth of the soldiers alive, at most four groups; an army under 240 waits
// for a quarter of itself.
std::size_t MinWave(const GameState& s, std::uint8_t p) {
    const Player& pl = s.players[p];
    std::size_t n = 0;
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        n += Soldier(s, p, pl.unit_slots[k]) ? 1 : 0;
    }
    return std::min(kWave, std::max(std::min(kMinWave, n / 4), n / 10));
}

// The staging point, where the soldiers above the home guard gather until a wave is full: 300 from the door of the
// own house nearest to the enemy house the waves go for (BestHouse from the base), among the houses within 5,000 of
// the base, toward that enemy house; the base itself when no enemy house stands. Taken from the house target, not from
// a threat, so it stays put while the threats come and go.
PlusTarget Staging(const GameState& s, std::uint8_t p) {
    double bx = 0, by = 0;
    if (!PlusArmy_Base(s, p, bx, by)) return {};
    const PlusTarget t = BestHouse(s, p, bx, by, 0.0);
    PlusTarget out;
    out.kind = 1, out.x = Ftol(bx), out.y = Ftol(by);
    if (t.kind == 0) return out;
    const double tx = t.x, ty = t.y;
    const House* best = nullptr;
    double bd = 0;
    for (const House& h : s.players[p].houses) {
        if (!Standing(h) || D2(h.x, h.y, bx, by) > kBaseEdge * kBaseEdge) continue;
        const double d = D2(h.x, h.y, tx, ty);
        if (best == nullptr || d < bd) best = &h, bd = d;
    }
    double rx = best != nullptr ? best->exit_x : bx, ry = best != nullptr ? best->exit_y : by;
    const double d = std::sqrt(D2(rx, ry, tx, ty));
    if (d > 300.0) rx += (tx - rx) * 300.0 / d, ry += (ty - ry) * 300.0 / d;
    out.x = Ftol(rx), out.y = Ftol(ry);
    return out;
}

// A field army to join: one of our groups of `min` or more living members heading away from home (its point), or a
// big crowd that just left for an enemy house. (x, y): where it is now (its first member).
struct FieldArmy {
    double x = 0, y = 0;
    PlusTarget to;
};

std::vector<FieldArmy> FieldArmies(const GameState& s, std::uint8_t p, const Near& home, std::size_t min) {
    std::vector<FieldArmy> out;
    for (const Group& g : s.players[p].groups) {
        if (g.dead != 0 || g.live < min || home.Within(g.tx, g.ty, kHome)) continue;
        for (const UnitRef& r : g.members) {
            if (const Unit* u = Unit_Find(s, r)) {
                FieldArmy a;
                a.x = u->x, a.y = u->y, a.to.kind = 1, a.to.x = g.tx, a.to.y = g.ty;
                out.push_back(a);
                break;
            }
        }
    }
    return out;
}

// The nearest of the armies within 4,000 of (x, y), or nullptr.
const FieldArmy* NearestArmy(const std::vector<FieldArmy>& armies, double x, double y) {
    const FieldArmy* best = nullptr;
    double bd = kMerge * kMerge;
    for (const FieldArmy& a : armies) {
        const double d = D2(a.x, a.y, x, y);
        if (d <= bd) best = &a, bd = d;
    }
    return best;
}

// The idle soldiers away from home, as crowds within 2,000 of their first unit (as AI_FormGroups splits). A crowd of
// a quarter of the minimum wave (30 or more) attacks the enemy house nearest to it within 3,000 when the player
// attacks, else walks back to the staging point; a smaller one joins the nearest field army within 4,000 (it walks to
// that army's point), else walks back to the staging point too, so no handful of stragglers wanders on alone. A
// threat's answer thus comes back once its threat is gone.
void Field(GameState& s, std::uint8_t p) {
    const Player& pl = s.players[p];
    const Near home = Homes(s, p);
    std::vector<Unit*> away;
    for (Unit* u : IdleSoldiers(s, p)) {
        if (!home.Within(u->x, u->y, kHome)) away.push_back(u);
    }
    if (away.empty()) return;
    const bool attacks = pl.field_9b2a98[1] != 0 && pl.ai_kind != 1;
    std::vector<std::vector<Unit*>> crowds;
    while (!away.empty()) {
        const Unit& first = *away[0];
        std::vector<Unit*> crowd, rest;
        for (Unit* u : away) (D2(first.x, first.y, u->x, u->y) < 2000.0 * 2000.0 ? crowd : rest).push_back(u);
        away.swap(rest);
        crowds.push_back(std::move(crowd));
    }
    const std::size_t min = std::max(kMinField, MinWave(s, p) / 4);
    std::vector<FieldArmy> armies = FieldArmies(s, p, home, min);
    PlusTarget stage;
    const auto back = [&](std::vector<Unit*>& crowd) {
        if (stage.kind == 0) stage = Staging(s, p);
        if (stage.kind != 0) {
            Send(s, p, std::move(crowd), 1, 1, stage);
        }
    };
    for (std::vector<Unit*>& crowd : crowds) {
        if (crowd.size() < min) continue;
        const Unit& first = *crowd[0];
        const PlusTarget next = attacks ? BestHouse(s, p, first.x, first.y, kNextTarget) : PlusTarget{};
        if (next.kind == 0) {
            back(crowd);
            crowd.clear();
            continue;
        }
        FieldArmy a;
        a.x = first.x, a.y = first.y, a.to = next;
        armies.push_back(a);
        Send(s, p, Compact(s, p, std::move(crowd)), 1, 2, next);
        crowd.clear();
    }
    for (std::vector<Unit*>& crowd : crowds) {
        if (crowd.empty() || crowd.size() >= min) continue;
        if (const FieldArmy* a = NearestArmy(armies, crowd[0]->x, crowd[0]->y)) {
            PlusTarget to = a->to;
            to.kind = 1;
            Send(s, p, std::move(crowd), 1, 1, to);
        } else {
            back(crowd);
        }
    }
}

std::uint32_t CountHouses(const Player& pl, std::uint16_t type) {
    std::uint32_t n = 0;
    for (const House& h : pl.houses) n += h.type == type && Standing(h) ? 1 : 0;
    return n;
}

// The heavy units a full army is better off with: tanks (11, 12), howitzers and cannons (0x15, 0x16), aircraft.
bool Heavy(const UnitType& t, std::uint16_t type) {
    return t.present && (type == 11 || type == 12 || type == 0x15 || type == 0x16 || t.is_air != 0);
}


void Repeat(GameState& s, std::uint8_t p, std::uint16_t k, std::uint16_t t) {
    const std::uint8_t nation = House_TrainNation(s, p, k);
    if (!Player_CanAfford(s, p, t, nation)) return;
    Player_PayForUnit(s, p, t, nation);
    House_Enqueue(s, p, k, t, true);
}

}  // namespace

bool PlusArmy_Base(const GameState& s, std::uint8_t p, double& x, double& y) {
    const Player& pl = s.players[p];
    for (const House& h : pl.houses) {
        if (h.type == 6 && Standing(h) && h.field_020 == 0) {
            x = h.x, y = h.y;
            return true;
        }
    }
    if (pl.script) {
        const std::int32_t cx = pl.script->base_x, cy = pl.script->base_y;
        if (cx >= 0 && cy >= 0 && cx < s.map.width && cy < s.map.height) {
            x = (cx * 2 - (~cy & 1)) * 48 + 48, y = cy * 24;
            return true;
        }
    }
    for (const House& h : pl.houses) {
        if (Standing(h)) {
            x = h.x, y = h.y;
            return true;
        }
    }
    return false;
}

PlusTarget PlusArmy_Target(const GameState& s, std::uint8_t p) {
    const std::vector<Threat> th = Threats(s, p);
    const Threat* best = nullptr;
    for (const Threat& t : th) {
        if (t.answered >= 1.5 * t.fe) continue;
        if (best == nullptr || t.fe > best->fe) best = &t;
    }
    if (best != nullptr) {
        PlusTarget t;
        t.kind = 3, t.player = best->player, t.id = best->id, t.x = Ftol(best->ax), t.y = Ftol(best->ay);
        return t;
    }
    double x = 0, y = 0;
    if (!PlusArmy_Base(s, p, x, y)) return {};
    return BestHouse(s, p, x, y, 0.0);
}

bool PlusArmy_NearCap(const Player& pl) {
    return static_cast<std::int64_t>(pl.units_alive) * 10 >= static_cast<std::int64_t>(pl.unit_cap) * 9;
}

bool PlusArmy_Wave(GameState& s, std::uint8_t p, const PlusTarget& t, bool force) {
    if (t.kind == 0) return false;
    Player& pl = s.players[p];
    double bx = 0, by = 0;
    const PlusTarget stage = Staging(s, p);
    if (stage.kind == 0 || !PlusArmy_Base(s, p, bx, by)) return false;
    // The idle soldiers at home (those in the field are Field's): the guard nearest the base, the rest gathers at the
    // staging point.
    const Near home = Homes(s, p);
    std::vector<Unit*> v;
    for (Unit* u : IdleSoldiers(s, p)) {
        if (home.Within(u->x, u->y, kHome)) v.push_back(u);
    }
    ByDistance(v, bx, by);
    const std::size_t guard = std::max<std::size_t>(150, (v.size() + 4) / 5);
    if (v.size() <= guard) return false;
    std::vector<Unit*> out, walk;
    for (std::size_t i = guard; i < v.size(); ++i) {
        (D2(v[i]->x, v[i]->y, stage.x, stage.y) <= kStaged * kStaged ? out : walk).push_back(v[i]);
    }
    const std::size_t min = MinWave(s, p);
    if (out.size() < min) {
        // The nearest to the staging point walk there, twice what the wave lacks (no churn of the whole crowd), in
        // groups of 30 or more (the others wait for more to walk with).
        ByDistance(walk, stage.x, stage.y);
        if (walk.size() > 2 * (min - out.size())) walk.resize(2 * (min - out.size()));
        Send(s, p, Compact(s, p, std::move(walk)), 1, 1, stage);
        // Not yet a wave: from a quarter of the minimum (30 or more) on, the gathered soldiers join the nearest field
        // army as its reinforcement; else they wait for more.
        if (out.size() < std::max(kMinField, min / 4)) return false;
        const std::vector<FieldArmy> armies = FieldArmies(s, p, home, kMinField);
        const FieldArmy* a = nullptr;
        for (const FieldArmy& f : armies) {  // any field army, the nearest to the staging point
            if (a == nullptr || D2(f.x, f.y, stage.x, stage.y) < D2(a->x, a->y, stage.x, stage.y)) a = &f;
        }
        if (a == nullptr) return false;
        Send(s, p, Compact(s, p, std::move(out)), 1, 1, a->to);
        return true;
    }
    if (!force && !PlusArmy_NearCap(pl) && out.size() <= CountHouses(pl, 4) * 10 + 50 && pl.ai_retry < 10) {
        ++pl.ai_retry;
        return false;
    }
    ByDistance(out, t.x, t.y);
    if (out.size() > kWave) out.resize(kWave);
    out = Compact(s, p, std::move(out));
    if (out.empty()) return false;
    // The guns spread evenly through the list, so each group takes its share and lays them in its back rows.
    std::vector<Unit*> front, guns, list;
    for (Unit* u : out) (Unit_Type(s, p, *u).move_class == 10 ? guns : front).push_back(u);
    std::size_t fi = 0, gi = 0;
    while (fi < front.size() || gi < guns.size()) {
        const bool gun = gi < guns.size() && (fi >= front.size() || gi * front.size() < fi * guns.size());
        list.push_back(gun ? guns[gi++] : front[fi++]);
    }
    s.formation_budget = 10;
    s.path_budget_used = 100;
    Send(s, p, std::move(list), 4, t.kind, t);
    pl.ai_retry = 0;
    return true;
}

void PlusArmy_Defend(GameState& s, std::uint8_t p, bool near, double x, double y) {
    std::vector<Threat> th = Threats(s, p);
    if (near) {
        std::erase_if(th, [&](const Threat& t) { return D2(t.ax, t.ay, x, y) > 2500.0 * 2500.0; });
    }
    if (th.empty()) return;
    // The strongest first; ties by the order found.
    std::vector<std::size_t> order(th.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return th[a].fe > th[b].fe; });
    std::vector<Unit*> pool = IdleSoldiers(s, p);
    for (std::size_t i : order) {
        const Threat& t = th[i];
        double need = 1.5 * t.fe - t.answered;
        if (!(need > 0.0) || pool.empty()) continue;
        ByDistance(pool, t.ax, t.ay);
        std::vector<Unit*> list;
        std::size_t n = 0;
        for (; n < pool.size() && need > 0.0; ++n) {
            list.push_back(pool[n]);
            need -= Fe(s, p, *pool[n]);
        }
        pool.erase(pool.begin(), pool.begin() + static_cast<std::ptrdiff_t>(n));
        PlusTarget target;
        target.kind = 3, target.player = t.player, target.id = t.id, target.x = Ftol(t.ax), target.y = Ftol(t.ay);
        Send(s, p, std::move(list), 1, 3, target);
    }
}

void PlusArmy_Tick(GameState& s, std::uint8_t p) {
    const Player& pl = s.players[p];
    if (!s.plus_rules || pl.is_lost != 0 || pl.ai_kind == 0) return;
    const std::uint32_t t = s.tick + 7u * p;
    if (t % 100 == 0 && pl.field_9b2a98[2] != 0) PlusArmy_Defend(s, p);
    if (t % 200 == 50) Field(s, p);
}

void PlusArmy_Spend(GameState& s, std::uint8_t p) {
    if (!s.plus_rules || p >= s.players.size()) return;
    Player& pl = s.players[p];
    if (pl.is_lost != 0 || !(pl.food > 1000000.0)) return;
    if (static_cast<std::int64_t>(pl.units_alive) * 20 < static_cast<std::int64_t>(pl.unit_cap) * 19) return;
    if (CountHouses(pl, 8) != 0) {  // a market
        // At most 200,000, halved until the food index stays at 0.5 or more after the sale (E2), half for iron and
        // half for gold.
        double x = std::min(200000.0, (pl.food - 1000000.0) * 0.5);
        while (x >= 2000.0 && Market_IndexAfter(pl, 0, x) < 0.5f) x *= 0.5;
        for (const std::uint8_t buy : {std::uint8_t{2}, std::uint8_t{3}}) {  // iron, gold
            if (x < 2000.0) break;
            const auto amount = static_cast<float>(x * 0.5);
            const MarketAmounts sell = {amount, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
            Market_Exchange(s, p, 0.0f, 0.0f, amount, 0.0f, 0.0f, buy, Market_Receive(sell, buy));
        }
    }
    if (pl.control_type != 1 || pl.field_9b2b3a == 0) return;  // your own orders stay yours
    for (std::size_t k = 0; k < pl.houses.size(); ++k) {
        const House& h = pl.houses[k];
        if (!Standing(h) || h.field_020 != 0) continue;
        const auto k16 = static_cast<std::uint16_t>(k);
        const std::uint8_t nation = House_TrainNation(s, p, k16);
        if (h.type == 4) {  // a barracks repeating soldiers: mortar men while the mix allows
            if (h.queue_count == 0xffff && !h.queue.empty() && h.queue[0] == 2 && s.unit_types[nation][5].present &&
                !AI_MixFull(s, p, 5) && !AI_CannotFeed(s, p, 5)) {
                Repeat(s, p, k16, 5);
            }
            continue;
        }
        if (h.queue_count != 0) continue;
        std::uint16_t best = 0xffff;
        double best_fe = 0;
        for (const std::uint16_t t : House_Type(s, p, h).produce) {
            if (t == 0xffff) break;
            if (t >= 40) continue;
            const UnitType& ut = s.unit_types[nation][t];
            if (!Heavy(ut, t) || AI_MixFull(s, p, t) || AI_CannotFeed(s, p, t) || !Player_CanAfford(s, p, t, nation)) {
                continue;
            }
            double v = 0;
            for (int r = 0; r < 5; ++r) v += static_cast<double>(ut.cost[static_cast<std::size_t>(r)]) * kPrice[r];
            if (best == 0xffff || v > best_fe) best = t, best_fe = v;
        }
        if (best != 0xffff) Repeat(s, p, k16, best);
    }
}

}  // namespace antanta::sim
