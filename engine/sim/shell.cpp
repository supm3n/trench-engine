

#include <cmath>

#include "dmath.hpp"
#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {
namespace {



struct ShellKind {
    std::uint8_t high = 0;
    double speed = 0;
    std::int32_t damage = 0, radius = 0;
    float substep = 0;
    std::uint8_t fragments = 0;
    float a4 = 0;
};
constexpr ShellKind kShellKinds[13] = {
    {0, 300.0, 700, 100, 180.0f, 1, 6.0f}, {0, 400.0, 900, 120, 150.0f, 2, 3.0f},
    {1, 170.0, 400, 80, 80.0f, 1, 1.0f},   {1, 200.0, 800, 80, 70.0f, 2, 1.0f},
    {1, 220.0, 1500, 100, 60.0f, 2, 1.0f}, {1, 250.0, 3000, 300, 60.0f, 2, 1.0f},
    {1, 60.0, 100, 40, 50.0f, 1, 3.0f},    {0, 3.0, 300, 100, 210.0f, 2, 6.0f},
    {0, 500.0, 200, 60, 210.0f, 0, 7.0f},  {0, 800.0, 300, 100, 20000.0f, 0, 80.0f},
    {1, 70.0, 50, 15, 30.0f, 0, 1.8f},     {1, 50.0, 100, 25, 30.0f, 0, 1.8f},
    {1, 60.0, 50, 15, 30.0f, 0, 2.0f},
};

// The argument block of the constructor (FireShell's locals f0..f12).
struct ShellArgs {
    float x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
    std::uint8_t player = 0xff;
    UnitRef owner;
    std::uint8_t kind = 0;
    std::int32_t damage = 0;
    float speed = 0, range = 0;
    std::uint8_t gas = 0;
    bool fixz = false;  // the torpedo keeps its computed end height (no ground height in the tail)
};


Shell Shell_Init(GameState& s, const ShellArgs& a) {
    Shell v;
    v.owner = a.owner;
    v.x0 = a.x0, v.y0 = a.y0, v.z0 = a.z0, v.x1 = a.x1, v.y1 = a.y1, v.z1 = a.z1;
    v.player = a.player, v.kind = a.kind, v.range = a.range, v.gas = a.gas;
    if (a.kind >= 10 && a.kind <= 12) {
        v.no_effect = 1, v.sound = 0;
    } else {
        v.sound = (a.kind == 6 || a.kind == 9) ? 0 : 1;
    }
    if (a.kind > 12) {
        Unported(s, orig::kShellCreate);  // reads past the table
        v.fail = 1;
        return v;
    }
    const ShellKind& k = kShellKinds[a.kind];
    v.v0 = a.speed != 0.0f ? static_cast<double>(a.speed) : k.speed;
    v.damage = a.damage != 0 ? a.damage : k.damage;
    v.radius = k.radius, v.fragments = k.fragments, v.a4 = k.a4;
    v.substep = static_cast<float>(static_cast<double>(k.substep) / static_cast<double>(k.a4));
    const double dz = static_cast<double>(v.z1) - v.z0;
    double dx = static_cast<double>(v.x1) - v.x0, dy = static_cast<double>(v.y1) - v.y0;
    double d = std::sqrt(dy * dy + dx * dx);
    if (!(d <= static_cast<double>(k.radius) + k.radius + static_cast<double>(v.range))) {  // out of range: shorten
        v.x1 = static_cast<float>(static_cast<double>(v.range) / d * dx + v.x0);
        v.y1 = static_cast<float>(static_cast<double>(v.range) / d * dy + v.y0);
        dx = static_cast<double>(v.x1) - v.x0, dy = static_cast<double>(v.y1) - v.y0;
        d = std::sqrt(dx * dx + dy * dy);
    }
    const double s8 = a.kind == 9 ? 0.0025000000000000005 : 24.059025000000002;
    v.g = a.kind == 9 ? 0.1 : 9.81;
    const double sq = d * d + dz * dz;
    if (a.kind == 3) {  // the howitzer picks its own speed
        const double aa = (d * 2.8 - dz) * 2.0 / v.g;
        v.v0 = std::sqrt((dz * v.g + sq / aa) + aa * s8);
    }
    const double bq = dz * v.g - v.v0 * v.v0;
    const double disc = bq * bq - sq * s8 * 4.0;
    if (disc < 0.0) {
        v.fail = 1;
        return v;
    }
    double q = std::sqrt(disc);
    if (k.high == 0) q = -q;
    const double t = std::sqrt((q - bq) / (s8 + s8));
    const double inv = 1.0 / t;
    v.vz = t * v.g * 0.5 + inv * dz;
    v.flight = static_cast<double>(v.a4) * t;

    v.cz = v.z0, v.flown = 0;
    const double inv2 = 1.0 / v.flight;
    v.vx = dx * inv2, v.vy = inv2 * dy;
    return v;
}




void MuzzlePoint(GameState& s, std::uint8_t player, const Unit& u, const WeaponType& w, float out[3]) {
    const UnitType& t = Unit_Type(s, player, u);
    if (w.field_48 != 0) Unported(s, orig::kMuzzlePoint);  // PointsB and the barrel elevation (vehicles)
    const double step = static_cast<double>(0.7853982f) / static_cast<double>(t.anim_size[5]);
    const auto aq = static_cast<float>(static_cast<double>(Ftol(static_cast<double>(u.angle) / step)) * step);
    const double sn = dmath::sin(static_cast<double>(aq)), cs = dmath::cos(static_cast<double>(aq));
    const float p0 = static_cast<float>(cs * w.point_a[0] - sn * w.point_a[1]);
    const float p1 = static_cast<float>(sn * w.point_a[0] + cs * w.point_a[1]);
    const float p2 = w.point_a[2];
    const double a = (static_cast<double>(p0) - p1) * static_cast<double>(0.8660254f);
    const auto b = static_cast<float>(-((static_cast<double>(p1) + p0) * 0.5 + p2));
    out[0] = static_cast<float>(a + u.x);
    out[1] = static_cast<float>((static_cast<double>(b) + u.y) + p2);
    out[2] = static_cast<float>((static_cast<double>(u.z) + u.field_024) + p2);
}



void Torpedo_Extend(ShellArgs& a) {
    const auto dx = static_cast<float>(static_cast<double>(a.x1) - a.x0);
    const auto dy = static_cast<float>(static_cast<double>(a.y1) - a.y0);
    const auto dz = static_cast<float>(static_cast<double>(a.z1) - a.z0);
    auto len3 = [&](float x, float y, float z) {
        return std::sqrt((static_cast<double>(x) * x + static_cast<double>(y) * y) + static_cast<double>(z) * z);
    };
    const auto l = static_cast<float>(len3(dx, dy, dz));
    const double inv = 1.0 / len3(dx, dy, dz);
    const auto nx = static_cast<float>(static_cast<double>(dx) * inv);
    const auto ny = static_cast<float>(static_cast<double>(dy) * inv);
    const auto e = static_cast<float>(static_cast<double>(l) + 300.0);
    const auto xe = static_cast<float>(static_cast<double>(nx) * e);
    const auto ye = static_cast<float>(static_cast<double>(ny) * e);
    const auto ze = static_cast<float>(inv * static_cast<double>(dz) * static_cast<double>(e));
    a.x1 = static_cast<float>(static_cast<double>(xe) + a.x0);
    a.y1 = static_cast<float>(static_cast<double>(ye) + a.y0);
    a.z1 = static_cast<float>(static_cast<double>(ze) + a.z0);
}

}  // namespace

