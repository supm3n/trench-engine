
#include "endgame.hpp"

#include <utility>

#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

void Game_SetClock(GameState& s, std::function<std::int64_t()> clock) {
    s.clock = std::move(clock);
    const std::int64_t now = s.clock ? s.clock() : 0;
    for (Player& pl : s.players) pl.last_time = now;
}

void Player_UpdatePlayTime(GameState& s, std::uint8_t p) {
    if (!s.clock) return;
    Player& pl = s.players[p];
    const std::int64_t now = s.clock();
    pl.play_time = static_cast<std::uint32_t>(static_cast<std::int64_t>(pl.play_time) + (now - pl.last_time));
    pl.last_time = now;
}

bool HasNoActiveUnits(const GameState& s, std::uint8_t p) {
    const Player& pl = s.players[p];
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        const Unit& u = pl.unit_slots[k];
        if (u.is_deleted != 0) continue;
        if (Unit_Type(s, p, u).is_air == 0) return false;
        if (u.map_x < s.map_width && u.map_y < s.map_height) return false;  // an aircraft on the map (u16 compare)
    }
    return true;
}





void TStrateg_Die(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    if (pl.is_lost != 0) return;  // the assertion "already dead"
    pl.is_lost = 1;
    Player_UpdatePlayTime(s, p);

    for (auto& c : s.house_cells) c.clear();
    for (auto& c : s.unit_cells) c.clear();




    if (s.net_game != 0) {
        if (p == s.my_player) {
            Event_Sound(s, 9, 2, 7);  // You_Lose
        } else if (NetGame_Outcome(s, s.my_player) == 0) {
            Event_Sound(s, 9, 1, 7);  // You_Win
        }
    } else if (p == s.my_player) {
        if (s.lua_mission == 0) s.end_message = 2, s.paused = 1;  // IDS_YOU_DEAD; a message window pauses the game
        s.game_result = 1;
        Event_Sound(s, 9, 2, 7);  // You_Lose
    } else {
        std::size_t q = 0;
        for (; q < s.players.size(); ++q) {
            if (s.players[q].is_lost == 0 && IsEnemy(s, s.my_player, static_cast<std::uint8_t>(q))) break;
        }
        if (q == s.players.size() && s.my_player < s.players.size() && s.players[s.my_player].is_lost == 0) {
            if (s.lua_mission == 0) s.end_message = 1, s.paused = 1;  // IDS_PLAYER + MyPlayer's name + IDS_WIN
            s.game_result = 0;
            Event_Sound(s, 9, 1, 7);  // You_Win
        }
    }


    for (std::size_t q = 0; q < s.players.size(); ++q) {
        Player& o = s.players[q];
        if (o.is_lost != 0) continue;
        for (std::uint32_t k = o.first_unit; k != kNoSlot; k = o.unit_slots[k].next) {
            Unit& v = o.unit_slots[k];
            if (v.player_house_attack == p || v.player_unit_attack == p) {
                v.has_attack_unit = false, v.player_unit_attack = 0xff;
                v.player_house_attack = 0xff, v.has_house_attack = false;
            }
            if (v.has_mg_target && v.mg_target.player == p) v.has_mg_target = false;
            if (v.work == 3 && v.in_house && v.house_player == p) {
                if (v.house_slot < pl.houses.size()) {
                    House_ReleaseUnit(s, p, pl.houses[v.house_slot], v);
                } else {
                    Unported(s, orig::kPlayerDie);  // a house slot the port does not have
                }
                v.in_house = false;
            }
        }
    }
    // Step 8: every cell that holds one of p's houses: the house is destroyed with hp at most -10000, so
    // OnHouseDestroyed(h, 0xff) removes it (no counter changes), then the cell is cleared.
    const auto lo = static_cast<std::uint16_t>((p + 1) * 1000);
    for (std::size_t i = 0; i < s.map.objects.size(); ++i) {
        const std::uint16_t c = s.map.objects[i];
        if (c < lo || c >= static_cast<std::uint16_t>(lo + 1000)) continue;
        const std::uint16_t slot = c % 1000;
        if (slot < pl.houses.size()) {
            House& h = pl.houses[slot];
            h.is_destroyed = 1;
            if (h.hp > -10000.0f) h.hp = -10010.0f;
            House_OnDestroyed(s, p, h, 0xff);
        } else {
            Unported(s, orig::kPlayerDie);  // a cell naming a house slot the port does not have
        }
        s.map.objects[i] = 0;
    }

    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        Cell_RemoveUnit(s, p, pl.unit_slots[k]);
    }


    pl.commands.clear();
    pl.script.reset();
    pl.zone_grid.clear();
    pl.groups.clear();
    for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
        Unit& u = pl.unit_slots[k];
        if (Unit_Type(s, p, u).is_ship != 0) Ship_Deleted(s, p, u);
        Cell_RemoveUnit(s, p, u);
    }
    pl.unit_slots.clear();
    pl.first_unit = kNoSlot;
    pl.num_units = 0;
    pl.id_slots.clear(), pl.free_slots.clear(), pl.freed_slots.clear();
    pl.free_ids.clear();
    pl.next_id = 0;

    pl.houses.clear();
    PathGrid_Build(s);
}

