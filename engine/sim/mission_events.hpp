





#pragma once

#include <cstdint>

#include "state.hpp"

namespace antanta::sim {



void MissionEvents_Start(GameState& s);



bool MissionEvents_Update(GameState& s, std::uint8_t p);



bool MissionEvents_Complete(GameState& s, std::uint8_t p, std::size_t k);


void MissionEvent_Execute(GameState& s, std::uint8_t p, const MisBlock& block);



void Mission_MergePlayer(GameState& s, std::uint8_t from, std::uint8_t to, bool houses, bool units, bool resources);

void SendMyUnitsToPlayer(GameState& s, std::uint8_t from, std::uint8_t to);

void SendMyHousesToPlayer(GameState& s, std::uint8_t from, std::uint8_t to, bool units_sent);



std::uint16_t Reveal_Add(GameState& s, std::int32_t x, std::int32_t y, std::uint8_t radius);




void Mission_Message(GameState& s, std::uint8_t p, std::size_t k, std::uint8_t which, bool end);

}  // namespace antanta::sim