double Shell_SpreadDivisor(const GameState& s, std::uint8_t player, std::uint8_t kind) {
    double d = static_cast<double>(s.players[player].shell_spread);
    if (s.plus_rules && kind == 8) d *= static_cast<double>(s.players[player].factor_9b33b8);
    return d;
}

namespace {




void FireShell_Tail(GameState& s, ShellArgs& a, const Unit& o, std::uint16_t acc, float hz, bool offset = true,
                    std::uint16_t delay = 0) {
    const float e = o.field_08c;
    const std::int32_t k = Ftol(e < 250.0f ? static_cast<double>(e) * static_cast<double>(0.02f) : 5.0);
    acc = static_cast<std::uint16_t>(acc + ((100 - acc) * k) / 10);
    const double dx = static_cast<double>(a.x0) - a.x1, dy = static_cast<double>(a.y0) - a.y1;
    const double dist = std::sqrt(dy * dy + dx * dx);
    double spread = dist * (static_cast<double>(acc) * static_cast<double>(0.01f)) * 0.5;
    if (a.player != 0xff) spread /= Shell_SpreadDivisor(s, a.player, a.kind);
    const int r0 = Rng_Next(s) % 100, r1 = Rng_Next(s) % 360;
    const double rad = static_cast<double>(r0) * spread * 0.01;
    const double ang = static_cast<double>(r1) * static_cast<double>(0.017453292f);
    if (offset) {
        a.x1 = static_cast<float>(dmath::cos(ang) * rad + a.x1);
        a.y1 = static_cast<float>(dmath::sin(ang) * rad + a.y1);
    }
    const double mx = static_cast<double>(static_cast<std::uint32_t>(s.map.width) * 96u);
    const double my = static_cast<double>(static_cast<std::uint32_t>(s.map.height) * 24u);
    if (a.x1 < 0.0f) a.x1 = 11.0f;
    if (!(a.x1 < mx)) a.x1 = static_cast<float>(mx - 11.0);
    if (a.y1 < 0.0f) a.y1 = 11.0f;
    if (!(a.y1 < my)) a.y1 = static_cast<float>(my - 11.0);
    if (!a.fixz) {
        a.z1 = static_cast<float>(GroundHeightAt(s, a.x1, a.y1));
        if (hz > 0.0f) a.z1 = hz;
    }
    Shell v = Shell_Init(s, a);
    if (v.fail != 0) return;
    v.delay = delay;
    s.shells.insert(s.shells.begin(), v);
}

}  // namespace



