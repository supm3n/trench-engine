// The random map generator's Sea and Islands maps (decisions 53 and 54): the water is drawn first as a field of its
// own (one of several kinds of sea, or the islands' shapes, from the seed, a slow domain warp and two octaves of
// noise, the level set for the sea's share), then the starts are fitted to the land it leaves (each near the sea,
// all spread, fair by land path, their land neighbours found), then the territories are cut by land path and
// balanced on buildable land. Integer only, its own random numbers from the key it is given.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "formats/map.hpp"
#include "mapgen.hpp"
#include "mapgen_internal.hpp"

namespace antanta::sim::mapgen {

// The drawn water of a Sea map on a grid of kSeaStep world pixels: per node about how far inside the water it lies
// (world px, > 0 water, <= 0 land).
constexpr std::int64_t kSeaStep = 128;
struct SeaDesign {
    std::int32_t gw = 0, gh = 0;      // nodes: (gw - 1) * kSeaStep >= the map's width, the same down
    std::vector<std::int32_t> value;  // per node, row by row
    std::string words;                // the shape in words (the report; the first word names the kind)
};

// The design at world point (x, y), bilinear between its nodes.
std::int64_t SeaDesignAt(const SeaDesign& d, std::int64_t x, std::int64_t y);

// A body of a drawn design: water (or land: a cape, an island; or a cut: water that takes land away, a bay in an
// island) within the radius of its line, the radius interpolated along it. BodyValue: how far inside the body (x, y)
// lies (negative outside, at least -kBodyDeep).
struct Body {
    std::vector<Pt> line;
    std::vector<std::int64_t> rad;
    bool land = false;
    bool cut = false;
};
constexpr std::int64_t kBodyDeep = 8000;
std::int64_t BodyValue(const Body& b, std::int64_t x, std::int64_t y);

// Unit vectors with 16 fraction bits, a point `d` px along one, the one turned left.
inline Pt Unit(std::int64_t x, std::int64_t y) {
    const std::int64_t len = std::max<std::int64_t>(1, ISqrt(x * x + y * y));
    return Pt{x * 65536 / len, y * 65536 / len};
}
inline Pt Toward(Pt c, Pt u, std::int64_t d) {
    return Pt{c.x + ((u.x * d + 32768) >> 16), c.y + ((u.y * d + 32768) >> 16)};
}
inline Pt Perp(Pt u) { return Pt{-u.y, u.x}; }

// ---- The fitter's grids and judge (Sea, Islands and, since decision 56, the land maps: sim/mapgen_land.cpp). ----

constexpr std::int64_t kGeoStep = 2 * kSeaStep;  // the land path grid: every other design node
constexpr std::int32_t kInf = 1 << 30;
using Pair = std::array<std::int8_t, 2>;  // two starts, indices into a plan's starts

struct SeaGrid {
    std::int32_t w = 0, h = 0;
    std::int64_t step = 1;
    std::size_t I(std::int32_t i, std::int32_t j) const {
        return static_cast<std::size_t>(j) * static_cast<std::size_t>(w) + static_cast<std::size_t>(i);
    }
    std::size_t N() const { return static_cast<std::size_t>(w) * static_cast<std::size_t>(h); }
};
// Chamfer distances (world px) from the nodes with seed != 0, a 5 x 5 mask.
std::vector<std::int32_t> Chamfer(const SeaGrid& g, const std::vector<std::uint8_t>& seed);
// The biggest connected set of nodes with on != 0 (8-connected or 4-connected): 1 there, else 0.
std::vector<std::uint8_t> Biggest(const SeaGrid& g, const std::vector<std::uint8_t>& on, bool eight);
// Path distances (world px) over the nodes with pass != 0 from `src`, 8 neighbours (a step sx across, sy down); -1
// where not reached.
std::vector<std::int32_t> Paths(const SeaGrid& g, const std::vector<std::uint8_t>& pass, std::size_t src,
                                std::int64_t sx, std::int64_t sy);
// Judges a set of starts by land path on the coarse grid `gg` (plan 5.6's needs, with margins): the score per mille
// (below 0: unfair, the value naming the first need missed), the starts in cycle order (the shortest cycle by land,
// for teammates side by side; on islands in the given order, island by island) and the pairs of neighbours by land
// (decision 54): two starts whose territories by land path share a border of 6 nodes or more and have six contested
// places; every start has one at least (on islands: every start with an island mate). `island` (empty but on
// Islands): per start its island; the nearest enemy by land is judged among island mates over the islands of one size.
std::int64_t Judge(const SeaGrid& gg, const std::vector<std::uint8_t>& land, std::vector<Pt> pts,
                   const std::vector<std::int8_t>& island, bool large, std::vector<Pt>* ordered,
                   std::vector<Pair>* pairs_out);

// A start's straight distance to the sea on a Sea map, by band (decision 57): the map code picks one of three bands
// (SeaBandDrawn), the same for every player, so the harbours stay fair: 0 the coast (kSeaNear 1,600, the zone and its
// margin, to kSeaFar 2,600), 1 and 2 deeper inland (2,400 to 3,600, 3,200 to 4,500), so the land behind the coast is
// used. 7 or 8 players on a Normal map keep band 0 with 600 more, so two staggered rows fit along the coasts. The
// computers' port search takes the nearest site of the list, however far (docs/ai.md 10.18); the port sites lie
// within PortFar by land and the coast within CoastFar (sim/mapgen_water.hpp), both by band.
constexpr std::array<std::array<std::int64_t, 2>, 3> kSeaBands{{{1600, 2600}, {2400, 3600}, {3200, 4500}}};
constexpr std::int64_t kSeaNear = kSeaBands[0][0], kSeaFar = kSeaBands[0][1];
// The band a Sea map's code draws (its own hash of the seed, about a third each; 0 for 7 or 8 players on Normal).
int SeaBandDrawn(const MapGenSettings& g);
// The band's nearest and farthest start distance from the sea for these settings.
std::array<std::int64_t, 2> SeaBandOf(const MapGenSettings& g, int band);

// Step 1 of a Sea map without islands: draws up to 24 designs until one holds `g.players` starts (each in the band's
// distances from the sea, spread, the nearest enemy by land within 0.85, room for the contested and own groups of
// plan 5.6); the starts in cycle order (neighbours by land next to each other, so teammates can sit side by side),
// from a drawn start either way round; `neighbours`: the pairs of starts (indices into `starts`) whose land borders
// touch (decision 54: each gets one contested group, no ring). The code's band first (SeaBandDrawn), then each
// nearer band in turn, each from the same key; `band` the one kept. false with the reason.
bool SeaPlan(const MapGenSettings& g, std::uint64_t key, SeaDesign* design, std::vector<Pt>* starts,
             std::vector<std::array<std::int8_t, 2>>* neighbours, std::uint8_t* band, std::string* why);

// Step 1 of an Islands map (decision 53, water-first since 2026-11-08): draws up to 24 designs of `sizes.size()`
// islands (the players per island, largest first) until each island holds its players (kSeaNear to 2,400 px from
// the sea, spread, the nearest island mate by land within 0.85 over islands of one size, every start's nearest start
// of another island within 0.85 in a straight line, room for the groups of plan 5.6); the islands' land per player
// equal within about 10 % by construction. The starts come island by island (island i holds sizes[i] of them, its
// players on consecutive positions: teammates), `island` per start, `neighbours` the pairs of island mates whose
// land borders touch. false with the reason.
bool IslandsPlan(const MapGenSettings& g, std::uint64_t key, const std::vector<int>& sizes, SeaDesign* design,
                 std::vector<Pt>* starts, std::vector<std::int8_t>* island,
                 std::vector<std::array<std::int8_t, 2>>* neighbours, std::string* why);

// Territories by land path (decision 51, Sea and Islands): every land cell goes to the start nearest by land path
// plus a weight per start, the weights set so the buildable land (`land`, BuildableLand) of the territories is as
// equal as the land allows; water and land no start reaches -1. `m` holds kWaterMark on its water cells.
void LandTerritories(const MapData& m, const std::vector<std::uint8_t>& land, const std::vector<Pt>& starts,
                     std::vector<std::int8_t>* territory);

}  // namespace antanta::sim::mapgen
