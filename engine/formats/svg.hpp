


#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bytes.hpp"
#include "map.hpp"
#include "mis.hpp"

namespace antanta {

// A (player, id or slot) reference from the link part of chunk 3; player 0xff = not stored.
struct SvgLink {
    std::uint8_t player = 0xff;
    std::uint16_t id = 0;
};



struct SvgGroup {
    std::array<std::uint8_t, 0x80> raw{};
    std::uint32_t queued_orders = 0;                 // m: 16-byte follow-up order records (kept raw below)
    std::vector<std::uint8_t> orders;                // m x 16 bytes
    std::vector<std::uint16_t> member_ids;
    std::vector<std::array<std::int32_t, 2>> slots;
    std::vector<std::uint8_t> field_0c;
    std::uint16_t wp_count = 0, wp_next = 0;
    std::vector<std::array<std::int32_t, 2>> wp;
    std::vector<std::uint8_t> wp_orient;
};

// One template entry of the AI script (6 bytes): u8 kind (1 unit, 2 house, 3 research), u8 unused, u16 type,
// u16 count.
struct SvgScriptEntry {
    std::uint8_t kind = 0;
    std::uint16_t type = 0, count = 0;
};


struct SvgScript {
    std::array<std::uint16_t, 40> want_u{};
    std::array<std::uint16_t, 30> want_h{};
    std::uint8_t nsteps = 0, step = 0;
    std::uint16_t entry = 0;
    std::vector<std::vector<SvgScriptEntry>> main, list2;
    std::int32_t base_x = 0, base_y = 0, field_660 = 0, field_664 = 0;
    std::vector<std::uint64_t> items;
    std::int32_t field_704 = 0, field_708 = 0;
};



struct SvgControlGroup {
    std::uint8_t kind = 0;
    std::vector<std::uint16_t> ids;
};



struct SvgCommand {
    std::array<std::uint8_t, 0x5c> raw{};
    bool has_record = false;
    std::array<std::uint8_t, 0x1c> record{};
    std::uint32_t group_id = 0;
    std::uint8_t group_player = 0;
    std::vector<SvgLink> list;  // (player, unit id), head first
};

struct SvgPlayer {
    // SelHasClass6, SelHasClass7, NumSelectedHouses, g_wNumSelectedHouse (UI selection summary)
    std::uint8_t sel_class6 = 0, sel_class7 = 0;
    std::uint16_t sel_houses = 0, sel_house = 0xffff;
    std::uint8_t nation = 0;
    std::uint8_t field_20099 = 0;  // not understood (1, 204, 253 or 255 in the saves: likely uninitialised)
    std::uint8_t ai_type = 0;
    std::u16string name;           // Name (UTF-16)
    std::int32_t units_alive = 0;
    std::int32_t units_lost = 0;
    std::int32_t units_killed = 0;
    double food_stat = 0, gold_stat = 0, iron_stat = 0, oil_stat = 0, wood_stat = 0;
    double gold = 0, wood = 0, iron = 0, food = 0, oil = 0;                           // saved in this order
    // HistFood, HistWood, HistOil, HistIron, HistGold (f64[1000] each, in this order) and HistTime (u32[1000]).
    std::array<std::vector<double>, 5> hist;
    std::vector<std::uint32_t> hist_time;
    std::uint8_t field_20170 = 0;  // not understood
    std::uint16_t unit_cap = 0;
    std::uint8_t field_2cc9a = 0;  // the mission stage (1 in the saves; docs/missions.md 3.4)
    std::vector<MisBlock> blocks;  // BlobSize, Blob: the mission event blocks (0x27e4 bytes each, as in the .mis)
    std::uint8_t control_type = 0;
    std::uint8_t player_index = 0;
    std::uint8_t is_lost = 0;        // lost players have no AI script, units or AI blocks in the save
    std::int8_t team = -1;
    std::uint16_t num_selected = 0;  // NumSelectedUnits
    std::uint8_t house_array_freed = 0;



    float score = 0;
    std::int32_t houses_lost = 0, houses_destroyed = 0, houses_built = 0;
    std::uint32_t play_time = 0;
    std::array<double, 5> start_res{}, mined{};
    std::vector<std::uint8_t> zone_grid;
    std::uint16_t zone_width = 0;
    std::array<std::uint8_t, 8> is_ally{};


    std::vector<std::uint16_t> free_ids;
    std::uint16_t next_id = 0;
    std::array<SvgControlGroup, 10> control_groups;  // after the units
    std::vector<SvgCommand> commands;                // the command queue (CmdQueueCount nodes)