std::uint8_t NetGame_Outcome(const GameState& s, std::uint8_t me) {
    if (me >= s.players.size() || s.players[me].is_lost != 0) return 1;
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        if (s.players[q].is_lost == 0 && IsEnemy(s, me, static_cast<std::uint8_t>(q))) return 3;
    }
    return 0;
}

void LuaMission_End(GameState& s) {
    const bool winner = s.script_winner < s.players.size();
    for (std::size_t q = 0; q < s.players.size(); ++q) {  // the winner as an int: any other value lets everyone die
        if (s.players[q].is_lost != 0 || q == s.script_winner) continue;

        const auto w = static_cast<std::uint8_t>(s.script_winner), o = static_cast<std::uint8_t>(q);
        if (!s.original_rules && winner && !IsEnemy(s, w, o)) continue;
        TStrateg_Die(s, o);
    }
    // With the allies alive no Die may have set the result: you are on the winner's side, so you won.
    if (!s.original_rules && s.game_result == 3 && s.my_player < s.players.size() &&
        s.players[s.my_player].is_lost == 0)
        s.game_result = 0;


    s.end_message = 4, s.paused = 1;
}

void LuaMission_Beaten(GameState& s) {
    s.players[s.my_player].is_lost = 0;  // IsLost set by DeleteUnit without Die: Die runs now (it skips a lost player)
    TStrateg_Die(s, s.my_player);
    s.end_message = 2, s.paused = 1;  // IDS_YOU_DEAD as in the skirmish; its close button opens the statistics
}

std::vector<std::uint8_t> Statistics_Players(const GameState& s) {
    std::vector<std::uint8_t> out;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        if (s.players[p].house_array_freed == 0) out.push_back(static_cast<std::uint8_t>(p));
    }
    return out;
}

void Statistics_Open(GameState& s) {
    for (const std::uint8_t p : Statistics_Players(s)) {
        Player& pl = s.players[p];
        float f = 0.5f;
        if (pl.is_lost == 0) {
            Player_UpdatePlayTime(s, p);
            f = 2.0f;
        }
        const std::int32_t k = pl.units_killed != 0 ? pl.units_killed : 1;
        const std::int32_t d = pl.houses_destroyed != 0 ? pl.houses_destroyed : 1;
        // x87 (53-bit): (D / (HousesLost + 1) * 200.0f + K / (UnitsLost + 1) * 50.0f) * f, stored as f32.
        const double v = static_cast<double>(d) / static_cast<double>(pl.houses_lost + 1) * 200.0 +
                         static_cast<double>(k) / static_cast<double>(pl.units_lost + 1) * 50.0;
        pl.score = static_cast<float>(v * static_cast<double>(f));
    }
}

PlayerStatistics Statistics_Of(const GameState& s, std::uint8_t p) {
    const Player& pl = s.players[p];
    PlayerStatistics r;
    // The screen's columns Gold, Food, Wood, Iron, Oil; the f32 tables by the canonical index Food 0, Wood 1, Iron 2,
    // Gold 3, Oil 4.
    static constexpr std::array<std::size_t, 5> kTable = {3, 0, 1, 2, 4};
    const std::array<double, 5> current = {pl.gold, pl.food, pl.wood, pl.iron, pl.oil};
    for (std::size_t i = 0; i < 5; ++i) {
        const double bought = static_cast<double>(pl.res_bought[kTable[i]]);
        const double sold = static_cast<double>(pl.res_sold[kTable[i]]);
        r.extracted[i] = Ftol(pl.mined[i]);
        r.spent[i] = Ftol(pl.start_res[i] + pl.mined[i] + bought - current[i] - sold);
        r.bought[i] = Ftol(bought);
        r.sold[i] = Ftol(sold);
    }
    r.units_total = pl.units_lost + pl.units_alive, r.units_lost = pl.units_lost, r.units_killed = pl.units_killed;
    r.houses_built = pl.houses_built, r.houses_lost = pl.houses_lost, r.houses_destroyed = pl.houses_destroyed;
    r.nation = pl.nation;
    r.duration = pl.play_time;
    r.points = Ftol(static_cast<double>(pl.score));
    return r;
}

}  // namespace antanta::sim
