// The random map generator's hills and mountains (docs/map-generator-plan.md 5.5, slice G5): the plain's slow swell,
// natural hills outside the start zones, and with the Mountains switch blocking ridges on the borders between the
// players' territories with passes where the corridors cross. Integer only, its own hashes from the key it is given,
// never the CRT rand() or the game's random numbers: the same bytes on every machine.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "formats/map.hpp"
#include "mapgen.hpp"
#include "mapgen_internal.hpp"

namespace antanta::sim {

namespace mapgen {

// Step 5: sets the height of every cell of `m` (the flat plain at 50 on entry). The start zones and their margins
// (kZoneR + kMargin around each start) stay exactly at 50, the corridors stay on the plain (corner span under 20),
// hills keep every cell's corner span within 40 (walkable, as the shipped hills); with `g.mountains` the ridges' crest
// bands are cliffs (corner span over 60 on every cell between the crest and its foot), cut open where a corridor
// crosses. `ridges`: 0 hills only, 1 the Ridges kind of a land map (decision 56: fewer, lower and narrower ridges with
// passes, crests 112 to 170), 2 the Mountains switch (crests 140 to 245). `corridor`: the corridor cells of step 3;
// `key`: the step's random stream. Returns "" when the terrain passes the step's own checks (tanks reach every start
// and the middle; with ridges every ridged border's pass is taken and the walks to the middle stay short), else the
// reason, and the attempt fails.
std::string Hills(MapData& m, const MapGenSettings& g, const std::vector<Pt>& starts,
                  const std::vector<std::uint8_t>& corridor, std::uint64_t key, int ridges);

}  // namespace mapgen

// The shape and fairness numbers of the terrain of a built map (the G5 report; plan 3.1 "Shape").
struct MapGenHillStats {
    std::int32_t play_cells = 0;     // cells 10 from the left and right edge, 20 rows from the top and bottom
    std::int32_t above65 = 0;        // play cells above height 65
    std::int32_t steep = 0;          // play cells over the walking rule (corner span over 60)
    std::int32_t large_blocked = 0;  // play cells over the tanks' rule (corner span over 25)
    std::int32_t hills = 0;          // groups of 30 or more cells above 65
    std::int32_t walls = 0;          // groups of 30 or more cells over the walking rule (ridge flanks)
    std::int32_t peak = 0;           // the highest cell
    bool large_reach = false;        // tanks (span 25, no object) reach every start and the middle from start 1
    std::vector<std::int32_t> region_cells, region_above65;   // per start: its territory (nearest start) outside zones
    std::vector<std::int32_t> to_middle, to_middle_straight;  // per start: land path and straight line to the middle
    std::vector<std::int32_t> to_next, to_next_straight;      // per start: land path and line to the nearest start
};
MapGenHillStats MapGen_HillStats(const MapData& m, const MapGenLayout& layout);
std::string MapGen_HillStatsText(const MapGenHillStats& s);

}  // namespace antanta::sim
