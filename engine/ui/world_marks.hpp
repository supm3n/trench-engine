// The marks in the world (docs/interface.md D.2, slice I9), as textured quads in view pixels; ui/world_marks_draw
// draws them in one batch. No SDL here.
//

//   Data\Misc\Select.444 (an L in the top left corner of a 4 x 4 picture), the texture flipped per corner, centred on
//   the corners of the unit's mark frame (render/live unit_mark_frame), so each reaches 2 px outside it.






//   owner's colour.

//
// The atlas: one 64 x 16 picture, rows 0..7 Numbers2.bmp, (0, 8) Select.444, (8, 8) a white 4 x 4 block for the bars.
// Quads are snapped to whole pixels as the original's Direct3D covers them (a pixel whose centre, at whole
// coordinates, lies in [x0, x1)).
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include "render/live.hpp"
#include "sim/state.hpp"

namespace antanta::ui {

constexpr int kMarksAtlasW = 64, kMarksAtlasH = 16;
constexpr int kMarksSelectX = 0, kMarksSelectY = 8;  // Select.444 in the atlas
constexpr int kMarksWhiteX = 8, kMarksWhiteY = 8;    // the white block

struct MarkQuad {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // view pixels, whole numbers
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // atlas pixels (flipped when u1 < u0 or v1 < v0)
    std::uint32_t argb = 0xffffffff;
    // A unit's mark (world_marks_units): TUnit_Draw draws it in the unit pass with the depth test at `key`, the
    // corners before the unit's sprite (`before`), the bars and digits after it (render/live UnitMarkQuad).
    std::uint32_t unit = 0;  // the unit's serial; 0: drawn over the finished picture
    float key = 0;
    bool before = false;
};

struct MarksInput {
    ViewPos pos;
    int view_w = 0, view_h = 0;
    const DrawBlend* blend = nullptr;  // units between the last two ticks, as render_live_units draws them
    const std::vector<sim::UnitRef>* selected = nullptr;  // the app's selection
    bool show_health = false;                             // the `~` switch
};

// The opaque bounding boxes of the house pictures, found once per picture.
using HouseBoxCache = std::map<const HouseGraphics*, std::array<int, 4>>;

// The unit marks of one frame into `out` (appended): corners, then with show_health the bars and group numbers, unit
// by unit in drawing order (bottom of the mark frame, then x), so a front unit's bars lie over a back unit's.
void world_marks_units(const WorldGraphics& g, UnitSprites& sprites, const GameFileLoader& load,
                       const sim::GameState& s, const MarksInput& in, std::vector<MarkQuad>* out);
// The house bars (show_health only) of the houses the static layer draws (lv), into `out`.
void world_marks_houses(const WorldGraphics& g, const LiveView& lv, const sim::GameState& s, ViewPos pos, int view_w,
                        int view_h, HouseBoxCache& cache, std::vector<MarkQuad>* out);
// The box select rectangle from (x0, y0) to (x1, y1) in view pixels: four 1 px quads of the white block in black.
void world_marks_box(float x0, float y0, float x1, float y1, std::vector<MarkQuad>* out);

}  // namespace antanta::ui
