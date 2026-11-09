


#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <iterator>
#include <utility>

#include "market.hpp"
#include "movement.hpp"
#include "original_ids.hpp"
#include "plus_army.hpp"
#include "plus_brain.hpp"

namespace antanta::sim {

namespace {

std::uint16_t R(const GameState& s, std::uint32_t i) { return s.rng_table[static_cast<std::uint16_t>(i)]; }


bool Idle(const GameState& s, std::uint8_t p, const Unit& u) {
    return u.work == 0xff && !u.in_group && u.player_unit_attack == 0xff && u.player_house_attack == 0xff &&
           u.is_deleted == 0 && Unit_Type(s, p, u).speed[p] > 0.5f && u.field_154 == 0;
}

double UnitDist(const Unit& a, const Unit& b) {
    const double dx = static_cast<double>(a.x) - b.x, dy = static_cast<double>(a.y) - b.y;
    return std::sqrt(dx * dx + dy * dy);
}


void Select(std::vector<Unit*>& list, Unit& u) {
    list.insert(list.begin(), &u);
    u.field_006 = 1;
}


void SelAir(GameState& s, std::uint8_t p, std::vector<Unit*>& list, bool& atk) {
    atk = false;
    Player& pl = s.players[p];
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        Unit& u = pl.unit_slots[k];
        if (Unit_Type(s, p, u).is_air != 0 && Idle(s, p, u)) Select(list, u);
    }
}



void SelShips(GameState& s, std::uint8_t p, std::vector<Unit*>& list, bool& atk) {
    atk = false;
    Player& pl = s.players[p];
    const Unit* first = nullptr;
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        Unit& u = pl.unit_slots[k];
        if (first == nullptr && Rng_Next(s) % 3 != 0) continue;
        if (Unit_Type(s, p, u).is_ship == 0 || u.type == 0x11 || u.work == 0xe || u.field_154 != 0) continue;
        if (first != nullptr && !(UnitDist(*first, u) < static_cast<double>(2500.0f))) continue;
        if (first == nullptr) first = &u;
        Select(list, u);
    }
}


void SelBC(GameState& s, std::uint8_t p, std::vector<Unit*>& list, bool& atk) {
    atk = false;
    Player& pl = s.players[p];
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        Unit& u = pl.unit_slots[k];
        if ((u.type == 0xb || u.type == 0xc) && u.is_deleted == 0 && u.work != 9) Select(list, u);
    }
}



void SelArmy(GameState& s, std::uint8_t p, std::vector<Unit*>& list, bool& atk) {
    atk = true;
    Player& pl = s.players[p];
    const Unit* first = nullptr;
    if (pl.ai_level == 3 && pl.num_units != 0) {
        const std::uint16_t r = R(s, s.rng_index);
        ++s.rng_index;
        if ((r & 1) != 0) {
            const std::uint32_t k = R(s, s.rng_index) % pl.num_units;
            ++s.rng_index;
            std::int32_t j = 0;
            std::uint32_t slot = pl.first_unit;
            for (; slot != kNoSlot; slot = pl.unit_slots[slot].next) {
                ++j;
                const Unit& u = pl.unit_slots[slot];
                if (j >= static_cast<std::int32_t>(k) && Idle(s, p, u) && Unit_Type(s, p, u).field_009 != 0) break;
            }
            if (slot == kNoSlot) return;
            first = &pl.unit_slots[slot];
        }
    }
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        Unit& u = pl.unit_slots[k];
        if (Unit_Type(s, p, u).field_009 == 0 || !Idle(s, p, u)) continue;
        if (first != nullptr) {
            if (UnitDist(*first, u) > static_cast<double>(2000.0f)) continue;
        } else {
            first = &u;
        }
        Select(list, u);
    }
}




void AI_Defend(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    std::uint32_t sent = 0;
    for (std::int32_t k = static_cast<std::int32_t>(pl.houses.size()) - 1; k >= 0; --k) {
        const House& h = pl.houses[static_cast<std::size_t>(k)];
        if (h.is_destroyed != 0 || House_Type(s, p, h).field_14c == 0 || h.field_020 != 0) continue;
        if (h.type == 0xb || h.type == 10 || h.type == 0xc || h.units_inside >= h.capacity) continue;
        if (Rng_Next(s) % 5 != 1) continue;
        ++sent;
        Unit* u = AI_FindIdle(s, p, 6, h.exit_x, h.exit_y, 999999.0f);
        if (u == nullptr) u = AI_FindIdle(s, p, 2, h.exit_x, h.exit_y, 999999.0f);
        if (u != nullptr) Worker_SendToHouse(s, p, *u, static_cast<std::uint16_t>(k), 0);
        if (sent > 4) return;
    }
}



std::int32_t HouseThreat(const GameState& s, std::uint8_t p, std::uint8_t hp, const House& h) {
    if (h.is_destroyed != 0 || !IsEnemy(s, p, hp)) return 0;
    switch (h.type) {
        case 1:
        case 4:
        case 7:
        case 0x10:
        case 0x13:
        case 0x15:
        case 0x17: return 100;
        default: return 10;
    }
}


std::uint32_t CountHouses(const GameState& s, std::uint8_t p, std::uint16_t type) {
    std::uint32_t n = 0;
    for (const House& h : s.players[p].houses) n += h.type == type && h.is_destroyed == 0 ? 1 : 0;
    return n;
}

}  // namespace


Unit* AI_FindIdle(GameState& s, std::uint8_t p, std::uint16_t type, float x, float y, float best) {
    Player& pl = s.players[p];
    Unit* found = nullptr;
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        Unit& u = pl.unit_slots[k];
        if (u.type != type || !Idle(s, p, u)) continue;
        const double dx = static_cast<double>(x) - u.x, dy = static_cast<double>(y) - u.y;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d < static_cast<double>(best)) {
            found = &u;
            best = static_cast<float>(d);
        }
    }
    return found;
}



std::int32_t AI_Threat(const GameState& s, std::uint8_t p, std::int32_t x, std::int32_t y, std::int32_t px,
                       std::int32_t py, std::uint8_t depth) {
    const std::int32_t w = s.map.width, h = s.map.height;
    std::int32_t x0 = x - 0x14, x1 = x + 0x14, y0 = y - 0x28, y1 = y + 0x28;
    const auto clamp = [w, h](std::int32_t& cx, std::int32_t& cy) {
        if (cx < 4) cx = 4;
        if (cy < 10) cy = 10;
        if (w - 3 <= cx) cx = w - 4;
        if (h - 9 <= cy) cy = h - 10;
    };
    clamp(x0, y0);
    clamp(x1, y1);
    std::int32_t sum = 0;
    for (std::int32_t cx = x0; cx <= x1; ++cx) {
        for (std::int32_t cy = y0; cy <= y1; ++cy) {
            const auto k = static_cast<std::size_t>(static_cast<std::uint32_t>(w * cy + cx));
            if (k < s.map.objects.size()) {
                const std::uint16_t o = s.map.objects[k];
                if (o > 999 && o < 9000) {
                    const std::size_t hp = o / 1000 - 1u, slot = o % 1000;
                    if (hp < s.players.size() && slot < s.players[hp].houses.size()) {
                        sum += HouseThreat(s, p, static_cast<std::uint8_t>(hp), s.players[hp].houses[slot]);
                    }
                }
            }
            if (k >= s.map.cell_units.size()) continue;
            for (const UnitRef& r : s.map.cell_units[k]) {
                const Unit* u = Unit_Find(s, r);
                if (u != nullptr && u->is_deleted == 0 && IsEnemy(s, p, r.player)) ++sum;
            }
        }
    }
    if (depth < 2) {
        const std::int32_t mx = (x - px) / 2 + px, my = (y - py) / 2 + py;
        sum += AI_Threat(s, p, mx, my, x, y, static_cast<std::uint8_t>(depth + 1));
    }
    return sum;
}

namespace {

void AI_NavalTransport(GameState& s, std::uint8_t p, std::uint8_t target);

// ---- Water maps (docs/ai.md 10.17): who can reach whom by land. Classic uses it for boarding, Plus for its plans ----

// The land mass (Land_Region) under a cell, else of the first of its eight neighbours on land; 0: none.
std::uint16_t RegionAtCell(GameState& s, Cell c) {
    std::uint16_t r = Land_Region(s, c.x, c.y);
    for (std::uint8_t d = 1; d <= 8 && r == 0; ++d) {
        const Cell q = Cell_Step(c, d);
        r = Land_Region(s, q.x, q.y);
    }
    return r;
}

std::uint16_t RegionAt(GameState& s, float x, float y) { return RegionAtCell(s, WorldToCell(x, y)); }

// The land mass of the player's base (the script's base cell); 0: unknown.
std::uint16_t BaseRegion(GameState& s, std::uint8_t q) {
    const Player& pl = s.players[q];
    return pl.script ? RegionAtCell(s, Cell{pl.script->base_x, pl.script->base_y}) : 0;
}

// A unit that walks: alive, not aboard, neither ship nor aircraft.
bool Walker(const GameState& s, std::uint8_t q, const Unit& u) {
    const UnitType& ut = Unit_Type(s, q, u);
    return u.is_deleted == 0 && !u.in_transport && ut.is_ship == 0 && ut.is_air == 0;
}

// An enemy house on the player's own land mass (an unknown base counts as reachable).
bool LandEnemy(GameState& s, std::uint8_t p) {
    const std::uint16_t rb = BaseRegion(s, p);
    if (rb == 0) return true;
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        const auto qq = static_cast<std::uint8_t>(q);
        if (s.players[q].is_lost != 0 || !IsEnemy(s, p, qq)) continue;
        for (const House& h : s.players[q].houses) {
            if (h.is_destroyed == 0 && RegionAtCell(s, Cell{h.map_x, h.map_y}) == rb) return true;
        }
    }
    return false;
}

// Every living enemy has a house or a walking unit on the player's own land mass; an enemy with neither anywhere does
// not count (an unknown base counts as all reachable).
bool AllByLand(GameState& s, std::uint8_t p) {
    const std::uint16_t rb = BaseRegion(s, p);
    if (rb == 0) return true;
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        const auto qq = static_cast<std::uint8_t>(q);
        const Player& e = s.players[q];
        if (e.is_lost != 0 || !IsEnemy(s, p, qq)) continue;
        bool any = false, near = false;
        for (const House& h : e.houses) {
            if (h.is_destroyed != 0) continue;
            any = true;
            near = RegionAtCell(s, Cell{h.map_x, h.map_y}) == rb;
            if (near) break;
        }
        for (std::uint32_t k = e.first_unit; k != kNoSlot && !near; k = e.unit_slots[k].next) {
            const Unit& u = e.unit_slots[k];
            if (!Walker(s, qq, u)) continue;
            any = true;
            near = RegionAt(s, u.x, u.y) == rb;
        }
        if (any && !near) return false;
    }
    return true;
}

// Plus, saving for the port (set by AIScript_Run, docs/ai.md 10.17): while it lasts, Build takes only the economy's
// houses and the training only units that cost at most 500 wood, iron, gold and oil together (soldiers, workers), so
// the 75,000 of the port add up.
bool SavingForPort(const GameState& s, std::uint8_t p) { return s.ai_saving_port == p; }

bool DearUnit(const GameState& s, std::uint8_t p, std::uint16_t t) {
    if (t >= 40) return false;
    const auto& c = s.unit_types[s.players[p].nation][t].cost;
    return c[1] + c[2] + c[3] + c[4] > 500.0f;
}

}  // namespace




