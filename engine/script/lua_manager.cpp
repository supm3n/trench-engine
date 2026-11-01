




#include "script/lua_manager.hpp"

#include "script/fixes.hpp"
#include "script/lua_vm.hpp"

#include <cstring>
#include <utility>

#include "formats/campaign.hpp"
#include "sim/fog.hpp"
#include "sim/mission_events.hpp"
#include "sim/movement.hpp"

extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include "sim/original_ids.hpp"
}

namespace antanta::sim {

void ScriptVmDeleter::operator()(ScriptVm* vm) const { std::unique_ptr<ScriptVm>{vm}.reset(); }

namespace {

constexpr float kThirtieth = 0.033333335f;

ScriptVm& Vm(lua_State* L) { return *static_cast<ScriptVm*>(lua_touserdata(L, lua_upvalueindex(1))); }



std::int64_t Ftol64(double v) {
    if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0)) return INT64_MIN;
    return static_cast<std::int64_t>(v);
}
std::int32_t ArgInt(lua_State* L, int i) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(Ftol64(luaL_checknumber(L, i))));
}
std::uint32_t ArgU32(lua_State* L, int i) { return static_cast<std::uint32_t>(Ftol64(luaL_checknumber(L, i))); }

// g_Strateg[p]: the original reads it without a test (a bad index crashes, docs/missions.md 5.3); here a Lua error.
std::uint8_t ArgPlayer(lua_State* L, int i) {
    const std::int32_t p = ArgInt(L, i);
    GameState& s = *Vm(L).s;
    if (p < 0 || static_cast<std::size_t>(p) >= s.players.size()) luaL_error(L, "player %d is not in the game", p);
    return static_cast<std::uint8_t>(p);
}


struct Rect {
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
};
Rect ArgRect(lua_State* L, int i) {
    luaL_checktype(L, i, LUA_TTABLE);
    Rect r;
    std::int32_t* field[4] = {&r.left, &r.top, &r.right, &r.bottom};
    static constexpr const char* kNames[4] = {"left", "top", "right", "bottom"};
    for (int k = 0; k < 4; ++k) {
        lua_pushstring(L, kNames[k]);
        lua_gettable(L, i);
        *field[k] = static_cast<std::int32_t>(static_cast<std::uint32_t>(Ftol64(lua_tonumber(L, -1))));
        lua_pop(L, 1);
    }
    return r;
}

std::string Text(const ScriptVm& vm, const std::string& key) {
    const TextRecord* r = find_text(vm.host.texts, key);
    return r != nullptr ? r->text : key;
}

void Event(GameState& s, SimEvent e) { s.events.push_back(std::move(e)); }

// Runs the function on the top of the stack (lua_pcall); an error is logged with `what`, the mission goes on.
bool Call(ScriptVm& vm, const char* what) {
    if (lua_pcall(vm.L, 0, 0, 0) == 0) return true;
    const char* m = lua_tostring(vm.L, -1);
    ScriptVm_Log(vm, std::string(what) + ": " + (m != nullptr ? m : "(no message)"));
    lua_pop(vm.L, 1);
    return false;
}





int Lua_MessageBox(lua_State* L) {
    const char* key = luaL_checkstring(L, 1);
    ScriptVm& vm = Vm(L);
    GameState& s = *vm.s;
    // After LuaManager_StopCurrentScript the rest of main() and the pass's timers still run, and the original shows
    // their messages after the end text (Sventian won with the convoy alive: "destroy the tent" after "you won").
    // Our rules drop a message once the script has stopped (docs/bugs.md B241).
    if (!s.original_rules && s.script_active == 0) return 0;
    s.script_keys.emplace_back(key);
    SimEvent e;
    e.kind = SimEvent::Kind::MissionMessage;
    e.player = s.my_player;
    e.text = Text(vm, key);
    e.mission.key = key;
    e.mission.pause = true;
    Event(s, std::move(e));
    ++vm.windows;
    return 0;
}


int Lua_AddMessage(lua_State* L) {
    const char* k1 = luaL_checkstring(L, 1);
    const std::int32_t n = ArgInt(L, 2);
    const char* k2 = luaL_checkstring(L, 3);
    ScriptVm& vm = Vm(L);
    SimEvent e;
    e.kind = SimEvent::Kind::MissionLine;
    e.player = vm.s->my_player;
    e.text = Text(vm, k1) + ' ' + std::to_string(n) + ' ' + Text(vm, k2);
    Event(*vm.s, std::move(e));
    return 0;
}


