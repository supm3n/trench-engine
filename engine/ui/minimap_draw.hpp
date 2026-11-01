// The minimap drawn with SDL_Renderer (docs/interface.md A.6, slice I2): the static layer, the bevels around it, the
// unit dots, the fog layer, the red view rectangle and the battle marks, then the six buttons. The rules (where, which
// colours, what a click means) are in ui/minimap.hpp; this file only loads the pictures and draws what the app hands
// it. Pictures come from the game's files at run time (Data/Interface/interface.les, InterfaceGroup.les and
// Data/Misc/MiniMapIndicators/Battle.les), scaled to the interface scale with the picture filter once and cached; the
// minimap's own pixels are made at the scale's size (ui/minimap.hpp), never scaled as a picture.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ui/frontend.hpp"
#include "ui/minimap.hpp"

namespace antanta::ui {

struct MinimapGraphics;
struct MinimapGraphicsFree {
    void operator()(MinimapGraphics* g) const;
};
using MinimapGraphicsPtr = std::unique_ptr<MinimapGraphics, MinimapGraphicsFree>;

// Fails when a picture is missing.
MinimapGraphicsPtr minimap_graphics_open(SDL_Renderer* renderer, GameFiles files, std::string* error);

// What one frame of the minimap shows, filled by the app (all positions in minimap pixels, 0 .. size - 1).
struct MinimapFrame {
    // The static layer, size x size pixels 0xRRGGBB, uploaded again when `static_version` changes.
    const std::vector<std::uint32_t>* static_rgb = nullptr;
    std::uint64_t static_version = 0;
    // The fog layer, size x size bytes (1 visible, 0 black), uploaded when `fog_version` changes; null: no fog.
    const std::vector<std::uint8_t>* fog = nullptr;
    std::uint64_t fog_version = 0;
    std::vector<MinimapDot> dots;        // the units, under the fog
    std::vector<MinimapDot> house_dots;  // the selected house's footprint and rally cross, over the fog
    std::array<int, 4> view{};  // the view rectangle in window pixels (left, top, right, bottom), minimap_view_rect
    std::vector<MinimapMarkDraw> marks;
    // The dots are drawn into a layer again when this changes (the app: once a tick); 0: as squares every frame.
    std::uint64_t dots_version = 0;
};


void minimap_draw(MinimapGraphics& g, const MinimapLayout& l, PictureFilter filter, const MinimapFrame& f);


void minimap_draw_buttons(MinimapGraphics& g, const MinimapLayout& l, PictureFilter filter, const HudButtonsState& st);

}  // namespace antanta::ui
