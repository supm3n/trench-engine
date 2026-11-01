// Transports (milestone 4 slice L, docs/transports.md): boarding the transport ship (type 0x11) and the motor
// vehicle (type 0x18), the passengers, unloading, passengers lost with their transport, and the computer player's
// landing turn. x87 values that stay on the stack are double (the game runs at 53-bit precision); every float store
// is a float cast.
#include <cmath>

#include "dmath.hpp"
#include "movement.hpp"

namespace antanta::sim {

namespace {

constexpr float kHalfPi = 1.5707964f;


void RotateZ(float& x, float& y, float a) {
    const double sn = dmath::sin(static_cast<double>(a)), cs = dmath::cos(static_cast<double>(a));
    const double vx = x, vy = y;
    x = static_cast<float>(cs * vx - sn * vy);
    y = static_cast<float>(vx * sn + cs * vy);
}



std::size_t Capacity(const Unit& t) { return t.type == 0x18 ? 50 : 500; }

void SetTransporter(Unit& u, const UnitRef& t) {
    u.transporter = t;
    u.in_transport = true;
}

void ClearTransporter(Unit& u) {
    u.transporter = UnitRef{};
    u.in_transport = false;
}


Unit* TransporterOf(GameState& s, const Unit& u) { return u.in_transport ? Unit_Find(s, u.transporter) : nullptr; }

}  // namespace

void Unit_SetFutureTransporter(Unit& u, const UnitRef& t) {
    u.future_transporter = t;
    u.has_future_transporter = true;
}

void Unit_ClearFutureTransporter(Unit& u) {
    u.future_transporter = UnitRef{};
    u.has_future_transporter = false;
}

Unit* Unit_FutureTransporter(GameState& s, const Unit& u) {
    return u.has_future_transporter ? Unit_Find(s, u.future_transporter) : nullptr;
}



bool Unit_MayBoard(const GameState& s, std::uint8_t player, const Unit& u, const Unit& t) {
    const UnitType& ut = Unit_Type(s, player, u);
    if (t.type != 0x18) return ut.field_00b != 0;
    const std::uint8_t m = ut.move_class;
    return m == 2 || m == 7 || m == 9 || m == 16 || u.type == 0 || u.type == 5;
}





bool BoardPointShip(GameState& s, const Unit& t, float& x, float& y, std::uint8_t up, Unit* u) {
    float vx = 5.0f, vy = 0.0f;
    RotateZ(vx, vy, static_cast<float>(static_cast<double>(t.angle) + static_cast<double>(kHalfPi)));
    const float ny = -vy;
    x = static_cast<float>(static_cast<double>(vx) * static_cast<double>(10.0f) + t.x);
    y = static_cast<float>(static_cast<double>(ny) * static_cast<double>(10.0f) + t.y);
    const std::int32_t W = s.map.width, H = s.map.height;
    for (int i = 0; i < 0x5a; ++i) {
        x = static_cast<float>(static_cast<double>(vx) + x);
        y = static_cast<float>(static_cast<double>(ny) + y);
        const Cell c = WorldToCell(x, y);
        if (c.x < 0 || c.y < 0 || c.x >= W || c.y >= H) continue;
        const std::int32_t idx = W * c.y + c.x;
        const bool ok = u != nullptr ? TUnit_CanEnterCell(s, up, *u, idx)
                                     : s.map.pass_land[static_cast<std::size_t>(idx)] == 0;
        if (ok) return true;
    }
    if (u != nullptr) {
        x = u->x, y = u->y;
        u->work = 0xff;
        Unit_ClearFutureTransporter(*u);
    }
    return false;
}





void BoardPointVehicle(GameState& s, const Unit& v, float& x, float& y, const Unit& u) {
    static constexpr std::uint8_t kDirs[4] = {4, 1, 3, 2};
    std::array<std::int32_t, 8> pt{};
    std::array<float, 4> d{};
    const std::int32_t W = s.map.width, H = s.map.height;
    for (int k = 0; k < 4; ++k) {
        const std::uint8_t dir = kDirs[k];
        const std::int32_t my = v.map_y;
        const auto& s1 = (my & 1) == 0 ? kDirCellEven[dir] : kDirCellOdd[dir];
        std::int32_t px = s1[0] + v.map_x, py = s1[1] + my;
        const auto& s2 = (py & 1) == 0 ? kDirCellEven[dir] : kDirCellOdd[dir];
        px += s2[0], py += s2[1];
        std::int32_t wx = 100000, wy = 100000;
        if (px >= 0 && py >= 0 && px < W && py < H && s.map.pass_land[static_cast<std::size_t>(py * W + px)] == 0) {
            wx = (px * 2 - (~py & 1)) * 0x30, wy = py * 0x18;
        }
        wx += 0x30;
        pt[static_cast<std::size_t>(2 * k)] = wx, pt[static_cast<std::size_t>(2 * k + 1)] = wy;
        const double dx = static_cast<double>(wx) - u.x, dy = static_cast<double>(wy) - u.y;
        d[static_cast<std::size_t>(k)] = static_cast<float>(std::sqrt(dx * dx + dy * dy));
    }
    const float m = std::min(std::min(d[0], d[1]), std::min(d[2], d[3]));
    const double lim = static_cast<double>(m) + static_cast<double>(0.1f);
    std::size_t k = 3;
    for (std::size_t i = 0; i < 3; ++i) {
        if (static_cast<double>(d[i]) < lim) {
            k = i;
            break;
        }
    }
    x = static_cast<float>(pt[2 * k]);
    y = static_cast<float>(pt[2 * k + 1]);
    if (x > 90000.0f && y >= 90000.0f) {
        const Cell c = RelocateTargetLand(s, Cell{v.map_x, v.map_y});
        x = static_cast<float>((c.x * 2 - (~c.y & 1)) * 0x30 + 0x2d);
        y = static_cast<float>(c.y * 0x18);
    }
}


bool Transport_CanUnload(GameState& s, const Unit& t) {
    if (t.type == 0x18) return true;
    float x = 0, y = 0;
    return BoardPointShip(s, t, x, y, 0xff, nullptr);
}




bool Transport_Board(GameState& s, std::uint8_t up, Unit& u, std::uint8_t tp, Unit& t, bool by_player) {
    float bx = 0, by = 0;
    bool ok = true;
    if (t.type == 0x18) {
        BoardPointVehicle(s, t, bx, by, u);
    } else {
        ok = BoardPointShip(s, t, bx, by, up, &u);
    }
    const double dx = static_cast<double>(bx) - u.x, dy = static_cast<double>(by) - u.y;
    const double d = std::sqrt(dx * dx + dy * dy);
    if (by_player && Player_IsYou(s, up) && (!ok || d > static_cast<double>(1500.0f))) {
        Event_TransportRange(s, up);
        return false;
    }
    u.field_142 = 100;
    Unit_SetFutureTransporter(u, Unit_Ref(tp, t));
    u.work = 0x15;
    if (d >= static_cast<double>(20.0f)) {
        TUnit_MoveTo(s, up, u, bx, by, 0);
        u.field_086 = 0;
        u.field_093 = 0;
    } else {
        EventUnitEndMove(s, up, u);
    }
    return true;
}




bool Transport_PutIn(GameState& s, std::uint8_t up, Unit& u, std::uint8_t tp, Unit& t) {
    if (u.in_transport) return false;  // a log line in the original
    if ((t.type != 0x18 && t.type != 0x11) || t.passengers.size() >= Capacity(t)) {
        TUnit_OrderEnd(s, up, u, 1);
        return false;
    }
    u.has_attack_unit = false, u.player_unit_attack = 0xff;
    u.player_house_attack = 0xff, u.has_house_attack = false;
    t.passengers.push_back(Unit_Ref(up, u));
    TUnit_ClearPath(u);
    TUnit_StopMoving(s, up, u);
    u.work = 0x14;
    SetTransporter(u, Unit_Ref(tp, t));
    s.deselected.push_back(Unit_Ref(up, u));  // DeleteSelection when it is selected
    return true;
}

namespace {




void Transport_Place(GameState& s, std::uint8_t up, Unit& u, const Unit& t) {
    const float tx = t.x, ty = t.y;
    u.work = 0xff;
    u.x = static_cast<float>(static_cast<double>(Rng_Next(s) % 10) + tx - static_cast<double>(5.0f));
    u.y = static_cast<float>(static_cast<double>(Rng_Next(s) % 10) + ty - static_cast<double>(50.0f));
    TUnit_ClearPath(u);
    if (t.type == 0x18) {
        BoardPointVehicle(s, t, u.x, u.y, u);
    } else if (!BoardPointShip(s, t, u.x, u.y, up, &u)) {
        u.work = 0x14;
        return;
    }
    const float dx = static_cast<float>(static_cast<double>(u.x) - tx);
    const float dy = static_cast<float>(static_cast<double>(u.y) - ty);
    const double len = std::sqrt((static_cast<double>(dx) * dx + static_cast<double>(dy) * dy) + 0.0);
    const double inv = static_cast<double>(1.0f) / len;
    const float ax = static_cast<float>(static_cast<double>(dx) * inv * static_cast<double>(50.0f));
    const float ay = static_cast<float>(static_cast<double>(static_cast<float>(inv * dy)) * static_cast<double>(50.0f));
    u.x = static_cast<float>(static_cast<double>(ax) + u.x);
    u.y = static_cast<float>(static_cast<double>(ay) + u.y);
    TUnit_ClearPath(u);
    u.field_142 = 0x14;
    TUnit_UpdateCellIfMoved(s, up, u);
    EventUnitEndMove(s, up, u);
}

}  // namespace


bool Transport_GetOut(GameState& s, std::uint8_t up, Unit& u) {
    Unit* t = TransporterOf(s, u);
    if (t == nullptr || u.work != 0x14) return false;  // a log line in the original
    std::vector<UnitRef>& list = t->passengers;
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (list[i].player != up || list[i].serial != u.serial) continue;
        Transport_Place(s, up, u, *t);
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
        ClearTransporter(u);
        return true;
    }
    u.work = 0xff;
    ClearTransporter(u);
    return false;
}