int Lua_GetTime(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(Vm(L).s->tick) * static_cast<double>(kThirtieth));
    return 1;
}


int Lua_SetPlayerAIType(lua_State* L) {
    const std::uint8_t p = ArgPlayer(L, 1);
    const auto t = static_cast<std::uint8_t>(ArgInt(L, 2));
    Player& pl = Vm(L).s->players[p];
    if (pl.is_lost == 0) pl.ai_type = t, pl.ai_kind = t;
    return 0;
}





int Lua_IsPlayerUnitInArea(lua_State* L) {
    const std::int32_t x1 = ArgInt(L, 1), y1 = ArgInt(L, 2), x2 = ArgInt(L, 3), y2 = ArgInt(L, 4);
    const std::uint32_t p = ArgU32(L, 5);
    const GameState& s = *Vm(L).s;
    const std::int64_t w = s.map.width, cells = static_cast<std::int64_t>(s.map.cell_units.size());
    bool found = false;
    for (std::int64_t y = y1; y < y2 && !found; ++y) {
        for (std::int64_t x = x1; x < x2 && !found; ++x) {
            const std::int64_t i = w * y + x;
            if (i < 0 || i >= cells) continue;
            if (!s.original_rules && (x < 0 || x >= w)) continue;
            const std::vector<UnitRef>& list = s.map.cell_units[static_cast<std::size_t>(i)];
            for (std::size_t k = 0; k < list.size() && k < 40; ++k) {
                if (list[k].player == p) found = true;
            }
        }
    }
    lua_pushboolean(L, found ? 1 : 0);
    return 1;
}



int Lua_IsPlayerHouseInArea(lua_State* L) {
    const std::int32_t x1 = ArgInt(L, 1), y1 = ArgInt(L, 2), x2 = ArgInt(L, 3), y2 = ArgInt(L, 4);
    const std::uint8_t p = ArgPlayer(L, 5);
    const std::uint32_t type = ArgU32(L, 6);
    const Player& pl = Vm(L).s->players[p];
    const std::size_t n = std::min<std::size_t>(static_cast<std::uint16_t>(pl.houses_built), pl.houses.size());
    bool found = false;
    for (std::size_t k = 0; k < n && !found; ++k) {
        const House& h = pl.houses[k];
        found = (h.type == type || type == 0xffffffffu) && h.is_destroyed == 0 && x1 < h.map_x && h.map_x < x2 &&
                y1 < h.map_y && h.map_y < y2;
    }
    lua_pushboolean(L, found ? 1 : 0);
    return 1;
}




int Lua_GetHousesQuantity(lua_State* L) {
    const std::int64_t i = static_cast<std::int64_t>(ArgInt(L, 1)) * 25 + ArgInt(L, 2);
    const GameState& s = *Vm(L).s;
    if (i < -1 || i >= 8 * 25) luaL_error(L, "GetHousesQuantity: index %d is outside the table", static_cast<int>(i));
    std::uint32_t v = 0;
    if (i == -1) {
        if (s.players.size() > 7) v = s.players[7].unit_count[39] >> 8;
    } else if (static_cast<std::size_t>(i / 25) < s.players.size()) {
        v = s.players[static_cast<std::size_t>(i / 25)].houses_done[static_cast<std::size_t>(i % 25)];
    }
    lua_pushnumber(L, static_cast<double>(v));
    return 1;
}


int Lua_GetPlayerStatsKilled(lua_State* L) {
    const std::uint8_t p = ArgPlayer(L, 1);
    lua_pushnumber(L, static_cast<double>(Vm(L).s->players[p].units_lost));
    return 1;
}


int Lua_GetPlayerStatsUnits(lua_State* L) {
    const std::uint8_t p = ArgPlayer(L, 1);
    lua_pushnumber(L, static_cast<double>(Vm(L).s->players[p].units_alive));
    return 1;
}


