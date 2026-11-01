#include "ui/camera.hpp"

#include <algorithm>
#include <cmath>

#include "ui/iface_layout.hpp"

namespace antanta::ui {

double scroll_step_ms(int slider, double frame_ms) { return frame_ms + 100.0 / std::clamp(slider, 1, 100); }

ScrollSpeed scroll_speed(int slider, double frame_ms) {
    const double step_s = scroll_step_ms(slider, frame_ms) / 1000.0;
    return {kStepX / step_s, kStepY / step_s};
}

CameraBounds camera_bounds(const MapData& map, int view_w, int view_h) {
    const ViewPos hi = clamp_camera(map, view_w, view_h, ViewPos{1 << 20, 1 << 20});
    CameraBounds b;
    b.x1 = hi.col * 96.0, b.y1 = hi.row * 24.0;
    b.x0 = std::min(96.0, b.x1), b.y0 = std::min(24.0, b.y1);
    return b;
}

namespace {

// One axis over dt: v moves toward `target` at `accel`; returns the distance (the exact integral of v).
double axis_advance(double& v, double target, double accel, double dt) {
    if (accel <= 0) {
        v = target;
        return target * dt;
    }
    const double diff = target - v;
    const double t = std::fabs(diff) / accel;  // time to reach the target
    if (t >= dt) {
        const double v1 = v + (diff > 0 ? accel : -accel) * dt;
        const double d = (v + v1) * 0.5 * dt;
        v = v1;
        return d;
    }
    const double d = (v + target) * 0.5 * t + target * (dt - t);
    v = target;
    return d;
}

}  // namespace

void camera_advance(Camera& c, double target_vx, double target_vy, double accel_x, double accel_y, double dt,
                    const CameraBounds& b) {
    dt = std::clamp(dt, 0.0, kMaxFrameSeconds);
    c.x += axis_advance(c.vx, target_vx, accel_x, dt);
    c.y += axis_advance(c.vy, target_vy, accel_y, dt);
    if (c.x <= b.x0 || c.x >= b.x1) c.x = std::clamp(c.x, b.x0, b.x1), c.vx = 0;
    if (c.y <= b.y0 || c.y >= b.y1) c.y = std::clamp(c.y, b.y0, b.y1), c.vy = 0;
}

void camera_scroll(Camera& c, int dir_x, int dir_y, ScrollSpeed speed, double dt, const CameraBounds& b) {
    camera_advance(c, dir_x * speed.x, dir_y * speed.y, speed.x / kEaseSeconds, speed.y / kEaseSeconds, dt, b);
}

void camera_jump(Camera& c, double x, double y, const CameraBounds& b) {
    c.x = std::clamp(x, b.x0, b.x1), c.y = std::clamp(y, b.y0, b.y1);
    c.vx = c.vy = 0;
}

ViewPos camera_view(const Camera& c) {
    return view_pos_at(static_cast<int>(std::floor(c.x + 0.5)), static_cast<int>(std::floor(c.y + 0.5)));
}

void camera_clamp(Camera& c, const CameraBounds& b) {
    if (c.x < b.x0 || c.x > b.x1) c.x = std::clamp(c.x, b.x0, b.x1), c.vx = 0;
    if (c.y < b.y0 || c.y > b.y1) c.y = std::clamp(c.y, b.y0, b.y1), c.vy = 0;
}

int zoom_view_size(int out, double z) {
    return std::max(1, static_cast<int>(std::ceil(static_cast<double>(out) / z - 1e-9)));
}

double zoom_min(int out_w, int out_h, int start, bool network) {
    const double cap = std::sqrt(static_cast<double>(out_w) * out_h / kZoomMaxArea);
    return network ? cap : std::min(cap, static_cast<double>(start));
}

double zoom_clamp(double z, double zmin) { return std::max(zmin, std::min(kZoomMax, z)); }

double zoom_step(double z, double steps) {
    const double r = z * std::exp2(steps / kZoomStepsPerDouble);
    const double whole = std::round(r);
    return std::fabs(r - whole) < 1e-9 * whole ? whole : r;
}

double zoom_ease(double z, double target, double dt) {
    if (z <= 0 || target <= 0) return target;
    dt = std::clamp(dt, 0.0, kMaxFrameSeconds);
    // About 95 % of the way in kZoomEaseSeconds; the last 0.05 % is a jump nobody sees.
    const double gap = std::log(target / z) * std::exp(-dt * 3 / kZoomEaseSeconds);
    return std::fabs(gap) < 5e-4 ? target : target / std::exp(gap);
}

void camera_zoom_about(Camera& c, double sx, double sy, double z_old, double z_new) {
    c.x += sx / z_old - sx / z_new;
    c.y += sy / z_old - sy / z_new;
}

void tick_clock_begin(TickClock& c, double elapsed_ms, bool paused) {
    c.ran = 0;
    if (!paused) c.accum_ms += elapsed_ms;
}

bool tick_clock_due(const TickClock& c, double period_ms, bool paused) {
    return c.ran < kMaxTicksPerFrame && ((!paused && c.accum_ms >= period_ms) || c.step_once > 0);
}

void tick_clock_ran(TickClock& c, double period_ms) {
    if (c.step_once > 0) {
        --c.step_once;
    } else {
        c.accum_ms -= period_ms;
    }
    ++c.ran;
}

void tick_clock_end(TickClock& c, double period_ms) {
    if (c.accum_ms > 4 * period_ms) c.accum_ms = 0;
}

}  // namespace antanta::ui
