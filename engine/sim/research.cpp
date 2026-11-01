




#include <algorithm>
#include <array>

#include "movement.hpp"
#include "original_ids.hpp"
#include "strike.hpp"

namespace antanta::sim {


const std::array<std::array<std::int32_t, 6>, 20> kSciencesCost = {
    {{10000, 0, 0, 2000, 0, 0},           {8000, 0, 0, 1000, 0, 0},
     {15000, 0, 0, 5500, 0, 0},           {20000, 25000, 30000, 40000, 0, 0},
     {30000, 35000, 40000, 50000, 0, 0},  {20000, 25000, 30000, 40000, 0, 0},
     {150000, 0, 0, 25000, 0, 0},         {200000, 0, 0, 100000, 0, 0},
     {10000, 0, 0, 10000, 20000, 0},      {30000, 0, 0, 30000, 60000, 0},
     {100000, 20000, 20000, 40000, 0, 0}, {10000, 0, 0, 10000, 20000, 0},
     {100000, 0, 0, 30000, 20000, 0},     {10000, 0, 1000, 5000, 5000, 0},
     {50000, 0, 0, 10000, 5000, 0},       {25000, 0, 0, 15000, 0, 0},
     {250000, 0, 50000, 100000, 0, 0},    {10000, 0, 0, 5000, 0, 0},
     {55000, 0, 0, 25000, 0, 0},          {0, 0, 0, 0, 0, 0}}};

const std::array<std::uint16_t, 20> kSciencesTime = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000,
                                                     1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000};

