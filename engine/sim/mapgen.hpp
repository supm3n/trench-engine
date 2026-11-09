// The random map generator (docs/map-generator-plan.md): a skirmish map built from a short map code, the same bytes
// for the same code on every machine (desktop and browser). Slice G2: the start positions, a fair start zone with the
// same kit for every player on a flat plain, the connections between the starts, the checks and the retry rule. The
// output is a MapData, the structure the map file reader gives, so the skirmish start (GameState_NewSkirmish), the
// drawing and the computer players take it as they take a map file. The generator reads no files and never touches
// the game's random numbers.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "formats/aitemplate.hpp"
#include "formats/map.hpp"
#include "mapgen_forest.hpp"
#include "mapgen_ground.hpp"

namespace antanta::sim {

struct GameData;

// What a map code holds (docs/map-generator-plan.md 4.1). Until the first release the version stays 1 and a deliberate
// change of the output updates the hashes in tests/test_mapgen.cpp (plan 6, 8.3).
struct MapGenSettings {
    std::uint8_t version = 1;   // the generator version; 1 is the only one so far
    std::uint8_t size = 0;      // 0 Normal (200 x 400 cells), 1 Large (300 x 600)
    std::uint8_t players = 2;   // start positions, 2..8
    bool winter = false;        // W: the winter climate
    bool sea = false;           // S: a water map (slice G6, sim/mapgen_water.cpp)
    bool mountains = false;     // M: ridges with passes (slice G5, sim/mapgen_hills.cpp)
    bool dense_forest = false;  // F: dense forest (slice G4; no effect until then)
    // I: islands (decision 53; implies `sea`): the players' land split into islands that only ships cross.
    // `island_plan`: the players per island, largest first, one nibble each from the lowest (0x22: two islands of
    // 2, 0x112: islands of 2, 1 and 1; 2 to 4 islands); 0: no teams, the generator draws (two islands with the
    // players split evenly, or with 3 or 4 players one island each). The skirmish start puts teammates on one island
    // (sim/skirmish.cpp, teams largest first on an islands map).
    bool islands = false;
    std::uint32_t island_plan = 0;
    std::uint32_t seed = 0;  // the map seed (separate from the game seed, SkirmishSetup::seed)
};

// The island sizes of a plan (largest first; empty for 0 or a plan that does not add up to `players`).
std::vector<int> MapGen_IslandSizes(std::uint32_t plan, int players);
// Whether a plan's islands have room with every start about as far from the other islands (0.82); a plan that does
// not fit builds the plan of no teams instead (the setup screen says so).
bool MapGen_IslandPlanFits(std::uint8_t size, int players, std::uint32_t plan);

// "v1-N-4-SM-3F9A2C71": version, size (N or L), players, the switches that are on (W, S, I, M, F in that order, "0"
// when none; I may carry the island sizes as digits, "SI22", and implies S), the seed (1 to 8 hex digits). The written
// form is the canonical one: upper-case, 8 hex digits, S written with I.
std::string MapGen_Code(const MapGenSettings& g);
// Reads a map code; nullopt with a message in `error` (may be null) when it is not one.
std::optional<MapGenSettings> MapGen_ParseCode(const std::string& code, std::string* error);

// One start and its kit (plan 5.2), as the generator placed them.
struct MapGenZone {
    std::int32_t x = 0, y = 0;                              // the start (the zone's centre) in world pixels
    std::array<std::array<std::int32_t, 2>, 6> deposits{};  // cells: near gold, iron, oil, then far gold, iron, oil
    std::array<std::array<std::int32_t, 2>, 2> sawmills{};  // cells: the home wood's and the second wood's sawmill site
};

// A group of free deposits (plan 5.6): ring 1 a player's own expansion group in its territory, ring 2 a group
// contested by two neighbouring players, ring 3 the rich group at the fairest place of a land or Sea map (shared by
// all), ring 4 a group on a small neutral island that only ships reach (decision 53).
struct MapGenGroup {
    std::uint8_t ring = 0;
    std::array<std::int8_t, 2> players{-1, -1};      // ring 1: the owner; ring 2: the two neighbours; ring 3: -1
    std::vector<std::array<std::int32_t, 2>> cells;  // the deposits (cells), gold, iron, oil in the recipe's order
    bool inland = false;  // ring 2 on a Sea map: the pair's second group, deeper inland (decision 57)
};

// The plan of a built map: the pattern of the starts, the zones in position order (the shortest cycle by land, so
// teammates can sit side by side), each cell's territory, the connection corridors (plan 5.3; later steps keep them
// clear) and the free deposit groups.
struct MapGenLayout {
    // 0 a land map (a kind of country drawn first, the starts fitted to it: sim/mapgen_land.cpp, decision 56), 1 a
    // Sea map (the water drawn first, the starts fitted to its land: sim/mapgen_sea.cpp), 4 islands (`island` per
    // zone); the territories are cut by land path on every map (LandTerritories)
    std::uint8_t pattern = 0;
    // The pairs of starts that are neighbours by land (their territories touch), each sorted, the list sorted; one
    // contested group per pair, no ring (decisions 54 and 56; a Sea map may add an inland one per pair, decision 57).
    std::vector<std::array<std::int8_t, 2>> neighbours;
    std::string shape;  // the drawn shape in words (the report)
    // A Sea map's band of start distances from the sea (decision 57: 0 the coast as before, 1 and 2 deeper inland,
    // sim/mapgen_sea.hpp kSeaBands); the coast and port reach of the water step and the check follow it (CoastFar,
    // PortFar in sim/mapgen_water.hpp). 0 on every other map.
    std::uint8_t sea_band = 0;
    std::vector<std::int8_t> island;                 // islands: per zone its island (empty on other maps)
    std::vector<std::array<std::int32_t, 3>> isles;  // small neutral islands: centre (world px) and radius
    std::vector<MapGenZone> zones;
    std::vector<std::int8_t> territory;  // per cell: the position whose territory it is, -1 the shared middle
    std::vector<std::uint8_t> corridor;  // per cell: 1 on a corridor outside the zones
    std::vector<MapGenGroup> groups;
};

// The numbers of one start (plan 5.12).
struct MapGenStartStats {
    std::int32_t height = 0;
    std::array<std::int32_t, 3> deposits{};  // gold, iron, oil within the zone's radius
    std::int32_t trees = 0, stones = 0;
    std::int32_t zone_cells = 0, free_cells = 0;  // cells within the radius; those buildable (no object, flat)
    std::array<std::int32_t, 3> to_deposit{};     // path distance to the nearest gold, iron, oil (world pixels)
    std::int32_t to_enemy = 0;                    // path distance to the nearest other start
    std::int32_t ports = 0;                       // port sites passing the port rule within PortFar by land (Sea)
    std::int32_t to_middle = -1;                  // path distance to the map's middle (-1: water there)
    // Its territory (plan 5.1): cells, those buildable (no object, flat, no water), trees outside the zones.
    std::int32_t terr_cells = 0, terr_free = 0, terr_trees = 0;
    std::int32_t own_walk = -1, own_finder = -1;  // its own groups: path to the farthest group's nearest deposit, and
                                                  // the computers' |dx| + |dy| (cells and rows) to it
};

// The free deposit rings of the report (plan 5.12): per ring the deposits per group and the path distances.
struct MapGenRingStats {
    std::int32_t groups = 0, deposits = 0;
    std::int32_t walk_lo = -1, walk_hi = 0;  // over the groups and their players: the nearest deposit's path distance
    std::int32_t ratio = 0;                  // per mille: the worst smallest over largest that the ring's rule takes
};

// What MapGen_Build did (the report of plan 5.12, the sweep's line).
struct MapGenReport {
    std::uint8_t attempt = 0;         // the attempt kept (0-based; the report prints it 1-based)
    std::vector<std::string> failed;  // per failed attempt: "check: detail"
    MapGenLayout layout;              // of the attempt kept
    std::vector<MapGenStartStats> starts;
    std::int32_t trees = 0, stones = 0, land_cells = 0;
    std::array<std::int32_t, 3> deposits{};  // on the whole map: gold, iron, oil
    std::int32_t above65_cells = 0, steep_cells = 0;
    std::int32_t water_share = 0;            // per mille of the obstacle grid in whole water blocks (LandMap's measure)
    std::array<MapGenRingStats, 4> rings{};  // own, contested, middle, isles
    std::int32_t middle_ratio = 0;           // per mille: the starts' path to the middle, smallest over largest
    std::int32_t enemy_ratio = 0;            // per mille: the nearest enemy's path, smallest over largest
    std::int32_t empty_disc = 0;             // the largest empty disc's radius (world px, plan 5.10 "Empty area")
    std::int32_t across_ratio = -1;  // islands, per mille: each start's straight line to the nearest start of another
                                     // island, smallest over largest over all starts (-1: no islands)
    std::int32_t tank_miss = 0;      // starts, free groups and port sites tanks do not reach (every object blocks)
    MapGenGroundLook look;           // the ground's look numbers (plan 3.1) of the map kept, and its wood
    MapGenWoodLook wood;             // numbers: filled by MapGen_LookNumbers, not by MapGen_Build
};

// Builds the map of `g`: up to 16 attempts, each one checked by the generator's own rules (MapGen_Check); the first
// that passes is the map. nullopt with a message for settings out of range or a code that makes no valid map.
std::optional<MapData> MapGen_Build(const MapGenSettings& g, std::string* error, MapGenReport* report = nullptr);
// The look and wood numbers of a report (plan 3.1) for the map MapGen_Build gave with it: apart, so a build for play
// or a preview does not pay for them.
void MapGen_LookNumbers(const MapData& m, MapGenReport* r);

// The hard checks of plan 5.10 with the generator's fixed rules (no game file): "" when all pass, else the first
// failing check and its number ("enemy distance 0.71"). Fills the numbers of `report` (may be null).
std::string MapGen_Check(const MapData& m, const MapGenSettings& g, const MapGenLayout& layout, MapGenReport* report);




std::string MapGen_Verify(const MapData& m, const MapGenSettings& g, const MapGenLayout& layout, const GameData& data,
                          const AiTemplate* tpl);

// The report of plan 5.12 as text (a head line, one line per start, the map line), and as one TSV line for the sweep
// (MapGen_ReportTsvHead names its columns).
std::string MapGen_ReportText(const MapGenSettings& g, const MapGenReport& r, double build_ms, double check_ms);
std::string MapGen_ReportTsvHead();
std::string MapGen_ReportTsv(const MapGenSettings& g, const MapGenReport& r, double build_ms,
                             const std::string& verify);

// A 64-bit FNV-1a hash of everything a MapData holds that the start reads (the hash test and the desktop / browser
// comparison).
std::uint64_t MapGen_Hash(const MapData& m);




void Map_ShadeFromHeights(MapData& m);

}  // namespace antanta::sim
