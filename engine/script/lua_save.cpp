// Saving and loading a Lua mission's script (script/lua_save.hpp, slice M6, docs/missions.md 5.5 and 5.8).
#include "script/lua_save.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>
#include <utility>

#include "formats/campaign.hpp"
#include "script/lua_vm.hpp"
#include "sim/events.hpp"
#include "sim/movement.hpp"

namespace antanta::sim {

namespace {

// A key of a table as the dump sorts it: numbers by value, then strings byte by byte, then false and true. Other key
// types (tables, functions) cannot be written; `other` marks them.
struct Key {
    int type = LUA_TNIL;
    double num = 0;
    std::string str;
    bool b = false;
};

int Rank(const Key& k) {
    return k.type == LUA_TNUMBER ? 0 : k.type == LUA_TSTRING ? 1 : k.type == LUA_TBOOLEAN ? 2 : 3;
}

bool KeyLess(const Key& a, const Key& b) {
    if (Rank(a) != Rank(b)) return Rank(a) < Rank(b);
    if (a.type == LUA_TNUMBER) return a.num < b.num;
    if (a.type == LUA_TSTRING) return a.str < b.str;
    return !a.b && b.b;
}

std::string LuaString(lua_State* L, int i) { return std::string(lua_tostring(L, i), lua_strlen(L, i)); }

// The keys of the table at stack index t (absolute), sorted; the number of keys of other types in *other.
std::vector<Key> SortedKeys(lua_State* L, int t, int* other) {
    std::vector<Key> out;
    lua_pushnil(L);
    while (lua_next(L, t) != 0) {
        lua_pop(L, 1);
        Key k;
        k.type = lua_type(L, -1);
        if (k.type == LUA_TNUMBER) {
            k.num = lua_tonumber(L, -1);
        } else if (k.type == LUA_TSTRING) {
            k.str = LuaString(L, -1);  // a string key: lua_tostring does not change it, so lua_next goes on
        } else if (k.type == LUA_TBOOLEAN) {
            k.b = lua_toboolean(L, -1) != 0;
        } else {
            ++*other;
            continue;
        }
        out.push_back(std::move(k));
    }
    std::sort(out.begin(), out.end(), KeyLess);
    return out;
}

void PushKey(lua_State* L, const Key& k) {
    if (k.type == LUA_TNUMBER) {
        lua_pushnumber(L, k.num);
    } else if (k.type == LUA_TSTRING) {
        lua_pushlstring(L, k.str.data(), k.str.size());
    } else {
        lua_pushboolean(L, k.b ? 1 : 0);
    }
}

// A number as Lua reads it back to the same double: the shortest round trip (std::to_chars, the same on every target;
// script/lua_user.cpp reads it with std::from_chars); the infinities as 1e999 (out of range, read as HUGE_VAL), NaN as
// 0/0 (its sign bit is not kept; no shipped script makes one).
std::string NumberText(double v) {
    if (std::isnan(v)) return "0/0";
    if (std::isinf(v)) return v > 0 ? "1e999" : "-1e999";
    char buf[40];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, r.ptr);
}

// A string as a Lua 5.0 literal: " and \ escaped, every byte outside 0x20..0x7e as \ddd (three digits, so a digit
// after it cannot join it).
std::string StringText(const std::string& s) {
    std::string out = "\"";
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\') {
            out += '\\', out += ch;
        } else if (c < 0x20 || c > 0x7e) {
            out += '\\';
            out += static_cast<char>('0' + c / 100), out += static_cast<char>('0' + c / 10 % 10);
            out += static_cast<char>('0' + c % 10);
        } else {
            out += ch;
        }
    }
    return out + '"';
}

std::string KeyText(const Key& k) {
    if (k.type == LUA_TNUMBER) return NumberText(k.num);
    if (k.type == LUA_TSTRING) return StringText(k.str);
    return k.b ? "true" : "false";
}

