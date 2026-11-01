// The water of the running game and what floats on it (docs/render.md 19): the animated water surface, its light


//
// Render only: nothing here is saved or read by the sim. The sim raises SimEvent::ShipWake where the original

// reads for the bubbles. Everything steps once per tick (the original once per drawn frame, one per tick at normal
// speed), so it stands while the game is paused, as the effect list (docs/bugs.md B21).
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "render/iso.hpp"
#include "render/raster.hpp"
#include "sim/events.hpp"

namespace antanta {

namespace sim {
struct GameState;
}


struct WaterPics {
    bool tried = false;
    std::vector<Texture> anim;     // Data/Misc/Water/Anim0/Anim_0.les, 16 frames of 256 x 256 by tag
    std::vector<Texture> foam;     // Data/Misc/Water/CoastWater0_0.les, 16 frames of 32 x 64
    std::array<Texture, 2> spots;  // Data/Misc/Water/LightSpots/LSpot0.444, LSpot1.444 (128 x 128)


    struct Set {
        std::vector<Texture> frames;
        int w = 0, h = 0, ax = 0, ay = 0;
    };
    Set bow, stern;  // Data/Misc/ShipWaves: Bow_wave_7.les (entry 0), Stern_wave_1.les (entry 1)
    Set bubbles;     // Data/Misc/ParticleAnims/WaterBubbles.les
    std::vector<std::string> warnings;
};

struct Water {
    WaterPics pics;
    float anim = 0;
    float foam = 0;  // the foam frame (+= 0.4f, back to 0 at 16)
    // Built on first use from the map's water grid and coast polygons (the map's water never changes in a game).
    bool built = false;
    std::vector<std::uint8_t> cell_type;  // per grid block (48 x 24): the water type (1 sea, 2 river: half the scale)
    struct Spot {
        int cx = 0, cy = 0, pic = 0;  // the centre in world pixels, LSpot index
    };
    std::vector<Spot> spots;
    // The light spots added up per 256 x 256 world tile (rgb per pixel; empty: no spot there), kept near the view.
    std::map<std::array<int, 2>, std::vector<float>> spot_tiles;
    struct Foam {
        std::array<float, 4> x{}, y{};  // corners v0..v3 (strip order), world pixels
        float px = 0, py = 0;           // the edge's first point (after the cut), the frame's phase
    };
    std::vector<Foam> foam_quads;

    struct Wave {
        float frame = 0;
        float size = 0;
        float rate = 0.04f;
        bool done = true;
        bool half = false;
        float angle = 0;
    };
    struct Wake {
        Wave bow, stern;
        std::uint64_t seen = 0;  // the step it was last found in the ship list
    };
    std::map<std::pair<std::uint8_t, std::uint32_t>, Wake> wakes;  // by player, Serial
    std::uint64_t steps = 0;


    struct WaveQuad {
        std::array<float, 4> x{}, y{}, v{};
        const Texture* tex = nullptr;
    };
    std::vector<WaveQuad> wave_quads;

    struct Bubble {
        float x = 0, y = 0;  // its point around the group's, turned
        std::int16_t delay = 0;
        float frame = 0;
    };
    struct Sink {
        float x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
        int life = 80;
        std::vector<Bubble> bubbles;
    };
    std::vector<Sink> sinks;