void AI_Think(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    ++pl.ai_counter;
    bool air = R(s, s.rng_index) < 1500;
    ++s.rng_index;
    if (s.plus_rules) PlusArmy_Tick(s, p);  // Entente Engine Plus: the defence per threat, the field groups
    bool atk = true, ship = false, bc = false, all = false;
    std::uint8_t tgt = 0xff;
    const double lvl = 4.0 - pl.ai_level;
    const auto t1 = Ftol(lvl * static_cast<double>(33.333332f));  // T1 = T2
    const auto t3 = Ftol(lvl * static_cast<double>(166.66667f));
    const std::uint32_t i0 = s.rng_index;
    switch (pl.ai_kind) {
        case 0: return;
        case 1:
            if (pl.ai_counter < t1) return;
            pl.ai_counter = 0;
            tgt = p, atk = false;
            break;
        case 2: {
            if (pl.ai_counter < t1) return;
            pl.ai_counter = 0;
            s.rng_index = static_cast<std::uint16_t>(i0 + 2);
            if (R(s, i0 + 1) % 5 != 1 && pl.field_9b2a98[0] != 0) {
                tgt = p, atk = false;
                break;
            }
            if (R(s, i0 + 2) % 5 == 1) bc = true;
            if (R(s, i0 + 3) % 5 != 1) atk = false;
            s.rng_index = static_cast<std::uint16_t>(i0 + 4);
            break;
        }
        case 3: {
            if (pl.ai_counter < t3) return;
            pl.ai_counter = 0;
            std::uint32_t next = i0 + 1;
            if (R(s, i0) % 5 == 1) {
                bc = true;
            } else {
                if (R(s, i0 + 1) % 5 == 1) air = true;
                next = i0 + 2;
            }
            if (R(s, next) % 5 != 1) atk = false;
            s.rng_index = static_cast<std::uint16_t>(next + 1);
            break;
        }
        case 4: {
            if (pl.ai_counter < t3) return;
            pl.ai_counter = 0;
            if (R(s, i0) % 5 != 1) atk = false;
            s.rng_index = static_cast<std::uint16_t>(i0 + 2);
            if ((R(s, i0 + 1) & 1) != 0) {
                tgt = p, atk = false;
                break;
            }
            s.rng_index = static_cast<std::uint16_t>(i0 + 3);
            if (R(s, i0 + 2) % 10 != 1) return;
            air = true;
            break;
        }
        case 5: {
            if (pl.ai_counter < t1) return;
            pl.ai_counter = 0;
            if ((R(s, i0) & 1) != 0) {
                s.rng_index = static_cast<std::uint16_t>(i0 + 1);
                tgt = p, atk = false, ship = false;
                break;
            }
            s.rng_index = static_cast<std::uint16_t>(i0 + 2);
            if (R(s, i0 + 1) % 10 != 1) break;
            ship = true;
            s.rng_index = static_cast<std::uint16_t>(i0 + 3);
            if (R(s, i0 + 2) < 15000) {
                air = true, ship = false;
            } else {
                air = false;
            }
            break;
        }
        default: break;  // no counter, no reset: the decision every tick
    }
    if (pl.ai_level == 3) {
        if (pl.ai_all == 0) {
            const std::uint32_t v = R(s, s.rng_index) % 0x65;
            ++s.rng_index;
            if (v == 1) {
                pl.ai_all = 1;
                pl.ai_countdown = R(s, s.rng_index) % 10;
                ++s.rng_index;
            } else {
                pl.ai_countdown = 0;
            }
        } else {
            pl.ai_countdown = 0;
        }
        if (pl.ai_kind == 3) {
            const std::uint32_t v = R(s, s.rng_index) % 0x33;
            ++s.rng_index;
            if (v == 2) pl.ai_kind = 2;
        }
        if (pl.ai_kind == 2) {
            const std::uint32_t v = R(s, s.rng_index) % 0x33;
            ++s.rng_index;
            if (v == 2) pl.ai_kind = 3;
        }
    }
    if (pl.ai_all != 0 && pl.ai_countdown != 0) {
        --pl.ai_countdown;
        return;
    }
    if (pl.ai_all != 0 && pl.ai_countdown == 0) {
        pl.ai_all = 0;
        all = true;
        tgt = 0xff;
    }

    // The enemy: over two rounds of the players, the first enemy, then each later meeting takes over on an odd read.
    const auto np = static_cast<std::uint32_t>(s.players.size());
    std::uint8_t best = 0xff;
    for (std::uint32_t i = 0; i < np * 2; ++i) {
        const auto q = static_cast<std::uint8_t>(i % np);
        if (s.players[q].is_lost != 0 || !IsEnemy(s, p, q)) continue;
        if (best == 0xff) {
            best = q;
        } else if ((Rng_Next(s) & 1) != 0) {
            best = q;
        }
    }
    if (tgt == 0xff && best == 0xff) return;
    if (p != best && tgt != best && (pl.ai_kind == 5 || s.land_map == 0) && pl.field_9b2a98[3] != 0) {

        AI_NavalTransport(s, p, best);
    }
    if (s.plus_rules) {
        // Entente Engine Plus, the army brain (sim/plus_army.hpp, docs/ai.md 11): the same wake-ups and dice, but the
        // target by score with the base as a world point, and the surplus over a home guard leaves in up to four
        // groups instead of one crowd; a defend wake-up also sends one from 90 % of the unit cap (not Defense).
        if (tgt == p) {
            if (pl.field_9b2a98[0] != 0) AI_Defend(s, p);
            if (pl.ai_kind != 1 && pl.field_9b2a98[1] != 0 && PlusArmy_NearCap(pl)) {
                PlusArmy_Wave(s, p, PlusArmy_Target(s, p), true);
            }
            return;
        }
        if (pl.field_9b2a98[1] == 0) return;
        const PlusTarget t = PlusArmy_Target(s, p);
        if (t.kind == 0) return;
        if (all || air || ship || bc) {
            std::vector<Unit*> list;
            if (all || air) SelAir(s, p, list, atk);
            if (all || ship) SelShips(s, p, list, atk);
            if (all || bc) SelBC(s, p, list, atk);
            if (!list.empty()) {
                s.formation_budget = 10;
                s.path_budget_used = 100;
                std::int32_t x = t.x, y = t.y;
                AI_FormGroups(s, p, list, 1, 2, 3, 1, x, y, 0, 0, 1, true);
            }
            if (!all) return;
        }
        if (s.land_map == 0 && t.kind == 2) {  // Plus on a water map (docs/ai.md 10.17): no wave over the water
            const std::uint16_t rb = BaseRegion(s, p);
            if (rb != 0 && RegionAt(s, static_cast<float>(t.x), static_cast<float>(t.y)) != rb) return;
        }
        PlusArmy_Wave(s, p, t, all);
        return;
    }

    // The target point. The base cell is used as a world point (original bug); the first enemy unit nearer than
    // 1e8 wins, and the best distance then becomes the comparison's result (1.0).
    const std::int32_t cx = pl.script ? pl.script->base_x : 0, cy = pl.script ? pl.script->base_y : 0;
    const float sx = static_cast<float>(cx), sy = static_cast<float>(cy);
    float bx = 0.0f, by = 0.0f, bd = 1e8f;
    bool found = false;
    if (atk) {
        if (pl.field_9b2a98[1] == 0) return;
        if (best != 0xff) {
            const Player& e = s.players[best];
            std::uint32_t n = 0;
            for (std::uint32_t k = e.first_unit; k != kNoSlot; k = e.unit_slots[k].next) {
                const Unit& u = e.unit_slots[k];
                if (u.is_deleted != 0) continue;
                const double dx = static_cast<double>(u.x) - sx, dy = static_cast<double>(u.y) - sy;
                const bool lt = std::sqrt(dx * dx + dy * dy) < static_cast<double>(bd);
                if (lt) {
                    bd = 1.0f;
                    bx = u.x, by = u.y;
                    ++n;
                }
            }
            found = n != 0;
        }
    }
    if (!found) {
        if (tgt != 0xff) best = tgt;
        if (best == p) {
            if (pl.field_9b2a98[0] != 0) AI_Defend(s, p);
            return;
        }
        if (pl.field_9b2a98[1] == 0 || best == 0xff || s.players[best].houses.empty()) return;
        const std::vector<House>& hs = s.players[best].houses;
        std::int32_t k = static_cast<std::int32_t>(hs.size()) - 1;
        while (k >= 0 && hs[static_cast<std::size_t>(k)].is_destroyed != 0) --k;
        if (k < 0) return;
        bx = 0.0f + hs[static_cast<std::size_t>(k)].x;
        by = 0.0f + hs[static_cast<std::size_t>(k)].y;
    }
    std::int32_t ptx = Ftol(bx), pty = Ftol(by);
    const Cell c = WorldToCell(bx, by);
    if (!(c.x > 3 && c.y > 9 && c.x < s.map.width - 3 && c.y < s.map.height - 9)) return;

    // The units and the order.
    std::vector<Unit*> list;
    atk = true;
    bool direct = false;
    if (all) {
        SelAir(s, p, list, atk);
        SelShips(s, p, list, atk);
        SelBC(s, p, list, atk);
        if (!list.empty()) {
            s.formation_budget = 10;
            s.path_budget_used = 100;
            std::int32_t x = ptx, y = pty;
            AI_FormGroups(s, p, list, 1, 2, 3, 1, x, y, 0, 0, 1, true);
            pl.ai_retry = 0;
        }
        list.clear();
        SelArmy(s, p, list, atk);
        atk = false;
    } else if (air) {
        SelAir(s, p, list, atk);
        direct = true;
    } else if (ship) {
        SelShips(s, p, list, atk);
        direct = true;
    } else if (bc) {
        SelBC(s, p, list, atk);
        direct = true;
    } else {
        SelArmy(s, p, list, atk);
    }
    const auto n = static_cast<std::uint16_t>(list.size());
    if (n == 0) return;
    s.formation_budget = 10;
    if (atk && n <= (CountHouses(s, p, 4) & 0xffff) * 10 + 50 && pl.ai_retry < 10) {
        ++pl.ai_retry;
        return;
    }
    s.path_budget_used = 100;
    AI_FormGroups(s, p, list, atk ? 4 : 1, 2, 3, 1, ptx, pty, 0, 0, 1, direct);
    pl.ai_retry = 0;
}



namespace {

std::uint16_t Rd(GameState& s) { return Rng_Next(s); }  // R(I++)

std::uint16_t NumUnitTypes(const GameState& s, std::uint8_t nation) {
    std::uint16_t n = 0;
    while (n < 40 && s.unit_types[nation][n].present) ++n;
    return n;
}

std::uint16_t NumHouseTypes(const GameState& s, std::uint8_t nation) {
    std::uint16_t n = 0;
    while (n < 25 && s.house_types[nation][n].present) ++n;
    return n;
}


std::uint8_t Producer(const GameState& s, std::uint8_t nation, std::uint16_t t) {
    for (std::uint8_t h = 0; h < 25; ++h) {
        for (std::uint16_t v : s.house_types[nation][h].produce) {
            if (v == 0xffff) break;
            if (v == t) return h;
        }
    }
    return 0xff;
}

bool Built(const House& h) { return h.is_destroyed == 0 && h.field_020 == 0; }



bool CanAffordN(const GameState& s, std::uint8_t p, std::uint16_t t, std::int32_t n, std::uint8_t nation) {
    const Player& pl = s.players[p];
    const UnitType& ut = s.unit_types[nation][t];
    const std::pair<double Player::*, int> order[5] = {
        {&Player::gold, 3}, {&Player::food, 0}, {&Player::wood, 1}, {&Player::iron, 2}, {&Player::oil, 4}};
    for (const auto& [f, c] : order) {
        const float cost = ut.cost[static_cast<std::size_t>(c)];
        if (cost > 1.0f && static_cast<double>(cost) * static_cast<double>(static_cast<float>(n)) > pl.*f) {
            return false;
        }
    }
    return static_cast<std::int32_t>(pl.unit_cap) > pl.units_alive + n;
}

bool Trains(const HouseType& ht, std::uint16_t t) {
    for (std::uint16_t v : ht.produce) {
        if (v == 0xffff) return false;
        if (v == t) return true;
    }
    return false;
}

// The house of the producing type with the smallest queue below lim (first slot wins ties), or 0xffff.
std::uint16_t TrainHouse(const GameState& s, std::uint8_t p, std::uint16_t t, std::uint16_t lim) {
    const Player& pl = s.players[p];
    const std::uint8_t ph = Producer(s, pl.nation, t);
    if (t >= NumUnitTypes(s, pl.nation) || ph == 0xff) return 0xffff;
    std::uint16_t best = 0xffff;
    for (std::size_t k = 0; k < pl.houses.size(); ++k) {
        const House& h = pl.houses[k];
        // Ours: a merged house of another nation only when its own list trains the type (B263; otherwise the
        // payment would be lost, House_Enqueue refuses a type its list lacks).
        if (!s.original_rules && h.nation != 0xff && !Trains(House_Type(s, p, h), t)) continue;
        if (Built(h) && h.type == ph && h.queue_count < lim) {
            lim = h.queue_count;
            best = static_cast<std::uint16_t>(k);
        }
    }
    return best;
}

bool TrainAllowed(const Player& pl, std::uint16_t t) { return (t == 0 ? pl.field_9b2b39 : pl.field_9b2b3a) != 0; }

}  // namespace

// Entente Engine Plus, A3 (docs/plus-balance.md 4): a Difficult computer and every advisor keep a sane mix. No new
// mortar men (type 5) while they number 1 per 12 riflemen (soldiers, officers, special soldiers: 2, 3, 14) or more;
// no new howitzers or cannons (0x15, 0x16) while the guns (0x15 to 0x17) number 1 per 25 riflemen or more. The callers
// test it after their dice, so the random sequence stays as it was.
bool AI_MixFull(const GameState& s, std::uint8_t p, std::uint16_t t) {
    if (!s.plus_rules || (t != 5 && t != 0x15 && t != 0x16)) return false;
    const Player& pl = s.players[p];
    if (pl.control_type == 1 && pl.ai_level != 3) return false;
    const std::int32_t rifles = pl.unit_count[2] + pl.unit_count[3] + pl.unit_count[14];
    if (t == 5) return 12 * pl.unit_count[5] >= rifles;
    return 25 * (pl.unit_count[0x15] + pl.unit_count[0x16] + pl.unit_count[0x17]) >= rifles;
}

