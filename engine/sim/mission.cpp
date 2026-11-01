

#include "mission.hpp"

#include "mission_events.hpp"
#include "skirmish.hpp"

namespace antanta::sim {

MissionSetup MissionSetup_FromMis(const MisFile& file, const std::string& path) {
    MissionSetup m;
    m.path = path;
    m.map = std::string(mis_name(file.info.map));
    m.my_player = file.my_player.value_or(0);
    for (std::size_t p = 0; p < m.players.size(); ++p) {
        const MisPlayer& f = file.players[p];
        // f32 in the file, stored as doubles (exact)
        m.players[p] = MissionPlayer{f.ai_type, f.team, f.gold, f.iron, f.wood, f.food, f.oil};
        if (p >= file.player_count) continue;
        m.blocks[p] = f.blocks;
        for (MisBlock& b : m.blocks[p]) b.done = 0, b.countdown = 0;
    }
    return m;
}

namespace {

std::u16string Utf16(std::string_view s) {
    std::u16string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<std::uint8_t>(s[i]);
        const int n = c < 0x80 ? 0 : c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
        std::uint32_t v = n == 0 ? c : n == 1 ? c & 0x1f : n == 2 ? c & 0x0f : c & 0x07;
        for (int j = 1; j <= n && i + j < s.size(); ++j) v = v << 6 | (static_cast<std::uint8_t>(s[i + j]) & 0x3f);
        i += static_cast<std::size_t>(n) + 1;
        if (v >= 0x10000) {
            out += static_cast<char16_t>(0xd800 + ((v - 0x10000) >> 10));
            out += static_cast<char16_t>(0xdc00 + ((v - 0x10000) & 0x3ff));
        } else {
            out += static_cast<char16_t>(v);
        }
    }
    return out;
}

}  // namespace

void MissionSetup_SetTexts(MissionSetup& setup, const TextTable& table) {
    const std::size_t slash = setup.path.find_last_of("\\/");
    if (slash == std::string::npos) return;
    const std::size_t prev = slash == 0 ? std::string::npos : setup.path.find_last_of("\\/", slash - 1);
    const std::string folder = setup.path.substr(prev == std::string::npos ? 0 : prev + 1,
                                                 prev == std::string::npos ? slash : slash - prev - 1);
    std::string stem = setup.path.substr(slash + 1);
    if (const std::size_t dot = stem.find_last_of('.'); dot != std::string::npos) stem.resize(dot);
    static constexpr const char* kKinds[3] = {"B", "C", "UC"};
    for (std::size_t p = 0; p < setup.blocks.size(); ++p) {
        for (std::size_t k = 0; k < setup.blocks[p].size(); ++k) {
            MisBlock& b = setup.blocks[p][k];
            std::u16string* field[3] = {&b.text_b, &b.text_c, &b.text_uc};
            for (int i = 0; i < 3; ++i) {
                const std::string key =
                    mission_block_key(folder, stem, static_cast<unsigned>(p), static_cast<unsigned>(k), kKinds[i]);
                const TextRecord* r = find_text(table, key);
                std::u16string t = Utf16(r != nullptr ? r->text : key);
                t.resize(512, u'\0');
                *field[i] = std::move(t);
            }
        }
    }
}

MissionSetup MissionSetup_FromMit(const MitFile& file, const std::string& path) {
    MissionSetup m;
    m.path = path;
    m.map = file.map;
    m.mit = true;
    m.my_player = static_cast<std::uint8_t>(file.my_player);
    m.player_count = static_cast<std::uint8_t>(file.players.size());
    for (std::size_t p = 0; p < file.players.size() && p < m.players.size(); ++p) {
        const MitPlayer& f = file.players[p];
        m.players[p] = MissionPlayer{static_cast<std::uint8_t>(f.ai_type),
                                     static_cast<std::int8_t>(f.team),
                                     static_cast<double>(f.gold),
                                     static_cast<double>(f.iron),
                                     static_cast<double>(f.wood),
                                     static_cast<double>(f.food),
                                     static_cast<double>(f.oil)};
    }
    return m;
}

