#include "ui/selection_rings.hpp"

#include <cmath>
#include <unordered_set>

#include "sim/movement.hpp"
#include "ui/house_panel.hpp"

namespace antanta::ui {
namespace {


bool range_type(std::uint16_t t) {
    return t == 7 || t == 1 || t == 0x15 || t == 0x12 || t == 0x11 || t == 0x16 || t == 0x13 || t == 0x14;
}

const sim::House* ring_house(const sim::GameState& s, const Selection& sel) {
    if (sel.player != s.my_player) return nullptr;
    const auto r = house_panel_house(s, sel);
    if (!r) return nullptr;
    return &s.players[r->player].houses[r->slot];
}

}  // namespace



static const sim::House* house_ring(SelectionRings& rings, const sim::GameState& s, const Selection& sel) {
    if (rings.house && rings.resets != sel.ring_resets) rings.house.reset();
    const sim::House* h = ring_house(s, sel);
    if (h == nullptr || rings.house) return h;
    const sim::HouseFootprint fp = sim::House_Footprint(s, sel.player, *h);
    const float a = static_cast<float>(-static_cast<int>(fp.b)) * 48.0f;
    const float b = static_cast<float>(fp.a) * 48.0f;
    const float d = (b - a) * 0.25f;
    const auto mx = static_cast<std::uint32_t>(static_cast<std::uint16_t>(h->map_x));
    const auto my = static_cast<std::uint32_t>(static_cast<std::uint16_t>(h->map_y));
    const float cx = (b + a) * 0.5f + static_cast<float>(static_cast<int>((mx * 2 - (~my & 1)) * 48)) + 48.0f;
    const float cy = (static_cast<float>(static_cast<int>(my * 24)) - d) + 48.0f;
    rings.house = ring_make(s, cx, cy, 0.0f, 10.0f);
    const double r0 = range_type(h->type) ? 5.0 : static_cast<double>(d) + d - 24.0;
    ring_set_radius(s, *rings.house, static_cast<std::int16_t>(sim::Ftol(r0)));
    rings.resets = sel.ring_resets;
    return h;
}

SelectionRing ring_make(const sim::GameState& s, float cx, float cy, float cz, float r) {
    SelectionRing ring;
    ring.cx = cx, ring.cy = cy, ring.cz = cz;
    ring.rx = r, ring.ry = r * 0.5f;
    ring_build(s, ring);
    return ring;
}

void ring_build(const sim::GameState& s, SelectionRing& r) {
    if (!(r.rx < 2048.0f)) return;
    r.n = static_cast<std::uint16_t>(sim::Ftol(static_cast<double>(r.rx) * 0.49951171875));
    // The step 2 pi / (n - 1) and the turn in the FPU's precision, each result stored as a float.
    const double step = static_cast<double>(6.2831854820251465f) / (static_cast<int>(r.n) - 1);
    const auto c = static_cast<float>(std::cos(step)), sn = static_cast<float>(std::sin(step));
    const double k1 = static_cast<double>(r.rx) / r.ry, k2 = static_cast<double>(r.ry) / r.rx;
    r.points[0][0] = r.rx, r.points[0][1] = 0.0f;
    for (std::size_t k = 1; k < r.n; ++k) {
        const double px = r.points[k - 1][0], py = r.points[k - 1][1];
        r.points[k][0] = static_cast<float>(px * c - py * k1 * sn);
        r.points[k][1] = static_cast<float>(py * c + px * k2 * sn);
    }
    const auto x_max = static_cast<float>(static_cast<int>(s.map.width * 96 - 100));
    const auto y_max = static_cast<float>(static_cast<int>(s.map.height * 24 - 50));
    for (std::size_t k = 0; k < r.n; ++k) {
        auto& p = r.points[k];
        p[0] += r.cx, p[1] = r.cy + p[1], p[2] = r.cz + p[2];
        if (100.0f < p[0] && p[0] < x_max && 50.0f < p[1] && p[1] < y_max) {
            p[2] = static_cast<float>(sim::GroundHeightAt(s, p[0], p[1])) + 26.0f;
        }
    }
}

void ring_set_radius(const sim::GameState& s, SelectionRing& r, std::int16_t radius) {
    r.rx = static_cast<float>(radius);
    r.ry = static_cast<float>(radius) * 0.5f;
    ring_build(s, r);
}

void ring_set_centre(const sim::GameState& s, SelectionRing& r, float x, float y, float z) {
    r.cx = x, r.cy = y, r.cz = z;
    ring_build(s, r);
}

void ring_next_phase(SelectionRing& r) {
    if (++r.phase > 3) r.phase = 0;
}

void ring_dots(const SelectionRing& r, ViewPos pos, std::vector<RingDot>* out) {
    const auto vx = static_cast<float>(view_x(pos)), vy = static_cast<float>(view_y(pos));
    for (std::size_t k = r.phase; k < r.n; k += 4) {
        const auto& p = r.points[k];
        const float x = p[0] - vx, y = (p[1] - vy) - p[2];
        // One depth for the whole quad: z = 0.99999 - (2 pz + scroll + 1200 + y + 4) / 4096, as a key y + 4 + 2 pz.
        out->push_back({x, y, y + 4.0f + 2.0f * p[2]});
    }
}

void rings_dots(SelectionRings& rings, const sim::GameState& s, const Selection& sel, ViewPos pos,
                const DrawBlend* blend, std::vector<RingDot>* house, std::vector<RingDot>* ships) {

    if (!rings.ship) rings.ship = ring_make(s, 0, 0, 0, 200.0f);
    if (!sel.units.empty() && sel.player == s.my_player) {
        std::unordered_set<std::uint64_t> picked;
        for (const sim::UnitRef& r : s.ships) {
            if (r.player != sel.player) continue;
            if (picked.empty()) {
                for (const sim::UnitRef& u : sel.units) picked.insert(std::uint64_t{u.id} << 32 | u.serial);
            }
            const sim::Unit* u = sim::Unit_Find(s, r);
            if (u == nullptr || picked.count(std::uint64_t{u->id} << 32 | u->serial) == 0) continue;
            const auto off = draw_blend_offset(blend, *u);
            ring_set_centre(s, *rings.ship, u->x + off[0], u->y + off[1], 0.0f);
            ring_dots(*rings.ship, pos, ships);
        }
    }
    if (house_ring(rings, s, sel) != nullptr) ring_dots(*rings.house, pos, house);
}

void rings_advance(SelectionRings& rings, const sim::GameState& s, const Selection& sel) {
    if (!rings.ship) rings.ship = ring_make(s, 0, 0, 0, 200.0f);
    if (!sel.units.empty() && sel.player == s.my_player) ring_next_phase(*rings.ship);
    const sim::House* h = house_ring(rings, s, sel);
    if (h == nullptr) return;
    ring_next_phase(*rings.house);
    if (range_type(h->type)) {
        const bool grow = rings.house->rx <= h->field_080;
        ring_set_radius(s, *rings.house,
                        grow ? static_cast<std::int16_t>(sim::Ftol(static_cast<double>(rings.house->rx) + 50.0)) : 5);
    }
}

}  // namespace antanta::ui