bool Transport_EndMove(GameState& s, std::uint8_t up, Unit& u) {
    Unit* f = u.work == 0x15 ? Unit_FutureTransporter(s, u) : nullptr;
    if (f == nullptr || f->is_deleted != 0) {
        if (u.has_future_transporter) Unit_ClearFutureTransporter(u);
        return false;
    }
    const std::uint8_t fp = u.future_transporter.player;
    float bx = 0, by = 0;
    bool give_up = false;
    if (f->type == 0x18) {
        BoardPointVehicle(s, *f, bx, by, u);
    } else if (!BoardPointShip(s, *f, bx, by, up, &u)) {
        give_up = true;
    }
    if (!give_up) {
        const double dx = static_cast<double>(u.x) - bx, dy = static_cast<double>(u.y) - by;
        if (std::sqrt(dx * dx + dy * dy) < static_cast<double>(20.0f)) {
            const bool in = Transport_PutIn(s, up, u, fp, *f);
            Unit_ClearFutureTransporter(u);
            if (in) return true;
            u.work = 0xff;
            return false;
        }
        if (u.field_145 < 4) {
            Transport_Board(s, up, u, fp, *f, true);
            return true;
        }
    }
    u.work = 0xff;
    Unit_ClearFutureTransporter(u);
    EventUnitEndMove(s, up, u);
    return true;
}



