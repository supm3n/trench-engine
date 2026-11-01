// Entente Engine Plus, stage 4: the smart research and upgrade rule (sim/plus_brain.hpp, docs/plus-balance.md 6).
// Every value is computed from the state at the moment of the check; doubles are summed in a fixed order (players,
// unit slots, houses), so every peer and every replay gets the same picks.
#include "plus_brain.hpp"

#include <algorithm>
#include <bit>
#include <vector>

#include "movement.hpp"
#include "plus_army.hpp"

namespace antanta::sim {

namespace {

constexpr double kMinute = 2000.0;  // ticks of one game minute (30 ms a tick)
// A farm worker's round trip (field, barn, field) in ticks, for the food research's pay back: each trip delivers
// 80 x the food factor (sim/worker.cpp, NumWork 11). Measured: about 0.2 food a tick per field worker at factor 1
// (Fran_2r_1, a Medium computer with about 90 field workers, ticks 10,400 to 17,900), so about 400 ticks.
constexpr double kFarmTrip = 400.0;
// The market's selling price of each resource in food (wood 3, iron 7.5, gold 15, oil 7.5), times the player's price
// index under E2.
constexpr double kPrice[5] = {1.0, 3.0, 7.5, 15.0, 7.5};

using Cost = std::array<double, 5>;  // Food, Wood, Iron, Gold, Oil

bool Built(const House& h) { return h.is_destroyed == 0 && h.field_020 == 0; }
bool Idle(const House& h) { return Built(h) && h.field_05c == 0 && h.field_06a == 0; }
bool Upgrading(const House& h) { return h.is_destroyed == 0 && (h.field_05c != 0 || h.field_06a != 0); }

double Weight(const Player& pl, int r) { return kPrice[r] * static_cast<double>(pl.market_index[r]); }

double Fe(const Player& pl, const std::int32_t* c) {
    double v = 0;
    for (int r = 0; r < 5; ++r) v += static_cast<double>(c[r]) * Weight(pl, r);
    return v;
}

double FeUnit(const GameState& s, const Player& pl, std::uint16_t t) {
    const UnitType& ut = s.unit_types[pl.nation % 6][t % 40];
    double v = 0;
    for (int r = 0; r < 5; ++r) v += static_cast<double>(ut.cost[static_cast<std::size_t>(r)]) * Weight(pl, r);
    return v;
}

double FeHouse(const GameState& s, std::uint8_t p, const House& h) {
    const Player& pl = s.players[p];
    const HouseType& ht = House_Type(s, p, h);
    double v = 0;
    for (int r = 0; r < 5; ++r) v += static_cast<double>(ht.cost[static_cast<std::size_t>(r)]) * Weight(pl, r);
    return v;
}

std::uint32_t Count(const Player& pl, std::uint16_t t) {
    std::uint32_t n = 0;
    for (const House& h : pl.houses) n += h.type == t && h.is_destroyed == 0 ? 1 : 0;
    return n;
}

bool NoneBuilding(const Player& pl, std::uint16_t t) {
    for (const House& h : pl.houses) {
        if (h.type == t && h.is_destroyed == 0 && h.field_020 != 0) return false;
    }
    return true;
}

// The infantry of the unit tables: soldier 2, officer 3, machine gunner 6, special soldier 14 and the heroes 25 to 27.
bool Infantry(const UnitType& t) {
    return t.is_air == 0 && (t.move_class == 2 || t.move_class == 7 || t.move_class == 16);
}


double MineRate(const GameState& s, const House& h, double factor, std::int32_t* workers_out) {
    std::int32_t workers = 0;
    for (std::uint16_t i = 0; i < h.units_inside && i < h.inside.size(); ++i) {
        const Unit* u = Unit_Find(s, h.inside[i]);
        if (u != nullptr && u->type == 0) ++workers;
    }
    if (workers_out != nullptr) *workers_out = workers;
    const std::int32_t others = h.units_inside - workers;
    return factor * static_cast<double>(h.field_070) * (static_cast<double>(others) * 0.5 + workers);
}

double IronFactor(const Player& pl) { return static_cast<double>(pl.factor_684); }
double GoldFactor(const Player& pl) { return static_cast<double>(pl.gold_factor) * pl.factor_684; }

// The upkeep and income figures one check needs.
struct Econ {
    std::array<double, 5> income{};  // per tick, by resource: farms, mines, sawmills, oil rigs
    std::array<double, 5> mined{};   // per tick of the houses a research raises: iron and gold mines (r10, r11)
    double farm = 0;                 // food per tick of the field workers
    int bottleneck = -1;             // the resource whose stock covers the fewest wanted units and the next house
    Cost reserve{};                  // five wanted units
};

// The next wanted unit type: soldiers for a player with a finished barracks, else the dearest queued type.
std::int32_t WantedUnit(const GameState& s, const Player& pl) {
    if (pl.houses_done[4] != 0) return 2;
    std::int32_t best = -1;
    double best_fe = 0;
    for (const House& h : pl.houses) {
        if (h.is_destroyed != 0) continue;
        for (std::uint16_t t : h.queue) {
            const double v = FeUnit(s, pl, t);
            if (v > best_fe) best_fe = v, best = t;
        }
    }
    return best;
}

Econ Economy(const GameState& s, std::uint8_t p) {
    const Player& pl = s.players[p];
    Econ e;
    std::int32_t farmers = 0;
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        const Unit& u = pl.unit_slots[k];
        if (u.is_deleted == 0 && u.type == 0 && (u.work == 10 || u.work == 11)) ++farmers;
    }
    e.farm = farmers * 80.0 * static_cast<double>(pl.factor_744) / kFarmTrip;
    e.income[0] = e.farm;
    for (const House& h : pl.houses) {
        if (!Built(h)) continue;
        if (h.type == 10) {
            const double v = MineRate(s, h, IronFactor(pl), nullptr);
            e.income[2] += v, e.mined[2] += v;
        } else if (h.type == 11) {
            const double v = MineRate(s, h, GoldFactor(pl), nullptr);
            e.income[3] += v, e.mined[3] += v;
        } else if (h.type == 12) {
            e.income[1] += MineRate(s, h, static_cast<double>(pl.factor_664), nullptr);
        } else if (h.type == 13) {
            e.income[4] += static_cast<double>(pl.factor_644) * h.field_070;
        }
    }
    const std::int32_t want = WantedUnit(s, pl);
    Cost unit{}, house{};
    if (want >= 0) {
        const UnitType& ut = s.unit_types[pl.nation % 6][static_cast<std::size_t>(want)];
        for (int r = 0; r < 5; ++r) unit[r] = static_cast<double>(ut.cost[static_cast<std::size_t>(r)]);
    }
    if (pl.script) {  // the script's next wanted house: the first type below its wanted count (as Fill)
        for (std::uint16_t t = 0; t < 25; ++t) {
            const HouseType& ht = s.house_types[pl.nation % 6][t];
            if (!ht.present || Count(pl, t) >= pl.script->want_h[t]) continue;
            for (int r = 0; r < 5; ++r) house[r] = static_cast<double>(ht.cost[static_cast<std::size_t>(r)]);
            break;
        }
    }
    const double* stock[5] = {&pl.food, &pl.wood, &pl.iron, &pl.gold, &pl.oil};
    double low = 0;
    for (int r = 0; r < 5; ++r) {
        e.reserve[r] = 5.0 * unit[r];
        const double need = 5.0 * unit[r] + house[r];
        if (!(need > 0.0)) continue;
        const double cover = *stock[r] / need;
        if (e.bottleneck < 0 || cover < low) e.bottleneck = r, low = cover;
    }
    return e;
}

bool BudgetOk(const Player& pl, const Econ& e, const std::int32_t* cost) {
    const double* stock[5] = {&pl.food, &pl.wood, &pl.iron, &pl.gold, &pl.oil};
    for (int r = 0; r < 5; ++r) {
        const double c = cost[r];
        if (!(c > 0.0)) continue;
        if (*stock[r] * 0.5 < c || *stock[r] - c < e.reserve[r]) return false;
    }
    return true;
}

// The extra income of an economy item in food-equivalent per game minute; the bottleneck resource counts double.
double PerMinute(const Player& pl, const Econ& e, const std::array<double, 5>& extra) {
    double v = 0;
    for (int r = 0; r < 5; ++r) v += extra[r] * Weight(pl, r) * (r == e.bottleneck ? 2.0 : 1.0);
    return v * kMinute;
}

// The army items: the unit types each improves and its gain (docs/plus-balance.md 6, from the economy draft's 1.6
// table: reload -15 % 0.18, -25 % 0.33, rifle attack x1.15 0.5, accuracy 0.1, HP +10 % 0.1). The infantry list is
// ApplyUpgrade's (3, 2, 14, 25, 26, 27).
struct ArmyRow {
    std::uint8_t academy, row;
    float gain;
    std::uint8_t n;
    std::array<std::uint8_t, 11> types;
};
constexpr ArmyRow kArmy[] = {
    {5, 0, 0.2f, 6, {2, 3, 14, 25, 26, 27}},       // Marksmanship: accuracy x1.25, range +10 %
    {5, 1, 0.45f, 1, {6}},                         // Light machine gun: reload and build time -30 %, speed +15 %
    {5, 3, 0.35f, 1, {3}},                         // Officer school: accuracy x1.3, reload -20 %
    {5, 4, 0.18f, 6, {2, 3, 14, 25, 26, 27}},      // infantry reload -15 %
    {5, 5, 0.33f, 6, {2, 3, 14, 25, 26, 27}},      // infantry reload -25 %
    {5, 6, 0.5f, 6, {2, 3, 14, 25, 26, 27}},       // rifle attack x1.15 (about +50 % damage against soldiers)
    {5, 7, 0.4f, 2, {7, 8}},                       // air scout and fighter: reload -25 %, accuracy +25 %
    {5, 8, 0.1f, 2, {2, 3}},                       // Conscription: soldier and officer build time -25 %
    {5, 11, 0.1f, 2, {4, 16}},                     // Cavalry shock tactics (counter: cavalry under enemy guns)
    {5, 12, 0.2f, 1, {5}},                         // mortar range x1.2
    {5, 13, 0.25f, 1, {5}},                        // mortar reload x0.8
    {5, 14, 0.15f, 4, {5, 21, 22, 23}},            // shelling accuracy x1.5
    {5, 15, 0.4f, 2, {4, 16}},                     // Remounts: cavalry HP x1.35, speed x1.1
    {5, 17, 0.25f, 2, {21, 23}},                   // howitzer reload -20 %
    {5, 18, 0.25f, 1, {22}},                       // cannon reload -20 %
    {5, 19, 0.25f, 2, {21, 22}},                   // cannon and howitzer accuracy +30 %, range +15 % (A6)
    {5, 21, 0.1f, 2, {11, 12}},                    // tank accuracy x1.2
    {5, 22, 0.1f, 2, {11, 12}},                    // tank build time -30 %
    {5, 23, 0.2f, 4, {17, 18, 19, 20}},            // ship durability x1.2
    {5, 24, 0.1f, 4, {17, 18, 19, 20}},            // ship build time -50 %
    {5, 25, 0.1f, 4, {17, 18, 19, 20}},            // ship build time -30 %
    {5, 26, 0.15f, 7, {2, 3, 6, 14, 25, 26, 27}},  // Steel helmets (counter: infantry under enemy guns)
    {2, 15, 0.33f, 2, {11, 12}},                   // tank damage taken x0.75
    {2, 16, 0.18f, 2, {11, 12}},                   // tank reload x0.85
    {2, 18, 0.1f, 11, {2, 3, 4, 5, 6, 14, 15, 16, 25, 26, 27}},  // Vaccination: HP x1.1
};

struct Scored {
    PlusPick pick;
    std::uint32_t order = 0;  // ties: the lower item first (Academy of Sciences rows, Military rows, houses by slot)
};

void Consider(Scored& best, const PlusPick& c, std::uint32_t order) {
    if (!(c.score >= 1.0)) return;
    if (best.pick.kind == PlusPick::Kind::None || c.score > best.pick.score ||
        (c.score == best.pick.score && order < best.order)) {
        best.pick = c, best.order = order;
    }
}

// The academy of the type that takes a research now: the first finished, standing one with no research running;
// 0xffff when none stands (the rule then builds one), 0xfffe when all are busy.
std::uint16_t Academy(const Player& pl, std::uint16_t type) {
    bool any = false;
    for (std::size_t k = 0; k < pl.houses.size(); ++k) {
        const House& h = pl.houses[k];
        if (h.type != type || h.is_destroyed != 0) continue;
        any = true;
        if (Idle(h)) return static_cast<std::uint16_t>(k);
    }
    return any ? 0xfffe : 0xffff;
}

// Not started and not done (both academies); the Academy of Sciences' chains and the winter rule.
bool Offered(const GameState& s, const Player& pl, std::uint16_t academy, std::uint16_t row) {
    if (academy == 2 && row == kPlusDeepDrilling) {  // Deep drilling: the Military Academy's k27 bytes
        return s.plus_rules && pl.research_9b3860[row] == 0 && pl.research_9b3770[row] == 0;
    }
    if (academy == 2) {
        if (row >= 20 || pl.research_9b39f0[row] != 0 || pl.research_9b3950[row] != 0) return false;
        if (s.climate == 3 && (row == 0 || row == 1 || row == 2 || row == 6 || row == 7)) return false;
        if (row == 2) return pl.research_flags[12] != 0;  // r0 done
        if (row == 4) return pl.research_flags[15] != 0;  // r3 done
        if (row == 7) return pl.research_flags[18] != 0;  // r6 done
        return true;
    }
    if (row >= 30 || pl.research_9b3860[row] != 0 || pl.research_9b3770[row] != 0) return false;
    return Research_Time(s, academy, row) != 0;
}

}  // namespace

