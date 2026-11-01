// Unit movement (docs/movement.md 2.4, 4.2, 5; arithmetic per docs/float-ops.md): orders, velocity,
// orientation and turning, the per-tick walk, arrival, and the per-tick MoveFuncs of infantry, workers and dead units.
// x87 values that stay on the stack are double here (the game runs at 53-bit precision); every float store is a
// float cast.
#include <cmath>
#include <cstdlib>

#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

namespace {

constexpr float kTwoPi = 6.2831855f;
constexpr float kFourOverPi = 1.2732395f;
constexpr float kPiOverFour = 0.7853982f;


bool CrawlSet(std::uint8_t nw) { return nw == 1 || nw == 2 || nw == 0x14 || nw == 8 || nw == 0xe || nw == 0x15; }

}  // namespace



double TUnit_Speed(GameState& s, std::uint8_t player, const Unit& u, std::uint32_t caller) {
    const double speed = Unit_Type(s, player, u).speed[player];
    if (!u.in_group) return speed;
    const Group* g = Unit_Group(s, player, u);
    if (g == nullptr) {
        Unported(s, caller);  // a group loaded from a save (chunk 9 is not loaded)
        return speed;
    }
    return u.field_106 != 0 ? speed * static_cast<double>(1.1f) : static_cast<double>(g->speed);
}


void TUnit_StartTurn(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    if (t.is_ship != 0 || u.target_orient == u.orient || u.num_work == 5) return;
    if (u.field_078 == 100) u.field_078 = 5;
    if (u.num_work == 6 && t.anim_size[0x13] != 0) {
        u.num_work = 0x13, u.num_sprite = 0;
        return;
    }
    if (u.num_work == 7 && t.anim_size[9] != 0) {
        u.num_work = 9, u.num_sprite = 0;
        return;
    }
    if (u.num_work != 9 && u.num_work != 0x13) {
        u.num_work = 5;
        if (u.field_078 == 5) u.field_078 = 100;
        u.num_sprite = 0;
    }
}

