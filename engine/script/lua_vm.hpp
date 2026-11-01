// The Lua VM of a mission inside script/ (not for the rest of the engine, which sees ScriptVm only as an opaque type):
// shared by the manager (lua_manager.cpp) and the saves of the script (lua_save.cpp).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "script/lua_manager.hpp"

extern "C" {
#include "lua.h"
}

namespace antanta::sim {

struct ScriptVm {
    lua_State* L = nullptr;
    ScriptHost host;
    std::string name;
    GameState* s = nullptr;     // the state while the script runs (a pass, the start, a test's run)
    int main_ref = LUA_NOREF;
    std::uint32_t windows = 0;  // the message windows MessageBox opened in this pass
    std::string running;        // the timer whose code runs now
    bool running_touched = false;
    std::vector<std::string> errors;
    std::uint32_t error_count = 0;
    ~ScriptVm() {
        if (L != nullptr) lua_close(L);
    }
};

// A line of the script's log (errors, print, fixes not applied); at most 100 kept, the count goes on.
void ScriptVm_Log(ScriptVm& vm, const std::string& what);

// lua_dobuffer / lua_dostring: load and run; false on an error of either (logged).
bool ScriptVm_DoBuffer(ScriptVm& vm, const char* data, std::size_t size, const char* chunk);



ScriptVmPtr ScriptVm_Start(GameState& s, ByteSpan script, const std::string& name, ScriptHost host);

}  // namespace antanta::sim
