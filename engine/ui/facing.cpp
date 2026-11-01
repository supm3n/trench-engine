#include "ui/facing.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>

#include "sim/movement.hpp"

namespace antanta::ui {
namespace {

constexpr std::uint32_t kYellow = 0xffffff00;     // the vertex colour of both arrows
constexpr double kPi = 3.14159274101257324;
constexpr double kHalfPi = 1.57079637050628662;
constexpr double kTwoPi = 6.28318548202514648;
constexpr double kEighth = 0.785398185253143311;
constexpr float kFrameHalf = 20.0f;
constexpr float kHeadAhead = 80.0f;

struct V {
    float x, y;
};


constexpr V kHead[5] = {{-5, -27}, {-12, -34}, {0, 0}, {12, -34}, {5, -27}};


V rotate(V v, float a) {
    const double s = std::sin(static_cast<double>(a)), c = std::cos(static_cast<double>(a));
    return {static_cast<float>(c * v.x - s * v.y), static_cast<float>(c * v.y + s * v.x)};
}

V add(V o, V v) { return {o.x + v.x, o.y + v.y}; }

float wrap(double a) { return static_cast<float>(a < 0 ? a + kTwoPi : a); }

// A 1 px line from p to q as quads: Bresenham between the rounded end points, a run of pixels along the long axis
// that share the other coordinate in one quad.
void line_quads(V p, V q, std::vector<MarkQuad>* out) {
    int x0 = static_cast<int>(std::floor(p.x + 0.5f)), y0 = static_cast<int>(std::floor(p.y + 0.5f));
    const int x1 = static_cast<int>(std::floor(q.x + 0.5f)), y1 = static_cast<int>(std::floor(q.y + 0.5f));
    const int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    const bool flat = dx >= -dy;
    int err = dx + dy;
    int lo = 0, hi = 0, fixed = 0;  // the current run: the long axis from lo to hi, the other axis fixed
    bool open = false;
    auto flush = [&] {
        if (!open) return;
        const auto a = static_cast<float>(lo), b = static_cast<float>(hi + 1), f = static_cast<float>(fixed);
        out->push_back({flat ? a : f, flat ? f : a, flat ? b : f + 1, flat ? f + 1 : b, kMarksWhiteX + 1.0f,
                        kMarksWhiteY + 1.0f, kMarksWhiteX + 3.0f, kMarksWhiteY + 3.0f, kYellow});
        open = false;
    };
    for (;;) {
        const int along = flat ? x0 : y0, across = flat ? y0 : x0;
        if (open && across != fixed) flush();
        if (!open) lo = hi = along, fixed = across, open = true;
        lo = std::min(lo, along), hi = std::max(hi, along);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) err += dy, x0 += sx;
        if (e2 <= dx) err += dx, y0 += sy;
    }
    flush();
}

}  // namespace

bool facing_drag_long(float px, float py, float mx, float my) {
    const float dx = px - mx, dy = py - my;
    return kFacingMinDrag < std::sqrt(dx * dx + dy * dy);
}

bool facing_selection_in_group(const sim::GameState& s, const Selection& sel) {
    if (sel.player >= s.players.size() || s.players[sel.player].is_lost != 0) return false;
    for (const sim::UnitRef& r : sel.units) {
        const sim::Unit* u = sim::Unit_Find(s, r);
        if (u != nullptr && u->in_group) return true;
    }
    return false;
}

std::vector<ArrowLine> facing_arrow(float px, float py, float mx, float my, bool formation) {
    const double dx = static_cast<double>(mx) - px, dy = static_cast<double>(my) - py;
    std::vector<ArrowLine> out;
    auto line = [&out](V a, V b) { out.push_back({a.x, a.y, b.x, b.y}); };
    const V press{px, py}, mouse{mx, my};
    if (!formation) {
        const float a = wrap(std::atan2(dy, dx) - kHalfPi);
        V h[5];
        for (int i = 0; i < 5; ++i) h[i] = add(mouse, rotate(kHead[i], a));
        for (int i = 0; i < 4; ++i) line(h[i], h[i + 1]);
        const V left = add(press, rotate({-15, 0}, a)), right = add(press, rotate({15, 0}, a));
        const V tail = add(press, rotate({0, 40}, a));
        line(left, h[0]);
        line(right, h[4]);
        line(right, tail);
        line(left, tail);
        return out;
    }


    const auto dir = sim::Orient_ToPoint(px, py, mx, my);
    const float a = wrap(kPi - static_cast<double>(dir) * kEighth);
    const auto len = static_cast<float>(std::sqrt(dx * dx + dy * dy));
    const float off = kFrameHalf + kHeadAhead;
    V h[5];
    for (int i = 0; i < 5; ++i) h[i] = add(press, rotate({kHead[i].x, kHead[i].y + off}, a));
    for (int i = 0; i < 4; ++i) line(h[i], h[i + 1]);
    const V left = add(press, rotate({-15, kFrameHalf}, a)), right = add(press, rotate({15, kFrameHalf}, a));
    line(left, h[0]);
    line(right, h[4]);
    const V c0 = add(press, rotate({-len, kFrameHalf}, a)), c1 = add(press, rotate({len, kFrameHalf}, a));
    const V c2 = add(press, rotate({-len, -kFrameHalf}, a)), c3 = add(press, rotate({len, -kFrameHalf}, a));
    line(left, c0);
    line(right, c1);
    line(c0, c2);
    line(c1, c3);
    line(c3, c2);
    return out;
}

void world_marks_facing_arrow(float px, float py, float mx, float my, bool formation, std::vector<MarkQuad>* out) {
    for (const ArrowLine& l : facing_arrow(px, py, mx, my, formation)) line_quads({l.x0, l.y0}, {l.x1, l.y1}, out);
}

std::optional<sim::Command> facing_order(const sim::GameState& s, const Selection& sel, float px, float py, float mx,
                                         float my) {
    if (sel.player >= s.players.size() || sel.units.empty() || !facing_drag_long(px, py, mx, my)) return std::nullopt;
    const float dx = px - mx, dy = py - my;
    sim::Command c;
    c.kind = sim::Command::Kind::FacingOrder;
    c.player = sel.player;
    sim::OrderArg p, d, l;
    p.raw = sel.player;
    d.raw = sim::Orient_ToPoint(px, py, mx, my);
    l.raw = std::bit_cast<std::uint32_t>(std::sqrt(dx * dx + dy * dy));
    c.args = {p, d, l};
    c.units = sel.units;
    return c;
}

}  // namespace antanta::ui
