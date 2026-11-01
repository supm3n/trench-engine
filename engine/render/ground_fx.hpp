// Things on the ground that the original draws every frame and that are no part of the simulation (docs/render.md 12):



// Crater event (sim/events.hpp), the map cells come from the state's map.
//
// Timing: the original steps a crater and an animation once per drawn frame, and draws one frame per tick; ours steps
// them once per tick (ground_fx_step), so they also stand still while the game is paused.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "formats/map.hpp"
#include "render/raster.hpp"

namespace antanta {

struct GroundFxGraphics {



    struct CraterSet {
        int kind = 0;
        int w = 0, h = 0;
        std::vector<Texture> frames;
    };
    std::vector<CraterSet> craters;



    struct AnimFrame {
        int w = 0, h = 0;
        std::vector<std::array<float, 3>> rgb;
        std::vector<std::uint8_t> opaque;
    };
    struct Anim {
        int frames = 0;
        float speed = 0;
        int w = 0, h = 0;
        int raise = 0;
        std::vector<AnimFrame> pics;
    };
    std::array<Anim, 8> anims;  // by anim_index
    std::vector<std::string> warnings;
};

// Reads the crater archives and the eight cell animations. A missing or broken file leaves its part empty (not drawn)
// with a line in g.warnings.
using GroundFxLoader = std::function<std::optional<Bytes>(const std::string& path, std::string* error)>;
void load_ground_fx_graphics(GroundFxGraphics& g, const GroundFxLoader& load);

// 0x2581 oil -> 0, 0x2582 gold -> 1, 0x25de..0x25e3 -> 2..7; -1 for any other cell object.
int anim_index(int object);

// Ours (docs/bugs.md B18, render.md 12): the original steps a type's frame once per cell of it drawn, so the speed
// depends on the view. Ours steps each type kTypicalCells times a tick whatever is drawn: 1 for oil and gold (the
// median of the views that show one is 1, the quartiles 1 and 2), 25 for each field type (the median of the views
// that show it is mostly 21 to 36 in the owner's seven saves, about 25 over all of them).
constexpr std::array<int, 8> kTypicalCells = {1, 1, 25, 25, 25, 25, 25, 25};

// The view's state.
struct GroundFx {
    struct Crater {
        MapDecoration d;   // the saved fields (map .dat tail A layout); d.rest holds the quad as floats
        bool show = true;  // the last step's answer: drawn this frame
        // Ours (B7): the ground height under the sprite on an 8-pixel grid, made when it is first drawn.
        int gw = 0, gh = 0;
        std::vector<float> ground;
    };
    bool started = false;  // the map's list has been taken (ground_fx_step / ground_fx_draw start it)
    std::vector<Crater> craters;
    std::array<float, 8> frame{};
};



void crater_add(GroundFx& fx, const GroundFxGraphics& g, const MapData& m, std::int32_t x, std::int32_t y,
                std::uint8_t type);



struct GroundFxEvent {
    std::int32_t x = 0, y = 0;
    std::uint8_t type = 0;
};
void ground_fx_step(GroundFx& fx, const GroundFxGraphics& g, const MapData& m, const std::vector<GroundFxEvent>& added);


void ground_fx_draw(const GroundFxGraphics& g, GroundFx& fx, const MapData& m, int vx, int vy, Target& rt);



double screen_ground_height(const MapData& m, double x, double y);

}  // namespace antanta
