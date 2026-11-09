// The random map generator's free deposits, forests, stones and decoration (docs/map-generator-plan.md 5.6 and 5.7,
// slice G4). Outside the start zones only: the zones' kit is mapgen.cpp's. Integer only, its own random numbers from
// the key it is given, never the CRT rand() or the game's random numbers: the same bytes on every machine.
#pragma once

#include <cstdint>
#include <string>

#include "formats/map.hpp"

namespace antanta::sim {

struct MapGenSettings;
struct MapGenLayout;
namespace mapgen {
struct SeaDesign;
}

// Plan 5.6: the free deposits in rings (2026-11-05), recorded in layout->groups. Contested: per pair of neighbours by
// land (layout->neighbours, decisions 54 and 56) one group (Normal gold, iron, oil; Large one more of the kind the own
// pair leaves out; two groups on a 2-player map) at path distances within 0.92 of its two players and no other start
// as near, every pair's group in one common distance window where the land allows it; on a Sea map every pair or
// none gets a second group deeper inland (decision 57: 1,500 px farther from the sea than the band's nearest start,
// 1,000 farther than the first group, 3,000 px from it; on a 2-player map it is the second group). The shared group
// (land and Sea maps, decision 57; not on Islands): one rich group (Normal 4, Large 5) at the fairest place where
// every start's path there is within 0.40 (the starts are fitted per code, so not every map has such a place; then
// none). Own: per player the same groups (Normal one of two
// kinds drawn per map; Large gold, iron, oil and a second of that pair) in its own territory on its outer side, in one
// window of path distance and one of the computers' measure for every player. Every free deposit is a mine site
// (CanPlace, the keep-out band of 5.0), 400 px from every other deposit, 600 px from every zone, and, for the

// start's near kit deposit of its kind, so every computer takes its near kit deposits first. false with the failed ring
// in `why` (the attempt fails, plan 5.11).
bool MapGen_FreeDeposits(MapData& m, const MapGenSettings& g, MapGenLayout* layout, std::uint64_t key,
                         std::string* why);

// Plan 5.7: parkland groves from a forest field (9 to 12 % of the land in trees; Dense forest 17 to 23 % with bigger
// stands), lone trees and small clumps on the open land, tree ids by measured stands (tools/mapgen_trees.py), single
// stones mostly next to a tree, and small decoration. `belts` (may be null): the Woodlands kind's forest belts and
// great woods (decision 56, sim/mapgen_land.cpp; > 0 inside), grown thick first, the corridors cut through them; the
// land then carries 14 to 18 % of trees (Dense forest 20 to 26), each territory its share counting its belt trees.
// Nothing within a zone and its margin, on a corridor (the cut paths of Dense forest) or in a deposit's room. Never
// fails: only decoration is ever left out.
void MapGen_Forests(MapData& m, const MapGenSettings& g, const MapGenLayout& layout, std::uint64_t key,
                    const mapgen::SeaDesign* belts);
// Tanks (MapGen_Check's tank rule: every object blocks them): from each island's first start, a lane to every start,
// free group and port site that tanks do not reach, through the fewest objects outside the zones, which are removed.
void MapGen_TankLanes(MapData& m, const MapGenLayout& layout);

// The wood numbers of plan 3.1 over the play area (cells 10 from the left and right edge, 20 rows from the top and
// bottom), measured as engine\build\mapgen-plan\measure2.py measures the shipped maps.
struct MapGenWoodLook {
    std::int32_t tree_share = 0;     // per mille of the cells with a tree
    std::int32_t lone_share = 0;     // per mille of the trees in groves of 1 to 3 (8-connected)
    std::int32_t biggest = 0;        // trees in the biggest grove
    std::int32_t density = 0;        // per mille: median tree share of the 9 rows x 5 cells around a tree
    std::int32_t stones_per_k = 0;   // stones per 100,000 cells (3 to 8 per 1,000 is 300 to 800)
    std::int32_t stones_near = 0;    // per mille of the stones within 150 px of a tree
    std::int32_t decoration = 0;     // objects 9605 to 9699 (9601 to 9603 are the deposits)
    std::int32_t big_grove_ids = 0;  // distinct tree ids in the biggest grove
    std::int32_t wood_ratio = 0;     // per mille: the fewest trees within 3,000 px of a start over the most (soft)
};
// `layout` (may be null): the starts for wood_ratio.
MapGenWoodLook MapGen_WoodLook(const MapData& m, const MapGenLayout* layout);

}  // namespace antanta::sim
