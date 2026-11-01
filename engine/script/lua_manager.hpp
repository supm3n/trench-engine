

// GameState; the manager's game data (active, winner, groups, timers, MessageBox keys) are GameState fields, so they

// MyPlayer's update when tick % 50 == MyPlayer (sim/tick.cpp).
//
// Determinism (docs/missions.md 5.6): numbers are doubles, converted to the C++ side by truncation (__ftol), the
// number formatting and parsing are ours (script/lua_user.h), the timers and groups are std::maps in byte order of
// their names as the original's, the scripts get no file, clock, OS, GC or coroutine function. An error of the script
// is caught and logged (the original crashes on one in main()), the mission goes on.
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/bytes.hpp"
#include "formats/museum.hpp"
#include "sim/state.hpp"

namespace antanta::sim {

// What the script reads from the game files: `read` a file by the path the script names in `require` ("Data\\
// Campaigns\\missions\\consts.lua", relative to the game folder; nullopt when missing), `texts` the text table of
// MessageBox and AddMessage (a key without a record shows as the key itself).
struct ScriptHost {
    std::function<std::optional<Bytes>(const std::string& path)> read;
    TextTable texts;
};

struct ScriptVmDeleter {
    void operator()(ScriptVm* vm) const;
};
using ScriptVmPtr = std::unique_ptr<ScriptVm, ScriptVmDeleter>;



std::string Mission_ScriptPath(std::string_view mission_path);






ScriptVmPtr ScriptVm_Open(GameState& s, ByteSpan script, const std::string& name, ScriptHost host);

// The script's errors so far, one line each (at most 100 kept; the count goes on), for the log and the tests.
const std::vector<std::string>& ScriptVm_Errors(const ScriptVm& vm);
std::uint32_t ScriptVm_ErrorCount(const ScriptVm& vm);

// Runs a piece of Lua in the mission's VM as a timer's code runs (tests). False on an error (logged).
bool ScriptVm_Run(ScriptVm& vm, GameState& s, std::string_view code);

}  // namespace antanta::sim
