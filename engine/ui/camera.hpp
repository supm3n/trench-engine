// The game view's camera and the game clock of the app, without SDL.
//
// Smooth scrolling (ours, owner 2026-10-15, docs/interface.md D.3 "deliberate difference"): the original moves the


// camera is a point in world pixels that moves with a velocity, eased in and out over kEaseSeconds, and is drawn at
// whole pixels. Its speed is the original's step rate at the same scroll slider (scroll_step_ms). The camera is view
// state only: the simulation never reads it (docs/view-dependence.md 4.5).
//
// The tick clock: how many game ticks a frame runs. It depends on the elapsed time, the tick period, pause and the
// step key only, never on scrolling (the original runs ticks faster while it scrolls, docs/bugs.md B10).
#pragma once

#include "formats/map.hpp"
#include "render/iso.hpp"

namespace antanta::ui {

constexpr double kStepX = 96, kStepY = 48;
constexpr double kEaseSeconds = 0.12;       // from rest to full speed, and from full speed to rest
constexpr double kMaxFrameSeconds = 0.1;    // a longer frame (a stall) moves the camera as far as this one







double scroll_step_ms(int slider, double frame_ms);

// Full scroll speed in world pixels per second along each axis: one step (kStepX, kStepY) per scroll_step_ms.
struct ScrollSpeed {
    double x = 0, y = 0;
};
ScrollSpeed scroll_speed(int slider, double frame_ms);




struct CameraBounds {
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
};
CameraBounds camera_bounds(const MapData& map, int view_w, int view_h);

struct Camera {
    double x = 0, y = 0;    // the view's top-left corner, world pixels
    double vx = 0, vy = 0;  // world pixels per second
};

// One frame of dt seconds (capped at kMaxFrameSeconds): each velocity moves toward its target by at most
// accel * dt (linear ease), and the position follows the exact integral of that velocity, so where the camera is
// after a given time does not depend on how the time was cut into frames. At a limit the position stops there and
// that axis's velocity drops to 0.
void camera_advance(Camera& c, double target_vx, double target_vy, double accel_x, double accel_y, double dt,
                    const CameraBounds& b);
// Scrolling: dir_x, dir_y in -1, 0, 1 (edge or arrow keys; 0, 0 eases out), full speed `speed`, eased over
// kEaseSeconds.
void camera_scroll(Camera& c, int dir_x, int dir_y, ScrollSpeed speed, double dt, const CameraBounds& b);
// An instant move (a minimap click, a script's camera, the start view): the corner at (x, y) within the limits,
// at rest. The middle-mouse drag calls it on every mouse move (the ground follows the cursor; release: no ease).
void camera_jump(Camera& c, double x, double y, const CameraBounds& b);
// The corner the view is drawn at: the camera rounded to whole world pixels. Every screen to world mapping of a
// frame uses this same ViewPos (hud.hpp screen_to_world and the pickers).
ViewPos camera_view(const Camera& c);
// Keeps the camera inside b (a new view size); an axis that had to move stops (its velocity drops to 0).
void camera_clamp(Camera& c, const CameraBounds& b);

// ---- the wheel zoom (ours, docs/decisions.md 41) ----
//
// The world zoom is output pixels per world pixel. The Game options row gives the whole zoom a game starts with and
// the reset key goes back to; the mouse wheel and the keys change it for the session, in steps of 2^(1/7) (about
// 10 %: seven steps double it, so 1, 2 and 4 are hit exactly), eased over about kZoomEaseSeconds and anchored on a
// screen point (the world point under the pointer stays under it). Zoom in up to kZoomMax; zoom out until the view
// covers kZoomMaxArea world pixels (2560 x 1440), in a network game for every player alike (a bigger Options zoom out
// is pushed in to the cap), alone down to the Options zoom where that shows more.
constexpr double kZoomMax = 4;
constexpr double kZoomMaxArea = 2560.0 * 1440.0;
constexpr double kZoomStepsPerDouble = 7;
constexpr double kZoomEaseSeconds = 0.12;

// The view's size in world pixels for an output of `out` pixels at zoom z (whole pixels, covering the output).
int zoom_view_size(int out, double z);
// The smallest zoom allowed for an output of out_w x out_h pixels and the Options zoom `start` (see above).
double zoom_min(int out_w, int out_h, int start, bool network);
// z within zoom_min .. kZoomMax (the cap wins where zoom_min is above kZoomMax).
double zoom_clamp(double z, double zmin);
// z after `steps` wheel steps (fractional from a trackpad; in > 0), a whole zoom when within rounding of one.
double zoom_step(double z, double steps);
// One frame of dt seconds of the ease from z toward target (exponential in the log of the zoom, so it does not depend
// on the frame rate and a new target mid-way just bends it); reaches the target exactly when close.
double zoom_ease(double z, double target, double dt);
// The camera after the zoom went from z_old to z_new about the screen point (sx, sy) in output pixels: the world
// point there stays there (the caller then keeps it inside the new view's bounds, camera_clamp).
void camera_zoom_about(Camera& c, double sx, double sy, double z_old, double z_new);

// The game clock (SDL_AppIterate): elapsed real time builds up while not paused; each tick of the period takes its
// share; the step key runs one tick even while paused; at most kMaxTicksPerFrame per frame, and a backlog of more
// than 4 periods after them is dropped (too slow to keep up).
constexpr int kMaxTicksPerFrame = 8;
struct TickClock {
    double accum_ms = 0;
    int step_once = 0;  // the step key's ticks still to run
    int ran = 0;        // ticks run in this frame
};
void tick_clock_begin(TickClock& c, double elapsed_ms, bool paused);
// True when one more tick is due in this frame; the caller runs it, then calls tick_clock_ran (a step key pressed
// by that tick's script events is taken by it, as before).
bool tick_clock_due(const TickClock& c, double period_ms, bool paused);
void tick_clock_ran(TickClock& c, double period_ms);
void tick_clock_end(TickClock& c, double period_ms);

}  // namespace antanta::ui
