



#pragma once

#include <cstdint>
#include <vector>

#include "sim/state.hpp"
#include "ui/hud.hpp"

namespace antanta::ui {




std::vector<sim::Command> fire_point_orders(const sim::GameState& s, const Selection& sel, std::int32_t x,
                                            std::int32_t y, std::uint8_t gas);

}  // namespace antanta::ui
