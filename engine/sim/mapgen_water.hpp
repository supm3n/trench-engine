// The random map generator's water (docs/map-generator-plan.md 5.4, slice G6): lakes on land maps, the sea of the Sea
// switch (a coast all around the players' land, bays between them), the heights under and beside the water, the
// obstacle grid with the coast outline records (marching squares, drawn as the original draws them: render.md 19
// "Coast foam"), and the port sites. Integer only, its own hashes from the key it is given, never the CRT rand() or the
// game's random numbers: the same bytes on every machine.
//

// every cell whose raised left corner (Obstacle_UnderCell) lies in a blocked grid block, and the walking and building

// grid and the heights it writes, so its own rules and the engine's agree cell by cell.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "formats/map.hpp"
#include "mapgen.hpp"
#include "mapgen_internal.hpp"
#include "mapgen_sea.hpp"

namespace antanta::sim {

namespace mapgen {

// The port's footprint (BasicParams, every nation: 7 x 7, docs/map-generator-plan.md 2.3).
constexpr Footprint kPort{22, 7, 7};
// Steps 6 and 7 (deposits, forests) see water as cells holding this object, so they keep off it without knowing
// water; Ports() removes it again. Never in a finished map.
constexpr std::uint16_t kWaterMark = 1;
// A Sea map's least share of whole water blocks (per mille; the check and the water step): a generated map tells the
// computers it is a water map (decision 54, sim/skirmish.cpp), so it no longer needs LandMap's third; Islands keep
// the third until they are reworked. A start's nearest coast at most kCoastFar px, 2 port sites within kPortFar by
// land (plan 5.4, 5.10; the starts stand kSeaNear to kSeaFar px from the sea, and the computers' port search takes
// the nearest site of the list, however far). A Sea map whose starts stand deeper inland (decision 57, its band in
// MapGenLayout::sea_band) takes CoastFar and PortFar instead: its band's farthest start plus the same 800 and 1,200.
constexpr std::int64_t kSeaLeast = 180, kIslesLeast = 345;
constexpr std::int64_t kCoastFar = 3400, kPortFar = 3800;
inline std::int64_t CoastFar(const MapGenLayout& l) {
    return l.sea_band == 0 ? kCoastFar : kSeaBands[l.sea_band][1] + (kCoastFar - kSeaFar);
}
inline std::int64_t PortFar(const MapGenLayout& l) {
    return l.sea_band == 0 ? kPortFar : kSeaBands[l.sea_band][1] + (kPortFar - kSeaFar);
}
// The starts' nearest port sites within this ratio by land (decision 54 e: a start's nearest site at most 1.5 times
// another's; 54 g: 0.50 on Normal Sea with 7 or 8 players, whose coast band is wider).
constexpr std::int64_t kSeaAccess = 67;

// Step 4, after the hills (step 5 in the plan's order; the heights must be known): with `g.sea` the sea of `design`
// (decision 53: drawn before the starts, sim/mapgen_sea.cpp; plus small neutral islands, lakes and islets) or the
// islands of g.islands (drawn before the starts too, IslandsPlan; the same design field); on a land map the ponds of
// `design` (the Ponds kind, decision 56, sim/mapgen_land.cpp) or, with no design, up to `lakes_most` lakes (0 to 4,
// Large up to 6, as the seed draws them). Lowers the land near the water (no cliff into it, the shore below 55 so the
// water shows up to its outline), sets height 30 under it, writes m.obstacle_grid and m.routes, and puts kWaterMark on
// every water cell. Nothing within a zone's margin or on a corridor. Adds the shape's words to layout->shape and the
// neutral islands to layout->isles. "" when the water passes the step's checks (the sea's share, every start's coast
// within CoastFar, every start reached on foot and by tanks from the others of its island, the middle on a land map),
// else the reason.
std::string Water(MapData& m, const MapGenSettings& g, MapGenLayout* layout, std::uint64_t key, const SeaDesign* design,
                  int lakes_most);

// After the forests (steps 6 and 7): removes kWaterMark, and on a Sea map picks the port sites (m.port_sites): per
// start 3 sites that pass the port rule (PortOk), 2 of them within PortFar by land, spaced apart, the objects on their
// footprint cleared, each next to the ships' biggest sea; the starts' nearest sites within kSeaAccess by land. ""
// or the reason (the attempt fails). `water` (may be null): one byte per cell, non-zero on water (WaterCells), for the
// ground's shore palette.
std::string Ports(MapData& m, const MapGenSettings& g, const MapGenLayout& layout, std::vector<std::uint8_t>* water);



std::vector<std::uint8_t> WaterCells(const MapData& m);
std::vector<std::uint8_t> BlockedCells(const MapData& m);




std::vector<std::uint8_t> ShipCells(const MapData& m);
std::vector<std::int32_t> ShipSeas(const std::vector<std::uint8_t>& open, std::int32_t w, std::int32_t h);
std::int32_t PortSea(const std::vector<std::int32_t>& seas, std::int32_t w, std::int32_t h, std::int32_t x,
                     std::int32_t y);




bool PortOk(const MapData& m, const std::vector<std::uint8_t>& blocked, std::int32_t x, std::int32_t y);

// The obstacle grid and its coast outline records from a field on the grid's corner lattice ((2W + 1) x (H + 1)
// points, point (i, j) at world (48 i, 24 j)); water where the value is above 0. A block with water at every point of
// its 3 x 3 sub-lattice is 1, without any 0, else 0x8000 + k with record k: the water part of the bilinear field in
// vertical strips (at x 0, 6, 12, ..., 48 and where the coast meets the top and bottom edges), convex polygons (byte
// points x | y << 8 inside the block, clockwise on the screen as the shipped records) that touch only along whole
// sides, so their fans draw and the renderer's foam rule puts foam on the coast and nowhere inside the water.
// false when more than 0x7fff records would be needed.
bool CoastRecords(const std::vector<std::int32_t>& field, std::int32_t W, std::int32_t H,
                  std::vector<std::uint16_t>* grid, std::vector<MapRoute>* routes);

}  // namespace mapgen

// The water numbers of a built map (the G6 report, plan 3.1 "Water", 5.10 "Water maps").
struct MapGenWaterStats {
    std::int32_t blocked_share = 0;   // per mille of the obstacle grid's entries that are blocked (LandMap's measure)
    std::int32_t lakes = 0;           // water bodies not touching the map's edge
    std::int32_t coast_records = 0;   // outline records
    std::int32_t ports = 0;           // port sites
    std::vector<std::int32_t> coast;  // per start: the straight distance to the nearest water cell (-1: none)
    std::vector<std::int32_t> port;   // per start: the land path to its nearest port site's land (-1: none)
    std::vector<std::int32_t> ports_near;  // per start: port sites passing PortOk within port_far by land
    std::int32_t port_far = 0;             // the map's PortFar
};
MapGenWaterStats MapGen_WaterStats(const MapData& m, const MapGenLayout& layout);
std::string MapGen_WaterStatsText(const MapGenWaterStats& s);

}  // namespace antanta::sim