void Weapon_Fire(GameState& s, std::uint8_t player, Unit& o) {
    const UnitType& t = Unit_Type(s, player, o);
    if (!t.weapons.empty()) {
        Event_WeaponSound(s, player, o, t.weapons[0].field_48, t.weapons[0].kind);
        Event_WeaponFlash(s, player, o, 0, 0, 0.0f);
    }
    if (t.weapons.empty() || t.weapons[0].kind == 0) return;
    const WeaponType& w = t.weapons[0];
    ShellArgs a;
    float m[3];
    MuzzlePoint(s, player, o, w, m);
    a.x0 = m[0], a.y0 = m[1], a.z0 = m[2];
    a.damage = w.attack, a.range = w.fire_range;
    bool lead = false;
    switch (w.kind) {
        case 1: a.kind = w.field_48 != 0 ? 1 : 0; break;
        case 2:
        case 5: a.kind = 5; break;
        case 3: a.kind = o.type != 5 ? 3 : 2; break;
        case 4: a.kind = 4; break;
        case 6: a.kind = 8, lead = true; break;
        default:
            Unported(s, orig::kFireShell);  // kind 7 (kc 9, no shipped weapon) and unknown kinds
            return;
    }
    a.player = player, a.owner = Unit_Ref(player, o);
    o.field_08c = static_cast<float>(static_cast<double>(o.field_08c) + (o.work == 8 ? 0.5f : 3.0f));
    if (o.field_15c != 0) {
        if (s.players[player].special_shells != 0) a.gas = 1, --s.players[player].special_shells;
        o.field_15c = 0;
    }
    if (o.weapon_target) {
        Unported(s, orig::kFireShell);
        return;
    }
    auto acc = static_cast<std::uint16_t>(0x65 - t.accuracy[player]);
    float hz = -1.0f;
    if (o.player_house_attack != 0xff) {
        const House* h = HouseTarget(s, o);
        if (h == nullptr) {
            Unported(s, orig::kFireShell);  // a stale house pointer
            return;
        }
        a.x1 = h->x, a.y1 = h->y;
    } else if (const Unit* tg = AttackTarget(s, o)) {
        a.x1 = tg->x, a.y1 = tg->y;
        if (lead) {
            a.x1 = static_cast<float>(static_cast<double>(tg->vel_x) * 17.0 + tg->x);
            a.y1 = static_cast<float>(static_cast<double>(tg->vel_y) * 17.0 + tg->y);
            hz = static_cast<float>(static_cast<double>(tg->z) + tg->field_024);
        }
    } else {
        a.x1 = o.field_0d4, a.y1 = o.field_0d8;
    }
    if (acc > 100) acc = 100;
    FireShell_Tail(s, a, o, acc, hz);
}



void Weapon_FireShip(GameState& s, std::uint8_t player, Unit& o, const ShipWeaponType& w, const float m[3],
                     std::uint8_t weapon, std::uint8_t barrel, float gun) {
    const UnitType& t = Unit_Type(s, player, o);
    Event_WeaponSound(s, player, o, w.slot, w.kind);
    Event_WeaponFlash(s, player, o, weapon, barrel, gun);
    if (w.kind != 1 && w.kind != 7) {
        Unported(s, orig::kFireShell);  // no other ship weapon kind is shipped
        return;
    }
    ShellArgs a;
    a.x0 = m[0], a.y0 = m[1], a.z0 = m[2];
    a.damage = w.attack, a.range = w.fire_range;
    a.kind = w.slot != 0 ? 1 : 0;
    if (w.kind == 7) a.kind = 9, a.z1 = 55.0f, a.fixz = true;  // the torpedo (docs/transports.md 11)
    a.player = player, a.owner = Unit_Ref(player, o);
    o.field_08c = static_cast<float>(static_cast<double>(o.field_08c) + (o.work == 8 ? 0.5f : 3.0f));
    if (o.field_15c != 0) {
        if (s.players[player].special_shells != 0) a.gas = 1, --s.players[player].special_shells;
        o.field_15c = 0;
    }
    auto acc = static_cast<std::uint16_t>(0x65 - t.accuracy[player]);
    if (o.player_house_attack != 0xff) {
        const House* h = HouseTarget(s, o);
        if (h == nullptr) {
            Unported(s, orig::kFireShell);  // a stale house pointer
            return;
        }
        a.x1 = h->x, a.y1 = h->y;
    } else if (const Unit* tg = AttackTarget(s, o)) {
        a.x1 = tg->x, a.y1 = tg->y;
    } else {
        a.x1 = o.field_0d4, a.y1 = o.field_0d8;
    }
    if (a.kind == 9) Torpedo_Extend(a);
    if (acc > 100) acc = 100;
    FireShell_Tail(s, a, o, acc, -1.0f);
}



