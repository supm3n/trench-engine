// GameState as trace lines in the recorder's format (tools/recorder/FORMAT.md), so tools/recorder/compare.py
// can diff the engine against a recording. A read-only view: no sim behaviour here.
#pragma once

#include <cstdint>
#include <string>

#include "state.hpp"

namespace antanta::sim {

inline constexpr int kTraceVersion = 2;  // tools/recorder/FORMAT.md; the engine writes no K lines

// The H lines (source = engine; fields the engine does not have are "-").
std::string trace_header();
// R line for a table fill: saveload = 1 when the fill came from loading a save. Caller address is "-".
std::string trace_rng_fill(const GameState& s, std::uint32_t tick, bool saveload);
// O lines for the orders the last step() applied before its tick (GameState::orders, Command_Print).
std::string trace_orders(const GameState& s);


std::string trace_commands(const GameState& s);
// D lines for the view inputs the last step() applied before its tick (GameState::view_inputs, Command_PrintView).
std::string trace_view(const GameState& s);
// Engine only, not in tools/recorder/FORMAT.md yet (its S is a script action): `G<TAB>tick<TAB>hash` with
// GameState_Hash as 16 hex digits, after the B lines of a tick. compare.py and trace_subset.py skip it.
std::string trace_state_hash(const GameState& s);
// T line, then P, U and B lines for the state after a tick. tick_enter = g_GameTick before the tick.
std::string trace_tick(const GameState& s, std::uint32_t tick_enter);

}  // namespace antanta::sim