    std::uint32_t ai_next_serial = 0;
    std::int32_t ai_counter = 0;
    std::uint8_t ai_kind = 0, ai_level = 0;
    std::vector<SvgGroup> groups;
    bool has_script = false;  // live players only
    SvgScript script;
};




struct SvgUnitTypeStats {
    std::array<std::uint16_t, 8> range{}, max_hp{}, accuracy{}, reload{};
    std::array<std::array<std::uint16_t, 20>, 8> attack{}, defense{};
};

// One THouse slot: the 0xc0-byte record by value (pointers inside are stale), plus its queue from chunk 3.
struct SvgHouse {
    std::array<std::uint8_t, 0xc0> raw{};
    std::uint16_t num = 0;
    std::int32_t map_x = 0;
    std::int32_t map_y = 0;
    std::uint16_t field_020 = 0;
    std::uint16_t type = 0;
    float hp = 0;
    std::uint8_t is_destroyed = 0;
    std::uint16_t queue_count = 0;
    float capacity = 0;
    std::vector<std::uint16_t> queue;          // chunk 3, only for houses with hp > -10000
    std::vector<SvgLink> inside;
    SvgLink attack;
    SvgLink ship;
    std::vector<std::uint32_t> upgrade_slots;
};




struct SvgWeapon {
    std::uint8_t index = 0, barrels = 0;
    std::uint32_t flash = 0;
    float angle = 0, target = 0;
    std::uint8_t unit_player = 0xff;
    std::uint16_t unit_id = 0;
    std::uint8_t house_player = 0xff;
    std::uint16_t house_slot = 0;
    std::uint16_t reload = 0;
    std::uint32_t field_38 = 0;
};

// One TUnit: the 0x17c-byte record by value (pointers inside are stale; only their non-zero-ness is used).
struct SvgUnit {
    std::array<std::uint8_t, 0x17c> raw{};
    std::uint8_t num_work = 0;
    std::uint8_t is_deleted = 0;
    float x = 0, y = 0;
    std::uint16_t map_x = 0;
    std::uint16_t map_y = 0;
    float dest_x = 0, dest_y = 0;
    float num_sprite = 0;
    std::uint8_t orient = 0;
    float angle = 0;
    std::uint8_t player = 0;
    std::uint32_t serial = 0;
    std::uint16_t type = 0;
    std::uint16_t id = 0;
    std::uint8_t work = 0;
    std::uint8_t player_unit_attack = 0;
    std::uint16_t attack_unit_id = 0;
    float hp = 0;
    bool has_house = false;
    std::uint8_t player_house_attack = 0;
    bool has_transporter = false;
    std::vector<std::uint8_t> path;
    bool weapon_target = false;
    std::vector<SvgWeapon> weapons;

    SvgLink attack;
    SvgLink house;
    SvgLink house_attack;
    SvgLink future_transporter;
    SvgLink transporter;
    std::vector<SvgLink> passengers;
};


struct SvgNeutralHouse {
    std::int32_t map_x = 0;  // +00
    std::int32_t map_y = 0;  // +04
    std::uint8_t type = 0;   // +08
};


struct SvgSpecial {
    std::uint8_t kind = 0;
    std::uint16_t x = 0, y = 0;
    float f_a = 0;
    float f_b = 0;
    std::uint8_t b = 0;
};


struct SvgShell {
    std::array<std::uint8_t, 0x110> raw{};
    SvgLink owner;
};


struct SvgBullet {
    std::array<std::uint8_t, 0x14> raw{};
    bool has_owner = false;
    SvgLink owner;
};

struct SvgChunk {
    std::uint16_t tag = 0;
    Bytes data;
};

struct SvgSave {
    std::vector<SvgChunk> chunks;  // every chunk, raw, in file order
    // chunk 8
    std::uint32_t tick = 0;  // g_GameTick when saved; the first tick after a load is tick + 1
    // chunk 4 "GameStat"
    std::uint32_t rng_seed = 0;
    std::uint16_t rng_index = 0;
    std::uint8_t my_player = 0;


    std::string mission_path;


    std::array<float, 8> power_used{}, power_6c4{}, power_6e4{}, power_made{};
    std::array<float, 8> factor_684{}, factor_664{}, factor_644{};  // per-player factors (iron, wood, oil income)
    std::array<float, 8> factor_744{};               // per-player food factor (a delivery brings 80 times it)
    std::array<float, 8> factor_724{};
    std::array<float, 2> factor_9b3338{1.0f, 1.0f};
    std::array<float, 8> factor_9e28fc{};
    std::array<float, 8> factor_9b32d8{};
    std::array<std::uint8_t, 8> field_9b2d9c{};
    std::array<std::uint8_t, 8> field_9b2d94{};


