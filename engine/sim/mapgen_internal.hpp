// Shared by the parts of the random map generator (mapgen.cpp: codes, the pipeline, positions, kit; mapgen_check.cpp:
// the checks and the report). Not for other code: the public interface is mapgen.hpp.
#pragma once

#include <cstdint>
#include <vector>

#include "formats/map.hpp"

namespace antanta::sim::mapgen {

// ---- The recipe's numbers (docs/map-generator-plan.md 5.0 to 5.2), world pixels. ----

constexpr std::int64_t kZoneR = 1300;  // the start zone's radius
constexpr std::int64_t kCoreR = 550;   // its flat, empty core
constexpr std::int64_t kMargin = 300;  // around a zone, kept clear by the world's features (G4 to G6)
constexpr std::int64_t kBandX = 1152;  // the keep-out band for anything of the kit: 12 cells from the left and right
constexpr std::int64_t kBandY = 720;   // and 30 rows from the top and bottom
constexpr std::int64_t kRoom = 250;    // no object this close to a kit deposit
constexpr std::int32_t kPlain = 50;    // the height of the plain and of every start
constexpr int kKitTrees = 150;         // trees in a zone: the home wood 45, the second wood 35, 70 scattered
constexpr int kKitStones = 12;
constexpr int kWorkers = 8;
constexpr int kDwellings = 8;             // small houses that must fit in a zone
constexpr std::int32_t kEmptyDisc = 900;  // the largest empty disc allowed (plan 5.10; the shipped maps 344 to 816)

constexpr std::uint16_t kOil = 0x2581, kGold = 0x2582, kIron = 0x2583;
constexpr std::uint16_t kKinds[3] = {kGold, kIron, kOil};  // the report's order: gold, iron, oil


struct Footprint {
    std::uint16_t type;
    std::int32_t a, b;
};
constexpr Footprint kCityCentre{6, 6, 5}, kDwelling{9, 4, 3}, kSawmill{12, 4, 4}, kBarn{15, 3, 4};
constexpr Footprint kMineOf[3] = {{11, 3, 3}, {10, 3, 3}, {13, 3, 3}};  // gold, iron, oil

inline bool IsTree(std::uint16_t o) { return o >= 9000 && o < 0x251c; }
inline bool IsStone(std::uint16_t o) { return o >= 0x251c && o < 9600; }
inline bool IsDeposit(std::uint16_t o) { return o >= kOil && o <= kIron; }
inline int KindIndex(std::uint16_t o) { return o == kGold ? 0 : o == kIron ? 1 : 2; }
// The walking rule's objects (BlocksLand 0x491d.., sim/pathgrid.cpp): men walk through trees and stones; every other
// object blocks (deposits, decoration: whether a decoration blocks is read from objects.les, so the generator takes
// every one as blocking).
inline bool BlocksWalk(std::uint16_t o) { return o != 0 && !IsTree(o) && !IsStone(o); }
// The walking rule's border band (Cell_LandStatic's Border): never a walking cell.
inline bool WalkBorder(std::int32_t W, std::int32_t H, std::int32_t x, std::int32_t y) {
    return x < 4 || y < 10 || x >= W - 3 || y >= H - 9;
}

// ---- Cells and world points. ----

struct Pt {
    std::int64_t x = 0, y = 0;
};
inline std::int64_t Dist2(Pt a, Pt b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }
std::int64_t ISqrt(std::int64_t v);



inline Pt CellCentre(std::int32_t x, std::int32_t y) { return Pt{96 * x + 48 * (y & 1), 24 * y}; }

void CellAt(Pt p, std::int32_t* cx, std::int32_t* cy);

void Step(std::int32_t* x, std::int32_t* y, int dir);
// The world length of a step in each direction (the path distances of the checks).
constexpr std::int32_t kStepLen[9] = {0, 54, 54, 54, 54, 48, 48, 96, 96};

// ---- The generator's own placing rules (plan 5.10, first level: fixed rules, every placed object blocks). ----

struct Grid {
    std::int32_t W = 0, H = 0;
    const MapData* map = nullptr;
    const std::vector<std::uint8_t>* taken = nullptr;  // cells under a house planned by the checks (null: none)
};



bool Box1(std::int32_t W, std::int32_t H, std::int32_t x, std::int32_t y, const Footprint& f,
          std::vector<std::int32_t>* cells);



bool CanPlace(const Grid& g, std::int32_t x, std::int32_t y, const Footprint& f, bool summer);

int SawmillTrees(const MapData& m, std::int32_t x, std::int32_t y);

// Per cell 1 when buildable land as the report counts it (decision 51: the territories are cut by it): not water
// (kWaterMark), off the edge band, the building rule's span (20) over the cell and its neighbours; objects left out.
std::vector<std::uint8_t> BuildableLand(const MapData& m);

// A tree or stone id of the climate from a 32-bit hash, by the measured weights (mapgen_forest.cpp).
std::uint16_t TreeIdOf(bool winter, std::uint32_t hash);
std::uint16_t StoneIdOf(bool winter, std::uint32_t hash);

// The checks' path search (mapgen_check.cpp): 24 moves in 16 directions or more, lengths in world pixels, over the
// walking cells. The graph holds per cell the moves whose crossed cells and end cell are walking cells, so the many
// searches on one map share that work.
struct PathGraph {
    std::int32_t W = 0, H = 0;
    std::vector<std::uint32_t> moves;  // per cell: bit k for move k
};
PathGraph MakePathGraph(const std::vector<std::uint8_t>& walk, std::int32_t W, std::int32_t H);
// The path distances from `sources` up to `bound` (-1: none); -1 where not reached; `label` (may be null): the
// index of the source each cell is nearest to.
void PathDistances(const PathGraph& pg, const std::vector<std::int32_t>& sources, std::int32_t bound,
                   std::vector<std::int32_t>* dist, std::vector<std::int8_t>* label = nullptr);

}  // namespace antanta::sim::mapgen
