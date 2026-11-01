// Drawing of the build ghost (ui/build_menu.hpp, docs/interface.md D.2, slice I5) with SDL_Renderer: the house's
// standing picture (phase 0 of its .les, as the static layer draws a finished house) as a texture of the ghost's size,
// multiplied by the ghost colour as the original's vertex colour does (red keeps only the red channel), opaque but
// for the picture's own transparent pixels (the original's pictures come out opaque, ui/build_menu.hpp). The barn's
// future fields first, each the first frame of its nation's field animation (render/ground_fx's picture), in the
// same colour, at the size of the Animation.anf record.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <span>

#include "render/iso.hpp"
#include "ui/build_menu.hpp"

namespace antanta::ui {

struct GhostGraphics;
struct GhostGraphicsFree {
    void operator()(GhostGraphics* g) const;
};
using GhostGraphicsPtr = std::unique_ptr<GhostGraphics, GhostGraphicsFree>;

GhostGraphicsPtr ghost_graphics_open(SDL_Renderer* renderer);

// Draws ghost `g` of house `type` of `nation` with the view's top left corner at world pixel (vx, vy), in the
// renderer's current coordinates (world pixels of the view; the caller sets the zoom as its scale). The picture is
// loaded into `world` on first use through `load` (missing pictures are logged once and left out).
void ghost_draw(GhostGraphics& gg, WorldGraphics& world, const GameFileLoader& load, const BuildGhost& g, int nation,
                std::uint16_t type, std::uint32_t rgb, int vx, int vy);



void pontoon_preview_draw(GhostGraphics& gg, const WorldGraphics& world, std::span<const sim::PontoonCell> cells,
                          int vx, int vy, bool red = false);

}  // namespace antanta::ui