namespace {


bool NearWaypoint(const GameState& s, std::uint8_t player, const Unit& u) {
    const double dx = static_cast<double>(u.x) - u.dest_x, dy = static_cast<double>(u.y) - u.dest_y;
    return std::sqrt(dy * dy + dx * dx) < Unit_Type(s, player, u).waypoint_radius[player];
}


void WrapAngle(float& a) {
    if (a < 0.0f) {
        a = a + kTwoPi;
        return;
    }
    if (!(a < kTwoPi)) a = a - kTwoPi;
    if (!(a < 6.2821856f)) a = 0.0f;
}


void TUnit_MoveAsideRandom(GameState& s, std::uint8_t player, Unit& u) {
    if (u.work == 10 || u.work == 11) {  // a worker on the fields: a free field spot instead
        Worker_FindField(s, player, u);
        return;
    }
    const Cell c = RelocateTargetLarge(s, Cell{u.map_x, u.map_y}, player, u);
    ++u.field_145;
    const int r = s.rng_table[s.rng_index] % 20;
    s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
    const float tx = static_cast<float>(((c.x * 2 - (~c.y & 1)) * 48 + r) + 48.0 - 10.0);
    const float ty = static_cast<float>((c.y * 24 + r) - 10.0);
    TUnit_OrderMove(s, player, u, tx, ty, 1);
}


void Repath(GameState& s, std::uint8_t player, Unit& u) {
    const int r = TUnit_RequestPath(s, player, u, u.final_x, u.final_y, 1);
    if (r == 0) {
        EventUnitEndMove(s, player, u);
    } else if (r == 2) {
        TUnit_StopMoving(s, player, u);
        u.is_stopped = 1;
    }
}


double DistTo(const Unit& u, float x, float y) {
    const double dx = static_cast<double>(u.x) - x, dy = static_cast<double>(u.y) - y;
    return std::sqrt(dy * dy + dx * dx);
}


void FireFromHere(GameState& s, std::uint8_t player, Unit& u, float x, float y) {
    TUnit_ClearPath(u);
    StartFireAnim(s, player, u);
    FaceToPoint(s, player, u, x, y);
}



void OnFinalWaypoint(GameState& s, std::uint8_t player, Unit& u) {
    u.field_0f8 = 0;
    const UnitType& t = Unit_Type(s, player, u);
    if (u.player_unit_attack != 0xff) {
        Unit* tg = AttackTarget(s, u);
        if (tg == nullptr) {
            Unported(s, orig::kOnFinalWaypoint);  // a target player without a target: the original reads a null pointer
            return;
        }
        if (tg->is_deleted != 0) {
            TUnit_AttackEnd(s, player, u);
            return;
        }
        const double dx = static_cast<double>(u.x) - tg->x, dy = static_cast<double>(u.y) - tg->y;
        const auto d = static_cast<float>(std::sqrt(dy * dy + dx * dx));
        if (TUnit_TryAttack(s, player, u, u.attack_ref.player, *tg, d)) {
            TUnit_ClearPath(u);
            return;
        }
    } else if (u.player_house_attack != 0xff) {
        const House* h = HouseTarget(s, u);
        if (h == nullptr) {
            Unported(s, orig::kOnFinalWaypoint);
            return;
        }
        if (h->is_destroyed != 0) {
            TUnit_AttackEnd(s, player, u);
            return;
        }


        const auto id = static_cast<std::uint16_t>((u.player_house_attack + 1) * 1000 + h->num);
        const float hx = h->x, hy = h->y;
        if (Unit_Touches(s, u, id, 2)) {
            if (TUnit_CanGrenade(s, player, u)) {
                TUnit_ClearPath(u);
                u.field_0d4 = hx, u.field_0d8 = hy;
                TUnit_StartGrenade(s, player, u, hx, hy);
                u.field_078 = u.num_work;
                TUnit_FaceTowards(s, player, u, u.field_0d4, u.field_0d8);
                if (u.target_orient == u.orient) u.field_078 = 100;
                return;
            }
        } else if (t.move_class == 10) {
            const double dx = static_cast<double>(u.x) - hx, dy = static_cast<double>(u.y) - hy;
            if (static_cast<double>(static_cast<float>(t.range[player])) > std::sqrt(dx * dx + dy * dy) &&
                t.anim_size[6] != 0) {
                FireFromHere(s, player, u, hx, hy);
                return;
            }
            TUnit_AttackEnd(s, player, u);
            return;
        }
        if (t.move_class == 9) {
            const double dx = static_cast<double>(u.x) - hx, dy = static_cast<double>(u.y) - hy;
            if (std::sqrt(dy * dy + dx * dx) < static_cast<double>(500.0f)) {
                StartFireAnim(s, player, u);
                FaceToPoint(s, player, u, hx, hy);
                return;
            }
        }
        TUnit_AttackEnd(s, player, u);
        return;
    } else if (u.work == 8) {
        if (!(static_cast<double>(t.range[player]) < DistTo(u, u.field_0d4, u.field_0d8))) {
            FireFromHere(s, player, u, u.field_0d4, u.field_0d8);
            return;
        }
    }
    if (u.field_09f != 0) {
        u.field_09f = 0, u.is_stopped = 1;
        TUnit_StopMoving(s, player, u);
        u.field_07c = 150;
        return;
    }
    if (u.work == 2) {
        TUnit_EnterHouseStep(s, player, u);
        return;
    }
    u.is_stopped = 1;
    EventUnitEndMove(s, player, u);
}

// The common tail of MoveTo and RequestPath: walk straight to Dest.
void StraightTail(GameState& s, std::uint8_t player, Unit& u) {
    TUnit_SetMoveAnimation(s, player, u);
    TUnit_ComputeVelocity(s, player, u);
    u.field_078 = u.num_work;
    TUnit_SetOrientToDest(s, player, u);
    if (u.target_orient == u.orient || u.field_078 == u.num_work) u.field_078 = 100;
}



void NextPathStep(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    if (u.player_unit_attack != 0xff) {
        Unit* tg = AttackTarget(s, u);
        if (tg == nullptr) {
            Unported(s, orig::kNextPathStep);  // a target player without a target: the original reads a null pointer
            return;
        }
        if (tg->is_deleted != 0) {
            TUnit_AttackEnd(s, player, u);
            return;
        }
        if ((Rng_Next(s) & 1) != 0) {
            TUnit_OnAttackedBy(s, player, u, u.attack_ref.player, *tg, 1);
            return;
        }
    } else if (u.player_house_attack != 0xff) {
        const House* h = HouseTarget(s, u);
        if (h == nullptr) {
            Unported(s, orig::kNextPathStep);
            return;
        }
        if (h->is_destroyed != 0) {
            TUnit_AttackEnd(s, player, u);
            return;
        }
        if (t.move_class == 10 && static_cast<double>(t.range[player]) > DistTo(u, h->x, h->y) && t.anim_size[6] != 0) {
            FireFromHere(s, player, u, h->x, h->y);
            return;
        }
    } else if (u.work == 8) {
        if (!(static_cast<double>(t.range[player]) < DistTo(u, u.field_0d4, u.field_0d8))) {
            FireFromHere(s, player, u, u.field_0d4, u.field_0d8);
            return;
        }
    }
    const std::int32_t w = s.map.width, h = s.map.height;
    if (static_cast<int>(u.path_index) < static_cast<int>(u.path.size()) - 1) {
        const std::uint8_t dir = u.path[u.path_index];
        const Cell c = Cell_Step(Cell{u.map_x, u.map_y}, dir);
        if (c.x <= 3 || c.y <= 9 || c.x >= w - 3 || c.y >= h - 9 ||
            s.map.pass_land[static_cast<std::size_t>(c.y * w + c.x)] != 0) {
            Repath(s, player, u);
            return;
        }
        u.dest_x = static_cast<float>(kDirWorld[dir][0]) + u.dest_x;
        u.dest_y = static_cast<float>(kDirWorld[dir][1]) + u.dest_y;
        TUnit_ComputeVelocity(s, player, u);
        u.x = u.vel_x + u.x;
        u.y = u.vel_y + u.y;
        TUnit_SetMoveAnimation(s, player, u);
        u.field_078 = u.num_work;
        TUnit_SetOrientToDest(s, player, u);
        if (u.target_orient == u.orient) u.field_078 = 100;
        ++u.path_index;
        return;
    }
    const double dy = static_cast<double>(u.y) - u.final_y, dx = static_cast<double>(u.x) - u.final_x;
    if (std::sqrt(dy * dy + dx * dx) > 300.0) {
        EventUnitEndMove(s, player, u);
        return;
    }
    const Cell c = WorldToCell(u.final_x, u.final_y);

    if (c.x < 0 || c.y < 0 || c.x >= w || c.y >= h || !TUnit_CanEnterCell(s, player, u, c.y * w + c.x)) {
        EventUnitEndMove(s, player, u);
        return;
    }
    u.path.clear();
    u.dest_x = u.final_x, u.dest_y = u.final_y;
    if (NearWaypoint(s, player, u)) {
        OnFinalWaypoint(s, player, u);
        return;
    }
    TUnit_ComputeVelocity(s, player, u);
    u.field_078 = u.num_work;
    TUnit_SetOrientToDest(s, player, u);
    if (u.target_orient == u.orient) u.field_078 = 100;
}



void WorkerPathStep(GameState& s, std::uint8_t player, Unit& u) {
    if (u.player_unit_attack != 0xff) {
        Unit* target = AttackTarget(s, u);
        if (target == nullptr) {
            Unported(s, orig::kWorkerPathStep);  // a target player without a target: the original reads a null pointer
            return;
        }
        if (target->is_deleted != 0) {
            TUnit_AttackEnd(s, player, u);
            return;
        }
        if ((Rng_Next(s) & 1) != 0) {
            TUnit_OnAttackedBy(s, player, u, u.attack_ref.player, *target, 1);
            return;
        }
    }
    if (u.player_house_attack != 0xff) {
        Unported(s, orig::kWorkerPathStep);
        return;
    }
    const std::int32_t w = s.map.width, h = s.map.height;
    if (u.path_index < u.path.size()) {
        const std::uint8_t dir = u.path[u.path_index];
        const Cell c = Cell_Step(Cell{u.map_x, u.map_y}, dir);
        if (c.x <= 3 || c.y <= 9 || c.x >= w - 3 || c.y >= h - 9 ||
            s.map.pass_land[static_cast<std::size_t>(c.y * w + c.x)] != 0) {
            Repath(s, player, u);
            return;
        }
        u.dest_x = static_cast<float>(kDirWorld[dir][0]) + u.dest_x;
        u.dest_y = static_cast<float>(kDirWorld[dir][1]) + u.dest_y;
        TUnit_ComputeVelocity(s, player, u);
        u.x = u.vel_x + u.x;
        u.y = u.vel_y + u.y;
        TUnit_SetMoveAnimation(s, player, u);
        u.field_078 = u.num_work;
        TUnit_SetOrientToDest(s, player, u);
        if (u.target_orient == u.orient) u.field_078 = 100;
        ++u.path_index;
        return;
    }
    const double dy = static_cast<double>(u.y) - u.final_y, dx = static_cast<double>(u.x) - u.final_x;
    if (std::sqrt(dy * dy + dx * dx) > 300.0) {
        EventUnitEndMove(s, player, u);
        return;
    }
    const Cell c = WorldToCell(u.final_x, u.final_y);
    if (c.x < 0 || c.y < 0 || c.x >= w || c.y >= h || s.map.pass_land[static_cast<std::size_t>(c.y * w + c.x)] != 0) {
        EventUnitEndMove(s, player, u);
        return;
    }
    u.path.clear();
    u.dest_x = u.final_x, u.dest_y = u.final_y;
    if (NearWaypoint(s, player, u)) {
        Worker_Arrive(s, player, u);
        return;
    }
    TUnit_ComputeVelocity(s, player, u);
    u.field_078 = u.num_work;
    TUnit_SetOrientToDest(s, player, u);
    if (u.target_orient == u.orient) u.field_078 = 100;
}


void UnitActionInfantry(GameState& s, std::uint8_t player, Unit& u) {
    const float k = u.field_144 != 0 ? 0.5f : 1.0f;
    if (NearWaypoint(s, player, u)) {
        if (!u.path.empty()) {
            NextPathStep(s, player, u);
        } else {
            OnFinalWaypoint(s, player, u);
        }
        TUnit_UpdateCellIfMoved(s, player, u);
        return;
    }
    if (CrawlSet(u.num_work)) {
        u.x = static_cast<float>(static_cast<double>(k) * u.vel_x * static_cast<double>(0.33333334f) + u.x);
        u.y = static_cast<float>(static_cast<double>(k) * u.vel_y * static_cast<double>(0.33333334f) + u.y);
    } else {
        u.x = static_cast<float>(static_cast<double>(k) * u.vel_x + u.x);
        u.y = static_cast<float>(static_cast<double>(k) * u.vel_y + u.y);
    }
    TUnit_UpdateCellIfMoved(s, player, u);
}



void UnitActionWorker(GameState& s, std::uint8_t player, Unit& u) {
    const float k = u.field_144 != 0 ? 0.5f : 1.0f;
    if (NearWaypoint(s, player, u)) {
        if (!u.path.empty()) {
            WorkerPathStep(s, player, u);
        } else {
            Worker_Arrive(s, player, u);
        }
        TUnit_UpdateCellIfMoved(s, player, u);
        return;
    }
    u.x = static_cast<float>(static_cast<double>(k) * u.vel_x + u.x);
    u.y = static_cast<float>(static_cast<double>(k) * u.vel_y + u.y);
    TUnit_UpdateCellIfMoved(s, player, u);
}

}  // namespace


