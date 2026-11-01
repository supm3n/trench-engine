// Entente Engine Plus, the call-in strikes (sim/strike.hpp, docs/decisions.md 34 (3)). No address: ours. The shells are
// the game's own (Strike_Shell, sim/shell.cpp): they fly, hit, gas and burn as every other shell, so a strike in flight
// is nothing but shells with a delay (saved and hashed with the shell list). Doubles are summed in a fixed order
// (players, unit slots), so every peer and every replay gets the same picks.
#include "strike.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "dmath.hpp"
#include "movement.hpp"
#include "plus_brain.hpp"

namespace antanta::sim {

namespace {

// The strikes' numbers, first guesses (docs/decisions.md 34, docs/plus-balance.md 4 "Call-in strikes"): kind, shells,
// damage (0: the kind's), span (ticks from the first impact to the last; recon: how long), radius (px; the creeping
// barrage's band is twice its 125; recon: cells), the longest line, gas, two points, price (Food, Wood, Iron, Gold,
// Oil), cooldown (ticks). The bombing run drops one bomb every 3 ticks (span 72 / 24 bombs).
constexpr StrikeInfo kStrikes[kStrikeCount] = {
    {3, 40, 800, 330, 300, 0, false, false, {0, 0, 30000, 75000, 15000}, 6000},
    {4, 60, 0, 600, 125, 1200, false, true, {0, 0, 50000, 125000, 25000}, 8000},
    {3, 4, 0, 60, 300, 0, true, false, {0, 0, 0, 60000, 30000}, 10000},
    {7, 24, 1500, 72, 50, 1500, false, true, {0, 0, 0, 100000, 40000}, 8000},
    {0, 0, 0, 2000, 12, 0, false, false, {0, 0, 0, 15000, 5000}, 2000},
};

// The guns fire from 400 to 600 px behind the impact, toward the caller's town centre; the bombs fall from 300 px up,
// released 80 px before the point at 24 px a tick (a bomber's speed x 3, as Bomb_Fire's).
constexpr double kGunNear = 400.0, kGunSpread = 201.0;
constexpr double kBombHeight = 300.0, kBombLead = 80.0;
constexpr float kBombSpeed = 24.0f;
// The brain: a strike is worth calling when the block's score reaches this share of its food-equivalent price. A
// quarter, not the whole price: in a 5-player Difficult game (headless, 60,000 ticks, 2026-10-23) the best block
// never scored above 1.1 million, while the strikes cost 2.25 to 4.9 million (gold counts 15, iron and oil 7.5).
constexpr double kStrikeWorth = 0.25;
// The market's selling price of each resource in food (as plus_brain.cpp), times the player's price index (E2).
constexpr double kPrice[5] = {1.0, 3.0, 7.5, 15.0, 7.5};

double Weight(const Player& pl, int r) { return kPrice[r] * static_cast<double>(pl.market_index[r]); }

bool Standing(const House& h) { return h.is_destroyed == 0 && h.field_020 == 0; }

// The caller's standing town centre (type 6) nearest the point, ties to the lower slot; -1 when none stands.
std::int32_t TownCentre(const GameState& s, std::uint8_t p, double x, double y) {
    const Player& pl = s.players[p];
    std::int32_t best = -1;
    double best_d = 0;
    for (std::size_t k = 0; k < pl.houses.size(); ++k) {
        const House& h = pl.houses[k];
        if (h.type != 6 || !Standing(h)) continue;
        const double dx = static_cast<double>(h.x) - x, dy = static_cast<double>(h.y) - y;
        const double d = dx * dx + dy * dy;
        if (best < 0 || d < best_d) best = static_cast<std::int32_t>(k), best_d = d;
    }
    return best;
}


void Clamp(const GameState& s, double& x, double& y) {
    const double mx = static_cast<double>(static_cast<std::uint32_t>(s.map.width) * 96u);
    const double my = static_cast<double>(static_cast<std::uint32_t>(s.map.height) * 24u);
    x = std::clamp(x, 11.0, mx - 11.0), y = std::clamp(y, 11.0, my - 11.0);
}

}  // namespace

const StrikeInfo& Strike_Info(std::uint8_t strike) { return kStrikes[strike < kStrikeCount ? strike : 0]; }

StrikeRefusal Strike_Check(const GameState& s, std::uint8_t p, std::uint8_t strike) {
    if (!s.plus_rules || p >= s.players.size() || strike >= kStrikeCount) return StrikeRefusal::NotPlus;
    const Player& pl = s.players[p];
    if (pl.research_9b3770[kStrikeUnlockRow] == 0) return StrikeRefusal::Locked;
    if (strike == kStrikeGas && pl.research_9b3950[12] == 0) return StrikeRefusal::NoGas;  // r12, the gas research
    if (pl.is_lost != 0 || TownCentre(s, p, 0.0, 0.0) < 0) return StrikeRefusal::NoTownCentre;
    if (s.tick < pl.plus_strike_ready[strike]) return StrikeRefusal::Cooling;
    const double* stock[5] = {&pl.food, &pl.wood, &pl.iron, &pl.gold, &pl.oil};
    for (int r = 0; r < 5; ++r) {
        if (*stock[r] < static_cast<double>(kStrikes[strike].cost[static_cast<std::size_t>(r)]))
            return StrikeRefusal::Short;
    }
    return StrikeRefusal::None;
}

std::uint32_t Strike_Wait(const GameState& s, std::uint8_t p, std::uint8_t strike) {
    if (p >= s.players.size() || strike >= kStrikeCount) return 0;
    const std::uint32_t ready = s.players[p].plus_strike_ready[strike];
    return ready > s.tick ? ready - s.tick : 0;
}

bool Strike_Call(GameState& s, std::uint8_t p, std::uint8_t strike, std::int32_t x, std::int32_t y, std::int32_t x2,
                 std::int32_t y2) {
    if (Strike_Check(s, p, strike) != StrikeRefusal::None) return false;
    const std::int32_t mx = s.map.width * 96, my = s.map.height * 24;
    if (x < 0 || y < 0 || x >= mx || y >= my) return false;
    const StrikeInfo& k = kStrikes[strike];
    Player& pl = s.players[p];

    constexpr double Player::* kStock[5] = {&Player::food, &Player::wood, &Player::iron, &Player::gold, &Player::oil};
    for (int r = 0; r < 5; ++r) {
        const auto c = static_cast<double>(k.cost[static_cast<std::size_t>(r)]);
        double& v = pl.*kStock[r];
        v = v - c;
        pl.*StatOf(kStock[r]) = pl.*StatOf(kStock[r]) - c;
        if (v < 0.0) v = 0.0;
    }
    pl.plus_strike_ready[strike] = s.tick + k.cooldown;
    if (strike == kStrikeRecon) {  // the fog lifts over the circle for the caller (sim/fog.cpp)
        const Cell c = WorldToCell(static_cast<float>(x), static_cast<float>(y));
        pl.plus_recon_x = c.x, pl.plus_recon_y = c.y, pl.plus_recon_end = s.tick + k.span;
        return true;
    }
    // A two-point strike's line, at most k.length long.
    double lx = 0, ly = 0, len = 0;
    if (k.two_points) {
        lx = std::clamp(x2, 0, mx - 1) - x, ly = std::clamp(y2, 0, my - 1) - y;
        len = std::sqrt(lx * lx + ly * ly);
        if (len > k.length) lx = lx * k.length / len, ly = ly * k.length / len, len = k.length;
    }
    const double rx = len >= 1.0 ? lx / len : 1.0, ry = len >= 1.0 ? ly / len : 0.0;  // the bombers' heading
    const House& tc = pl.houses[static_cast<std::size_t>(TownCentre(s, p, x, y))];
    for (std::uint8_t i = 0; i < k.shells; ++i) {


        const double t = k.two_points && k.shells > 1 ? static_cast<double>(i) / (k.shells - 1) : 0.0;
        const double cx = x + lx * t, cy = y + ly * t;
        const int r0 = Rng_Next(s) % 100, r1 = Rng_Next(s) % 360;
        const double rad = static_cast<double>(r0) * static_cast<double>(k.radius) * 0.01;
        const double ang = static_cast<double>(r1) * static_cast<double>(0.017453292f);
        double ix = dmath::cos(ang) * rad + cx, iy = dmath::sin(ang) * rad + cy;
        Clamp(s, ix, iy);
        const auto delay = static_cast<std::uint16_t>(static_cast<std::uint32_t>(i) * k.span / k.shells);
        double sx = 0, sy = 0, sz = 0;
        float speed = 0.0f;
        if (k.shell_kind == 7) {  // a bomb: from above, a little before the point on the run's heading
            sx = ix - rx * kBombLead, sy = iy - ry * kBombLead;
            sz = GroundHeightAt(s, static_cast<float>(ix), static_cast<float>(iy)) + kBombHeight;
            speed = kBombSpeed;
        } else {  // a gun behind the impact toward the town centre (away from it when the map's edge is in the way)
            const double dx = static_cast<double>(tc.x) - ix, dy = static_cast<double>(tc.y) - iy;
            const double dl = std::sqrt(dx * dx + dy * dy);
            const double ux = dl >= 1.0 ? dx / dl : 1.0, uy = dl >= 1.0 ? dy / dl : 0.0;
            const double d = kGunNear + static_cast<double>((37u * i) % static_cast<std::uint32_t>(kGunSpread));
            sx = ix + ux * d, sy = iy + uy * d;
            Clamp(s, sx, sy);
            if ((sx - ix) * (sx - ix) + (sy - iy) * (sy - iy) < 100.0 * 100.0) {
                sx = ix - ux * d, sy = iy - uy * d;
                Clamp(s, sx, sy);
            }
            sz = GroundHeightAt(s, static_cast<float>(sx), static_cast<float>(sy)) + 15.0;
        }
        Strike_Shell(s, p, k.shell_kind, static_cast<float>(sx), static_cast<float>(sy), static_cast<float>(sz),
                     static_cast<float>(ix), static_cast<float>(iy), k.damage, delay, k.gas, speed);
    }
    return true;
}

void Strike_Brain(GameState& s, std::uint8_t p) {
    if (!s.plus_rules || p >= s.players.size()) return;
    Player& pl = s.players[p];
    if (pl.is_lost != 0) return;
    if (pl.control_type == 1 && pl.ai_kind == 0) return;  // a computer without a script
    const std::uint8_t lvl = Plus_Level(pl);
    if (lvl < 2) return;  // Easy never
    const std::int32_t W = s.map.width, H = s.map.height;
    if (W <= 0 || H <= 0) return;
    // The blocks of Plus_Sight (8 x 16 cells): what the player's units and houses see, and what lies within 10 cells of
    // its own houses (twice as many rows: a row is half a cell high).
    const std::int32_t bw = (W + 7) / 8, bh = (H + 15) / 16;
    const auto nb = static_cast<std::size_t>(bw * bh);
    std::vector<std::uint8_t> seen(nb, 0), near(nb, 0);
    const auto mark = [&](std::vector<std::uint8_t>& m, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                          std::int32_t y1) {
        x0 = std::clamp(x0, 0, W - 1) / 8, x1 = std::clamp(x1, 0, W - 1) / 8;
        y0 = std::clamp(y0, 0, H - 1) / 16, y1 = std::clamp(y1, 0, H - 1) / 16;
        for (std::int32_t by = y0; by <= y1; ++by) {
            for (std::int32_t bx = x0; bx <= x1; ++bx) m[static_cast<std::size_t>(by * bw + bx)] = 1;
        }
    };
    const auto block = [&](std::int32_t x, std::int32_t y) -> std::int32_t {
        if (x < 0 || y < 0 || x >= W || y >= H) return -1;
        return (y / 16) * bw + x / 8;
    };
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        const Unit& u = pl.unit_slots[k];
        if (u.is_deleted != 0) continue;
        const std::int32_t sv = static_cast<std::uint8_t>(Unit_Type(s, p, u).field_588);
        if (sv == 0 || sv >= 40) continue;
        mark(seen, u.map_x - sv, u.map_y - 2 * sv, u.map_x + sv, u.map_y + 2 * sv);
    }
    for (const House& h : pl.houses) {
        if (h.is_destroyed != 0) continue;
        const std::int32_t sv = h.field_08c;
        if (sv != 0 && sv < 40) mark(seen, h.map_x - sv, h.map_y - 2 * sv, h.map_x + sv, h.map_y + 2 * sv);
        mark(near, h.map_x - 10, h.map_y - 20, h.map_x + 10, h.map_y + 20);
    }
    // Per block: the seen enemies' food-equivalent (at the player's prices) and their mean point; the own and allied
    // units' food-equivalent. Aircraft and units outside the cell lists (inside houses or transports) are not targets.
    std::vector<double> enemy(nb, 0.0), own(nb, 0.0), sx(nb, 0.0), sy(nb, 0.0);
    std::vector<std::int32_t> count(nb, 0);
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        const Player& e = s.players[q];
        const auto qp = static_cast<std::uint8_t>(q);
        if (e.is_lost != 0) continue;
        const bool foe = IsEnemy(s, p, qp);
        for (std::uint32_t k = e.first_unit; k != kNoSlot; k = e.unit_slots[k].next) {
            const Unit& u = e.unit_slots[k];
            if (u.is_deleted != 0 || !u.in_cell_list) continue;
            const std::int32_t b = block(u.map_x, u.map_y);
            if (b < 0) continue;
            const UnitType& t = Unit_Type(s, qp, u);
            double v = 0;
            for (int r = 0; r < 5; ++r) v += static_cast<double>(t.cost[static_cast<std::size_t>(r)]) * Weight(pl, r);
            const auto bi = static_cast<std::size_t>(b);
            if (!foe) {
                own[bi] += v;
            } else if (seen[bi] != 0 && t.is_air == 0) {
                enemy[bi] += v, sx[bi] += u.x, sy[bi] += u.y, ++count[bi];
            }
        }
    }
    std::int32_t best = -1;
    double best_score = 0;
    for (std::size_t b = 0; b < nb; ++b) {
        if (count[b] == 0 || (lvl < 3 && near[b] == 0)) continue;  // Medium and the advisor: near their houses only
        const double score = enemy[b] * (near[b] != 0 ? 2.0 : 1.0) - 3.0 * own[b];
        if (best < 0 || score > best_score) best = static_cast<std::int32_t>(b), best_score = score;
    }
    if (best < 0) return;
    const auto bi = static_cast<std::size_t>(best);
    const double tx = sx[bi] / count[bi], ty = sy[bi] / count[bi];
    const auto worth = [&](std::uint8_t strike) {
        double fe = 0;
        for (int r = 0; r < 5; ++r)
            fe += static_cast<double>(kStrikes[strike].cost[static_cast<std::size_t>(r)]) * Weight(pl, r);
        return best_score >= fe * kStrikeWorth;
    };
    if (pl.research_9b3770[kStrikeUnlockRow] == 0) {
        // Not unlocked: a crowd worth the cheapest strike (the gas attack) makes it research k16 in an idle Military
        // Academy, when the reserve rule allows.
        if (pl.research_9b3860[kStrikeUnlockRow] != 0 || !worth(kStrikeGas)) return;
        std::array<std::int32_t, 6> price{};
        if (!Research_Price(s, p, 5, kStrikeUnlockRow, price) || !Plus_Budget(s, p, price.data())) return;
        for (House& h : pl.houses) {
            if (h.type == 5 && Standing(h) && h.field_05c == 0 && h.field_06a == 0) {
                Research_Button(s, p, h, Research_Time(s, 5, kStrikeUnlockRow),
                                static_cast<std::uint16_t>(kStrikeUnlockRow + 3));
                return;
            }
        }
        return;
    }
    // Gas first (the cheapest), then the barrage, the bombing run and the creeping barrage; one strike a check.
    for (const std::uint8_t strike : {std::uint8_t{kStrikeGas}, std::uint8_t{kStrikeBarrage},
                                      std::uint8_t{kStrikeBombing}, std::uint8_t{kStrikeCreeping}}) {
        if (Strike_Check(s, p, strike) != StrikeRefusal::None || !worth(strike)) continue;
        if (!Plus_Budget(s, p, kStrikes[strike].cost.data())) continue;
        // A line through the crowd, away from the town centre (400 px each side).
        const House& tc = pl.houses[static_cast<std::size_t>(TownCentre(s, p, tx, ty))];
        const double dx = tx - static_cast<double>(tc.x), dy = ty - static_cast<double>(tc.y);
        const double dl = std::sqrt(dx * dx + dy * dy);
        const double ux = dl >= 1.0 ? dx / dl : 1.0, uy = dl >= 1.0 ? dy / dl : 0.0;
        const bool line = kStrikes[strike].two_points;
        const auto x0 = static_cast<std::int32_t>(line ? tx - ux * 400.0 : tx);
        const auto y0 = static_cast<std::int32_t>(line ? ty - uy * 400.0 : ty);
        const auto x1 = static_cast<std::int32_t>(line ? tx + ux * 400.0 : tx);
        const auto y1 = static_cast<std::int32_t>(line ? ty + uy * 400.0 : ty);
        const std::int32_t mx = W * 96 - 1, my = H * 24 - 1;
        if (Strike_Call(s, p, strike, std::clamp(x0, 0, mx), std::clamp(y0, 0, my), x1, y1)) return;
    }
}

}  // namespace antanta::sim
