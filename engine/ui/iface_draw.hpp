// The original's in-game interface drawn over the game view with SDL_Renderer and SDL3_ttf (docs/interface.md part A,
// slice I1): the four frame bevels, the resource bar, the electricity thermometer, the screen texts (event messages,
// "Build more houses.") and "Entente v 2.0". The layout and the rules are in ui/iface_layout.hpp; this file only
// loads the pictures and fonts and draws. Nothing here reads or writes the simulation: the app fills InterfaceFrame.
//
// Pictures come from the game's files at run time (Data/Interface/MapBevels.les, Data/Interface/interface.les, the
// magenta key transparent), scaled to the interface scale with the chosen filter once and cached; text is drawn from
// the font at the scale's size, never scaled as a picture.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/frontend.hpp"
#include "ui/iface_layout.hpp"

namespace antanta::ui {

struct GameInterface;
struct GameInterfaceFree {
    void operator()(GameInterface* g) const;
};
using GameInterfacePtr = std::unique_ptr<GameInterface, GameInterfaceFree>;

// `serif_fonts` are tried in order for Times New Roman Bold (the messages and "Entente v 2.0"), `sans_fonts` for the
// face Windows gives "BankGothic RUSS" (Arial: the resource numbers). Fails when a picture or a font is missing.
// `lang`: the app's display language, for the marker check of docs/languages.md 0 on every text drawn (none: no check).
GameInterfacePtr interface_open(SDL_Renderer* renderer, GameFiles files, const std::vector<std::string>& serif_fonts,
                                const std::vector<std::string>& sans_fonts, std::string* error,
                                Language* lang = nullptr);

// What one frame shows, filled by the app from the game state.
struct InterfaceFrame {
    std::array<std::string, 6> numbers;  // Gold, Food, Wood, Iron, Oil, People (resource_text)
    ThermometerFill fill;
    bool build_more_houses = false;  // UnitsAlive >= UnitCap (A.3)
    bool version = false;            // "Entente v 2.0" (version_visible)
};

// Draws the interface in window pixels over what the renderer holds (no logical presentation), in the original's
// order: the screen texts, then `after_texts` (the minimap, slice I2), the bevels, the thermometer, the numbers,
// "Entente v 2.0" (A.1).
void interface_draw(GameInterface& g, const HudLayout& layout, PictureFilter filter, const InterfaceFrame& frame,
                    const ScreenTexts& texts, const std::function<void()>& after_texts = {});

// The width of a text in the message font at scale 1 (for the screen text list's line breaks).
int interface_text_width(GameInterface& g, const std::string& text);

}  // namespace antanta::ui
