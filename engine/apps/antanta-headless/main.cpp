// antanta-headless: loads an original save, replays the player's orders of a recording and writes the state as
// trace lines (tools/recorder/FORMAT.md), so tools/recorder/compare.py and tools/trace_subset.py can diff it against
// the recording of the original.
//
// usage: antanta-headless <game dir> <save.svg|map.dat> <out.tsv> [ticks=1] [--orders <trace.tsv.gz>]
//                         [--grid-out <file>] [--adopt <tick>] [--sparse <n>] [--original-rules]
// Writes the H lines, the R line of the load's table fill, then per step the O lines of the orders it applied, the
// T/P/U/B lines after it and a G line (GameState_Hash; engine only).
// --orders: the O lines of a recording, parsed into sim::Command and given to step() before the tick they name
//           (children included). Orders the sim cannot execute yet are counted as unported calls. The recording's
//           D lines (view inputs, recorder version 4) are given to step() the same way.
// --view:   a file of D lines (view inputs, docs/view-dependence.md), gzip or plain text, added to those.
// --view-static <W>x<H>: inferred view input for a recording without D lines, valid while the camera stays at
//           the save's camera cell on a screen of W x H pixels: before every tick, every house the draw list


//           docs/view-dependence.md says when this holds and what it leaves out.
// --grid-out: writes the pass grids after the load in the tools/passgrid.py dump layout (u16 land grid, then
//           u16 large grid, little endian, row-major), for comparing with it. With a map (.dat) instead of a
//           save, only the map part of a new game is built and no ticks run.
// --adopt: test bridge for recordings with production (not ported): before tick <tick>, every unit of the
//           --orders recording's tick - 1 that the engine lacks is added from its U line, the units are put in the
//           recording's list order and RngIndex is set to the recorded one. Printed on stderr.
// --original-rules: the original's balance slips of research 0x10 and 0x11 instead of the owner's fixed rules
//           (GameState::original_rules, docs/bugs.md B13). For replays of the original's recordings only. A new
//           skirmish or mission starts with it (SkirmishSetup::original_rules), so its map setup is the original's.
// --plus:   a new skirmish with Entente Engine Plus (SkirmishSetup::plus, GameState::plus_rules,
//           docs/your-version.md). Not with --original-rules. Our own save keeps the rules it was played with.
// --run-to-end: the measurement runs of docs/plus-balance.md 8: when the end message of your player's loss (or win)
//           pauses the game, the run notes it on stderr, clears the pause and goes on, until the tick count or until
//           no two living players are enemies. Your slot stays idle.
// --stats <file>: measurement lines (docs/plus-balance.md 8) every --stats-every ticks (default 5000), after the
//           first tick and at the end, tab separated, read by tools/plus_stats.py:
//             M tick ms         wall-clock milliseconds per tick since the last M line (the lag)
//             P tick p level nation lost food wood iron gold oil cap alive killed lost_units houses_lost
//               houses_destroyed gold_low   (gold_low: ticks so far with the gold stock below 1)
//             N tick p type=count ...       living units by type
//             H tick p type.level=count ... finished, standing houses by type and upgrade level
//             R tick p sciences military building   researches done: one 0/1 per row (20, 30 and 3 characters;
//                                           building: the Building Academy's rows of Plus F7)
//             K tick p index x5 bought x5   the market: Plus's price index of Food, Wood, Iron, Gold, Oil (1.0 in
//                                           Classic) and the amounts bought so far (res_bought), the same order
//             S tick p ready x5 mined x5    the call-in strikes' ready ticks (Plus, sim/strike.hpp; 0 when never
//                                           called, so a strike called at t shows t + its cooldown) and the
//                                           production income so far (the *Mined doubles) of Food, Wood, Iron, Gold,
//                                           Oil
//             E tick text                   a loss noted by --run-to-end; at the end the players left
// --no-trace: the out file gets only the header lines (long measurement runs; --stats gives the numbers).
// --sparse: U and B lines only on every n-th tick, the first and the last (as the recorder's sparse mode; the T
//           line keeps its unit count and CRC, so every tick still shows a unit change). For long runs.
// --new-skirmish: the second argument is a map (Maps/<name>.dat) and a new skirmish starts on it from the setup
//           screen's choices (sim/skirmish.hpp) instead of loading a save; two R lines (InitGame's and
//           RandomMap_Setup's fills). The choices, as the recorder's "H skirmish" line writes them:
//             --players <2..8>         the player count (default 2)
//             --resources <0|1|2>      little, average, much (default 1)
//             --slots <l/n/c/t,...>    per slot from slot 0: level (0 No, 1 Easy, 2 Medium, 3 Difficult, 4 you),
//                                      nation (0 Russia, 1 England, 2 France, 3 Germany, 4 Austria, 5 Random),
//                                      colour 0..7, team 0..8; unlisted slots keep the screen's defaults (slot 0
//                                      4/0/0/0, slot 1 1/1/1/0, the rest closed)
//             --seed <n>               the seed of both table fills (default 12345, the recorder's)
//           Or a map code (docs/map-generator-plan.md, v1-N-4-W-3F9A2C71) in place of the map: the map is
//           generated (sim/mapgen.hpp) and its hash noted on stderr.
// --mapgen-report <code> (game dir only): builds the map, prints the report of plan 5.12 and the engine's own check
//           (MapGen_Verify); with --mapgen-out <file.ppm> also a plan picture (zone rings, water, kit objects).
// --mapgen-sweep <first seed (hex)> <count> <settings>: builds and checks count codes per setting ("land": both
//           sizes, 2 to 8 players, summer and winter; or a list like N-4-0,L-8-W), one TSV line each to stdout or
//           --mapgen-out, a summary on stderr (validity, attempts, first failures, build time p50/p99).
//           Your advisor switches come from Data/GameInf.txt. Without --orders and --view every house counts as
//           drawn from the moment it exists (docs/view-dependence.md, the owner's answer 7).
// --new-mission: the second argument is a mission (.mis, or the setup part of a .mit without its Lua script) and the
//           game starts from it as the original's mission start does (sim/mission.hpp, docs/missions.md 2): the map
//           of its header from <game dir>/Maps, one R line (InitGame's fill; --seed as above). The mission path of
//           the state is the file's path relative to the game dir (backslashes) when it lies inside it. Houses count
//           as drawn as for --new-skirmish.
// Our own saves (sim/ownsave.hpp, docs/save-own.md): the second argument may be one of our save files instead of an
//           .svg; the run goes on from it (its house depth maps are loaded again from the game files).
// Lua missions (slice M6, script/lua_save.hpp): our own save of one opens the mission's script again with the globals
//           of its LUA chunk; an .svg that names a Lua mission opens its script as the original's load does, with the
//           globals, groups, timers and keys of the .lua file beside the .svg when there is one. --save-load-at takes
//           the script along (its globals must dump to the same lines after the load).
// --save-load-at <t>[,<t>...]: the round trip of our own save (the gate's save-load step): whenever the state has
//           reached tick t (also the first tick, before any step; "+n" means n ticks after the first one), it is
//           written to <--save-dir>/roundtrip-<t>.sav (through the atomic write), read back from the disk into a new
//           state and the run goes on with that one. The file read back must be the bytes written, writing the loaded
//           state again must give the same bytes, and the loaded state's RngTable must be the one it had. Any
//           difference fails the run (exit 3). The trace must then equal the run without --save-load-at byte for
//           byte (the gate compares them).
// --save-dir <dir>: where --save-load-at writes (default: the folder of out.tsv).
// --net-log: the second argument is a network game server's order log (antanta-server --log-dir, net/lockstep.hpp):
//           the game starts from its setup line on Maps/<its map> or the generated map of its code (slice G8; a
//           resumed game, slice MP9: from the save of its R line) and runs to tick <ticks> with the log's orders, every
//           house drawn, writing the network game's trace (the tick lines only, MyPlayer as "-", a G line with
//           GameState_NetHash) that every player of that game wrote (apps/common/net_game.hpp).
// --mission-state <file>: after each tick in which a player with mission event blocks had its check (tick % 50 ==
//           player), a line "<tick> p <p> stage <s> lost <l> result <r> blocks <i>:<done>/<countdown>/<open>/<timer>
//           ..." as the recorder's `S mstate` lines (scenario line `mission_state on`; tools/mission_state_compare.py).
// stderr gets a summary: orders given, the final state hash, and GameState::unported (calls into code not ported
// yet) as address, count and first tick.
#include <zlib.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "apps/common/net_game.hpp"
#include "apps/common/view_input.hpp"
#include "formats/campaign.hpp"
#include "formats/gameinf.hpp"
#include "formats/map.hpp"
#include "formats/menu.hpp"
#include "formats/params.hpp"
#include "formats/svg.hpp"
#include "net/lockstep.hpp"
#include "platform/files.hpp"
#include "script/lua_manager.hpp"
#include "script/lua_save.hpp"
#include "sim/mapgen.hpp"
#include "sim/mapgen_water.hpp"
#include "sim/mission.hpp"
#include "sim/movement.hpp"
#include "sim/ownsave.hpp"
#include "sim/skirmish.hpp"
#include "sim/state.hpp"
#include "sim/trace.hpp"