int Lua_GetPlayerStatsHouses(lua_State* L) {
    const std::uint8_t p = ArgPlayer(L, 1);
    const Player& pl = Vm(L).s->players[p];
    lua_pushnumber(L, static_cast<double>(pl.houses_built - pl.houses_lost));
    return 1;
}


int Lua_SetShowFogForAllies(lua_State* L) {
    Vm(L).s->show_fog_allies = lua_toboolean(L, 1) != 0 ? 1 : 0;
    return 0;
}


int Lua_OpenMapToPlayer(lua_State* L) {
    const std::int32_t x = ArgInt(L, 1), y = ArgInt(L, 2);
    const auto r = static_cast<std::uint8_t>(ArgInt(L, 3));
    Reveal_Add(*Vm(L).s, x, y, r);
    return 0;
}


int Lua_SetCamera(lua_State* L) {
    const auto x = static_cast<std::int16_t>(ArgInt(L, 1)), y = static_cast<std::int16_t>(ArgInt(L, 2));
    GameState& s = *Vm(L).s;
    SimEvent e;
    e.kind = SimEvent::Kind::MissionCamera;
    e.player = s.my_player;
    e.cell_x = x, e.cell_y = y;
    e.mission.centred = true;
    Event(s, std::move(e));
    return 0;
}


int Lua_StopCurrentScript(lua_State* L) {
    const std::uint32_t w = ArgU32(L, 1);
    GameState& s = *Vm(L).s;
    s.script_active = 0, s.script_winner = w;
    Event_Sound(s, 9, w == s.my_player ? 1 : 2, 7);
    return 0;
}


int Lua_GroupCreate(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    Vm(L).s->script_groups[name] = GameState::ScriptGroup{};
    return 0;
}


int Lua_GroupRemove(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    Vm(L).s->script_groups.erase(name);
    return 0;
}




int Lua_GroupAddInRect(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const Rect r = ArgRect(L, 2);
    const std::uint8_t p = ArgPlayer(L, 3);
    const std::uint32_t type = ArgU32(L, 4), max = ArgU32(L, 5);
    GameState& s = *Vm(L).s;
    const auto it = s.script_groups.find(name);
    if (it == s.script_groups.end()) return 0;
    GameState::ScriptGroup& g = it->second;
    g.player = p;
    Player& pl = s.players[p];
    std::uint32_t added = 0;
    std::uint32_t k = pl.first_unit;
    do {
        if (k == kNoSlot) break;
        const Unit& u = pl.unit_slots[k];
        if (u.is_deleted == 0 && (type == 0xffffffffu || u.type == type) && u.work == 0xff && r.left < u.map_x &&
            u.map_x < r.right && r.top < u.map_y && u.map_y < r.bottom) {
            g.members[u.id] = Unit_Ref(p, u);
            ++added;
        }
        k = u.next;
    } while (added < max);
    return 0;
}







int Lua_GroupSendTo(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const auto x = static_cast<std::uint16_t>(ArgInt(L, 2)), y = static_cast<std::uint16_t>(ArgInt(L, 3));
    luaL_checktype(L, 4, LUA_TTABLE);
    std::int32_t num[6];
    static constexpr const char* kNum[6] = {"AiLevel",    "TemplateType", "TemplateCount",
                                            "TargetType", "TargetPlayer", "TargetNum"};
    for (int k = 0; k < 6; ++k) {
        lua_pushstring(L, kNum[k]);
        lua_gettable(L, 4);
        num[k] = static_cast<std::int32_t>(static_cast<std::uint32_t>(Ftol64(lua_tonumber(L, -1))));
        lua_pop(L, 1);
    }
    bool flag[2];
    static constexpr const char* kFlag[2] = {"MayDismiss", "NoGroup"};
    for (int k = 0; k < 2; ++k) {
        lua_pushstring(L, kFlag[k]);
        lua_gettable(L, 4);
        flag[k] = lua_toboolean(L, -1) != 0;
        lua_pop(L, 1);
    }
    GameState& s = *Vm(L).s;
    const auto it = s.script_groups.find(name);
    if (it == s.script_groups.end()) return 0;
    const GameState::ScriptGroup& g = it->second;
    std::vector<Unit*> list;
    for (const auto& [id, ref] : g.members) {
        Unit* u = Unit_Find(s, ref);
        if (u != nullptr && u->is_deleted == 0 && u->work != 3 && u->work != 0x14) list.insert(list.begin(), u);
    }
    if (g.player >= s.players.size()) {  // a group no AddInRect filled: no members, and its player byte is garbage
        if (!list.empty()) Unported(s, orig::kLuaGroupSendTo);
        return 0;
    }
    std::int32_t px = (static_cast<std::int32_t>(x) * 2 - (~static_cast<std::int32_t>(y) & 1)) * 0x30;
    std::int32_t py = static_cast<std::int32_t>(y) * 0x18;
    AI_FormGroups(s, g.player, std::move(list), static_cast<std::uint8_t>(num[0]), static_cast<std::uint8_t>(num[1]),
                  static_cast<std::uint8_t>(num[2]), static_cast<std::uint8_t>(num[3]), px, py,
                  static_cast<std::uint8_t>(num[4]), static_cast<std::uint16_t>(num[5]), flag[0] ? 1 : 0, flag[1],
                  false, static_cast<std::int32_t>(static_cast<std::uint16_t>(g.members.size())));
    return 0;
}





