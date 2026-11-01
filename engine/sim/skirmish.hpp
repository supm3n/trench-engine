


#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "formats/aitemplate.hpp"
#include "formats/map.hpp"
#include "state.hpp"

namespace antanta::sim {


struct SkirmishSlot {
    std::uint8_t level = 0;
                              // game 4 and 5 are the human players (5 is handled like 4, docs/skirmish-start.md)
    std::uint8_t nation = 0;
    std::uint8_t colour = 0;
    std::uint8_t team = 0;
};


struct SkirmishSetup {
    std::string map;           // the file name in Maps\, as random.ind lists it ("Aust_2r_1.dat")
    std::uint8_t players = 2;
    std::array<SkirmishSlot, 8> slots{SkirmishSlot{4, 0, 0, 0}, SkirmishSlot{1, 1, 1, 0}};
    std::uint8_t resources = 1;
    // The seed of both table fills (InitGame's and RandomMap_Setup's). The original takes time(NULL) for the second
    // and whatever RngSeed holds for the first; the recorder forces both to its seed.
    std::uint32_t seed = 12345;



    std::array<std::u16string, 8> comp_names{};


    std::array<bool, 20> economic{true, true, true, true, true, true, true, true, true, true,
                                  true, true, true, true, true, true, true, true, true, true};
    std::array<bool, 20> military{true, true, true, true, true, true, true, true, true, true,
                                  true, true, true, true, true, true, true, true, true, true};
    // A network game (docs/multiplayer.md, GameState::net_game): every open slot of level 4 or 5 is a human player
    // (ControlType 4, the advisor switches above, the name in names); my_slot is this machine's slot, whose position
    // becomes MyPlayer. Every machine builds the same state but for MyPlayer. Off: the single player start.
    bool net = false;
    std::uint8_t my_slot = 0;
    std::array<std::u16string, 8> names{};  // per slot; empty: the position's IDS_COMP name
    // The host's game speed and short animations (Data/GameInf.txt, GameData::speed_ms, short_anims), which the state
    // keeps and hashes: a network game takes them from here, so every machine has the host's (0: GameData's).
    std::uint8_t speed_ms = 0, short_anims = 0;
    // The rule set (GameState::plus_rules, original_rules). plus: Entente Engine Plus, the owner's skirmish rules
    // (docs/your-version.md), part of a network game's start settings and of our own save's setup. original_rules:
    // the comparison replays only (antanta-headless --original-rules), never offered to players and not sent; it
    // wins over plus.
    bool plus = false;
    bool original_rules = false;
};

// The sim player of each screen slot when GameState_NewSkirmish starts `setup` on a map with `positions` player
// records (0xff: a closed slot, one past the player count or one the map has no position for). Every machine of a
// network game makes the same; the server (net/server.cpp) takes each connection's player from it, without the game's
// files, instead of believing the player a client names (docs/multiplayer.md). Depends only on the slots' levels, the
// player count and the seed.
std::array<std::uint8_t, 8> SkirmishSetup_SlotPlayers(const SkirmishSetup& setup, std::size_t positions);

// Fills setup.comp_names from a string table (IDS_COMP1..8; a missing key leaves that name empty).
void SkirmishSetup_CompNames(SkirmishSetup* setup, const std::map<std::string, std::string>& strings);

// The state at the end of the start, before tick 1 (the first GameFrame). tpl: Data\Template4.tpl, which every
// player's script loads (nullptr: the file is missing, the scripts stay empty as in the original). A map with fewer
// than 8 positions (newhouses.dat, 5) takes at most that many players and shuffles over its own positions, and its
// player houses go only to players in the game (deliberately different, docs/bugs.md B5).
std::optional<GameState> GameState_NewSkirmish(const SkirmishSetup& setup, const MapData& map, const GameData& data,
                                               const AiTemplate* tpl, std::string* error);

// Entente Engine Plus, item F4 (docs/plus-balance.md 2, docs/bugs.md B304): France's two likely typos in the unit
// data, for every player's row: the special soldier's (type 14) rifle attack 95 becomes 105, the battleship's (type
// 20) 10 becomes 100. Other values stay (data that already differs is left alone). GameState_NewSkirmish calls it
// for a Plus game; the unit tables are part of our own save, so a loaded game keeps them.
void Plus_FixUnitData(GameState& s);

// Entente Engine Plus, the upkeep of plus-balance.md 3 and 4 (per tick, every nation's type record): A2 the fixed
// heavy howitzer (type 23) 0.1 iron and 0.1 gold (the data has none); E1 the mortar man (5) 0.02 iron (was 0.01) and
// 0.015 gold, the machine gunner (6) 0.005 gold more; O1 (2026-10-23) the power station (house type 0) 0.25 oil a tick
// instead of 0.5. GameState_NewSkirmish calls it for a Plus game; the unit and house tables are part of our own save.
void Plus_SetUpkeep(GameState& s);

// Entente Engine Plus, R1 (docs/plus-balance.md 3, 2026-10-23): the Academy of Sciences (house type 2) and the Military
// Academy (5) cost 3,000 wood, iron and gold instead of 5,000, every nation. GameState_NewSkirmish calls it for a Plus
// game; the house tables are part of our own save, so a loaded game keeps the price it started with.
void Plus_SetHouseCost(GameState& s);




struct MapPosition {
    bool closed = false;           // a closed skirmish slot: lost, no house array, so no houses and units
    std::uint8_t nation = 0;       // 0..4
    std::uint8_t colour = 0;       // the colour table index 0..7
    bool map_name = false;         // named after the map's player record when it has a name
    std::u16string name;           // else (or with map_name and an empty record) this name
    std::uint8_t field_20099 = 0;  // TStrateg field_20099
    bool human = false;            // a network game's human player besides MyPlayer (ControlType 4)
};
// The map's size, player records and cell arrays are usable.
bool NewGame_CheckMap(const MapData& map, std::string* error);

void NewGame_InitGame(GameState& s, const GameData& data, std::uint32_t seed);



bool NewGame_LoadMap(GameState& s, const MapData& map, const std::string& name, const std::array<MapPosition, 8>& pos,
                     const std::array<std::int8_t, 8>* teams, const AiTemplate* tpl, std::string* error);

void NewGame_AfterMap(GameState& s, const std::array<bool, 20>& economic, const std::array<bool, 20>& military);

}  // namespace antanta::sim
