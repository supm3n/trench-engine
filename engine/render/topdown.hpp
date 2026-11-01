// Flat top-down overview of a map, as tools/map_dump.py draws it: one colour per cell (mean colour of its land
// texture or sub-tiles, shaded by height) on the staggered grid, plus markers for houses, units, decoration,
// neutral houses, port sites and blocked grid cells. Not the game's isometric view (that comes in part C).
#pragma once

#include <array>
#include <vector>

#include "formats/map.hpp"
#include "formats/sprites.hpp"

namespace antanta {

constexpr int kTopDownCellW = 4;  // output pixels per cell horizontally
constexpr int kTopDownRowH = 2;   // output pixels per row

using Rgb = std::array<double, 3>;

// Mean RGB of each land texture (ignoring alpha), tools/map_dump.py tile_colours.
std::vector<Rgb> tile_colours(const LandTextures& textures);

// `colours` may be empty (no LandTextures.dat): cells are then coloured from their tile id.
Image render_top_down(const MapData& map, const std::vector<Rgb>& colours);

}  // namespace antanta