void Grenade_Fire(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    ShellArgs a;
    a.x0 = u.x, a.y0 = u.y, a.z0 = static_cast<float>(static_cast<double>(u.z) + static_cast<double>(15.0f));
    a.x1 = u.field_0d4, a.y1 = u.field_0d8;
    a.damage = t.attack[player][2];
    a.player = player, a.owner = Unit_Ref(player, u);
    a.kind = 6;
    a.range = 270.0f;
    FireShell_Tail(s, a, u, 50, -1.0f);
}




void Bomb_Fire(GameState& s, std::uint8_t player, Unit& u, std::uint16_t delay) {
    const UnitType& t = Unit_Type(s, player, u);
    u.field_08c = static_cast<float>(static_cast<double>(u.field_08c) + (u.work == 8 ? 3.0f : 10.0f));
    ShellArgs a;
    a.x0 = u.x, a.y0 = u.y, a.z0 = static_cast<float>(static_cast<double>(u.z) + u.field_024);
    a.x1 = static_cast<float>(static_cast<double>(u.vel_x) * static_cast<double>(10.0f) + u.x);
    a.y1 = static_cast<float>(static_cast<double>(u.vel_y) * static_cast<double>(10.0f) + u.y);
    a.player = player, a.owner = Unit_Ref(player, u);
    a.kind = 7;
    auto acc = static_cast<std::uint16_t>(0x65 - t.accuracy[player]);
    a.speed = static_cast<float>(TUnit_Speed(s, player, u, orig::kFireShellBomb) * static_cast<double>(3.0f));
    a.damage = t.attack[player][2];
    a.range = 500.0f;
    if (acc > 100) acc = 100;
    FireShell_Tail(s, a, u, acc, -1.0f, false, delay);
}






bool Strike_Shell(GameState& s, std::uint8_t p, std::uint8_t kind, float x0, float y0, float z0, float x1, float y1,
                  std::int32_t damage, std::uint16_t delay, bool gas, float speed) {
    ShellArgs a;
    a.x0 = x0, a.y0 = y0, a.z0 = z0, a.x1 = x1, a.y1 = y1;
    a.player = p, a.kind = kind, a.damage = damage, a.gas = gas ? 1 : 0, a.speed = speed;
    const double mx = static_cast<double>(static_cast<std::uint32_t>(s.map.width) * 96u);
    const double my = static_cast<double>(static_cast<std::uint32_t>(s.map.height) * 24u);
    if (a.x1 < 0.0f) a.x1 = 11.0f;
    if (!(a.x1 < mx)) a.x1 = static_cast<float>(mx - 11.0);
    if (a.y1 < 0.0f) a.y1 = 11.0f;
    if (!(a.y1 < my)) a.y1 = static_cast<float>(my - 11.0);
    a.z1 = static_cast<float>(GroundHeightAt(s, a.x1, a.y1));
    const double dx = static_cast<double>(a.x1) - a.x0, dy = static_cast<double>(a.y1) - a.y0;
    a.range = static_cast<float>(std::sqrt(dx * dx + dy * dy) + 1.0);
    Shell v = Shell_Init(s, a);
    if (v.fail != 0) return false;
    v.delay = delay;
    s.shells.insert(s.shells.begin(), v);
    return true;
}

namespace {


constexpr std::uint8_t kFragmentRows[3][9] = {
    {6, 10, 10, 10, 12, 11, 11, 0, 0},
    {8, 10, 10, 12, 10, 10, 11, 12, 11},
    {0, 0, 0, 0, 0, 0, 0, 0, 0},
};







constexpr std::uint16_t kHouseHeightNoDepth = 50;


std::uint16_t Depth_Span(GameState& s, const HouseDepthMap& m, std::int32_t x, std::int32_t y) {
    if (y < 0 || y >= m.height) {
        Unported(s, orig::kHouseDepthSpan);  // a row past the span table: the original reads past it
        return 0x8000;
    }
    if (x < 0 || x >= m.width) return 0x8000;
    return m.depth[static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x)];
}


std::uint16_t Depth_Raw(GameState& s, const HouseDepthMap& m, std::size_t i) {
    if (i >= m.depth.size()) {
        Unported(s, orig::kShellStep);  // past the array: the original reads whatever follows
        return 0x7fff;
    }
    return m.depth[i];
}

}  // namespace

const HouseDepthMap* House_DepthMap(const GameState& s, std::uint8_t player, const House& h) {
    if (h.depth_phase == 0xff || player >= s.players.size()) return nullptr;
    const std::uint8_t nation = House_Nation(s, player, h);  // the graphic of THouse::Basic
    for (const HouseDepthMap& m : s.house_depth_maps) {
        if (m.nation == nation && m.type == h.type && m.phase == h.depth_phase && m.stage == h.depth_stage) return &m;
    }
    return nullptr;
}

