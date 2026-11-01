



#include "events.hpp"

#include "movement.hpp"

namespace antanta::sim {

namespace {

SimEvent SoundOf(std::uint8_t sound, std::uint8_t variant, std::uint8_t priority) {
    SimEvent e;
    e.kind = SimEvent::Kind::Sound;
    e.sound = sound, e.variant = variant, e.priority = priority;
    return e;
}

std::uint8_t NationOf(const GameState& s, std::uint8_t player) {
    return player < s.players.size() ? s.players[player].nation : 0;
}

// The table value the last Rng_Next returned (the call sites append right after their read).
std::uint16_t Rng_Last(const GameState& s) {
    return s.rng_table.empty() ? 0 : s.rng_table[static_cast<std::uint16_t>(s.rng_index - 1)];
}

void Message(GameState& s, const char* key) {
    SimEvent e;
    e.kind = SimEvent::Kind::Message;
    e.text = key;
    e.colour = 0xffffff;
    s.events.push_back(std::move(e));
}

void Mark(GameState& s, std::int32_t x, std::int32_t y) {
    SimEvent e;
    e.kind = SimEvent::Kind::BattleMark;
    e.cell_x = x, e.cell_y = y;
    s.events.push_back(std::move(e));
}




std::uint8_t ExplosionType(std::uint8_t kind, std::uint16_t r) {
    static constexpr std::uint8_t k01[3] = {27, 28, 29}, k6[3] = {24, 25, 26}, k8[3] = {21, 22, 23},
                                  other[3] = {15, 16, 17}, k5[2] = {2, 3};
    switch (kind) {
        case 0:
        case 1: return k01[r % 3];
        case 3:
        case 4: return 8;
        case 5: return k5[r & 1];
        case 6: return k6[r % 3];
        case 7: return 27;
        case 8: return k8[r % 3];
        default: return other[r % 3];  // 2 and the kinds above 8
    }
}

}  // namespace


void Event_UnitSound(GameState& s, std::uint8_t player, const Unit& u, std::uint8_t variant, std::uint8_t priority) {
    SimEvent e = SoundOf(3, variant, priority);
    e.player = player, e.nation = Unit_Nation(s, player, u), e.type = u.type, e.id = u.id;  // Basic's sounds
    e.x = u.x;
    e.y = static_cast<float>(static_cast<double>(u.y) - u.field_024 - u.z);
    e.flag = variant == 5;
    s.events.push_back(std::move(e));
}



void Event_HouseSound(GameState& s, std::uint8_t player, const House& h, std::uint8_t variant, std::uint8_t priority) {
    SimEvent e = SoundOf(2, variant, priority);
    e.player = player, e.nation = NationOf(s, player), e.type = h.type, e.id = h.num;
    e.build_left = h.field_020;
    e.x = h.x, e.y = h.y;
    s.events.push_back(std::move(e));
}

void Event_Sound(GameState& s, std::uint8_t sound, std::uint8_t variant, std::uint8_t priority) {
    s.events.push_back(SoundOf(sound, variant, priority));
}


void Event_WeaponSound(GameState& s, std::uint8_t player, const Unit& u, std::uint8_t weapon_48, std::uint8_t kind) {
    std::uint8_t variant = 2, priority = 4;
    if (weapon_48 == 0) {
        if (Unit_Type(s, player, u).is_ship != 0) variant = 3;
    } else if (u.type == 0x12) {
        if (kind != 7) variant = 3;
    } else {
        priority = 7;
    }
    Event_UnitSound(s, player, u, variant, priority);
}

void Event_CommandVoice(GameState& s, std::uint8_t player, const UnitRef& head) {
    if (player != s.my_player) return;
    if (const Unit* u = Unit_Find(s, UnitRef{player, head.id, head.serial})) Event_UnitSound(s, player, *u, 1, 4);
}

void Event_Horn(GameState& s, std::uint8_t player, const Unit& u) {
    if (Rng_Last(s) < 3000 && (u.type == 0x18 || u.type == 0xd)) Event_UnitSound(s, player, u, 3, 3);
}





void Event_ShellLeadIn(GameState& s, const Shell& v, double x, double y) {
    if (v.sound == 0 || s.speed_ms == 0) return;
    const float fps = 1000.0f / static_cast<float>(s.speed_ms);
    const std::int32_t n = Ftol(v.flight) + 1;
    const std::uint8_t variant = static_cast<double>(n) <= static_cast<double>(fps * 3.0f) ? 1 : 3;
    const std::int32_t step = Ftol(static_cast<double>(n) - static_cast<double>(variant) * fps - 1.0);
    if (step < 0 || step != v.flown) return;
    SimEvent e = SoundOf(4, variant, 2);
    e.x = static_cast<float>(Ftol(x)), e.y = static_cast<float>(Ftol(y));
    s.events.push_back(std::move(e));
}




// 0x25de..0x25e3 raises the point by 0x28. The point is (x, y - z) with the shell's ftol'd position. On dry land the


// (medium; docs/sound.md 10).
void Event_ShellExplosion(GameState& s, const Shell& v) {
    const std::uint8_t type = ExplosionType(v.kind, v.kind == 7 ? 0 : Rng_Last(s));
    Event_Effect(s, 0, false, Ftol(v.cx), Ftol(v.cy), Ftol(v.cz), type);
}


SimEvent::EffectArgs* Event_Effect(GameState& s, std::uint8_t kind, bool unit, std::int32_t x, std::int32_t y,
                                   std::int32_t z, std::uint8_t type) {
    static constexpr std::uint8_t kVariant[50] = {1, 1, 1, 1, 2, 6, 1, 1, 1, 6, 1, 1, 1, 1, 1, 1, 1,
                                                  1, 1, 0, 2, 0, 0, 0, 0, 0, 0, 1, 1, 1, 3, 4, 5, 3,
                                                  6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6};
    static constexpr std::uint8_t kWater[50] = {31, 30, 30, 30, 30, 0, 31, 31, 30, 32, 33, 33, 33, 33, 31, 31, 32,
                                                32, 30, 32, 30, 0,  0, 0,  31, 31, 31, 30, 30, 30, 30, 31, 32, 33,
                                                31, 0,  0,  0,  0,  0, 0,  0,  0,  0,  0,  0,  0,  0,  0,  0};
    if (type > 0x31) return nullptr;
    bool water = false;
    const Cell c = WorldToCell(static_cast<float>(x), static_cast<float>(y));
    if (c.x >= 0 && c.y >= 0 && c.x < s.map.width && c.y < s.map.height) {
        const std::uint16_t obj = s.map.objects[static_cast<std::size_t>(c.y) * s.map.width + c.x];
        const bool wet =
            Obstacle_UnderCell(s.map, c.x, c.y) && (obj < 0x65 || obj > 10999 || (obj >= 9000 && obj < 0x25e4));
        if (!wet || unit) {


            static constexpr std::uint8_t kCrater[50] = {
                1,   12,  4,   5,   4,   255, 2,   8,   8,   255, 5,   4,   4,   5,   10,  1,   10,
                11,  4,   255, 5,   255, 255, 255, 10,  11,  1,   1,   10,  11,  255, 255, 255, 255,
                255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
            const std::size_t idx = static_cast<std::size_t>(c.y) * s.map.width + c.x;
            if (kind == 0 && kCrater[type] != 0xff && !Cell_Obstacle(s.map, c.x, c.y) && idx < s.map.heights.size() &&
                z - 20 < static_cast<std::int32_t>(s.map.heights[idx])) {
                SimEvent e;
                e.kind = SimEvent::Kind::Crater;
                e.type = static_cast<std::uint8_t>(kCrater[type] - 1);
                e.x = static_cast<float>(x), e.y = static_cast<float>(y - z);
                s.events.push_back(std::move(e));
            }
            if (obj > 0x25dd && obj < 0x25e4) z += 0x28;
        } else if (type != 9 && type != 0x26 && kWater[type] != 0) {
            type = kWater[type], water = true, z = 0x32;
        }
    }
    if (kVariant[type] != 6) {
        SimEvent e = SoundOf(6, kVariant[type], water ? 5 : 7);
        e.x = static_cast<float>(x), e.y = static_cast<float>(y - z);
        s.events.push_back(std::move(e));
    }
    SimEvent e;
    e.kind = SimEvent::Kind::Effect;
    e.effect.kind = kind, e.effect.type = type;
    e.effect.x = x, e.effect.y = y, e.effect.z = z;
    s.events.push_back(std::move(e));
    return &s.events.back().effect;
}


void Event_HouseAttacked(GameState& s, std::uint8_t player, const House& h) {
    if (Rng_Last(s) >= 5000 || player != s.my_player) return;
    Mark(s, h.map_x, h.map_y);
    Message(s, "IDS_HOUSE_UNDER_ATTACK");
}

void Event_TreeLeaves(GameState& s, std::int32_t x, std::int32_t y, std::uint16_t record) {
    SimEvent e;
    e.kind = SimEvent::Kind::Leaves;
    e.cell_x = x, e.cell_y = y, e.type = record;
    s.events.push_back(std::move(e));
}

void Event_UnitDust(GameState& s, std::uint8_t player, const Unit& u, std::uint8_t what) {
    SimEvent e;
    e.kind = SimEvent::Kind::Dust;

    e.player = player, e.nation = NationOf(s, player), e.type = u.type;
    e.x = u.x, e.y = u.y;
    e.dust.what = what, e.dust.orient = u.orient, e.dust.size = Unit_Type(s, player, u).size;
    e.dust.serial = u.serial, e.dust.z = u.z;
    s.events.push_back(std::move(e));
}

void Event_WeaponFlash(GameState& s, std::uint8_t player, const Unit& u, std::uint8_t weapon, std::uint8_t barrel,
                       float gun) {
    SimEvent e;
    e.kind = SimEvent::Kind::Flash;
    e.player = player, e.nation = Unit_Nation(s, player, u), e.type = u.type, e.id = u.id;
    e.x = u.x, e.y = u.y;
    e.flash.weapon = weapon, e.flash.barrel = barrel, e.flash.ship = Unit_Type(s, player, u).is_ship;
    e.flash.serial = u.serial, e.flash.z = u.z, e.flash.alt = u.field_024, e.flash.angle = u.angle;
    e.flash.gun = gun;
    s.events.push_back(std::move(e));
}

void Event_RifleFlash(GameState& s, std::uint8_t player, const Unit& u) {
    SimEvent e;
    e.kind = SimEvent::Kind::RifleFlash;
    e.player = player, e.nation = Unit_Nation(s, player, u), e.type = u.type, e.id = u.id;
    e.x = u.x, e.y = u.y;
    e.flash.serial = u.serial, e.flash.z = u.z;
    s.events.push_back(std::move(e));
}

void Event_ShipWake(GameState& s, std::uint8_t player, const Unit& u, bool moving) {
    if (Unit_Type(s, player, u).is_ship == 0) return;  // Basic+4
    SimEvent e;
    e.kind = SimEvent::Kind::ShipWake;
    e.player = player, e.serial = u.serial, e.flag = moving;
    s.events.push_back(std::move(e));
}

void Event_ShipSink(GameState& s, std::uint8_t player, const Unit& u, std::vector<SimEvent::SinkArgs::Bubble> bubbles) {
    SimEvent e;
    e.kind = SimEvent::Kind::ShipSink;
    e.player = player, e.nation = Unit_Nation(s, player, u), e.type = u.type;
    e.x = u.x, e.y = u.y - u.z;
    e.sink.orient = u.orient;
    e.sink.bubbles = std::move(bubbles);
    s.events.push_back(std::move(e));
}


void Event_GroupAlert(GameState& s, const Group& g, bool any) {
    SimEvent e;
    e.kind = SimEvent::Kind::GroupAlert;
    e.player = g.player, e.flag = any;
    for (const UnitRef& r : g.members) {
        if (r.player == 0xff) continue;
        if (const Unit* u = Unit_Find(s, r)) {
            const Cell c = WorldToCell(u->x, u->y);
            e.cell_x = c.x, e.cell_y = c.y;
            break;
        }
    }
    s.events.push_back(std::move(e));
}

void Event_TrainRefused(GameState& s, std::uint8_t player) {
    if (player != s.my_player || player >= s.players.size()) return;
    const Player& p = s.players[player];
    if (static_cast<std::int32_t>(p.unit_cap) <= p.units_alive) Message(s, "IDS_BUILD_MORE_HOUSE");
}

void Event_TransportRange(GameState& s, std::uint8_t player) {
    if (player == s.my_player) Message(s, "IDS_CANNOT_GO_TO_TRANSPORT_RANGE");
}

}  // namespace antanta::sim