const GameState::ScriptGroup* FindGroup(const GameState& s, const char* name) {
    const auto it = s.script_groups.find(name);
    return it == s.script_groups.end() ? nullptr : &it->second;
}
const GameState::ScriptGroup* KnownGroup(lua_State* L, const char* name) {
    const GameState::ScriptGroup* g = FindGroup(*Vm(L).s, name);
    if (g == nullptr) luaL_error(L, "no units group `%s'", name);
    return g;
}



int Lua_GroupIsInRect(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const Rect r = ArgRect(L, 2);
    const GameState::ScriptGroup* g = KnownGroup(L, name);
    GameState& s = *Vm(L).s;
    bool found = false;
    for (const auto& [id, ref] : g->members) {
        const Unit* u = Unit_Find(s, ref);
        if (u != nullptr && r.left < u->map_x && u->map_x < r.right && r.top < u->map_y && u->map_y < r.bottom) {
            found = true;
            break;
        }
    }
    lua_pushboolean(L, found ? 1 : 0);
    return 1;
}


int Lua_GroupGetUnitsQuantity(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const GameState::ScriptGroup* g = KnownGroup(L, name);
    GameState& s = *Vm(L).s;
    std::int32_t n = 0;
    for (const auto& [id, ref] : g->members) {
        const Unit* u = Unit_Find(s, ref);
        if (u != nullptr && u->is_deleted == 0) ++n;
    }
    lua_pushnumber(L, static_cast<double>(n));
    return 1;
}



int Lua_TimerCreate(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const char* code = luaL_checkstring(L, 2);
    const auto period = static_cast<float>(luaL_checknumber(L, 3));
    const std::uint32_t repeats = ArgU32(L, 4);
    ScriptVm& vm = Vm(L);
    GameState& s = *vm.s;
    GameState::ScriptTimer t;
    t.code = code;
    t.base = static_cast<float>(static_cast<double>(s.tick) * static_cast<double>(kThirtieth));
    t.period = period, t.repeats = repeats;
    s.script_timers[name] = std::move(t);
    if (vm.running == name) vm.running_touched = true;
    return 0;
}


int Lua_TimerDestroy(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    ScriptVm& vm = Vm(L);
    vm.s->script_timers.erase(name);
    if (vm.running == name) vm.running_touched = true;
    return 0;
}

// The base library's print goes to the script's log, not to stdout.
int Lua_Print(lua_State* L) {
    const int n = lua_gettop(L);
    std::string line = "print:";
    for (int i = 1; i <= n; ++i) {
        const char* v = lua_tostring(L, i);
        line += ' ';
        line += v != nullptr ? v : lua_typename(L, lua_type(L, i));
    }
    ScriptVm_Log(Vm(L), line);
    return 0;
}