namespace {



House* CellHouse(GameState& s, std::uint16_t obj, std::uint8_t* player) {
    const std::size_t p = obj / 1000u - 1u, slot = obj % 1000u;
    if (p >= s.players.size() || slot >= s.players[p].houses.size()) return nullptr;
    *player = static_cast<std::uint8_t>(p);
    return &s.players[p].houses[slot];
}

// THouse+0 (Basic): g_HouseBasic[nation * 25 + type] of the house's own nation (House_Type).
const HouseType& Type_Of(const GameState& s, std::uint8_t player, const House& h) { return House_Type(s, player, h); }




void Shell_SnapToHouse(GameState& s, std::uint8_t player, const House& h, float* x, float* y, float* z) {
    const HouseType& ht = Type_Of(s, player, h);
    if (ht.hit_count == 0) return;
    const auto fx = static_cast<float>(static_cast<double>(h.x) - static_cast<double>(ht.field_140));
    const auto fy = static_cast<float>(static_cast<double>(h.y) - static_cast<double>(ht.field_142));

    const auto mx = static_cast<std::uint16_t>(h.map_x), my = static_cast<std::uint16_t>(h.map_y);
    std::uint8_t hg = 0;
    if (mx < s.map.width && my < s.map.height) {
        hg = s.map.heights[static_cast<std::size_t>(my) * s.map.width + mx];
    } else {
        Unported(s, orig::kCellHeight);  // the original logs and gives 0
    }
    std::size_t best = 99;
    float min = 99999.0f;
    for (std::size_t i = 0; i < ht.hit_count && i < 10; ++i) {
        const double ey = ((static_cast<double>(ht.hit_y[i]) + fy) - static_cast<double>(*y)) + *z;
        const double ex = (static_cast<double>(ht.hit_x[i]) + fx) - static_cast<double>(*x);
        const double d = std::sqrt(ex * ex + ey * ey);
        if (d < static_cast<double>(min)) min = static_cast<float>(d), best = i;  // the first minimum wins
    }
    if (best >= 10) {
        // no point nearer than 99999 (or more than 10): the original reads past the table
        Unported(s, orig::kShellSnapToHouse);
        return;
    }
    const std::uint16_t px = ht.hit_x[best], py = ht.hit_y[best];
    const auto wx = static_cast<float>(static_cast<double>(px) + fx);
    const auto wy = static_cast<float>(static_cast<double>(py) + fy);



    std::uint16_t hv = kHouseHeightNoDepth;
    if (h.depth_phase != 0xff) {
        const HouseDepthMap* m = House_DepthMap(s, player, h);
        if (m == nullptr) {
            Unported(s, orig::kShellSnapToHouse);  // a depth map that was not loaded: 50 as if null
        } else if (m->spans) {
            hv = Depth_Span(s, *m, px, py);
        } else if (py < ht.sprite_h && px < ht.sprite_w) {
            hv = Depth_Raw(s, *m, static_cast<std::size_t>(ht.sprite_w) * py + px);
            if (hv == 0x7fff) hv = 0;
        } else {
            hv = 0;
        }
    }
    const std::int32_t sum = static_cast<std::int32_t>(hv) + hg;
    *z = static_cast<float>(sum);
    *y = static_cast<float>((static_cast<double>(sum) + static_cast<double>(wy)) - static_cast<double>(hg));
    *x = wx;
}





void DamageForUnits(GameState& s, float x, float y, float z, std::uint8_t cc, std::int32_t dmg, std::int32_t r,
                    House* h, std::uint8_t hp, const Shell* sv, std::uint8_t flag) {
    const Shell none;  // no shell: player 0xff, no owner; its kind is not read
    const Shell& v = sv != nullptr ? *sv : none;
    Cell c = WorldToCell(x, y);
    const std::int32_t w = s.map.width;
    const bool in = c.x >= 0 && c.y >= 0 && c.x < w && c.y < s.map.height;
    const std::uint16_t obj = in ? s.map.objects[static_cast<std::size_t>(c.y) * w + c.x] : 0;
    Unit* owner = nullptr;
    if (v.owner.player != 0xff) {
        owner = Unit_Find(s, v.owner);
        // the owner was deleted: the original reads a stale pointer
        if (owner == nullptr) Unported(s, orig::kShellDamageUnits);
    }
    // The house damage: kind 6 (grenade) halves, the others double, a mortar man's (type 5) shell halves again;
    // without a shell the damage as given.
    std::int32_t hd = dmg;
    if (sv != nullptr) hd = v.kind == 6 ? dmg / 2 : dmg * 2;
    if (owner != nullptr && owner->type == 5) hd /= 2;
    bool alert = sv != nullptr;  // no shell: no alert and no kind 8 test
    bool flak = false;
    float flak_y = 0;
    if (h != nullptr && flag == 0) {
        const HouseType& ht = Type_Of(s, hp, *h);
        const double armour = static_cast<double>(s.players[hp].factor_724);
        const double durability = House_Durability(s, hp, *h);
        if (h->hp < 0.0f) {
            alert = false;               // the jump goes past the alert, to the units
        } else if (h->field_020 != 0) {  // still being built: the damage goes into the work left
            const double q = static_cast<double>(hd) / (durability * armour * static_cast<double>(0.6666667f));
            h->field_020 = static_cast<std::uint16_t>(h->field_020 + Ftol(q));
            if (h->field_020 >= ht.work_rate) {
                h->hp = -10.0f;
                House_OnDestroyed(s, hp, *h, v.player);
            }
        } else if (h->hp > 0.0f) {
            float nh =
                static_cast<float>(static_cast<double>(h->hp) -
                                   static_cast<double>(hd) * static_cast<double>(100.0f) / (durability * armour));
            if (v.player != hp) {
                const UnitType* ot = owner != nullptr ? &Unit_Type(s, v.owner.player, *owner) : nullptr;
                if (ot == nullptr || (ot->is_ship == 0 && ot->is_air == 0)) {

                    if (h->field_0a8 > 0 || h->is_destroyed != 0 || s.players[hp].is_lost != 0) {
                        --h->field_0a8;
                    } else {
                        AI_HouseAttacked(s, hp, *h);
                    }
                }
            }
            if (nh < 1.0f) {
                h->hp = -10.0f;
                House_OnDestroyed(s, hp, *h, v.player);
                nh = h->hp;
            }
            h->hp = nh;
        }
    }
    if (alert) {
        const std::uint8_t p = v.player;
        if (p < s.players.size() && s.players[p].is_lost == 0) {


            if (owner != nullptr) {
                AlertNearby(s, p, static_cast<std::uint16_t>(c.x), static_cast<std::uint16_t>(c.y), v.owner.player,
                            *owner);
            } else if (!(s.plus_rules && v.owner.player == 0xff)) {
                Unported(s, orig::kAlertNearby);  // a player's shell without its owner: the original reads a null unit
            }  // Entente Engine Plus: a call-in strike's shell has no owner unit (sim/strike.cpp), so no alert
        }
        if (v.kind == 8) {
            // Kind 8 (flak): the point 400 above the shell (z + y - 400 of its last position, f32) gives the cell
            // for the units and the object; only MoveClass 1 units are hit, by the flat distance.
            flak = true;
            flak_y = static_cast<float>((static_cast<double>(v.cz) + v.cy) - static_cast<double>(400.0f));
            c = WorldToCell(v.cx, flak_y);
        }
    }
    const std::int32_t cells = w * static_cast<std::int32_t>(s.map.height);
    for (std::int32_t dx = -2; dx <= 2; ++dx) {
        for (std::int32_t dy = -4; dy <= 4; ++dy) {
            const std::int32_t k = (c.y + dy) * w + c.x + dx;  // rows wrap at the map edge
            if (k < 0 || k >= cells) continue;
            // Up to 40 slots, the list read again after every hit (a killed unit stays in its cell).
            for (std::size_t i = 0; i < 40; ++i) {
                const std::vector<UnitRef>& list = s.map.cell_units[static_cast<std::size_t>(k)];
                if (i >= list.size()) break;
                const UnitRef ref = list[i];
                Unit* u = Unit_Find(s, ref);
                if (u == nullptr) {
                    Unported(s, orig::kShellDamageUnits);  // a stale cell entry
                    continue;
                }
                if (u->is_deleted != 0) continue;
                double dist = 0;
                const double ex = static_cast<double>(u->x) - x;
                if (flak) {
                    if (Unit_Type(s, ref.player, *u).move_class != 1) continue;
                    const double ey = static_cast<double>(flak_y) - u->y;
                    dist = std::sqrt((ey * ey + 0.0) + ex * ex);
                } else {
                    const double ey = static_cast<double>(u->y) - y;
                    const double ez = (static_cast<double>(u->z) + u->field_024) - z;
                    dist = std::sqrt((ey * ey + ez * ez) + ex * ex);
                }
                if (!(dist <= static_cast<double>(r))) continue;
                // The raw damage argument (not the house damage), fimul; the low 16 bits of the ftol.
                const auto hit = static_cast<std::uint16_t>(
                    Ftol((static_cast<double>(r) - dist) * static_cast<double>(dmg) / static_cast<double>(r)));
                if (hit == 0) continue;
                UnitHit(s, ref.player, *u, hit, v.player, flag);
            }
        }
    }
    if (Special_IsPontoon(s, obj)) Special_Hit(s, obj, sv);
    if (cc != 0) {
        if (obj >= 9000 && obj < 9500) {  // a tree burns down: a stump (0x239f) where the record's byte 0 is 1
            const std::size_t i = obj - 9000u;
            // a tree record objects.les does not have
            if (i >= s.object_blocking[0].size()) Unported(s, orig::kShellDamageUnits);
            const bool stump = i < s.object_blocking[0].size() && s.object_blocking[0][i] == 1;
            Terrain_SetObject(s, c.x, c.y, stump ? 0x239f : 0);
            Event_TreeLeaves(s, c.x, c.y, static_cast<std::uint16_t>(i));
        }
        if (obj >= 0x25de && obj <= 0x25e3) Terrain_SetObject(s, c.x, c.y, 0);
    }
}


bool Shell_Step(GameState& s, Shell& v) {
    if (v.delay != 0) {
        --v.delay;
        return false;
    }
    const auto sub = static_cast<float>(v.v0 / static_cast<double>(v.substep));
    const double n = v.flown;
    const double bx = n * v.vx + v.x0, by = n * v.vy + v.y0;
    Event_ShellLeadIn(s, v, bx, by);
    if (!(0.0f < sub)) {
        ++v.flown;
        return false;
    }
    std::int32_t edi = 0;      // the obstacle height; kept from substep to substep where the cell does not set it
    House* house = nullptr;    // ebx: the last player house crossed; kept, cleared only by a neutral house
    std::uint8_t house_p = 0;  // its player
    float i = 0.0f;
    for (;;) {
        const auto x = static_cast<float>(static_cast<double>(i) * v.vx / static_cast<double>(sub) + bx);
        const auto y = static_cast<float>(static_cast<double>(i) * v.vy / static_cast<double>(sub) + by);
        v.cx = x, v.cy = y;
        const Cell c = WorldToCell(x, y);
        if (c.x < 0 || c.y < 0 || c.x >= s.map.width || c.y >= s.map.height) return true;  // off the map: no damage
        auto gh = static_cast<std::uint8_t>(GroundHeightAt(s, x, y));
        const std::uint16_t bp = s.map.objects[static_cast<std::size_t>(c.y) * s.map.width + c.x];
        if (v.kind == 8) {
            gh = 0, edi = 0;
        } else if (bp == 0) {
            edi = 0;
            if (Obstacle_UnderCell(s.map, c.x, c.y)) {
                if (v.cz >= 55.0f && v.cz <= 95.0f && Shell_HitShip(s, x, y, v.cz, v)) {
                    v.no_effect = 1;
                    return true;
                }
            } else if (v.kind == 9) {
                Unported(s, orig::kShellStep);
            }
        } else if (bp >= 1000 && bp < 9000) {
            std::uint8_t hp = 0;
            House* h = CellHouse(s, bp, &hp);
            if (h == nullptr) {
                Unported(s, orig::kShellStep);  // a house slot the port does not have
                edi = 0;
            } else {
                house = h, house_p = hp;

                if (h->is_destroyed != 0) {
                    edi = 20;
                } else {
                    // The point in the sprite: both differences stay on the x87 stack, the results are f32.
                    const HouseType& ht = Type_Of(s, hp, *h);
                    const auto ax = static_cast<float>(static_cast<double>(x) -
                                                       (static_cast<double>(h->x) - static_cast<double>(ht.field_140)));
                    const auto ay = static_cast<float>((static_cast<double>(y) - static_cast<double>(gh)) -
                                                       (static_cast<double>(h->y) - static_cast<double>(ht.field_142)));
                    edi = 50;
                    if (static_cast<double>(ht.sprite_w) <= static_cast<double>(ax) || ax < 0.0f) edi = 0;
                    if (static_cast<double>(ht.sprite_h) <= static_cast<double>(ay) || ay < 0.0f) edi = 0;



                    if (edi != 0 && h->depth_phase != 0xff) {
                        const HouseDepthMap* m = House_DepthMap(s, hp, *h);
                        if (m == nullptr) {
                            Unported(s, orig::kShellStep);  // a depth map that was not loaded: 50 as if null
                        } else {
                            const std::uint16_t d = m->spans
                                                        ? Depth_Span(s, *m, Ftol(ax), Ftol(ay))
                                                        : Depth_Raw(s, *m,
                                                                    static_cast<std::size_t>(Ftol(ay)) * ht.sprite_w +
                                                                        static_cast<std::size_t>(Ftol(ax)));
                            edi = d < (m->spans ? 0x8000 : 0x7fff) ? d : 0;
                        }
                    }
                }
            }
        } else if (bp >= 9000 && bp < 9500) {  // a tree: one random number, sometimes 30 high
            const std::uint16_t r = Rng_Next(s);
            const std::int32_t k = Ftol(static_cast<double>(sub) + 1.0);
            if (r % (5 * k) == 1) {
                if (v.flown > 2) edi = 30;
            } else {
                edi = 0;
            }
        } else if (bp >= 0x65 && bp < 1000) {
            edi = 60;  // a neutral house
            house = nullptr;
        } else {
            edi = 0;
        }
        const double t = static_cast<double>(i) / static_cast<double>(sub) + n;
        const double tt = t / static_cast<double>(v.a4);
        const auto z = static_cast<float>((tt * v.vz - tt * tt * v.g * 0.5) + v.z0);
        v.cz = z;
        const auto obstacle = static_cast<std::uint16_t>(gh + edi);
        if (static_cast<double>(obstacle) >= static_cast<double>(z) + 15.0) {  // hit an obstacle
            if (house != nullptr) {
                Shell_SnapToHouse(s, house_p, *house, &v.cx, &v.cy, &v.cz);
                v.fragments = 0;
            } else {
                v.cz = static_cast<float>(static_cast<std::int32_t>(obstacle) + 15);
            }
            DamageForUnits(s, v.cx, v.cy, v.cz, v.fragments, v.damage, v.radius, house, house_p, &v, 0);
            return true;
        }
        if (t >= v.flight) {  // landed
            if (v.kind == 9) {
                v.no_effect = 1;
                return true;
            }
            if (static_cast<double>(gh) >= static_cast<double>(z)) v.cz = static_cast<float>(gh + 5);
            DamageForUnits(s, v.cx, v.cy, v.cz, v.fragments, v.damage, v.radius, house, house_p, &v, 0);
            return true;
        }
        i = static_cast<float>(static_cast<double>(i) + 1.0);
        if (!(i < sub)) break;
    }
    ++v.flown;
    return false;
}



std::size_t Shell_Fragments(GameState& s, const Shell& v) {
    if (v.fragments == 0 || v.fragments > 3) return 0;
    const std::uint8_t* row = kFragmentRows[v.fragments - 1];
    ShellArgs a;  // player 0xff, no owner, damage and speed from the kind table
    a.x0 = v.cx, a.y0 = v.cy, a.z0 = v.cz;
    std::size_t made = 0;
    for (std::uint8_t k = 0; k < row[0]; ++k) {
        const int r0 = Rng_Next(s) % 50;
        const int r1 = Rng_Next(s) % 360;
        double rad = static_cast<double>(r0) * static_cast<double>(0.01f) * static_cast<double>(v.radius);
        rad = rad + rad;
        const double ang = static_cast<double>(r1) * static_cast<double>(0.017453292f);
        a.x1 = static_cast<float>(dmath::cos(ang) * rad + static_cast<double>(a.x0));
        a.y1 = static_cast<float>(dmath::sin(ang) * rad + static_cast<double>(a.y0));
        a.range = static_cast<float>(rad + 1.0);
        a.z1 = static_cast<float>(GroundHeightAt(s, a.x1, a.y1));
        a.kind = row[1 + k];
        const Shell f = Shell_Init(s, a);
        if (f.fail != 0) continue;
        s.shells.insert(s.shells.begin(), f);
        ++made;
    }
    return made;
}

}  // namespace