std::uint8_t Plus_Level(const Player& pl) {
    if (pl.control_type != 1) return 2;
    return pl.ai_level < 1 ? 1 : pl.ai_level > 3 ? 3 : pl.ai_level;
}

bool Plus_Budget(const GameState& s, std::uint8_t p, const std::int32_t* cost) {
    return BudgetOk(s.players[p], Economy(s, p), cost);
}

bool AI_CannotFeed(const GameState& s, std::uint8_t p, std::uint16_t t) {
    if (!s.plus_rules || (t != 5 && t != 0x15 && t != 0x16 && t != 0x17)) return false;
    const Player& pl = s.players[p];
    double gold = 0;  // per tick
    for (std::uint16_t g : {std::uint16_t{5}, std::uint16_t{0x15}, std::uint16_t{0x16}, std::uint16_t{0x17}}) {
        gold += static_cast<double>(s.unit_types[pl.nation % 6][g].upkeep[3]) * pl.unit_count[g];
    }
    return pl.gold < gold * 20000.0;
}

PlusSight Plus_Sight(const GameState& s, std::uint8_t p) {
    PlusSight out;
    const std::int32_t W = s.map.width, H = s.map.height;
    if (W <= 0 || H <= 0 || p >= s.players.size()) return out;
    const std::int32_t bw = (W + 7) / 8, bh = (H + 15) / 16;
    std::vector<std::uint8_t> seen(static_cast<std::size_t>(bw * bh), 0), hit(seen.size(), 0);
    out.bw = bw, out.enemy.assign(seen.size(), 0);
    const auto mark = [&](std::vector<std::uint8_t>& m, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                          std::int32_t y1) {
        x0 = std::clamp(x0, 0, W - 1) / 8, x1 = std::clamp(x1, 0, W - 1) / 8;
        y0 = std::clamp(y0, 0, H - 1) / 16, y1 = std::clamp(y1, 0, H - 1) / 16;
        for (std::int32_t by = y0; by <= y1; ++by) {
            for (std::int32_t bx = x0; bx <= x1; ++bx) m[static_cast<std::size_t>(by * bw + bx)] = 1;
        }
    };
    const auto at = [&](const std::vector<std::uint8_t>& m, std::int32_t x, std::int32_t y) {
        if (x < 0 || y < 0 || x >= W || y >= H) return false;
        return m[static_cast<std::size_t>((y / 16) * bw + x / 8)] != 0;
    };
    const Player& pl = s.players[p];
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        const Unit& u = pl.unit_slots[k];
        if (u.is_deleted != 0) continue;
        const std::int32_t sv = static_cast<std::uint8_t>(Unit_Type(s, p, u).field_588);  // the fog's sight (cells)
        if (sv == 0 || sv >= 40) continue;
        mark(seen, u.map_x - sv, u.map_y - 2 * sv, u.map_x + sv, u.map_y + 2 * sv);
    }
    for (const House& h : pl.houses) {
        const std::int32_t sv = h.field_08c;
        if (h.is_destroyed != 0 || sv == 0 || sv >= 40) continue;
        mark(seen, h.map_x - sv, h.map_y - 2 * sv, h.map_x + sv, h.map_y + 2 * sv);
    }
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        const Player& e = s.players[q];
        const auto qp = static_cast<std::uint8_t>(q);
        if (qp == p || e.is_lost != 0 || !IsEnemy(s, p, qp)) continue;
        for (std::uint32_t k = e.first_unit; k != kNoSlot; k = e.unit_slots[k].next) {
            const Unit& u = e.unit_slots[k];
            if (u.is_deleted != 0 || !at(seen, u.map_x, u.map_y)) continue;
            const UnitType& t = Unit_Type(s, qp, u);
            if (t.is_air == 0) out.enemy[static_cast<std::size_t>((u.map_y / 16) * bw + u.map_x / 8)] = 1;
            if (t.is_air != 0) {
                ++out.aircraft;
            } else if (t.move_class == 10) {
                ++out.guns;
                const std::int32_t r = t.range[q];  // pixels: a cell is 96 wide and 24 high
                mark(hit, u.map_x - r / 96 - 1, u.map_y - r / 24 - 1, u.map_x + r / 96 + 1, u.map_y + r / 24 + 1);
            } else if (t.move_class == 4) {
                ++out.cavalry;
            } else if (t.move_class == 3 && (u.type == 11 || u.type == 12)) {
                ++out.tanks;
            } else if (Infantry(t)) {
                ++out.infantry;
            }
        }
    }
    for (const Shell& sh : s.shells) {
        if (sh.gas == 0 || sh.player >= s.players.size() || !IsEnemy(s, p, sh.player)) continue;
        if (at(seen, static_cast<std::int32_t>(sh.cx) / 96, static_cast<std::int32_t>(sh.cy) / 24)) out.gas = true;
    }
    if (out.guns == 0) return out;
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        const Unit& u = pl.unit_slots[k];
        if (u.is_deleted != 0 || !at(hit, u.map_x, u.map_y)) continue;
        const UnitType& t = Unit_Type(s, p, u);
        if (Infantry(t)) out.infantry_hit += FeUnit(s, pl, u.type);
        if (t.move_class == 4) out.cavalry_hit += FeUnit(s, pl, u.type);
    }
    for (const House& h : pl.houses) {
        if (h.is_destroyed == 0 && at(hit, h.map_x, h.map_y)) out.houses_hit += FeHouse(s, p, h);
    }
    return out;
}

