// Saving and loading a Lua mission's script (slice M6, docs/missions.md 5.5 and 5.8, docs/save-own.md).
//
// Our own save: the game data of the script manager (groups, timers, keys, active, winner) are GameState fields in the
// SIM chunk; the Lua globals go into the LUA chunk as Lua lines (ScriptVm_DumpGlobals), sorted and exact (numbers
// round trip, shared tables stay shared). The load (ScriptVm_Reload) runs the script's top level again, as the

// globals; then the MessageBox texts come back (the F1 log, the last one in a window).
//

// lua_next order as "name = value" and "T.k = value" lines (numbers with 6 digits), a blank line, the groups (name,
// count, player, unit ids), the timers (name, code, base, period, repeats, fired, done), the MessageBox keys.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "formats/bytes.hpp"
#include "script/lua_manager.hpp"

namespace antanta::sim {

// The script's globals as Lua lines "name = value", sorted (numbers by value, then strings byte by byte, then
// booleans), each table once as "path = {}" and its fields after it, a table met again as "path = <first path>".
// Left out: functions (the script's top level makes them again), _G and _LOADED, and what cannot be written (a
// userdata, a table as a key, a global whose name is not a Lua name); each of those adds a line to the script's log.
std::string ScriptVm_DumpGlobals(ScriptVm& vm);

// Our own save's load: the script of the mission started on a copy of `s` (its top level runs as at the original's
// load, but whatever it changes in the game is thrown away), then every global the copy's run made that is not a
// function is cleared and `globals` (ScriptVm_DumpGlobals's lines) run, then the MessageBox texts are raised on `s` as
// events (ScriptVm_ReShow). `s` keeps its fields otherwise. No `globals` (a save made before slice M6): the top level's
// globals stay, and a log line says so.
ScriptVmPtr ScriptVm_Reload(GameState& s, ByteSpan script, const std::string& name, ScriptHost host,
                            const std::optional<std::string>& globals);




void ScriptVm_ReShow(GameState& s, const ScriptVm& vm);

// The original's Save\<name>.lua.
struct LuaSaveFile {
    struct Group {
        std::string name;
        std::uint32_t player = 0;
        std::vector<std::uint16_t> ids;
    };
    struct Timer {
        std::string name, code;
        float base = 0, period = 0;
        std::uint32_t repeats = 0, fired = 0;
        std::uint32_t done = 0;
    };
    std::vector<std::string> globals;  // the lines before the blank line, each run with lua_dostring
    std::vector<Group> groups;
    std::vector<Timer> timers;
    std::vector<std::string> keys;
};
std::optional<LuaSaveFile> parse_lua_save(ByteSpan file, std::string* error);






ScriptVmPtr ScriptVm_OpenSaved(GameState& s, ByteSpan script, const std::string& name, ScriptHost host,
                               const LuaSaveFile* save);

}  // namespace antanta::sim