namespace {


constexpr std::array<std::int32_t, 6> kMilitaryCost[26] = {{10000, 0, 0, 10000, 0, 0},
                                                           {100000, 10000, 30000, 20000, 10000, 0},
                                                           {10000, 0, 0, 10000, 0, 0},
                                                           {10000, 0, 0, 10000, 0, 0},
                                                           {20000, 10000, 30000, 20000, 10000, 0},
                                                           {20000, 15000, 30000, 20000, 10000, 0},
                                                           {20000, 15000, 30000, 20000, 10000, 0},
                                                           {20000, 15000, 30000, 20000, 10000, 0},
                                                           {20000, 15000, 30000, 20000, 10000, 0},
                                                           {75000, 15000, 30000, 20000, 10000, 0},
                                                           {10000, 0, 0, 2000, 0, 0},
                                                           {50000, 0, 0, 20000, 0, 0},
                                                           {25000, 0, 30000, 20000, 0, 0},
                                                           {25000, 0, 30000, 20000, 0, 0},
                                                           {30000, 0, 40000, 50000, 0, 0},
                                                           {65000, 0, 0, 10000, 0, 0},
                                                           {100000, 0, 0, 25000, 0, 0},
                                                           {25000, 0, 30000, 40000, 0, 0},
                                                           {25000, 0, 30000, 40000, 0, 0},
                                                           {125000, 0, 40000, 60000, 0, 0},
                                                           {200000, 0, 0, 100000, 0, 0},
                                                           {40000, 0, 10000, 10000, 0, 0},
                                                           {0, 0, 0, 40000, 0, 0},
                                                           {50000, 0, 30000, 50000, 0, 0},
                                                           {200000, 0, 50000, 100000, 0, 0},
                                                           {100000, 0, 30000, 50000, 0, 0}};


constexpr std::uint16_t kMilitaryTime[26] = {1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000,
                                             1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000, 1000,
                                             1000, 2000, 1000, 2000, 2000, 2000, 2000, 2000};

// The float constants of ApplyUpgrade (the exact binary floats of the exe).
constexpr float k1_1 = 1.1f;
constexpr float k1_15 = 1.15f;
constexpr float k1_2 = 1.2f;
constexpr float k1_25 = 1.25f;
constexpr float k1_3 = 1.3f;
constexpr float k1_5 = 1.5f;
constexpr float k0_1 = 0.1f;
constexpr float k0_15 = 0.15f;
constexpr float k0_2 = 0.2f;
constexpr float k0_25 = 0.25f;
constexpr float k0_3 = 0.3f;
constexpr float k0_5 = 0.5f;
constexpr float k0_8 = 0.8f;
constexpr float kM0_2 = -0.2f;
constexpr float kM0_25 = -0.25f;
constexpr float kM0_3 = -0.3f;

// "R = x": fild of the u16, fmul of the f32, ftol, the low 16 bits stored.
void SetTo(std::uint16_t& v, float k) {
    v = static_cast<std::uint16_t>(Ftol(static_cast<double>(v) * static_cast<double>(k)));
}

// "R -= x": R - ftol(R * x) in 16 bits (a negative x raises R).
void SubBy(std::uint16_t& v, float k) {
    v = static_cast<std::uint16_t>(v -
                                   static_cast<std::uint16_t>(Ftol(static_cast<double>(v) * static_cast<double>(k))));
}

// fld dword, fmul dword, fstp dword.
void Scale(float& f, float k) { f = static_cast<float>(static_cast<double>(f) * static_cast<double>(k)); }



void RescalePortTimers(GameState& s, std::uint8_t p) {
    for (House& h : s.players[p].houses) {
        if (h.type != 0x16 || !h.has_field_0b0) continue;
        const Unit* u = Unit_Find(s, h.field_0b0);
        if (u == nullptr) {
            Unported(s, orig::kRescalePortTimers);  // a ship the port cannot find
            continue;
        }
        const double bt = Unit_Type(s, h.field_0b0.player, *u).build_time[p];
        const double two = bt + bt;
        if (two >= 1.0) h.field_044 = static_cast<float>(static_cast<double>(h.field_044) / two * bt);
    }
}

// Entente Engine Plus, the research list of docs/plus-balance.md 5 (stage 2): the rows whose price differs from the
// original's (Food, Wood, Iron, Gold, Oil, unused). Military Academy: the merges k0 (with k1), k3 (with k8), k9 (with
// k11) and k15 (with k16); the repriced k2, k4, k5, k6; the new k26 Steel helmets and k27 Deep drilling; stage 5's
// items in three of the freed rows: k1 Light machine gun, k8 Conscription, k11 Cavalry shock tactics, and (2026-10-23,
// docs/decisions.md 34) the fourth, k16, Fire direction staff, which unlocks the call-in strikes (sim/strike.hpp).
// Academy of Sciences: r3 to r5 (building durability), r16 (tank reload), the new r19 Steam sawmills, and (2026-10-23,
// the owner's 6-player game, plus-balance.md 3) r8, r9 and r11 with a tenth of their oil, the rest paid in gold (9 gold
// for 10 oil), and k27 Deep drilling moved to the Academy of Sciences as its row 27 (kPlusDeepDrilling).
struct PlusRow {
    std::uint16_t type, row;
    std::array<std::int32_t, 6> cost;
};
constexpr PlusRow kPlusCost[] = {{5, 0, {15000, 0, 0, 2000, 0, 0}},
                                 {5, 2, {8000, 0, 0, 2000, 0, 0}},
                                 {5, 3, {10000, 0, 0, 2000, 0, 0}},
                                 {5, 4, {25000, 0, 2000, 1000, 0, 0}},
                                 {5, 5, {40000, 0, 4000, 2000, 0, 0}},
                                 {5, 6, {60000, 0, 5000, 3000, 0, 0}},
                                 {5, 9, {20000, 0, 0, 5000, 0, 0}},
                                 {5, 15, {30000, 0, 0, 3000, 0, 0}},
                                 {5, 26, {20000, 0, 10000, 0, 0, 0}},
                                 {5, 27, {20000, 0, 0, 10000, 0, 0}},
                                 {2, 3, {10000, 10000, 10000, 10000, 0, 0}},
                                 {2, 4, {20000, 20000, 20000, 20000, 0, 0}},
                                 {2, 5, {30000, 30000, 30000, 30000, 0, 0}},
                                 {2, 16, {50000, 0, 15000, 20000, 0, 0}},
                                 {2, 19, {15000, 0, 0, 5000, 0, 0}},
                                 {2, 8, {10000, 0, 0, 19000, 2000, 0}},
                                 {2, 9, {30000, 0, 0, 57000, 6000, 0}},
                                 {2, 11, {10000, 0, 0, 19000, 2000, 0}},
                                 {2, kPlusDeepDrilling, {20000, 0, 0, 10000, 0, 0}},
                                 {5, 1, {50000, 0, 20000, 0, 0, 0}},
                                 {5, 8, {30000, 0, 0, 0, 0, 0}},
                                 {5, 11, {30000, 0, 0, 0, 0, 0}},
                                 {5, kStrikeUnlockRow, {0, 0, 0, 100000, 0, 0}}};
// The Military Academy row moved to the Academy of Sciences under Plus (k27 Deep drilling, 2026-10-23): no longer
// offered there; its price stays, so a research an old save runs there is refunded and finishes as before.
bool PlusMoved(std::uint16_t type, std::uint16_t row) { return type == 5 && row == kPlusDeepDrilling; }

}  // namespace