bool TUnit_ActionGate(GameState& s, std::uint8_t player, Unit& u) {
    if (u.is_stopped == 0) {
        if (u.num_work == 0 || u.num_work == 1) {
            const std::uint8_t mc = Unit_Type(s, player, u).move_class;
            if (mc == 6) {
                UnitActionWorker(s, player, u);
            } else if (mc == 3) {
                UnitActionVehicle(s, player, u);
            } else {
                UnitActionInfantry(s, player, u);
            }
        }
        return false;
    }
    if (u.num_work == 0) TUnit_StopMoving(s, player, u);
    if (u.work == 2 && u.field_107 == 0) u.work = 0xff;
    if (u.field_07c != 0) {
        --u.field_07c;
        if (u.field_07c == 0 && Unit_Type(s, player, u).move_class == 3) {  // the wait of a vehicle ends
            u.is_stopped = 0;
            TankRedirectAllArround(s, player, u);
            if (u.is_stopped == 0) {
                TUnit_SetMoveAnimation(s, player, u);
                UnitActionVehicle(s, player, u);
            }
        }
        return true;
    }
    return u.num_work == 1;
}

namespace {




bool InfantryAttackCheck(GameState& s, std::uint8_t player, Unit& u, const UnitType& t) {
    if (t.move_class == 10) {
        float tx = 0, ty = 0;
        if (u.player_unit_attack != 0xff) {
            const Unit* tg = AttackTarget(s, u);
            if (tg == nullptr) {
                // a target player without a target: the original reads a null pointer
                Unported(s, orig::kMoveInfantry);
                return true;
            }
            tx = tg->x, ty = tg->y;
            if (tg->field_024 > 1.0f) tx = 999999.0f;
        } else if (u.player_house_attack != 0xff) {
            const House* h = HouseTarget(s, u);
            if (h == nullptr) {
                Unported(s, orig::kMoveInfantry);
                return true;
            }
            tx = h->x, ty = h->y;
        } else if (u.work == 8) {
            tx = u.field_0d4, ty = u.field_0d8;
            TUnit_ClearTargets(u);
        } else {
            TUnit_AttackEnd(s, player, u);
            return true;
        }
        const float min_r = u.weapon_count != 0 && !t.weapons.empty()
                                ? t.weapons[0].min_range
                                : static_cast<float>(static_cast<double>(t.range[player]) * static_cast<double>(0.2f));
        const double dx = static_cast<double>(tx) - u.x, dy = static_cast<double>(ty) - u.y;
        const double d = std::sqrt(dy * dy + dx * dx);
        if (d < min_r || d > static_cast<double>(t.range[player])) {  // too close: a message for the local player
            TUnit_AttackEnd(s, player, u);
            return true;
        }
        if (u.field_0ee == 0) {
            const double ns = u.num_sprite, sp = t.anim_speed[u.num_work];
            const auto f = static_cast<double>(static_cast<int>(t.anim_size[u.num_work]) - 6);
            const bool cross = ns <= f && ns + sp > f;
            const bool c3 = ns <= 3.0 && ns + sp > 3.0;
            bool fire = false;
            if (u.type == 5) {
                fire = cross;
            } else if ((u.type == 0x16 || u.type == 0x17 || u.type == 0x15) && c3) {
                u.field_0ee = t.reload[player];
                fire = true;
            }
            if (fire) {
                if (u.work == 8) u.has_attack_unit = false;
                if (u.weapon_count != 0) Weapon_Fire(s, player, u);
                return false;
            }
        }
    }
    // L_c7e: a standing house target, a live unit target or Work 8 keeps the animation; else the target is gone.
    if (u.player_house_attack != 0xff && u.has_house_attack) {
        const House* h = HouseTarget(s, u);
        if (h != nullptr && h->is_destroyed == 0) return false;
    }
    if (u.player_unit_attack != 0xff && u.has_attack_unit) {
        const Unit* tg = AttackTarget(s, u);
        if (tg != nullptr && tg->is_deleted == 0) return false;
    }
    if (u.work == 8) return false;
    TUnit_AttackEnd(s, player, u);
    return false;
}


void MakeMoveInfantry(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    if (u.field_118 < static_cast<float>(t.field_594)) u.field_118 = t.field_598 + u.field_118;
    if (s.players[player].nation == 0 && u.type == 0xe && static_cast<float>(t.max_hp[player]) > u.hp) {
        u.hp = u.hp + 0.5f;  // the wild woman's regeneration
    }
    if (TUnit_ActionGate(s, player, u)) return;
    const float ns0 = u.num_sprite;  // the hit frame test of NumWork 7 uses the value from here
    if (u.num_work == 4) {
        if (s.short_anims == 0) {
            const float half = static_cast<float>(t.anim_size[4]) * 0.5f;
            if (u.num_sprite <= half && half < t.anim_speed[4] + u.num_sprite) {
                u.num_work = 3;
                u.num_sprite = 0.001f;
            }
        } else {
            u.num_work = 3;
        }
    }
    if (t.move_class == 4 && u.num_work == 0) Event_UnitDust(s, player, u, 2);  // cavalry dust (render, CRT rand)
    switch (u.num_work) {
        case 2:
        case 6:
        case 7:
            if (InfantryAttackCheck(s, player, u, t)) return;
            break;
        case 3:
            u.field_09f = 0;
            if (u.work == 2 && u.field_07c == 0 && u.field_107 == 0) u.work = 0xff;
            if (u.player_unit_attack != 0xff) {  // the attack resumes
                Unit* tg = AttackTarget(s, u);
                if (tg == nullptr || tg->is_deleted != 0) {
                    TUnit_AttackEnd(s, player, u);
                } else {
                    TUnit_OnAttackedBy(s, player, u, u.attack_ref.player, *tg, 1);
                }
                return;
            }
            if (u.player_house_attack != 0xff) {
                const House* h = HouseTarget(s, u);
                if (h == nullptr || h->is_destroyed != 0) {
                    TUnit_AttackEnd(s, player, u);
                } else {
                    TUnit_AttackHouse(s, player, u, u, u.house_attack_player, u.house_attack_slot, 0, 1);
                }
                return;
            }
            Transport_IdleCheck(s, u);
            break;
        case 0x16:  // the grenade leaves the hand at frame 11
            if (Ftol(u.num_sprite) != 11) break;
            u.field_118 = u.field_118 - 1.0f;
            if (u.player_unit_attack != 0xff) {  // aimed ahead of a moving target: 2 RNG reads
                const Unit* tg = AttackTarget(s, u);
                if (tg == nullptr) {
                    Unported(s, orig::kMoveInfantry);
                    break;
                }
                const int r0 = Rng_Next(s) % 20 + 5;
                u.field_0d4 = static_cast<float>(static_cast<double>(r0) * tg->vel_x + tg->x);
                const int r1 = Rng_Next(s) % 20 + 5;
                u.field_0d8 = static_cast<float>(static_cast<double>(r1) * tg->vel_y + tg->y);
            }
            Grenade_Fire(s, player, u);  // FireShell(4)
            break;
        default: break;
    }
    if (u.field_0ee != 0) {
        if (!Unit_ReloadHeld(s, player, t)) --u.field_0ee;  // Plus, E1: an unpaid unit reloads at half speed
        if ((u.num_work == 6 || u.num_work == 2) && u.num_sprite < 1.0f) return;
    }
    const double sum = static_cast<double>(t.anim_speed[u.num_work]) + u.num_sprite;
    u.num_sprite = static_cast<float>(sum);
    if (u.num_work == 7) {  // the hit frame: the middle of the animation is passed in this step
        const double half = static_cast<double>(t.anim_size[7]) * static_cast<double>(0.5f);
        if (ns0 <= half && !(sum < half)) {
            if (u.player_unit_attack != 0xff) {
                Unit* tg = AttackTarget(s, u);
                if (tg == nullptr) {
                    Unported(s, orig::kMoveInfantry);
                } else {
                    FaceToPoint(s, player, u, tg->x, tg->y);
                    MeleeHitUnit(s, player, u, u.attack_ref.player, *tg);
                }
            } else if (u.player_house_attack != 0xff) {
                if (s.plus_rules) {
                    MeleeHitHouse(s, player, u);
                } else {
                    Unported(s, orig::kMeleeHitHouse);  // MeleeHitHouse
                }
            } else {
                TUnit_AttackEnd(s, player, u);
            }
        }
    }
    if (static_cast<float>(t.anim_size[u.num_work]) <= u.num_sprite) {
        u.num_sprite = 0.01f;
        switch (u.num_work) {
            case 2:
            case 6:
                if (u.field_0ee != 0) break;
                if (t.move_class == 10) {
                    if (u.type == 0x17) break;
                    u.field_0ee = t.reload[player];
                    if (u.weapon_count != 0) break;
                    if (u.work == 8) u.has_attack_unit = false;
                    Unported(s, orig::kFireShell);  // FireShell(0): a shell of kind 0 (no weapon)
                    break;
                }
                if (u.player_unit_attack != 0xff && u.has_attack_unit) {
                    if (Unit* tg = AttackTarget(s, u)) {
                        RifleShot(s, player, u, u.attack_ref.player, *tg);
                    } else {
                        Unported(s, orig::kRifleShot);
                    }
                }
                {
                    const std::uint8_t size = t.anim_size[u.num_work];
                    const std::uint16_t reload = t.reload[player];
                    u.field_0ee = reload < size ? 0 : static_cast<std::uint16_t>(reload - size);
                }
                break;
            case 0x16:
                if (u.field_118 < 1.0f) {
                    TUnit_AttackEnd(s, player, u);
                    break;
                }
                if (u.player_unit_attack != 0xff) {
                    Unit* tg = AttackTarget(s, u);
                    if (tg == nullptr) {
                        Unported(s, orig::kMoveInfantry);
                        break;
                    }
                    const double dx = static_cast<double>(u.x) - tg->x, dy = static_cast<double>(u.y) - tg->y;
                    if (tg->is_deleted != 0 || std::sqrt(dy * dy + dx * dx) > 270.0) {
                        TUnit_AttackEnd(s, player, u);
                    } else {
                        FaceToPoint(s, player, u, tg->x, tg->y);
                    }
                    break;
                }
                if (u.player_house_attack != 0xff) {
                    const House* h = HouseTarget(s, u);
                    if (h != nullptr && h->is_destroyed == 0) {
                        FaceToPoint(s, player, u, h->x, h->y);
                        break;
                    }
                }
                TUnit_AttackEnd(s, player, u);
                break;
            case 3:
                if (s.short_anims != 0) {
                    u.num_work = 3;
                    break;
                }
                if (Rng_Next(s) > 99 || t.anim_size[4] == 0) break;
                u.num_work = 4;
                if (Rng_Next(s) < 0x4000) {
                    u.num_sprite = static_cast<float>(t.anim_size[4]) * 0.5f + 1.01f;
                } else {
                    u.num_sprite = 0;
                }
                break;
            case 4:
                if (s.short_anims == 0) {
                    TUnit_StopMoving(s, player, u);
                } else {
                    u.num_work = 3;
                }
                break;
            case 8:
                if (u.target_orient != u.orient) u.orient = u.target_orient;
                if (u.field_078 != 100) {
                    const std::uint8_t next = u.field_078;
                    u.field_078 = 100;
                    if (next == 0) {
                        u.num_work = 1;
                        TUnit_SetMoveAnimation(s, player, u);
                    } else {
                        u.num_work = next;
                        u.num_sprite = 0;
                    }
                } else {
                    u.num_work = 1;
                }
                break;
            case 9:
            case 0x13:
                if (u.target_orient != u.orient) {
                    const std::uint8_t next = u.field_078;
                    u.num_work = 5;
                    if (next == 6 && t.anim_size[0x12] != 0) {
                        u.field_078 = 0x12;
                    } else if (next == 7 && t.anim_size[0xd] != 0) {
                        u.field_078 = 0xd;
                    }
                    break;
                }
                if (u.field_078 == 100) {
                    u.num_work = u.is_stopped != 0 ? 3 : 0;
                    break;
                }
                u.num_work = u.field_078;
                u.field_078 = 100;
                u.num_sprite = 0;
                break;
            case 0xd: u.num_work = 7; break;
            case 0xe:
                if (u.target_orient == u.orient) {
                    if (u.field_078 == 100) {
                        u.num_work = u.is_stopped != 0 ? 3 : 0;
                    } else {
                        u.num_work = u.field_078;
                        u.field_078 = 100;
                        u.num_sprite = 0;
                    }
                } else {
                    u.num_work = 5;
                }
                break;
            case 0x12: u.num_work = 6; break;
            case 0x14:
                u.num_work = 2;
                if (u.field_078 == 0) {
                    u.num_work = 1;
                    u.field_078 = 100;
                    TUnit_SetMoveAnimation(s, player, u);
                }
                break;
            case 0x15: {
                if (u.target_orient != u.orient) {
                    u.orient = u.target_orient;
                    if (u.field_078 == 2) {
                        u.num_work = 0x14;
                        u.field_078 = 100;
                        break;
                    }
                }
                const std::uint8_t next = u.field_078;
                if (next == 100) {
                    u.num_work = 1;
                } else if (next == 0x12 || next == 6 || next == 7 || next == 0xd || next == 0) {
                    u.num_work = 0xe;
                } else {
                    u.num_work = next;
                    u.field_078 = 100;
                }
                break;
            }
            default: break;
        }
    }
    if (s.tick % 1000 == u.frame_slot) TUnit_ResyncCell(s, player, u);
}



void MakeMoveMashineGunner(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    if (TUnit_ActionGate(s, player, u)) return;
    if (u.num_work == 4) {
        if (s.short_anims == 0) {
            const float half = static_cast<float>(t.anim_size[4]) * 0.5f;
            if (u.num_sprite <= half && half < t.anim_speed[4] + u.num_sprite) {
                u.num_work = 3;
                u.num_sprite = 0.001f;
            }
        } else {
            u.num_work = 3;
        }
    }
    if (u.num_work == 2 || u.num_work == 6) {
        if (u.player_house_attack != 0xff && u.has_house_attack) {
            Unported(s, orig::kMoveMachineGunner);  // house target: an assertion, then AttackEnd
        } else {
            const Unit* target = u.player_unit_attack != 0xff ? AttackTarget(s, u) : nullptr;
            if (target == nullptr || target->is_deleted != 0) TUnit_AttackEnd(s, player, u);
        }
    } else if (u.num_work == 3) {
        u.field_09f = 0;
        if (u.player_unit_attack != 0xff || u.player_house_attack != 0xff) TUnit_AttackEnd(s, player, u);
        Transport_IdleCheck(s, u);
    }
    if (u.field_0ee != 0) {
        if (!Unit_ReloadHeld(s, player, t)) --u.field_0ee;  // Plus, E1: an unpaid unit reloads at half speed
        if (u.num_work == 6 || u.num_work == 2) return;
    }
    u.num_sprite = t.anim_speed[u.num_work] + u.num_sprite;
    if (static_cast<float>(t.anim_size[u.num_work]) <= u.num_sprite) {
        u.num_sprite = 0.01f;
        switch (u.num_work) {
            case 2:
            case 6: {
                if (u.field_0ee != 0) break;
                if (u.player_unit_attack != 0xff && u.has_attack_unit) {
                    if (Unit* tg = AttackTarget(s, u)) {
                        MgBurst(s, player, u, u.attack_ref.player, *tg);
                    } else {
                        Unported(s, orig::kMgBurst);
                    }
                }
                const std::uint8_t size = t.anim_size[u.num_work];
                const std::uint16_t reload = t.reload[player];
                u.field_0ee = reload < size ? 0 : static_cast<std::uint16_t>(reload - size);
                if (Rng_Next(s) % 20 == 1) u.field_0ee = 150;
                break;
            }
            case 3:
                if (s.short_anims != 0) {
                    u.num_work = 3;
                    break;
                }
                if (Rng_Next(s) > 99 || t.anim_size[4] == 0) break;
                u.num_work = 4;
                if (Rng_Next(s) < 0x4000) {
                    u.num_sprite = static_cast<float>(t.anim_size[4]) * 0.5f + 1.01f;
                } else {
                    u.num_sprite = 0;
                }
                break;
            case 4:
                if (s.short_anims == 0) {
                    TUnit_StopMoving(s, player, u);
                } else {
                    u.num_work = 3;
                }
                break;
            case 9:
            case 0x13:
                if (u.target_orient != u.orient) {
                    const std::uint8_t next = u.field_078;
                    u.num_work = 5;
                    if (next == 6 && t.anim_size[0x12] != 0) {
                        u.field_078 = 0x12;
                    } else if (next == 7 && t.anim_size[0xd] != 0) {
                        u.field_078 = 0xd;
                    }
                    break;
                }
                if (u.field_078 == 100) {
                    u.num_work = u.is_stopped != 0 ? 3 : 0;
                    break;
                }
                u.num_work = u.field_078;
                u.field_078 = 100;
                u.num_sprite = 0;
                break;
            case 0xd: u.num_work = 7; break;
            case 0x12: u.num_work = 6; break;
            default: break;
        }
    }
    if (s.tick % 1000 == u.frame_slot) TUnit_ResyncCell(s, player, u);
}





void MakeMoveFlameThrower(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    if (TUnit_ActionGate(s, player, u)) return;
    const float ns0 = u.num_sprite;
    if (u.num_work == 4) {
        if (s.short_anims == 0) {
            const double half = static_cast<double>(t.anim_size[4]) * static_cast<double>(0.5f);
            if (half >= u.num_sprite && static_cast<double>(t.anim_speed[4]) + u.num_sprite > half) {
                u.num_work = 3;
                u.num_sprite = 0.001f;
            }
        } else {
            u.num_work = 3;
        }
    }
    if (u.num_work == 3) {
        u.field_09f = 0;
        if (u.player_unit_attack != 0xff || u.player_house_attack != 0xff) TUnit_AttackEnd(s, player, u);
        Transport_IdleCheck(s, u);
    } else if (u.num_work == 6 && u.field_0ee == 0) {
        const double sp = t.anim_speed[6];
        if (ns0 <= 1.0f && static_cast<double>(ns0) + sp > 1.0) {
            if (u.weapon_count != 0) Weapon_Fire(s, player, u);
        }
        const double half = static_cast<double>(t.anim_size[6]) * static_cast<double>(0.5f);
        if (static_cast<double>(ns0) < half && static_cast<double>(ns0) + sp >= half) {
            CombatFireFlameThrower(s, player, u);
        }
    }
    if (u.field_0ee != 0) {
        if (!Unit_ReloadHeld(s, player, t)) --u.field_0ee;  // Plus, E1: an unpaid unit reloads at half speed
        if (u.num_work == 6 && u.num_sprite <= 1.0f) return;
    }
    const double sum = static_cast<double>(t.anim_speed[u.num_work]) + u.num_sprite;
    u.num_sprite = static_cast<float>(sum);
    if (sum >= static_cast<double>(t.anim_size[u.num_work])) {
        u.num_sprite = 0.01f;
        switch (u.num_work) {
            case 3:
                if (s.short_anims != 0) {
                    u.num_work = 3;
                    break;
                }
                if (Rng_Next(s) > 99 || t.anim_size[4] == 0) break;
                u.num_work = 4;
                if (Rng_Next(s) < 0x4000) {
                    u.num_sprite = static_cast<float>(t.anim_size[4]) * 0.5f + 1.01f;
                } else {
                    u.num_sprite = 0;
                }
                break;
            case 4:
                if (s.short_anims == 0) {
                    TUnit_StopMoving(s, player, u);
                } else {
                    u.num_work = 3;
                }
                break;
            case 6: {
                if (u.field_0ee != 0) break;
                const std::uint8_t size = t.anim_size[6];
                const std::uint16_t reload = t.reload[player];
                u.field_0ee = reload < size ? 0 : static_cast<std::uint16_t>(reload - size);
                break;
            }
            case 9:
            case 0x13:
                if (u.target_orient != u.orient) {
                    const std::uint8_t next = u.field_078;
                    u.num_work = 5;
                    if (next == 6 && t.anim_size[0x12] != 0) {
                        u.field_078 = 0x12;
                    } else if (next == 7 && t.anim_size[0xd] != 0) {
                        u.field_078 = 0xd;
                    }
                    break;
                }
                if (u.field_078 == 100) {
                    u.num_work = u.is_stopped != 0 ? 3 : 0;
                    break;
                }
                u.num_work = u.field_078;
                u.field_078 = 100;
                u.num_sprite = 0;
                break;
            case 0xd: u.num_work = 7; break;
            case 0x12: u.num_work = 6; break;
            default: break;
        }
    }
    if (s.tick % 1000 == u.frame_slot) TUnit_ResyncCell(s, player, u);
}



void MakeMoveWorker(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    if (TUnit_ActionGate(s, player, u)) return;
    if (u.work == 10 && u.num_work == 3 && u.is_stopped != 0 && u.field_107 == 0) {
        u.work = 0xff;
        Worker_FindField(s, player, u);
        return;
    }
    if (u.num_work == 3) {
        u.field_09f = 0;
        if (u.player_unit_attack != 0xff || u.player_house_attack != 0xff) TUnit_AttackEnd(s, player, u);
        if (u.work == 0xb && u.field_107 == 0) u.work = 0xff;
        Transport_IdleCheck(s, u);
    } else if (u.num_work == 7) {  // a standing house or a live unit keeps the attack, else it ends
        const House* h = u.player_house_attack != 0xff ? HouseTarget(s, u) : nullptr;
        const Unit* tg = u.player_unit_attack != 0xff ? AttackTarget(s, u) : nullptr;
        if (!(h != nullptr && h->is_destroyed == 0) && !(tg != nullptr && tg->is_deleted == 0)) {
            TUnit_AttackEnd(s, player, u);
        }
    }
    if (u.num_work == 4) {
        if (s.short_anims == 0) {
            const float half = static_cast<float>(t.anim_size[4]) * 0.5f;
            if (u.num_sprite <= half && half < t.anim_speed[4] + u.num_sprite) {
                u.num_work = 3;
                u.num_sprite = 0.001f;
            }
        } else {
            u.num_work = 3;
        }
    }
    const float ns0 = u.num_sprite;
    u.num_sprite = t.anim_speed[u.num_work] + u.num_sprite;
    if (u.num_work == 7) {  // the hit frame: the middle of the animation is crossed in this step (old < half)
        const float half = static_cast<float>(t.anim_size[7]) * 0.5f;
        if (ns0 < half && !(u.num_sprite < half)) {
            if (u.player_unit_attack != 0xff) {
                Unit* tg = AttackTarget(s, u);
                if (tg == nullptr) {
                    Unported(s, orig::kMoveWorker);
                } else {
                    FaceToPoint(s, player, u, tg->x, tg->y);
                    MeleeHitUnit(s, player, u, u.attack_ref.player, *tg);
                }
            } else if (u.player_house_attack != 0xff) {
                if (s.plus_rules) {
                    MeleeHitHouse(s, player, u);
                } else {
                    Unported(s, orig::kMeleeHitHouse);  // MeleeHitHouse
                }
            } else {
                TUnit_AttackEnd(s, player, u);
            }
        }
    }
    if (static_cast<float>(t.anim_size[u.num_work]) <= u.num_sprite) {
        u.num_sprite = 0;
        switch (u.num_work) {
            case 3: {
                const std::uint16_t first = s.rng_table[s.rng_index];
                if (first < 0x46) {
                    ++s.rng_index;
                    Worker_JobSearch(s, player, u);
                    break;
                }
                const std::uint16_t second = s.rng_table[static_cast<std::uint16_t>(s.rng_index + 1)];
                s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
                if (second < 0x3c && t.anim_size[4] != 0) {
                    u.num_work = 4;
                    if ((Rng_Next(s) & 1) != 0) u.num_sprite = static_cast<float>(t.anim_size[4]) * 0.5f + 1.01f;
                }
                break;
            }
            case 4: TUnit_StopMoving(s, player, u); break;
            case 9:
                if (u.target_orient == u.orient) {
                    if (u.field_078 == 100) {
                        u.num_work = u.is_stopped != 0 ? 3 : 0;
                    } else {
                        u.num_work = u.field_078;
                        u.field_078 = 100;
                        u.num_sprite = 0;
                    }
                } else {
                    u.num_work = 5;
                    if (u.field_078 == 7) u.field_078 = 0xd;
                }
                break;
            case 10: Worker_HarvestStep(s, player, u); break;
            case 0xb:
                if (Worker_WorkStep(s, player, u)) return;
                break;
            case 0xd: u.num_work = 7; break;
            default: break;
        }
    }
    if (s.tick % 1000 == u.frame_slot) TUnit_ResyncCell(s, player, u);
}

}  // namespace

