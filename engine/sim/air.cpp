





#include <cmath>

#include "dmath.hpp"
#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {
namespace {

constexpr float kTwoPi = 6.2831855f;
constexpr float kPi = 3.1415927f;
constexpr float kPiOver2 = 1.5707964f;
constexpr float kFourOverPi = 1.2732395f;
constexpr float kPiOver4 = 0.7853982f;
constexpr float kWrapTop = 6.2821856f;

double D(float v) { return static_cast<double>(v); }
float F(double v) { return static_cast<float>(v); }

double Dist(const Unit& a, const Unit& b) {
    const double dx = D(a.x) - b.x, dy = D(a.y) - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

double DistPt(float a, float b, float c, float d) {
    const double dx = D(a) - c, dy = D(b) - d;
    return std::sqrt(dx * dx + dy * dy);
}


bool NearWaypoint(const UnitType& t, std::uint8_t player, const Unit& u) {
    const double dx = D(u.x) - u.dest_x, dy = D(u.y) - u.dest_y;
    return std::sqrt(dy * dy + dx * dx) < t.waypoint_radius[player];
}


void WrapAngle(float& a) {
    if (a < 0.0f) {
        a = F(D(a) + D(kTwoPi));
        return;
    }
    if (!(a < kTwoPi)) a = F(D(a) - D(kTwoPi));
    if (!(a < kWrapTop)) a = 0.0f;
}

// The same test on a value kept on the x87 stack (AirRotate's first two wraps of the target angle).
double WrapStack(double a) {
    if (a < 0.0) return a + D(kTwoPi);
    if (!(a < D(kTwoPi))) a -= D(kTwoPi);
    if (!(a < D(kWrapTop))) a = 0.0;
    return a;
}

// atan2(DestX - X, DestY - Y) (fpatan), plus 2pi when negative: the heading to the waypoint.
double Heading(const Unit& u) {
    double a = dmath::atan2(D(u.dest_x) - u.x, D(u.dest_y) - u.y);
    if (a < 0.0) a += D(kTwoPi);
    return a;
}


int Sign(float v) { return v > 0.0f ? 1 : (v < 0.0f ? -1 : 0); }


bool HasIntent(const Unit& u) {
    const std::uint8_t nw = u.num_work;
    return nw == 6 || nw == 0x12 || nw == 2 || nw == 0x14 || nw == 0x15 || u.player_unit_attack != 0xff ||
           u.player_house_attack != 0xff || u.work == 8;
}

House* InHouse(GameState& s, const Unit& u) {
    if (!u.in_house || u.house_player >= s.players.size()) return nullptr;
    Player& p = s.players[u.house_player];
    return u.house_slot < p.houses.size() ? &p.houses[u.house_slot] : nullptr;
}


void AirVelocity(Unit& u, double speed) {
    if (u.dest_y == u.y) {
        u.vel_y = 0.0f;
    } else {
        const double dx = D(u.dest_x) - u.x, dy = D(u.dest_y) - u.y;
        u.vel_y = F(speed / std::sqrt(dx * dx / dy / dy + 1.0));
    }
    if (u.dest_y != u.y) {
        u.vel_x = F((D(u.dest_x) - u.x) * D(u.vel_y) / (D(u.dest_y) - u.y));
    } else {
        u.vel_x = F(speed);
    }
    if (!(u.y <= u.dest_y)) u.vel_y = -u.vel_y;
    u.vel_x = std::fabs(u.vel_x);
    if (!(u.x <= u.dest_x)) u.vel_x = -u.vel_x;
}


void SetRunStart(Unit& u) {
    u.field_050 = F(D(u.x) - (D(u.dest_x) - u.x) * D(0.05f));
    u.field_054 = F(D(u.y) - (D(u.dest_y) - u.y) * D(0.05f));
}

// The share of the run done: sqrt(|pos - start|^2 / max(|dest - start|^2, 0.01f)).
double RunShare(const Unit& u) {
    const double ex = D(u.dest_x) - u.field_050, ey = D(u.dest_y) - u.field_054;
    double d2 = ey * ey + ex * ex;
    if (!(d2 > D(0.01f))) d2 = D(0.01f);
    const double px = D(u.x) - u.field_050, py = D(u.y) - u.field_054;
    return std::sqrt((py * py + px * px) / d2);
}


void AirTurnTo(Unit& u, const UnitType& t, float a) {
    const float tr = t.turn_rate;
    if (std::fabs(D(u.angle) - a) < D(tr)) return;
    if (u.field_07c != 0) {
        --u.field_07c;
        return;
    }
    u.field_07c = 3;
    double A = u.angle;
    if (!(A <= D(kTwoPi))) A -= D(kTwoPi);
    const bool sub = A > D(a) ? (A - a) < D(kPi) : !((D(a) - A) < D(kPi));
    u.angle = sub ? F(D(u.angle) - tr) : F(D(tr) + u.angle);
    if (!(u.angle < kTwoPi)) u.angle = F(D(u.angle) - D(kTwoPi));
    if (u.angle < 0.0f) u.angle = F(D(u.angle) + D(kTwoPi));
    double v = D(u.angle) + D(kPi);
    if (!(v < D(kTwoPi))) v -= D(kTwoPi);
    const auto o = static_cast<std::int8_t>(Ftol(D(kFourOverPi) * v));
    u.orient = static_cast<std::uint8_t>(o);
    const double q = (v - static_cast<double>(o) * D(kPiOver4)) / (D(kPiOver4) / static_cast<double>(t.anim_size[0]));
    u.num_sprite = static_cast<float>(static_cast<std::int16_t>(Ftol(q)));
    // o >= 8 or a frame past AnimationSize[0]: the original only logs.
}


double CircleFactor(const UnitType& t, const Unit& u) { return HasIntent(u) && t.move_class != 11 ? D(3.0f) : D(1.4f); }



void AirRotate(Unit& u, const UnitType& t, std::uint8_t player) {
    const float X = u.x, Y = u.y;
    const double ang = WrapStack(WrapStack(dmath::atan2(D(u.dest_x) - X, D(u.dest_y) - Y)));
    const double R = t.turn_radius, tr = t.turn_rate;
    const double A = u.angle;
    const bool plus = ang > A ? (ang - A) < D(kPi) : !((A - ang) < D(kPi));
    const double c = dmath::cos(A), sn = dmath::sin(A);
    float cx = 0, cy = 0;
    double cyu = 0;
    if (plus) {
        cx = F(c * R + X);
        cyu = D(Y) - sn * R;
    } else {
        cx = F(D(X) - c * R);
        cyu = sn * R + Y;
    }
    cy = F(cyu);
    const double ey = cyu - u.dest_y, ex = D(cx) - u.dest_x;
    const float df = F(std::sqrt(ex * ex + ey * ey));
    if (CircleFactor(t, u) * R >= D(df)) {
        const double a = A - D(kPiOver2);
        u.vel_x = F(dmath::cos(a) * t.speed[player]);
        u.vel_y = F(-(dmath::sin(a) * t.speed[player]));
        u.x = F(D(u.vel_x) + u.x);
        u.y = F(D(u.y) + u.vel_y);
    } else {
        u.angle = plus ? F(tr + u.angle) : F(D(u.angle) - tr);
        WrapAngle(u.angle);
        WrapAngle(u.angle);
        const double na = u.angle;
        double vx = 0, vy = 0;
        if (plus) {
            vx = (D(cx) - dmath::cos(na) * R) - X;
            vy = (dmath::sin(na) * R + cy) - Y;
        } else {
            vx = (dmath::cos(na) * R + cx) - X;
            vy = (D(cy) - dmath::sin(na) * R) - Y;
        }
        u.vel_x = F(vx), u.vel_y = F(vy);
        u.x = F(vx + u.x);
        u.y = F(vy + u.y);
        float a2 = F(D(u.angle) + D(3.1426928f));
        WrapAngle(a2);
        WrapAngle(a2);
        const double tt = D(a2) * D(kFourOverPi);
        const std::int32_t o = Ftol(tt);
        u.orient = static_cast<std::uint8_t>(o);
        u.num_sprite = F((tt - static_cast<double>(static_cast<std::int8_t>(o))) * t.anim_size[0]);
    }
    if (static_cast<std::uint8_t>(Ftol(u.num_sprite)) >= t.anim_size[0] || u.num_work != 0) {
        u.num_sprite = 0.0f;  // the original logs the frame
        u.num_work = 0;
    }
}



bool FlightStep(GameState& s, std::uint8_t player, Unit& u, const UnitType& t) {
    if (u.field_07c != 0) {
        --u.field_07c;
        return true;
    }
    if (u.work == 0xff) return false;
    const std::uint8_t bl = u.type == 10 ? std::uint8_t{4} : std::uint8_t{0};
    // AnimationSpeed[(s8)NumWork]; aircraft use 0 and 0xe.
    const float as = u.num_work < t.anim_speed.size() ? t.anim_speed[u.num_work] : 0.0f;
    switch (u.work) {
        case 3: return true;
        case 5:
            if (u.field_024 == 0.0f) {
                if (NearWaypoint(t, player, u)) {
                    if (static_cast<double>(bl) > u.num_sprite) {
                        u.x = F(D(u.vel_x) + u.x), u.y = F(D(u.vel_y) + u.y);
                        u.dest_x = F(D(u.dest_x) + u.vel_x), u.dest_y = F(D(u.dest_y) + u.vel_y);
                        TUnit_UpdateGroundHeight(s, player, u);
                        u.num_sprite = F(D(as) + u.num_sprite);
                        return true;
                    }
                    const float ox = u.dest_x, oy = u.dest_y;
                    u.x = ox, u.y = oy, u.field_050 = ox, u.field_054 = oy;
                    u.dest_x = F(static_cast<double>(Sign(u.vel_x) * static_cast<std::int32_t>(t.field_48a)) + ox);
                    u.dest_y = F(static_cast<double>(Sign(u.vel_y) * static_cast<std::int32_t>(t.field_48a)) + oy);
                    u.field_024 = 1.01f;
                    u.field_07c = 1;
                    return true;
                }
                const double f = RunShare(u);
                u.x = F(f * u.vel_x + u.x);
                u.y = F(f * u.vel_y + u.y);
                TUnit_UpdateGroundHeight(s, player, u);
                return true;
            }
            if (D(u.z) + u.field_024 < D(400.0f)) {
                if (!(static_cast<double>((t.anim_size[14] >> 1) + 1) <= u.num_sprite))
                    u.num_sprite = F(D(as) + u.num_sprite);
                const double f = RunShare(u);
                u.field_024 = static_cast<float>(static_cast<std::int16_t>(Ftol(f * D(400.0f) + D(1.0f))));
                u.x = F(D(u.vel_x) + u.x), u.y = F(D(u.vel_y) + u.y);
                return true;
            }
            {
                const double sum = D(as) + u.num_sprite;
                u.num_sprite = F(sum);
                if (!(sum < static_cast<double>(t.anim_size[14]))) {
                    u.work = 0xff, u.num_work = 0, u.num_sprite = 0.0f;
                    // Our rules (docs/bugs.md B276): the airborne aircraft takes the queued rally point
                    // (THouse_ReleaseUnit, or Air_ProduceUnit for a full hangar), as a ground unit does at the end of
                    // its door walk. The original's EventUnitEndMove circles an aircraft before it reads the queue, so
                    // its new planes stay over the hangar.
                    if (!s.original_rules && u.field_0bc != 0 && !u.in_group) {
                        const Unit::Waypoint w = u.waypoints[0];
                        for (std::size_t i = 1; i < u.field_0bc && i < u.waypoints.size(); ++i) {
                            u.waypoints[i - 1] = u.waypoints[i];
                        }
                        --u.field_0bc;
                        TUnit_GoToWaypoint(s, player, u, w.x, w.y, w.work, w.field_0ca);
                    }
                    return false;
                }
                u.x = F(D(u.vel_x) + u.x), u.y = F(D(u.vel_y) + u.y);
                u.dest_x = F(D(u.dest_x) + u.vel_x), u.dest_y = F(D(u.dest_y) + u.vel_y);
                return true;
            }
        case 0xc:
        case 0xd:
        case 2:
        case 6: break;
        default: return false;
    }
    House* h = InHouse(s, u);
    if (h == nullptr) {
        Unported(s, orig::kFlightStep);  // the approach without a house: the original reads a null pointer
        return false;
    }
    if (u.work == 0xc) {
        if (!(std::fabs(D(u.x) - u.dest_x) < D(20.0f)) || !(std::fabs(D(u.y) - u.dest_y) < D(20.0f))) return false;
        u.work = 0xd;
        u.dest_x = F(static_cast<double>(t.field_48a) + h->exit_x);
        u.dest_y = F(static_cast<double>(t.field_48a) + h->exit_y);
        return true;
    }
    if (u.work == 0xd) {
        const double ang = Heading(u);
        if (std::fabs(D(u.x) - u.dest_x) <= D(t.speed[player]) && std::fabs(D(u.y) - u.dest_y) <= D(t.speed[player]))
            u.work = 2;
        if (u.work != 2) {
            if (D(t.turn_rate) * D(0.5f) > std::fabs(ang - u.angle)) {
                AirVelocity(u, TUnit_Speed(s, player, u, orig::kAirVelocity));
                u.x = F(D(u.vel_x) * D(0.33333334f) + u.x);
                u.y = F(D(u.vel_y) * D(0.33333334f) + u.y);
            }
            return false;
        }
    }
    if (u.work == 2) {
        u.x = u.dest_x, u.work = 6, u.y = u.dest_y;
        u.field_050 = u.dest_x, u.field_054 = u.dest_y;
        u.dest_x = h->exit_x, u.dest_y = h->exit_y;
        AirVelocity(u, TUnit_Speed(s, player, u, orig::kAirVelocity));
        return true;
    }
    // Work 6, the landing.
    if (u.field_024 < 1.0f) {
        if (NearWaypoint(t, player, u)) {
            PutInHouse(s, u.house_player, u, u.house_slot);
            return true;
        }
        if (D(u.num_sprite) > 0.01) {
            const double v = D(u.num_sprite) - as;
            u.num_sprite = F(v);
            if (!(v > 0.0)) u.num_sprite = 0.001f;
        }
        const double py = D(u.y) - u.field_054, px = D(u.x) - u.field_050;
        const double ey = D(u.dest_y) - u.field_054, ex = D(u.dest_x) - u.field_050;
        const double f = 1.0 - std::sqrt((px * px + py * py) / (ex * ex + ey * ey));
        u.x = F(f * u.vel_x + u.x);
        u.y = F(f * u.vel_y + u.y);
        TUnit_UpdateGroundHeight(s, player, u);
        return true;
    }
    if (NearWaypoint(t, player, u)) {
        u.x = u.dest_x, u.field_024 = 0.0f, u.y = u.dest_y;
        u.field_050 = u.dest_x, u.field_054 = u.dest_y;
        u.dest_x = h->x, u.dest_y = h->y;
        u.num_work = 0xe, u.orient = 1;
        u.num_sprite = F(static_cast<double>(bl) + D(1.0f));
        AirVelocity(u, TUnit_Speed(s, player, u, orig::kAirVelocity));
        return true;
    }
    const double f = RunShare(u);
    u.field_024 = static_cast<float>(static_cast<std::int16_t>(Ftol(D(400.0f) - f * D(400.0f))));
    if (u.field_024 < 0.0f) u.field_024 = 0.0f;
    u.x = F(D(u.vel_x) + u.x), u.y = F(D(u.vel_y) + u.y);
    AirTurnTo(u, t, 3.926991f);
    return true;
}

// The end of every aircraft MoveFunc: auto targeting back on near the ordered point, the cell (every 1000 ticks
// also when unchanged), and the altitude kept at 400 above the ground.
void FlightTail(GameState& s, std::uint8_t player, Unit& u) {
    const double fx = D(u.x) - u.final_x, fy = D(u.y) - u.final_y;
    if (std::sqrt(fy * fy + fx * fx) < D(700.0f) && u.player_unit_attack == 0xff && u.player_house_attack == 0xff &&
        u.field_006 == 0 && u.field_142 != 0) {
        u.field_006 = 1, u.field_142 = 0;
    }
    const Cell c = WorldToCell(u.x, u.y);
    if (c.y != u.map_y || c.x != u.map_x || s.tick % 1000 == u.frame_slot) TUnit_SetCell(s, player, u, c);
    if (!(D(u.field_024) + u.z < D(400.0f))) {
        TUnit_UpdateGroundHeight(s, player, u);
        u.field_024 = F((D(400.0f) - u.z) + D(0.01f));
    }
}


bool Strafable(GameState& s, std::uint8_t vp, const Unit& v, const Unit& u) {
    if (v.work == 3) return false;
    return !((u.type == 7 || u.type == 8) && Unit_Type(s, vp, v).move_class == 3 && v.type != 0x17);
}



bool StrafeNext(GameState& s, std::uint8_t player, Unit& u) {
    const std::uint8_t w = u.work;
    if (u.field_006 == 0 || w == 3 || w == 5 || w == 0xc || w == 0xd || w == 6 || w == 4) return false;
    const UnitType& t = Unit_Type(s, player, u);
    float px = u.x, py = u.y;
    if (u.player_unit_attack != 0xff && u.has_attack_unit) {
        if (const Unit* tg = AttackTarget(s, u)) {
            px = tg->x, py = tg->y;
        } else {
            Unported(s, orig::kStrafeNext);  // a stale target pointer
        }
    }
    const std::int32_t cells = static_cast<std::int32_t>(s.map.width) * s.map.height;
    Unit* found = nullptr;
    UnitRef fref;
    for (int step = 0; step < 20 && found == nullptr; ++step) {
        const Cell c = WorldToCell(px, py);
        const std::int32_t idx = c.y * static_cast<std::int32_t>(s.map.width) + c.x;
        if (idx >= 0 && idx < cells) {
            for (std::size_t k = 0; k < 40; ++k) {
                const std::vector<UnitRef>& list = s.map.cell_units[static_cast<std::size_t>(idx)];
                if (k >= list.size()) break;
                Unit* v = Unit_Find(s, list[k]);
                if (v == nullptr) {
                    Unported(s, orig::kStrafeNext);  // a stale cell entry
                    continue;
                }
                if (!(v->hp >= 0.0f) || !Strafable(s, list[k].player, *v, u)) continue;
                if (!(Dist(u, *v) < static_cast<double>(t.range[player]))) continue;
                const double ex = D(v->x) - px, ey = D(v->y) - py, ez = v->field_024;
                if (!(std::sqrt((ez * ez + ex * ex) + ey * ey) < 30.0)) continue;
                found = v, fref = list[k];
                break;
            }
        }
        if (found != nullptr) break;
        py = F(D(u.vel_y) * D(0.5f) + py);
        px = F(D(u.vel_x) * D(0.5f) + px);
    }
    if (found == nullptr) {
        TUnit_ClearTargets(u);
        u.work = 0xff;
        return false;
    }
    u.player_unit_attack = fref.player;
    u.attack_ref = fref, u.has_attack_unit = true;
    u.field_093 = 0, u.work = 7, u.field_0ca = 0xffff;
    u.is_stopped = 0, u.num_work = 0, u.player_house_attack = 0xff;
    u.dest_y = found->y, u.dest_x = found->x;
    return true;
}


bool OnMove(const Unit& u) { return u.field_006 == 0 && u.field_142 != 0; }

// Our rules (B277): a fighter shot at by an enemy plane answers it when it is on a move order without a target, or
// when its target is a ground unit, which it keeps to go back to. A fighter already on a plane, holding fire or
// without an order does as the original (the original tells no aircraft it is shot at).
void AnswerShot(GameState& s, std::uint8_t tp, Unit& t, std::uint8_t ap, Unit& a) {
    if (s.original_rules || t.is_deleted != 0 || Unit_Type(s, tp, t).move_class != 11 || !IsEnemy(s, tp, ap)) return;
    const std::uint8_t w = t.work;
    if (w == 3 || w == 5 || w == 0xc || w == 0xd || w == 6 || w == 4) return;
    if (t.player_unit_attack == 0xff) {
        if (OnMove(t)) Air_OnAttackedBy(s, tp, t, ap, a);
        return;
    }
    if (t.field_006 == 0 || !t.has_attack_unit) return;
    const Unit* g = AttackTarget(s, t);
    if (g == nullptr || Unit_Type(s, t.attack_ref.player, *g).is_air != 0) return;
    const UnitRef keep = t.attack_ref;
    Air_OnAttackedBy(s, tp, t, ap, a);
    t.return_target = keep;
}


void AirShot(GameState& s, std::uint8_t ap, Unit& a, std::uint8_t tp, Unit& t) {
    const UnitType& at = Unit_Type(s, ap, a);
    const double dy = D(a.y) - t.y, dx = D(a.x) - t.x;
    const double d = std::sqrt(dy * dy + dx * dx);
    if (d > static_cast<double>(at.range[ap])) return;
    const int r = Rng_Next(s) % 100;
    if (r <= at.accuracy[ap]) {
        t.hp = F(D(t.hp) - CalcHitDamage(s, ap, a, tp, t) * D(10.0f));
        const double e = D(a.field_08c) + D(0.1f);
        a.field_08c = F(e);
        if (t.hp < 0.0f) {
            a.field_08c = F(e + D(30.0f));
            OnUnitKilled(s, tp, t, ap);
            return;
        }
    }
    AnswerShot(s, tp, t, ap, a);  // our rules, hit or missed (B277)
}

// Our rules (B277): a fight our rules began, by a fighter on a move order or by one that keeps a ground target to go
// back to.
bool OurFight(const GameState& s, const UnitType& t, const Unit& u) {
    return !s.original_rules && t.move_class == 11 && u.player_unit_attack != 0xff &&
           (u.return_target.player != 0xff || OnMove(u));
}

// Our rules (B277): such a fight is over when the plane is down, has landed or is beyond twice the fighter's range.
bool FightOver(GameState& s, std::uint8_t player, const Unit& u, const UnitType& t) {
    const Unit* tg = AttackTarget(s, u);
    return tg == nullptr || tg->is_deleted != 0 || tg->work == 3 ||
           Dist(u, *tg) > 2.0 * static_cast<double>(t.range[player]);
}

// Our rules (B277): after the fight the fighter goes back to its ground target while that lives, else on with its
// move order to the same point; false when it has neither (the original's AttackEnd then decides).
bool ResumeAfterFight(GameState& s, std::uint8_t player, Unit& u) {
    const UnitRef g = u.return_target;
    u.return_target = UnitRef{};
    TUnit_ClearTargets(u);
    Unit* gu = g.player != 0xff ? Unit_Find(s, g) : nullptr;
    if (gu != nullptr && gu->is_deleted == 0 && gu->work != 3) {
        Air_OnAttackedBy(s, player, u, g.player, *gu);
        return true;
    }
    if (!OnMove(u)) return false;
    Air_OrderMove(s, player, u, u.final_x, u.final_y);
    return true;
}



void Strafe(GameState& s, std::uint8_t ap, Unit& a, std::uint8_t tp, Unit& t) {
    if (t.is_deleted != 0 || t.work == 3) {
        if (t.work == 3) TUnit_ClearTargets(a);
        StrafeNext(s, ap, a);
        return;
    }
    if (a.field_0ec == 0) return;     // no ammunition (never used up)
    Event_UnitSound(s, ap, a, 2, 4);
    if (Unit_Type(s, tp, t).is_air != 0) {
        AirShot(s, ap, a, tp, t);
        return;
    }
    float lx = F(D(t.x) - D(a.vel_x) * D(4.0f));
    float ly = F(D(t.y) - D(a.vel_y) * D(4.0f));
    const float nvy = -a.vel_y, nvx = -a.vel_x;
    a.field_08c = F(D(a.field_08c) + D(1.0f));
    for (std::uint16_t i = 1; i <= 14; ++i) {
        const int ra = (s.rng_table[s.rng_index] & 1) - 4;
        const int rb = (s.rng_table[static_cast<std::uint16_t>(s.rng_index + 1)] & 1) - 4;
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
        Bullet b;
        b.x = F(static_cast<double>(ra) * nvy + lx);
        b.y = F(static_cast<double>(rb) * nvx + ly);
        b.z = static_cast<float>(GroundHeightAt(s, b.x, b.y));
        b.owner = Unit_Ref(ap, a);
        b.delay = i;
        s.bullets.push_back(b);
        ly = F(D(ly) + a.vel_y);
        lx = F(D(lx) + a.vel_x);
    }
    if (t.hp < 0.0f) {
        a.field_08c = F(D(a.field_08c) + (Unit_Type(s, tp, t).is_air != 0 ? D(30.0f) : D(5.0f)));
        StrafeNext(s, ap, a);
    }
}



bool Bomber_CanBomb(GameState& s, std::uint8_t player, const Unit& u) {
    const std::int32_t w = s.map.width, h = s.map.height;
    if (!(u.map_x > 3 && u.map_y > 9 && u.map_x < w - 3 && u.map_y < h - 9)) return false;
    bool own = false, hflag = false, enemy = false;
    Cell row{u.map_x, u.map_y + 4};
    for (int r = 0; r < 5; ++r) {
        Cell c = row;
        for (int k = 0; k < 5; ++k) {
            const bool in = c.x >= 0 && c.y >= 0 && c.x < w && c.y < h;
            if (in) {
                const std::vector<UnitRef>& list = s.map.cell_units[static_cast<std::size_t>(c.y) * w + c.x];
                for (std::size_t i = 0; i < list.size() && i < 40; ++i) {
                    const Unit* v = Unit_Find(s, list[i]);
                    if (v == nullptr) {
                        Unported(s, orig::kBomberCanBomb);  // a stale cell entry
                        continue;
                    }
                    if (v->is_deleted != 0 || !(v->field_024 <= 1.0f)) continue;
                    if (list[i].player == player) {
                        own = true;
                    } else if (IsEnemy(s, player, list[i].player)) {
                        enemy = true;
                    }
                }
                const std::uint16_t obj = s.map.objects[static_cast<std::size_t>(c.y) * w + c.x];
                if (obj >= 1000 && obj < 9000) {
                    const std::size_t hp = obj / 1000u - 1u, slot = obj % 1000u;
                    if (hp < s.players.size() && slot < s.players[hp].houses.size()) {
                        const House& hh = s.players[hp].houses[slot];
                        if (hh.is_destroyed == 0 && IsEnemy(s, player, static_cast<std::uint8_t>(hp))) hflag = true;
                    } else {
                        Unported(s, orig::kBomberCanBomb);  // a house slot the port does not have
                    }
                }
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    if (own) return false;
    return hflag || enemy;
}



void DropBomb(GameState& s, std::uint8_t player, Unit& u) {
    if (u.type != 10) {
        Bomb_Fire(s, player, u, 0);
        return;
    }
    const float x0 = u.x, y0 = u.y;
    for (int i = 0; i < 5; ++i) {
        const double xv = static_cast<double>(i) * u.vel_x * D(5.0f) + x0;
        u.x = F(xv);
        u.y = F(static_cast<double>(i) * u.vel_y * D(5.0f) + y0);
        const int r0 = Rng_Next(s) % 10 - 5;
        u.x = F(static_cast<double>(r0) + xv);
        const int r1 = Rng_Next(s) % 10 - 5;
        u.y = F(static_cast<double>(r1) + u.y);
        Bomb_Fire(s, player, u, static_cast<std::uint16_t>(i * 5));
    }
    u.x = x0, u.y = y0;
}


void Dropped(GameState& s, std::uint8_t player, Unit& u, const UnitType& t) {
    DropBomb(s, player, u);
    u.field_092 = 1;
    u.field_0ee = t.reload[player];
}


void AimAhead(Unit& u) {
    u.field_0d4 = F(D(u.x) + D(2.0f));
    u.field_0d8 = F(D(u.y) + D(2.0f));
}


void MoveScoutFighter(GameState& s, std::uint8_t player, Unit& u, const UnitType& t) {
    if (u.num_work == 0) {
        // The trail (new_dust7, 0x30; docs/render.md 14): the fighter one puff at its point, the scout three along its
        // last step, each moved by CRT rand() % 3 (drawn for height, y, x in that order).
        const std::int32_t z0 = Ftol(D(u.field_024) + u.z);
        if (t.move_class == 1) {
            for (const float f : {0.0f, 0.33333334f, 0.6666667f}) {
                const std::int32_t z = Crt_rand(s) % 3 + z0;
                const std::int32_t y = Crt_rand(s) % 3 + (f == 0 ? Ftol(u.y) : Ftol(D(u.y) - D(u.vel_y) * D(f)));
                const std::int32_t x = Crt_rand(s) % 3 + (f == 0 ? Ftol(u.x) : Ftol(D(u.x) - D(u.vel_x) * D(f)));
                Event_Effect(s, 0, false, x, y, z, 0x30);
            }
        } else {
            Event_Effect(s, 0, false, Ftol(u.x), Ftol(u.y), z0, 0x30);
        }
    }
    // Our rules (docs/bugs.md B277): a fighter on a move order still looks for enemy planes (planes only) on the
    // original's 50-tick beat of the target search (without its zone throttle).
    if (!s.original_rules && t.move_class == 11 && OnMove(u) && u.player_unit_attack == 0xff && u.work != 3 &&
        s.tick % 50 == u.frame_slot) {
        Air_FindTarget(s, player, u, true);
    }
    if (!FlightStep(s, player, u, t)) {
        if (OurFight(s, t, u) && FightOver(s, player, u, t) && !ResumeAfterFight(s, player, u)) {
            TUnit_AttackEnd(s, player, u);
        }
        const double ang = Heading(u);
        if (u.field_0ee != 0) --u.field_0ee;
        if (D(t.turn_rate) * D(0.5f) <= std::fabs(ang - u.angle)) {
            AirRotate(u, t, player);
        } else {
            u.x = F(D(u.vel_x) + u.x);
            u.y = F(D(u.vel_y) + u.y);
            if (u.player_unit_attack != 0xff) {
                Unit* tg = AttackTarget(s, u);
                if (tg == nullptr) {
                    Unported(s, t.move_class == 1 ? orig::kMoveScout : orig::kMoveFighter);  // a null target pointer
                } else if (tg->is_deleted != 0) {
                    TUnit_AttackEnd(s, player, u);
                } else if (static_cast<double>(t.range[player]) > Dist(u, *tg) && u.field_0ee == 0) {
                    u.field_092 = 1;
                    Strafe(s, player, u, u.attack_ref.player, *tg);
                    u.field_0ee = t.reload[player];
                }
            }
        }
    }
    FlightTail(s, player, u);
}




void MoveBomber(GameState& s, std::uint8_t player, Unit& u, const UnitType& t) {
    if (u.num_work == 0) Event_Effect(s, 0, false, Ftol(u.x), Ftol(u.y), Ftol(D(u.z) + u.field_024), 0x30);  // trail
    if (!FlightStep(s, player, u, t)) {
        if (u.player_unit_attack != 0xff) {
            Unit* tg = AttackTarget(s, u);
            if (s.players[u.player_unit_attack].is_lost != 0 || tg == nullptr || tg->is_deleted != 0) {
                TUnit_AttackEnd(s, player, u);
            } else if (Rng_Next(s) < 200) {
                TUnit_OnAttackedBy(s, player, u, u.attack_ref.player, *tg, 1);
            }
        } else if (u.player_house_attack != 0xff) {
            const House* h = HouseTarget(s, u);
            if (s.players[u.player_house_attack].is_lost != 0 || h == nullptr || h->is_destroyed != 0) {
                TUnit_AttackEnd(s, player, u);
            }
        }
        const double ang = Heading(u);
        if (u.field_0ee != 0) --u.field_0ee;
        if (D(t.turn_rate) * D(0.5f) <= std::fabs(ang - u.angle)) {
            AirRotate(u, t, player);
        } else {
            u.x = F(D(u.vel_x) + u.x);
            u.y = F(D(u.vel_y) + u.y);
        }
        if (u.field_0ee == 0 && HasIntent(u)) {
            const double sp = TUnit_Speed(s, player, u, orig::kMoveBomberSpeed) / D(s.players[player].shell_spread);
            float R = F(sp * (u.type == 9 ? D(10.0f) : D(30.0f)));
            const Unit* tg0 = u.player_unit_attack != 0xff ? AttackTarget(s, u) : nullptr;
            if (tg0 != nullptr && Unit_Type(s, u.attack_ref.player, *tg0).is_ship != 0) R = F(D(R) * D(5.0f));
            const double reach = static_cast<double>(t.reload[player]) * t.speed[player];
            if (u.work == 8) {
                AimAhead(u);
                Dropped(s, player, u, t);
            } else if (u.player_unit_attack != 0xff) {
                Unit* tg = AttackTarget(s, u);
                if (tg == nullptr) {
                    TUnit_AttackEnd(s, player, u);  // the original logs first
                    return;                         // and skips the tail
                }
                float px = u.x, py = u.y;
                if (Unit_Type(s, u.attack_ref.player, *tg).is_ship != 0) {
                    px = F(static_cast<double>(100 - Rng_Next(s) % 200) + px);
                    py = F(static_cast<double>(100 - Rng_Next(s) % 200) + py);
                }
                if (tg->is_deleted != 0) {
                    TUnit_AttackEnd(s, player, u);
                } else {
                    const double d = DistPt(px, py, tg->x, tg->y);
                    if (d < D(R)) {
                        Dropped(s, player, u, t);
                    } else if (!(reach >= d - R) && Bomber_CanBomb(s, player, u)) {
                        AimAhead(u);
                        Dropped(s, player, u, t);
                    }
                }
            } else if (u.player_house_attack != 0xff) {
                const House* h = HouseTarget(s, u);
                if (h == nullptr) {
                    Unported(s, orig::kMoveBomber);  // a null house pointer (the original logs, then reads it)
                } else if (h->is_destroyed != 0) {
                    TUnit_AttackEnd(s, player, u);
                } else {
                    const double d = DistPt(u.x, u.y, h->x, h->y);
                    if (d < D(R)) {
                        Dropped(s, player, u, t);
                    } else if (!(reach >= d - R) && Bomber_CanBomb(s, player, u)) {
                        AimAhead(u);
                        Dropped(s, player, u, t);
                    }
                }
            } else {
                TUnit_AttackEnd(s, player, u);
            }
        }
    }
    FlightTail(s, player, u);
}

}  // namespace



void Air_CreateUnit(const UnitType& t, std::uint8_t player, Unit& u) {
    u.field_09f = 1;
    u.orient = 5;
    u.angle = 0.7953982f;
    u.dest_x = F(D(u.x) + D(100.0f));
    u.dest_y = F(D(u.y) + D(100.0f));
    SetRunStart(u);
    u.is_stopped = 0;
    u.work = 5;
    u.num_work = 0xe;
    u.num_sprite = 0.0f;
    AirVelocity(u, t.speed[player]);  // TUnit_Speed: a new unit has no group
}



void Air_TakeOff(GameState& s, std::uint8_t up, Unit& u, std::uint8_t hp, House& h) {
    const UnitType& t = Unit_Type(s, up, u);
    u.field_09f = 1;
    u.angle = kPiOver4;
    u.orient = 5;
    u.field_024 = 0.0f;
    u.dest_x = h.exit_x, u.dest_y = h.exit_y;
    u.is_stopped = 0;
    u.work = 5;
    u.num_work = 0xe;
    u.field_0f8 = 1;
    u.num_sprite = 0.0f;
    u.in_house = true, u.house_player = hp;
    u.house_slot = static_cast<std::uint16_t>(&h - s.players[hp].houses.data());
    SetRunStart(u);
    u.field_0ec = t.field_434;
    AirVelocity(u, TUnit_Speed(s, up, u, orig::kAirVelocity));
    TUnit_UpdateGroundHeight(s, up, u);
    if (h.field_0b4 != 0) u.field_07c = static_cast<std::uint16_t>(h.field_0b4);
    h.field_0b4 += 50;
}



void Air_ProduceUnit(GameState& s, std::uint8_t p, std::uint16_t slot, Unit& u) {
    PutInHouse(s, p, u, slot);
    House& h = s.players[p].houses[slot];
    u.in_house = true, u.house_player = p, u.house_slot = slot;
    House_ReleaseUnit(s, p, h, u);
    Air_TakeOff(s, p, u, p, h);
    // B276: the rally point for the end of the take-off. A full hangar sends the aircraft straight off from PutInHouse,
    // so THouse_ReleaseUnit (which needs Work 3) queues nothing.
    if (!s.original_rules) {
        u.waypoints[0] = Unit::Waypoint{h.rally_x, h.rally_y, 0xff, 0xffff};
        u.field_0bc = 1;
    }
}



bool Air_ResumeAfterFight(GameState& s, std::uint8_t player, Unit& u) {
    return OurFight(s, Unit_Type(s, player, u), u) && ResumeAfterFight(s, player, u);
}

// TBasicUnit MoveFunc for MoveClass 1, 11 and 12.
void Air_MoveFunc(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    if (t.move_class == 12) {
        MoveBomber(s, player, u, t);
    } else {
        MoveScoutFighter(s, player, u, t);
    }
}


void Air_OrderMove(GameState& s, std::uint8_t player, Unit& u, float x, float y) {
    (void)s, (void)player;
    const std::uint8_t w = u.work;
    if (w == 0xe || u.field_154 != 0 || w == 5 || w == 0xc || w == 0xd || w == 6 || w == 4) return;
    u.dest_x = x, u.dest_y = y, u.final_x = x, u.final_y = y;
    u.num_work = 0;
    u.is_stopped = 0;
    u.field_174 = 1;
    u.work = 0xff;
}


void Air_OrderEnd(GameState& s, std::uint8_t player, Unit& u) {
    u.return_target = UnitRef{};  // our rules (B277)
    u.dest_x = u.x, u.dest_y = u.y, u.final_x = u.x, u.final_y = u.y;
    EventUnitEndMove(s, player, u);
}


void Air_GoToWaypoint(Unit& u, float x, float y, std::uint8_t work, std::uint16_t arg) {
    u.dest_x = x, u.dest_y = y, u.final_x = x, u.final_y = y;
    u.work = work;
    u.field_0ca = arg;
    u.num_work = 0;
    u.is_stopped = 0;
    u.field_174 = 1;
}


void Air_OrderXy(Unit& u, float x, float y) {
    if (u.field_024 < 400.0f) return;
    u.num_work = 0;
    u.is_stopped = 0;
    u.dest_x = x, u.dest_y = y;
}


void Air_OnAttackedBy(GameState& s, std::uint8_t player, Unit& u, std::uint8_t tp, Unit& t) {
    TUnit_ClearTargets(u);
    u.return_target = UnitRef{};  // our rules (B277): a new target, nothing to go back to
    if (u.work == 5) return;
    if (u.work == 3) {
        House* h = InHouse(s, u);
        if (h == nullptr) {
            Unported(s, orig::kOnAttackedBy);  // Work 3 without a house
            return;
        }
        const std::uint8_t hp = u.house_player;
        House_ReleaseUnit(s, hp, *h, u);
        Air_TakeOff(s, player, u, hp, *h);
        if (!s.original_rules) u.field_0bc = 0;  // B276: a scramble stays over the hangar, not off to the rally point
        return;
    }
    u.attack_ref = Unit_Ref(tp, t), u.has_attack_unit = true;
    u.field_093 = 0, u.field_0ca = 0xffff;
    u.player_unit_attack = tp, u.player_house_attack = 0xff;
    u.attack_unit_id = t.id;
    u.dest_x = t.x, u.dest_y = t.y;
    u.is_stopped = 0;
    u.num_work = 0;
}



void Air_AttackHouse(GameState& s, std::uint8_t player, Unit& u, House& h, std::uint8_t flag) {
    const std::uint8_t w = u.work;
    if (w == 5 || w == 0xc || w == 0xd || w == 6) return;
    if (flag != 0) SaveOrder(u);
    const std::uint8_t mc = Unit_Type(s, player, u).move_class;
    if (mc == 1 || mc == 11) {
        u.dest_x = h.x, u.dest_y = h.y;
        u.is_stopped = 0;
        u.player_house_attack = 0xff, u.has_house_attack = false;
    } else if (mc == 12) {
        u.dest_x = h.x, u.dest_y = h.y;
        if (u.work != 3) u.work = 0xff;
    }
}





int Air_FindTarget(GameState& s, std::uint8_t player, Unit& u, bool air_only) {
    if (u.field_006 == 0 && !air_only) return 0;
    const UnitType& ut = Unit_Type(s, player, u);
    const std::int32_t range = ut.range[player];
    const std::int32_t r96 = range / 96, r48 = range / 48;
    const std::uint8_t w = u.work;
    if (w == 8 || w == 9 || w == 5 || w == 0xc || w == 0xd || w == 6 || w == 4) return 0;
    const std::int32_t mw = s.map.width;
    const std::int32_t cells = mw * static_cast<std::int32_t>(s.map.height);
    float lim = static_cast<float>(range);
    struct Pick {
        Unit* u = nullptr;
        std::uint8_t p = 0;
    };
    Pick list[5];
    std::uint8_t count = 0;
    bool found_air = false, at_once = false;
    for (int phase = 0; phase < 2 && !at_once; ++phase) {
        if (phase == 1 && (found_air || air_only)) break;
        const std::uint8_t maxc = phase == 0 ? std::uint8_t{3} : std::uint8_t{5};
        for (std::int32_t i = -r96; i <= r96 && !at_once; ++i) {
            for (std::int32_t j = -r48; j <= r48 && !at_once; ++j) {
                const std::int32_t idx = (static_cast<std::int32_t>(u.map_y) - j) * mw + u.map_x - i;
                if (idx < 0 || idx >= cells) break;  // the next column
                bool next_j = false;
                for (std::size_t k = 0; k < 40 && !next_j && !at_once; ++k) {
                    const std::vector<UnitRef>& cl = s.map.cell_units[static_cast<std::size_t>(idx)];
                    if (k >= cl.size()) break;
                    Unit* v = Unit_Find(s, cl[k]);
                    if (v == nullptr) {
                        Unported(s, orig::kFindTargetFighter);  // a stale cell entry
                        continue;
                    }
                    const std::uint8_t vp = cl[k].player;
                    if (v->is_deleted != 0) continue;
                    if (phase == 1 && v->work == 3) continue;
                    if (!IsEnemy(s, player, vp)) continue;
                    const double dd = Dist(*v, u);
                    const float d = F(dd);
                    if (!(dd < D(lim))) continue;
                    if (phase == 0) {
                        if (Unit_Type(s, vp, *v).is_air == 0) continue;
                        found_air = true;
                        if (v->type == 9 || v->type == 10) {
                            count = 1, list[0] = Pick{v, vp};
                            next_j = true;
                            continue;
                        }
                    } else if (v->has_attack_unit && v->attack_ref.player == player && v->attack_ref.id == u.id &&
                               v->attack_ref.serial == u.serial) {
                        count = 1, list[0] = Pick{v, vp};
                        at_once = true;
                        continue;
                    }
                    if (count < maxc) {
                        list[count++] = Pick{v, vp};
                        continue;
                    }
                    // Full: v goes in front of every entry farther than it (the loop goes on, so it can be put in
                    // more than once), the last entry's distance is the new limit.
                    for (int m = 0; m < maxc; ++m) {
                        if (!(Dist(*list[m].u, u) <= D(d))) {
                            for (int n = maxc - 1; n > m; --n) list[n] = list[n - 1];
                            list[m] = Pick{v, vp};
                        }
                    }
                    lim = F(Dist(*list[maxc - 1].u, u));
                }
            }
        }
    }
    if (count == 0) return 0;
    const Pick& pk = list[Rng_Next(s) % count];
    if (air_only) {
        Air_OnAttackedBy(s, player, u, pk.p, *pk.u);
    } else {
        TUnit_OnAttackedBy(s, player, u, pk.p, *pk.u, 1);
    }
    return 1;
}



void Air_OnKilled(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    u.return_target = UnitRef{};  // our rules (B277)
    u.work = 4;
    u.num_work = 0;
    if (u.field_024 > 1.0f) {
        double v = D(u.angle) + D(kPi);
        if (!(v < D(kTwoPi))) v -= D(kTwoPi);
        const auto o = static_cast<std::int8_t>(Ftol(D(kFourOverPi) * v));
        u.orient = static_cast<std::uint8_t>(o);
        const double q =
            (v - static_cast<double>(o) * D(kPiOver4)) / (D(kPiOver4) / static_cast<double>(t.anim_size[0]));
        u.num_sprite = static_cast<float>(static_cast<std::int16_t>(Ftol(q)));
        if (o >= 8) u.orient = 0;
        if (!(u.num_sprite < static_cast<float>(t.anim_size[0]))) u.num_sprite = 0.0f;
    } else {
        u.num_sprite = 0.0f;
        if (u.orient >= 8) u.orient = 0;
    }
}





bool Air_Falling(GameState& s, std::uint8_t player, std::uint32_t slot) {
    Unit& u = s.players[player].unit_slots[slot];
    const UnitType& t = Unit_Type(s, player, u);
    float top = static_cast<float>(Ftol(D(u.z) + u.field_024));
    const std::int32_t L = Ftol(D(top));  // the height on entry, for the effects
    u.num_work = 0;
    u.x = F(D(u.vel_x) + u.x);
    u.y = F(D(u.vel_y) + u.y);
    top = F(D(top) - D(t.speed[player]) * D(0.5f));
    const Cell c = WorldToCell(u.x, u.y);
    if (c.x != u.map_x || c.y != u.map_y) TUnit_SetCell(s, player, u, c);
    TUnit_UpdateGroundHeight(s, player, u);
    u.field_024 = F(D(top) - u.z);


    const std::int32_t px = Ftol(u.x), py = Ftol(D(u.y) - u.z - u.field_024);
    auto rng_at = [&s](int ahead) -> std::uint16_t {
        return s.rng_table.empty() ? 0 : s.rng_table[static_cast<std::uint16_t>(s.rng_index + ahead)];
    };
    const std::int32_t r10 = rng_at(0) % 10;
    Event_Effect(s, 3, false, px - r10 + 5, py - r10 + L + 5, L, 9);
    s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
    if (u.field_024 <= 0.0f) {
        const std::uint16_t r = rng_at(0);
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 1);  // the explosion kind (Rng & 3) + 10
        Effect_Damage(s, static_cast<float>(Ftol(u.x)), static_cast<float>(Ftol(u.y)), static_cast<float>(Ftol(u.z)),
                      300, 100);
        if (SimEvent::EffectArgs* e =
                Event_Effect(s, 0, false, Ftol(u.x), Ftol(u.y), Ftol(u.z), static_cast<std::uint8_t>((r & 3) + 10))) {
            e->delay = 10;
        }
        if (!Cell_Obstacle(s.map, u.map_x, u.map_y)) {
            u.num_work = 0xf, u.num_sprite = 0.0f, u.work = 0xff, u.field_0ca = 0xffff;
            return false;
        }
        u.field_0ca = 0xffff, u.work = 0xff;
        u.field_108 = 0;
        return false;
    }
    if (u.field_024 <= 15.0f) {  // the fire effect: one read for its picture and place, the index moves by 3
        const std::uint16_t r = rng_at(0);
        const std::int32_t d = r % 50;
        Event_Effect(s, 3, false, px - d + 25, py - d + L + 25, L, static_cast<std::uint8_t>((r & 3) + 10));
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 3);
    }
    const std::int32_t w = s.map.width, h = s.map.height;
    std::uint16_t obj = 0;
    if (u.map_x < w && u.map_y < h) {
        obj = s.map.objects[static_cast<std::size_t>(u.map_y) * w + u.map_x];
    } else {
        u.map_x = 0, u.map_y = 0;  // the original logs and clamps
    }
    const std::uint8_t hg = s.map.heights[static_cast<std::size_t>(u.map_y) * w + u.map_x];
    if (!(static_cast<float>(hg) < top) || (u.field_024 < 100.0f && obj >= 1000 && obj < 9000)) {
        const std::uint16_t r = rng_at(0);
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 1);  // the explosion kind
        Effect_Damage(s, static_cast<float>(Ftol(D(u.vel_x) * D(3.0f) + u.x)),
                      static_cast<float>(Ftol(D(u.vel_y) * D(3.0f) + u.y)), static_cast<float>(Ftol(D(top))), 800, 150);
        Event_Effect(s, 0, false, Ftol(D(u.vel_x) * D(3.0f) + u.x), Ftol(D(u.vel_y) * D(3.0f) + u.y), Ftol(D(top)),
                     static_cast<std::uint8_t>((r & 3) + 10));
        u.work = 0xff, u.field_024 = 0.0f, u.field_0ca = 0xffff, u.num_work = 0xf, u.num_sprite = 0.0f;
        u.field_108 = 0;
        return false;
    }
    return false;
}



void Projectiles_Update(GameState& s) {
    for (std::size_t k = 0; k < s.bullets.size();) {
        Bullet& b = s.bullets[k];
        if (b.delay != 0) {
            --b.delay;
            ++k;
            continue;
        }
        const Bullet v = b;
        Unit* o = v.owner.player != 0xff ? Unit_Find(s, v.owner) : nullptr;
        if (o == nullptr) {
            // no owner (a deleted aircraft): the original passes the stale pointer
            Unported(s, orig::kProjectilesUpdate);
        } else {
            BulletToPoint(s, v.x, v.y, v.owner.player, *o, true, 20.0f);
        }
        // The hit effect gunplane (0x27) when (x, y - z) is in the view (docs/render.md 14).
        if (SimEvent::EffectArgs* e = Event_Effect(s, 3, false, Ftol(v.x), Ftol(v.y), Ftol(v.z), 0x27)) {
            e->on_screen = true, e->view_x = v.x, e->view_y = v.y - v.z;
        }
        s.bullets.erase(s.bullets.begin() + static_cast<std::ptrdiff_t>(k));
    }
}

}  // namespace antanta::sim
