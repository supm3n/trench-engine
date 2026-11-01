
// one circle per unit and standing house of MyPlayer and of every player that shares sight with MyPlayer, with the
// sight of its type, into the minimap fog surface and the view mask; there is no explored memory (ground that falls
// out of sight is black again) and no RNG (docs/fog.md 3 and 4.3).
//
// Here the sim keeps the list of those circles, rebuilt by step() on every tick that is a multiple of 30 and once
// after a load or a new game (the original runs the update with the tick forced to 0 then, docs/fog.md 5). The
// masks are pure functions of that list, made by the view. GameState::fog is scratch: never saved or hashed, because

// byte only, docs/fog.md 4.1), and the original's save does not hold it either.
#pragma once

#include <cstdint>
#include <vector>

namespace antanta::sim {

struct GameState;

struct FogCircle {
    std::int32_t map_x = 0, map_y = 0;  // the cell (MapX, MapY) the view ellipse is centred on
    float x = 0, y = 0;                 // the minimap centre in world pixels (a unit's X, Y; a house's anchor point)
    std::uint8_t sight = 0;             // the sight in cells (a dying unit's reduced sight), 1..39
    bool unit = false;                  // units stamp sight - 1 on the minimap, houses the sight (docs/fog.md 7.1)
};

struct Fog {
    bool built = false;      // a rebuild ran since the load or the start
    bool on = false;         // fog_553ae0 at the rebuild: off, nothing is fogged
    std::uint32_t tick = 0;  // the tick of the rebuild
    std::vector<FogCircle> circles;
};




void Fog_Rebuild(GameState& s);





std::vector<std::uint8_t> Fog_CellMask(const Fog& fog, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                       std::int32_t y1);





std::vector<std::uint8_t> Fog_MinimapMask(const Fog& fog, std::int32_t map_w, std::int32_t map_h, std::int32_t n);

}  // namespace antanta::sim
