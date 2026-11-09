// The random map generator's plain land maps (decision 56): the code first draws a kind of country (open plains,
// forest belts, hill ridges with passes, a few ponds) as a design of its own, then the starts are fitted to it with
// the Sea maps' climb and judge (no fixed ring, two sides or scatter: every code gets its own layout, fair by land
// path, the neighbours by land found), and the territories are cut by land path after the water. Integer only, its own
// random numbers from the key it is given.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mapgen.hpp"
#include "mapgen_internal.hpp"
#include "mapgen_sea.hpp"

namespace antanta::sim::mapgen {

// The kinds of country, chosen by the code (Ridges only without the Mountains switch, which builds ridges itself);
// the kind is the first word of the map's description.
enum class LandKind : std::uint8_t { kPlains, kWoodlands, kRidges, kPonds };

struct LandDesign {
    LandKind kind = LandKind::kPlains;
    // The drawn features on the design grid (SeaDesign's nodes and words): Woodlands > 0 inside a forest belt or a
    // great wood (the forests fill them, the corridors cut through); Ponds > 0 inside a pond (the water step draws
    // them). Plains and Ridges draw nothing: the ridges come from the hills step along the territories' borders.
    SeaDesign field;
};

// Step 1 of a land map: draws the kind and, up to 24 times, its features until `g.players` starts fit (off the
// features, spread, the nearest enemy by land within 0.85, room for the contested and own groups of plan 5.6); the
// starts in cycle order (neighbours by land next to each other, so teammates can sit side by side), from a drawn
// start either way round; `neighbours`: the pairs of starts whose land borders touch (one contested group each).
// false with the reason.
bool LandPlan(const MapGenSettings& g, std::uint64_t key, LandDesign* design, std::vector<Pt>* starts,
              std::vector<Pair>* neighbours, std::string* why);

}  // namespace antanta::sim::mapgen