void TUnit_Repath(GameState& s, std::uint8_t player, Unit& u) { Repath(s, player, u); }


void TUnit_ClearTargets(Unit& u) {
    u.has_attack_unit = false, u.player_unit_attack = 0xff;
    u.player_house_attack = 0xff, u.has_house_attack = false;
}


void TUnit_ClearPath(Unit& u) {
    u.path.clear();
    u.path_index = 0xffff;
    u.dest_x = u.x, u.dest_y = u.y;
    u.vel_x = 0, u.vel_y = 0;
    u.final_x = u.x, u.final_y = u.y;
}


void TUnit_StopMoving(GameState& s, std::uint8_t player, Unit& u) {
    u.num_sprite = 0;
    u.is_stopped = 1;
    u.field_09f = 0;
    u.num_work = u.field_174 != 0 ? 3 : 1;
    TUnit_ClearPath(u);
    if (u.field_178 != 0xff && static_cast<std::int8_t>(u.field_178) != static_cast<std::int8_t>(u.orient)) {
        u.target_orient = u.field_178;
        TUnit_StartTurn(s, player, u);
    }
}


void TUnit_ComputeVelocity(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    if (t.move_class == 3) {



        const Cell c = WorldToCell(u.dest_x, u.dest_y);
        if (IsLowBridgeCell(s, c)) {
            const std::uint16_t v = s.map.objects[static_cast<std::size_t>(c.y * s.map.width + c.x)];
            const std::int32_t ox = s.map.neutral_houses[v - 0x65u].type == 1 ? -10 : 10, oy = -10;
            const auto x = static_cast<float>((c.x * 2 - (~c.y & 1)) * 48 + 0x30 + ox);
            const auto y = static_cast<float>(c.y * 24 + oy);
            const double dx = static_cast<double>(u.dest_x) - u.final_x, dy = static_cast<double>(u.dest_y) - u.final_y;
            const bool near = std::sqrt(dx * dx + dy * dy) < 1.0;
            u.dest_x = x, u.dest_y = y;
            if (near) u.final_x = x, u.final_y = y;
        }
    }
    const double ady = std::abs(static_cast<double>(u.dest_y) - u.y);
    const double adx = std::abs(static_cast<double>(u.dest_x) - u.x);
    const float r = static_cast<float>(adx / ady);
    const double speed = TUnit_Speed(s, player, u, orig::kComputeVelocity);
    if (ady > static_cast<double>(0.1f)) {
        const double v = speed / std::sqrt(static_cast<double>(r) * r + 1.0);
        u.vel_y = static_cast<float>(v);
        u.vel_x = static_cast<float>(v * r);
    } else {
        u.vel_y = 0;
        u.vel_x = static_cast<float>(speed);
    }
    if (u.y > u.dest_y) u.vel_y = -u.vel_y;
    if (u.x > u.dest_x) u.vel_x = -u.vel_x;
    TUnit_SlopeSpeedScale(s, u);
}



