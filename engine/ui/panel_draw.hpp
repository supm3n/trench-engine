// Drawing of the panel toolkit (ui/panel.hpp, docs/interface.md B.2, B.3, B.6, slice I4) with SDL_Renderer and
// SDL3_ttf, over what the renderer holds, in window pixels. Pictures come from the game's archives at run time (by name
// or entry id, the magenta key transparent): a widget's frame is first stretched to the widget's size in the original's
// pixels (DirectDraw's stretching Blt, nearest), then scaled to the interface scale with the picture filter, once, and
// cached. Text is drawn from the font at the scale's size, never scaled as a picture.
#pragma once

#include <SDL3/SDL.h>

#include <memory>
#include <string>
#include <vector>

#include "ui/frontend.hpp"
#include "ui/iface_layout.hpp"
#include "ui/panel.hpp"

namespace antanta::ui {

struct PanelGraphics;
struct PanelGraphicsFree {
    void operator()(PanelGraphics* g) const;
};
using PanelGraphicsPtr = std::unique_ptr<PanelGraphics, PanelGraphicsFree>;

// `serif_fonts` are tried in order for Times New Roman Bold, `sans_fonts` for the face of "BankGothic RUSS" (Arial), as
// for interface_open. Fails when no font opens; a missing picture is logged once and left out when drawn.
PanelGraphicsPtr panel_graphics_open(SDL_Renderer* renderer, GameFiles files,
                                     const std::vector<std::string>& serif_fonts,
                                     const std::vector<std::string>& sans_fonts, std::string* error,
                                     Language* lang = nullptr);  // the marker check, as interface_open

// Draws the panel on a window of w x h pixels at interface scale k: the widgets in order, each button in its state
// (panel_state), the shadow overlay on disabled ones, the red frame on active ones, captions and labels with their
// shadow. The tooltip is not drawn here: it belongs to the screen text list (panel_tooltip).
void panel_draw(PanelGraphics& g, const Panel& p, const PanelInput& in, int w, int h, int k, PictureFilter filter);

}  // namespace antanta::ui
