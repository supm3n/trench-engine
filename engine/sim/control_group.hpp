



#pragma once

#include <cstdint>

#include "state.hpp"

namespace antanta::sim {




void ControlGroup_RemoveUnit(Player& pl, Unit& u);


void ControlGroup_RemoveHouse(Player& pl, std::uint16_t house_slot);





void ControlGroup_Bind(GameState& s, const Command& c);

}  // namespace antanta::sim
