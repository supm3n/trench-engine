

#include <array>

#include "endgame.hpp"
#include "market.hpp"
#include "mission_events.hpp"
#include "movement.hpp"
#include "original_ids.hpp"
#include "plus_brain.hpp"
#include "prefetch.hpp"
#include "strike.hpp"

namespace antanta::sim {

bool Unit_ReloadHeld(const GameState& s, std::uint8_t p, const UnitType& t) {
    if (!s.plus_rules || s.tick % 2 == 0) return false;
    const Player& pl = s.players[p];
    // upkeep: Food, Wood, Iron, Gold, Oil; food (and wood) never count
    return (t.upkeep[2] > 0.0f && pl.iron < 1.0) || (t.upkeep[3] > 0.0f && pl.gold < 1.0) ||
           (t.upkeep[4] > 0.0f && pl.oil < 1.0);
}

void Unported(GameState& s, std::uint32_t address) {
    UnportedHit& h = s.unported[address];
    if (h.count++ == 0) h.first_tick = s.tick;
}

namespace {



void Tick_ResetFrameState(GameState& s) {
    if (s.deferred_path_count < 0x65) {
        s.path_budget_max = 0x14;
    } else {
        s.path_budget_max = s.deferred_path_count > 300 ? 100 : 0x28;
    }
    s.deferred_path_count = 0;
    if (s.tick % 3 == 0) s.formation_budget = 0;
    s.path_budget_used = 0;
    s.cmd_counter = 0;
    Grid_TickReset(s);
}



bool ExecuteCommand(GameState& s, CommandNode& n) {
    if (n.cls == 1 && n.cmd == 0x2d) {
        Group_DeferredCreate(s, s.cur_player, n);
        return false;
    }
    if (n.cls == 1 && n.cmd == 0x2f) {
        Group_DeferredOrder(s, s.cur_player, n);
        return false;
    }
    if (n.cls == 2 && n.cmd == 7) {


        const auto type = static_cast<std::uint16_t>(n.a2c & 0xffff);
        const std::uint16_t k = AddHouseToPointFull(s, s.cur_player, n.x, n.y, type, n.list);
        if (k != 0xffff) House_SetGraphic(s.players[s.cur_player].houses[k], 1, 0);
        if (k == 0xffff && s.cur_player == s.my_player) Event_Sound(s, 1, 0, 0);
        return false;
    }
    if (n.cls != 1 || n.cmd != 0x0d) {
        Unported(s, orig::kExecuteCommand);  // other command classes
        return false;
    }
    Unit* u = Unit_Find(s, n.unit);
    if (u == nullptr || u->is_deleted != 0 || u->field_107 == 0 || u->field_160 != n.seq) return false;
    if (Ftol(n.x) != u->field_060 || Ftol(n.y) != u->field_064) return false;
    const Player& cur = s.players[s.cur_player];
    if (s.deferred_path_count == 0) s.deferred_human_count = 0;
    ++s.deferred_path_count;
    if (cur.control_type == 1) ++s.deferred_human_count;
    // Our rules (docs/bugs.md B88, B89): a small unit's request takes the shared route field without the budget, and
    // a computer's unit never drops its order when more than 100 wait; it waits for the next tick instead.
    if (s.path_budget_used >= s.path_budget_max && !Flow_Usable(s, n.unit.player, *u)) {
        if (s.original_rules && cur.control_type == 1 && s.deferred_human_count > 100) {
            u->field_107 = 0;
            u->field_160 = 0;
            TUnit_OrderEnd(s, n.unit.player, *u, 1);
            u->work = 0xff;
            TUnit_ClearTargets(*u);
            u->field_07c = 5;
            return false;
        }
        s.players[s.cur_player].commands.push_back(n);
        return true;
    }
    u->field_160 = 0;
    if (TUnit_RequestPath(s, n.unit.player, *u, n.x, n.y, 1) == 1) TUnit_SetMoveAnimation(s, n.unit.player, *u);
    return false;
}


void TStrateg_ExecuteCommands(GameState& s, std::uint8_t player) {
    std::vector<CommandNode> queue;
    queue.swap(s.players[player].commands);
    // Entente Engine Plus (docs/ai.md 11): one long route search of this player's waiting requests even when the
    // players before it spent the tick's budget; the original's order (humans, then the computers by slot) otherwise
    // leaves a later computer's vehicles and blocked requests waiting for good in a big game, and the groups they
    // belong to standing with them. The spent budget is restored for the rest of the tick.
    const std::int32_t spent = s.path_budget_used;
    if (s.plus_rules && spent >= s.path_budget_max) s.path_budget_used = s.path_budget_max - 10;
    for (CommandNode& n : queue) {
        ++s.cmd_counter;
        s.executed.push_back(CommandExec{s.cmd_counter, player, s.rng_index, n});
        ExecuteCommand(s, n);
        if (s.players[player].is_lost != 0) return;
    }
    if (s.plus_rules && s.path_budget_used < spent) s.path_budget_used = spent;
}



bool FarmNear(const GameState& s, std::uint32_t entry) {
    Cell row{static_cast<std::int32_t>(entry & 0xffff), static_cast<std::int32_t>(entry >> 16) + 6};
    const std::int32_t w = s.map.width, h = s.map.height;
    for (int i = 0; i < 7; ++i) {
        Cell c = row;
        for (int j = 0; j < 7; ++j) {
            if (c.x > 3 && c.y > 9 && c.x < w - 3 && c.y < h - 9) {
                const std::uint16_t o = s.map.objects[static_cast<std::size_t>(c.y * w + c.x)];
                if (o >= 1000 && o < 9000 && o / 1000 - 1u < s.players.size()) {
                    const std::vector<House>& hs = s.players[o / 1000 - 1].houses;
                    const std::size_t k = o % 1000;
                    if (k < hs.size() && hs[k].is_destroyed == 0 && hs[k].field_020 == 0 && hs[k].type == 0xf) {
                        return true;
                    }
                }
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    return false;
}



void Fields_Decay(GameState& s) {
    std::vector<std::uint32_t>& list = s.decaying_fields;
    const std::size_t w = s.map.width;
    for (std::size_t i = 0; i < list.size();) {
        const std::uint32_t e = list[i];
        const std::size_t cell = static_cast<std::size_t>(e >> 16) * w + (e & 0xffff);
        const std::uint16_t o = cell < s.map.objects.size() ? s.map.objects[cell] : 0;
        if (!FarmNear(s, e) && o >= 0x25de && o <= 0x25e3) {
            if (Rng_Next(s) % 300 != 1) {
                ++i;
                continue;
            }
            if ((e & 0xffff) < w && (e >> 16) < s.map.height) s.map.objects[cell] = 0;
        }
        list.erase(list.begin() + static_cast<std::ptrdiff_t>(i));
    }
}


// u+6 = 0) does not run out while your land unit or ship is still on its way: each tick it enters a new map cell (a
// ship also when it turns) more than 100 px from its Final, the delay is set back to 100 if it is lower. So it neither
// looks for targets nor answers attacks on the way; once it gets there, or is stuck (a blocked unit can step back and
// forth on the spot), the delay runs out within 100 ticks and it fights as before. Aircraft keep their own rule
// (65000 ticks, FlightTail); the computer's units never get the delay. The original lets the 300 ticks run out on the
// way, and the unit turns on the first enemy it meets.
void MoveHold_Refresh(GameState& s, std::uint8_t p, Unit& u, const UnitType& t, Cell c0, float a0) {
    if (s.original_rules || u.field_006 != 0 || u.field_142 == 0 || t.is_air != 0 || u.is_deleted != 0) return;
    if (!Player_IsYou(s, p)) return;
    if (u.map_x == c0.x && u.map_y == c0.y && (t.is_ship == 0 || u.angle == a0)) return;  // no headway this tick
    const double dx = static_cast<double>(u.x) - u.final_x, dy = static_cast<double>(u.y) - u.final_y;
    if (dx * dx + dy * dy <= 100.0 * 100.0) return;
    if (u.field_142 < 100) u.field_142 = 100;
}




void TStrateg_UpdateUnits(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    const std::uint32_t mod50 = s.tick % 50;
    // Cache hints only (docs/bugs.md B59): the list order of the player's last walk is kept, and the unit kAhead
    // places on in it is loaded while this one updates; the list changes little from tick to tick.
    constexpr std::size_t kAhead = 6;
    static thread_local std::array<std::vector<std::uint32_t>, 8> last_order;
    std::vector<std::uint32_t>& order = last_order[p % 8];
    std::size_t at = 0;
    for (std::uint32_t slot = pl.first_unit; slot != kNoSlot; ++at) {
        Unit& u = pl.unit_slots[slot];
        if (u.next != kNoSlot) Prefetch_Lines(&pl.unit_slots[u.next], 0, sizeof(Unit));  // loaded while u updates
        if (at + kAhead < order.size() && order[at + kAhead] < pl.unit_slots.size()) {
            Prefetch_Lines(&pl.unit_slots[order[at + kAhead]], 0, sizeof(Unit));
        }
        if (at < order.size()) {
            order[at] = slot;
        } else {
            order.push_back(slot);
        }
        const UnitType& t = Unit_Type(s, p, u);
        if (u.is_deleted == 0 && u.field_024 > 1.0f) Event_UnitSound(s, p, u, 5, 6);  // engine loop, view: on screen
        if (s.tick % 200 == static_cast<std::uint32_t>(p) * 20) TUnit_CheckTargets(s, p, u);
        if (u.field_141 != 0) --u.field_141;
        if (u.field_144 != 0) --u.field_144;
        if (u.field_142 != 0) {
            --u.field_142;
            if (u.field_142 == 0) u.field_006 = 1;
        }
        if (u.num_work == 5) {
            TUnit_TurnStep(s, p, u);
        } else if (u.is_deleted == 0) {
            if (t.field_468 != 0) TankRedirectAllArround(s, p, u);
            // Upkeep: the x87 subtraction of the float cost from the double stock (53-bit), clamped at 0.0.
            double* stock[5] = {&pl.gold, &pl.food, &pl.iron, &pl.wood, &pl.oil};
            double* stat[5] = {&pl.gold_stat, &pl.food_stat, &pl.iron_stat, &pl.wood_stat, &pl.oil_stat};
            const float cost[5] = {t.upkeep[3], t.upkeep[0], t.upkeep[2], t.upkeep[1], t.upkeep[4]};
            for (int k = 0; k < 5; ++k) {
                *stock[k] = *stock[k] - static_cast<double>(cost[k]);
                *stat[k] = *stat[k] - static_cast<double>(cost[k]);
                if (*stock[k] < 0.0) *stock[k] = 0.0;
            }
            if (mod50 == u.frame_slot && u.field_006 != 0) TStrateg_AutoTarget(s, p, u);
            if (!u.order_queue.empty()) OrderQueue_Next(s, p, u);  // our rules (B317): the Shift-click queue
            const Cell c0{u.map_x, u.map_y};
            const float a0 = u.angle;
            if (u.work != 0x14) TUnit_MoveFunc(s, p, u);
            MoveHold_Refresh(s, p, u, t, c0, a0);  // our rules (B316)
            if (s.tick % 33 == 0) Grid_PushUnit(s, p, u);
        } else {
            const std::uint32_t next = u.next;
            if (MakeMoveForDead(s, p, slot)) {
                slot = next;
                continue;
            }
        }
        if (s.tick % 30 == 0 && t.is_ship != 0) Ship_ReleaseSprites(s, p, pl.unit_slots[slot]);
        slot = u.next;
    }
    order.resize(std::min(order.size(), at));
    LargeCells_Flush(s);

    if (Rng_Next(s) % 30 == 1) Fields_Decay(s);
    TStrateg_UpdateHouses(s, p);
}


void TStrateg_Update(GameState& s, ScriptVm* vm, std::uint8_t p) {
    Player& pl = s.players[p];
    if (pl.is_lost != 0) {
        // DeleteUnit set IsLost when your last corpse left the list before a check saw it (docs/endgame.md 1.6): in a
        // Lua mission the script then never runs again and the game hangs. Our rules end it as lost (docs/bugs.md
        // B236), or, when the script has already stopped, with the script's winner (B242).
        if (s.tick % 50 == p && s.lua_mission != 0 && !s.original_rules && p == s.my_player && s.end_message == 0) {
            if (s.script_active == 0) {
                pl.is_lost = 0;  // IsLost set by DeleteUnit without Die: Die may run for you in the end
                LuaMission_End(s);
            } else {
                LuaMission_Beaten(s);
            }
        }
        return;
    }
    s.cur_player = p;
    if (s.tick % 1000 == 10) {  // the history sample of the Stat doubles (the wall-clock time is not kept)
        pl.history[pl.history_index] = HistSample{pl.gold_stat, pl.wood_stat, pl.iron_stat, pl.oil_stat, pl.food_stat};
        ++pl.history_index;
        if (pl.history_index >= 1000) pl.history_index = 0;
    }
    if (s.tick % 20 == 0) Market_AutoTrade(s, p);
    if (s.plus_rules && s.tick % 1000 == 0) Market_RecoverPlus(pl);  // Entente Engine Plus, E2: the prices come back
    if (s.tick % 50 == p && s.lua_mission != 0) {



        if (p == s.my_player) {
            // Our rules: the skirmish's death rule (B236), while the script runs; once it has stopped (its end message
            // up, the end at this pass) its winner stands, as in the original (B242).
            if (!s.original_rules && s.script_active != 0 && HasNoActiveUnits(s, p)) {
                LuaMission_Beaten(s);
                return;
            }
            if (vm == nullptr) {
                // a Lua mission stepped without its VM (a test, or a game loaded without M6)
                Unported(s, orig::kLuaManagerPass);
            } else if (ScriptVm_Pass(*vm, s) == 0 && s.script_active == 0) {
                LuaMission_End(s);
                if (pl.is_lost != 0) return;
            }
        }
    } else if (s.tick % 50 == p) {



        if (MissionEvents_Update(s, p)) return;
        if (HasNoActiveUnits(s, p)) {
            for (std::size_t q = 0; q < s.players.size(); ++q) {
                if (s.players[q].is_lost != 0 && q != p) continue;
                if (MissionEvents_Update(s, static_cast<std::uint8_t>(q))) break;
            }
            TStrateg_Die(s, p);  // every player has an AI object, so the original's "no AI" return never happens
            return;
        }
    }
    if (pl.field_9b2d9c != 0 || pl.control_type == 1) AI_Think(s, p);
    AI_UpdateGroups(s, p);
    if (pl.control_type != 4 || pl.field_9b2d94 != 0) {
        const std::uint32_t period = pl.ai_level == 1 ? 200 : pl.ai_level == 2 ? 100 : 60;
        if (s.tick % period == static_cast<std::uint32_t>(p) * 6) AIScript_Run(s, p);
    }
    // Entente Engine Plus, stage 4 (docs/plus-balance.md 6): the smart research and upgrade rule of a computer and of
    // your economic advisor, every 500 ticks, the players spread by 50 ticks.
    if (s.plus_rules && (pl.control_type == 1 || pl.field_9b2d94 != 0) &&
        (s.tick + 50u * static_cast<std::uint32_t>(p)) % 500 == 0) {
        Plus_Brain(s, p);
    }
    // Entente Engine Plus, the call-in strikes (docs/decisions.md 34, sim/strike.hpp): a computer and your military
    // advisor, every 500 ticks, half way between the smart rule's checks.
    if (s.plus_rules && (pl.control_type == 1 || pl.field_9b2d9c != 0) &&
        (s.tick + 50u * static_cast<std::uint32_t>(p) + 250u) % 500 == 0) {
        Strike_Brain(s, p);
    }
    TStrateg_ExecuteCommands(s, p);
    if (pl.is_lost == 0) TStrateg_UpdateUnits(s, p);
}




void View_SetHouseDepth(GameState& s, const Command& c) {
    if (c.args.size() != 4 || c.args[0].tag != OrderArg::Tag::House) return;
    Command rec = c;
    rec.tick = s.tick + 1;
    s.view_inputs.push_back(rec);
    House* h = House_Find(s, c.args[0].house);
    if (h == nullptr) {
        Unported(s, orig::kRenderHouses);  // a house slot the port does not have
        return;
    }
    // the input names another house type than the port's
    if (h->type != c.args[1].raw) Unported(s, orig::kRenderHouses);
    h->depth_phase = static_cast<std::uint8_t>(c.args[2].raw > 2 ? 0xff : c.args[2].raw);
    h->depth_stage = static_cast<std::uint8_t>(c.args[3].raw);
    if (h->depth_phase != 0xff && House_DepthMap(s, c.args[0].house.player, *h) == nullptr) {
        Unported(s, orig::kRenderHouses);  // the depth map of that graphic was not loaded
    }
}

}  // namespace



void step(GameState& s, ScriptVm* vm, std::span<const Command> commands) {
    if (s.paused != 0) return;
    for (Player& pl : s.players) {  // unit slots freed in the previous tick become reusable
        pl.free_slots.insert(pl.free_slots.end(), pl.freed_slots.begin(), pl.freed_slots.end());
        pl.freed_slots.clear();
    }
    s.orders.clear();
    s.executed.clear();
    s.deselected.clear();
    s.view_inputs.clear();
    s.events.clear();
    CoarseWater_RefreshMarks(s);
    for (const Command& c : commands) {
        if (c.kind == Command::Kind::HouseDepth) {
            View_SetHouseDepth(s, c);
        } else if (c.parent == 0) {
            Order_Apply(s, c, commands);
        }
    }
    ++s.tick;
    Projectiles_Update(s);
    Shells_Update(s);
    PathHelper_TickRefresh(s);
    Particles_Update(s);
    Tick_ResetFrameState(s);
    for (int pass = 0; pass < 2; ++pass) {
        for (std::size_t p = 0; p < s.players.size(); ++p) {
            if ((s.players[p].control_type == 1) == (pass == 1)) TStrateg_Update(s, vm, static_cast<std::uint8_t>(p));
        }
    }
    if (s.tick % 30 == 0) Fog_Rebuild(s);
}

}  // namespace antanta::sim