void TUnit_SetOrientToDest(GameState& s, std::uint8_t player, Unit& u) {
    const float dyf = static_cast<float>(static_cast<double>(u.dest_y) - u.y);
    const double dx = static_cast<double>(u.dest_x) - u.x;
    float sl = 0;
    if (dx > 0.0) {
        sl = static_cast<float>(dyf / dx);
    } else if (dx < 0.0) {
        sl = static_cast<float>(-(dyf / dx));
    } else {
        sl = dyf * 10000.0f;
    }
    const bool pos = !(dx < 0.0);
    std::uint8_t t = 4;
    if (sl < -2.41) {
        t = 0;
    } else if (sl < -0.41) {
        t = pos ? 7 : 1;
    } else if (sl < 0.42) {
        t = pos ? 6 : 2;
    } else if (sl < 2.42) {
        t = pos ? 5 : 3;
    }
    u.target_orient = t;
    if (CrawlSet(u.num_work)) {
        u.orient = u.target_orient;
        return;
    }
    if (u.target_orient != u.orient) {
        if (u.num_work != 5) TUnit_StartTurn(s, player, u);
    } else if (u.type == 0xb || u.type == 0xc) {

        Event_UnitDust(s, player, u, 1);
    }
}



void TUnit_FaceTowards(GameState& s, std::uint8_t player, Unit& u, float px, float py) {
    const double dx = static_cast<double>(px) - u.x;
    const double dy = static_cast<double>(py) - u.y;
    float sl = 0;
    if (dx > 0.0) {
        sl = static_cast<float>(dy / dx);
    } else if (dx < 0.0) {
        sl = static_cast<float>(dy / (static_cast<double>(u.x) - px));
    } else {
        sl = static_cast<float>(dy * static_cast<double>(10000.0f));
    }
    const bool pos = !(dx < 0.0);
    std::uint8_t t = 4;
    if (sl < -2.41) {
        t = 0;
    } else if (sl < -0.41) {
        t = pos ? 7 : 1;
    } else if (sl < 0.42) {
        t = pos ? 6 : 2;
    } else if (sl < 2.42) {
        t = pos ? 5 : 3;
    }
    u.target_orient = t;
    if (CrawlSet(u.num_work)) {
        u.orient = u.target_orient;
    } else if (u.target_orient != u.orient && u.num_work != 5) {
        TUnit_StartTurn(s, player, u);
    }
}