PlusPick Plus_Choose(const GameState& s, std::uint8_t p, const PlusSight& sight) {
    Scored best;
    if (!s.plus_rules || p >= s.players.size()) return best.pick;
    const Player& pl = s.players[p];
    const std::uint8_t lvl = Plus_Level(pl);
    const double limit = lvl == 1 ? 3.0 : lvl == 2 ? 6.0 : 10.0;  // the pay back limit in minutes
    const Econ e = Economy(s, p);
    const std::uint16_t sciences = Academy(pl, 2), military = Academy(pl, 5);

    // An economy item: its pay back against the level's limit.
    const auto economy = [&](PlusPick::Kind kind, std::uint16_t row, std::uint16_t house, const std::int32_t* cost,
                             const std::array<double, 5>& extra, std::uint32_t order) {
        if (cost == nullptr || !BudgetOk(pl, e, cost)) return;
        const double gain = PerMinute(pl, e, extra);
        if (!(gain > 0.0)) return;
        PlusPick c;
        c.kind = kind, c.row = row, c.house = house;
        c.payback = Fe(pl, cost) / gain;
        c.score = c.payback > 0.0 ? limit / c.payback : 1e9;
        Consider(best, c, order);
    };
    // An army or counter item: its value against 1.5 times its cost.
    const auto army = [&](PlusPick::Kind kind, std::uint16_t row, std::uint16_t house, const std::int32_t* cost,
                          double value, std::int32_t units, std::uint32_t order) {
        if (cost == nullptr || !(value > 0.0) || !BudgetOk(pl, e, cost)) return;
        PlusPick c;
        c.kind = kind, c.row = row, c.house = house, c.units = units;
        const double fe = Fe(pl, cost);
        c.score = fe > 0.0 ? value / (1.5 * fe) : 1e9;
        Consider(best, c, order);
    };

    // The Academy of Sciences' economy rows: food (r0, r1, r2, r6, r7), mining (r10, r11), Steam sawmills (r19).
    if (sciences != 0xfffe) {
        struct Food {
            std::uint16_t row;
            double k;
        };
        constexpr Food kFood[5] = {{0, 1.35}, {1, 1.15}, {2, 1.5}, {6, 2.0}, {7, 3.0}};
        for (const auto& [row, k] : kFood) {
            if (!Offered(s, pl, 2, row)) continue;
            economy(PlusPick::Kind::Sciences, row, sciences, Research_CostRow(s, 2, row), {e.farm * (k - 1.0)}, row);
        }
        for (std::uint16_t row : {std::uint16_t{10}, std::uint16_t{11}}) {
            if (!Offered(s, pl, 2, row)) continue;
            economy(PlusPick::Kind::Sciences, row, sciences, Research_CostRow(s, 2, row),
                    {0, 0, e.mined[2] * (kPlusMiningResearch - 1.0), e.mined[3] * (kPlusMiningResearch - 1.0), 0}, row);
        }
        if (Offered(s, pl, 2, 19)) {
            economy(PlusPick::Kind::Sciences, 19, sciences, Research_CostRow(s, 2, 19),
                    {0, e.income[1] * (kPlusMiningResearch - 1.0)}, 19);
        }
        // Deep drilling (k27, oil x1.25), in the Academy of Sciences since 2026-10-23 (kPlusDeepDrilling)
        if (Offered(s, pl, 2, kPlusDeepDrilling)) {
            economy(PlusPick::Kind::Sciences, kPlusDeepDrilling, sciences, Research_CostRow(s, 2, kPlusDeepDrilling),
                    {0, 0, 0, 0, e.income[4] * (kPlusMiningResearch - 1.0)}, kPlusDeepDrilling);
        }
    }
    // Mine, gold mine, oil rig and sawmill upgrades: one per resource at a time; a mine or sawmill with fewer than 3
    // workers inside never; a sawmill (O4, 2026-10-23) only while wood is the bottleneck.
    bool busy[25] = {};
    for (const House& h : pl.houses) {
        if (h.type < 25 && Upgrading(h)) busy[h.type] = true;
    }
    for (std::size_t k = 0; k < pl.houses.size(); ++k) {
        const House& h = pl.houses[k];
        if ((h.type != 10 && h.type != 11 && h.type != 12 && h.type != 13) || busy[h.type] || !Idle(h) ||
            h.field_065 >= 5)
            continue;
        if (h.type == 12 && e.bottleneck != 1) continue;
        // the next level's output against today's (House_PlusNextOutput: x1.3, x1.6, then +50 % of the base)
        const double mult = static_cast<double>(House_PlusNextOutput(s, p, h, h.field_065)) / h.field_070;
        std::array<double, 5> extra{};
        if (h.type == 13) {
            extra[4] = static_cast<double>(pl.factor_644) * h.field_070 * (mult - 1.0);
        } else {
            std::int32_t workers = 0;
            const double factor = h.type == 10   ? IronFactor(pl)
                                  : h.type == 11 ? GoldFactor(pl)
                                                 : static_cast<double>(pl.factor_664);
            const double v = MineRate(s, h, factor, &workers);
            if (workers < 3) continue;
            extra[h.type == 10 ? 2 : h.type == 11 ? 3 : 1] = v * (mult - 1.0);
        }
        economy(PlusPick::Kind::Upgrade, h.field_065, static_cast<std::uint16_t>(k),
                House_UpgradeCostRow(s, h.type, h.field_065), extra, 1000 + static_cast<std::uint32_t>(k));
    }
    if (lvl < 2) return best.pick;  // Easy: economy items only

    // Army items: the cost of the own units the item improves (alive and queued) times its gain.
    std::array<std::int32_t, 40> units = {};
    for (std::size_t t = 0; t < 40; ++t) units[t] = pl.unit_count[t];
    for (const House& h : pl.houses) {
        if (h.is_destroyed != 0) continue;
        for (std::uint16_t t : h.queue) units[t % 40] += 1;
    }
    const bool counters = lvl >= 3;
    for (const ArmyRow& a : kArmy) {
        const std::uint16_t academy = a.academy, row = a.row;
        const std::uint16_t slot = academy == 2 ? sciences : military;
        if (slot == 0xfffe || !Offered(s, pl, academy, row)) continue;
        double value = 0;
        std::int32_t n = 0;
        for (std::uint8_t i = 0; i < a.n; ++i) {
            const std::uint8_t t = a.types[i];
            if (!s.unit_types[pl.nation % 6][t].present || units[t] <= 0) continue;
            value += units[t] * FeUnit(s, pl, t) * static_cast<double>(a.gain);
            n += units[t];
        }
        if (counters) {
            if (academy == 5 && row == 26) value += 0.4 * sight.infantry_hit;  // helmets against seen guns
            if (academy == 5 && row == 11) value += 0.4 * sight.cavalry_hit;   // cavalry shock, the same
            if (sight.tanks > 0 && ((academy == 5 && (row == 21 || row == 22)) || (academy == 2 && row != 18))) {
                value *= 1.5;  // own tank items while enemy tanks are in sight
            }
        }
        army(academy == 2 ? PlusPick::Kind::Sciences : PlusPick::Kind::Military, row, slot,
             Research_CostRow(s, academy, row), value, n, academy == 2 ? row : 100u + row);
    }
    // The Building Academy (F7, Medium and Difficult): its next research while seen enemy guns reach the player's
    // houses (the state does not keep what destroyed a house, so the houses within reach stand for those lost to
    // shells). Value: the houses within reach times 0.5 for the first two rows (the first, more room in pillboxes and
    // watch towers, counts as the way to the second, half the damage from shells) and 0.33 for the third (+50 %
    // health). No academy yet: build one once the Academy of Sciences (its prerequisite) stands.
    if (sight.houses_hit > 0.0 && pl.plus_build_level < 3 && !House_BuildAcademyBusy(pl)) {
        const std::uint16_t building = Academy(pl, 3);
        if (building != 0xfffe && (building != 0xffff || pl.houses_done[2] != 0)) {
            constexpr double kGain[3] = {0.5, 0.5, 0.33};
            const std::uint16_t row = pl.plus_build_level;
            army(PlusPick::Kind::Building, row, building, House_UpgradeCostRow(s, 3, row),
                 kGain[row] * sight.houses_hit, 0, 200u + row);
        }
    }
    if (!counters) return best.pick;

    // Counter items (Difficult): building durability against seen guns, anti-aircraft accuracy against seen aircraft,
    // gas masks against a seen gas shell.
    if (sciences != 0xfffe && sight.houses_hit > 0.0) {
        for (std::uint16_t row : {std::uint16_t{3}, std::uint16_t{4}, std::uint16_t{5}}) {
            if (!Offered(s, pl, 2, row)) continue;
            army(PlusPick::Kind::Sciences, row, sciences, Research_CostRow(s, 2, row), 0.25 * sight.houses_hit, 0, row);
        }
    }
    if (military != 0xfffe && sight.aircraft > 0 && Offered(s, pl, 5, 2)) {
        double v = units[13] * FeUnit(s, pl, 13);
        for (const House& h : pl.houses) {
            if (h.type == 0x15 && Built(h)) v += FeHouse(s, p, h);
        }
        army(PlusPick::Kind::Military, 2, military, Research_CostRow(s, 5, 2), 0.2 * v, units[13], 100 + 2);
    }
    if (military != 0xfffe && sight.gas && Offered(s, pl, 5, 9)) {
        double v = 0;
        std::int32_t n = 0;
        for (std::uint16_t t = 0; t < 40; ++t) {
            const UnitType& ut = s.unit_types[pl.nation % 6][t];
            if (ut.present && Infantry(ut) && units[t] > 0) v += units[t] * FeUnit(s, pl, t), n += units[t];
        }
        army(PlusPick::Kind::Military, 9, military, Research_CostRow(s, 5, 9), 0.5 * v, n, 100 + 9);
    }
    return best.pick;
}