const std::int32_t* Research_CostRow(std::uint16_t type, std::uint16_t row) {
    if (type == 2 && row < kSciencesCost.size()) return kSciencesCost[row].data();
    if (type == 5 && row < 26) return kMilitaryCost[row].data();
    return nullptr;
}

std::uint16_t Research_Time(std::uint16_t type, std::uint16_t row) {
    if (type == 2 && row < kSciencesTime.size()) return kSciencesTime[row];
    if (type == 5 && row < 26) return kMilitaryTime[row];
    return 0;
}

const std::int32_t* Research_CostRow(const GameState& s, std::uint16_t type, std::uint16_t row) {
    if (!s.plus_rules) return Research_CostRow(type, row);
    for (const PlusRow& r : kPlusCost) {
        if (r.type == type && r.row == row) return r.cost.data();
    }
    return Research_CostRow(type, row);
}

std::uint16_t Research_Time(const GameState& s, std::uint16_t type, std::uint16_t row) {
    if (s.plus_rules && PlusMoved(type, row)) return 0;
    if (s.plus_rules && type == 5 && row == 26) return 1000;                // the new rows, as their neighbours
    if (s.plus_rules && type == 5 && row == kStrikeUnlockRow) return 3000;  // Fire direction staff, a late item
    if (s.plus_rules && type == 2 && row == kPlusDeepDrilling) return 1000;
    return Research_Time(type, row);
}

bool Research_Price(const GameState& s, std::uint8_t p, std::uint16_t type, std::uint16_t row,
                    std::array<std::int32_t, 6>& out) {
    const std::int32_t* c = Research_CostRow(s, type, row);
    if (c == nullptr) return false;
    for (std::size_t i = 0; i < 6; ++i) out[i] = c[i];
    if (s.plus_rules && type == 2 && row == 17 && p < s.players.size()) {
        const int n = std::min<int>(s.players[p].plus_delousing, 10);  // F6: twice the price per use, at most x1024
        for (std::size_t i = 0; i < 5; ++i) out[i] = out[i] << n;
    }
    return true;
}

std::uint8_t* Research_FlagByte(Player& pl, House::ResearchFlag f) {
    switch (f.kind) {
        case 1: return f.index < pl.research_9b39f0.size() ? &pl.research_9b39f0[f.index] : nullptr;
        case 2: return f.index < 12 ? &pl.research_flags[f.index] : nullptr;
        case 3: return f.index < pl.research_9b3860.size() ? &pl.research_9b3860[f.index] : nullptr;
        default: return nullptr;
    }
}





