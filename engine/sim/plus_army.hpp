





#pragma once

#include <cstdint>

#include "state.hpp"

namespace antanta::sim {

// What a wave or a field group goes for: kind 3 an enemy unit (player, id) that attacks us or an ally, kind 2 an
// enemy house (player, slot), 0 nothing. (x, y): its world point.
struct PlusTarget {
    std::uint8_t kind = 0, player = 0xff;
    std::uint16_t id = 0xffff;
    std::int32_t x = 0, y = 0;
};

// The target by score: the strongest group of enemies attacking our or an ally's houses or units that our units do
// not answer yet (1.5 times its cost in food-equivalent); else the enemy house nearest to our town centre, a town
// centre counting at half its distance and a mine, oil well or producing house at two thirds.
PlusTarget PlusArmy_Target(const GameState& s, std::uint8_t p);

// The town centre's world point (the first standing town centre, else the script's base cell as a world point, the
// cell the original reads as one, docs/ai.md 2, else the first standing house); false when the player has none.
bool PlusArmy_Base(const GameState& s, std::uint8_t p, double& x, double& y);

// The attack wave (AI_Think's attack wake-up under Plus): the idle soldiers at home (land units that may join a group,
// on a free cell, within 1,500 of an own house) less a home guard of the larger of 150 and a fifth of them, those
// nearest the town centre. The rest gathers at the staging point (the base edge facing the enemy house the waves go
// for; they walk there in groups of 30 or more); a wave leaves once those within 3,000 of it reach the minimum (the
// larger of 60 and a tenth of the soldiers, at most 4 x 126; a quarter of an army under 240): nearest to the target
// first, at most 4 x 126, as groups of 30 or more with the guns (MoveClass 10) spread among them as the back rank
// (FrontRank). Below the minimum, from a quarter of it (30 or more) on, the gathered soldiers join the nearest field
// army instead. The original's wait (no wave while the crowd is at most barracks x 10 + 50, at most 10 times in a row)
// holds below 90 % of the unit cap; force skips it, not the minimum. True when a wave or a reinforcement left.
bool PlusArmy_Wave(GameState& s, std::uint8_t p, const PlusTarget& t, bool force);

// True from 90 % of the unit cap on (the wave no longer waits, the defend wake-ups send waves too).
bool PlusArmy_NearCap(const Player& pl);

// Every tick from AI_Think (a computer, or a player whose military advisor is on), not for the Passive template:
// every 100 ticks the defence per threat (when the player sends help), every 200 ticks the idle units in the field:
// a crowd of 30 or more (a quarter of the minimum wave if more) takes the next enemy house within 3,000 when the
// player attacks; smaller crowds join the nearest field army within 4,000, else they walk back to the staging point,
// as does a crowd with no house near (a threat's answer once its threat is gone). The players are spread by 7 ticks.
void PlusArmy_Tick(GameState& s, std::uint8_t p);

// The defence per threat: each group of enemy units attacking our or an ally's houses or units, or marching within
// 2,000 of such a house, is one threat; each gets defenders worth 1.5 times its cost less what already answers it
// (our busy units within 2,000, our groups heading there), the nearest idle soldiers first (the home guard too), as
// groups that attack its first unit. near: only threats within 2,500 of (x, y) (a house just hit, AI_HouseAttacked).
void PlusArmy_Defend(GameState& s, std::uint8_t p, bool near = false, double x = 0, double y = 0);

// Food at the cap (from Plus_Brain, every 500 ticks; a computer or your economic advisor): at 95 % of the unit cap
// and more than 1,000,000 food, a market sells up to 200,000 food for iron and gold at the moving price while the
// food index is at least 0.5; a computer's idle tank plants, hangars and artillery depots repeat the dearest tank, gun
// or aircraft it can pay for (the mix and feed rules of A3 and stage 4 kept), and its barracks repeating soldiers
// switch to mortar men while the mix allows.
void PlusArmy_Spend(GameState& s, std::uint8_t p);

}  // namespace antanta::sim
