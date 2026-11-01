#include "render/effects_emit.hpp"

#include <algorithm>

#include "render/ground_fx.hpp"
#include "render/vehicle_dust.hpp"
#include "sim/movement.hpp"
#include "sim/state.hpp"

namespace antanta {

EffectSpawn effect_spawn(const sim::SimEvent::EffectArgs& a) {
    EffectSpawn e;
    e.kind = a.kind, e.type = a.type;
    e.x = a.x, e.y = a.y, e.z = a.z;
    e.delay = a.delay, e.rise = a.rise, e.rise_step = a.rise_step;
    e.frame = a.frame, e.speed = a.speed, e.ground = a.ground;
    return e;
}

int effect_rand(Effects& fx) {
    fx.rand_state = fx.rand_state * 214013u + 2531011u;
    return static_cast<int>(fx.rand_state >> 16 & 0x7fff);
}

bool effect_point_in_view(float x, float y, ViewPos pos, int w, int h) {
    const auto vx = static_cast<float>(view_x(pos)), vy = static_cast<float>(view_y(pos));
    return vx <= x && x <= vx + static_cast<float>(w) && vy <= y && y <= vy + static_cast<float>(h);
}

bool unit_in_view(const WorldGraphics& g, int nation, int type, float x, float y, float lift, ViewPos pos, int w,
                  int h) {
    const auto& params = g.params[static_cast<std::size_t>(nation % 6)];
    if (!params || type % 40 >= static_cast<int>(params->units.size())) return false;
    const UnitParams& up = params->units[static_cast<std::size_t>(type % 40)];
    const float x0 = (x - static_cast<float>(view_x(pos))) - (static_cast<float>(up.anchor_x) + up.width * 0.5f);
    const float y0 =
        ((y - static_cast<float>(view_y(pos))) - lift) - (static_cast<float>(up.anchor_y) + up.height * 0.5f);
    return x0 < static_cast<float>(w) && y0 < static_cast<float>(h) && x0 + up.width > 0 && y0 + up.height > 0;
}

namespace {



int field_lift(const sim::GameState& s, int x, int y) {
    const sim::Cell c = sim::WorldToCell(static_cast<float>(x), static_cast<float>(y));
    if (c.x < 0 || c.y < 0 || c.x >= s.map.width || c.y >= s.map.height) return 0;
    const std::uint16_t obj = s.map.objects[static_cast<std::size_t>(c.y) * s.map.width + c.x];
    const bool wet =
        sim::Obstacle_UnderCell(s.map, c.x, c.y) && (obj < 0x65 || obj > 10999 || (obj >= 9000 && obj < 0x25e4));
    return !wet && obj > 0x25dd && obj < 0x25e4 ? 0x28 : 0;
}

}  // namespace

void effects_dust(Effects& fx, EffectGraphics& eg, const GameFileLoader& load, const WorldGraphics& g,
                  const sim::GameState& s, const sim::SimEvent& ev, ViewPos pos, int w, int h) {
    const sim::SimEvent::DustArgs& d = ev.dust;
    if (!unit_in_view(g, ev.nation, ev.type, ev.x, ev.y, d.z, pos, w, h)) return;
    const auto z = static_cast<int>(d.z);
    auto add = [&](EffectSpawn e) {
        e.z += field_lift(s, e.x, e.y);
        effects_add(fx, eg, load, e);
    };
    if (d.what == 2) {
        std::uint32_t& gate = fx.dust_gate[d.serial];
        if (gate > s.tick) return;
        EffectSpawn e;
        e.type = 0x29;
        e.z = z - effect_rand(fx) % 5 - 3;
        e.y = static_cast<int>(ev.y), e.x = static_cast<int>(ev.x);
        e.rise_step = 1, e.ground = 1, e.delay = 2;
        add(e);
        gate = s.tick + 2;
        return;
    }
    const auto pts = vehicle_dust_points(ev.nation, ev.type, d.size, d.orient);
    if (!pts) return;
    // A puff at point p: x and y each a little to the left and up (rand() % 3, y first).
    auto puff = [&](const DustPoint& p, std::uint8_t kind, std::uint8_t type) {
        EffectSpawn e;
        e.kind = kind, e.type = type, e.z = z;
        e.y = static_cast<int>(static_cast<double>(p.y) + ev.y);
        e.x = static_cast<int>(static_cast<double>(p.x) + ev.x);
        return e;
    };
    if (d.what == 1) {
        const int o = d.orient;
        const bool g4 = o > 4 && (ev.nation == 3 || ev.nation == 4);  // the left side, for nations 3 and 4 only
        const bool g5 = o > 0 && o < 4;
        const bool p5 = ev.nation == 3 || ev.nation == 4 || (ev.type == 0xb && ev.nation == 1);
        for (int k = 4; k <= (p5 ? 5 : 4); ++k) {
            EffectSpawn e = puff((*pts)[static_cast<std::size_t>(k)], 3, 0x31);
            e.rise = 3, e.rise_step = 5, e.delay = 0;
            e.ground = k == 4 ? g4 : g5;
            add(e);
        }
        return;
    }

    std::uint32_t& gate = fx.dust_gate[d.serial];
    if (gate > s.tick) return;
    const bool four = ev.type == 0xd || ev.type == 0x18;
    for (int k = four ? 0 : 2; k < 4; ++k) {
        if (k == 2) gate = s.tick + (four ? 1 : 3);
        EffectSpawn e = puff((*pts)[static_cast<std::size_t>(k)], 0, 0x29);
        e.y -= effect_rand(fx) % 3;
        e.x -= effect_rand(fx) % 3;
        if (k >= 2 && (ev.type == 0xb || ev.type == 0xc)) {
            e.rise_step = 0, e.speed = 1.0f * 0.5f;
        } else {
            e.rise_step = effect_rand(fx) & 1;
        }
        e.ground = 1, e.delay = 2;
        add(e);
    }
}

void effects_house_fires(Effects& fx, EffectGraphics& eg, const GameFileLoader& load, const WorldGraphics& g,
                         const LiveView& lv, const sim::GameState& s) {
    for (std::size_t i = 0; i < lv.houses.size() && i < lv.house_refs.size(); ++i) {
        const sim::HouseRef& ref = lv.house_refs[i];
        if (ref.player >= s.players.size() || ref.slot >= s.players[ref.player].houses.size()) continue;
        const sim::House& h = s.players[ref.player].houses[ref.slot];


        if (h.is_destroyed != 0 || h.field_020 != 0) continue;
        const int nation = ref.player < lv.view.nations.size() ? sim::House_Nation(s, ref.player, h) : -1;
        if (nation < 0 || nation >= static_cast<int>(g.params.size()) || !g.params[static_cast<std::size_t>(nation)]) {
            continue;
        }
        const BasicParams& bp = *g.params[static_cast<std::size_t>(nation)];
        if (h.type >= bp.houses.size()) continue;
        const HouseParams& hp = bp.houses[h.type];
        const int graphic = i < lv.view.house_graphic.size() ? lv.view.house_graphic[i] : 0;
        const auto it = g.houses.find({nation, h.type, graphic});
        if (it == g.houses.end() || it->second.picture.width <= 0) continue;
        const HouseGraphics& gfx = it->second;

        const int half = h.max_hp >> 1;
        const int d = half - static_cast<int>(h.hp);
        if (half == 0 || d <= 0) continue;
        const int lvl = 4 * d / half;
        if (lvl > 3) continue;
        const MapData& m = lv.map;
        const int hc =
            h.map_x >= 0 && h.map_y >= 0 && h.map_x < m.width && h.map_y < m.height
                ? m.cells[static_cast<std::size_t>(h.map_y) * static_cast<std::size_t>(m.width) + h.map_x].height
                : 0;
        for (int k = 0; k < std::min<int>(hp.field_1c8, 10); ++k) {
            if (hp.fire_level[static_cast<std::size_t>(k)][static_cast<std::size_t>(lvl)] == 0) continue;
            const int px = hp.fire_x[static_cast<std::size_t>(k)], py = hp.fire_y[static_cast<std::size_t>(k)];
            // The depth there: the picture's depth + the cell's height (0 without a depth map); none (0x7fff, or a
            // short that wraps to 0x8000): no fire at that point.
            std::int16_t D = 0;
            if (!gfx.depth.empty()) {
                const Image& pic = gfx.picture;
                const std::int16_t z =
                    px < pic.width && py < pic.height
                        ? gfx.depth[static_cast<std::size_t>(py) * static_cast<std::size_t>(pic.width) + px]
                        : static_cast<std::int16_t>(kNoDepth);
                D = static_cast<std::int16_t>(z + hc);
                if (D > 0x7ffe || D == -0x8000) continue;
            }
            if (effect_rand(fx) % 3 != 0) continue;
            const auto z5 = static_cast<std::int16_t>(D + 5);
            const int X = static_cast<int>(h.x - static_cast<float>(hp.offset_x) + static_cast<float>(px));
            const int Y = static_cast<int>(h.y - static_cast<float>(hp.offset_y) + static_cast<float>(py) +
                                           static_cast<float>(z5) - static_cast<float>(hc));
            EffectSpawn fire;
            fire.type = 0x22;  // SmallFire
            fire.y = Y - effect_rand(fx) % 5;
            fire.x = X - (effect_rand(fx) & 1) + 1;
            fire.z = z5;
            effects_add(fx, eg, load, fire);
            EffectSpawn smoke;
            smoke.type = 9;  // SM1
            smoke.y = Y - effect_rand(fx) % 10;
            smoke.x = X - effect_rand(fx) % 10 + 5;
            smoke.z = z5;
            smoke.rise_step = 3;
            effects_add(fx, eg, load, smoke);
        }
    }
}

void effects_tick(LiveView& lv, const WorldGraphics& g, const GameFileLoader& load, const sim::GameState& s,
                  const std::vector<sim::SimEvent>& events, ViewPos pos, int w, int h,
                  std::optional<sim::HouseRef> selected, UnitSprites* sprites) {
    Effects& fx = lv.effects;
    EffectGraphics& eg = lv.effect_graphics;
    if (s.tick == fx.stepped_tick) return;


    leaves_step(fx);
    for (const sim::SimEvent& ev : events) {
        if (ev.kind == sim::SimEvent::Kind::Leaves) leaves_add(fx, g.objects[0], lv.map, ev.cell_x, ev.cell_y, ev.type);
    }
    for (const sim::SimEvent& ev : events) {
        if (ev.kind == sim::SimEvent::Kind::Dust) effects_dust(fx, eg, load, g, s, ev, pos, w, h);
        if (ev.kind != sim::SimEvent::Kind::Effect) continue;
        if (ev.effect.on_screen && !effect_point_in_view(ev.effect.view_x, ev.effect.view_y, pos, w, h)) continue;
        EffectSpawn e = effect_spawn(ev.effect);
        if (ev.effect.view_rand == 1) {
            if (effect_rand(fx) % 3 == 0) continue;
            e.y -= effect_rand(fx) % 5;
            e.x -= effect_rand(fx) % 10;
        } else if (ev.effect.view_rand == 2) {
            e.z += static_cast<int>(screen_ground_height(lv.map, ev.effect.view_x, ev.effect.view_y));
        }
        effects_add(fx, eg, load, e);
    }
    effects_house_fires(fx, eg, load, g, lv, s);
    effects_step(fx, eg, s.tick);
    shell_trails_step(lv.shell_trails, s);

    const std::vector<bool> ship_slots = players_with_ship_slots(g, s);  // one scan for the smoke and the waves
    if (sprites != nullptr) ship_smoke(fx, eg, g, *sprites, load, s, pos, w, h, &ship_slots);
    // The water's timers, the ships' waves and the sinking ships' bubbles (render/water, docs/render.md 17).
    water_tick(lv.water, lv.map, g, load, s, events, pos, w, h, &ship_slots);


    for (const sim::Shell& sh : s.shells) {
        if (sh.kind != 9 || sh.flown <= 9) continue;
        EffectSpawn t;
        t.type = 0x26;  // torpedo
        t.x = static_cast<int>(sh.cx), t.y = static_cast<int>(sh.cy), t.z = static_cast<int>(sh.cz);
        t.delay = 3, t.frame = 1.0f;
        effects_add(fx, eg, load, t);
    }


    if (selected && s.tick % 10 == 1 && selected->player < s.players.size() &&
        selected->slot < s.players[selected->player].houses.size()) {
        const sim::Player& pl = s.players[selected->player];
        const sim::House& hs = pl.houses[selected->slot];
        const bool flag = sim::House_Type(s, selected->player, hs).produce[0] != 0xffff || hs.capacity != 0;
        const auto ground = static_cast<float>(sim::GroundHeightAt(s, hs.rally_x, hs.rally_y));
        if (hs.type == 0x16 && flag && effect_point_in_view(hs.rally_x, hs.rally_y - ground, pos, w, h)) {
            EffectSpawn m;
            m.type = 0x25;
            m.x = static_cast<int>(hs.rally_x), m.y = static_cast<int>(hs.rally_y), m.z = 55;
            m.rise_step = 0;
            effects_add(fx, eg, load, m);
        }
    }
    // The units' flashes (render/unit_fx): the shots of this step, then a step of every flash its unit draws.
    unit_fx_tick(lv.unit_fx, lv.unit_fx_graphics, load, g, s, events, pos, w, h);
}

}  // namespace antanta
