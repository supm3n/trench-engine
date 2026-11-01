

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "state.hpp"

namespace antanta::sim {



void Game_SetClock(GameState& s, std::function<std::int64_t()> clock);


void Player_UpdatePlayTime(GameState& s, std::uint8_t p);


bool HasNoActiveUnits(const GameState& s, std::uint8_t p);



void TStrateg_Die(GameState& s, std::uint8_t p);





void LuaMission_End(GameState& s);

// Our rules only (docs/bugs.md B236): in a Lua mission you are beaten as in the skirmish (no active unit at your
// 50-tick check, or IsLost set by DeleteUnit before that check): TStrateg_Die for you (the lost result and the horn),
// the skirmish's message "!!! Your troops have been destroyed !!!" (end_message 2) and then the statistics. The
// original never runs the script again then and the mission hangs.
void LuaMission_Beaten(GameState& s);

// A network game's end for one player (docs/multiplayer.md, the owner's rule of 2026-10-18): 1 lost (wiped out), 0 won
// (alive and no living enemy left: the team won), 3 still playing. The sim keeps no per-player result there.
std::uint8_t NetGame_Outcome(const GameState& s, std::uint8_t me);



std::vector<std::uint8_t> Statistics_Players(const GameState& s);
void Statistics_Open(GameState& s);
struct PlayerStatistics {
    // Gold, Food (Provisions), Wood, Iron, Oil: extracted, spent, bought, sold.
    std::array<std::int32_t, 5> extracted{}, spent{}, bought{}, sold{};
    std::int32_t units_total = 0, units_lost = 0, units_killed = 0;
    std::int32_t houses_built = 0, houses_lost = 0, houses_destroyed = 0;
    std::uint8_t nation = 0;
    std::uint32_t duration = 0;  // seconds, shown as %02d:%02d:%02d
    std::int32_t points = 0;
};
PlayerStatistics Statistics_Of(const GameState& s, std::uint8_t p);

}  // namespace antanta::sim
