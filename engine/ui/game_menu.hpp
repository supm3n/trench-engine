// The in-game menu (docs/frontend.md 4.12, Menu.txt window 4): what its buttons do, which of them work in the new
// engine, and the setup of a restarted skirmish. No SDL: the front-end (ui/frontend) draws the window and the app
// builds the new game.
#pragma once

#include <cstdint>
#include <string>

#include "sim/skirmish.hpp"
#include "sim/state.hpp"
#include "ui/lang.hpp"

namespace antanta::ui {


enum class GameMenuItem : std::uint8_t { None, Continue, Restart, Settings, Save, Load, BackToMenu, QuickLoad };




GameMenuItem game_menu_item(std::uint16_t handler);

// Why a button does nothing in the new engine, in the display language (empty: it works). Restart needs the choices of
// a game started from the skirmish setup (`can_restart`), which our own saves keep and the original's .svg files do
// not.
std::string game_menu_unavailable(const Language& l, GameMenuItem item, bool can_restart);



std::string game_menu_confirm_key(GameMenuItem item);







sim::SkirmishSetup game_menu_restart_setup(const sim::SkirmishSetup& last, const sim::GameState& s, std::uint32_t seed);

}  // namespace antanta::ui