void Transport_IdleCheck(GameState& s, Unit& u) {
    if (!u.has_future_transporter) return;
    if (u.work == 0x15) {
        const Unit* f = Unit_FutureTransporter(s, u);
        if (f != nullptr && f->is_deleted == 0) return;
    } else {
        u.work = 0xff;
    }
    Unit_ClearFutureTransporter(u);
}




void UI_Board(GameState& s, std::uint8_t p, const std::vector<Unit*>& list, std::uint8_t tp, Unit& t,
              std::uint32_t parent) {
    if (!Transport_CanUnload(s, t)) return;
    const std::size_t free = Capacity(t) - std::min(Capacity(t), t.passengers.size());
    std::size_t n = 0;
    for (Unit* u : list) {
        if (n >= free) break;
        if (u == &t || !Unit_MayBoard(s, p, *u, t)) continue;
        Order_LogBoard(s, parent, p, *u, Unit_Ref(tp, t));
        Transport_Board(s, p, *u, tp, t, true);
        if (u->work == 0x15) s.deselected.push_back(Unit_Ref(p, *u));  // DeleteSelection when it is selected
        ++n;
    }
}



void Transport_Unload(GameState& s, std::uint8_t p, std::span<const UnitRef> selection) {
    for (const UnitRef& r : selection) {
        Unit* t = Unit_Find(s, UnitRef{p, r.id, r.serial});
        if (t == nullptr || t->passengers.empty() || !Transport_CanUnload(s, *t)) continue;
        for (std::size_t n = t->passengers.size(); n > 0 && !t->passengers.empty(); --n) {
            const UnitRef q = t->passengers.front();
            Unit* u = Unit_Find(s, q);
            if (u == nullptr || !Transport_GetOut(s, q.player, *u)) break;  // a passenger the sim lost (never)
        }
    }
}