using namespace antanta;

namespace {

int fail(const std::string& msg) {
    std::fprintf(stderr, "antanta-headless: %s\n", msg.c_str());
    return 1;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = s.find(sep, start);
        out.push_back(s.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos) return out;
        start = end + 1;
    }
}

// Reads the O lines of a gzip trace line by line (recordings are large; never loaded whole) as Commands by tick.
bool read_orders(const std::string& path, std::map<std::uint32_t, std::vector<sim::Command>>* out, std::string* error) {
    gzFile f = gzopen(path.c_str(), "rb");
    if (f == nullptr) {
        *error = "cannot open " + path;
        return false;
    }
    std::vector<char> buf(1 << 16);
    std::string line;
    while (gzgets(f, buf.data(), static_cast<int>(buf.size())) != nullptr) {
        line += buf.data();
        if (line.empty() || line.back() != '\n') continue;
        line.pop_back();
        if (!line.empty() && line.back() == '\r') line.pop_back();  // a scenario file checked out with CRLF
        if (line.size() > 2 && (line[0] == 'O' || line[0] == 'D') && line[1] == '\t') {
            auto c = line[0] == 'O' ? sim::Command_Parse(line, error) : sim::Command_ParseView(line, error);
            if (!c) {
                gzclose(f);
                return false;
            }
            (*out)[c->tick].push_back(std::move(*c));
        }
        line.clear();
    }
    gzclose(f);
    return true;
}

// --view-static: the camera cell box of the save's camera cell (chunk 1 "GameMap") on a screen of "<W>x<H>".
bool view_box(const std::string& size, const SvgSave& save, ViewBox* box, std::string* error) {
    const std::size_t x = size.find('x');
    const int w = x == std::string::npos ? 0 : std::atoi(size.substr(0, x).c_str());
    const int hgt = x == std::string::npos ? 0 : std::atoi(size.substr(x + 1).c_str());
    if (!view_box_for(w, hgt, static_cast<std::int16_t>(save.view_x), static_cast<std::int16_t>(save.view_y), box)) {
        *error = "--view-static: not a screen size of the game (1024x768, 1152x864, 1280x960, 1280x1024, 1600x1200)";
        return false;
    }
    return true;
}

// Loads the depth maps the view inputs name (apps/common/view_input.hpp); missing ones are reported on stderr.
void load_house_depth(const std::string& game, const std::vector<BasicParams>& params,
                      const std::map<std::uint32_t, std::vector<sim::Command>>& commands, sim::GameState& s) {
    const FileLoader load = [&game](const std::string& path, std::string* error) {
        return load_file(game + "/" + path, error);
    };
    std::vector<std::string> warnings;
    for (const auto& [tick, list] : commands) load_house_depth(load, params, list, s, &warnings);
    for (const std::string& w : warnings) std::fprintf(stderr, "antanta-headless: %s\n", w.c_str());
}

std::uint32_t hex32(const std::string& s) { return static_cast<std::uint32_t>(std::strtoul(s.c_str(), nullptr, 16)); }
float f32_bits(const std::string& s) { return std::bit_cast<float>(hex32(s)); }

// --adopt: units, list order and RngIndex of the recording at tick - 1 (U columns per tools/recorder/FORMAT.md).
bool adopt(const std::string& path, std::uint32_t tick, sim::GameState& s, std::string* error) {
    gzFile f = gzopen(path.c_str(), "rb");
    if (f == nullptr) {
        *error = "cannot open " + path;
        return false;
    }
    std::vector<char> buf(1 << 16);
    std::string line;
    std::vector<std::vector<std::string>> units;
    long long rng = -1;
    while (gzgets(f, buf.data(), static_cast<int>(buf.size())) != nullptr) {
        line += buf.data();
        if (line.empty() || line.back() != '\n') continue;
        line.pop_back();
        const std::vector<std::string> v = split(line, '\t');
        line.clear();
        if (v.size() < 2 || (v[0] != "T" && v[0] != "U")) continue;
        const auto t = static_cast<std::uint32_t>(std::strtoul(v[1].c_str(), nullptr, 10));
        if (t >= tick) break;
        if (t + 1 != tick) continue;
        if (v[0] == "T" && v.size() > 4) rng = std::atoll(v[4].c_str());
        if (v[0] == "U" && v.size() >= 31) units.push_back(v);
    }
    gzclose(f);
    if (rng < 0) {
        *error = "--adopt: tick " + std::to_string(tick - 1) + " not in the recording";
        return false;
    }
    int added = 0;
    std::map<std::pair<int, std::uint32_t>, int> rank;
    for (const auto& v : units) {
        const auto p = static_cast<std::uint8_t>(std::atoi(v[2].c_str()));
        const auto serial = static_cast<std::uint32_t>(std::strtoul(v[4].c_str(), nullptr, 10));
        rank[{p, serial}] = static_cast<int>(rank.size());
        if (p >= s.players.size()) continue;
        bool have = false;
        for (std::uint32_t k : sim::Player_UnitSlots(s.players[p]))
            have = have || s.players[p].unit_slots[k].serial == serial;
        if (have) continue;
        sim::Unit u;
        u.id = static_cast<std::uint16_t>(std::atoi(v[3].c_str())), u.serial = serial;
        u.type = static_cast<std::uint16_t>(std::atoi(v[5].c_str()));
        u.weapon_count = static_cast<std::uint8_t>(s.unit_types[s.players[p].nation][u.type].weapons.size());
        u.x = f32_bits(v[7]), u.y = f32_bits(v[9]);
        u.map_x = static_cast<std::uint16_t>(std::atoi(v[10].c_str()));
        u.map_y = static_cast<std::uint16_t>(std::atoi(v[11].c_str()));
        u.hp = f32_bits(v[13]);
        u.orient = u.target_orient = static_cast<std::uint8_t>(std::atoi(v[14].c_str()));
        u.angle = f32_bits(v[16]), u.num_sprite = f32_bits(v[18]);
        u.work = static_cast<std::uint8_t>(std::atoi(v[19].c_str()));
        u.num_work = static_cast<std::uint8_t>(std::atoi(v[20].c_str()));
        u.dest_x = u.final_x = f32_bits(v[22]), u.dest_y = u.final_y = f32_bits(v[24]);
        u.player_unit_attack = static_cast<std::uint8_t>(std::atoi(v[25].c_str()));
        u.attack_unit_id = static_cast<std::uint16_t>(std::atoi(v[26].c_str()));
        u.player_house_attack = static_cast<std::uint8_t>(std::atoi(v[27].c_str()));
        u.in_house = v[28] != "0", u.in_transport = v[29] != "0";
        u.is_deleted = static_cast<std::uint8_t>(std::atoi(v[30].c_str()));
        u.frame_slot = static_cast<std::uint8_t>(serial % 50);
        sim::GameState_AdoptUnit(s, p, u);
        ++added;
    }
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        sim::Player& pl = s.players[p];
        auto key = [&](std::uint32_t k) {
            const auto it = rank.find({static_cast<int>(p), pl.unit_slots[k].serial});
            return it == rank.end() ? 1 << 30 : it->second;
        };
        std::vector<std::uint32_t> order = sim::Player_UnitSlots(pl);
        std::stable_sort(order.begin(), order.end(), [&](std::uint32_t a, std::uint32_t b) { return key(a) < key(b); });
        sim::Player_SetUnitOrder(pl, order);
    }
    std::fprintf(stderr, "antanta-headless: adopt at tick %u: %d units added, RngIndex %u -> %lld\n", tick, added,
                 s.rng_index, rng);
    s.rng_index = static_cast<std::uint16_t>(rng);
    return true;
}

