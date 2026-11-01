// The cursor pictures with SDL (docs/interface.md D.1, slice I9): Data/Misc/Cursors/*.bmp of the game files, read on
// first use, magenta (255, 0, 255) transparent, hot spot (0, 0) as the original. In a window they are SDL colour
// cursors (the system draws them, no lag behind the mouse); a batch picture gets the picture drawn at the mouse point,
// as the original's software blit.
#pragma once

#include <SDL3/SDL.h>

#include <memory>

#include "render/iso.hpp"
#include "ui/cursor.hpp"

namespace antanta::ui {

struct CursorGraphics;
struct CursorGraphicsFree {
    void operator()(CursorGraphics* g) const;
};
using CursorGraphicsPtr = std::unique_ptr<CursorGraphics, CursorGraphicsFree>;

// The pictures are read through `load` on first use; a missing one is logged once and the system cursor stands in.
CursorGraphicsPtr cursor_graphics_open(SDL_Renderer* renderer, GameFileLoader load);

// The window's cursor: picture `c`, each pixel `scale` x `scale` window pixels (the world zoom, so it keeps its size
// beside the world); set only when it changes.
void cursor_show(CursorGraphics& g, Cursor c, int scale);
// The system's default cursor, as the menus have it (set only when it changes).
void cursor_show_system(CursorGraphics& g);

// Draws picture `c` with its top left pixel at (x, y) in the renderer's current coordinates (batch pictures).
void cursor_draw(CursorGraphics& g, Cursor c, float x, float y);

}  // namespace antanta::ui
