// The fog of war over the game view (docs/fog.md 7.2 and 7.3), the part without SDL. The original projects its cell
// mask with its own scales, 5 half rows (about 116 px at 1280 x 1024) too high and 4.8 % too narrow; ours puts the
// mask on the cells the world draws (the owner's rule to fix the original's bugs, docs/bugs.md). The edge is the
// original's look measured on its pictures: black within 48 px of the mask edge inside the lit area, then the alpha
// ramp of SuperFog.tga out to 144 px, clear beyond; outside the mask black.
//
// The fog is computed on a grid of samples every `step` world pixels (aligned to the world, so it does not shimmer
// while the view scrolls) and drawn as a small texture stretched with a linear filter.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "sim/fog.hpp"
#include "sim/movement.hpp"

namespace antanta::sim {
struct GameState;
}

namespace antanta::ui {

constexpr int kFogStep = 8;          // world pixels per sample
constexpr double kFogBlack = 48.0;
constexpr double kFogClear = 144.0;

// The 32 alpha values of SuperFog.tga's column, opaque first (index 0 at curve A, 31 at curve B).
using FogRamp = std::array<std::uint8_t, 32>;
// The ramp from the decoded SuperFog.tga (4 x 32, RGBA): the first column, turned so that it starts opaque.
FogRamp fog_ramp(const std::vector<std::uint8_t>& rgba, int width, int height);

// The fog's opacity (0 clear .. 255 black) at d world pixels inside the lit area from the mask edge.
std::uint8_t fog_alpha(const FogRamp& ramp, double d);

// The fog over a view: alpha per sample, `cols` x `rows` samples, sample (i, j) at world pixel (x0 + i step,
// y0 + j step); it covers the view (view_x, view_y, view_w x view_h world pixels) with one sample to spare on every
// side.
struct FogView {
    int x0 = 0, y0 = 0, cols = 0, rows = 0, step = kFogStep;
    std::vector<std::uint8_t> alpha;
};
// The screen picks of the last call's samples (a pure function of the map's heights and size), kept from frame to
// frame: a scrolling view or a fog rebuild picks only the samples it has not picked yet (the picks were nearly all of
// the time, about 4 ms a frame at 1280 x 1024). A sample is taken from here only when the heights its pick read are
// unchanged, so the result is the same as without it.
struct FogPicks {
    int step = 0, map_w = 0, map_h = 0;
    int gx0 = 0, gy0 = 0, gw = 0, gh = 0;    // the samples
    int cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;  // the cells whose heights they read (end exclusive)
    std::vector<sim::Cell> cells;            // per sample, row by row
    std::vector<std::uint8_t> heights;       // the heights of those cells when picked (0 off the map)
};

// Empty (cols 0) when the fog is off or not built yet: nothing to draw.
// The mask is s.fog's, each sample takes the cell the world shows there (the screen pick with the heights).
// `picks`, when given, lends the last call's picks and keeps this call's for the next.
FogView fog_view(const sim::GameState& s, const FogRamp& ramp, int view_x, int view_y, int view_w, int view_h,
                 int step = kFogStep, FogPicks* picks = nullptr);

}  // namespace antanta::ui