int Lua_Require(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    lua_settop(L, 1);
    lua_getglobal(L, "_LOADED");
    if (!lua_istable(L, 2)) return luaL_error(L, "`_LOADED' is not a table");
    lua_pushvalue(L, 1);
    lua_rawget(L, 2);
    if (lua_toboolean(L, -1)) return 1;
    lua_settop(L, 2);
    int status = -1;
    {
        ScriptVm& vm = Vm(L);
        const std::optional<Bytes> file = vm.host.read ? vm.host.read(name) : std::nullopt;
        if (file) {
            status = luaL_loadbuffer(L, reinterpret_cast<const char*>(file->data()), file->size(), name);
        }
    }
    if (status < 0) return luaL_error(L, "could not load package `%s'", name);
    if (status != 0) return lua_error(L);  // the load error's message is on the stack
    lua_call(L, 0, 1);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushboolean(L, 1);
    }
    lua_pushvalue(L, 1);
    lua_pushvalue(L, -2);
    lua_rawset(L, 2);
    return 1;
}

void Register(ScriptVm& vm, const char* name, lua_CFunction f) {
    lua_pushstring(vm.L, name);
    lua_pushlightuserdata(vm.L, &vm);
    lua_pushcclosure(vm.L, f, 1);
    lua_settable(vm.L, LUA_GLOBALSINDEX);
}

void SetNil(lua_State* L, const char* name) {
    lua_pushnil(L);
    lua_setglobal(L, name);
}

}  // namespace

void ScriptVm_Log(ScriptVm& vm, const std::string& what) {
    ++vm.error_count;
    if (vm.errors.size() < 100) vm.errors.push_back(what);
}

bool ScriptVm_DoBuffer(ScriptVm& vm, const char* data, std::size_t size, const char* chunk) {
    if (luaL_loadbuffer(vm.L, data, size, chunk) != 0) {
        const char* m = lua_tostring(vm.L, -1);
        ScriptVm_Log(vm, std::string("load ") + chunk + ": " + (m != nullptr ? m : "(no message)"));
        lua_pop(vm.L, 1);
        return false;
    }
    return Call(vm, chunk);
}

std::string Mission_ScriptPath(std::string_view mission_path) {
    std::string p(mission_path.substr(0, mission_path.size() >= 3 ? mission_path.size() - 3 : 0));
    return p + "lua";
}

ScriptVmPtr ScriptVm_Start(GameState& s, ByteSpan script, const std::string& name, ScriptHost host) {
    ScriptVmPtr vm(std::make_unique<ScriptVm>().release());
    vm->host = std::move(host);
    vm->name = name;
    vm->s = &s;

    s.lua_mission = 1;
    s.script_active = 1, s.script_winner = 0;
    s.script_groups.clear(), s.script_timers.clear(), s.script_keys.clear();
    lua_State* L = lua_open();
    vm->L = L;
    luaopen_base(L);
    lua_settop(L, 0);
    // The sandbox: no files, no clock, no OS, no GC control or weak tables, no coroutines; print to our log, our
    // require; the original's _VERSION (LuaPlus's Lua 5.0, No_name.lua).
    for (const char* n : {"dofile", "loadfile", "collectgarbage", "gcinfo", "newproxy", "coroutine", "LUA_PATH"}) {
        SetNil(L, n);
    }
    lua_pushstring(L, "Lua 5.0");
    lua_setglobal(L, "_VERSION");
    Register(*vm, "print", Lua_Print);
    Register(*vm, "require", Lua_Require);
    static constexpr std::pair<const char*, lua_CFunction> kFunctions[] = {
        {"MessageBox", Lua_MessageBox},
        {"GetTime", Lua_GetTime},
        {"SetPlayerAIType", Lua_SetPlayerAIType},
        {"IsPlayerUnitInArea", Lua_IsPlayerUnitInArea},
        {"OpenMapToPlayer", Lua_OpenMapToPlayer},
        {"SetCamera", Lua_SetCamera},
        {"IsPlayerHouseInArea", Lua_IsPlayerHouseInArea},
        {"GetPlayerStatsKilled", Lua_GetPlayerStatsKilled},
        {"GetPlayerStatsUnits", Lua_GetPlayerStatsUnits},
        {"GetPlayerStatsHouses", Lua_GetPlayerStatsHouses},
        {"SetShowFogForAllies", Lua_SetShowFogForAllies},
        {"LuaManager_StopCurrentScript", Lua_StopCurrentScript},
        {"UnitsGroup_Create", Lua_GroupCreate},
        {"UnitsGroup_Remove", Lua_GroupRemove},
        {"UnitsGroup_AddInRect", Lua_GroupAddInRect},
        {"UnitsGroup_SendTo", Lua_GroupSendTo},
        {"UnitsGroup_IsInRect", Lua_GroupIsInRect},
        {"UnitsGroup_GetUnitsQuantity", Lua_GroupGetUnitsQuantity},
        {"Timer_Create", Lua_TimerCreate},
        {"Timer_Destroy", Lua_TimerDestroy},
        {"GetHousesQuantity", Lua_GetHousesQuantity},
        {"AddMessage", Lua_AddMessage},
    };
    for (const auto& [n, f] : kFunctions) Register(*vm, n, f);


    std::string text(reinterpret_cast<const char*>(script.data()), script.size());
    if (!s.original_rules) {
        std::vector<std::string> notes;
        text = Script_ApplyFixes(name, text, &notes);
        for (const std::string& n : notes) ScriptVm_Log(*vm, n);
    }
    const bool ok = !text.empty() && ScriptVm_DoBuffer(*vm, text.data(), text.size(), name.c_str());
    if (ok) {
        lua_getglobal(L, "main");
        vm->main_ref = luaL_ref(L, LUA_REGISTRYINDEX);  // LUA_REFNIL when there is none
    } else {
        s.script_active = 0;
    }
    lua_settop(L, 0);
    vm->s = nullptr;
    return vm;
}

