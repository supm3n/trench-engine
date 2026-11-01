




#pragma once

#include <cstdint>

#include "render/iso.hpp"
#include "render/raster.hpp"
#include "render/water.hpp"

namespace antanta {

// The reflections of the view's neutral houses of Type 0 and 1 (the bridges) and of its ports (Type 0x16, any owner)
// into rt, on its water pixels only. tick: the game tick (g_GameTick, the wobble).
void bridge_reflections_draw(const WorldGraphics& g, const WorldView& view, std::uint32_t tick, ViewPos pos,
                             Target& rt);

}  // namespace antanta