    std::array<std::array<std::uint8_t, 8>, 24> research_flags{};
    std::array<std::uint8_t, 160> research_9b39f0{}, research_9b3950{};
    std::uint8_t flag_8ab825 = 0, flag_8ab824 = 0, flag_8ef4d4 = 0, flag_8ef4d5 = 0, flag_8ef4d6 = 0;  // mission flags
    std::int32_t climate = 0;
    std::uint8_t fog_553ae0 = 0;
    std::array<float, 2> shell_spread{1.0f, 1.0f};
    std::array<float, 2> accuracy_factor{1.0f, 1.0f};

    std::array<std::uint8_t, 8> mint_done{};
    std::array<std::uint32_t, 8> stat_colour{};
    std::array<std::uint32_t, 8> colour{};
    std::array<std::uint16_t, 8> minimap_colour{};
    std::array<std::uint32_t, 8> choosable_colour{};



    std::array<std::uint8_t, 240> research_9b3860{}, research_9b3770{};
    std::array<std::uint32_t, 8> special_shells{};
    std::array<float, 2> factor_9b33b8{1.0f, 1.0f}, factor_9b3318{1.0f, 1.0f};
    std::uint8_t flag_8e836e = 0;
    std::uint16_t sel_8ab774 = 0, sel_8ab770 = 0;  // the selection summary: first unit's type, group or house id
    std::uint8_t formation_budget = 0;
    std::uint32_t field_553aec = 0;
    std::uint8_t flag_9b2df9 = 0;
    std::uint8_t skirmish_flag = 0;


    std::array<std::array<std::uint8_t, 0x68>, 8> slots{};
    std::uint8_t slot_count = 0;
    std::string map_name;

    std::array<std::array<float, 6>, 8> res_sold{}, res_bought{};
    std::vector<std::uint32_t> port_sites;
    std::vector<std::uint32_t> decaying_fields;

    struct Reveal {
        std::int32_t x = 0, y = 0;
        std::uint8_t radius = 0;
    };
    std::array<Reveal, 10> reveal{};
    // chunk 1 "GameMap"
    std::uint16_t num_players = 0;
    std::uint16_t map_width = 0;               // cells per row
    std::uint16_t map_height = 0;              // rows
    std::uint16_t view_x = 0, view_y = 0;
    std::vector<std::uint8_t> heights;
    std::vector<std::uint8_t> cell_data;       // W*H*22, the per-cell block read after the heights
    std::vector<std::uint16_t> objects;
    std::vector<std::uint8_t> flags;           // W*H, the byte grid read after the objects
    std::vector<std::uint16_t> obstacle_grid;
    std::vector<MapRoute> routes;  // the coast polygons after the grid (map .dat tail B; drawn, not simulated)
    std::vector<MapDecoration> decorations;
    Bytes house_basic;                          // g_HouseBasic as saved, 150 records of 0x2b0 bytes
    std::vector<SvgPlayer> players;             // num_players
    std::vector<std::vector<SvgHouse>> houses;  // per player, NumHouse[p] slots
    std::array<std::vector<SvgUnitTypeStats>, 5> unit_stats;  // per nation, NumUnitTypes[n] types
    std::array<std::uint16_t, 8> num_units{};                 // NumUnits[8] as saved
    std::vector<std::vector<SvgUnit>> units;  // per player in list order (FirstUnit -> Next); empty if lost
    std::uint16_t neutral_houses = 0;
    std::vector<SvgNeutralHouse> neutral;  // neutral_houses records
    // chunk 6 (optional): table capacity and records
    std::uint16_t special_capacity = 0;
    std::vector<SvgSpecial> specials;

    std::uint32_t bullets = 0;
    std::vector<SvgBullet> bullet_records;  // in file order
    std::vector<SvgShell> shells;
};



using UnitTypeCounts = std::array<std::uint16_t, 5>;

// Parses a whole .svg (pack wrapper included). Bad data is an error, never a crash.
std::optional<SvgSave> parse_svg(ByteSpan file, const UnitTypeCounts& unit_types, std::string* error);
// Parses the inflated chunk stream.
std::optional<SvgSave> parse_svg_stream(ByteSpan data, const UnitTypeCounts& unit_types, std::string* error);

}  // namespace antanta