void TUnit_SetMoveAnimation(GameState& s, std::uint8_t player, Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    if (t.speed[player] <= static_cast<float>(0.1)) return;
    if (u.field_174 == 0) {
        if (u.num_work != 1) Unported(s, orig::kSetMoveAnimationProne);  // prone
        return;
    }
    const std::uint8_t nw = u.num_work;
    if (nw == 0) return;
    if (nw == 0x14 || nw == 8 || nw == 0xe || nw == 0x15) {
        u.field_078 = 0;
        return;
    }
    if (nw == 2 && t.anim_size[0x15] != 0) {
        u.num_work = 0x15, u.num_sprite = 0, u.field_078 = 0;
        return;
    }
    if (nw == 1 || nw == 2) {
        u.num_sprite = 0;
        if (t.anim_size[0xe] == 0) {
            u.num_work = 0;
        } else {
            u.num_work = 0xe, u.field_078 = 0;
        }
        return;
    }
    if (nw == 6 && t.anim_size[0x13] != 0) {
        u.num_work = 0x13, u.num_sprite = 0, u.field_078 = 0;
        return;
    }
    if (nw == 7 && t.anim_size[9] != 0) {
        u.num_work = 9, u.num_sprite = 0, u.field_078 = 0;
        return;
    }
    if (nw != 5) u.num_work = 0, u.num_sprite = 0;
    u.field_078 = 0;
}