namespace {


void TrainOne(GameState& s, std::uint8_t p, std::uint16_t t, std::uint16_t lim) {
    if (!TrainAllowed(s.players[p], t) || AI_MixFull(s, p, t) || AI_CannotFeed(s, p, t)) return;
    if (DearUnit(s, p, t) && SavingForPort(s, p)) return;
    const std::uint16_t k = TrainHouse(s, p, t, lim);
    if (k != 0xffff && Player_PayForUnit(s, p, t, House_TrainNation(s, p, k))) House_Enqueue(s, p, k, t, false);
}



void TrainMany(GameState& s, std::uint8_t p, std::uint16_t t, std::uint16_t lim, std::int32_t n) {
    if (!TrainAllowed(s.players[p], t) || AI_MixFull(s, p, t) || AI_CannotFeed(s, p, t)) return;
    if (DearUnit(s, p, t) && SavingForPort(s, p)) return;
    const std::uint16_t k = TrainHouse(s, p, t, lim);
    if (k == 0xffff) return;
    const std::uint8_t nation = House_TrainNation(s, p, k);  // B263
    if (!CanAffordN(s, p, t, n, nation)) return;
    for (std::int32_t i = 0; i < n; ++i) Player_PayForUnit(s, p, t, nation);
    for (std::int32_t i = 0; i < n; ++i) House_Enqueue(s, p, k, t, false);
}

void TrainFive(GameState& s, std::uint8_t p, std::uint16_t t, std::uint16_t lim) { TrainMany(s, p, t, lim, 5); }


void OrderAt(GameState& s, std::uint8_t p, const House& h, std::uint16_t t, int n) {
    const std::uint8_t nation = s.players[p].nation;
    if (t >= NumUnitTypes(s, nation) || Producer(s, nation, t) != h.type || AI_MixFull(s, p, t) ||
        AI_CannotFeed(s, p, t) || (DearUnit(s, p, t) && SavingForPort(s, p))) {
        return;
    }
    const std::uint16_t num = h.num;
    for (int i = 0; i < n; ++i) {
        if (!Player_PayForUnit(s, p, t, House_TrainNation(s, p, num))) return;
        House_Enqueue(s, p, num, t, false);
    }
}


bool CanBuild(const GameState& s, std::uint8_t p, std::uint16_t t) {
    const Player& pl = s.players[p];
    if (pl.houses.size() > 998) return false;
    const HouseType& ht = s.house_types[pl.nation][t % 25];
    const std::pair<double Player::*, int> order[5] = {
        {&Player::gold, 3}, {&Player::food, 0}, {&Player::wood, 1}, {&Player::iron, 2}, {&Player::oil, 4}};
    for (const auto& [f, c] : order) {
        const float cost = ht.cost[static_cast<std::size_t>(c)];
        if (cost > 1.0f && static_cast<double>(cost) > pl.*f) return false;
    }
    for (std::uint16_t req : ht.prereq) {
        if (req >= 25 || pl.houses_done[req] == 0) return false;
    }
    // Entente Engine Plus, A2 (docs/plus-balance.md 4): the fixed heavy howitzer's site (house 24 becomes unit 23) is
    // checked against the unit cap when it is ordered, its open sites counted as units, so a finished site always
    // turns into its unit.
    if (s.plus_rules && t == 0x18) {
        std::int32_t n = pl.units_alive;
        for (const House& h : pl.houses) n += h.type == 0x18 && h.is_destroyed == 0 ? 1 : 0;
        if (n >= static_cast<std::int32_t>(pl.unit_cap)) return false;
    }
    return true;
}

bool FindSite(GameState& s, std::uint8_t p, std::uint16_t t, Cell* out);
Unit* FindWorker(GameState& s, std::uint8_t p, std::int32_t x, std::int32_t y);
void WalkToSite(GameState& s, std::uint8_t p, Unit& u, Cell pt);
bool MoveBase(GameState& s, std::uint8_t p, bool far);



std::uint16_t AI_CloseBuild(GameState& s, std::uint8_t p, std::int32_t x, std::int32_t y, std::uint16_t type) {
    if (!CanBuild(s, p, type)) return 0xffff;
    House h = CreateHouse(s, x, y, type, s.players[p].nation);
    House_PlusNew(s, p, h);
    const std::uint16_t k = AddHouseToPlayer(s, p, std::move(h));
    if (k > 900) Unported(s, orig::kAiCloseBuild);  // the original only logs
    PayForHouse(s, p, k);
    House_SetGraphic(s.players[p].houses[k], 1, 0);
    return k;
}


void AI_SendWorker(GameState& s, std::uint8_t p, Unit& u, std::uint16_t k) {
    if (u.work != 9 && u.work != 2) Worker_SendToHouse(s, p, u, k, 0);
}



bool Build(GameState& s, std::uint8_t p, std::uint16_t t) {
    if (s.players[p].field_9b2b38 == 0 || !CanBuild(s, p, t)) return false;
    // Plus: while saving for the port only the power station, barracks, town centre, market, dwellings, mines,
    // sawmill, oil rig, barn and the port itself.
    constexpr std::uint32_t kEconomy = 1u << 0 | 1u << 4 | 1u << 6 | 1u << 8 | 1u << 9 | 1u << 10 | 1u << 11 |
                                       1u << 12 | 1u << 13 | 1u << 15 | 1u << 0x16;
    if (t < 32 && (kEconomy >> t & 1u) == 0 && SavingForPort(s, p)) return false;
    if (t == 0x18 && AI_CannotFeed(s, p, 0x17)) return false;  // Plus, stage 4: the fixed howitzer's gold upkeep
    Cell pt;
    if (!FindSite(s, p, t, &pt)) return false;
    Unit* w = FindWorker(s, p, pt.x, pt.y);
    if (w == nullptr) return false;
    const std::int32_t dx = pt.x - w->map_x, dy = pt.y - w->map_y;
    if (dy * dy + 4 * dx * dx < 500) {
        const std::uint16_t k = AI_CloseBuild(s, p, pt.x, pt.y, t);
        AI_SendWorker(s, p, *w, k);
        s.players[p].script->field_6fc = 0;
        return k != 0xffff;
    }
    if (w->work != 9) WalkToSite(s, p, *w, pt);
    const std::uint8_t save = w->work;
    w->work = 2;  // hides the first worker from the second search
    Unit* w2 = FindWorker(s, p, pt.x, pt.y);
    if (w2 != nullptr && w2->work != 9) WalkToSite(s, p, *w2, pt);
    w->work = save;
    AIScript& sc = *s.players[p].script;
    if (++sc.field_6fc > 40 && !MoveBase(s, p, false)) MoveBase(s, p, true);
    return false;
}

std::uint16_t Count16(const GameState& s, std::uint8_t p, std::uint16_t t) {
    return static_cast<std::uint16_t>(CountHouses(s, p, t));
}

double Delta(const Player& pl, double HistSample::* r) {
    if (pl.history_index <= 1) return 0.0;
    return pl.history[pl.history_index - 1].*r - pl.history[pl.history_index - 2].*r;
}

constexpr double Player::* kStock[5] = {&Player::food, &Player::wood, &Player::iron, &Player::gold, &Player::oil};



bool CanResearch(const GameState& s, std::uint8_t p, std::uint16_t r) {
    if (r >= 20) return false;
    const Player& pl = s.players[p];
    if (pl.research_9b39f0[r] != 0 || pl.research_9b3950[r] != 0) return false;
    const std::int32_t* cost = Research_CostRow(s, 2, r);  // Plus: its prices (docs/plus-balance.md 5)
    for (int i = 0; i < 5; ++i) {
        if (pl.*kStock[i] * 0.5 < static_cast<double>(cost[i])) return false;
    }
    if (s.climate == 3 && (r == 0 || r == 1 || r == 2 || r == 6 || r == 7)) return false;
    if (r == 2) return pl.research_flags[12] != 0;
    if (r == 4) return pl.research_flags[15] != 0;
    if (r == 7) return pl.research_flags[18] != 0;
    return true;
}




void StartResearch(GameState& s, std::uint8_t p, House& h, std::uint16_t r) {
    if (h.field_05c != 0) return;
    Player& pl = s.players[p];
    const std::int32_t* cost = Research_CostRow(s, 2, r);  // Plus: its prices (docs/plus-balance.md 5)
    for (int i = 0; i < 5; ++i) {
        const double c = static_cast<double>(cost[i]);
        double& v = pl.*kStock[i];
        v = v - c;
        pl.*StatOf(kStock[i]) = pl.*StatOf(kStock[i]) - c;
        if (v < 0.0) v = 0.0;
    }
    Research_Start(s, p, h, kSciencesTime[r], static_cast<std::uint16_t>(r + 3));
}



void TryResearch(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    if (pl.field_9b2b3b == 0 || s.plus_rules) return;
    House* h = nullptr;
    for (House& o : pl.houses) {
        // B100: the original takes the first slot of type 2, a ruin or a building site included (it then never
        // researches again, or pays the full price to the ruin); ours takes the first standing, finished one.
        if (o.type == 2 && (s.original_rules || (o.is_destroyed == 0 && o.field_020 == 0))) {
            h = &o;
            break;
        }
    }
    if (h == nullptr || h->field_05c != 0) return;
    const auto tryset = [&](std::initializer_list<std::uint16_t> rs) {
        for (std::uint16_t r : rs) {
            if (CanResearch(s, p, r)) {
                StartResearch(s, p, *h, r);
                return true;
            }
        }
        return false;
    };
    const bool deltas = pl.history_index > 1;
    if (Rd(s) < 3000 && deltas && Delta(pl, &HistSample::food) < 0.0 && tryset({0, 1, 2, 6, 7})) return;
    if (Rd(s) < 3000 && deltas && Delta(pl, &HistSample::gold) < 0.0 && tryset({10, 11})) return;
    if (Rd(s) < 3000 && deltas && Delta(pl, &HistSample::iron) < 0.0 && tryset({10, 11})) return;
    if (Rd(s) < 50 && tryset({4, 5, 6})) return;
}


bool Actionable(const GameState& s, std::uint8_t p, const AIScript& sc, const ScriptEntry& e) {
    const Player& pl = s.players[p];
    switch (e.kind) {
        case 1: return Player_CanAfford(s, p, e.type) && pl.unit_count[e.type % 40] < sc.want_u[e.type % 40];
        case 2: return CanBuild(s, p, e.type) && Count16(s, p, e.type) < sc.want_h[e.type % 30];
        case 3: return e.type == 0 && e.count != 0 && CanResearch(s, p, e.count);
        default: return true;
    }
}


void Template(GameState& s, std::uint8_t p, AIScript& sc) {
    const std::vector<ScriptEntry>& ents = sc.main[sc.step];
    if (ents.empty() || sc.entry >= ents.size()) {
        Unported(s, orig::kAiTemplate);  // an empty step or an entry past its end
        return;
    }
    const Player& pl = s.players[p];
    const std::uint16_t e0 = sc.entry;
    const bool rich = !(pl.food < 1000.0 || pl.gold < 100.0 || pl.iron < 100.0);
    const ScriptEntry cur = ents[e0];
    if (Actionable(s, p, sc, cur)) {
        if (cur.kind == 1) {
            const std::uint16_t t = cur.type;
            if (t == 0 || rich) {
                if (static_cast<std::int32_t>(pl.unit_count[t % 40]) <
                    static_cast<std::int32_t>(sc.want_u[t % 40]) + 5) {
                    TrainFive(s, p, t, 10);
                } else {
                    TrainOne(s, p, t, 10);
                }
            }
        } else if (cur.kind == 2) {
            const std::uint16_t n = Count16(s, p, cur.type);
            if (s.climate == 3) sc.want_h[15] = 0;
            if (n < sc.want_h[cur.type % 30]) Build(s, p, cur.type);
        }
    }
    do {
        ++sc.entry;
        if (sc.entry == ents.size()) sc.entry = 0;
    } while (sc.entry != e0 && !Actionable(s, p, sc, ents[sc.entry]));
}


void StepDone(GameState& s, std::uint8_t p, AIScript& sc) {
    const Player& pl = s.players[p];
    for (const ScriptEntry& e : sc.main[sc.step]) {
        if (e.kind == 1 && pl.unit_count[e.type % 40] < sc.want_u[e.type % 40]) return;
        if (e.kind == 2 && Count16(s, p, e.type) < sc.want_h[e.type % 30]) return;
    }
    ++sc.step;
    sc.entry = 0;
    const auto add = [&sc](const std::vector<ScriptEntry>& l) {
        for (const ScriptEntry& e : l) {
            if (e.kind == 1) sc.want_u[e.type % 40] = static_cast<std::uint16_t>(sc.want_u[e.type % 40] + e.count);
            if (e.kind == 2) sc.want_h[e.type % 30] = static_cast<std::uint16_t>(sc.want_h[e.type % 30] + e.count);
        }
    };
    add(sc.list2[sc.step - 1]);
    if (sc.step < sc.nsteps) add(sc.main[sc.step]);
}




void Trade(Player& pl, int from, int to, float amt, bool plus) {
    static const float kRate[5][5] = {
        {1.0f, std::bit_cast<float>(0x3eaaaaabu), std::bit_cast<float>(0x3de38e39u), std::bit_cast<float>(0x3d430c31u),
         std::bit_cast<float>(0x3de38e39u)},
        {3.0f, 1.0f, std::bit_cast<float>(0x3eaaaaabu), std::bit_cast<float>(0x3e124925u),
         std::bit_cast<float>(0x3eaaaaabu)},
        {7.5f, 2.5f, 1.0f, std::bit_cast<float>(0x3ecccccdu), std::bit_cast<float>(0x3f666666u)},
        {15.0f, 5.0f, 2.0f, 1.0f, 2.0f},
        {7.5f, 2.5f, std::bit_cast<float>(0x3f666666u), std::bit_cast<float>(0x3ecccccdu), 1.0f}};
    double g = static_cast<double>(amt) * static_cast<double>(kRate[from][to]);
    if (plus) {
        float& index = pl.market_index[static_cast<std::size_t>(from)];
        const float after = Market_IndexAfter(pl, from, amt);
        g *= (static_cast<double>(index) + static_cast<double>(after)) * 0.5;
        index = after;
    }
    double& t = pl.*kStock[to];
    t = t + g;
    pl.*StatOf(kStock[to]) = pl.*StatOf(kStock[to]) + g;
    if (t < 0.0) t = 0.0;
    double& f = pl.*kStock[from];
    f = f - static_cast<double>(amt);
    pl.*StatOf(kStock[from]) = pl.*StatOf(kStock[from]) - static_cast<double>(amt);
    if (f < 0.0) f = 0.0;
    pl.res_sold[from] = static_cast<float>(static_cast<double>(amt) + static_cast<double>(pl.res_sold[from]));
    pl.res_bought[to] = static_cast<float>(g + static_cast<double>(pl.res_bought[to]));
}





struct Source {
    int res;
    double hi, lo;  // hi < 0: no HI test
    float amt;
};

bool MarketSelect(Player& pl, bool plus, int need_res, std::int32_t need, const std::array<std::int32_t, 4>& d,
                  const std::array<Source, 4>& src) {
    if (!(static_cast<double>(need) > pl.*kStock[need_res])) return false;
    constexpr double HistSample::* kHist[5] = {&HistSample::food, &HistSample::wood, &HistSample::iron,
                                               &HistSample::gold, &HistSample::oil};
    for (std::size_t k = 0; k < 4; ++k) {
        const Source& c = src[k];
        const double v = pl.*kStock[c.res];
        if (!(v > static_cast<double>(d[k]))) continue;
        if (plus && pl.market_index[static_cast<std::size_t>(c.res)] < 0.5f) continue;
        if ((c.hi >= 0.0 && v > c.hi) || (v > c.lo && Delta(pl, kHist[c.res]) > 0.0)) {
            Trade(pl, c.res, need_res, c.amt, plus);
            return true;
        }
    }
    return false;
}

}  // namespace



void Market_Buy(GameState& s, std::uint8_t p, float tf, float tg, float ti, float tw, float to) {
    if (CountHouses(s, p, 8) == 0) return;
    Player& pl = s.players[p];
    const std::int32_t f = Ftol(tf), g = Ftol(tg), i = Ftol(ti), w = Ftol(tw), o = Ftol(to);
    constexpr int F = 0, W = 1, I = 2, G = 3, O = 4;
    if (pl.food < static_cast<double>(tf)) {
        MarketSelect(pl, s.plus_rules, F, f, {0, 0, 0, 0},
                     {Source{G, 30000, 5000, 1000}, Source{I, 50000, 5000, 2000}, Source{W, 50000, 10000, 5000},
                      Source{O, 40000, 5000, 2000}});
    } else if (pl.gold < static_cast<double>(tg)) {
        MarketSelect(pl, s.plus_rules, G, g, {f + 50000, 0, 0, 0},
                     {Source{F, 200000, 100000, 50000}, Source{I, 50000, 5000, 2000}, Source{W, 50000, 10000, 5000},
                      Source{O, 40000, 5000, 2000}});
    } else if (pl.iron < static_cast<double>(ti)) {
        MarketSelect(pl, s.plus_rules, I, i, {f + 50000, g + 1000, 0, 0},
                     {Source{F, 200000, 100000, 50000}, Source{G, 30000, 5000, 1000}, Source{W, 30000, 10000, 5000},
                      Source{O, 50000, 5000, 2000}});
    } else if (pl.wood < static_cast<double>(tw)) {
        MarketSelect(pl, s.plus_rules, W, w, {f + 50000, g + 1000, i + 2000, 0},
                     {Source{F, 200000, 100000, 50000}, Source{G, 30000, 5000, 1000}, Source{I, -1, 5000, 2000},
                      Source{O, 40000, 5000, 2000}});
    } else if (pl.oil < static_cast<double>(to)) {
        MarketSelect(pl, s.plus_rules, O, o, {f + 50000, g + 1000, i + 2000, w + 5000},
                     {Source{F, 200000, 100000, 50000}, Source{G, 30000, 5000, 1000}, Source{I, 40000, 5000, 2000},
                      Source{W, 30000, 10000, 5000}});
    }
}