// --slots: "l/n/c/t" per slot, comma separated, into setup->slots from slot 0.
bool parse_slots(const std::string& text, sim::SkirmishSetup* setup, std::string* error) {
    const std::vector<std::string> slots = split(text, ',');
    if (slots.size() > 8) {
        *error = "--slots: more than 8 slots";
        return false;
    }
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const std::vector<std::string> v = split(slots[i], '/');
        if (v.size() != 4) {
            *error = "--slots: '" + slots[i] + "' is not level/nation/colour/team";
            return false;
        }
        sim::SkirmishSlot& sl = setup->slots[i];
        sl.level = static_cast<std::uint8_t>(std::atoi(v[0].c_str()));
        sl.nation = static_cast<std::uint8_t>(std::atoi(v[1].c_str()));
        sl.colour = static_cast<std::uint8_t>(std::atoi(v[2].c_str()));
        sl.team = static_cast<std::uint8_t>(std::atoi(v[3].c_str()));
    }
    return true;
}

// GameData and GameState are about 150 KB each: they live on the heap, and the optional each loader returns stays
// in these helpers' frames, so main's frame stays small (the MSVC Debug stack is 1 MB).
std::unique_ptr<sim::GameData> data_on_heap(std::span<const BasicParams> params, std::string* err) {
    auto d = sim::GameData_FromParams(params, err);
    return d ? std::make_unique<sim::GameData>(std::move(*d)) : nullptr;
}
template <class Load>
std::unique_ptr<sim::GameState> state_on_heap(Load&& load) {
    auto s = load();
    return s ? std::make_unique<sim::GameState>(std::move(*s)) : nullptr;
}

// A Lua mission's script as the runner opened it (slice M6): its text and name, and the game folder for its host.
struct LuaSource {
    Bytes text;
    std::string name;
    std::string game;
    sim::ScriptHost host() const {
        sim::ScriptHost h;
        h.read = [g = game](const std::string& p) {
            std::string e, q = p;
            for (char& c : q) c = c == '\\' ? '/' : c;
            return load_file(g + "/" + q, &e);
        };
        std::string e;
        if (auto tt = load_file(game + "/Data/TextTable_eng.tt", &e)) {
            if (auto parsed = parse_text_table(*tt, &e)) h.texts = std::move(*parsed);
        }
        return h;
    }
};

// --save-load-at: writes the state as our own save, reads it back from the disk into a new state, checks the bytes and
// the table, and replaces `state` with the loaded one. A Lua mission's script goes along (its globals in the LUA
// chunk): it is opened again on the loaded state, and its globals must dump to the same lines.
bool save_load(const std::string& game, const std::vector<BasicParams>& params,
               const std::map<std::uint32_t, std::vector<sim::Command>>& commands, const std::string& dir,
               const std::optional<sim::SkirmishSetup>& setup, std::unique_ptr<sim::GameState>& state,
               sim::ScriptVmPtr& script, const LuaSource& lua, std::string* err) {
    sim::OwnSaveMeta meta;
    meta.info.name = "roundtrip " + std::to_string(state->tick);
    meta.setup = setup;
    if (script) meta.lua = sim::ScriptVm_DumpGlobals(*script);
    const Bytes written = sim::OwnSave_Write(*state, meta);
    if (written.empty()) {
        *err = "the save could not be packed";
        return false;
    }
    const std::string path = dir + "/roundtrip-" + std::to_string(state->tick) + ".sav";
    if (!save_file_atomic(path, written, err)) return false;
    auto file = load_file(path, err);
    if (!file) return false;
    if (*file != written) {
        *err = path + ": the file read back is not what was written";
        return false;
    }
    auto loaded = std::make_unique<sim::GameState>();
    sim::OwnSaveMeta back;
    if (!sim::OwnSave_Read(*file, loaded.get(), &back, err)) {
        *err = path + ": " + *err;
        return false;
    }
    if (loaded->rng_table != state->rng_table) {
        *err = path + ": RngTable filled again from the seed differs from the saved state's";
        return false;
    }
    if (sim::OwnSave_Write(*loaded, back) != written) {
        *err = path + ": saving the loaded state again does not give the same bytes";
        return false;
    }
    const FileLoader load = [&game](const std::string& p, std::string* e) { return load_file(game + "/" + p, e); };
    std::vector<std::string> warnings;
    load_saved_depth(load, params, *loaded, &warnings);
    for (const auto& [tick, list] : commands) load_house_depth(load, params, list, *loaded, &warnings);
    for (const std::string& w : warnings) std::fprintf(stderr, "antanta-headless: %s\n", w.c_str());
    if (script) {
        if (!back.lua) {
            *err = path + ": the Lua globals did not come back";
            return false;
        }
        sim::ScriptVmPtr vm = sim::ScriptVm_Reload(*loaded, lua.text, lua.name, lua.host(), back.lua);
        if (sim::ScriptVm_DumpGlobals(*vm) != *meta.lua) {
            *err = path + ": the Lua globals opened again do not dump to the lines saved";
            return false;
        }
        script = std::move(vm);
    }
    std::fprintf(stderr, "antanta-headless: saved and loaded at tick %u: %zu bytes, written again the same\n",
                 loaded->tick, written.size());
    state = std::move(loaded);
    return true;
}
// --net-log: a network game's order log replayed on a new skirmish from its setup.
int replay_net_log(const std::string& game, const std::string& log_path, const std::string& out_path, int ticks,
                   const std::vector<BasicParams>& params, const sim::GameData& data) {
    std::string err;
    auto log = load_file(log_path, &err);
    if (!log) return fail(err);
    sim::SkirmishSetup setup;
    std::vector<std::pair<std::uint32_t, std::vector<net::OrderGroup>>> frames;
    std::string resume;
    if (!net::NetLog_Read(std::string(log->begin(), log->end()), &setup, &frames, &err, &resume))
        return fail(log_path + ": " + err);
    std::optional<MapData> map;
    if (const auto gen = sim::MapGen_ParseCode(setup.map, nullptr)) {  // slice G8: a generated map, built from its code
        map = sim::MapGen_Build(*gen, &err);
    } else {
        auto mapfile = load_file(game + "/Maps/" + setup.map, &err);
        if (!mapfile) return fail(err);
        map = parse_map(*mapfile, &err);
    }
    if (!map) return fail(setup.map + ": " + err);
    std::optional<AiTemplate> tpl;
    if (auto t = load_file(game + "/Data/Template4.tpl", &err)) {
        tpl = parse_ai_template(*t, &err);
        if (!tpl) return fail("Template4.tpl: " + err);
    }
    const FileLoader load = [&game](const std::string& p, std::string* e) { return load_file(game + "/" + p, e); };
    std::vector<std::string> warnings;
    std::unique_ptr<sim::GameState> state;
    if (resume.empty()) {
        state =
            state_on_heap([&] { return sim::GameState_NewSkirmish(setup, *map, data, tpl ? &*tpl : nullptr, &err); });
    } else {  // slice MP9: a resumed game starts from the save of its R line (ticks: the tick to run to)
        state = std::make_unique<sim::GameState>();
        sim::OwnSaveMeta meta;
        const Bytes file(resume.begin(), resume.end());
        if (!sim::OwnSave_Read(file, state.get(), &meta, &err)) state.reset();
        if (state) load_saved_depth(load, params, *state, &warnings);
    }
    if (!state) return fail(log_path + ": " + err);
    std::string text;
    std::size_t k = 0;
    while (state->tick < static_cast<std::uint32_t>(ticks)) {
        std::vector<net::OrderGroup> frame;
        while (k < frames.size() && frames[k].first <= state->tick) ++k;  // ticks before the run's start
        if (k < frames.size() && frames[k].first == state->tick + 1) frame = std::move(frames[k++].second);
        text += NetGame_Tick(*state, frame, load, params, &warnings, true);
    }
    for (const std::string& w : warnings) std::fprintf(stderr, "antanta-headless: %s\n", w.c_str());
    std::FILE* out = std::fopen(out_path.c_str(), "wb");
    if (!out) return fail("cannot write " + out_path);
    const bool ok = std::fwrite(text.data(), 1, text.size(), out) == text.size();
    std::fprintf(stderr, "antanta-headless: network game log, %d ticks, net hash %016llx\n", ticks,
                 static_cast<unsigned long long>(sim::GameState_NetHash(*state)));
    return std::fclose(out) == 0 && ok ? 0 : fail("write error on " + out_path);
}