std::optional<GameState> GameState_NewMission(const MissionSetup& setup, const MapData& map, const GameData& data,
                                              const AiTemplate* tpl, std::string* error) {
    if (!NewGame_CheckMap(map, error)) return std::nullopt;
    const std::size_t positions = map.player_nations.size();
    if (setup.my_player >= positions) {
        *error = "MyPlayer " + std::to_string(setup.my_player) + " is not a position of the map";
        return std::nullopt;
    }
    if (setup.mit && setup.player_count != positions) {  // Mission_Setup: PlayersQuantity must be NumPlayers
        *error =
            "the .mit has " + std::to_string(setup.player_count) + " players, the map " + std::to_string(positions);
        return std::nullopt;
    }

    GameState s;
    NewGame_InitGame(s, data, setup.seed);  // one table fill: no time(NULL) reseed, no position shuffle
    s.original_rules = setup.original_rules;





    s.my_player = setup.my_player;
    std::array<MapPosition, 8> spec{};
    for (std::size_t p = 0; p < positions; ++p) {
        spec[p].nation = map.player_nations[p];
        spec[p].colour = static_cast<std::uint8_t>(p);
        spec[p].map_name = p != s.my_player;
        spec[p].field_20099 = map.player_bytes29[p];
    }
    if (!NewGame_LoadMap(s, map, setup.map, spec, nullptr, tpl, error)) return std::nullopt;

    // Per player: AIType and TAI +6 from the file; in a .mis the human's become 2 (the helper AI); the level stays
    // the TAI constructor's 2 unless +hardlevel gives the computer players another; SetTeam(team) as is; the start
    // stocks from the file (single player: no resource level, no winter food), StartRes = them; a .mis sets the
    // stage field_2cc9a to 1 (its blocks are slice M3). The water-map AIType override is the network game's only.
    for (std::size_t p = 0; p < positions; ++p) {
        Player& pl = s.players[p];
        const MissionPlayer& f = setup.players[p];
        pl.ai_type = f.ai_type, pl.ai_kind = f.ai_type;
        if (!setup.mit && (pl.control_type == 4 || pl.control_type == 5)) pl.ai_type = 2, pl.ai_kind = 2;
        if (setup.hard_level != 0 && p != s.my_player) pl.ai_level = setup.hard_level;

        pl.team = f.team;
        if (pl.is_lost == 0 && s.flag_9b2df9 != 0) {
            for (std::size_t q = 0; q < positions; ++q) {
                const Player& o = s.players[q];
                if (q != p && o.is_lost == 0 && o.team != -1 && o.team == f.team) s.flag_9b2df9 = 0;
            }
        }
        pl.gold = f.gold, pl.iron = f.iron, pl.wood = f.wood, pl.food = f.food, pl.oil = f.oil;
        pl.start_res = {pl.gold, pl.food, pl.wood, pl.iron, pl.oil};  // StartRes: Gold, Food, Wood, Iron, Oil
        if (!setup.mit) pl.field_2cc9a = 1;
    }
    // TStrateg_UpdateAllies for every player: allied with the players of its team, itself included; team -1 none.
    for (std::size_t p = 0; p < positions; ++p) {
        for (std::size_t q = 0; q < positions; ++q) {
            s.players[p].is_ally[q] = s.players[p].team != -1 && s.players[q].team == s.players[p].team ? 1 : 0;
        }
    }
    s.mission_path = setup.path;
    if (!setup.mit) {
        for (std::size_t p = 0; p < positions; ++p) s.players[p].blocks = setup.blocks[p];
    }

    NewGame_AfterMap(s, setup.economic, setup.military);


    Fog_Rebuild(s);
    MissionEvents_Start(s);
    return s;
}

}  // namespace antanta::sim
