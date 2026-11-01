



#include <bit>
#include <cmath>

#include "events.hpp"
#include "movement.hpp"

namespace antanta::sim {

namespace {

// The unit of an order_point command: the first argument (or the unit list) with the acting player; an O line of the
// recorder may not name the player, then the unit's serial (never reused) finds it.
Unit* PointUnit(GameState& s, const Command& c, std::uint8_t* player) {
    UnitRef r = !c.args.empty() && c.args[0].tag == OrderArg::Tag::Unit ? c.args[0].unit
                : !c.units.empty()                                      ? c.units[0]
                                                                        : UnitRef{};
    if (c.player >= 0 && c.player < static_cast<std::int32_t>(s.players.size())) {
        r.player = static_cast<std::uint8_t>(c.player);
    }
    if (r.player < s.players.size()) {
        *player = r.player;
        return Unit_Find(s, r);
    }
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        if (Unit* u = Unit_Find(s, UnitRef{static_cast<std::uint8_t>(p), r.id, r.serial})) {
            *player = static_cast<std::uint8_t>(p);
            return u;
        }
    }
    return nullptr;
}




void GasCloud_Step(GameState& s, GasCloud& g) {
    const std::int32_t r = g.radius;
    const auto n = static_cast<std::uint8_t>(static_cast<std::int32_t>(g.puffs) * r / g.max_radius);
    const std::int32_t d = r + r;
    for (std::uint32_t k = 0; k < n; ++k) {
        std::int32_t a = 0, b = 0;
        for (;;) {
            a = static_cast<std::int32_t>(Rng_Next(s)) % d - r;
            b = static_cast<std::int32_t>(Rng_Next(s)) % d - r;
            if (std::sqrt(static_cast<double>(a * a + b * b)) < static_cast<double>(r)) break;
        }
        std::int32_t px = g.x + a, py = g.y + (b >> 1);
        const std::int32_t z = GroundHeightAt(s, static_cast<float>(px), static_cast<float>(py));
        const std::int32_t v = s.rng_table[s.rng_index];
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
        py = py - v % 5 + 5;
        px = px - v % 10 + 5;

        static constexpr std::uint8_t kCc[50] = {1, 1, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
                                                 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
                                                 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0};
        if (g.type <= 0x31 && g.damage != 0) {
            Effect_Damage(s, static_cast<float>(px), static_cast<float>(py), static_cast<float>(z), g.damage,
                          g.damage_radius, kCc[g.type], true);
        }
        if (SimEvent::EffectArgs* e = Event_Effect(s, 3, false, px, py, z, g.type)) e->rise_step = 2;
    }
    g.radius = static_cast<std::int16_t>(g.radius + static_cast<std::int16_t>(g.step));
    if (static_cast<std::int32_t>(g.radius) > static_cast<std::int32_t>(g.max_radius)) {
        g.radius = static_cast<std::int16_t>(g.max_radius);
    }
    --g.life;
}

}  // namespace





void TUnit_OrderPoint(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t gas) {
    u.field_15c = 0;
    if (u.field_154 != 0) return;
    const UnitType& t = Unit_Type(s, player, u);
    if (gas != 0 && t.move_class == 10 && s.players[player].special_shells != 0) u.field_15c = 1;
    const double dx = static_cast<double>(u.x) - x, dy = static_cast<double>(u.y) - y;
    const auto dist = static_cast<float>(std::sqrt(dy * dy + dx * dx));
    if (t.move_class == 3 && dist < 250.0f) return;
    u.has_house_attack = false, u.has_attack_unit = false;
    u.work = 8, u.field_093 = 0, u.attack_unit_id = 0xffff, u.field_0ca = 0xffff;
    u.player_house_attack = 0xff, u.player_unit_attack = 0xff;
    if (t.is_air != 0) {
        u.field_0d4 = x, u.field_0d8 = y, u.dest_x = x, u.dest_y = y;
        u.is_stopped = 0, u.num_work = 0;
        return;
    }
    u.field_0d4 = x, u.field_0d8 = y, u.field_086 = 0;
    if (t.anim_size[6] != 0) {
        if (static_cast<float>(t.range[player]) <= dist) {
            TUnit_MoveTo(s, player, u, x, y, 1);
            return;
        }
        TUnit_StopMoving(s, player, u);
        StartFireAnim(s, player, u);
        FaceToPoint(s, player, u, x, y);
        return;
    }
    if (u.weapon_count == 0) {
        u.work = 0xff;
        TUnit_StopMoving(s, player, u);
        TUnit_AttackEnd(s, player, u);
    }
}

// The O line on entry, as the recorder's hook writes it, then the order.
void Order_ApplyPoint(GameState& s, const Command& c) {
    Command rec = c;
    s.order_seq = c.oseq != 0 ? c.oseq : s.order_seq + 1;
    rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
    std::uint8_t player = 0xff;
    Unit* u = PointUnit(s, c, &player);
    if (rec.units.empty() && u != nullptr) rec.units.push_back(Unit_Ref(player, *u));
    s.orders.push_back(std::move(rec));
    if (u == nullptr || c.args.size() != 4 || u->is_deleted != 0) return;
    TUnit_OrderPoint(s, player, *u, std::bit_cast<float>(c.args[1].raw), std::bit_cast<float>(c.args[2].raw),
                     static_cast<std::uint8_t>(c.args[3].raw & 0xff));
}


void Gas_Add(GameState& s, GasCloud g) {
    g.radius = 1;
    if (g.kind == 0) g.type = 0x24;
    if (g.kind == 1) g.type = 0x1e;
    s.gas_clouds.push_back(g);
}


void Particles_Update(GameState& s) {
    for (std::size_t k = 0; k < s.gas_clouds.size();) {
        GasCloud_Step(s, s.gas_clouds[k]);
        if (s.gas_clouds[k].life < 1) {
            s.gas_clouds.erase(s.gas_clouds.begin() + static_cast<std::ptrdiff_t>(k));
        } else {
            ++k;
        }
    }
}

}  // namespace antanta::sim
