



#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <memory>

#include "render/iso.hpp"

namespace antanta::ui {

struct RallyGraphics;
struct RallyGraphicsFree {
    void operator()(RallyGraphics* g) const;
};
using RallyGraphicsPtr = std::unique_ptr<RallyGraphics, RallyGraphicsFree>;

// The pictures are read through `load` on first use; a missing one is logged once and left out.
RallyGraphicsPtr rally_graphics_open(SDL_Renderer* renderer, GameFileLoader load);

// Draws the flag centred on (x, y) in the renderer's current coordinates, sprite frame `frame` (0..7), the cloth
// modulated by `rgb` (0xRRGGBB).
void rally_flag_draw(RallyGraphics& g, float x, float y, int frame, std::uint32_t rgb);

}  // namespace antanta::ui
