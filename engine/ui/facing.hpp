// The right-drag facing order (docs/interface.md D.1 "Right-drag facing arrow", slice I9): dragging with the right
// button held, more than 100 world pixels from the press point, with units selected, draws a yellow arrow and on the

//



#pragma once

#include <optional>
#include <vector>

#include "sim/state.hpp"
#include "ui/hud.hpp"
#include "ui/world_marks.hpp"

namespace antanta::ui {

constexpr float kFacingMinDrag = 100.0f;

struct ArrowLine {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// Is the drag from the press point (px, py) to (mx, my) long enough (a distance over 100 pixels)?
bool facing_drag_long(float px, float py, float mx, float my);


bool facing_selection_in_group(const sim::GameState& s, const Selection& sel);

// The lines of the arrow, from the press point to the mouse, in the same pixel space as the points (the original
// subtracts the view offset first; the direction and length do not depend on it).
std::vector<ArrowLine> facing_arrow(float px, float py, float mx, float my, bool formation);

// The arrow as 1 px quads of the marks atlas's white block in yellow (appended to `out`).
void world_marks_facing_arrow(float px, float py, float mx, float my, bool formation, std::vector<MarkQuad>* out);

// The order a right release gives: the selection's units, the direction of the drag as the original's 8 orientations
// and its length. Nothing when no unit is selected or the drag is not longer than 100 pixels. Points in world pixels.
std::optional<sim::Command> facing_order(const sim::GameState& s, const Selection& sel, float px, float py, float mx,
                                         float my);

}  // namespace antanta::ui
