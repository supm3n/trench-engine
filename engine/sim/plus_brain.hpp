// Entente Engine Plus, stage 4 (docs/plus-balance.md 6): the smart research and upgrade rule of the computer players
// and of your economic advisor when it makes upgrades. Plus only; Classic keeps the original's dice (TryResearch) and
// the houses' blind upgrades. Deterministic: on a fixed schedule from the tick, from saved state only (stocks, houses
// and the workers in them, units alive and queued, research flags) and the enemy units within the sight of the
// player's own units and houses at that moment; no random number, ties go to the lower item.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "state.hpp"

namespace antanta::sim {

// What a player's own units and houses see of its enemies now: enemy units by class, an enemy gas shell in flight,
// and the food-equivalent of the player's own infantry, cavalry and houses within the range of a seen enemy gun
// (MoveClass 10). Coarse on purpose: the sight boxes (sight columns, twice as many rows) and the gun ranges are
// rounded out to blocks of 8 x 16 cells.
struct PlusSight {
    std::int32_t infantry = 0, guns = 0, cavalry = 0, tanks = 0, aircraft = 0;
    bool gas = false;
    double infantry_hit = 0, cavalry_hit = 0, houses_hit = 0;
    std::int32_t bw = 0;              // the blocks per row of `enemy`
    std::vector<std::uint8_t> enemy;  // 1 in each block of 8 x 16 cells with a seen enemy land unit
};
PlusSight Plus_Sight(const GameState& s, std::uint8_t p);

// One pick of the rule. kind Sciences / Military / Building: an academy row (house = the idle academy's slot, or 0xffff
// when the player has no academy of that type yet and should build one; Building is the Building Academy of F7, its row
// the player's next level); Upgrade: the house slot and its level as the row.
struct PlusPick {
    enum class Kind : std::uint8_t { None, Sciences, Military, Upgrade, Building };
    Kind kind = Kind::None;
    std::uint16_t row = 0;
    std::uint16_t house = 0;
    double score = 0;        // 1 or more passes: the level's pay back limit / pay back, or value / (1.5 x cost)
    double payback = 0;      // an economy item's pay back in game minutes (0 for the others)
    std::int32_t units = 0;  // an army item's own units (alive and queued) it improves
};

// The rule's level: a computer's ai_level (1 Easy, 2 Medium, 3 Difficult), your advisor 2 (the Medium rule).
std::uint8_t Plus_Level(const Player& pl);
// The best research or mine, gold mine or oil rig upgrade now, Kind::None when none passes. Changes nothing.
PlusPick Plus_Choose(const GameState& s, std::uint8_t p, const PlusSight& sight);
// The reserve rule: after paying cost (Food, Wood, Iron, Gold, Oil) every stock keeps at least half of what it had
// and at least the cost of the next five units the player wants (soldiers with a barracks, else the dearest queued
// type).
bool Plus_Budget(const GameState& s, std::uint8_t p, const std::int32_t* cost);
// "Do not train what you cannot feed" (docs/plus-balance.md 6, step 4): true under Plus for a mortar man (5), a
// howitzer or cannon (0x15, 0x16) or the fixed howitzer (0x17) while the gold stock is below ten game minutes
// (20,000 ticks) of the gold upkeep of the player's mortar men and guns alive. The computer's and advisor's orders
// test it; your own orders never do.
bool AI_CannotFeed(const GameState& s, std::uint8_t p, std::uint16_t t);
// Every 500 ticks for a computer, or a player whose economic advisor is on with "Makes upgrades", at
// (tick + 50 p) % 500 == 0 (sim/tick.cpp): power when short, room when the cap is full (a new dwelling when a site is
// free, else one dwelling upgrade), up to four threatened towers and pillboxes (a target, damage, or an enemy land
// unit seen within about 1,500), then up to three picks of Plus_Choose, then one quiet tower or pillbox (the lowest
// level) when every stock it takes holds 20 times its price.
// The sight (Plus_Sight) is taken for Medium and Difficult computers and for advisors (the Building Academy reads it
// from Medium on, the other counter items only on Difficult).
void Plus_Brain(GameState& s, std::uint8_t p);

}  // namespace antanta::sim