namespace {

void SetStatus(GameState& s, std::uint8_t p, std::uint8_t kind, std::uint16_t item, std::int32_t value) {
    s.players[p].plus_status = Player::PlusStatus{kind, item, value, s.tick};
}

// Power when short (the power station's own test): the cheapest of r8, r9, one station upgrade and a new station per
// unit of power, within the reserve.
bool Power(GameState& s, std::uint8_t p, const Econ& e) {
    Player& pl = s.players[p];
    const std::int32_t made = Ftol(static_cast<double>(pl.power_6e4) * pl.power_made);
    const std::int32_t used = Ftol(static_cast<double>(pl.power_used) / pl.power_6c4);
    if (!(made < used)) return false;
    const double eff = static_cast<double>(pl.power_6e4) * pl.power_made;
    double best = 0;
    int what = 0;  // 1 r8, 2 r9, 3 a station upgrade, 4 a new station
    std::size_t slot = 0;
    const auto offer = [&](int w, double fe, double gain, std::size_t k) {
        if (!(gain > 0.0)) return;
        const double v = fe / gain;
        if (what == 0 || v < best) best = v, what = w, slot = k;
    };
    const std::uint16_t sciences = Academy(pl, 2);
    if (sciences < 0xfffe) {
        for (std::uint16_t row : {std::uint16_t{8}, std::uint16_t{9}}) {
            const std::int32_t* c = Research_CostRow(s, 2, row);
            if (!Offered(s, pl, 2, row) || c == nullptr || !BudgetOk(pl, e, c)) continue;
            offer(row == 8 ? 1 : 2, Fe(pl, c), eff * (row == 8 ? 0.15 : 0.2), sciences);
        }
    }
    bool upgrading = false;
    for (const House& h : pl.houses) upgrading |= h.type == 0 && Upgrading(h);
    for (std::size_t k = 0; k < pl.houses.size() && !upgrading; ++k) {
        const House& h = pl.houses[k];
        if (h.type != 0 || !Idle(h) || h.field_065 >= 5) continue;
        const std::int32_t* c = House_UpgradeCostRow(s, 0, h.field_065);
        if (c == nullptr || !BudgetOk(pl, e, c)) continue;
        const double gain = static_cast<double>(h.field_070) * (h.field_065 == 0 ? 0.15 : 0.2) * pl.power_6e4;
        offer(3, Fe(pl, c), gain, k);
    }
    const HouseType& station = s.house_types[pl.nation % 6][0];
    if (pl.script && pl.field_9b2b38 != 0 && NoneBuilding(pl, 0) && House_CanBuild(s, p, 0)) {
        std::array<std::int32_t, 5> c{};
        for (int r = 0; r < 5; ++r) c[r] = static_cast<std::int32_t>(station.cost[static_cast<std::size_t>(r)]);
        if (BudgetOk(pl, e, c.data())) {
            offer(4, Fe(pl, c.data()), static_cast<double>(std::bit_cast<float>(station.field_190)) * pl.power_6e4, 0);
        }
    }
    switch (what) {
        case 1:
        case 2: {
            const auto row = static_cast<std::uint16_t>(what == 1 ? 8 : 9);
            Research_Button(s, p, pl.houses[slot], Research_Time(s, 2, row), static_cast<std::uint16_t>(row + 3));
            SetStatus(s, p, 5, row, 0);
            return true;
        }
        case 3: {
            House& h = pl.houses[slot];
            Research_Button(s, p, h, House_UpgradeTime(0, h.field_065), static_cast<std::uint16_t>(h.field_065 + 3));
            SetStatus(s, p, 5, 0, 0);
            return true;
        }
        case 4: AI_Build(s, p, 0), SetStatus(s, p, 5, 0, 0); return true;
        default: return false;
    }
}

// A free site for the type in the script's box around the base (FindSite's box, without its miss count).
bool SiteFree(const GameState& s, std::uint8_t p, std::uint16_t t) {
    const Player& pl = s.players[p];
    if (!pl.script) return false;
    const AIScript& sc = *pl.script;
    const std::int32_t W = s.map.width, H = s.map.height;
    const std::int32_t bx = sc.base_x, by = sc.base_y, r = sc.field_660;
    std::int32_t x0 = bx - r, x1 = bx + r, y0 = by - 2 * r, y1 = by + 2 * r;
    const auto clamp = [&](std::int32_t& x, std::int32_t& y) {
        if (x < 4) x = 4;
        if (y < 10) y = 10;
        if (x >= W - 3) x = W - 4;
        if (y >= H - 9) y = H - 10;
    };
    clamp(x0, y0);
    clamp(x1, y1);
    for (std::int32_t x = x0; x < x1; ++x) {
        for (std::int32_t y = y0; y < y1; ++y) {
            if (House_CanPlace(s, p, x, y, t)) return true;
        }
    }
    return false;
}

// Room when the cap is full (E4): a new dwelling when a site is free, else one dwelling upgrade at a time.
bool Room(GameState& s, std::uint8_t p, const Econ& e) {
    Player& pl = s.players[p];
    if (pl.units_alive < static_cast<std::int32_t>(pl.unit_cap)) return false;
    for (const House& h : pl.houses) {
        if (h.type == 9 && Upgrading(h)) return false;
    }
    if (pl.script && pl.field_9b2b38 != 0) {
        if (!NoneBuilding(pl, 9)) return false;  // one on the way
        if (House_CanBuild(s, p, 9) && SiteFree(s, p, 9)) {
            AI_Build(s, p, 9);
            SetStatus(s, p, 6, 9, 0);
            return true;
        }
    }
    std::size_t pick = pl.houses.size();
    for (std::size_t k = 0; k < pl.houses.size(); ++k) {
        const House& h = pl.houses[k];
        if (h.type != 9 || !Idle(h) || h.field_065 >= 5) continue;
        if (pick == pl.houses.size() || h.field_065 < pl.houses[pick].field_065) pick = k;
    }
    if (pick == pl.houses.size()) return false;
    House& h = pl.houses[pick];
    const std::int32_t* c = House_UpgradeCostRow(s, 9, h.field_065);
    if (c == nullptr || !BudgetOk(pl, e, c)) return false;
    Research_Button(s, p, h, House_UpgradeTime(9, h.field_065), static_cast<std::uint16_t>(h.field_065 + 3));
    SetStatus(s, p, 6, 9, 0);
    return true;
}

// A seen enemy land unit within 2 blocks across and 4 down of the cell (about 1,500 world units each way).
bool EnemyNear(const PlusSight& sight, std::int32_t x, std::int32_t y) {
    if (sight.bw <= 0) return false;
    const auto bh = static_cast<std::int32_t>(sight.enemy.size()) / sight.bw;
    for (std::int32_t by = std::max(y / 16 - 4, 0); by <= std::min(y / 16 + 4, bh - 1); ++by) {
        for (std::int32_t bx = std::max(x / 8 - 2, 0); bx <= std::min(x / 8 + 2, sight.bw - 1); ++bx) {
            if (sight.enemy[static_cast<std::size_t>(by * sight.bw + bx)] != 0) return true;
        }
    }
    return false;
}





bool Towers(GameState& s, std::uint8_t p, const Econ& e, const PlusSight& sight, bool threatened) {
    Player& pl = s.players[p];
    int done = 0;
    std::size_t quiet = pl.houses.size();
    for (std::size_t k = 0; k < pl.houses.size() && done < 4; ++k) {
        House& h = pl.houses[k];
        if ((h.type != 1 && h.type != 7 && h.type != 0x15) || !Idle(h) || h.field_065 >= 4) continue;
        const std::int32_t* c = House_UpgradeCostRow(s, h.type, h.field_065);
        if (c == nullptr || !BudgetOk(pl, e, c)) continue;
        if (!threatened) {
            const double* stock[5] = {&pl.food, &pl.wood, &pl.iron, &pl.gold, &pl.oil};
            bool rich = true;
            for (int r = 0; r < 5; ++r) rich = rich && !(*stock[r] < 20.0 * c[r]);
            if (rich && (quiet == pl.houses.size() || h.field_065 < pl.houses[quiet].field_065)) quiet = k;
            continue;
        }
        if (h.field_084 == 0xff && !(h.hp < static_cast<float>(h.max_hp)) && !EnemyNear(sight, h.map_x, h.map_y)) {
            continue;
        }
        Research_Button(s, p, h, House_UpgradeTime(h.type, h.field_065), static_cast<std::uint16_t>(h.field_065 + 3));
        SetStatus(s, p, 7, h.type, 0);
        ++done;
    }
    if (quiet != pl.houses.size()) {
        House& h = pl.houses[quiet];
        Research_Button(s, p, h, House_UpgradeTime(h.type, h.field_065), static_cast<std::uint16_t>(h.field_065 + 3));
        SetStatus(s, p, 7, h.type, 0);
        ++done;
    }
    return done != 0;
}

}  // namespace