namespace {


void Market(GameState& s, std::uint8_t p, std::uint8_t lvl) {
    if (CountHouses(s, p, 8) == 0) return;
    float tf = 100000, tg = 10000, ti = 10000, tw = 5000, to = 1000;
    if (lvl == 1) {
        tf = 5000, tg = 1200, ti = 500, tw = 1000, to = 300;
    } else if (lvl == 2) {
        tf = 30000, tg = 5000, ti = 5000, tw = 5000, to = 1000;
    } else if (lvl == 3) {  // by tick: before 5000, before 20000, later
        if (s.tick < 5000) {
            tf = 3000, tg = 5500, ti = 5500, tw = 5500;
        } else if (s.tick < 20000) {
            tf = 30000, tg = 5500, ti = 5500, tw = 5500;
        } else {
            tf = 100000, tg = 15000, ti = 15000, tw = 10000;
        }
    }
    Market_Buy(s, p, tf, tg, ti, tw, to);
}


void Script_4b1130(GameState& s, std::uint8_t p, std::uint16_t a, std::uint16_t b) {
    const Player& pl = s.players[p];
    if (s.climate != 3) {
        if (Rd(s) < a && static_cast<std::int32_t>(CountHouses(s, p, 0xf)) <= pl.units_alive / 200) Build(s, p, 0xf);
        if (pl.history_index > 1 && Delta(pl, &HistSample::food) < 0.0 && s.tick > 3000) {
            if (Rd(s) < a) Build(s, p, 0xf);
        }
    }
    const std::uint16_t r = Rd(s);
    if (r < b && s.climate != 3) TrainOne(s, p, 0, 10);
}


void Script_4b1250(GameState& s, std::uint8_t p, std::uint16_t a, std::uint16_t b) {
    if (s.tick < 5000) return;
    const Player& pl = s.players[p];
    if (static_cast<std::int32_t>(pl.unit_cap) <= pl.units_alive + 1000) {
        if (Rd(s) < a) {
            Build(s, p, 9);
            return;
        }
        if (Rd(s) < b) {
            Build(s, p, 6);
            return;
        }
    }
    if (Rd(s) >= 20000) return;
    if (pl.houses_done[4] <= (pl.houses_done[9] >> 1) + pl.houses_done[6] * 2) return;
    if (Rd(s) < a) {
        Build(s, p, 9);
        return;
    }
    if (Rd(s) < b) Build(s, p, 6);
}






void MintUpgrade(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    House* h = nullptr;
    for (House& o : pl.houses) {
        if (Built(o) && o.type == 0xe) {
            h = &o;
            break;
        }
    }
    if (h == nullptr || h->field_06a != 0 || h->field_05c != 0) return;
    constexpr std::int32_t kCost[5] = {10000, 0, 5000, 1000, 0};
    if (static_cast<double>(kCost[0]) > pl.food || static_cast<double>(kCost[3]) > pl.gold ||
        static_cast<double>(kCost[1]) > pl.wood || static_cast<double>(kCost[2]) > pl.iron ||
        static_cast<double>(kCost[4]) > pl.oil) {
        return;
    }
    for (const auto& [r, c] : {std::pair{&Player::food, 0}, std::pair{&Player::gold, 3}, std::pair{&Player::wood, 1},
                               std::pair{&Player::iron, 2}, std::pair{&Player::oil, 4}}) {
        const auto cost = static_cast<double>(kCost[c]);
        pl.*r = pl.*r - cost;
        pl.*StatOf(r) = pl.*StatOf(r) - cost;
        if (pl.*r < 0.0) pl.*r = 0.0;
    }
    h->field_05c = 500;
    h->field_064 = 3;
    h->field_06a = 1;
    h->field_060 = 500.0f;
}


void Script_4b14b0(GameState& s, std::uint8_t p) {
    const Player& pl = s.players[p];
    if (pl.field_9b2b38 == 0) return;
    if (pl.mint_done == 0 && pl.field_9b2b3b == 1) MintUpgrade(s, p);
    if (Rd(s) < 7000) {
        Build(s, p, 0xb);
        return;
    }
    if (Rd(s) < 7000) {
        Build(s, p, 10);
        return;
    }
    if (Rd(s) < 7000) {
        Build(s, p, 0xd);
        return;
    }
    if (Rd(s) < 10000) Build(s, p, 0xc);
}




void Script_4b1580(GameState& s, std::uint8_t p, std::uint8_t lvl) {
    if (s.tick <= 24999 || lvl == 1) return;
    if (lvl != 2 && lvl != 3) {
        MoveBase(s, p, Rd(s) % 3 == 1);
        return;
    }
    if (Build(s, p, lvl == 2 ? 9 : 6)) {
        Build(s, p, 9);
        MoveBase(s, p, false);
    }
}


bool NoneBuilding(const GameState& s, std::uint8_t p, std::uint16_t t) {
    for (const House& h : s.players[p].houses) {
        if (h.type == t && h.is_destroyed == 0 && h.field_020 != 0) return false;
    }
    return true;
}

bool NoneLive(const GameState& s, std::uint8_t p, std::uint16_t t) { return CountHouses(s, p, t) == 0; }


std::int32_t NthBuilt(const GameState& s, std::uint8_t p, std::uint16_t t, std::uint8_t nth) {
    if (nth == 0) return -1;
    std::uint8_t n = 0;
    const std::vector<House>& hs = s.players[p].houses;
    for (std::size_t k = 0; k < hs.size(); ++k) {
        if (Built(hs[k]) && hs[k].type == t) ++n;
        if (n == nth) return static_cast<std::int32_t>(k);
    }
    return -1;
}


bool RepeatAt(GameState& s, std::uint8_t p, std::uint16_t t, std::uint16_t k) {
    // A3 (Plus): soldiers instead of mortar men at the share, or while their gold upkeep cannot be paid (stage 4)
    if (t == 5 && (AI_MixFull(s, p, t) || AI_CannotFeed(s, p, t))) t = 2;
    if (DearUnit(s, p, t) && SavingForPort(s, p)) t = 2;     // Plus: soldiers while saving for the port
    const std::uint8_t nation = House_TrainNation(s, p, k);  // B263
    if (!Player_CanAfford(s, p, t, nation)) return false;
    Player_PayForUnit(s, p, t, nation);
    House_Enqueue(s, p, k, t, true);
    return true;
}






bool BuildOrBuy(GameState& s, std::uint8_t p, std::uint16_t t) {
    if (CanBuild(s, p, t)) {
        Build(s, p, t);
        return true;
    }
    const Player& pl = s.players[p];
    const HouseType& ht = s.house_types[pl.nation][t % 25];
    for (std::uint16_t req : ht.prereq) {
        if (req < 25 && pl.houses_done[req] != 0) continue;
        if (NoneBuilding(s, p, req)) Build(s, p, s.original_rules ? t : req);
        return false;
    }
    const auto& c = ht.cost;  // 0 food, 1 wood, 2 iron, 3 gold, 4 oil
    Market_Buy(s, p, c[0], c[3], c[2], c[1], c[4]);
    if (!CanBuild(s, p, t)) return false;
    Build(s, p, t);
    return true;
}



void Script_4b1870(GameState& s, std::uint8_t p) {
    if (!(Rd(s) < 0x7d1 && s.tick > 4999)) return;
    AIScript& sc = *s.players[p].script;
    for (const Player& q : s.players) {
        if (q.is_lost != 0 || q.house_array_freed != 0 || q.control_type == 1) continue;
        const auto raise = [](std::uint16_t& w, std::uint16_t v) { w = w < v ? v : w; };
        raise(sc.want_h[4], static_cast<std::uint16_t>(q.houses_done[4] + 4));
        raise(sc.want_h[0x10], static_cast<std::uint16_t>(q.houses_done[0x10] + 1));
        raise(sc.want_h[0x14], static_cast<std::uint16_t>(q.houses_done[0x14] + 1));
    }
}




void Script_4b1ad0_Level3(GameState& s, std::uint8_t p, std::uint8_t lvl) {
    Player& pl = s.players[p];
    AIScript& sc = *pl.script;
    const double gold0 = pl.gold;
    const float pu = pl.power_used, p6c4 = pl.power_6c4, p6e4 = pl.power_6e4, pm = pl.power_made;
    if (Rd(s) % 0x14 == 1) {
        sc.field_70c = 5;
        return;
    }
    Script_4b1870(s, p);
    const auto valid = [&](std::int32_t k) {
        return k >= 0 && static_cast<std::size_t>(k) < pl.houses.size() &&
               pl.houses[static_cast<std::size_t>(k)].is_destroyed == 0 &&
               pl.houses[static_cast<std::size_t>(k)].type == 4;
    };
    if (!valid(sc.field_704)) {
        sc.field_704 = NthBuilt(s, p, 4, 1);
    } else if (pl.houses[static_cast<std::size_t>(sc.field_704)].queue_count < 1000 &&
               Player_CanAfford(s, p, 2, House_TrainNation(s, p, static_cast<std::uint16_t>(sc.field_704)))) {
        Player_PayForUnit(s, p, 2, House_TrainNation(s, p, static_cast<std::uint16_t>(sc.field_704)));
        House_Enqueue(s, p, static_cast<std::uint16_t>(sc.field_704), 2, true);
    }
    // A3 (Plus): soldiers instead of mortar men at the share, or while their gold upkeep cannot be paid (stage 4)
    const std::uint16_t t708 = AI_MixFull(s, p, 5) || AI_CannotFeed(s, p, 5) ? 2 : 5;
    if (!valid(sc.field_708)) {
        sc.field_708 = NthBuilt(s, p, 4, 2);
    } else if (pl.houses[static_cast<std::size_t>(sc.field_708)].queue_count < 0x19 && gold0 > 3000.0 &&
               Player_CanAfford(s, p, t708, House_TrainNation(s, p, static_cast<std::uint16_t>(sc.field_708)))) {
        Player_PayForUnit(s, p, t708, House_TrainNation(s, p, static_cast<std::uint16_t>(sc.field_708)));


        House_Enqueue(s, p, static_cast<std::uint16_t>(s.original_rules ? sc.field_704 : sc.field_708), t708, true);
    }
    if (s.climate != 3) {
        const std::int32_t k = NthBuilt(s, p, 6, static_cast<std::uint8_t>((Rd(s) & 1) + 1));
        if (k >= 0 && pl.houses[static_cast<std::size_t>(k)].queue_count != 0xffff &&
            Player_CanAfford(s, p, 0, House_TrainNation(s, p, static_cast<std::uint16_t>(k)))) {
            Player_PayForUnit(s, p, 0, House_TrainNation(s, p, static_cast<std::uint16_t>(k)));
            House_Enqueue(s, p, static_cast<std::uint16_t>(k), 0, true);
        }
    }
    TrainFive(s, p, 2, 0x14);
    TrainFive(s, p, 2, 0x14);
    if (Rd(s) < 100) Script_4b1580(s, p, lvl);
    if (Rd(s) < 10000) {
        if (NoneLive(s, p, 2) && NoneBuilding(s, p, 2)) {
            BuildOrBuy(s, p, 2);
        } else if (NoneLive(s, p, 5) && NoneBuilding(s, p, 5)) {
            BuildOrBuy(s, p, 5);
        }
    }
    if (Rd(s) < 3000) BuildOrBuy(s, p, 0x10);
    if (Rd(s) < 3000) BuildOrBuy(s, p, 0x18);
    if (Rd(s) < 8000) BuildOrBuy(s, p, 7);
    Script_4b1130(s, p, 10000, 30000);
    if (pl.houses_done[0xf] < 3 && NoneBuilding(s, p, 0xf)) BuildOrBuy(s, p, 0xf);
    if (Rd(s) < 2000) BuildOrBuy(s, p, 0xb);
    if (Rd(s) < 2000) BuildOrBuy(s, p, 10);
    if (pl.houses_done[0xe] == 0 && NoneBuilding(s, p, 0xe)) BuildOrBuy(s, p, 0xe);
    if (p6e4 * pm < pu / p6c4 && NoneBuilding(s, p, 0)) Build(s, p, 0);
    if (pl.houses_done[0x10] == 0) {
        if (NoneBuilding(s, p, 0x10)) BuildOrBuy(s, p, 0x10);
    } else if (gold0 > 5000.0) {
        if (Rd(s) < 10000) TrainOne(s, p, 0x16, 3);
        if (Rd(s) < 10000) TrainOne(s, p, 0x15, 3);
    }
    const auto& n = pl.unit_count;
    if (pl.houses_done[0x13] == 0) {
        if (NoneBuilding(s, p, 0x13)) BuildOrBuy(s, p, 0x13);
    } else {
        bool tanks = true;
        if (n[0xd] < 3 || n[0xc] != 0 || n[0xb] != 0) {
            if (!(gold0 > 5000.0)) {
                tanks = false;
            } else if (Rd(s) < 1000 && n[0xd] < 10) {
                TrainOne(s, p, 0xd, 1);
            }
        }
        if (tanks && gold0 > 5000.0) {
            if (Rd(s) < 16000) TrainOne(s, p, 0xc, 1);
            if (Rd(s) < 16000) TrainOne(s, p, 0xb, 1);
        }
    }
    if (pl.houses_done[0x14] == 0) {
        if (NoneBuilding(s, p, 0x14)) BuildOrBuy(s, p, 0x14);
    } else {
        if (Rd(s) < 10000) TrainOne(s, p, 8, 1);
        if (Rd(s) < 10000) TrainOne(s, p, 9, 1);
        if (Rd(s) < 10000) TrainOne(s, p, 10, 1);
    }
    std::int32_t idle_workers = 0;
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        const Unit& u = pl.unit_slots[k];
        if (Unit_Type(s, p, u).move_class == 6 && Idle(s, p, u)) ++idle_workers;
    }
    if (idle_workers < 10) TrainOne(s, p, 0, 0x14);
    Script_4b1250(s, p, 25000, 20000);
    Script_4b14b0(s, p);
    if ((Rd(s) & 1) != 0) Script_4b14b0(s, p);
}


void Script_4b22c0(GameState& s, std::uint8_t p, std::uint8_t lvl) {
    Player& pl = s.players[p];
    // A3 (Plus): a computer's barracks repeating mortar men switches back to soldiers at the share (no random number
    // read; your own repeated orders stay yours).
    const bool no_mortars = AI_MixFull(s, p, 5) || AI_CannotFeed(s, p, 5);
    for (std::size_t k = 0; k < pl.houses.size() && pl.control_type == 1 && no_mortars; ++k) {
        const House& h = pl.houses[k];
        if (h.type == 4 && h.is_destroyed == 0 && h.queue_count == 0xffff && !h.queue.empty() && h.queue[0] == 5) {
            RepeatAt(s, p, 2, static_cast<std::uint16_t>(k));
        }
    }
    if (pl.iron < 5000.0 || pl.food < 5000.0) {
        if (Rd(s) < 20000) return;
    }
    if (pl.field_9b2b3a == 0 || (lvl != 2 && lvl != 3)) return;
    const bool ai = pl.control_type == 1;
    for (std::size_t k = 0; k < pl.houses.size(); ++k) {
        const House& h = pl.houses[k];
        if (h.type != 4 || h.is_destroyed != 0 || h.field_020 != 0) continue;
        if (h.queue_count != 0) {
            if (!ai) continue;
            if (Rd(s) >= 4000) continue;
        }
        if (lvl == 2) {
            const std::uint16_t r = Rd(s);
            if (r < 3000 && pl.gold > 3000.0) {
                OrderAt(s, p, pl.houses[k], 5, 1);
            } else {
                OrderAt(s, p, pl.houses[k], 2, 5);
            }
            continue;
        }



        const auto k16 = static_cast<std::uint16_t>(k);
        bool done = false;
        for (const std::uint16_t t : {std::uint16_t{5}, std::uint16_t{3}, std::uint16_t{6}}) {
            if (Rd(s) < 3000 && Delta(pl, &HistSample::gold) > 0.0) {
                if (ai) {
                    RepeatAt(s, p, t, k16);
                } else {
                    OrderAt(s, p, pl.houses[k], t, 1);
                }
                done = true;
                break;
            }
        }
        if (done || !(Delta(pl, &HistSample::iron) > 0.0)) continue;
        if (ai) {
            RepeatAt(s, p, 2, k16);
        } else {
            OrderAt(s, p, pl.houses[k], 2, 6);
        }
    }
}


void Script_4b1450(GameState& s, std::uint8_t p, std::uint16_t b) {
    if (!(Rd(s) > b) && AI_FindIdle(s, p, 2, 0.0f, 0.0f, 999999.0f) == nullptr) TrainFive(s, p, 2, 10);
}


std::uint16_t KindThreshold(float k, std::uint8_t lvl) {
    const double inv = static_cast<double>(1.0f) / static_cast<double>(4 - static_cast<std::int32_t>(lvl));
    return static_cast<std::uint16_t>(Ftol(static_cast<double>(k) * inv));
}



void AIScript_Kind4(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    const std::uint8_t lvl = pl.ai_level;
    const std::uint16_t a = KindThreshold(819.0f, lvl), b = KindThreshold(1638.0f, lvl);
    const std::uint16_t c = KindThreshold(3276.0f, lvl), d = KindThreshold(6553.0f, lvl);
    const std::uint16_t e = KindThreshold(10922.0f, lvl);
    const auto& n = pl.unit_count;
    if (n[0xd] < 10 && Rd(s) < b) TrainOne(s, p, 0xd, 2);
    const std::uint16_t hangars = Count16(s, p, 0x14);
    if (hangars < 4 || (hangars < 8 && hangars < (n[10] + n[9] + n[8] + n[7]) / 5 + 1)) Build(s, p, 0x14);
    Script_4b1130(s, p, a, d);
    Script_4b1250(s, p, d, c);
    if (Rd(s) < b) TrainOne(s, p, 10, 2);
    if (Rd(s) < b) TrainOne(s, p, 9, 2);
    if (Rd(s) < c) TrainOne(s, p, 8, 2);
    if (n[7] == 0 && Rd(s) < a) TrainOne(s, p, 7, 1);
    Market(s, p, lvl);
    Script_4b22c0(s, p, lvl);
    if (Count16(s, p, 0x13) < 2) Build(s, p, 0x13);
    Script_4b1450(s, p, b);
    if (Rd(s) < e) Script_4b14b0(s, p);
}




