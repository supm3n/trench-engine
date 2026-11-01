


#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "formats/aitemplate.hpp"
#include "formats/campaign.hpp"
#include "formats/map.hpp"
#include "formats/mis.hpp"
#include "formats/museum.hpp"
#include "state.hpp"

namespace antanta::sim {

// What Mission_Setup takes from the mission file for one map position.
struct MissionPlayer {
    std::uint8_t ai_type = 0;                                // 0 Passive .. 5 Sea (consts.lua)
    std::int8_t team = 0;                                    // passed to SetTeam as is (0-based)
    double gold = 0, iron = 0, wood = 0, food = 0, oil = 0;  // the start stocks
};

struct MissionSetup {
    std::string path;
    std::string map;
    bool mit = false;               // from a .mit: the human keeps the file's AIType, the stage is not set
    std::uint8_t my_player = 0;     // the .mis's optional last byte (0 in every shipped file), the .mit's MyPlayer
    std::uint8_t player_count = 0;  // a .mit's PlayersQuantity, which must be the map's player count (0: a .mis)
    std::array<MissionPlayer, 8> players{};
    // A .mis's event blocks per player (BlobSize, Blob), done and countdown zeroed; their texts are the file's until
    // MissionSetup_SetTexts.
    std::array<std::vector<MisBlock>, 8> blocks{};

    std::uint8_t hard_level = 0;
    std::uint32_t seed = 12345;  // InitGame's table fill (the recorder's forced seed; the original keeps RngSeed)
    // Your advisor switches from Data/GameInf.txt, as SkirmishSetup's.
    std::array<bool, 20> economic{true, true, true, true, true, true, true, true, true, true,
                                  true, true, true, true, true, true, true, true, true, true};
    std::array<bool, 20> military{true, true, true, true, true, true, true, true, true, true,
                                  true, true, true, true, true, true, true, true, true, true};
    // GameState::original_rules from the start (the comparison replays, antanta-headless --original-rules). Missions
    // keep Classic otherwise; Entente Engine Plus is for skirmishes only (docs/decisions.md 1).
    bool original_rules = false;
};

// The setup of a .mis: the header's tables for every one of the 8 entries (Mission_Setup reads them for each map
// position); the map name is mis_name(info.map).
MissionSetup MissionSetup_FromMis(const MisFile& file, const std::string& path);


void MissionSetup_SetTexts(MissionSetup& setup, const TextTable& table);
// The setup of a .mit (the five resources are ints in the file).
MissionSetup MissionSetup_FromMit(const MitFile& file, const std::string& path);

// The state at the end of the start, before tick 1. tpl as for GameState_NewSkirmish. Fails when MyPlayer is not a
// position of the map or a .mit's player count is not the map's (the original's Mission_Setup returns 0 then).
std::optional<GameState> GameState_NewMission(const MissionSetup& setup, const MapData& map, const GameData& data,
                                              const AiTemplate* tpl, std::string* error);

}  // namespace antanta::sim