void Research_Start(GameState& s, std::uint8_t p, House& h, std::uint16_t time, std::uint16_t id) {
    Player& pl = s.players[p];
    h.field_05c = time;
    h.field_064 = static_cast<std::uint8_t>(id);
    h.field_06a = 1;
    h.field_060 = static_cast<float>(time);
    if (h.type == 2) {
        const int r = id - 3;
        if (r < 0 || (r >= 20 && !(s.plus_rules && r == kPlusDeepDrilling))) {
            Unported(s, orig::kResearchStartSciences);  // an id past the player's row
            return;
        }
        if (s.plus_rules && r == kPlusDeepDrilling) {  // Entente Engine Plus: k27 in its Military Academy byte
            pl.research_9b3860[kPlusDeepDrilling] = 1;
            h.field_06c = {3, static_cast<std::uint8_t>(r)};
            return;
        }
        pl.research_9b39f0[static_cast<std::size_t>(r)] = 1;
        h.field_06c = {1, static_cast<std::uint8_t>(r)};
        if (r <= 11) {
            h.field_06c = {2, static_cast<std::uint8_t>(r)};
            pl.research_flags[static_cast<std::size_t>(r)] = 1;
        }
    } else if (h.type == 5) {
        const int k = (id & 0xff) - 3;
        if (k < 0 || k >= 30) {
            Unported(s, orig::kResearchStartMilitary);
            return;
        }
        pl.research_9b3860[static_cast<std::size_t>(k)] = 1;
        h.field_06c = {3, static_cast<std::uint8_t>(k)};
    }
}





void Research_Button(GameState& s, std::uint8_t p, House& h, std::uint16_t time, std::uint16_t id) {
    if (h.field_06a != 0) return;
    const int row = id - 3;
    if (s.plus_rules && row >= 0 && PlusMoved(h.type, static_cast<std::uint16_t>(row))) return;  // not offered
    // Plus F7: the Building Academy's researches in order, one at a time for the player (its level is the player's)
    if (s.plus_rules && h.type == 3 && (row != s.players[p].plus_build_level || House_BuildAcademyBusy(s.players[p])))
        return;
    const std::int32_t* c = nullptr;
    std::array<std::int32_t, 6> price{};
    if (h.type == 2 || h.type == 5) {
        // Plus: its own prices (docs/plus-balance.md 5), Delousing doubling with each use (F6)
        if (row >= 0 && Research_Price(s, p, h.type, static_cast<std::uint16_t>(row), price)) c = price.data();
    } else if (h.type == 14) {
        c = House_UpgradeCostRow(14, 0);
    } else if (row >= 0) {
        c = House_UpgradeCostRow(s, h.type, static_cast<std::uint16_t>(row));
    }
    if (c == nullptr) {
        Unported(s, orig::kResearchButton);  // a window whose table is not known for this house type or row
        return;
    }
    Player& pl = s.players[p];
    constexpr double Player::* kOrder[5] = {&Player::food, &Player::gold, &Player::wood, &Player::iron, &Player::oil};
    constexpr int kCol[5] = {0, 3, 1, 2, 4};
    for (int i = 0; i < 5; ++i) {
        double& v = pl.*kOrder[i];
        v = v - static_cast<double>(c[kCol[i]]);
        pl.*StatOf(kOrder[i]) = pl.*StatOf(kOrder[i]) - static_cast<double>(c[kCol[i]]);
        if (v < 0.0) v = 0.0;
    }
    Research_Start(s, p, h, time, id);
}






