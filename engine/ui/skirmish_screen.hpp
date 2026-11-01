



#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
};



SkirmishScreen skirmish_defaults(const std::string& name);



void skirmish_set_players(SkirmishScreen& s, int players);

void skirmish_set_resources(SkirmishScreen& s, int resources);



void skirmish_set_level(SkirmishScreen& s, int row, int level);
void skirmish_set_nation(SkirmishScreen& s, int row, int nation);


bool skirmish_set_colour(SkirmishScreen& s, int row, int colour);
void skirmish_set_team(SkirmishScreen& s, int row, int team);






sim::SkirmishSetup skirmish_setup(const SkirmishScreen& s, const std::string& map, std::uint32_t seed,
                                  const Settings& settings);

}  // namespace antanta::ui
