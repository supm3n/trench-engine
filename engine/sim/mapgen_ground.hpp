// The random map generator's ground painting (docs/map-generator-plan.md 5.8, slice G3): the base tile and the nine
// sub-tiles of every cell, painted as regions of "ground mixes" measured from the shipped maps (tools/mapgen_mixes.py),
// with interleaved borders, hill tops in the high mixes and a shore mix by water. Integer only, its own hashes from the
// key it is given, never the CRT rand() or the game's random numbers: the same bytes on every machine.
#pragma once

#include <cstdint>
#include <span>

#include "formats/map.hpp"

namespace antanta::sim {

// Paints m.cells[].tile (with the sub-tile flag 0x8000) and subtile[0..8] of every cell. Reads the size, the heights
// and the objects (trees, ids 9000 to 9499: the forest floor) of `m`; `water` is one byte per cell (non-zero: water),
// or empty for a land map. `key` is the step's random stream (plan 5: each step its own); the same inputs and key give
// the same ground. Nothing else of `m` changes (the shade is the next step's).
void MapGen_PaintGround(MapData& m, std::span<const std::uint8_t> water, bool winter, std::uint64_t key);

// The look numbers of plan 3.1 over the play area (cells 10 from the left and right edge, 20 rows from the top and
// bottom), measured as the plan's scripts measure the shipped maps (engine\build\mapgen-plan\measure2.py, measure3.py).
struct MapGenGroundLook {
    std::int32_t subtile_share = 0;  // per mille of land cells with the sub-tile flag
    std::int32_t same_slots = 0;     // per mille of neighbouring sub-tile slots (along u and v) with the same id
    std::int32_t blob_median = 0;    // same-id blobs of the 12 main ids on the sub-tile lattice: median, 90th
    std::int32_t blob_p90 = 0;       // percentile (slots, 4-connected)
    std::int32_t window_ids = 0;     // per window of 12 cells x 24 rows: the median count of ids with 3 % or more
    std::int32_t window_top = 0;     // and the median share of the top id (per mille)
    std::int32_t base_differ = 0;    // per mille of neighbouring land cell pairs with different base tiles
    std::int32_t land_height = 0;    // median height of the land
    std::int32_t distinct_ids = 0;   // sub-tile ids on the map
};
MapGenGroundLook MapGen_GroundLook(const MapData& m, std::span<const std::uint8_t> water);

}  // namespace antanta::sim