// A Lua name (llex.c: a letter or _, then letters, digits, _), not a reserved word.
bool IsName(const std::string& s) {
    static constexpr const char* kReserved[] = {"and", "break",    "do",     "else", "elseif", "end",   "false",
                                                "for", "function", "if",     "in",   "local",  "nil",   "not",
                                                "or",  "repeat",   "return", "then", "true",   "until", "while"};
    if (s.empty() || (s[0] >= '0' && s[0] <= '9')) return false;
    for (const char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return std::none_of(std::begin(kReserved), std::end(kReserved), [&](const char* r) { return s == r; });
}

struct Dumper {
    ScriptVm& vm;
    std::map<const void*, std::string> seen;  // a table's first path
    std::string out;

    // The fields of the table at index t under `path` ("" for the globals: a name, or _G[key] for another key).
    void Table(int t, const std::string& path) {
        lua_State* L = vm.L;
        int other = 0;
        const std::vector<Key> keys = SortedKeys(L, t, &other);
        if (other != 0) {
            ScriptVm_Log(vm, "save: " + std::to_string(other) + " keys of " + (path.empty() ? "the globals" : path) +
                                 " are tables or functions and are not saved");
        }
        for (const Key& k : keys) {
            std::string child;
            if (path.empty()) {
                if (k.type == LUA_TSTRING && (k.str == "_G" || k.str == "_LOADED")) continue;
                child = k.type == LUA_TSTRING && IsName(k.str) ? k.str : "_G[" + KeyText(k) + "]";
            } else {
                child = k.type == LUA_TSTRING && IsName(k.str) ? path + "." + k.str : path + "[" + KeyText(k) + "]";
            }
            lua_checkstack(L, 4);
            PushKey(L, k);
            lua_rawget(L, t);
            switch (lua_type(L, -1)) {
                case LUA_TFUNCTION: break;  // the top level makes the script's functions again
                case LUA_TNUMBER: out += child + " = " + NumberText(lua_tonumber(L, -1)) + '\n'; break;
                case LUA_TSTRING: out += child + " = " + StringText(LuaString(L, -1)) + '\n'; break;
                case LUA_TBOOLEAN: out += child + (lua_toboolean(L, -1) != 0 ? " = true\n" : " = false\n"); break;
                case LUA_TTABLE: {
                    const void* p = lua_topointer(L, -1);
                    const auto it = seen.find(p);
                    if (it != seen.end()) {
                        out += child + " = " + it->second + '\n';  // the same table again: shared, not a copy
                    } else {
                        seen.emplace(p, child);
                        out += child + " = {}\n";
                        Table(lua_gettop(L), child);
                    }
                    break;
                }
                default:
                    ScriptVm_Log(vm, "save: " + child + " is a " + lua_typename(L, lua_type(L, -1)) + ", not saved");
            }
            lua_pop(L, 1);
        }
    }
};

// Every global that is not a function, _G or _LOADED is set to nil (before the saved globals come back).
void ClearDataGlobals(lua_State* L) {
    std::vector<std::string> names;
    lua_pushnil(L);
    while (lua_next(L, LUA_GLOBALSINDEX) != 0) {
        const bool data = lua_type(L, -1) != LUA_TFUNCTION;
        lua_pop(L, 1);
        if (data && lua_type(L, -1) == LUA_TSTRING) {
            std::string n = LuaString(L, -1);
            if (n != "_G" && n != "_LOADED") names.push_back(std::move(n));
        }
    }
    for (const std::string& n : names) {
        lua_pushlstring(L, n.data(), n.size());
        lua_pushnil(L);
        lua_rawset(L, LUA_GLOBALSINDEX);
    }
}


void RunLines(ScriptVm& vm, const std::string& text, const char* chunk) {
    std::size_t a = 0;
    while (a < text.size()) {
        std::size_t b = text.find('\n', a);
        if (b == std::string::npos) b = text.size();
        std::size_t e = b;
        if (e > a && text[e - 1] == '\r') --e;
        if (e > a) ScriptVm_DoBuffer(vm, text.data() + a, e - a, chunk);
        lua_settop(vm.L, 0);
        a = b + 1;
    }
}

bool ParseU32(const std::string& s, std::uint32_t* v) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), *v);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}
bool ParseF32(const std::string& s, float* v) {
    const auto r = std::from_chars(s.data(), s.data() + s.size(), *v);
    return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

}  // namespace

std::string ScriptVm_DumpGlobals(ScriptVm& vm) {
    lua_State* L = vm.L;
    Dumper d{vm, {}, {}};
    lua_settop(L, 0);
    lua_pushvalue(L, LUA_GLOBALSINDEX);
    d.seen.emplace(lua_topointer(L, 1), "_G");
    lua_pushstring(L, "_LOADED");
    lua_rawget(L, 1);
    if (lua_istable(L, -1)) d.seen.emplace(lua_topointer(L, -1), "_LOADED");
    lua_pop(L, 1);
    d.Table(1, "");
    lua_settop(L, 0);
    return d.out;
}

ScriptVmPtr ScriptVm_Reload(GameState& s, ByteSpan script, const std::string& name, ScriptHost host,
                            const std::optional<std::string>& globals) {
    // The top level on a copy: its calls (SetShowFogForAllies, a group or a timer made there) must not change the
    // saved game, which has their results already.
    auto scratch = std::make_unique<GameState>(s);
    ScriptVmPtr vm = ScriptVm_Start(*scratch, script, name, std::move(host));
    vm->s = scratch.get();
    if (globals) {
        ClearDataGlobals(vm->L);
        RunLines(*vm, *globals, "saved globals");
    } else {
        ScriptVm_Log(*vm, "load: this save holds no Lua globals (made before slice M6); the script starts over");
    }
    lua_settop(vm->L, 0);
    vm->s = nullptr;
    ScriptVm_ReShow(s, *vm);
    return vm;
}