void AIScript_Kind5(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    const std::uint8_t lvl = pl.ai_level;
    const double inv = static_cast<double>(1.0f) / static_cast<double>(4 - static_cast<std::int32_t>(lvl));
    const auto a = static_cast<std::uint16_t>(Ftol(static_cast<double>(819.0f) * inv));
    const auto b = static_cast<std::uint16_t>(Ftol(static_cast<double>(3276.0f) * inv));
    const auto c = static_cast<std::uint16_t>(Ftol(static_cast<double>(6553.0f) * inv));
    const auto d = static_cast<std::uint16_t>(Ftol(inv * static_cast<double>(10922.0f)));
    const auto air = static_cast<std::uint16_t>(CountHouses(s, p, 0x14));
    const std::int32_t planes = pl.unit_count[7] + pl.unit_count[8] + pl.unit_count[9] + pl.unit_count[10];
    if (air < 3 || (air < 5 && air < planes / 5 + 1)) Build(s, p, 0x14);
    Script_4b1130(s, p, b, d);
    Script_4b1250(s, p, c, b);
    if (Rd(s) < b) TrainOne(s, p, 10, 2);
    if (Rd(s) < b) TrainOne(s, p, 9, 2);
    if (Rd(s) < c) TrainOne(s, p, 8, 2);
    if (pl.unit_count[7] == 0 && Rd(s) < a) TrainOne(s, p, 7, 2);
    if (pl.unit_count[0x11] < 3 && Rd(s) < c) TrainOne(s, p, 0x11, 1);
    TrainOne(s, p, 0x12, 1);
    TrainOne(s, p, 0x13, 1);
    if (Rd(s) < d) TrainOne(s, p, 0x14, 2);
    TrainFive(s, p, 2, 10);
    // Plus on a water map: with every enemy within walking reach the army grows as it does with a transport.
    if (pl.unit_count[0x11] != 0 || (s.plus_rules && s.land_map == 0 && AllByLand(s, p))) {
        if (Rd(s) < d) TrainFive(s, p, 2, 10);
        if (Rd(s) < c) TrainFive(s, p, 5, 10);
        if (Rd(s) < c) TrainFive(s, p, 6, 10);
        if (Rd(s) < c) TrainOne(s, p, 3, 10);
        if (Rd(s) < c) TrainFive(s, p, 0xe, 10);
    }
    Market(s, p, lvl);
    if (s.tick > 15000 && pl.wood < 50000.0) Market_Buy(s, p, 50000.0f, 10000.0f, 80000.0f, 60000.0f, 0.0f);
    const auto plants = static_cast<std::uint16_t>(CountHouses(s, p, 0x13));
    if (plants < 1 || (plants < 2 && s.tick > 25000)) Build(s, p, 0x13);
    if (Rd(s) < b && pl.unit_count[0xd] < 7) TrainOne(s, p, 0xd, 1);
    Script_4b1450(s, p, b);
    Script_4b14b0(s, p);
    if (lvl != 3) return;
    Script_4b14b0(s, p);
    const auto barracks = static_cast<std::uint16_t>(CountHouses(s, p, 4));
    if (barracks < 5 || (barracks < 10 && s.tick > 60000)) Build(s, p, 4);
    Script_4b22c0(s, p, 3);
    TrainOne(s, p, 0, 10);
}


void AIScript_Update(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    AIScript& sc = *pl.script;
    if (sc.field_70c != 0) {
        --sc.field_70c;
        return;
    }
    const std::uint8_t lvl = pl.ai_level;
    const double gold0 = pl.gold;
    const std::uint32_t n16 = CountHouses(s, p, 0x10);
    if (pl.iron < 1000.0 && pl.houses_done[10] == 0) {
        if (pl.houses_done[12] == 0) Build(s, p, 12);
        if (pl.houses_done[10] == 0) Build(s, p, 10);
        if (pl.houses_done[8] == 0) Build(s, p, 8);
    }
    if (pl.ai_kind == 4 || pl.ai_kind == 5 || lvl > 3) {
        if (pl.ai_kind == 5) {
            AIScript_Kind5(s, p);
        } else if (pl.ai_kind == 4) {
            AIScript_Kind4(s, p);
        } else {
            Unported(s, orig::kAiScriptUpdate);  // a level above 3: 4 - lvl is 0 or below (the original divides by it)
        }
        return;
    }
    // The thresholds: Kf * (1.0f / (4 - lvl)) on the x87, truncated.
    static constexpr float kKf[8] = {205.0f, 410.0f, 819.0f, 1638.0f, 3276.0f, 6553.0f, 8192.0f, 10922.0f};
    const double inv = static_cast<double>(1.0f) / static_cast<double>(4 - lvl);
    std::array<std::uint16_t, 8> K{};
    for (std::size_t j = 0; j < 8; ++j) K[j] = static_cast<std::uint16_t>(Ftol(static_cast<double>(kKf[j]) * inv));
    bool bv5 = false;
    for (const House& h : pl.houses) {
        if (Built(h) && h.type == 4 && h.queue_count == 0) {
            bv5 = true;
            break;
        }
    }
    const std::uint16_t n4 = Count16(s, p, 4);
    if (n4 < 5 && Rd(s) < K[5]) Build(s, p, 4);
    if (pl.unit_count[2] < 100) TrainFive(s, p, 2, 20);
    std::int32_t div = 150;
    if (lvl == 3) div = s.tick < 6000 ? 50 : 100;
    if (!bv5 && static_cast<std::int32_t>(n4) <= pl.units_alive / div) {
        if (Rd(s) < K[7]) {
            bool unfinished = false;
            for (const House& h : pl.houses) unfinished |= h.type == 4 && h.is_destroyed == 0 && h.field_020 != 0;
            if (!unfinished) Build(s, p, 4);
        }
    }
    Script_4b1130(s, p, K[2], K[7]);
    if (Rd(s) < K[2] && CountHouses(s, p, 0x17) <= 2) Build(s, p, 0x17);
    if (s.climate != 3 && Rd(s) < K[4]) TrainOne(s, p, 0, 10);
    Script_4b1250(s, p, K[5], K[4]);
    if (Rd(s) < K[7]) TrainFive(s, p, 2, 10);
    if (gold0 > 6000.0 && Rd(s) < K[5]) TrainOne(s, p, 5, 10);
    if (gold0 > 5000.0 && n16 != 0 && Rd(s) < K[4]) TrainFive(s, p, 0xe, 10);
    if (gold0 > 3000.0 && n16 != 0 && Rd(s) < K[3]) TrainOne(s, p, 6, 5);
    if (gold0 > 5000.0 && Rd(s) < K[4]) TrainOne(s, p, 3, 10);
    if (Rd(s) < K[4]) TrainFive(s, p, 4, 10);
    if (gold0 > 6000.0 && Rd(s) < K[4]) TrainOne(s, p, 0x10, 10);
    if (Rd(s) < K[2]) TrainOne(s, p, 10, 2);
    if (Rd(s) < K[2]) TrainOne(s, p, 9, 2);
    if (Rd(s) < K[2]) TrainOne(s, p, 8, 2);
    if (Rd(s) < K[2]) TrainOne(s, p, 7, 2);
    if (Rd(s) < K[0]) TrainOne(s, p, 0xb, 1);
    if (Rd(s) < K[0]) TrainOne(s, p, 0xc, 1);
    if (Rd(s) < K[1]) TrainOne(s, p, 0xd, 1);
    if (pl.gold > 120000.0 && pl.food > 200000.0) {  // the rich block, with the live stocks (docs/ai.md 10.3)
        if (!bv5 && Rd(s) < K[3]) Build(s, p, 4);
        if (Rd(s) < K[0]) Build(s, p, 0x18);
        if (Rd(s) < K[4]) Build(s, p, 9);
        if (Rd(s) < K[1]) Build(s, p, 6);
        if (Rd(s) < K[2]) Build(s, p, 0x17);
        if (Rd(s) < K[0]) Build(s, p, 0x10);
        if (Rd(s) < K[7]) TrainMany(s, p, 2, 10, 50);
        if (Rd(s) < K[7]) TrainFive(s, p, 0xe, 10);
        if (Rd(s) < K[4]) TrainFive(s, p, 5, 10);
        if (Rd(s) < K[4]) TrainFive(s, p, 6, 10);
        if (Rd(s) < K[4]) TrainFive(s, p, 0x16, 10);
        if (Rd(s) < K[4]) TrainFive(s, p, 0x15, 10);
        if (Rd(s) < K[5]) TrainFive(s, p, 4, 0x33);
        if (Rd(s) < K[5]) TrainFive(s, p, 0x10, 0x33);
        if (Rd(s) < K[2]) TrainOne(s, p, 0xb, 2);
        if (Rd(s) < K[2]) TrainOne(s, p, 0xc, 2);
        if (Rd(s) < K[3]) TrainOne(s, p, 0xd, 2);
        const auto& c = pl.unit_count;
        const std::uint16_t n19 = Count16(s, p, 0x13);  // tank plants for the tanks (types 0xb, 0xc, 0xd, 0x18)
        if (n19 < 5 && n19 < (c[0x18] + c[0xd] + c[0xc] + c[0xb]) / 5 + 1) Build(s, p, 0x13);
        if (Rd(s) < K[3]) TrainOne(s, p, 10, 2);
        if (Rd(s) < K[4]) TrainOne(s, p, 9, 2);
        if (Rd(s) < K[4]) TrainOne(s, p, 8, 2);
        if (Rd(s) < K[3]) TrainOne(s, p, 7, 2);
        const std::uint16_t n20 = Count16(s, p, 0x14);  // hangars for the aircraft (types 7 to 10)
        if (n20 < 5 && n20 < (c[10] + c[9] + c[8] + c[7]) / 5 + 1) Build(s, p, 0x14);
    }
    Market(s, p, lvl);

    const double used = static_cast<double>(pl.power_used) / static_cast<double>(pl.power_6c4);
    double m = static_cast<double>(pl.power_6e4) * static_cast<double>(pl.power_made);
    m = m + m;
    if (m < used && Rd(s) < 3000) Build(s, p, 0);
    if (Rd(s) < K[6]) Script_4b14b0(s, p);
    if (lvl == 2) {
        if (Rd(s) < 50) Script_4b1580(s, p, lvl);
    } else if (lvl == 3) {
        Script_4b1ad0_Level3(s, p, lvl);
    }
    Script_4b22c0(s, p, lvl);
}