// --stats: the measurement lines of one sample (see the top of this file).
std::string stats_lines(const sim::GameState& s, double ms_per_tick, const std::vector<std::uint32_t>& gold_low) {
    std::string out;
    char buf[512];
    std::snprintf(buf, sizeof buf, "M\t%u\t%.3f\n", s.tick, ms_per_tick);
    out += buf;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        const sim::Player& pl = s.players[p];
        std::snprintf(buf, sizeof buf,
                      "P\t%u\t%zu\t%u\t%u\t%u\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\t%u\t%d\t%d\t%d\t%d\t%d\t%u\n", s.tick, p,
                      unsigned{pl.ai_level}, unsigned{pl.nation}, unsigned{pl.is_lost}, pl.food, pl.wood, pl.iron,
                      pl.gold, pl.oil, unsigned{pl.unit_cap}, pl.units_alive, pl.units_killed, pl.units_lost,
                      pl.houses_lost, pl.houses_destroyed, gold_low[p]);
        out += buf;
        out += "N\t" + std::to_string(s.tick) + "\t" + std::to_string(p);
        for (std::size_t t = 0; t < pl.unit_count.size(); ++t) {
            if (pl.unit_count[t] != 0) out += "\t" + std::to_string(t) + "=" + std::to_string(pl.unit_count[t]);
        }
        out += "\n";
        std::map<std::pair<int, int>, int> houses;
        for (const sim::House& h : pl.houses) {
            if (h.is_destroyed == 0 && h.field_020 == 0 && h.hp > 0.0f) ++houses[{h.type, h.field_065}];
        }
        out += "H\t" + std::to_string(s.tick) + "\t" + std::to_string(p);
        for (const auto& [k, n] : houses) {
            out += "\t" + std::to_string(k.first) + "." + std::to_string(k.second) + "=" + std::to_string(n);
        }
        out += "\nR\t" + std::to_string(s.tick) + "\t" + std::to_string(p) + "\t";
        for (std::uint8_t v : pl.research_9b3950) out += v != 0 ? '1' : '0';
        out += '\t';
        for (std::uint8_t v : pl.research_9b3770) out += v != 0 ? '1' : '0';
        out += '\t';
        for (int row = 0; row < 3; ++row) out += pl.plus_build_level > row ? '1' : '0';  // Plus F7
        out += '\n';
        std::snprintf(buf, sizeof buf, "K\t%u\t%zu\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\n",
                      s.tick, p, pl.market_index[0], pl.market_index[1], pl.market_index[2], pl.market_index[3],
                      pl.market_index[4], pl.res_bought[0], pl.res_bought[1], pl.res_bought[2], pl.res_bought[3],
                      pl.res_bought[4]);
        out += buf;
        std::snprintf(buf, sizeof buf, "S\t%u\t%zu\t%u\t%u\t%u\t%u\t%u\t%.0f\t%.0f\t%.0f\t%.0f\t%.0f\n", s.tick, p,
                      pl.plus_strike_ready[0], pl.plus_strike_ready[1], pl.plus_strike_ready[2],
                      pl.plus_strike_ready[3], pl.plus_strike_ready[4], pl.mined[1], pl.mined[2], pl.mined[3],
                      pl.mined[0], pl.mined[4]);
        out += buf;
    }
    return out;
}

// --run-to-end: true while two living players are enemies (the game goes on).
bool enemies_left(const sim::GameState& s) {
    for (std::size_t a = 0; a < s.players.size(); ++a) {
        for (std::size_t b = a + 1; b < s.players.size(); ++b) {
            if (s.players[a].is_lost == 0 && s.players[b].is_lost == 0 &&
                sim::IsEnemy(s, static_cast<std::uint8_t>(a), static_cast<std::uint8_t>(b))) {
                return true;
            }
        }
    }
    return false;
}

