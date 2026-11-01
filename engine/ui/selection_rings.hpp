



#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "render/iso.hpp"
#include "render/live.hpp"
#include "render/sel_ring.hpp"
#include "sim/state.hpp"
#include "ui/hud.hpp"

namespace antanta::ui {

// The ring object (0x3020 bytes): centre, radii, the points and the phase.
struct SelectionRing {
    float cx = 0, cy = 0, cz = 0;                                                         // +0, +4, +8
    float rx = 0, ry = 0;
    std::uint16_t n = 0;
    std::vector<std::array<float, 3>> points = std::vector<std::array<float, 3>>(0x400);
    std::uint8_t phase = 1;
};


SelectionRing ring_make(const sim::GameState& s, float cx, float cy, float cz, float r);


void ring_build(const sim::GameState& s, SelectionRing& r);
void ring_set_radius(const sim::GameState& s, SelectionRing& r, std::int16_t radius);
void ring_set_centre(const sim::GameState& s, SelectionRing& r, float x, float y, float z);
void ring_next_phase(SelectionRing& r);

void ring_dots(const SelectionRing& r, ViewPos pos, std::vector<RingDot>* out);

struct SelectionRings {
    std::optional<SelectionRing> house;
    std::uint32_t resets = 0;            // Selection::ring_resets when the house ring was made
    std::optional<SelectionRing> ship;
};

// This frame's dots: the ship ring around each of your selected ships (centred where the hull is drawn, blend: the
// smooth camera's offsets) into `ships`, the house ring of your selected house into `house`.
void rings_dots(SelectionRings& rings, const sim::GameState& s, const Selection& sel, ViewPos pos,
                const DrawBlend* blend, std::vector<RingDot>* house, std::vector<RingDot>* ships);
// Once per game frame (a tick at normal speed, also while paused): the phases step on and the ring of a house that
// shoots grows by 50 up to its range, then starts again at 5. A ring not drawn yet is made first, as the frame's
// drawing would (a batch run draws only its last frame).
void rings_advance(SelectionRings& rings, const sim::GameState& s, const Selection& sel);

}  // namespace antanta::ui
