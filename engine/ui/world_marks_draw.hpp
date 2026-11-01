// The marks in the world (ui/world_marks) with SDL: one atlas texture made at run time from the game's
// Data/Misc/Numbers2.bmp (magenta transparent) and Data/Misc/Select.444 and a white block, and every quad of a frame
// in one SDL_RenderGeometry call.
#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <vector>

#include "render/iso.hpp"
#include "ui/world_marks.hpp"

namespace antanta::ui {

struct MarksGraphics;
struct MarksGraphicsFree {
    void operator()(MarksGraphics* g) const;
};
using MarksGraphicsPtr = std::unique_ptr<MarksGraphics, MarksGraphicsFree>;

// The atlas as RGBA8 pixels, kMarksAtlasW x kMarksAtlasH (the CPU picture draws the units' marks from it).
std::vector<std::uint8_t> marks_atlas(const GameFileLoader& load);

// The pictures are read through `load` now; a missing one is logged and its part of the atlas stays empty (the bars
// still draw).
MarksGraphicsPtr marks_graphics_open(SDL_Renderer* renderer, const GameFileLoader& load);

// Draws the quads in the renderer's current coordinates (view pixels, before the render scale), one call.
void marks_draw(MarksGraphics& g, const std::vector<MarkQuad>& quads);

}  // namespace antanta::ui