void ScriptVm_ReShow(GameState& s, const ScriptVm& vm) {
    for (std::size_t i = 0; i < s.script_keys.size(); ++i) {
        const std::string& key = s.script_keys[i];
        const TextRecord* r = find_text(vm.host.texts, key);
        SimEvent e;
        e.kind = SimEvent::Kind::MissionMessage;
        e.player = s.my_player;
        e.text = r != nullptr ? r->text : key;
        e.mission.key = key;
        e.mission.pause = true;
        e.mission.log_only = i + 1 < s.script_keys.size();
        s.events.push_back(std::move(e));
    }
}

std::optional<LuaSaveFile> parse_lua_save(ByteSpan file, std::string* error) {
    std::vector<std::string> lines;
    {
        std::string line;
        for (const std::uint8_t c : file) {
            if (c == '\n') {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                lines.push_back(std::move(line));
                line.clear();
            } else {
                line += static_cast<char>(c);
            }
        }
        if (!line.empty()) {
            if (line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
        }
    }
    LuaSaveFile out;
    std::size_t i = 0;
    for (; i < lines.size() && !lines[i].empty(); ++i) out.globals.push_back(lines[i]);
    ++i;  // the blank line
    // The rest is read with operator>> (numbers, which skip blank lines) and getline (names, code, keys).
    std::size_t bad = 0;
    auto skip_blank = [&] {
        while (i < lines.size() && lines[i].empty()) ++i;
    };
    auto number = [&](std::uint32_t* v) {
        skip_blank();
        if (i >= lines.size() || !ParseU32(lines[i], v)) bad = bad != 0 ? bad : i + 1;
        ++i;
    };
    auto real = [&](float* v) {
        skip_blank();
        if (i >= lines.size() || !ParseF32(lines[i], v)) bad = bad != 0 ? bad : i + 1;
        ++i;
    };
    auto text = [&](std::string* v) {
        if (i >= lines.size()) bad = bad != 0 ? bad : i + 1;
        if (i < lines.size()) *v = lines[i];
        ++i;
    };
    std::uint32_t n = 0;
    number(&n);
    for (std::uint32_t g = 0; g < n && bad == 0 && i < lines.size(); ++g) {
        LuaSaveFile::Group gr;
        text(&gr.name);
        std::uint32_t count = 0;
        number(&count);
        number(&gr.player);
        for (std::uint32_t k = 0; k < count && bad == 0; ++k) {
            std::uint32_t id = 0;
            number(&id);
            gr.ids.push_back(static_cast<std::uint16_t>(id));
        }
        out.groups.push_back(std::move(gr));
    }
    if (bad == 0) number(&n);
    for (std::uint32_t t = 0; t < n && bad == 0 && i < lines.size(); ++t) {
        LuaSaveFile::Timer tm;
        text(&tm.name);
        text(&tm.code);
        real(&tm.base);
        real(&tm.period);
        number(&tm.repeats);
        number(&tm.fired);
        number(&tm.done);
        out.timers.push_back(std::move(tm));
    }
    if (bad == 0) number(&n);
    for (std::uint32_t k = 0; k < n && bad == 0; ++k) {
        std::string key;
        text(&key);
        out.keys.push_back(std::move(key));
    }
    if (bad != 0) {
        *error = "line " + std::to_string(bad) + " is not what the original's Lua save holds there";
        return std::nullopt;
    }
    return out;
}

ScriptVmPtr ScriptVm_OpenSaved(GameState& s, ByteSpan script, const std::string& name, ScriptHost host,
                               const LuaSaveFile* save) {
    ScriptVmPtr vm = ScriptVm_Start(s, script, name, std::move(host));
    if (save == nullptr) return vm;
    vm->s = &s;
    for (const std::string& line : save->globals) {
        ScriptVm_DoBuffer(*vm, line.data(), line.size(), line.c_str());
        lua_settop(vm->L, 0);
    }
    for (const LuaSaveFile::Group& g : save->groups) {
        GameState::ScriptGroup group;
        group.player = static_cast<std::uint8_t>(g.player);
        if (g.player >= s.players.size()) {
            ScriptVm_Log(*vm, "load: group " + g.name + " names player " + std::to_string(g.player) +
                                  ", who is not in the game; it is kept empty");
        } else {
            std::size_t missing = 0;
            for (const std::uint16_t id : g.ids) {
                if (const Unit* u = Unit_FindById(s, group.player, id)) {
                    group.members[id] = Unit_Ref(group.player, *u);
                } else {
                    ++missing;  // the original stores a null unit, which a later call reads (docs/bugs.md B240)
                }
            }
            if (missing != 0) {
                ScriptVm_Log(*vm, "load: group " + g.name + ": " + std::to_string(missing) +
                                      " of its units are not in the game; left out");
            }
        }
        s.script_groups[g.name] = std::move(group);
    }
    for (const LuaSaveFile::Timer& t : save->timers) {
        s.script_timers[t.name] = GameState::ScriptTimer{t.code, t.base, t.period, t.repeats, t.fired};
    }
    s.script_keys = save->keys;
    vm->s = nullptr;
    ScriptVm_ReShow(s, *vm);
    return vm;
}

}  // namespace antanta::sim
