// The market (docs/interface.md C.3): see market.hpp.
#include "market.hpp"

#include <algorithm>
#include <bit>

namespace antanta::sim {

namespace {


constexpr std::uint32_t kRateBits[6][6] = {
    {0x3f800000, 0x3eaaaaab, 0x3de38e39, 0x3d430c31, 0x3de38e39, 0x3c257eb5},
    {0x40400000, 0x3f800000, 0x3eaaaaab, 0x3e124925, 0x3eaaaaab, 0x3cf83e10},
    {0x40f00000, 0x40200000, 0x3f800000, 0x3ecccccd, 0x3f666666, 0x3d99999a},
    {0x41700000, 0x40a00000, 0x40000000, 0x3f800000, 0x40000000, 0x3e19999a},
    {0x40f00000, 0x40200000, 0x3f666666, 0x3ecccccd, 0x3f800000, 0x3d99999a},
    {0x42b40000, 0x41f00000, 0x41400000, 0x40c00000, 0x41400000, 0x3f800000},
};

// The stock and statistic of resource r (Food, Wood, Iron, Gold, Oil).
double& Stock(Player& pl, int r) {
    switch (r) {
        case 0: return pl.food;
        case 1: return pl.wood;
        case 2: return pl.iron;
        case 3: return pl.gold;
        default: return pl.oil;
    }
}
double& Stat(Player& pl, int r) {
    switch (r) {
        case 0: return pl.food_stat;
        case 1: return pl.wood_stat;
        case 2: return pl.iron_stat;
        case 3: return pl.gold_stat;
        default: return pl.oil_stat;
    }
}

// The fsubr/fadd into a double stock and its statistic, then the clamp of the stock (test ah,5 / jp: below 0 only).
void Take(Player& pl, int r, float f) {
    double& stock = Stock(pl, r);
    stock = stock - static_cast<double>(f);
    Stat(pl, r) = Stat(pl, r) - static_cast<double>(f);
    if (stock < 0.0) stock = 0.0;
}
void Give(Player& pl, int r, float f) {
    double& stock = Stock(pl, r);
    stock = static_cast<double>(f) + stock;
    Stat(pl, r) = static_cast<double>(f) + Stat(pl, r);
    if (stock < 0.0) stock = 0.0;
}
void Count(float& stat, float f) { stat = static_cast<float>(static_cast<double>(f) + static_cast<double>(stat)); }

// The order of every market function: gold, food, iron, wood, oil.
constexpr int kOrder[5] = {3, 0, 2, 1, 4};

// act_c26e0's body without its message; amounts indexed Food, Wood, Iron, Gold, Oil.
void Trade(Player& pl, const std::array<float, 5>& sell, std::uint8_t buy, float received) {
    for (int r : kOrder) {
        Take(pl, r, sell[r]);
        Count(pl.res_sold[r], sell[r]);
    }
    if (buy < pl.res_bought.size()) Count(pl.res_bought[buy], received);
    if (buy <= 4) Give(pl, buy, received);
}



bool SaleValid(const GameState& s, std::uint8_t p, const MarketAmounts& sell) {
    for (float v : sell) {
        if (!(v >= 0.0f)) return false;  // below 0 or not a number
    }
    return Market_CanTrade(s, p, sell);
}

}  // namespace

float Market_Rate(std::uint8_t sold, std::uint8_t bought) {
    if (sold >= 6 || bought >= 6) return 0;
    return std::bit_cast<float>(kRateBits[sold][bought]);
}

float Market_Receive(const MarketAmounts& sell, std::uint8_t buy) {
    if (buy >= 6) return 0;
    auto term = [&](int r) {
        return static_cast<double>(Market_Rate(static_cast<std::uint8_t>(r), buy)) * static_cast<double>(sell[r]);
    };
    return static_cast<float>(((((term(5) + term(4)) + term(1)) + term(2)) + term(3)) + term(0));
}

bool Market_CanTrade(const GameState& s, std::uint8_t p, const MarketAmounts& sell) {
    if (p >= s.players.size()) return false;
    const Player& pl = s.players[p];
    if (pl.power_made < 0.0f || pl.power_made < sell[5]) return false;  // test ah,5 / jnp: below only
    const double stock[5] = {pl.food, pl.wood, pl.iron, pl.gold, pl.oil};
    for (int r : kOrder) {
        if (static_cast<double>(sell[r]) > stock[r]) return false;  // test ah,0x41 / je: above only
    }
    return true;
}

void Market_Exchange(GameState& s, std::uint8_t p, float gold, float wood, float food, float oil, float iron,
                     std::uint8_t buy, float received) {
    if (p >= s.players.size()) return;
    if (!s.original_rules) {  // B298: a sale the stock does not cover changes nothing; no more than the rate gives
        const MarketAmounts sell = {food, wood, iron, gold, oil, 0.0f};
        if (buy >= 6 || !(received >= 0.0f) || !SaleValid(s, p, sell)) return;
        received = std::min(received, Market_Receive(sell, buy));
    }
    if (s.plus_rules)
        received = std::min(received, Market_SellPlus(s.players[p], {food, wood, iron, gold, oil, 0.0f}, buy));
    Trade(s.players[p], {food, wood, iron, gold, oil}, buy, received);
}

void Market_Send(GameState& s, std::uint8_t from, std::uint8_t to, float gold, float wood, float food, float oil,
                 float iron) {
    if (from >= s.players.size() || to >= s.players.size()) return;
    if (!s.original_rules && !SaleValid(s, from, {food, wood, iron, gold, oil, 0.0f})) return;  // B298
    const float amount[5] = {food, wood, iron, gold, oil};
    for (int r : kOrder) {
        Take(s.players[from], r, amount[r]);
        Give(s.players[to], r, amount[r]);
        Count(s.players[from].res_sold[r], amount[r]);
        Count(s.players[to].res_bought[r], amount[r]);
    }
}

void Market_AutoTrade(GameState& s, std::uint8_t p) {
    if (p >= s.players.size()) return;
    Player& pl = s.players[p];
    if (pl.market_auto == 0) return;
    // fcomp, test ah,0x41 / jnp: off when power_made <= 0; then each amount against its stock, off when above.
    bool off = pl.power_made <= 0.0f;
    const double stock[5] = {pl.food, pl.wood, pl.iron, pl.gold, pl.oil};
    for (int r : kOrder) off = off || static_cast<double>(pl.market_sell[r]) > stock[r];
    if (off) {
        pl.market_auto = 0;
        return;
    }
    float received = pl.market_received;
    if (s.plus_rules) {  // E2: at the indexes of now, which the sale moves
        const MarketAmounts sell = {pl.market_sell[0], pl.market_sell[1], pl.market_sell[2],
                                    pl.market_sell[3], pl.market_sell[4], 0.0f};
        received = std::min(received, Market_SellPlus(pl, sell, pl.market_buy));
    }
    Trade(pl, pl.market_sell, pl.market_buy, received);
}

void Market_SetAuto(GameState& s, const Command& c) {
    const std::uint8_t p = c.args.empty() ? 0xff : static_cast<std::uint8_t>(c.args[0].raw & 0xff);
    if (p >= s.players.size() || c.args.size() < 2) return;
    Player& pl = s.players[p];
    if (c.args[1].raw == 0 || c.args.size() != 9) {
        pl.market_auto = 0;
        return;
    }
    auto f = [&c](std::size_t i) { return std::bit_cast<float>(c.args[i].raw); };
    pl.market_sell = {f(4), f(3), f(6), f(2), f(5)};  // args gold, wood, food, oil, iron
    pl.market_buy = static_cast<std::uint8_t>(c.args[7].raw);
    pl.market_received = f(8);
    if (!s.original_rules) {  // B298: amounts the panel cannot set leave the switch off; no more than the rate gives
        const MarketAmounts sell = {pl.market_sell[0], pl.market_sell[1], pl.market_sell[2],
                                    pl.market_sell[3], pl.market_sell[4], 0.0f};
        bool valid = pl.market_buy < 6 && pl.market_received >= 0.0f;
        for (float v : sell) valid = valid && v >= 0.0f;
        if (!valid) {
            pl.market_auto = 0;
            return;
        }
        pl.market_received = std::min(pl.market_received, Market_Receive(sell, pl.market_buy));
    }
    pl.market_auto = 1;
}

namespace {

// K: Food, Wood, Iron, Gold, Oil. Food 50,000 (drafted 1,000,000), tuned after stage 4 (docs/plus-balance.md 8)
constexpr double kPlusDepth[5] = {50000.0, 300000.0, 150000.0, 75000.0, 150000.0};
constexpr double kPlusFloor = 0.1;

}  // namespace

float Market_IndexAfter(const Player& pl, int r, double x) {
    const double v = static_cast<double>(pl.market_index[static_cast<std::size_t>(r)]) * (1.0 - x / kPlusDepth[r]);
    return static_cast<float>(std::max(v, kPlusFloor));
}

float Market_ReceivePlus(const Player& pl, const MarketAmounts& sell, std::uint8_t buy) {
    if (buy >= 6) return 0;
    auto term = [&](int r) {
        double t = static_cast<double>(Market_Rate(static_cast<std::uint8_t>(r), buy)) * static_cast<double>(sell[r]);
        if (r < 5) {
            const double before = pl.market_index[static_cast<std::size_t>(r)];
            t *= (before + static_cast<double>(Market_IndexAfter(pl, r, static_cast<double>(sell[r])))) * 0.5;
        }
        return t;
    };
    return static_cast<float>(((((term(5) + term(4)) + term(1)) + term(2)) + term(3)) + term(0));
}

float Market_SellPlus(Player& pl, const MarketAmounts& sell, std::uint8_t buy) {
    const float received = Market_ReceivePlus(pl, sell, buy);
    for (int r = 0; r < 5; ++r) {
        if (sell[r] > 0.0f) pl.market_index[static_cast<std::size_t>(r)] = Market_IndexAfter(pl, r, sell[r]);
    }
    return received;
}

void Market_RecoverPlus(Player& pl) {
    for (float& v : pl.market_index) v = static_cast<float>(static_cast<double>(v) + (1.0 - v) * 0.1);
}

}  // namespace antanta::sim