void Plus_Brain(GameState& s, std::uint8_t p) {
    if (!s.plus_rules || p >= s.players.size()) return;
    PlusArmy_Spend(s, p);  // food at the cap (sim/plus_army.hpp)
    Player& pl = s.players[p];
    if (pl.is_lost != 0 || pl.field_9b2b3b == 0) return;  // "Makes upgrades"
    if (pl.control_type == 1 && pl.ai_kind == 0) return;  // a computer without a script
    Power(s, p, Economy(s, p));
    Room(s, p, Economy(s, p));
    const PlusSight sight = Plus_Level(pl) >= 2 ? Plus_Sight(s, p) : PlusSight{};
    Towers(s, p, Economy(s, p), sight, true);
    for (int n = 0; n < 3; ++n) {
        const PlusPick c = Plus_Choose(s, p, sight);
        if (c.kind == PlusPick::Kind::None) break;
        const std::int32_t value = c.kind == PlusPick::Kind::Upgrade || c.payback > 0.0
                                       ? static_cast<std::int32_t>(c.payback + 0.999)
                                       : c.units;
        if (c.kind == PlusPick::Kind::Upgrade) {
            House& h = pl.houses[c.house];
            Research_Button(s, p, h, House_UpgradeTime(h.type, c.row), static_cast<std::uint16_t>(c.row + 3));
            SetStatus(s, p, 3, h.type, value);
            continue;
        }
        if (c.kind == PlusPick::Kind::Building) {  // F7
            if (c.house == 0xffff) {               // no Building Academy: build one, then wait for it
                if (pl.script && pl.field_9b2b38 != 0 && NoneBuilding(pl, 3)) {
                    AI_Build(s, p, 3);
                    SetStatus(s, p, 4, 3, 0);
                }
                break;
            }
            Research_Button(s, p, pl.houses[c.house], House_UpgradeTime(3, c.row),
                            static_cast<std::uint16_t>(c.row + 3));
            SetStatus(s, p, 8, c.row, 0);
            continue;
        }
        const std::uint16_t academy = c.kind == PlusPick::Kind::Sciences ? 2 : 5;
        if (c.house == 0xffff) {  // no academy of that type: build one (one at a time), then wait for it
            if (pl.script && pl.field_9b2b38 != 0 && NoneBuilding(pl, academy)) {
                AI_Build(s, p, academy);
                SetStatus(s, p, 4, academy, 0);
            }
            break;
        }
        Research_Button(s, p, pl.houses[c.house], Research_Time(s, academy, c.row),
                        static_cast<std::uint16_t>(c.row + 3));
        SetStatus(s, p, academy == 2 ? 1 : 2, c.row, value);
    }
    Towers(s, p, Economy(s, p), sight, false);  // a quiet one after the economy and army items
}

}  // namespace antanta::sim