void TUnit_MoveTo(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t flag) {
    const UnitType& t = Unit_Type(s, player, u);
    u.field_107 = 0;
    if (t.is_ship != 0) {
        Ship_MoveTo(s, player, u, x, y);  // ships go straight
        return;
    }
    if (TUnit_Speed(s, player, u, orig::kMoveTo) < 0.1) {
        u.dest_x = x, u.dest_y = y;
        TUnit_SetOrientToDest(s, player, u);
        u.is_stopped = 1;
        return;
    }
    const Cell own{u.map_x, u.map_y};
    const Cell c = WorldToCell(x, y);
    const std::int32_t w = s.map.width, h = s.map.height;
    if (c.x < 4 || c.y < 10 || w - 3 <= c.x || h - 9 <= c.y) {
        const Cell r = RelocateTargetLand(s, c);
        TUnit_RequestPath(s, player, u, static_cast<float>((r.x * 2 - (~r.y & 1)) * 48) + 48.0f,
                          static_cast<float>(r.y * 24), 1);
        return;
    }
    if (t.field_468 != 0) {
        TUnit_MoveToLarge(s, player, u, c, x, y);
        return;
    }
    if (s.map.pass_land[static_cast<std::size_t>(c.y * w + c.x)] != 0) {
        if (flag == 0) {
            TUnit_StopMoving(s, player, u);
            TUnit_ClearTargets(u);
            return;
        }
        const Cell r = RelocateTargetLand(s, c);
        TUnit_RequestPath(s, player, u, static_cast<float>((r.x * 2 - (~r.y & 1)) * 48) + 48.0f,
                          static_cast<float>(r.y * 24), 1);
        return;
    }
    TUnit_ClearPath(u);
    u.final_x = x, u.final_y = y;
    u.field_09d = 0, u.is_stopped = 0;
    const double ddx = static_cast<double>(x) - u.x, ddy = static_cast<double>(y) - u.y;
    if (std::sqrt(ddy * ddy + ddx * ddx) < 120.0) {
        int i = 0;
        for (; i < 4; ++i) {
            const float px = static_cast<float>(ddx * i * 0.25 + u.x);
            const float py = static_cast<float>(ddy * i * 0.25 + u.y);
            const Cell p = WorldToCell(px, py);
            if (s.map.pass_land[static_cast<std::size_t>(p.y * w + p.x)] != 0) break;
        }
        if (i == 4) {
            u.dest_x = x, u.dest_y = y;
            StraightTail(s, player, u);
            return;
        }
    }
    if (c.x == own.x && c.y == own.y) {
        u.dest_x = x, u.dest_y = y;
        StraightTail(s, player, u);
        return;
    }
    std::vector<std::uint8_t> steps;
    Cell g = own;
    int checked = 0;
    for (std::uint8_t d = GreedyStep(g, c); d != 0; d = GreedyStep(g, c)) {
        if (checked < 10 && ++checked < 10 && s.map.pass_land[static_cast<std::size_t>(g.y * w + g.x)] != 0) {
            const int r = TUnit_RequestPath(s, player, u, x, y, 1);
            if (r == 1) return;
            if (r == -1) {
                u.dest_x = u.final_x, u.dest_y = u.final_y;
                StraightTail(s, player, u);
                return;
            }
            if (r == 2) {
                ++u.field_145;
                TUnit_StopMoving(s, player, u);
                return;
            }
            TUnit_StopMoving(s, player, u);
            EventUnitEndMove(s, player, u);
            return;
        }
        steps.push_back(d);
        if (steps.size() > 4999) {
            Unported(s, orig::kStopAndHold);  // error path
            return;
        }
    }
    u.path = steps;
    u.path_index = 0;
    if (TUnit_CanStepDir(s, player, u, steps[0])) {
        u.dest_x = static_cast<float>(kDirWorld[steps[0]][0]) + u.x;
        u.dest_y = static_cast<float>(kDirWorld[steps[0]][1]) + u.y;
        TUnit_SetMoveAnimation(s, player, u);
        TUnit_ComputeVelocity(s, player, u);
        u.path_index = 1;
        u.field_078 = u.num_work;
        TUnit_SetOrientToDest(s, player, u);
        if (u.target_orient == u.orient || u.field_078 == u.num_work) u.field_078 = 100;
        return;
    }
    u.path_index = 1;
    const int r = TUnit_RequestPath(s, player, u, x, y, 1);
    if (r == 1) return;
    if (r == -1) {
        u.dest_x = u.final_x, u.dest_y = u.final_y;
        StraightTail(s, player, u);
        return;
    }
    if (r != 2) {
        ++u.field_145;
        TUnit_StopMoving(s, player, u);
        EventUnitEndMove(s, player, u);
    }
    TUnit_StopMoving(s, player, u);
}



void TUnit_OrderMove(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t flag) {
    if (s.log_moves) Order_LogMove(s, s.log_parent, player, u, x, y, flag);
    const UnitType& t = Unit_Type(s, player, u);
    if (t.is_ship != 0) {
        Ship_OrderMove(s, player, u, x, y);
        return;
    }
    if (t.is_air != 0) {
        Air_OrderMove(s, player, u, x, y);
        return;
    }
    if (u.work == 0x14 || u.work == 9 || u.work == 3) return;
    u.work = 0xff;
    TUnit_MoveTo(s, player, u, x, y, flag);
    u.field_086 = 0;
    u.field_093 = 0;
}


void TUnit_OrderEnd(GameState& s, std::uint8_t player, Unit& u, std::uint8_t flag) {
    const UnitType& t = Unit_Type(s, player, u);
    if (u.work == 0x14) return;
    if (u.work == 9) u.work = 0xff;
    Unit_ClearFutureTransporter(u);
    u.has_attack_unit = false, u.player_unit_attack = 0xff;
    u.player_house_attack = 0xff, u.has_house_attack = false;
    u.field_09f = 0;
    if (u.work == 3) return;
    if (t.is_ship != 0) {
        Ship_OrderEnd(s, player, u);
        return;
    }
    if (t.is_air != 0) {
        Air_OrderEnd(s, player, u);
        return;
    }
    if (flag != 0 && !TUnit_IsSpotFree(s, player, u) && u.field_145 < 3) {
        TUnit_MoveAsideRandom(s, player, u);
        return;
    }
    u.field_145 = 0;
    u.field_0bc = 0;
    TUnit_ClearPath(u);
    if (!CrawlSet(u.num_work)) {
        TUnit_SetMoveAnimation(s, player, u);
        if (u.num_work == 9 || u.num_work == 0x13) {
            if (u.field_078 != 0) u.field_078 = 3;
        } else {
            if (u.work == 3) return;
            TUnit_StopMoving(s, player, u);
            u.work = 0xff;
        }
    } else if (u.num_work == 6 && t.anim_size[0x15] != 0) {
        u.num_work = 0x15;
    } else {
        u.num_work = 1;
    }
    u.is_stopped = 1;
    if (u.work != 3) u.work = 0xff;
}




void TUnit_GoToWaypoint(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t work,
                        std::uint16_t arg) {
    u.work = 0xff;
    if (u.field_154 != 0) return;
    u.field_09f = 0;
    const UnitType& t = Unit_Type(s, player, u);
    if (t.is_ship != 0) {
        Ship_OrderQueued(s, player, u, x, y, work, arg);
        return;
    }
    if (t.is_air != 0) {
        Air_GoToWaypoint(u, x, y, work, arg);
        return;
    }
    const Cell c = WorldToCell(x, y);
    if (CellInside(s, c)) {
        const std::uint16_t obj = s.map.objects[static_cast<std::size_t>(c.y * s.map.width + c.x)];
        if (obj / 1000 - 1 == player) {
            Worker_SendToHouse(s, player, u, static_cast<std::uint16_t>(obj % 1000), 0);
            return;
        }
        if (obj >= 1000 && obj < 9000) {
            const std::size_t hp = obj / 1000 - 1, slot = obj % 1000;
            if (hp >= s.players.size() || slot >= s.players[hp].houses.size()) {
                Unported(s, orig::kAttackHouse);  // a stale cell object: the original reads past its house table
                return;
            }
            TUnit_AttackHouse(s, player, u, u, static_cast<std::uint8_t>(hp), static_cast<std::uint16_t>(slot), 0, 1);
            return;
        }
        if (obj >= 0x25de && obj <= 0x25e3) {
            TUnit_OrderXy(s, player, u, x, y, obj);
            return;
        }
    }
    u.work = work;
    u.field_0ca = arg;
    TUnit_MoveTo(s, player, u, x, y, 1);
    u.field_086 = 0;
    u.field_093 = 0;
}



void TUnit_OrderXy(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint16_t object) {
    if (u.work == 0x14) return;
    TUnit_ClearTargets(u);
    const UnitType& t = Unit_Type(s, player, u);
    if (t.move_class == 6 && object >= 0x25de && object <= 0x25e3) {
        Worker_GoToField(s, player, u, x, y);
        return;
    }
    if (t.is_air != 0) {
        Air_OrderXy(u, x, y);
        return;
    }
    if (u.work == 3) return;  // an assert in the original
    u.work = 0xff;
    u.field_0ca = 0xffff;
    u.field_0bc = 0;
    TUnit_MoveTo(s, player, u, x, y, 1);
    u.field_086 = 0;
    u.field_093 = 0;
}


