



#pragma once

#include <array>
#include <cstdint>

#include "state.hpp"

namespace antanta::sim {

// The amounts of the market panel, in the resource order of the statistics: Food, Wood, Iron, Gold, Oil, Electricity.
using MarketAmounts = std::array<float, 6>;



float Market_Rate(std::uint8_t sold, std::uint8_t bought);



float Market_Receive(const MarketAmounts& sell, std::uint8_t buy);



bool Market_CanTrade(const GameState& s, std::uint8_t p, const MarketAmounts& sell);





void Market_Exchange(GameState& s, std::uint8_t p, float gold, float wood, float food, float oil, float iron,
                     std::uint8_t buy, float received);




void Market_Send(GameState& s, std::uint8_t from, std::uint8_t to, float gold, float wood, float food, float oil,
                 float iron);



void Market_AutoTrade(GameState& s, std::uint8_t p);

// The market_auto input (Command::Kind::MarketAuto): sets the switch and, when on, the amounts.
void Market_SetAuto(GameState& s, const Command& c);

// Entente Engine Plus, E2 (docs/plus-balance.md 3): the moving price. Each player has an index per resource it sells
// (Player::market_index, 1.0 at the start). A sale of x of resource r multiplies its index by (1 - x / K[r]), down
// to 0.1 (K: Food 1,000,000, Wood 300,000, Iron 150,000, Gold 75,000, Oil 150,000), and is paid at the rate times the
// mean of the index before and after the sale. Every 1,000 ticks each index moves a tenth of the way back to 1.0.
// Buying moves no index; gifts (act_c2d80) are not sales. Classic and original_rules have no index.
//
// The index a sale of x of r would end at (no change to the player).
float Market_IndexAfter(const Player& pl, int r, double x);
// The amount `sell` (Food, Wood, Iron, Gold, Oil, Electricity) would get of `buy` at the player's indexes now: the
// panel's sum with each term times its resource's mean index (electricity has none). No change to the player.
float Market_ReceivePlus(const Player& pl, const MarketAmounts& sell, std::uint8_t buy);
// The same, and the indexes of the sold resources move: what a Plus sale pays.
float Market_SellPlus(Player& pl, const MarketAmounts& sell, std::uint8_t buy);
// Every 1,000 ticks (TStrateg_Update, Plus only): each index a tenth of the way back to 1.0.
void Market_RecoverPlus(Player& pl);

}  // namespace antanta::sim