bool TowerGap(const GameState& s, std::uint8_t p, const House& h) {
    static constexpr std::uint16_t kTypes[9] = {0, 6, 10, 11, 12, 13, 15, 20, 22};
    if (std::find(std::begin(kTypes), std::end(kTypes), h.type) == std::end(kTypes)) return false;
    const int pad = h.type == 15 ? 20 : 12;
    Cell row{h.map_x, h.map_y};
    for (int i = 0; i < (h.type == 15 ? 10 : 6); ++i) row = Cell_Step(row, 6);
    const HouseFootprint& fp = House_Footprint(s, p, h);
    const auto rows = static_cast<std::uint16_t>(fp.a + pad), cols = static_cast<std::uint16_t>(fp.b + pad);
    const std::uint32_t cells = static_cast<std::uint32_t>(s.map.width) * s.map.height;
    std::uint8_t n15 = 0;
    for (std::uint16_t r = 0; r < rows; ++r) {
        Cell c = row;
        for (std::uint16_t k = 0; k < cols; ++k) {
            const auto idx = static_cast<std::uint32_t>(static_cast<std::int32_t>(s.map.width) * c.y + c.x);
            if (idx < cells) {
                const std::int32_t o = s.map.objects[idx];
                std::int32_t q = o / 1000;
                const std::int32_t rem = o % 1000;
                if (q != 0) {
                    --q;
                    if (rem != h.num && q == p && static_cast<std::size_t>(rem) < s.players[p].houses.size()) {
                        const std::uint16_t t = s.players[p].houses[static_cast<std::size_t>(rem)].type;
                        if (t == 0x15 && ++n15 > 1) return false;
                        if (t == 7 || t == 1) return false;
                    }
                }
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    return true;
}


bool ObjectFree(const GameState& s, std::uint16_t o) {
    auto byte = [&](int table, std::size_t i) -> std::uint8_t {
        const std::vector<std::uint8_t>& t = s.object_blocking[static_cast<std::size_t>(table)];
        return i < t.size() ? t[i] : 0xff;
    };
    if (o == 100 || o == 99) return false;
    if (o < 1000) return true;
    if (o < 9000) return false;
    if (o < 0x251c) return byte(0, o - 9000u) != 1;
    if (o < 0x2580) return byte(1, o - 0x251cu) != 0;
    if (o >= 0x25e4) return true;
    if (o >= 0x25de) return false;
    return byte(2, o - 0x2580u) != 0;
}


bool ResourceOk(std::uint16_t o, std::uint16_t type) {
    if (o == 0x2581) return type == 0xd;
    if (o == 0x2582) return type == 0xb;
    if (o == 0x2583) return type == 0xa;
    if (o >= 0x25de && o <= 0x25e3) return type == 0xf || type == 0xd || type == 0xa || type == 0xb;
    return true;
}

}  // namespace


bool StandsOnSite(const UnitType& ut, std::uint16_t t) {
    if (t == 0) return true;
    if (t == 0x17) return false;
    const std::uint8_t mc = ut.move_class;
    return mc == 2 || mc == 4 || mc == 5 || mc == 7 || mc == 8 || mc == 9 || mc == 10 || mc == 16;
}

namespace {




bool FarmSite(const GameState& s, std::uint8_t nation, std::int32_t x, std::int32_t y) {
    const Map& m = s.map;
    const std::int32_t W = m.width, H = m.height;
    if (x < 4 || y < 10 || x >= W - 3 || y >= H - 9) return false;
    const HouseFootprint fp = s.house_footprints[nation % 6][0xf];
    const std::int32_t h0 = m.heights[static_cast<std::size_t>(W) * y + x];
    auto resource = [](std::uint16_t o) { return o == 0x2581 || o == 0x2582 || o == 0x2583; };
    Cell row{x, y + 8};
    for (std::uint16_t r = 0; r < static_cast<std::uint16_t>(fp.b + 10); ++r) {
        Cell c = row;
        for (std::uint16_t k = 0; k < static_cast<std::uint16_t>(fp.a + 8); ++k) {
            if (c.x >= 4 && c.y >= 10 && c.x < W - 3 && c.y < H - 9) {
                const std::size_t i =
                    static_cast<std::size_t>(W) * static_cast<std::size_t>(c.y) + static_cast<std::size_t>(c.x);
                const std::uint16_t o = m.objects[i];
                if ((o > 999 && o < 9000) || resource(o)) return false;
                if (std::abs(static_cast<std::int32_t>(m.heights[i]) - h0) > 10) return false;
                for (std::uint8_t d = 1; d < 9; ++d) {
                    const Cell n = Cell_Step(c, d);
                    if (n.x >= 0 && n.y >= 0 && n.x < W && n.y < H &&
                        resource(m.objects[static_cast<std::size_t>(W) * static_cast<std::size_t>(n.y) +
                                           static_cast<std::size_t>(n.x)])) {
                        return false;
                    }
                }
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    return true;
}



bool SawmillSite(const GameState& s, std::int32_t x, std::int32_t y) {
    const std::int32_t W = s.map.width, H = s.map.height;
    std::int32_t x0 = x - 5, y0 = y - 10, x1 = x + 5, y1 = y + 10;
    auto clamp = [&](std::int32_t& cx, std::int32_t& cy) {
        if (cx <= 3) cx = 4;
        if (cy <= 9) cy = 10;
        if (cx >= W - 3) cx = W - 4;
        if (cy >= H - 9) cy = H - 10;
    };
    clamp(x0, y0);
    clamp(x1, y1);
    std::uint16_t trees = 0;
    for (std::int32_t cx = x0; cx < x1; ++cx) {
        for (std::int32_t cy = y0; cy < y1; ++cy) {
            if (static_cast<std::uint16_t>(cx) >= W || static_cast<std::uint16_t>(cy) >= H) continue;  // logged
            const std::uint16_t o = s.map.objects[static_cast<std::size_t>(W) * static_cast<std::uint16_t>(cy) +
                                                  static_cast<std::uint16_t>(cx)];
            if (o >= 9000 && o < 0x251c) ++trees;
            if (o >= 1000 && o < 9000) {
                const std::size_t pi = o / 1000u - 1, k = o % 1000u;
                if (pi < s.players.size() && k < s.players[pi].houses.size() && s.players[pi].houses[k].type == 12) {
                    return false;
                }
            }
        }
    }
    return trees >= 15;
}


std::int64_t RuleIndex(const Map& m, Cell c) {
    const std::int64_t i = static_cast<std::int64_t>(m.width) * c.y + c.x;
    return i < 0 || i >= static_cast<std::int64_t>(m.width) * m.height ? -1 : i;
}

// A player house (1000 .. 8999) or a tree (9000 .. 0x251b) on the cell.
bool HouseOrTree(std::uint16_t o) { return (o >= 9000 && o < 0x251c) || (o >= 1000 && o < 9000); }



bool HangarSite(const GameState& s, std::uint8_t nation, std::int32_t x, std::int32_t y) {
    const Map& m = s.map;
    const HouseFootprint fp = s.house_footprints[nation % 6][0x14];
    Cell row{x, y};
    for (int k = 0; k < 6; ++k) row = Cell_Step(row, 2);
    for (int r = 0; r < 6; ++r) {
        Cell c = row;
        for (std::uint8_t k = 0; k < fp.a; ++k) {
            const std::int64_t i = RuleIndex(m, c);
            if (i < 0 || HouseOrTree(m.objects[static_cast<std::size_t>(i)])) return false;
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    return true;
}



bool FreeForPlant(const Map& m, std::size_t i) {
    return m.cell_units[i].empty() && m.pass_large[i] == 0 && !HouseOrTree(m.objects[i]);
}





bool TankPlantSite(const GameState& s, std::uint8_t nation, std::int32_t x, std::int32_t y) {
    const Map& m = s.map;
    const std::int32_t W = m.width, H = m.height;
    const HouseFootprint fp = s.house_footprints[nation % 6][0x13];
    Cell row = Cell_Step(Cell_Step(Cell{x, y}, 2), 2);
    for (int r = 0; r < 2; ++r) {
        if (fp.a != 0) {
            const Cell c = row;
            if (c.x <= 3 || c.y <= 9 || c.x >= W - 3 || c.y >= H - 9) return false;
            if (!FreeForPlant(m, static_cast<std::size_t>(RuleIndex(m, c)))) return false;
        }
        row = Cell_Step(row, 4);
    }
    return true;
}



bool DepotSite(const GameState& s, std::uint8_t nation, std::int32_t x, std::int32_t y) {
    const Map& m = s.map;
    const HouseFootprint fp = s.house_footprints[nation % 6][0x10];
    Cell row = Cell_Step(Cell_Step(Cell{x, y}, 2), 2);
    for (int r = 0; r < 2; ++r) {
        Cell c = row;
        for (std::uint8_t k = 0; k < fp.a; ++k) {
            const std::int64_t i = RuleIndex(m, c);
            if (i < 0 || !FreeForPlant(m, static_cast<std::size_t>(i))) return false;
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    return true;
}



bool PortSite(const GameState& s, std::uint8_t nation, std::int32_t x, std::int32_t y) {
    const Map& m = s.map;
    const std::int32_t k = s.house_footprints[nation % 6][0x16].b >> 1;
    for (std::int32_t dx = -9; dx <= 9; ++dx) {
        for (std::int32_t dy = -18; dy <= 17; ++dy) {
            const Cell c{x + dx, y - k + dy};
            if (c.x < 0 || c.y < 0 || c.x >= m.width || c.y >= m.height) continue;
            const std::uint16_t o = m.objects[static_cast<std::size_t>(m.width) * c.y + c.x];
            if (o < 1000 || o >= 9000) continue;
            const std::size_t pi = o / 1000u - 1, hk = o % 1000u;
            if (pi < s.players.size() && hk < s.players[pi].houses.size() && s.players[pi].houses[hk].type == 0x16) {
                return false;
            }
        }
    }
    return true;
}





bool CanPlaceHouse(GameState& s, std::uint8_t p, std::int32_t x, std::int32_t y, std::uint16_t type,
                   std::uint8_t nation) {
    const Map& m = s.map;
    const std::int32_t W = m.width, H = m.height;
    auto edge = [&](std::int32_t cx, std::int32_t cy) { return cx <= 3 || cy <= 9 || cx >= W - 3 || cy >= H - 9; };
    if (edge(x, y)) return false;
    const bool mine = type == 0xd || type == 0xa || type == 0xb;
    std::uint8_t lo = m.heights[static_cast<std::size_t>(W) * y + x], hi = lo;
    const HouseFootprint fp = s.house_footprints[nation][type % 25];
    std::int32_t trees = 0;
    std::int16_t land = 0, water = 0;  // a port: the footprint cells off and on the obstacle grid
    Cell row = Cell_Step(Cell{x, y}, 6);
    for (int r = 0; r < fp.b + 2; ++r) {
        Cell c = row;
        for (int k = 0; k < fp.a + 2; ++k) {
            if (edge(c.x, c.y)) return false;
            const auto idx = static_cast<std::size_t>(W) * c.y + c.x;
            const std::uint16_t o = m.objects[idx];
            if (!ObjectFree(s, o)) {
                if ((o == 99 || o == 100) && !mine) return false;
                if (o != 100 && o < 9000) return false;
                ++trees;
            }
            if (!ResourceOk(o, type)) return false;
            if (m.flags[idx] == 1 && !mine) return false;
            if (Cell_Obstacle(m, c.x, c.y)) {
                if (type != 0x16) return false;
                ++water;
            } else {
                ++land;
            }
            const std::vector<UnitRef>& list = m.cell_units[idx];
            for (std::size_t i = 0; i < list.size() && i < 40; ++i) {
                const Unit* u = Unit_Find(s, list[i]);
                if (u == nullptr || u->is_deleted != 0 || !(u->field_024 < 1.0f)) continue;
                if (list[i].player != p) return false;
                const UnitType& ut = Unit_Type(s, list[i].player, *u);
                if (ut.is_air != 0) continue;
                if (!StandsOnSite(ut, u->type)) return false;
            }
            const std::uint8_t h = m.heights[idx];
            if (h < lo) {
                lo = h;
            } else if (h > hi) {
                hi = h;
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    if (type == 0x16 && (land < 0x14 || water < 0x32 || Port_ShipSide(s, x, y, fp) == 0)) return false;
    // Ours (B313, the owner 2026-10-22): an iron or gold mine (10, 11) skips box 2's edge band, so an ore deposit
    // near the map's edge can get a mine; the type rule below still needs its deposit at (x, y). The anchor and box 1
    // keep the band (the mine, its door and its border stay inside), box 2's resource check reads only map cells.
    // original_rules keeps the refusal (IDS_CANTBUILD_CORNER_NEAR).
    const bool ore_mine = !s.original_rules && (type == 10 || type == 11);
    row = Cell{x, y + 6};
    for (int r = 0; r < fp.b + 6; ++r) {
        Cell c = row;
        for (int k = 0; k < fp.a + 6; ++k) {
            if (!ore_mine && edge(c.x, c.y)) return false;
            if (c.x >= 0 && c.y >= 0 && c.x < W && c.y < H &&
                !ResourceOk(m.objects[static_cast<std::size_t>(W) * c.y + c.x], type)) {
                return false;
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    bool ok = true;
    if (type == 0x16) {
        if (hi - lo > 0x32) return false;  // a port takes a height difference up to 50
    } else {
        ok = !(hi - lo > 20 && !mine);
    }
    if (!(type == 0xc || mine) && static_cast<std::uint16_t>(trees) > 5) ok = false;
    if (!ok) return false;


    switch (type) {
        case 10: return m.objects[static_cast<std::size_t>(W) * y + x] == 0x2583;
        case 11: return m.objects[static_cast<std::size_t>(W) * y + x] == 0x2582;
        case 13: return m.objects[static_cast<std::size_t>(W) * y + x] == 0x2581;
        case 12: return SawmillSite(s, x, y);
        case 15: return s.climate != 3 && FarmSite(s, nation, x, y);
        case 16: return DepotSite(s, nation, x, y);
        case 19: return TankPlantSite(s, nation, x, y);
        case 20: return HangarSite(s, nation, x, y);
        case 22: return PortSite(s, nation, x, y);
        default: return true;
    }
}



Unit* FindWorker(GameState& s, std::uint8_t p, std::int32_t x, std::int32_t y) {
    Player& pl = s.players[p];
    for (int pass = 0; pass < 2; ++pass) {
        Unit* best = nullptr;
        std::int32_t bd = 15000;
        for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
            Unit& u = pl.unit_slots[k];
            if (Unit_Type(s, p, u).move_class != 6) continue;
            if (pass == 0 ? u.work != 0xff : !(u.work == 0xb || u.work == 0xa || u.work == 7)) continue;
            if (u.is_deleted != 0) continue;
            const std::int32_t d =
                2 * std::abs(static_cast<std::int32_t>(u.map_x) - x) + std::abs(static_cast<std::int32_t>(u.map_y) - y);
            if (d < bd) bd = d, best = &u;
        }
        if (best != nullptr) return best;
    }
    return nullptr;
}


void WalkToSite(GameState& s, std::uint8_t p, Unit& u, Cell pt) {
    const std::int32_t dx = pt.x - u.map_x, dy = pt.y - u.map_y;
    const double sq = std::sqrt(static_cast<double>(dx * dx + dy * dy));
    const double len = sq - 7.0;
    if (len < 1.0) return;
    const double inv = static_cast<double>(1.0f) / sq;
    std::int32_t x = Ftol(static_cast<double>(dx) * inv * len) + u.map_x;
    std::int32_t y = Ftol(static_cast<double>(dy) * inv * len) + u.map_y;
    const Map& m = s.map;
    if (x <= 3 || y <= 9 || x >= m.width - 3 || y >= m.height - 9 ||
        m.pass_land[static_cast<std::size_t>(m.width) * y + x] != 0) {
        const Cell c = RelocateTargetLand(s, Cell{x, y});
        x = c.x, y = c.y;
    }
    TUnit_OrderMove(s, p, u, static_cast<float>((2 * x - (~y & 1)) * 48), static_cast<float>(y * 24), 1);
}



void TowerBuild(GameState& s, std::uint8_t p, std::uint16_t hk) {
    Player& pl = s.players[p];
    if (pl.field_9b2b38 == 0) return;
    std::uint16_t type = 0x15;
    if (CountHouses(s, p, 5) != 0) {
        if (CanBuild(s, p, 7)) {
            type = 7;
        } else if (CanBuild(s, p, 1)) {
            type = 1;
        } else if (CanBuild(s, p, 0x15)) {
            type = 0x15;
        } else {
            return;
        }
    }
    if (!CanBuild(s, p, type)) return;
    const House& h = pl.houses[hk];
    Cell row{h.map_x, h.map_y};
    for (int i = 0; i < 6; ++i) row = Cell_Step(row, 6);
    const int pad = h.type == 0xf ? 0x14 : 0xc;
    const HouseFootprint fh = House_Footprint(s, p, h);
    const auto rows = static_cast<std::uint16_t>(fh.a + pad), cols = static_cast<std::uint16_t>(fh.b + pad);
    std::vector<Cell> sites;
    const std::uint32_t cells = static_cast<std::uint32_t>(s.map.width) * s.map.height;
    for (std::uint8_t i = 0; i < rows; ++i) {
        Cell c = row;
        for (std::uint8_t j = 0; j < cols; ++j) {
            const std::uint32_t idx = static_cast<std::uint32_t>(s.map.width) * static_cast<std::uint32_t>(c.y) +
                                      static_cast<std::uint32_t>(c.x);
            if (idx < cells && (s.map.objects[idx] < 1000 || s.map.objects[idx] >= 9000) &&
                CanPlaceHouse(s, p, c.x, c.y, type, pl.nation)) {
                sites.push_back(c);
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    if (sites.empty()) return;
    const Cell site = sites[Rd(s) % sites.size()];
    Unit* w = FindWorker(s, p, site.x, site.y);
    if (w == nullptr) return;
    const std::int32_t dx = site.x - w->map_x, dy = site.y - w->map_y;
    if (dy * dy + 4 * dx * dx < 500) {
        AI_SendWorker(s, p, *w, AI_CloseBuild(s, p, site.x, site.y, type));
        return;
    }
    WalkToSite(s, p, *w, site);
}



// 24 and times 20 moves the base; the candidates on the map are kept in order (none: false). Near (far 0): one read
// picks one of them. Far: 100 random cells (two reads each, 20 cells from the edges) go into a list where each is put
// before the first entry nearer to it than the base is (|dx| + |dy|), else at the end; of the first 20 the one with


bool MoveBase(GameState& s, std::uint8_t p, bool far) {
    if (s.tick < 10000) return false;
    AIScript& sc = *s.players[p].script;
    const std::int32_t W = s.map.width, H = s.map.height;
    if ((sc.field_6f4 != sc.base_x || sc.field_6f8 != sc.base_y) && sc.field_6f4 >= 0 && sc.field_6f8 >= 0 &&
        sc.field_6f4 < W && sc.field_6f8 < H) {
        if (Rd(s) < 16000) {
            sc.base_x = sc.field_6f4, sc.base_y = sc.field_6f8;
            sc.field_6fc = 0;
            return true;
        }
    }
    static constexpr std::int32_t kStep[9][2] = {{0, 0},   {48, -24}, {48, 24}, {-48, 24}, {-48, -24},
                                                 {0, -48}, {0, 48},   {96, 0},  {-96, 0}};
    std::array<std::array<std::int32_t, 2>, 8> cand{};
    for (auto& c : cand) {
        const std::uint32_t d = (Rd(s) & 7u) + 1;
        c = {sc.base_x + kStep[d][0] / 24 * 20, sc.base_y + kStep[d][1] / 24 * 20};
    }
    std::vector<std::array<std::int32_t, 2>> on_map;
    for (const auto& c : cand) {
        if (c[0] >= 0 && c[1] >= 0 && c[0] < W && c[1] < H) on_map.push_back(c);
    }
    if (on_map.empty()) return false;
    if (!far) {
        const auto& c = on_map[R(s, s.rng_index) % on_map.size()];
        ++s.rng_index;
        sc.base_x = c[0], sc.base_y = c[1];
        sc.field_6fc = 0;
    } else {
        const auto mx = static_cast<std::int32_t>(static_cast<std::uint16_t>(W - 0x28));
        const auto my = static_cast<std::int32_t>(static_cast<std::uint16_t>(H - 0x28));
        std::vector<std::array<std::int32_t, 2>> pts;
        for (int k = 0; k < 100; ++k) {
            const std::int32_t x = static_cast<std::int32_t>(R(s, s.rng_index)) % mx + 0x14;
            const std::int32_t y = static_cast<std::int32_t>(R(s, s.rng_index + 1)) % my + 0x14;
            s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
            const std::int32_t to_base = std::abs(y - sc.base_y) + std::abs(x - sc.base_x);
            std::size_t at = pts.size();
            for (std::size_t i = 0; i < pts.size(); ++i) {
                if (to_base > std::abs(y - pts[i][1]) + std::abs(x - pts[i][0])) {
                    at = i;
                    break;
                }
            }
            pts.insert(pts.begin() + static_cast<std::ptrdiff_t>(at), {x, y});
        }
        std::int32_t least = 999999;
        std::size_t pick = 0;
        for (std::size_t i = 0; i < 20; ++i) {
            const std::int32_t t = AI_Threat(s, p, pts[i][0], pts[i][1], sc.base_x, sc.base_y, 0);
            if (t < least) least = t, pick = i;
        }
        sc.base_x = pts[pick][0], sc.base_y = pts[pick][1];
        sc.field_6fc = 0;
    }
    sc.want_h[6] = static_cast<std::uint16_t>(sc.want_h[6] + 2);
    return true;
}





struct SiteLess {
    std::int32_t bx, by;
    static std::int32_t key(std::int32_t dx, std::int32_t dy) {
        dx = std::abs(dx), dy = std::abs(dy);
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(dy * dy) + static_cast<std::uint32_t>(dx * dx));
    }
    std::int32_t key(std::uint32_t v) const {
        return key(static_cast<std::int32_t>(static_cast<std::uint32_t>(bx) - (v & 0xffff)),
                   static_cast<std::int32_t>(static_cast<std::uint32_t>(by) - (v >> 16)));
    }
    std::int32_t key(std::uint64_t v) const {
        return key(static_cast<std::int32_t>(static_cast<std::uint32_t>(bx) - static_cast<std::uint32_t>(v)),
                   static_cast<std::int32_t>(static_cast<std::uint32_t>(by) - static_cast<std::uint32_t>(v >> 32)));
    }
    template <class T>
    bool operator()(T a, T b) const {
        return key(a) < key(b);
    }
};


template <class It>
void Sort_Med3(It a, It m, It l, const SiteLess& less) {
    if (less(*m, *a)) std::iter_swap(m, a);
    if (less(*l, *m)) std::iter_swap(l, m);
    if (less(*m, *a)) std::iter_swap(m, a);
}


template <class It>
void Sort_Median(It f, It m, It l, const SiteLess& less) {
    const std::ptrdiff_t n = l - f;
    if (n > 40) {
        const std::ptrdiff_t st = (n + 1) / 8;
        Sort_Med3(f, f + st, f + 2 * st, less);
        Sort_Med3(m - st, m, m + st, less);
        Sort_Med3(l - 2 * st, l - st, l, less);
        Sort_Med3(f + st, m, l - st, less);
    } else {
        Sort_Med3(f, m, l, less);
    }
}


template <class It>
std::pair<It, It> Sort_Partition(It first, It last, const SiteLess& less) {
    It mid = first + (last - first) / 2;
    Sort_Median(first, mid, last - 1, less);
    It pf = mid, pl = pf + 1;
    while (first < pf && !less(*(pf - 1), *pf) && !less(*pf, *(pf - 1))) --pf;
    while (pl < last && !less(*pl, *pf) && !less(*pf, *pl)) ++pl;
    It gf = pl, gl = pf;
    for (;;) {
        for (; gf < last; ++gf) {
            if (less(*pf, *gf)) continue;
            if (less(*gf, *pf)) break;
            std::iter_swap(pl++, gf);
        }
        for (; first < gl; --gl) {
            if (less(*(gl - 1), *pf)) continue;
            if (less(*pf, *(gl - 1))) break;
            std::iter_swap(--pf, gl - 1);
        }
        if (gl == first && gf == last) return {pf, pl};
        if (gl == first) {
            if (pl != gf) std::iter_swap(pf, pl);
            ++pl;
            std::iter_swap(pf++, gf++);
        } else if (gf == last) {
            if (--gl != --pf) std::iter_swap(gl, pf);
            std::iter_swap(pf, --pl);
        } else {
            std::iter_swap(gf++, --gl);
        }
    }
}


template <class It, class T>
void Sort_PushHeap(It f, std::ptrdiff_t hole, std::ptrdiff_t top, T v, const SiteLess& less) {
    for (std::ptrdiff_t i = (hole - 1) / 2; top < hole && less(f[i], v); i = (hole - 1) / 2) {
        f[hole] = f[i];
        hole = i;
    }
    f[hole] = v;
}


template <class It, class T>
void Sort_AdjustHeap(It f, std::ptrdiff_t hole, std::ptrdiff_t bottom, T v, const SiteLess& less) {
    const std::ptrdiff_t top = hole;
    std::ptrdiff_t i = 2 * hole + 2;
    for (; i < bottom; i = 2 * i + 2) {
        if (less(f[i], f[i - 1])) --i;
        f[hole] = f[i];
        hole = i;
    }
    if (i == bottom) {
        f[hole] = f[bottom - 1];
        hole = bottom - 1;
    }
    Sort_PushHeap(f, hole, top, v, less);
}




template <class It>
void Sort_Sites(It first, It last, std::ptrdiff_t ideal, const SiteLess& less) {
    using T = typename std::iterator_traits<It>::value_type;
    std::ptrdiff_t n;
    for (; (n = last - first) > 32 && ideal > 0;) {
        const auto mid = Sort_Partition(first, last, less);
        ideal = ideal / 2 + (ideal / 2) / 2;
        if (mid.first - first < last - mid.second) {
            Sort_Sites(first, mid.first, ideal, less);
            first = mid.second;
        } else {
            Sort_Sites(mid.second, last, ideal, less);
            last = mid.first;
        }
    }
    if (n > 32) {
        if (n > 1) {
            for (std::ptrdiff_t hole = n / 2; hole > 0;) {
                --hole;
                Sort_AdjustHeap(first, hole, n, T{first[hole]}, less);
            }
        }
        for (std::ptrdiff_t k = n; k > 1; --k) {
            const T v = first[k - 1];
            first[k - 1] = *first;
            Sort_AdjustHeap(first, 0, k - 1, v, less);
        }
    } else if (n > 1) {
        for (It next = first + 1; next != last; ++next) {
            const T v = *next;
            if (less(v, *first)) {
                std::copy_backward(first, next, next + 1);
                *first = v;
            } else {
                It hole = next;
                for (It prev = next - 1; less(v, *prev); --prev) *hole = *prev, hole = prev;
                *hole = v;
            }
        }
    }
}



bool PortSiteSearch(GameState& s, std::uint8_t p, Cell* out) {
    const AIScript& sc = *s.players[p].script;
    std::vector<std::uint32_t>& v = s.port_sites;
    Sort_Sites(v.begin(), v.end(), static_cast<std::ptrdiff_t>(v.size()), SiteLess{sc.base_x, sc.base_y});
    for (const std::uint32_t site : v) {
        const Cell c{static_cast<std::int32_t>(site & 0xffff), static_cast<std::int32_t>(site >> 16)};
        *out = c;
        if (CanPlaceHouse(s, p, c.x, c.y, 0x16, 0)) return true;
    }
    return false;
}

// Plus: a site of the list where the player may place a port now (the list as it stands, nothing sorted).
bool PortSiteFree(GameState& s, std::uint8_t p) {
    for (const std::uint32_t site : s.port_sites) {
        if (CanPlaceHouse(s, p, static_cast<std::int32_t>(site & 0xffff), static_cast<std::int32_t>(site >> 16), 0x16,
                          0))
            return true;
    }
    return false;
}


bool Unit_IsFree(const GameState& s, std::uint8_t p, const Unit& u) {
    return u.work == 0xff && !u.in_group && u.player_unit_attack == 0xff && u.player_house_attack == 0xff &&
           u.is_deleted == 0 && Unit_Type(s, p, u).speed[p] > 0.5f && u.field_154 == 0;
}



std::array<std::int32_t, 2> Script_PickCoastPoint(GameState& s, AIScript& sc, std::int32_t x, std::int32_t y) {
    std::vector<std::uint64_t>& v = sc.items;
    if (v.empty()) return {sc.base_x, sc.base_y};
    Sort_Sites(v.begin(), v.end(), static_cast<std::ptrdiff_t>(v.size()), SiteLess{x, y});
    const std::size_t n = v.size() < 5 ? v.size() : 5;
    const std::uint64_t pt = v[Rd(s) % n];
    return {static_cast<std::int32_t>(static_cast<std::uint32_t>(pt)), static_cast<std::int32_t>(pt >> 32)};
}



bool Transport_PickLanding(GameState& s, std::uint8_t q, Unit& t) {
    if (!s.players[q].script) return false;
    const auto pt = Script_PickCoastPoint(s, *s.players[q].script, Ftol(t.x), Ftol(t.y));
    t.field_16c = static_cast<float>(pt[0]), t.field_170 = static_cast<float>(pt[1]);
    return true;
}

// Plus (docs/ai.md 10.17): the visits of state 2 a transport with 250 to 499 aboard waits before it sails. The visits
// come at the planner's cadence (every 100, 66 or 33 ticks for Easy, Medium, Difficult), so the wait is about 4,500,
// 3,000 or 1,500 ticks: several times the 750 ticks a soldier (speed 4) needs to walk the 3,000 px of a call, short
// against the 18,500 ticks a transport stood with 363 aboard.
constexpr std::uint8_t kWaitVisits = 45;



// then after six tries back to state 0.
// Ours, Classic and Plus (docs/ai.md 10.17): Board says yes even for a unit that cannot get aboard, so a crowd out of
// reach kept the "nobody came" count at 0 and a transport with 250 to 499 aboard never sailed. Only units on the land
// mass of the boarding point are called and counted (nobody while the ship has no boarding point). Plus: the calls

void Transport_CallUnits(GameState& s, std::uint8_t p, Unit& t) {
    if (t.passengers.size() > 499) return;
    Player& pl = s.players[p];
    std::uint16_t reg = 0;
    if (!s.original_rules) {
        float bx = 0, by = 0;
        if (BoardPointShip(s, t, bx, by, 0xff, nullptr)) reg = RegionAt(s, bx, by);
    }
    std::uint32_t n = 0;
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        Unit& v = pl.unit_slots[k];
        const UnitType& vt = Unit_Type(s, p, v);
        if (vt.field_00b == 0 || vt.move_class == 6 || !Unit_IsFree(s, p, v)) continue;
        const double dy = static_cast<double>(t.y) - v.y, dx = static_cast<double>(t.x) - v.x;
        if (!(std::sqrt(dy * dy + dx * dx) < 3000.0)) continue;
        if (!s.original_rules && (reg == 0 || RegionAt(s, v.x, v.y) != reg)) continue;
        if (Transport_Board(s, p, v, p, t, false)) ++n;
    }
    if (n == 0) {

        if (pl.control_type == 1 || pl.field_9b2d9c != 0) Farm_SetRally(s, p, 4, t.x, t.y, true);
        if (s.plus_rules) {
            if (t.passengers.empty() && t.field_0e8 >= 2 * kWaitVisits) t.field_0e4 = 0, t.field_0e8 = 0;
            return;
        }
        if (++t.field_0e8 < 6) return;
        t.field_0e4 = 0;
    }
    if (s.plus_rules) return;
    t.field_0e8 = 0;
}

// Plus: the enemy whose base's land mass holds the most of our walking units (at least kHold) and the coast point of
// its script nearest to one of them; false when no landing holds.
bool Beachhead(GameState& s, std::uint8_t p, Unit& t, std::uint8_t& target) {
    constexpr std::uint32_t kHold = 20;
    const Player& pl = s.players[p];
    std::uint32_t most = 0;
    std::uint16_t reg = 0;
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        const auto qq = static_cast<std::uint8_t>(q);
        if (s.players[q].is_lost != 0 || !IsEnemy(s, p, qq) || !s.players[q].script) continue;
        const std::uint16_t r = BaseRegion(s, qq);
        if (r == 0 || r == BaseRegion(s, p)) continue;
        std::uint32_t n = 0;
        for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
            const Unit& u = pl.unit_slots[k];
            n += Walker(s, p, u) && RegionAt(s, u.x, u.y) == r ? 1 : 0;
        }
        if (n >= kHold && n > most) most = n, reg = r, target = qq;
    }
    if (most == 0) return false;
    const std::vector<std::uint64_t>& pts = s.players[target].script->items;
    double best = 1e18;
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        const Unit& u = pl.unit_slots[k];
        if (!Walker(s, p, u) || RegionAt(s, u.x, u.y) != reg) continue;
        for (const std::uint64_t pt : pts) {
            const auto x = static_cast<float>(static_cast<std::int32_t>(static_cast<std::uint32_t>(pt)));
            const auto y = static_cast<float>(static_cast<std::int32_t>(pt >> 32));
            const double dx = static_cast<double>(x) - u.x, dy = static_cast<double>(y) - u.y;
            if (dx * dx + dy * dy < best) best = dx * dx + dy * dy, t.field_16c = x, t.field_170 = y;
        }
    }
    return best < 1e18;
}







void Transport_Think(GameState& s, std::uint8_t p, Unit& t, std::uint8_t target) {
    if (target == 0xff) return;
    auto dist = [&t] {
        const double dx = static_cast<double>(t.x) - t.final_x, dy = static_cast<double>(t.y) - t.final_y;
        return std::sqrt(dy * dy + dx * dx);
    };
    auto land = [&s, &t] {
        float x = 0, y = 0;
        return BoardPointShip(s, t, x, y, 0xff, nullptr);
    };
    switch (t.field_0e4) {
        case 0:
            if (!Transport_PickLanding(s, p, t)) return;
            TUnit_OrderMove(s, p, t, t.field_16c, t.field_170, 1);
            t.field_0e4 = 1;
            t.field_07c = 0;
            return;
        case 1:
            if (dist() < 1.0 && land()) {
                t.field_0e8 = 0;
                t.field_07c = 0;
                t.field_0e4 = 2;
                return;
            }
            if (++t.field_0e8 < 6) {
                TUnit_OrderMove(s, p, t, t.field_16c, t.field_170, 1);
                return;
            }
            t.field_0e4 = 0;
            t.field_07c = 0;
            return;
        case 2:
            if (s.plus_rules) {


                if (t.field_0e8 < 0xff) ++t.field_0e8;
                const std::size_t n = t.passengers.size();
                if (n >= 500 || (n > 0xf9 && t.field_0e8 >= kWaitVisits)) {
                    std::uint8_t q = target;
                    if (Beachhead(s, p, t, q) || Transport_PickLanding(s, q, t)) {
                        t.field_0e9 = q;
                        t.field_0e8 = 0;
                        TUnit_OrderMove(s, p, t, t.field_16c, t.field_170, 1);
                        t.field_07c = 0;
                        t.field_0e4 = 3;
                        return;
                    }
                }
                if (Rd(s) % 3 == 1) Transport_CallUnits(s, p, t);
                return;
            }
            if (t.passengers.size() > 0xf9 && (t.passengers.size() == 500 || t.field_0e8 != 0)) {
                if (Transport_PickLanding(s, target, t)) {
                    t.field_0e9 = target;
                    t.field_0e8 = 0;
                    TUnit_OrderMove(s, p, t, t.field_16c, t.field_170, 1);
                    t.field_07c = 0;
                    t.field_0e4 = 3;
                    return;
                }
                if (++t.field_0e8 > 5) {
                    t.field_0e4 = 0;
                    t.field_07c = 0;
                    t.field_0e8 = 0;
                    return;
                }
            }
            if (Rd(s) % 3 == 1) Transport_CallUnits(s, p, t);
            return;
        case 3:
            if (dist() < 100.0) {
                if (land()) {
                    t.field_0e8 = 0;
                    t.field_07c = 0;
                    t.field_0e4 = 4;
                    return;
                }
                Transport_TurnToLand(s, p, t);
            }
            if (++t.field_0e8 > 5) {
                t.field_07c = 0;
                t.field_0e4 = 2;
                if (s.plus_rules) t.field_0e8 = kWaitVisits;  // Plus: sails again at the next visit
                return;
            }
            TUnit_OrderMove(s, p, t, t.field_16c, t.field_170, 1);
            return;
        case 4:
            if (!t.passengers.empty()) {
                if (Transport_CanUnload(s, t)) {
                    t.field_0e4 = 0;
                    t.field_07c = 0;
                    for (std::size_t n = t.passengers.size(); n > 0 && !t.passengers.empty(); --n) {
                        const UnitRef q = t.passengers.front();
                        Unit* u = Unit_Find(s, q);
                        if (u == nullptr || !Transport_GetOut(s, q.player, *u)) break;  // never
                        Player_MoveUnitToFront(s.players[q.player], *u);
                    }
                    return;
                }
                Transport_TurnToLand(s, p, t);
                return;
            }
            t.field_0e4 = 0;
            t.field_07c = 0;
            return;
        default: Unported(s, orig::kTransportThink); return;  // a state the original only logs
    }
}



void AI_NavalTransport(GameState& s, std::uint8_t p, std::uint8_t target) {
    if (s.players[p].unit_count[0x11] == 0) return;
    for (std::size_t i = 0; i < s.ships.size(); ++i) {
        const UnitRef r = s.ships[i];
        Unit* t = r.player == p ? Unit_Find(s, r) : nullptr;
        if (t == nullptr || t->type != 0x11 || t->is_deleted != 0 || t->field_154 != 0 || t->field_142 != 0) continue;
        if (t->work == 0xe && !s.original_rules) continue;  // ours: not while it is still in its port (being built)
        const float dx = t->x - t->final_x, dy = t->y - t->final_y;
        if (std::sqrt(dx * dx + dy * dy) <= 50.0f) Transport_Think(s, p, *t, target);
    }
}





bool FindSite(GameState& s, std::uint8_t p, std::uint16_t t, Cell* out) {
    if (t == 10 || t == 11 || t == 0xd) {
        const std::uint16_t obj = t == 0xd ? 0x2581 : t == 0xb ? 0x2582 : 0x2583;
        const AIScript& sc = *s.players[p].script;
        std::int32_t best = 99999;
        for (std::uint16_t i = 0; i < s.mine_site_count; ++i) {
            const Cell c{s.mine_sites[i][0], s.mine_sites[i][1]};
            if (c.x < 0 || c.y < 0 || c.x >= s.map.width || c.y >= s.map.height) continue;
            if (s.map.objects[static_cast<std::size_t>(s.map.width * c.y + c.x)] != obj) continue;
            if (!CanPlaceHouse(s, p, c.x, c.y, t, s.players[p].nation)) continue;
            const std::int32_t d = std::abs(sc.base_y - c.y) + std::abs(sc.base_x - c.x);
            if (d < best) best = d, *out = c;
        }
        return best != 99999;
    }
    if (t == 0x16) return PortSiteSearch(s, p, out);
    Player& pl = s.players[p];
    AIScript& sc = *pl.script;
    const std::int32_t W = s.map.width, H = s.map.height;
    const std::int32_t bx = sc.base_x, by = sc.base_y, r = sc.field_660;
    std::int32_t x0 = bx - r, x1 = bx + r, y0 = by - 2 * r, y1 = by + 2 * r;
    auto clamp = [&](std::int32_t& x, std::int32_t& y) {
        if (x < 4) x = 4;
        if (y < 10) y = 10;
        if (x >= W - 3) x = W - 4;
        if (y >= H - 9) y = H - 10;
    };
    clamp(x0, y0);
    clamp(x1, y1);
    if (x0 < 10) x1 = x0 + 2 * r;
    if (y0 < 10) y1 = y0 + 2 * r;
    if (x1 >= W - 10) x0 = x1 - 2 * r;
    if (y1 >= H - 10) y0 = y1 - 2 * r;
    clamp(x0, y0);
    clamp(x1, y1);
    if (t == 0x15) {
        std::int32_t far = 0;
        bool hit = false;
        for (std::int32_t x = static_cast<std::uint16_t>(x0); x < x1; x = static_cast<std::uint16_t>(x + 3)) {
            for (std::int32_t y = static_cast<std::uint16_t>(y0); y < y1; y = static_cast<std::uint16_t>(y + 3)) {
                const std::int32_t d = std::abs(y - by) + std::abs(x - bx);
                if (far <= d && CanPlaceHouse(s, p, x, y, t, pl.nation)) far = d, *out = Cell{x, y}, hit = true;
            }
        }
        if (hit) return true;
        if (++sc.field_664 == 13) {
            MoveBase(s, p, true);
            sc.field_664 = 0;
        } else if (sc.field_664 % 3 == 0) {
            MoveBase(s, p, false);
        }
        return false;
    }
    std::int32_t best = 99999;
    bool found = false;
    for (std::int32_t x = static_cast<std::uint16_t>(x0); x < x1; ++x) {
        for (std::int32_t y = static_cast<std::uint16_t>(y0); y < y1; ++y) {
            const std::int32_t d = std::abs(x - bx) + std::abs(y - by);
            if (d < best && CanPlaceHouse(s, p, x, y, t, pl.nation)) {
                best = d, *out = Cell{x, y}, found = true;
            }
        }
    }
    if (found) return true;
    if (++sc.field_664 == 9) {
        MoveBase(s, p, true);
        sc.field_664 = 0;
    } else if (sc.field_664 % 3 == 0) {
        MoveBase(s, p, false);
    }
    return false;
}


void Tail(GameState& s, std::uint8_t p) {
    if (Rd(s) > 6000) return;
    const Player& pl = s.players[p];
    const auto nh = static_cast<std::uint16_t>(pl.houses.size());
    std::uint16_t start = 0;
    if (nh > 5) {
        start = static_cast<std::uint16_t>(static_cast<std::int32_t>(Rd(s)) % nh);
        if (start >= 5) start = static_cast<std::uint16_t>(start - 5);
    }
    std::vector<std::uint16_t> picks;
    for (std::uint16_t k = start; k < nh; ++k) {
        const House& h = pl.houses[k];
        if (h.is_destroyed == 0 && TowerGap(s, p, h)) {
            if (h.type == 0x18) {  // dead in practice: TowerGap rejects type 0x18
                TowerBuild(s, p, k);
                continue;
            }
            picks.push_back(k);
            if (picks.size() >= 5) break;
        }
    }
    if (!picks.empty()) TowerBuild(s, p, picks[Rd(s) % picks.size()]);
}

}  // namespace

bool AI_Build(GameState& s, std::uint8_t p, std::uint16_t t) { return Build(s, p, t); }


void AIScript_Run(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    if (!pl.script) {
        Unported(s, orig::kAiScriptRun);  // a player without a script
        return;
    }
    AIScript& sc = *pl.script;
    if (pl.ai_kind == 5) {
        const std::uint16_t n = Count16(s, p, 0x16);
        const std::int32_t r = static_cast<std::int32_t>(Rd(s)) % 10;
        const bool plus_sea = s.plus_rules && s.land_map == 0;  // Plus on a water map (docs/ai.md 10.17)
        if (plus_sea && n == 0 && !LandEnemy(s, p) && PortSiteFree(s, p)) {
            // No enemy to walk to and a free port site: the market helps, the power station comes first, and a
            // computer at Medium or Difficult saves for the port until tick 60,000 (a later port is no longer early).
            if (pl.control_type == 1 && pl.ai_level >= 2 && s.tick < 60000) s.ai_saving_port = p;
            BuildOrBuy(s, p, 0x16);
        } else if (plus_sea && AllByLand(s, p)) {
            // Every enemy can be walked to: one port only late and with money to spare (twice its cost in stock).
            if (n == 0 && s.tick >= 60000 && pl.wood >= 40000.0 && pl.iron >= 40000.0 && pl.gold >= 20000.0 &&
                pl.oil >= 40000.0) {
                Build(s, p, 0x16);
            }
        } else if ((r == 1 || n < 1) && n < 3) {
            Build(s, p, 0x16);
        }
    } else if (pl.ai_kind == 0) {
        return;
    }
    if (sc.step < sc.nsteps && sc.step < sc.main.size()) {
        Template(s, p, sc);
        StepDone(s, p, sc);
    }

    const std::uint8_t nation = pl.nation;
    for (std::uint16_t t = 0; t < NumUnitTypes(s, nation); ++t) {
        if (pl.unit_count[t] < sc.want_u[t] && Player_CanAfford(s, p, t)) {
            TrainOne(s, p, t, 10);
            break;
        }
    }
    bool built = false;
    if (Count16(s, p, 6) < sc.want_h[6] && CanBuild(s, p, 6) && Build(s, p, 6)) built = true;
    if (!built && Count16(s, p, 9) < sc.want_h[9] && CanBuild(s, p, 9) && Build(s, p, 9)) built = true;
    if (!built) {
        for (std::uint16_t t = 0; t < NumHouseTypes(s, nation); ++t) {
            if (Count16(s, p, t) < sc.want_h[t] && CanBuild(s, p, t) && Build(s, p, t)) break;
        }
    }
    const bool saving = SavingForPort(s, p);  // Plus: no research and no towers while saving for the port
    if (!saving) TryResearch(s, p);
    AIScript_Update(s, p);
    if (!saving) Tail(s, p);
    s.ai_saving_port = 0xff;
}





void AIScript_Start(GameState& s, std::uint8_t p) {
    AIScript& sc = *s.players[p].script;
    sc.want_u.fill(0);
    std::fill_n(sc.want_h.begin(), 25, std::uint16_t{0});
    if (sc.step == 0 && sc.nsteps > 0) {
        for (const ScriptEntry& e : sc.main[0]) {
            if (e.kind == 1) sc.want_u[e.type % 40] = e.count;
            if (e.kind == 2) sc.want_h[e.type % 30] = e.count;
        }
    }
    while (sc.step < sc.nsteps) {
        const std::uint8_t before = sc.step;
        StepDone(s, p, sc);
        if (sc.step == before) break;
    }
}


// (below 5000, for the local player's own house, the "house under attack" message, which is not sim state). If the





// u+6 untouched) and sent by AI_FormGroups(list, n, 0, 2, 3, 1, &door, 0, 0, 1, 0).
void AI_HouseAttacked(GameState& s, std::uint8_t p, House& h) {
    h.field_0a8 = 40;
    Rng_Next(s);
    Event_HouseAttacked(s, p, h);  // below 5000 for your house: the mark and "Building under enemy attack!"
    Player& pl = s.players[p];
    if (pl.field_9b2a98[2] == 0) return;
    if (s.plus_rules && pl.ai_kind != 0 && (pl.control_type == 1 || pl.field_9b2d9c != 0)) {


        PlusArmy_Defend(s, p, true, h.x, h.y);
        return;
    }
    const std::int32_t W = s.map.width, H = s.map.height;
    std::int32_t rx = h.map_x, ry = h.map_y + 30;
    if (rx < 4) rx = 4;
    if (ry < 10) ry = 10;
    if (rx >= W - 3) rx = W - 4;
    if (ry >= H - 9) ry = H - 10;
    const HouseFootprint fp = House_Footprint(s, p, h);
    const std::int32_t rows = fp.b + 30, cols = fp.a + 30;
    std::vector<Unit*> list;
    for (std::int32_t row = 0; row < rows && list.size() < 20; ++row) {
        std::int32_t x = rx, y = ry;
        for (std::int32_t col = 0; col < cols && list.size() < 20; ++col) {
            if (x > 3 && y > 9 && x < W - 3 && y < H - 9) {
                const auto k = static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x);
                const std::vector<UnitRef>& cell = s.map.cell_units[k];
                for (std::size_t i = 0; i < cell.size() && i < 40; ++i) {
                    if (cell[i].player != p) continue;
                    Unit* u = Unit_Find(s, cell[i]);
                    if (u == nullptr || !Idle(s, p, *u)) continue;
                    const UnitType& ut = Unit_Type(s, p, *u);
                    if (ut.field_009 == 0) continue;
                    const double dx = static_cast<double>(h.exit_x) - u->x, dy = static_cast<double>(h.exit_y) - u->y;
                    if (!(static_cast<double>(ut.range[p]) > std::sqrt(dx * dx + dy * dy))) continue;
                    list.insert(list.begin(), u);
                }
            }
            if ((y & 1) == 0) {
                y -= 1;
            } else {
                x += 1, y -= 1;
            }
        }
        if ((ry & 1) == 0) {
            rx -= 1, ry -= 1;
        } else {
            ry -= 1;
        }
    }
    if (list.empty()) return;
    std::int32_t x = Ftol(h.exit_x), y = Ftol(h.exit_y);
    AI_FormGroups(s, p, std::move(list), 0, 2, 3, 1, x, y, 0, 0, 1, false);
}



bool House_CanPlace(const GameState& s, std::uint8_t p, std::int32_t x, std::int32_t y, std::uint16_t type) {
    if (p >= s.players.size() || type >= 25) return false;
    return CanPlaceHouse(const_cast<GameState&>(s), p, x, y, type, s.players[p].nation);
}

bool House_CanBuild(const GameState& s, std::uint8_t p, std::uint16_t type) {
    if (p >= s.players.size()) return false;
    if (type == 0xfffd) return s.players[p].houses.size() <= 998;
    if (type == 0xfffe) return s.players[p].houses.size() <= 998 && s.players[p].wood >= 300.0;
    return type < 25 && CanBuild(s, p, type);
}




std::uint16_t AddHouseAtPoint(GameState& s, std::uint8_t p, float x, float y, std::uint16_t type,
                              std::span<const UnitRef> selected) {
    const float fx = static_cast<float>(static_cast<double>(x) + static_cast<double>(2.0f));
    const Cell c = PickCell(s.map, fx, y);
    const std::uint8_t nation = s.players[p].nation;
    if (!CanPlaceHouse(s, p, c.x, c.y, type, nation)) return 0xffff;
    House h = CreateHouse(s, c.x, c.y, type, nation);
    House_PlusNew(s, p, h);
    const std::uint16_t k = AddHouseToPlayer(s, p, std::move(h), selected);
    if (k != 0xffff && k > 900) Unported(s, orig::kAddHouseAtPoint);  // the original only logs
    return k;
}






std::uint16_t AddHouseToPointFull(GameState& s, std::uint8_t p, float x, float y, std::uint16_t type,
                                  std::span<const UnitRef> selection) {
    if (type > 65000) return 0xffff;           // the original logs
    if (!CanBuild(s, p, type)) return 0xffff;  // the original prints a message for the player
    const std::uint16_t k = AddHouseAtPoint(s, p, x, y, type, selection);
    if (k == 0xffff) return k;
    PayForHouse(s, p, k);
    const Unit* head = selection.empty() ? nullptr : Unit_Find(s, selection[0]);
    std::uint16_t spot = 0;
    for (const UnitRef& r : selection) {
        Unit* u = Unit_Find(s, r);
        if (u == nullptr || head == nullptr) {
            Unported(s, orig::kAddHouseToPoint);  // a selected unit the sim does not know
            continue;
        }
        if (u->work == 9 || Unit_Type(s, p, *u).move_class != 6) continue;
        Worker_SendToHouse(s, p, *u, k, spot, head);
        ++spot;
    }
    return k;
}

}  // namespace antanta::sim
