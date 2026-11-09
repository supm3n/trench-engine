



#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sim/mapgen.hpp"
#include "sim/skirmish.hpp"
#include "ui/settings.hpp"

namespace antanta::ui {

// Maps\random.ind: pairs of lines, a player count and a map file name ("8", "Aust_2r_1.dat"). The shipped file tags
// all 11 maps 8.
struct MapEntry {
    int players = 0;
    std::string file;
};
std::vector<MapEntry> parse_random_ind(std::string_view text);

// An addition of ours (2026-10-17, docs/skirmish-start.md): Aust_3r_5, Russ_3r_2 and Russ_3r_4 are 8-player maps the
// game ships but random.ind does not list (the original offers their winter twins wAust_3r_5 and wRuss_3r_2 and
// leaves these out). They are appended at the end of the list, after the original's maps, tagged 8, when the map file
// and its picture can be read through `exists(file)` and the list does not hold them yet. The original's list and its
// order stay as they are; the screen shows pictures only, so there is no name to translate.
void append_extra_maps(std::vector<MapEntry>& maps, const std::function<bool(const std::string& file)>& exists);

// Ours (2026-11-01, docs/decisions.md 46): the Extras folder next to the exe, handed out separately (never in git or a
// release), holds maps a bought copy lacks (the fan 1.2 package's five maps, the intact Fran_2r_2.dat). The app reads
// Maps\<file> from Extras\Maps first, and random.ind as the game's text with a "8", "<file>" pair appended for each of
// `extra` (the Extras .dat files that have a picture) the list does not hold yet, in the given order.
std::string random_ind_with(std::string_view ind, const std::vector<std::string>& extra);

// Deliberately different (docs/bugs.md B5, owner 2026-10-15): random.ind tags newhouses.dat 8 though it has 5
// positions, and 6 to 8 players there crash the original. Each entry's count is lowered to the map's own position count
// (its player records), so the screen offers a map only for as many players as it has positions. `read_map(file)` gives
// the bytes of Maps\<file>; a map that cannot be read keeps its tag. A map whose file is there but does not parse (the
// Steam copy of Fran_2r_2.dat, a broken zlib stream, docs/bugs.md B4) is taken out of the list and its name put in
// `*left_out` for the caller to log, so it cannot be started. Until 2026-10-18 Fran_2r_2 was left out by its name; the
// working copy now holds the intact file from the fan 1.2 package (owner, 2026-10-18), which is listed again.
void limit_to_map_positions(std::vector<MapEntry>& maps,
                            const std::function<std::optional<Bytes>(const std::string& file)>& read_map,
                            std::vector<std::string>* left_out);



const MapEntry* map_at(const std::vector<MapEntry>& maps, int players, int index);

// The index among the maps offered for `players` (map_at) of the map file `file` (its name compared without case: a
// scenario's header may spell it otherwise), or -1 when it is not offered.
int map_index(const std::vector<MapEntry>& maps, int players, std::string_view file);


std::string preview_file(const std::string& map_file);



struct SkirmishScreen {
    std::string name;
    int players = 2;                        // 2..8
    int resources = 1;                      // 0 Too little, 1 Average, 2 Too much
    int map = 0;                            // index among the maps offered for `players`
    std::array<sim::SkirmishSlot, 8> rows;  // level 4 is you (row 0), 0 No, 1 Easy, 2 Medium, 3 Difficult
    bool plus = false;  // ours: the rule set, Entente Engine Plus (SkirmishSetup::plus) or Entente Engine
    // Ours (slice G7, docs/map-generator-plan.md 4): with gen_offered the first entry of the map list is "Generated
    // map" and `map` counts it (index 0); the network host's setup does not offer it until slice G8. `gen` holds its
    // choices; gen.players follows `players`.
    bool gen_offered = false;
    sim::MapGenSettings gen;
    bool gen_teams_dropped = false;  // the teams make a plan whose islands do not fit (skirmish_gen_teams): plan 0
};



SkirmishScreen skirmish_defaults(const std::string& name);




void skirmish_set_players(SkirmishScreen& s, int players);

void skirmish_set_resources(SkirmishScreen& s, int resources);



void skirmish_set_level(SkirmishScreen& s, int row, int level);
void skirmish_set_nation(SkirmishScreen& s, int row, int nation);


bool skirmish_set_colour(SkirmishScreen& s, int row, int colour);
void skirmish_set_team(SkirmishScreen& s, int row, int team);

// ---- ours, slice G7: the generated map on the setup screen (docs/map-generator-plan.md 4) ----

// Whether the chosen entry is the generated map.
bool skirmish_generated(const SkirmishScreen& s);
// The map file of entry `index` of the list the screen offers (the generated map counted first when offered): nullptr
// for the generated map and past the end.
const MapEntry* skirmish_map_file(const std::vector<MapEntry>& maps, const SkirmishScreen& s, int index);
// Whether entry `index` exists (Prev and Next).
bool skirmish_has_entry(const std::vector<MapEntry>& maps, const SkirmishScreen& s, int index);
// The chosen map as SkirmishSetup::map has it: the generated map's code, a map file's name, or "" when none.
std::string skirmish_map_name(const std::vector<MapEntry>& maps, const SkirmishScreen& s);

// The yes/no switches of the generated map in the screen's order, and the one place that maps them to the settings.
// Islands ticks Sea too, and Sea unticked unticks Islands (decision 53).
enum class GenSwitch { Winter, Sea, Islands, Mountains, DenseForest };
inline constexpr int kGenSwitches = 5;
bool gen_switch(const sim::MapGenSettings& g, GenSwitch w);
void gen_set_switch(sim::MapGenSettings& g, GenSwitch w, bool on);
// On an Islands map the teams set the island plan (decision 53): one island per team, a player without a team on an
// island of its own, at most four islands; without teams (or with more than four groups) the generator draws them. The
// setters of the players, levels and teams call it; the screen calls it after a switch.
void skirmish_gen_teams(SkirmishScreen& s);
// The size: 0 Normal, 1 Large.
void skirmish_gen_size(SkirmishScreen& s, int size);
// A typed or pasted map code sets every choice of the generated map from it, the player count too (rows above it are
// closed as skirmish_set_players closes them; an island plan sets the teams). False, with the reason in `error`, when
// it is not a code; nothing is changed then.
bool skirmish_gen_code(SkirmishScreen& s, const std::string& code, std::string* error);
// "New map" when the seed is fixed (tests, --seed): the next seed after `seed`, never 0. The app's own New map takes a
// random one.
std::uint32_t gen_next_seed(std::uint32_t seed);

// The name the load list and the network pages show for a game's map: a map file without ".dat", a generated map's
// code as it is.
std::string map_display_name(const std::string& map);






sim::SkirmishSetup skirmish_setup(const SkirmishScreen& s, const std::string& map, std::uint32_t seed,
                                  const Settings& settings);

}  // namespace antanta::ui