void Transport_OnKilled(GameState& s, std::uint8_t player, Unit& u, std::uint8_t killer) {
    if (u.work == 0x14) {
        Transport_GetOut(s, player, u);
        u.field_108 = 0;
    }
    for (std::size_t n = u.passengers.size(); n > 0 && !u.passengers.empty(); --n) {
        const UnitRef q = u.passengers.front();
        Unit* v = Unit_Find(s, q);
        if (v == nullptr) {
            u.passengers.erase(u.passengers.begin());  // a passenger the sim lost (never)
            continue;
        }
        OnUnitKilled(s, q.player, *v, killer);
        if (!u.passengers.empty() && u.passengers.front().serial == q.serial) break;  // it stayed aboard (never)
    }
    u.passengers.clear();
}



void Transport_ClearLinks(Unit& v, const UnitRef& dead) {
    const bool future = v.has_future_transporter && v.future_transporter.player == dead.player &&
                        v.future_transporter.serial == dead.serial;
    const bool in = v.in_transport && v.transporter.player == dead.player && v.transporter.serial == dead.serial;
    if (!future && !in) return;
    Unit_ClearFutureTransporter(v);
    ClearTransporter(v);
    v.work = 0xff;
}



bool Transport_TurnToLand(GameState& s, std::uint8_t p, Unit& t) {
    const float save = t.angle;
    float first = -1.0f, last = -1.0f, a = 0.0f, x = 0, y = 0;
    for (;;) {
        t.angle = a;
        if (BoardPointShip(s, t, x, y, 0xff, nullptr)) {
            if (first < 0.0f) first = a;
            last = a;
        }
        const double next = static_cast<double>(a) + static_cast<double>(0.3926991f);
        a = static_cast<float>(next);
        if (!(next < static_cast<double>(6.2831855f))) break;
    }
    if (!(first < 0.0f)) {
        t.angle = static_cast<float>((static_cast<double>(last) - first) * static_cast<double>(0.5f) + first);
        if (BoardPointShip(s, t, x, y, 0xff, nullptr)) {
            t.angle = save;
            TUnit_OrderMove(s, p, t, x, y, 1);
            return true;
        }
    }
    t.angle = save;
    return false;
}

}  // namespace antanta::sim