ScriptVmPtr ScriptVm_Open(GameState& s, ByteSpan script, const std::string& name, ScriptHost host) {
    ScriptVmPtr vm = ScriptVm_Start(s, script, name, std::move(host));
    Fog_Rebuild(s);  // MenuStart_OnClick's fog pass comes after Mission_Setup: the allies' sight may be shared now
    return vm;
}

const std::vector<std::string>& ScriptVm_Errors(const ScriptVm& vm) { return vm.errors; }
std::uint32_t ScriptVm_ErrorCount(const ScriptVm& vm) { return vm.error_count; }

bool ScriptVm_Run(ScriptVm& vm, GameState& s, std::string_view code) {
    vm.s = &s;
    const std::string c(code);
    const bool ok = ScriptVm_DoBuffer(vm, c.data(), c.size(), c.c_str());
    lua_settop(vm.L, 0);
    vm.s = nullptr;
    return ok;
}



// iterator then steps once more, so the next timer is not checked in this pass (fixed under our rules, docs/bugs.md
// B213); else when base + period < tick * f32(1/30) (both on the x87, 53 bits, strict) its code runs (lua_dostring,
// errors ignored), base = f32(base + period), fired + 1. Timer code that makes or removes timers: a new one after this
// one in name order is checked in the same pass; this timer replaced or removed by its own code is not updated (the
// original writes into the freed timer).
std::uint32_t ScriptVm_Pass(ScriptVm& vm, GameState& s) {
    vm.s = &s;
    vm.windows = 0;
    if (s.script_active != 0) {
        if (vm.main_ref != LUA_NOREF && vm.main_ref != LUA_REFNIL) {
            lua_rawgeti(vm.L, LUA_REGISTRYINDEX, vm.main_ref);
            Call(vm, "main");
        }
        std::map<std::string, GameState::ScriptTimer>& timers = s.script_timers;
        auto it = timers.begin();
        while (it != timers.end()) {
            if (it->second.fired >= it->second.repeats) {
                it = timers.erase(it);
                if (s.original_rules && it != timers.end()) ++it;
                continue;
            }
            const double now = static_cast<double>(s.tick) * static_cast<double>(kThirtieth);
            if (static_cast<double>(it->second.period) + static_cast<double>(it->second.base) < now) {
                const std::string name = it->first, code = it->second.code;
                vm.running = name, vm.running_touched = false;
                ScriptVm_DoBuffer(vm, code.data(), code.size(), code.c_str());
                lua_settop(vm.L, 0);
                vm.running.clear();
                it = timers.find(name);
                if (it == timers.end()) {
                    it = timers.upper_bound(name);
                    continue;
                }
                if (!vm.running_touched) {
                    GameState::ScriptTimer& t = it->second;
                    t.base = static_cast<float>(static_cast<double>(t.period) + static_cast<double>(t.base));
                    ++t.fired;
                }
            }
            ++it;
        }
    }
    vm.s = nullptr;
    return vm.windows;
}

}  // namespace antanta::sim