void Research_Cancel(GameState& s, std::uint8_t p, House& h, const std::array<std::int32_t, 5>& cost) {
    if (h.field_05c == 0) return;
    const double ratio = static_cast<double>(h.field_060) / static_cast<double>(h.field_05c);
    h.field_05c = 0;
    h.field_06a = 0;
    Player& pl = s.players[p];
    constexpr double Player::* kOrder[5] = {&Player::food, &Player::gold, &Player::wood, &Player::iron, &Player::oil};
    constexpr int kCol[5] = {0, 3, 1, 2, 4};
    for (int i = 0; i < 5; ++i) {
        const double add = static_cast<double>(cost[static_cast<std::size_t>(kCol[i])]) * ratio;
        double& v = pl.*kOrder[i];
        v = v + add;
        pl.*StatOf(kOrder[i]) = pl.*StatOf(kOrder[i]) + add;
        if (v < 0.0) v = 0.0;
    }
    if (h.type == 2) {
        const int r = h.field_064 - 3;
        if (s.plus_rules && r == kPlusDeepDrilling) {  // Entente Engine Plus: k27's Military Academy byte
            pl.research_9b3860[kPlusDeepDrilling] = 0;
            return;
        }
        if (r >= 0 && r < 20) pl.research_9b39f0[static_cast<std::size_t>(r)] = 0;
        if (r >= 0 && r <= 11) pl.research_flags[static_cast<std::size_t>(r)] = 0;
        if (r < 0 || r >= 20) Unported(s, orig::kResearchClearStarted);  // a byte outside the player's row
    } else if (h.type == 5) {
        const int k = h.field_064 - 3;
        if (k >= 0 && k < 30) {
            pl.research_9b3860[static_cast<std::size_t>(k)] = 0;
        } else {
            Unported(s, orig::kResearchClearStarted);
        }
    }
}





