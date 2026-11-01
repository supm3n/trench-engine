




//
// Render only: nothing here writes the state. The trails are view state (the original's per drawn frame, ours per
// tick, standing while the game is paused, as the effect list: docs/bugs.md B21).
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

#include "formats/sprites.hpp"
#include "render/iso.hpp"
#include "sim/state.hpp"

namespace antanta {

struct DrawBlend;



struct ShellGraphics {
    bool tried = false;
    std::array<Image, 9> pics;  // Data/Misc/Snar_S, Snar_M, Snar_L, Snar_gr, Snar_bo, torpeda, Oskolok_1, _2, _3 .444
    Image trace;                // Data/Misc/Trace.bmp, opaque (no magenta in it)
    std::vector<std::string> warnings;
};
void shell_graphics_load(ShellGraphics& g, const GameFileLoader& load);


int shell_picture(int kind);
float shell_height_factor(int kind);

float shell_draw_height(const sim::Shell& sh, float cz);





struct ShellTrails {
    struct Point {
        float x = 0, y = 0, z = 0;
        int alpha = 128;
    };
    struct Trail {
        std::array<float, 6> key{};  // the shell's x0, y0, z0, x1, y1, z1
        std::uint8_t kind = 0;
        bool attached = true;
        std::uint32_t made = 0;    // the tick the shell was first seen in
        std::deque<Point> points;
    };
    std::vector<Trail> trails;  // oldest first
    std::uint32_t stepped_tick = 0;
    bool stepped = false;
};

void shell_trails_step(ShellTrails& t, const sim::GameState& s);



struct ShellQuad {
    const Image* pic = nullptr;
    std::array<float, 4> x{}, y{};
    float alpha = 1;
};
// The shells' pictures, then the trails' pieces, in the original's order, for the view at pos. blend: the shells drawn
// between the last two ticks (DrawBlend), nullptr as they are. trails: nullptr draws no trails.
void shell_quads(ShellGraphics& g, const GameFileLoader& load, const sim::GameState& s, ViewPos pos,
                 const DrawBlend* blend, const ShellTrails* trails, std::vector<ShellQuad>* out);
// Draws the quads into the finished RGBA8 picture (w x h), alpha blended, no depth test.
void shell_quads_draw(std::span<const ShellQuad> quads, std::vector<std::uint8_t>& rgba, int w, int h);

}  // namespace antanta