    std::map<std::string, std::optional<std::array<std::array<float, 3>, 2>>> text_points;
};

// Once per tick after the step (from effects_tick): the timers, the step's ShipWake and ShipSink events, the
// bubbles' step and the waves of every ship on the screen (the w x h view at pos).
// ship_slots: per player, whether any of its unit slots holds a ship type (players_with_ship_slots), or nullptr
// (then each player's slots are looked at here).
void water_tick(Water& w, const MapData& map, const WorldGraphics& g, const GameFileLoader& load,
                const sim::GameState& s, const std::vector<sim::SimEvent>& events, ViewPos pos, int vw, int vh,
                const std::vector<bool>* ship_slots = nullptr);



void water_draw(Water& w, const MapData& map, ViewPos pos, Target& rt);

// The triangle filler of the passes on the water (also the reflections of render/bridges and render/live, which only
// show on water pixels, ZFUNC EQUAL on the water's depth): every pixel centre inside the triangle (top-left rule, so
// a shared edge is filled once) on a water pixel of rows top..bottom - 1 takes the texel sample(u, v) gives (rgba in
// 0..1, point sampled), its colour times `tint` and its alpha times `alpha`, blended over the pixel.
struct WaterVertex {
    double x, y, u, v;
};
template <class Sample>
void water_triangle(Target& rt, const Sample& sample, std::array<float, 3> tint, float alpha, WaterVertex a,
                    WaterVertex b, WaterVertex c, int top, int bottom) {
    double det = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (!(std::fabs(det) >= 1e-9)) return;
    if (det < 0) std::swap(b, c), det = -det;
    const std::array<const WaterVertex*, 3> v = {&a, &b, &c};
    // Edge k runs from v[k] to v[k + 1]; a point is inside when every edge function is >= 0. An edge function of 0
    // counts only on a top edge (horizontal, going right: the inside is below it) or a left edge (going up on the
    // screen).
    std::array<bool, 3> owns{};
    for (std::size_t k = 0; k < 3; ++k) {
        const WaterVertex& p = *v[k];
        const WaterVertex& q = *v[(k + 1) % 3];
        owns[k] = (p.y == q.y && q.x > p.x) || q.y < p.y;
    }
    auto edge = [&](std::size_t k, double x, double y) {
        const WaterVertex& p = *v[k];
        const WaterVertex& q = *v[(k + 1) % 3];
        return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x);
    };
    const double fx0 = std::max(0.0, std::ceil(std::min({a.x, b.x, c.x})));
    const double fx1 = std::min(rt.width - 1.0, std::floor(std::max({a.x, b.x, c.x})));
    const double fy0 = std::max(static_cast<double>(top), std::ceil(std::min({a.y, b.y, c.y})));
    const double fy1 = std::min(bottom - 1.0, std::floor(std::max({a.y, b.y, c.y})));
    if (!(fx0 <= fx1 && fy0 <= fy1)) return;
    for (int py = static_cast<int>(fy0); py <= static_cast<int>(fy1); ++py) {
        for (int px = static_cast<int>(fx0); px <= static_cast<int>(fx1); ++px) {
            const std::size_t i = static_cast<std::size_t>(py) * static_cast<std::size_t>(rt.width) + px;
            if (!rt.water[i]) continue;
            bool inside = true;
            for (std::size_t k = 0; k < 3 && inside; ++k) {
                const double e = edge(k, px, py);
                inside = e > 0 || (e == 0 && owns[k]);
            }
            if (!inside) continue;
            const double wb = edge(2, px, py) / det, wc = edge(0, px, py) / det;
            std::array<float, 4> t =
                sample(a.u + wb * (b.u - a.u) + wc * (c.u - a.u), a.v + wb * (b.v - a.v) + wc * (c.v - a.v));
            const float al = t[3] * alpha;
            if (al <= 0) continue;
            float* dst = &rt.rgb[i * 3];
            for (std::size_t ch = 0; ch < 3; ++ch) dst[ch] = t[ch] * tint[ch] * al + dst[ch] * (1.0f - al);
        }
    }
}

// A point-sampled texel of a Texture (nibbles / 15) at u, v in 0..1 (clamped to the picture).
inline std::array<float, 4> texture_texel(const Texture& t, double u, double v) {
    const int x = std::clamp(static_cast<int>(std::floor(u * t.width)), 0, t.width - 1);
    const int y = std::clamp(static_cast<int>(std::floor(v * t.height)), 0, t.height - 1);
    const std::uint8_t* p = &t.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(t.width) + x) * 4];
    return {p[0] / 15.0f, p[1] / 15.0f, p[2] / 15.0f, p[3] / 15.0f};
}

}  // namespace antanta