void ApplyUpgrade(GameState& s, std::uint8_t p, House& h, std::uint16_t id) {
    (void)h;
    Player& pl = s.players[p];
    const int k = id - 1;
    if (k < 0 || k >= 30) {
        Unported(s, orig::kApplyUpgrade);  // bytes outside the player's rows
        return;
    }
    pl.research_9b3860[static_cast<std::size_t>(k)] = 1;
    pl.research_9b3770[static_cast<std::size_t>(k)] = 1;
    auto T = [&s, &pl](std::uint16_t t) -> UnitType& { return s.unit_types[pl.nation % 6][t]; };
    constexpr std::uint16_t kInfantry[6] = {3, 2, 14, 25, 26, 27};
    if (s.plus_rules) {  // Entente Engine Plus, the merged and new items (docs/plus-balance.md 5)
        switch (id) {
            case 0x1:  // k0 Marksmanship (k0 + k1): infantry accuracy x1.25 and infantry range +10 %
                Scale(pl.accuracy_factor, k1_25);
                for (std::uint16_t t : kInfantry) SetTo(T(t).range[p], k1_1);
                return;
            case 0x4: SetTo(T(3).accuracy[p], k1_3), SubBy(T(3).reload[p], k0_2); return;    // k3 Officer school (+ k8)
            case 0xa: Scale(pl.factor_9b3338, k1_5), Scale(pl.factor_9b3338, k1_2); return;  // k9 Gas masks (+ k11)
            case 0x10:  // k15 Remounts (k15 + k16): cavalryman and special horseman HP x1.35, speed x1.1 (as B13)
                for (std::uint16_t t : {std::uint16_t{4}, std::uint16_t{16}}) {
                    UnitType& u = T(t);
                    SetTo(u.max_hp[p], 1.35f);
                    Scale(u.speed[p], k1_1);
                    const float factor = u.field_468 != 0 ? 3.0f : 1.15f;
                    u.waypoint_radius[p] =
                        static_cast<float>(static_cast<double>(u.speed[p]) * static_cast<double>(factor));
                }
                return;
            case 0x2: {  // k1 Light machine gun (stage 5): machine gunner reload and build time -30 %, speed +15 %
                UnitType& u = T(6);
                SubBy(u.reload[p], k0_3), SubBy(u.build_time[p], k0_3);
                Scale(u.speed[p], k1_15);
                const float factor = u.field_468 != 0 ? 3.0f : 1.15f;
                u.waypoint_radius[p] =
                    static_cast<float>(static_cast<double>(u.speed[p]) * static_cast<double>(factor));
                return;
            }
            case 0x9:  // k8 Conscription (stage 5): soldier and officer build time -25 %
                SubBy(T(2).build_time[p], k0_25), SubBy(T(3).build_time[p], k0_25);
                return;
            case 0xc:  // k11 Cavalry shock tactics (stage 5): the cavalry's shell defence 5 -> 25, as Steel helmets
                for (std::uint16_t t : {std::uint16_t{4}, std::uint16_t{16}}) {
                    std::uint16_t& d = T(t).defense[p][2];
                    if (d < 25) d = 25;
                }
                return;
            case 0x14:  // k19, A6 (stage 5): cannon and howitzer accuracy +30 % as before, range +15 % instead of +30 %
                SubBy(T(22).accuracy[p], kM0_3), SubBy(T(22).range[p], -0.15f);
                SubBy(T(21).accuracy[p], kM0_3), SubBy(T(21).range[p], -0.15f);
                return;
            case 0x1b:  // k26 Steel helmets: the shell defence (Defense[p][2], read by UnitHit) of the infantry 5 -> 25
                for (std::uint16_t t : {std::uint16_t{2}, std::uint16_t{3}, std::uint16_t{6}, std::uint16_t{14},
                                        std::uint16_t{25}, std::uint16_t{26}, std::uint16_t{27}}) {
                    std::uint16_t& d = T(t).defense[p][2];
                    if (d < 25) d = 25;
                }
                return;
            // k27 Deep drilling: oil x1.35 (x1.5 until the top end retune of 2026-10-23 late)
            case 0x1c: Scale(pl.factor_644, static_cast<float>(kPlusMiningResearch)); return;
            case 0x11: return;  // k16 Fire direction staff: its done byte unlocks the call-in strikes (sim/strike.cpp)
            default: break;
        }
    }
    switch (id) {
        case 0x1: Scale(pl.accuracy_factor, k1_1); return;
        case 0x2: Scale(pl.accuracy_factor, k1_15); return;
        case 0x3: Scale(pl.factor_9b33b8, k1_2); return;
        case 0x4: SetTo(T(3).accuracy[p], k1_3); return;
        case 0x5:
            for (std::uint16_t t : kInfantry) SubBy(T(t).reload[p], k0_15);
            return;
        case 0x6:
            for (std::uint16_t t : kInfantry) SubBy(T(t).reload[p], k0_25);
            return;
        case 0x7:
            for (std::uint16_t t : kInfantry) SetTo(T(t).attack[p][1], k1_15);
            return;
        case 0x8:
            SubBy(T(7).reload[p], k0_25), SubBy(T(7).accuracy[p], kM0_25);
            SubBy(T(8).reload[p], k0_25), SubBy(T(8).accuracy[p], kM0_25);
            return;
        case 0x9: SubBy(T(3).reload[p], k0_2); return;
        case 0xa: Scale(pl.factor_9b3338, k1_5); return;
        case 0xb: Scale(pl.factor_9b3318, k1_2); return;
        case 0xc: Scale(pl.factor_9b3338, k1_2); return;
        case 0xd: SetTo(T(5).range[p], k1_2); return;
        case 0xe: SetTo(T(5).reload[p], k0_8); return;
        case 0xf: Scale(pl.shell_spread, k1_5); return;
        case 0x10:
            SetTo(T(4).max_hp[p], k1_1);
            if (s.original_rules) {


                for (float& v : T(16).speed) Scale(v, k1_1);
                T(16).max_hp[p] = static_cast<std::uint16_t>(Ftol(static_cast<double>(T(4).max_hp[p]) * k1_1));
                for (float& v : T(4).speed) Scale(v, k1_1);
                return;
            }

            SetTo(T(16).max_hp[p], k1_1);
            for (std::uint16_t t : {std::uint16_t{4}, std::uint16_t{16}}) {
                UnitType& u = T(t);
                Scale(u.speed[p], k1_1);
                const float factor = u.field_468 != 0 ? 3.0f : 1.15f;
                u.waypoint_radius[p] =
                    static_cast<float>(static_cast<double>(u.speed[p]) * static_cast<double>(factor));
            }
            return;
        case 0x11:
            SetTo(T(4).max_hp[p], k1_25);
            if (s.original_rules) {  // the same slip as 0x10
                T(16).max_hp[p] = static_cast<std::uint16_t>(Ftol(static_cast<double>(T(4).max_hp[p]) * k1_25));
            } else {
                SetTo(T(16).max_hp[p], k1_25);
            }
            return;
        case 0x12: SubBy(T(21).reload[p], k0_2), SubBy(T(23).reload[p], k0_2); return;
        case 0x13: SubBy(T(22).reload[p], k0_2); return;
        case 0x14:
            SubBy(T(22).accuracy[p], kM0_3), SubBy(T(22).range[p], kM0_3);
            SubBy(T(21).accuracy[p], kM0_3), SubBy(T(21).range[p], kM0_3);
            return;
        case 0x15:
            if (Player_IsYou(s, p)) s.fog_553ae0 = 0;  // a network game: any human's (docs/multiplayer.md)
            return;
        case 0x16: SubBy(T(11).accuracy[p], kM0_2), SubBy(T(12).accuracy[p], kM0_2); return;
        case 0x17: {  // Entente Engine Plus, F1 (B300): the one tank build time item, -30 % (0x1a does the ships)
            const float tank = s.plus_rules ? k0_3 : k0_1;
            SubBy(T(11).build_time[p], tank), SubBy(T(12).build_time[p], tank);
            return;
        }
        case 0x18: Scale(pl.factor_9b32d8, k1_2); return;
        case 0x19:
            for (std::uint16_t t : {std::uint16_t{17}, std::uint16_t{18}, std::uint16_t{19}, std::uint16_t{20}}) {
                SubBy(T(t).build_time[p], k0_5);
            }
            RescalePortTimers(s, p);
            return;
        case 0x1a:
            if (s.plus_rules) {  // Entente Engine Plus, F1 (B300): the ships, as its text says (-30 % build time)
                for (std::uint16_t t : {std::uint16_t{17}, std::uint16_t{18}, std::uint16_t{19}, std::uint16_t{20}}) {
                    SubBy(T(t).build_time[p], k0_3);
                }
                return;
            }
            SubBy(T(11).build_time[p], k0_3), SubBy(T(12).build_time[p], k0_3);  // the text says ships
            return;
        default: return;  // "Unknown upgrade in acadwar" (a debug message)
    }
}



void Research_HealAll(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        Unit& u = pl.unit_slots[k];
        const UnitType& t = Unit_Type(s, p, u);
        if (t.field_00c != 0) u.hp = static_cast<float>(t.max_hp[p]);
    }
}




void Research_Vaccinate(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    for (UnitType& t : s.unit_types[pl.nation % 6]) {
        if (!t.present) continue;
        if (t.field_00c != 0) SetTo(t.max_hp[p], k1_1);
    }
    constexpr float kK = 0.9090908765792847f;
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        Unit& u = pl.unit_slots[k];
        const UnitType& t = Unit_Type(s, p, u);
        if (t.field_00c == 0) continue;
        const double m = t.max_hp[p];
        const double v = m - (static_cast<double>(kK) * m - static_cast<double>(u.hp));
        const float vf = static_cast<float>(v);
        u.hp = vf;
        if (v < 1.0) {
            u.hp = 1.0f;
        } else if (static_cast<double>(vf) > m) {
            u.hp = static_cast<float>(m);
        }
    }
}

}  // namespace antanta::sim