void Effect_Damage(GameState& s, float x, float y, float z, std::int32_t dmg, std::int32_t r, std::uint8_t cc,
                   bool gas) {
    DamageForUnits(s, x, y, z, cc, dmg, r, nullptr, 0, nullptr, gas ? 2 : 1);
}



void Shells_Update(GameState& s) {
    for (std::size_t k = 0; k < s.shells.size();) {
        if (!Shell_Step(s, s.shells[k])) {
            ++k;
            continue;
        }
        const Shell v = s.shells[k];
        if (v.gas != 0) {

            GasCloud g;
            g.x = Ftol(v.cx), g.y = Ftol(v.cy), g.life = 1500;
            const float f = v.player < s.players.size() ? s.players[v.player].factor_9b32f8 : 1.0f;
            g.max_radius = static_cast<std::uint16_t>(Ftol(static_cast<double>(f) * static_cast<double>(600.0f)));
            g.step = 2, g.puffs = 0x10, g.damage = 50, g.damage_radius = 50;
            Gas_Add(s, g);
        } else if (v.no_effect == 0) {

            if (v.kind != 7) Rng_Next(s);
            const Cell c = WorldToCell(v.cx, v.cy);
            if (c.x >= 0 && c.y >= 0 && c.x < s.map.width && c.y < s.map.height) {
                Event_ShellExplosion(s, v);


                const std::uint16_t obj = s.map.objects[static_cast<std::size_t>(c.y) * s.map.width + c.x];
                const bool wet =
                    Obstacle_UnderCell(s.map, c.x, c.y) && (obj < 0x65 || obj > 10999 || (obj >= 9000 && obj < 0x25e4));
                if (v.fragments != 0 && !wet) k += Shell_Fragments(s, v);
            }
        }
        s.shells.erase(s.shells.begin() + static_cast<std::ptrdiff_t>(k));
    }
}

}  // namespace antanta::sim