void EventUnitEndMove(GameState& s, std::uint8_t player, Unit& u) {
    if (u.work == 3 || u.work == 0x14) return;
    if (u.work == 9) u.work = 0xff;
    u.field_09f = 0;
    u.field_158 = 0;
    const UnitType& t = Unit_Type(s, player, u);
    if (Transport_EndMove(s, player, u)) return;
    if (t.is_air != 0) {
        TUnit_OrderMove(s, player, u, u.dest_x, u.dest_y, 1);
        return;
    }
    if (t.is_ship != 0) {
        Ship_EndMove(s, player, u);
        return;
    }
    if (u.player_unit_attack != 0xff) {  // the end of a walk toward a unit target
        Unit* tg = AttackTarget(s, u);
        if (tg == nullptr) {
            Unported(s, orig::kUnitEndMove);  // a target player without a target: the original reads a null pointer
            return;
        }
        const bool melee = t.anim_size[7] != 0 && u.work != 3 && u.work != 0x14;
        if (tg->is_deleted == 0 && (t.anim_size[6] != 0 || melee)) {
            if (u.field_145 < 4) {
                TUnit_OnAttackedBy(s, player, u, u.attack_ref.player, *tg, 1);
                return;
            }
            TUnit_ClearTargets(u);
            u.field_145 = 0;
            TUnit_StopMoving(s, player, u);
            u.is_stopped = 1;
            u.work = 0xff;  // Work 3 also logs a message
            return;
        }
        if (tg->is_deleted != 0) TUnit_ClearTargets(u);
        if (TUnit_IsSpotFree(s, player, u) || u.field_145 > 4) {
            u.field_145 = 0;
            TUnit_StopMoving(s, player, u);
            u.is_stopped = 1;
            return;
        }
        TUnit_MoveAsideRandom(s, player, u);
        return;
    }
    if (u.field_0bc != 0 && !u.in_group) {
        if (u.work == 3) return;
        const Unit::Waypoint w = u.waypoints[0];
        u.work = 0xff;
        for (std::size_t i = 1; i < u.field_0bc && i < u.waypoints.size(); ++i) u.waypoints[i - 1] = u.waypoints[i];
        --u.field_0bc;
        TUnit_GoToWaypoint(s, player, u, w.x, w.y, w.work, w.field_0ca);
        return;
    }
    u.field_0bc = 0;
    if (u.field_093 != 0) {
        Unported(s, orig::kUnitEndMove);
        return;
    }
    if (TUnit_IsSpotFree(s, player, u) || u.field_145 > 4) {
        u.field_145 = 0;
        TUnit_StopMoving(s, player, u);
        u.is_stopped = 1;
        if (u.work != 3) u.work = 0xff;
        return;
    }
    TUnit_MoveAsideRandom(s, player, u);
}



namespace {

void TurnDone(GameState& s, std::uint8_t player, Unit& u) {
    if (u.orient != u.target_orient || static_cast<std::uint8_t>(Ftol(u.num_sprite)) != 0) return;
    if (u.field_078 != 100) {
        u.num_work = u.field_078;
        u.field_078 = 100;
    } else {
        u.num_work = u.path.empty() ? 3 : 0;
    }
    if (u.num_work == 0 && (u.type == 0xb || u.type == 0xc)) Event_UnitDust(s, player, u, 1);
    u.num_sprite = 0;
}

}  // namespace


void TUnit_TurnStep(GameState& s, std::uint8_t player, Unit& u) {
    if (u.num_work != 5) return;
    const UnitType& t = Unit_Type(s, player, u);
    if (t.move_class == 4) {
        if (u.target_orient < 8) {
            u.orient = u.target_orient;
            u.angle = static_cast<float>(static_cast<double>(static_cast<std::int8_t>(u.target_orient)) *
                                             static_cast<double>(0.7853982f) +
                                         static_cast<double>(0.0001f));
        }
        u.num_sprite = 0;
        TurnDone(s, player, u);
        return;
    }
    if (t.is_ship != 0) {
        u.num_work = 0;
        return;
    }
    const std::int8_t to = static_cast<std::int8_t>(u.target_orient), o = static_cast<std::int8_t>(u.orient);
    const bool add = to > o ? to - o <= 4 : o - to > 4;
    if (add) {
        u.angle = static_cast<float>((static_cast<double>(t.turn_rate) + u.angle) + static_cast<double>(0.01f));
    } else {
        u.angle = u.angle - t.turn_rate;
    }
    WrapAngle(u.angle);
    if (u.angle >= kTwoPi) WrapAngle(u.angle);
    u.orient = static_cast<std::uint8_t>(Ftol(static_cast<double>(u.angle) * kFourOverPi));

    const int n = t.anim_size[5];
    const double a =
        static_cast<double>(u.angle) - static_cast<std::int8_t>(u.orient) * static_cast<double>(kPiOverFour);
    u.num_sprite = static_cast<float>(static_cast<std::uint8_t>(Ftol((n * a) * kFourOverPi)));
    TurnDone(s, player, u);
}


void TUnit_MoveFunc(GameState& s, std::uint8_t player, Unit& u) {
    const std::uint8_t mc = Unit_Type(s, player, u).move_class;
    if (mc == 1 || mc == 11 || mc == 12) {
        Air_MoveFunc(s, player, u);
        return;
    }
    if (mc == 6) {
        MakeMoveWorker(s, player, u);
    } else if (mc == 16) {
        MakeMoveMashineGunner(s, player, u);
    } else if (mc == 3) {
        MakeMoveVehicle(s, player, u);
    } else if (mc >= 13 && mc <= 15) {
        MakeMoveShip(s, player, u);
    } else if (mc == 9) {
        MakeMoveFlameThrower(s, player, u);
    } else {
        MakeMoveInfantry(s, player, u);
    }
}



bool MakeMoveForDead(GameState& s, std::uint8_t player, std::uint32_t slot) {
    Unit& u = s.players[player].unit_slots[slot];
    const UnitType& t = Unit_Type(s, player, u);



    if (t.move_class == 3 && u.field_0ca > 59000) {
        const double x = static_cast<float>(static_cast<double>(u.x) + 5.0);
        if (SimEvent::EffectArgs* e = Event_Effect(s, 3, false, Ftol(x), Ftol(static_cast<double>(u.y) + 100.0),
                                                   Ftol(static_cast<double>(u.z) + 100.0), 9)) {
            e->rise_step = 3, e->view_rand = 1;
        }
    }
    if (t.is_air != 0 && u.work == 4) return Air_Falling(s, player, slot);
    if (t.is_ship != 0) return Ship_MoveForDead(s, player, slot);
    if (u.work == 4) {
        Unported(s, orig::kMoveFalling);  // falling unit
        return false;
    }
    if (u.num_work != 0xf && u.num_work != 0x10 && u.num_work != 0x11) {
        u.num_work = 0xf;
        u.num_sprite = 0;
    }
    u.num_sprite = t.anim_speed[u.num_work] + u.num_sprite;
    if (!(static_cast<float>(t.anim_size[u.num_work]) <= u.num_sprite)) return false;
    u.field_0ca = static_cast<std::uint16_t>(u.field_0ca - 30);
    if (u.field_0ca > 62000 && u.field_0ca < 63000) {
        Cell_RemoveUnit(s, player, u);
        u.field_0ca = 61999;
    }
    if (u.field_0ca < 20001) {
        u.field_0ca = 0xb;
        // DeleteUnit: units aiming at it drop it and look for another target (transporter links are not ported).
        // The unit leaves its cell list and its player's list; a player without units is lost.
        Unit_ClearReferences(s, Unit_Ref(player, u));
        if (u.in_cell_list) Cell_RemoveUnit(s, player, u);
        if (u.work == 3) Unported(s, orig::kDeleteUnit);     // leaving the house
        if (u.work == 0x14) Transport_GetOut(s, player, u);
        Unit_Delete(s, player, slot);
        return true;
    }
    u.num_sprite = u.num_sprite - t.anim_speed[u.num_work];
    return false;
}

}  // namespace antanta::sim