// --mapgen-report and --mapgen-sweep (docs/map-generator-plan.md 5.12, 7.3): build maps from codes, check them at
// both levels (the generator's own rules decide, the engine's code verifies) and print the report.
int mapgen_mode(const std::string& game, const sim::GameData& data, const std::string& report_code,
                const std::vector<std::string>& sweep, const std::string& out_path) {
    std::string err;
    std::optional<AiTemplate> tpl;
    if (auto t = load_file(game + "/Data/Template4.tpl", &err)) tpl = parse_ai_template(*t, &err);
    auto run = [&](const sim::MapGenSettings& g, sim::MapGenReport* r, double* ms, std::string* verify) {
        const auto t0 = std::chrono::steady_clock::now();
        auto map = sim::MapGen_Build(g, &err, r);
        *ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!map) return false;
        sim::MapGen_LookNumbers(*map, r);
        *verify = sim::MapGen_Verify(*map, g, r->layout, data, tpl ? &*tpl : nullptr);
        return true;
    };
    if (!report_code.empty()) {
        const auto g = sim::MapGen_ParseCode(report_code, &err);
        if (!g) return fail(report_code + ": " + err);
        sim::MapGenReport r;
        double ms = 0;
        std::string verify;
        if (!run(*g, &r, &ms, &verify)) {
            for (std::size_t k = 0; k < r.failed.size(); ++k)
                std::printf("attempt %zu: %s\n", k + 1, r.failed[k].c_str());
            return fail(report_code + ": " + err);
        }
        // The checks alone, timed once more on the map kept.
        auto map = sim::MapGen_Build(*g, &err, nullptr);
        const auto t0 = std::chrono::steady_clock::now();
        sim::MapGen_Check(*map, *g, r.layout, nullptr);
        const double check_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("%sengine check: %s\n", sim::MapGen_ReportText(*g, r, ms, check_ms).c_str(),
                    verify.empty() ? "passed" : verify.c_str());
        // The water numbers (slice G6) and the port sites (cells), for the fairness report.
        std::string sites;
        for (std::uint32_t v : map->port_sites)
            sites += " " + std::to_string(v & 0xffff) + "," + std::to_string(v >> 16);
        if (!sites.empty()) sites = "port sites (cells):" + sites + "\n";
        std::printf("%s%s", sim::MapGen_WaterStatsText(sim::MapGen_WaterStats(*map, r.layout)).c_str(), sites.c_str());
        if (!out_path.empty()) {
            // --mapgen-out with a report: a plan picture (binary PPM, 1 pixel = 32 world pixels, the map's own
            // shape): the zones (a thin ring at their radius; the ground there is no paler in the game), the
            // territories' borders, water, trees, stones,
            // deposits (gold yellow, iron rust, oil black) with a frame per free group (own white, contested
            // magenta, middle cyan) and the starts. Shows the generator's objects only, no game graphics.
            const int pw = map->width * 3, ph = map->height * 3 / 4;
            std::vector<std::uint8_t> px(static_cast<std::size_t>(pw) * ph * 3);
            auto put = [&](int x, int y, std::uint8_t cr, std::uint8_t cg, std::uint8_t cb) {
                if (x < 0 || y < 0 || x >= pw || y >= ph) return;
                std::uint8_t* q = &px[(static_cast<std::size_t>(y) * pw + x) * 3];
                q[0] = cr, q[1] = cg, q[2] = cb;
            };
            for (int y = 0; y < ph; ++y) {
                for (int x = 0; x < pw; ++x) {
                    const long wx = x * 32 + 16, wy = y * 32 + 16;
                    std::uint8_t c[3] = {196, 200, 160};
                    for (const sim::MapGenZone& z : r.layout.zones) {
                        const long d2 = (wx - z.x) * (wx - z.x) + (wy - z.y) * (wy - z.y);
                        if (d2 > 1250L * 1250 && d2 <= 1300L * 1300) c[0] = 150, c[1] = 150, c[2] = 120;
                    }
                    put(x, y, c[0], c[1], c[2]);
                }
            }
            const std::vector<std::uint8_t> water = sim::mapgen::WaterCells(*map);
            for (int cy = 0; cy < map->height; ++cy) {
                for (int cx = 0; cx < map->width; ++cx) {
                    const std::size_t i = static_cast<std::size_t>(cy) * map->width + cx;
                    const int x = (96 * cx + 48 * (cy & 1)) / 32, y = 24 * cy / 32;
                    const std::uint16_t o = map->cells[i].object;
                    if (water[i] != 0) put(x, y, 90, 120, 200);
                    if (!r.layout.territory.empty() && cx + 1 < map->width && cy + 2 < map->height &&
                        (r.layout.territory[i] != r.layout.territory[i + 1] ||
                         r.layout.territory[i] != r.layout.territory[i + 2 * static_cast<std::size_t>(map->width)]))
                        put(x, y, 110, 110, 90);
                    if (o >= 9000 && o < 9500) put(x, y, 30, 100, 40);
                    if (o >= 9500 && o < 9600) put(x, y, 120, 120, 120);
                }
            }
            for (int cy = 0; cy < map->height; ++cy) {
                for (int cx = 0; cx < map->width; ++cx) {
                    const std::uint16_t o = map->cells[static_cast<std::size_t>(cy) * map->width + cx].object;
                    if (o < 0x2581 || o > 0x2583) continue;
                    const int x = (96 * cx + 48 * (cy & 1)) / 32, y = 24 * cy / 32;
                    for (int dy = -2; dy <= 2; ++dy) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            if (o == 0x2582) put(x + dx, y + dy, 240, 200, 0);
                            if (o == 0x2583) put(x + dx, y + dy, 170, 70, 30);
                            if (o == 0x2581) put(x + dx, y + dy, 0, 0, 0);
                        }
                    }
                }
            }
            for (const sim::MapGenGroup& gr : r.layout.groups) {  // a frame around each free group's deposits
                const std::uint8_t cr = gr.ring == 3 ? 0 : 255, cg = gr.ring == 2 ? 0 : 255, cb = 255;
                for (const auto& cell : gr.cells) {
                    const int x = (96 * cell[0] + 48 * (cell[1] & 1)) / 32, y = 24 * cell[1] / 32;
                    for (int k = -4; k <= 4; ++k) {
                        put(x + k, y - 4, cr, cg, cb), put(x + k, y + 4, cr, cg, cb);
                        put(x - 4, y + k, cr, cg, cb), put(x + 4, y + k, cr, cg, cb);
                    }
                }
            }
            for (const sim::MapGenZone& z : r.layout.zones) {
                for (int d = -4; d <= 4; ++d)
                    put(z.x / 32 + d, z.y / 32, 200, 0, 0), put(z.x / 32, z.y / 32 + d, 200, 0, 0);
                for (const auto& s : z.sawmills) {  // the sawmill sites: a small blue square
                    const int x = (96 * s[0] + 48 * (s[1] & 1)) / 32, y = 24 * s[1] / 32;
                    for (int k = -2; k <= 2; ++k) {
                        put(x + k, y - 2, 0, 60, 200), put(x + k, y + 2, 0, 60, 200);
                        put(x - 2, y + k, 0, 60, 200), put(x + 2, y + k, 0, 60, 200);
                    }
                }
            }
            std::FILE* f = std::fopen(out_path.c_str(), "wb");
            if (f == nullptr) return fail("cannot write " + out_path);
            std::fprintf(f, "P6\n%d %d\n255\n", pw, ph);
            std::fwrite(px.data(), 1, px.size(), f);
            std::fclose(f);
        }
        return verify.empty() ? 0 : 1;
    }
    // <first seed> <count> <settings>: settings "land" (both sizes, 2 to 8 players, summer and winter) or a comma list
    // of size-players-switches ("N-4-0,L-8-W").
    const auto first = static_cast<std::uint32_t>(std::strtoul(sweep[0].c_str(), nullptr, 16));
    const int count = std::atoi(sweep[1].c_str());
    std::vector<std::string> sets;
    if (sweep[2] == "land") {
        for (const char* size : {"N", "L"}) {
            for (int p = 2; p <= 8; ++p) {
                for (const char* sw : {"0", "W"})
                    sets.push_back(std::string(size) + "-" + std::to_string(p) + "-" + sw);
            }
        }
    } else {
        sets = split(sweep[2], ',');
    }
    std::FILE* out = out_path.empty() ? stdout : std::fopen(out_path.c_str(), "wb");
    if (out == nullptr) return fail("cannot write " + out_path);
    std::fputs(sim::MapGen_ReportTsvHead().c_str(), out);
    int maps = 0, first_ok = 0, invalid = 0, verify_bad = 0;
    std::map<int, int> attempts;
    std::map<std::string, int> first_fail;
    std::vector<double> times;
    for (const std::string& set : sets) {
        for (int k = 0; k < count; ++k) {
            char code[64];
            std::snprintf(code, sizeof code, "v1-%s-%08X", set.c_str(), first + static_cast<std::uint32_t>(k));
            const auto g = sim::MapGen_ParseCode(code, &err);
            if (!g) return fail(std::string(code) + ": " + err);
            sim::MapGenReport r;
            double ms = 0;
            std::string verify;
            ++maps;
            if (!run(*g, &r, &ms, &verify)) {
                ++invalid;
                std::fprintf(out, "%s\tinvalid\t%s\n", code, err.c_str());
                continue;
            }
            times.push_back(ms);
            ++attempts[r.attempt + 1];
            if (r.attempt == 0) ++first_ok;
            if (!r.failed.empty()) {
                const std::string& f = r.failed[0];
                ++first_fail[f.substr(0, f.find_first_of(":0123456789"))];
            }
            if (!verify.empty()) ++verify_bad;
            std::fputs(sim::MapGen_ReportTsv(*g, r, ms, verify).c_str(), out);
            std::fflush(out);
        }
    }
    if (out != stdout) std::fclose(out);
    std::sort(times.begin(), times.end());
    auto pct = [&](int q) { return times.empty() ? 0.0 : times[std::min(times.size() - 1, times.size() * q / 100)]; };
    std::fprintf(stderr, "mapgen sweep: %d maps, valid %d (%.1f %%), first attempt %.1f %%, engine check failed %d\n",
                 maps, maps - invalid, 100.0 * (maps - invalid) / std::max(1, maps),
                 100.0 * first_ok / std::max(1, maps), verify_bad);
    std::fprintf(stderr, "attempts:");
    for (const auto& [a, c] : attempts) std::fprintf(stderr, " %d:%d", a, c);
    std::fprintf(stderr, "\nfirst failure by check:");
    for (const auto& [f, c] : first_fail) std::fprintf(stderr, " [%s] %d", f.c_str(), c);
    std::fprintf(stderr, "\nbuild with checks: p50 %.1f ms, p99 %.1f ms, max %.1f ms\n", pct(50), pct(99),
                 times.empty() ? 0.0 : times.back());
    return invalid == 0 && verify_bad == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> pos;
    std::string orders_path, grid_out, view_path, view_size;
    std::uint32_t adopt_tick = 0;
    int sparse = 1;
    std::vector<std::uint32_t> save_load_at, save_load_after;  // ticks; "+n": n ticks after the first one
    std::string save_dir;
    bool new_skirmish = false, new_mission = false, original_rules = false, net_log = false;
    bool plus = false, run_to_end = false, no_trace = false;
    std::string stats_path;
    std::uint32_t stats_every = 5000;
    std::string mission_state_path;
    std::string mapgen_report, mapgen_out;  // --mapgen-report <code>, --mapgen-out <file>
    std::vector<std::string> mapgen_sweep;  // --mapgen-sweep <first seed> <count> <settings>
    sim::SkirmishSetup setup;
    std::string slots_text;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--original-rules") {
            original_rules = true;
        } else if (a == "--plus") {
            plus = true;
        } else if (a == "--run-to-end") {
            run_to_end = true;
        } else if (a == "--no-trace") {
            no_trace = true;
        } else if (a == "--stats" && i + 1 < argc) {
            stats_path = argv[++i];
        } else if (a == "--stats-every" && i + 1 < argc) {
            stats_every = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10)));
        } else if (a == "--mapgen-report" && i + 1 < argc) {
            mapgen_report = argv[++i];
        } else if (a == "--mapgen-out" && i + 1 < argc) {
            mapgen_out = argv[++i];
        } else if (a == "--mapgen-sweep" && i + 3 < argc) {
            mapgen_sweep = {argv[i + 1], argv[i + 2], argv[i + 3]};
            i += 3;
        } else if (a == "--new-skirmish") {
            new_skirmish = true;
        } else if (a == "--new-mission") {
            new_mission = true;
        } else if (a == "--net-log") {
            net_log = true;
        } else if (a == "--players" && i + 1 < argc) {
            setup.players = static_cast<std::uint8_t>(std::atoi(argv[++i]));
        } else if (a == "--resources" && i + 1 < argc) {
            setup.resources = static_cast<std::uint8_t>(std::atoi(argv[++i]));
        } else if (a == "--slots" && i + 1 < argc) {
            slots_text = argv[++i];
        } else if (a == "--seed" && i + 1 < argc) {
            setup.seed = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (a == "--mission-state" && i + 1 < argc) {
            mission_state_path = argv[++i];
        } else if ((a == "--orders" || a == "--grid-out") && i + 1 < argc) {
            (a == "--orders" ? orders_path : grid_out) = argv[++i];
        } else if ((a == "--view" || a == "--view-static") && i + 1 < argc) {
            (a == "--view" ? view_path : view_size) = argv[++i];
        } else if (a == "--adopt" && i + 1 < argc) {
            adopt_tick = static_cast<std::uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        } else if (a == "--save-load-at" && i + 1 < argc) {
            for (const std::string& t : split(argv[++i], ',')) {
                const bool after = !t.empty() && t[0] == '+';
                const auto n = static_cast<std::uint32_t>(std::strtoul(t.c_str() + (after ? 1 : 0), nullptr, 10));
                (after ? save_load_after : save_load_at).push_back(n);
            }
        } else if (a == "--save-dir" && i + 1 < argc) {
            save_dir = argv[++i];
        } else if (a == "--sparse" && i + 1 < argc) {
            sparse = std::max(1, std::atoi(argv[++i]));
        } else {
            pos.push_back(a);
        }
    }
    const bool mapgen = !mapgen_report.empty() || !mapgen_sweep.empty();
    if (mapgen ? pos.size() != 1 : pos.size() < 3 || pos.size() > 4) {
        std::fprintf(stderr,
                     "usage: antanta-headless <game dir> <save.svg|map.dat|map code> <out.tsv> [ticks=1] "
                     "[--orders <trace.tsv.gz>] [--grid-out <file>] [--adopt <tick>] [--view <file>] "
                     "[--view-static <W>x<H>] [--new-skirmish [--players <n>] [--resources <0..2>] "
                     "[--slots <l/n/c/t,...>] [--seed <n>]] [--new-mission [--seed <n>]] [--original-rules] "
                     "[--plus] [--save-load-at <t,...>] [--net-log] [--save-dir <dir>] [--run-to-end] "
                     "[--stats <file> [--stats-every <n>]] [--no-trace]\n"
                     "       antanta-headless <game dir> --mapgen-report <map code> [--mapgen-out <plan.ppm>]\n"
                     "       antanta-headless <game dir> --mapgen-sweep <first seed (hex)> <count> <land|N-4-0,...> "
                     "[--mapgen-out <file.tsv>]\n");
        return 2;
    }
    if (plus && original_rules) return fail("--plus and --original-rules exclude each other");
    setup.plus = plus, setup.original_rules = original_rules;
    const std::string game = pos[0];
    const int ticks = pos.size() == 4 ? std::atoi(pos[3].c_str()) : 1;
    std::string err;

    UnitTypeCounts types{};
    std::vector<BasicParams> params;
    for (int n = 0; n < 6; ++n) {  // 5 nations and the neutral one (houses only)
        auto file = load_file(game + "/Data/BasicParams" + std::to_string(n) + ".dat", &err);
        if (!file) return fail(err);
        auto p = parse_basic_params(*file, &err);
        if (!p) return fail("BasicParams" + std::to_string(n) + ".dat: " + err);
        if (n < 5) types[n] = static_cast<std::uint16_t>(p->units.size());
        params.push_back(std::move(*p));
    }
    const std::unique_ptr<sim::GameData> data = data_on_heap(params, &err);
    if (!data) return fail(err);
    auto objects = load_file(game + "/Data/Objects/objects.les", &err);
    if (!objects) return fail(err);
    if (!sim::GameData_LoadObjects(*data, *objects, &err)) return fail("objects.les: " + err);
    {  // ships: ShipTowersParams.txt and the ship graphics (milestone 3 slice H)
        auto towers = load_file(game + "/Data/ShipTowersParams.txt", &err);
        if (!towers) return fail(err);
        auto read_les = [&game](const std::string& path) {
            std::string e;
            return load_file(game + "/Data/Units/" + path + ".les", &e);
        };
        if (!sim::GameData_LoadShips(*data, params, *towers, read_les, &err)) return fail(err);
    }
    // Compiled-in fallbacks (CreateAppWindow) when GameInf.txt is unreadable: 53 ms, full animations.
    if (auto file = load_file(game + "/Data/GameInf.txt", &err)) {
        if (auto v = parse_game_speed_ms(*file, &err)) data->speed_ms = *v;
        if (auto v = parse_short_anims(*file, &err)) data->short_anims = *v ? 1 : 0;
        if (auto v = parse_gameinf(*file, &err)) setup.economic = v->economic, setup.military = v->military;
    }

    if (auto st = load_file(game + "/Data/StringTable_eng.st", &err)) {
        if (auto table = parse_string_table(*st, &err)) sim::SkirmishSetup_CompNames(&setup, *table);
    }
    if (!slots_text.empty() && !parse_slots(slots_text, &setup, &err)) return fail(err);
    if (mapgen) return mapgen_mode(game, *data, mapgen_report, mapgen_sweep, mapgen_out);
    if (net_log) return replay_net_log(game, pos[1], pos[2], ticks, params, *data);
    // A generated map (docs/map-generator.md): a map code in place of the map file, with --new-skirmish.
    const auto gen = sim::MapGen_ParseCode(pos[1], nullptr);
    auto file = gen ? std::optional<Bytes>(Bytes{}) : load_file(pos[1], &err);
    if (!file) return fail(err);
    if (gen && !new_skirmish) return fail("a map code needs --new-skirmish");
    const bool is_map =
        pos[1].size() > 4 && (pos[1].substr(pos[1].size() - 4) == ".dat" || pos[1].substr(pos[1].size() - 4) == ".DAT");
    std::unique_ptr<sim::GameState> state;
    std::optional<SvgSave> save;
    const bool own_save = sim::OwnSave_IsOwn(*file);
    std::optional<sim::SkirmishSetup> own_setup;  // the setup kept in our own save (written again by --save-load-at)
    std::optional<std::string> own_lua;           // and its Lua globals (a Lua mission, slice M6)
    const std::string ext = pos[1].size() > 4 ? pos[1].substr(pos[1].size() - 4) : std::string();
    const bool is_mission = ext == ".mis" || ext == ".MIS" || ext == ".mit" || ext == ".MIT";
    if (new_mission && (new_skirmish || !is_mission)) return fail("--new-mission needs a mission (.mis or .mit)");
    if (own_save) {
        if (new_skirmish) return fail("--new-skirmish needs a map (.dat)");
        state = std::make_unique<sim::GameState>();
        sim::OwnSaveMeta meta;
        if (!sim::OwnSave_Read(*file, state.get(), &meta, &err)) return fail(pos[1] + ": " + err);
        own_setup = meta.setup;
        own_lua = meta.lua;
        const FileLoader load = [&game](const std::string& p, std::string* e) { return load_file(game + "/" + p, e); };
        std::vector<std::string> warnings;
        load_saved_depth(load, params, *state, &warnings);
        for (const std::string& w : warnings) std::fprintf(stderr, "antanta-headless: %s\n", w.c_str());
    } else if (new_mission) {
        sim::MissionSetup ms;
        // The path as the game would name it: relative to the game dir, with backslashes.
        std::string path = pos[1];
        for (char& c : path) c = c == '/' ? '\\' : c;
        std::string root = game;
        for (char& c : root) c = c == '/' ? '\\' : c;
        if (!root.empty() && root.back() != '\\') root += '\\';
        if (path.size() > root.size() && path.compare(0, root.size(), root) == 0) path = path.substr(root.size());
        if (ext == ".mis" || ext == ".MIS") {
            auto mis = parse_mis(*file, &err);
            if (!mis) return fail(pos[1] + ": " + err);
            ms = sim::MissionSetup_FromMis(*mis, path);
            // The blocks' texts from Data/TextTable_eng.tt (unreadable: each text is its key, as for a missing record).
            TextTable table;
            if (auto tt = load_file(game + "/Data/TextTable_eng.tt", &err)) {
                if (auto parsed = parse_text_table(*tt, &err)) table = std::move(*parsed);
            }
            if (table.empty()) std::fprintf(stderr, "antanta-headless: no Data/TextTable_eng.tt: %s\n", err.c_str());
            sim::MissionSetup_SetTexts(ms, table);
        } else {
            auto mit = parse_mit(*file, &err);
            if (!mit) return fail(pos[1] + ": " + err);
            ms = sim::MissionSetup_FromMit(*mit, path);
        }
        ms.seed = setup.seed, ms.economic = setup.economic, ms.military = setup.military;
        ms.original_rules = original_rules;
        auto mapfile = load_file(game + "/Maps/" + ms.map, &err);
        if (!mapfile) return fail(err);
        auto map = parse_map(*mapfile, &err);
        if (!map) return fail(ms.map + ": " + err);
        std::optional<AiTemplate> tpl;
        if (auto t = load_file(game + "/Data/Template4.tpl", &err)) {
            tpl = parse_ai_template(*t, &err);
            if (!tpl) return fail("Template4.tpl: " + err);
        }
        state = state_on_heap([&] { return sim::GameState_NewMission(ms, *map, *data, tpl ? &*tpl : nullptr, &err); });
    } else if (new_skirmish) {
        if (!is_map && !gen) return fail("--new-skirmish needs a map (.dat) or a map code");
        std::optional<MapData> map;
        if (gen) {
            const auto t0 = std::chrono::steady_clock::now();
            map = sim::MapGen_Build(*gen, &err);
            const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            if (!map) return fail(pos[1] + ": " + err);
            setup.map = sim::MapGen_Code(*gen);  // the map's name is its code (the climate comes from it)
            std::fprintf(stderr, "antanta-headless: generated map %s, %d x %d in %.0f ms, map hash %016llx\n",
                         setup.map.c_str(), map->width, map->height, ms,
                         static_cast<unsigned long long>(sim::MapGen_Hash(*map)));
        } else {
            map = parse_map(*file, &err);
            if (!map) return fail(pos[1] + ": " + err);
            const std::size_t slash = pos[1].find_last_of("/\\");
            setup.map = slash == std::string::npos ? pos[1] : pos[1].substr(slash + 1);
        }

        std::optional<AiTemplate> tpl;
        if (auto t = load_file(game + "/Data/Template4.tpl", &err)) {
            tpl = parse_ai_template(*t, &err);
            if (!tpl) return fail("Template4.tpl: " + err);
        }
        state =
            state_on_heap([&] { return sim::GameState_NewSkirmish(setup, *map, *data, tpl ? &*tpl : nullptr, &err); });
    } else if (is_map) {
        auto map = parse_map(*file, &err);
        if (!map) return fail(pos[1] + ": " + err);
        state = state_on_heap([&] { return sim::GameState_FromMap(*map, *data, &err); });
    } else {
        save = parse_svg(*file, types, &err);
        if (!save) return fail(pos[1] + ": " + err);
        state = state_on_heap([&] { return sim::GameState_FromSave(*save, *data, &err); });
    }
    if (!state) return fail(pos[1] + ": " + err);
    if (!own_save) state->original_rules = original_rules;  // our own save keeps the rules it was played with
    if (plus && !new_skirmish) return fail("--plus needs --new-skirmish (a skirmish rule set)");



    sim::ScriptVmPtr script;
    LuaSource lua_src;
    lua_src.game = game;
    if (new_mission) {
        lua_src.name = sim::Mission_ScriptPath(pos[1]);
        if (auto lua = load_file(lua_src.name, &err)) {
            lua_src.text = std::move(*lua);
            script = sim::ScriptVm_Open(*state, lua_src.text, lua_src.name, lua_src.host());
        }
    } else if ((own_save && state->lua_mission != 0) || (save && state->mission_path.size() > 3)) {
        lua_src.name = sim::Mission_ScriptPath(state->mission_path);
        std::string script_file = lua_src.name;
        for (char& c : script_file) c = c == '\\' ? '/' : c;
        std::string lua_err;
        if (auto lua = load_file(game + "/" + script_file, &lua_err)) {
            lua_src.text = std::move(*lua);
            if (own_save) {
                script = sim::ScriptVm_Reload(*state, lua_src.text, lua_src.name, lua_src.host(), own_lua);
            } else {
                std::optional<sim::LuaSaveFile> saved;
                if (auto bytes = load_file(sim::Mission_ScriptPath(pos[1]), &lua_err)) {
                    saved = sim::parse_lua_save(*bytes, &lua_err);
                    if (!saved) return fail(sim::Mission_ScriptPath(pos[1]) + ": " + lua_err);
                }
                script = sim::ScriptVm_OpenSaved(*state, lua_src.text, lua_src.name, lua_src.host(),
                                                 saved ? &*saved : nullptr);
            }
        } else if (own_save) {
            std::fprintf(stderr, "antanta-headless: the mission's script: %s\n", lua_err.c_str());
        }
    }
    if (!grid_out.empty()) {
        std::string bytes;
        for (const auto* g : {&state->map.pass_land, &state->map.pass_large}) {
            for (std::uint16_t v : *g) bytes += static_cast<char>(v & 0xff), bytes += static_cast<char>(v >> 8);
        }
        std::FILE* f = std::fopen(grid_out.c_str(), "wb");
        if (!f || std::fwrite(bytes.data(), 1, bytes.size(), f) != bytes.size() || std::fclose(f) != 0) {
            return fail("cannot write " + grid_out);
        }
        if (is_map && !new_skirmish) return 0;
    }
    if (is_map && !new_skirmish) return fail("a map gives no trace; use --grid-out or --new-skirmish");
    std::map<std::uint32_t, std::vector<sim::Command>> commands;
    if (!orders_path.empty() && !read_orders(orders_path, &commands, &err)) return fail(err);
    if (!view_path.empty() && !read_orders(view_path, &commands, &err)) return fail(err);
    ViewBox box;
    if (!view_size.empty() && (new_skirmish || new_mission)) return fail("--view-static needs a save's camera");
    if (!view_size.empty() && !view_box(view_size, *save, &box, &err)) return fail(err);
    // A new game, and a game from our own save (it was a new game or played in a window), takes the owner's rule:
    // every house counts as drawn.
    const bool all_drawn = (new_skirmish || new_mission || own_save) && orders_path.empty() && view_path.empty();
    if (own_save && !view_size.empty()) return fail("--view-static needs an .svg's camera");
    if (save_dir.empty()) {
        const std::size_t slash = pos[2].find_last_of("/\\");
        save_dir = slash == std::string::npos ? std::string(".") : pos[2].substr(0, slash);
    }
    const std::optional<sim::SkirmishSetup> keep_setup = new_skirmish ? std::optional(setup) : own_setup;
    load_house_depth(game, params, commands, *state);
    int given = 0, views = 0;

    std::FILE* out = std::fopen(pos[2].c_str(), "wb");
    if (!out) return fail("cannot write " + pos[2]);
    std::FILE* mission_state = nullptr;
    if (!mission_state_path.empty()) {
        mission_state = std::fopen(mission_state_path.c_str(), "wb");
        if (!mission_state) return fail("cannot write " + mission_state_path);
    }
    std::FILE* stats = nullptr;
    if (!stats_path.empty()) {
        stats = std::fopen(stats_path.c_str(), "wb");
        if (!stats) return fail("cannot write " + stats_path);
    }
    std::vector<std::uint32_t> gold_low(state->players.size(), 0);
    auto stats_clock = std::chrono::steady_clock::now();
    std::uint32_t stats_tick = state->tick;
    auto write_stats = [&] {
        const auto now = std::chrono::steady_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(now - stats_clock).count();
        const std::uint32_t n = state->tick - stats_tick;
        const std::string lines = stats_lines(*state, n != 0 ? ms / n : 0.0, gold_low);
        std::fwrite(lines.data(), 1, lines.size(), stats);
        std::fflush(stats);
        stats_clock = now, stats_tick = state->tick;
    };
    auto stats_end = [&](const std::string& what) {
        const std::string e = "E\t" + std::to_string(state->tick) + "\t" + what + "\n";
        std::fwrite(e.data(), 1, e.size(), stats);
    };
    std::string text = sim::trace_header();
    // A new skirmish: InitGame's and RandomMap_Setup's fills (one seed); a mission: InitGame's; a save: the load's.
    if (new_skirmish) text += sim::trace_rng_fill(*state, 0, false);
    text += sim::trace_rng_fill(*state, 0, !new_skirmish && !new_mission);
    for (const std::uint32_t n : save_load_after) save_load_at.push_back(state->tick + n);
    int run = 0;
    for (int i = 0; i < ticks; ++i) {
        if (std::find(save_load_at.begin(), save_load_at.end(), state->tick) != save_load_at.end() &&
            !save_load(game, params, commands, save_dir, keep_setup, state, script, lua_src, &err)) {
            std::fprintf(stderr, "antanta-headless: save and load: %s\n", err.c_str());
            return 3;
        }
        if (state->paused != 0 && run_to_end && enemies_left(*state)) {  // the measurement runs go on
            const std::string what = "end message " + std::to_string(state->end_message) + ", result " +
                                     std::to_string(state->game_result) + ", the run goes on";
            std::fprintf(stderr, "antanta-headless: tick %u: %s\n", state->tick, what.c_str());
            if (stats) stats_end(what);
            state->paused = 0, state->end_message = 0;
        }
        if (run_to_end && !enemies_left(*state)) break;  // one side is left
        if (state->paused != 0) break;                   // the end of the game (a message window): no more ticks
        ++run;
        const std::uint32_t enter = state->tick;
        if (adopt_tick != 0 && enter + 1 == adopt_tick && !adopt(orders_path, adopt_tick, *state, &err))
            return fail(err);
        std::vector<sim::Command> now;
        const auto it = commands.find(enter + 1);
        if (it != commands.end()) now = it->second;
        if (!view_size.empty() || all_drawn) {
            const ViewBox b = all_drawn ? view_box_whole_map(*state) : box;
            std::map<std::uint32_t, std::vector<sim::Command>> inferred{{enter + 1, view_inputs(b, *state)}};
            load_house_depth(game, params, inferred, *state);
            now.insert(now.begin(), inferred.begin()->second.begin(), inferred.begin()->second.end());
        }
        {
            for (const sim::Command& c : now) {
                given += c.parent == 0 && c.kind != sim::Command::Kind::HouseDepth ? 1 : 0;
                views += c.kind == sim::Command::Kind::HouseDepth ? 1 : 0;
            }
        }
        sim::step(*state, script.get(), now);
        if (stats) {
            for (std::size_t p = 0; p < state->players.size() && p < gold_low.size(); ++p) {
                gold_low[p] += state->players[p].gold < 1.0 ? 1 : 0;
            }
            if (i == 0 || state->tick % stats_every == 0) write_stats();
        }
        if (no_trace) continue;
        for (std::size_t p = 0; mission_state && p < state->players.size(); ++p) {
            const sim::Player& pl = state->players[p];
            if (pl.blocks.empty() || state->tick % 50 != p) continue;
            std::fprintf(mission_state, "%u p %zu stage %u lost %u result %u blocks", state->tick, p,
                         unsigned{pl.field_2cc9a}, unsigned{pl.is_lost}, unsigned{state->game_result});
            for (std::size_t k = 0; k < pl.blocks.size(); ++k) {
                const MisBlock& b = pl.blocks[k];
                std::fprintf(mission_state, " %zu:%u/%d/%u/%d", k, unsigned{b.done}, b.countdown,
                             unsigned{b.open_prereq}, b.timer);
            }
            std::fprintf(mission_state, "\n");
        }
        std::string tick_lines = sim::trace_tick(*state, enter);
        if (sparse > 1 && i != 0 && i + 1 != ticks && (i + 1) % sparse != 0) {
            std::string kept;
            for (std::size_t a = 0; a < tick_lines.size();) {
                std::size_t b = tick_lines.find('\n', a);
                b = b == std::string::npos ? tick_lines.size() : b + 1;
                if (tick_lines.compare(a, 2, "U\t") != 0 && tick_lines.compare(a, 2, "B\t") != 0)
                    kept.append(tick_lines, a, b - a);
                a = b;
            }
            tick_lines = std::move(kept);
        }
        text += sim::trace_orders(*state) + sim::trace_view(*state) + sim::trace_commands(*state) + tick_lines +
                sim::trace_state_hash(*state);
        if (text.size() > (1u << 20)) {
            std::fwrite(text.data(), 1, text.size(), out);
            text.clear();
        }
    }
    const bool ok = std::fwrite(text.data(), 1, text.size(), out) == text.size();
    if (stats) {
        if (state->tick != stats_tick) write_stats();
        std::string players;
        for (std::size_t p = 0; p < state->players.size(); ++p) {
            if (state->players[p].is_lost == 0) players += " " + std::to_string(p);
        }
        stats_end("the end, players left:" + players);
        if (std::fclose(stats) != 0) return fail("write error on " + stats_path);
    }
    std::uint64_t calls = 0;
    for (const auto& [address, hit] : state->unported) calls += hit.count;
    std::fprintf(stderr,
                 "antanta-headless: %d ticks, orders given %d, view inputs %d, state hash %016llx, unported calls "
                 "%llu\n",
                 run, given, views, static_cast<unsigned long long>(sim::GameState_Hash(*state)),
                 static_cast<unsigned long long>(calls));
    if (state->paused != 0) {
        std::fprintf(stderr, "antanta-headless: paused after tick %u, end message %u, result %u\n", state->tick,
                     static_cast<unsigned>(state->end_message), static_cast<unsigned>(state->game_result));
    }
    for (const auto& [address, hit] : state->unported) {
        std::fprintf(stderr, "antanta-headless: unported 0x%06x: %u calls, first at tick %u\n", address, hit.count,
                     hit.first_tick);
    }
    for (std::size_t k = 0; script && k < sim::ScriptVm_Errors(*script).size(); ++k) {
        std::fprintf(stderr, "antanta-headless: script: %s\n", sim::ScriptVm_Errors(*script)[k].c_str());
    }
    if (mission_state && std::fclose(mission_state) != 0) return fail("write error on " + mission_state_path);
    return std::fclose(out) == 0 && ok ? 0 : fail("write error on " + pos[2]);
}
