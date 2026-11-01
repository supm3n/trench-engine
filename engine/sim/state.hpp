// The one GameState (docs/architecture.md "sim/"). Milestone 2: a loaded save plus what one infantry unit needs to
// walk (docs/movement.md). It grows per milestone; field names follow docs/structs.md, unknown fields keep their
// TUnit offset (field_xxx).
//
// Field classes (engine/CONVENTIONS.md "GameState field classes"): every GameState field is one of
//   saved       part of the game: written by our own save, covered by GameState_Hash in a fixed order;
//   scratch     never saved or hashed: reset inside every tick, or rebuilt on load from saved fields and game data;
//   diagnostic  traces and tests only; the sim never reads it for behaviour.
// Structs held by a saved field (Unit, House, Player, ...) are saved in full unless a field says otherwise.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "formats/map.hpp"
#include "formats/params.hpp"
#include "formats/svg.hpp"
#include "sim/events.hpp"
#include "sim/fog.hpp"

namespace antanta::sim {




struct WeaponType {
    std::uint8_t kind = 0;
    std::uint16_t attack = 0;
    std::uint16_t reload = 0;
    float fire_range = 0;
    float min_range = 0;
    std::uint8_t field_48 = 0;
    std::array<float, 3> point_a{};
};




struct ShipWeaponType {
    std::array<float, 3> pos{};
    std::uint8_t arc_count = 0;
    std::array<float, 6> arcs{};
    float f28 = 0;
    std::uint8_t barrels = 0;
    float turn_step = 0;
    std::uint8_t steps = 0;
    std::uint8_t kind = 1;
    std::uint16_t attack = 300, reload = 0;
    float fire_range = 2000.0f, min_range = 400.0f;
    std::uint8_t slot = 0;
    std::vector<std::array<float, 3>> points;
};



struct ShipGraphic {
    bool loaded = false;                     // entry 0 was read (the nation 0 cruiser's file is missing)
    std::uint8_t frames = 0;
    std::vector<ShipWeaponType> weapons;
    std::vector<std::array<float, 3>> hull;
    bool has_text = false;
    std::uint8_t sink_count = 0;
};

struct UnitType {
    bool present = false;
    std::uint16_t graphic = 0;
    std::uint8_t move_class = 0;
    std::uint8_t is_ship = 0;
    std::uint8_t is_air = 0;
    std::uint8_t field_006 = 0;
    std::uint8_t field_007 = 0;
    std::uint8_t field_008 = 0;
    std::uint8_t field_009 = 0;
    std::uint8_t field_00b = 0;
    std::uint8_t field_00c = 0;
    std::uint8_t field_5e0 = 0;
    std::vector<WeaponType> weapons;



    std::array<float, 8> speed{};
    std::array<float, 8> waypoint_radius{};
    float turn_rate = 0;

    std::uint16_t field_434 = 0;
    std::uint16_t field_48a = 0;
    float turn_radius = 0;
    std::array<float, 30> anim_speed{};
    std::array<std::uint8_t, 30> anim_size{};
    std::uint8_t field_468 = 0;
    std::uint8_t field_594 = 0;
    float field_598 = 0;
    std::int16_t size = 0;
    std::uint8_t field_5d8 = 0;
    float field_5dc = 0;
    std::uint16_t field_588 = 0;
    std::array<float, 6> upkeep{};
    std::array<float, 6> cost{};
    // Per-player tables (index = player). MaxHP and ReloadTime come from the save when one is loaded (upgrades).
    std::array<std::uint16_t, 8> reload{};
    std::array<std::uint16_t, 8> max_hp{};
    std::array<std::uint16_t, 8> build_time{};
    std::array<std::uint16_t, 8> range{};
    std::array<std::uint16_t, 8> accuracy{};

    std::vector<std::array<std::uint16_t, 20>> attack = std::vector<std::array<std::uint16_t, 20>>(8);
    std::vector<std::array<std::uint16_t, 20>> defense = std::vector<std::array<std::uint16_t, 20>>(8);
    ShipGraphic ship;
};

// THouseBasic (docs/structs.md), the fields production and the house loop read.
struct HouseType {
    bool present = false;
    std::array<float, 5> upkeep{};
    std::array<float, 5> cost{};
    std::vector<std::uint16_t> prereq;
    float power_use = 0;
    std::uint8_t field_1b0 = 0;
    std::uint8_t field_1b1 = 0;
    std::uint8_t field_1b3 = 0;

    std::uint8_t field_1f2 = 0;
    std::uint8_t field_2ae = 0;
    std::uint8_t field_2ac = 0;
    std::uint16_t work_rate = 0;
    std::uint16_t max_hp = 0;
    std::uint16_t field_14c = 0;
    std::uint8_t field_26e = 0;


    std::array<std::uint16_t, 10> fire_x{}, fire_y{}, flash_x{}, flash_y{};
    std::uint16_t sprite_w = 0;
    std::uint16_t sprite_h = 0;
    std::int16_t field_140 = 0;
    std::int16_t field_142 = 0;
    std::uint16_t anchor_x = 0;
    std::uint16_t anchor_y = 0;
    std::int16_t exit_x = 0, exit_y = 0;
    std::uint32_t field_190 = 0;
    std::uint16_t field_148 = 0;
    std::uint8_t hit_count = 0;
    std::array<std::uint16_t, 10> hit_x{}, hit_y{};
    std::array<std::uint16_t, 10> produce{0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
                                          0xffff, 0xffff, 0xffff, 0xffff, 0xffff};
};

// The external handle of a unit: per-player id (reused after death) and global serial (never reused), docs/
// architecture.md "handles". Resolved in O(1) by Unit_Find through the player's id -> slot table.
struct UnitRef {
    std::uint8_t player = 0xff;
    std::uint16_t id = 0;
    std::uint32_t serial = 0;
};

// A house slot handle. The original reuses dead house slots, so a slot carries a generation that changes when the
// slot gets a new house. generation 0 means "whatever house is in the slot now" (O lines carry no generation: the
// original passes the slot's THouse*).
struct HouseRef {
    std::uint8_t player = 0xff;
    std::uint16_t slot = 0;  // index in g_pHouse[player] (THouse.Num of a live house)
    std::uint32_t generation = 0;
};

inline constexpr std::uint32_t kNoSlot = 0xffffffffu;

struct Unit {                                   // TUnit (0x17c bytes in the original)
    std::uint8_t num_work = 0;
    std::uint8_t is_deleted = 0;
    std::uint8_t field_006 = 0;
    std::uint8_t frame_slot = 0;
    float field_024 = 0;
    float x = 0, y = 0;
    float z = 0;
    std::uint32_t next = kNoSlot;
    std::uint32_t prev = kNoSlot;
    float vel_x = 0, vel_y = 0;
    std::uint16_t map_x = 0, map_y = 0;
    float dest_x = 0, dest_y = 0;
    float final_x = 0, final_y = 0;
    std::int32_t field_060 = 0, field_064 = 0;
    float num_sprite = 0;
    std::uint8_t orient = 0;
    std::uint8_t target_orient = 0;
    float angle = 0;
    std::uint8_t field_078 = 100;
    std::uint16_t field_07c = 0;
    std::uint32_t serial = 0;
    std::uint8_t field_086 = 0;
    std::uint16_t type = 0;
    std::uint16_t id = 0;
    std::uint8_t field_093 = 0;
    std::uint8_t field_09d = 0;
    std::uint8_t is_stopped = 0;
    std::uint8_t field_09f = 0;
    std::uint8_t field_09c = 0;
    float field_08c = 0;
    float field_0d4 = 0, field_0d8 = 0;
    UnitRef attack_ref;
    // Side by side (not in the original's order): a dying unit's clean-up scans these in every slot of every player
    // (AnySlotRefersTo, docs/bugs.md B59), and together they share one cache line.
    bool has_attack_unit = false;
    bool has_future_transporter = false;
    bool in_transport = false;
    UnitRef future_transporter;
    UnitRef transporter;
    std::uint8_t house_attack_player = 0xff;
    std::uint16_t house_attack_slot = 0;
    std::uint8_t weapon_count = 0;

    std::uint8_t field_158 = 0;
    std::uint8_t field_15c = 0;
    bool weapon_target = false;
    float field_050 = 0, field_054 = 0;
    float field_094 = 0, field_098 = 0;
    std::uint8_t house_player = 0xff;
    std::uint16_t house_slot = 0;
    struct Waypoint {
        float x = 0, y = 0;
        std::uint8_t work = 0xff;
        std::uint16_t field_0ca = 0xffff;
    };
    std::array<Waypoint, 2> waypoints{};
    std::uint8_t field_0bc = 0;
    std::vector<std::uint8_t> path;
    std::uint16_t path_index = 0xffff;
    std::uint8_t work = 0xff;
    std::uint16_t field_0ca = 0xffff;
    std::uint8_t player_unit_attack = 0xff;
    std::uint16_t attack_unit_id = 0;
    float hp = 0;
    std::uint32_t field_0e4 = 0;
    std::uint8_t field_0e8 = 0;
    std::uint8_t field_0e9 = 0;
    std::uint16_t field_0ee = 0;
    std::uint16_t field_0ec = 0;
    std::uint8_t field_092 = 0;
    bool in_house = false;
    std::uint8_t field_0f8 = 0;
    std::uint8_t player_house_attack = 0xff;
    bool has_house_attack = false;
    bool in_group = false;
    std::uint32_t group = 0;
    std::uint16_t group_index = 0xffff;
    std::uint8_t field_106 = 0;
    std::uint8_t field_107 = 0;
    std::uint8_t field_116 = 0xff;
    float field_118 = 0;
    std::uint8_t field_141 = 0;
    std::uint16_t field_142 = 0;
    std::uint8_t field_144 = 0;
    std::uint8_t field_145 = 0;
    std::uint8_t field_154 = 0;
    // Not understood, kept as the save has them (docs/save-audit.md 3.5; the simulation does not write them).
    std::uint8_t field_018 = 1;
    float field_058 = 0;
    float field_05c = 0;
    std::uint8_t field_06d = 100;
    std::uint8_t field_0ea = 0xff;
    std::uint32_t field_160 = 0;
    std::uint32_t field_174 = 1;
    std::uint8_t field_178 = 0xff;
    UnitRef mg_target;
    bool has_mg_target = false;
    std::uint8_t mg_delay = 20;
    bool in_cell_list = false;      // listed in g_CellUnits at (map_x, map_y)
    // saved (milestone 3 slice H, ships; docs/movement.md 15)
    std::uint8_t field_108 = 1;
    std::array<std::uint8_t, 8> field_121{};
    std::array<std::uint8_t, 8> field_129{};
    std::array<std::uint8_t, 8> field_131{};
    std::uint8_t field_139 = 0;
    std::uint32_t field_11c = 0;
    std::uint8_t field_120 = 0;
    std::vector<UnitRef> passengers;
    float field_16c = 0, field_170 = 0;
    struct ShipGun {
        float angle = 0;
        float target = 0;
        std::uint16_t reload = 0;
    };
    std::vector<ShipGun> guns;  // ships only


    std::uint8_t nation = 0xff;
    // Our rules (docs/bugs.md B277): the ground unit a fighter goes back to after fighting a plane that shot at it
    // (player 0xff: none). Not in the original.
    UnitRef return_target;
};

struct House {  // THouse slot


    std::uint8_t field_04a = 0, field_08f = 0, field_090 = 0, field_0b9 = 0;
    std::vector<std::uint32_t> upgrade_slots;
    std::uint16_t num = 0;
    std::uint16_t type = 0;
    std::int32_t map_x = 0, map_y = 0;
    float hp = 0;
    std::uint8_t is_destroyed = 0;
    std::uint16_t queue_count = 0;
    std::vector<std::uint16_t> queue;
    std::uint32_t generation = 1;    // HouseRef: changes when the slot is given to a new house (not ported yet)
    float x = 0, y = 0;
    float exit_x = 0, exit_y = 0;
    std::uint16_t field_020 = 0;
    std::uint16_t units_inside = 0;
    std::vector<UnitRef> inside;
    std::uint8_t field_030 = 0;
    float rally_x = 0, rally_y = 0;
    float field_044 = 0;
    std::uint16_t field_05c = 0;
    float field_060 = 0;
    std::uint8_t field_064 = 0;
    std::uint8_t field_065 = 0;
    std::uint16_t capacity = 0;
    std::uint16_t max_hp = 0;
    std::uint8_t field_06a = 0;



    struct ResearchFlag {
        std::uint8_t kind = 0, index = 0;
    } field_06c;
    float field_070 = 0;

    std::uint8_t field_074 = 0;
    float field_078 = 0;
    float field_07c = 0;
    float field_080 = 0;
    std::uint8_t field_084 = 0xff;
    UnitRef field_088;
    std::uint8_t field_08c = 0;
    std::uint32_t field_0b4 = 0;
    bool has_field_0b0 = false;
    std::uint8_t field_0b8 = 0xff;
    std::int32_t field_0a8 = 0;
    std::uint8_t selected = 0;


    std::uint8_t graphic_phase = 1;
    std::uint8_t graphic_stage = 0;




    std::uint8_t depth_phase = 0xff;
    std::uint8_t depth_stage = 0;


    std::uint32_t field_0ac = 0;
    UnitRef field_0b0;


    std::uint8_t nation = 0xff;
};




struct HouseDepthMap {
    std::uint8_t nation = 0, type = 0, phase = 0, stage = 0;
    bool spans = false;
    std::int32_t width = 0, height = 0;
    std::vector<std::uint16_t> depth;  // width * height
};

// A formation group: TAIGroup (0x80 bytes, docs/groups.md 1.2), in the player's AI group list.
struct Group {
    std::uint32_t serial = 0;
    std::vector<UnitRef> members;
    std::vector<std::array<std::int32_t, 2>> slots;
    std::vector<std::array<std::int32_t, 2>> wp;
    std::vector<std::uint8_t> wp_orient;
    std::uint16_t wp_count = 0, wp_next = 0;
    std::uint16_t live = 0;
    std::uint16_t quorum = 0;
    std::uint8_t type = 1, spacing = 1, cols = 1;
    std::uint16_t leader = 0xffff;
    std::uint8_t keep_spacing = 0;
    float target_fx = 0, target_fy = 0;
    std::uint8_t slow = 0;
    std::uint8_t arrange_orient = 0xff;
    float spacing_override = -10.0f;
    std::uint16_t wait = 0;
    std::uint8_t queued = 0;
    std::uint8_t selected = 0;
    std::uint8_t ai_owned = 1;
    std::int32_t field_048 = 0;
    std::uint8_t large = 0;
    std::uint8_t retries = 0;
    std::uint16_t blocked = 0;
    std::uint8_t has_mc7 = 0, has_mc10 = 0;
    std::uint8_t arg3 = 0;
    std::uint8_t player = 0;
    std::uint8_t kind = 0;
    std::uint8_t target_player = 0xff;
    std::uint16_t target_id = 0xffff;
    std::int32_t tx = 0, ty = 0;
    std::uint8_t stepping = 0;
    std::uint16_t half = 0;
    std::uint8_t dead = 0;
    float speed = 10.0f;
    float speed_factor = 1.0f;
    std::uint8_t orient = 5;
    std::uint16_t capacity = 0;
    std::uint8_t field_069 = 0;
    std::vector<std::uint8_t> field_0c;
    std::vector<std::array<std::uint8_t, 16>> follow_up;
};



struct CommandNode {
    std::uint32_t seq = 0;  // identifies the node (the original compares node pointers)
    std::uint16_t cls = 0;
    std::uint8_t cmd = 0;
    float x = 0, y = 0;
    UnitRef unit;

    std::uint32_t group = 0;                            // the group (Group::serial; the record's +0 is the TAIGroup*)
    std::vector<UnitRef> list;                          // +4 the unit list (head first)
    std::uint8_t a3 = 0, type = 0, cols = 0, kind = 0;
    std::int32_t px = 0, py = 0;
    std::uint8_t tplayer = 0;
    std::uint16_t tid = 0;
    std::uint32_t a2c = 0;
};

// One entry of an AI script template step (6 bytes in the original): kind 1 unit, 2 house, 3 research.
struct ScriptEntry {
    std::uint8_t kind = 0;
    std::uint16_t type = 0, count = 0;
};


struct AIScript {
    std::array<std::uint16_t, 40> want_u{};
    std::array<std::uint16_t, 30> want_h{};
    std::uint8_t nsteps = 0, step = 0;
    std::uint16_t entry = 0;
    std::vector<std::vector<ScriptEntry>> main, list2;         // per step
    std::int32_t base_x = 99999, base_y = 0;
    std::int32_t field_660 = 30, field_664 = 0;
    std::vector<std::uint64_t> items;
    std::int32_t field_6f4 = 0, field_6f8 = 0, field_6fc = 0;  // chunk 9: saved base, build fail count
    std::int32_t field_704 = -1, field_708 = -1;
    std::int32_t field_70c = 0;
};

// One history sample (TStrateg_Update at tick % 1000 == 10): the Stat doubles.
struct HistSample {
    double gold = 0, wood = 0, iron = 0, oil = 0, food = 0;
};


struct CommandExec {
    std::uint32_t seq = 0;
    std::uint8_t player = 0;
    std::uint16_t rng_index = 0;
    CommandNode node;
};

struct Player {  // TStrateg
    // saved
    std::uint8_t nation = 0;
    std::uint8_t ai_type = 0;
    std::uint8_t control_type = 0;  // 1 = computer, 4 = the local human in the saves seen so far
    std::uint8_t is_lost = 0;
    std::int32_t units_alive = 0, units_lost = 0, units_killed = 0;
    std::uint16_t unit_cap = 0;
    double gold = 0, wood = 0, iron = 0, oil = 0, food = 0;


    double gold_stat = 0, wood_stat = 0, iron_stat = 0, oil_stat = 0, food_stat = 0;
    std::vector<HistSample> history = std::vector<HistSample>(1000);  // HistGold.. [HistoryIndex] (not loaded)
    std::array<std::uint16_t, 40> unit_count{};
    std::array<std::uint8_t, 25> houses_done{};
    std::optional<AIScript> script;
    // The unit list (FirstUnit -> Next, the order the game updates units) over stable slots: a slot never moves, so
    // a Unit& stays valid while units are created or deleted; a freed slot is reused from the next tick on, so it
    // never aliases another unit inside a tick. Saved as the units in list order (slot numbers are not state).
    std::deque<Unit> unit_slots;
    std::uint32_t first_unit = kNoSlot;
    std::uint32_t num_units = 0;
    std::vector<House> houses;           // g_pHouse[p], NumHouse[p] slots
    std::vector<CommandNode> commands;   // TStrateg command queue
    std::vector<Group> groups;
    std::uint32_t next_group = 1;


    std::uint32_t ai_next_serial = 0;
    std::int32_t ai_counter = 0;
    std::uint8_t ai_kind = 0, ai_level = 0;
    std::int32_t ai_retry = 0;
    std::uint8_t ai_all = 0;
    std::int32_t ai_countdown = 0;

    std::array<std::uint8_t, 4> field_9b2a98{1, 1, 1, 1};
    std::vector<std::uint16_t> free_ids;
    std::uint16_t next_id = 0;


    float power_used = 0, power_6c4 = 1, power_6e4 = 1, power_made = 0;
    std::uint8_t field_9b2b38 = 1;
    std::uint8_t field_9b2b39 = 1;  // + 1 (the AI may train workers)
    std::uint8_t field_9b2b3a = 1;  // + 2 (the AI may train other units)
    std::uint8_t field_9b2b3b = 1;
    std::uint8_t field_9b2b3c = 1;
    std::uint8_t field_9b2b3d = 1;
    std::uint16_t history_index = 0;
    std::vector<std::uint8_t> zone_grid;
    std::uint16_t zone_width = 0;
    std::array<std::uint8_t, 8> is_ally{};
    float shell_spread = 1.0f;
    float accuracy_factor = 1.0f;
    std::uint8_t field_9b2d94 = 0;
    std::uint8_t field_9b2d9c = 0;
    std::array<std::uint8_t, 24> research_flags{};
    std::array<std::uint8_t, 20> research_9b39f0{}, research_9b3950{};  // [p * 20 + r]: research started (GameStat)

    float factor_684 = 1, factor_664 = 1, factor_644 = 1;
    float factor_744 = 1;
    float factor_724 = 1;


    float factor_9b3338 = 1, factor_9e28fc = 1, factor_9b32d8 = 1;




    std::uint8_t mint_done = 0;
    std::int32_t mint_count = 0;
    float gold_factor = 1.0f;






    std::int32_t houses_lost = 0, houses_destroyed = 0, houses_built = 0;
    std::array<double, 5> start_res{}, mined{};
    std::array<float, 6> res_sold{}, res_bought{};




    std::uint8_t market_auto = 0, market_buy = 0;
    std::array<float, 5> market_sell{};
    float market_received = 0;
    // Entente Engine Plus, E2 (docs/plus-balance.md 3): the market's price index of each resource the player sells
    // (Food, Wood, Iron, Gold, Oil), 1.0 at the start. Saved (SIM version 13); hashed only under Plus.
    std::array<float, 5> market_index{1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    // Entente Engine Plus, F6 (docs/plus-balance.md 2): how often Delousing (Academy of Sciences row 17) was done; its
    // price doubles each time. Saved (SIM version 14); hashed only under Plus.
    std::uint8_t plus_delousing = 0;



    std::uint8_t plus_build_level = 0;
    // Entente Engine Plus, the call-in strikes (docs/decisions.md 34, sim/strike.hpp): the tick from which each strike
    // (artillery barrage, creeping barrage, gas attack, bombing run, recon flight) may be called again (0: now), and
    // the recon flight's reveal (its centre cell and the tick it ends; the fog shows it to this player only). Saved
    // (SIM version 16); hashed only under Plus.
    std::array<std::uint32_t, 5> plus_strike_ready{};
    std::int32_t plus_recon_x = 0, plus_recon_y = 0;
    std::uint32_t plus_recon_end = 0;
    // Entente Engine Plus, stage 4 (docs/plus-balance.md 6 and 11): the smart research rule's last action, for your
    // advisor's status line. Interface only: not saved, not hashed, never read by the simulation.
    struct PlusStatus {
        // 0 nothing yet, 1 an Academy of Sciences row, 2 a Military Academy row, 3 a mine, gold mine or oil rig
        // upgrade, 4 an academy built for a research, 5 power (research, station upgrade or a new station), 6 room
        // (a new dwelling or a dwelling upgrade), 7 a tower or pillbox upgrade, 8 a Building Academy row (F7)
        std::uint8_t kind = 0;
        std::uint16_t item = 0;  // the row (kinds 1, 2, 8), else the house type
        std::int32_t value = 0;  // pay back in game minutes (economy), own units it improves (army), else 0
        std::uint32_t tick = 0;
    } plus_status;
    float score = 0;
    std::uint8_t house_array_freed = 0;
    std::uint32_t stat_colour = 0;
    std::uint32_t play_time = 0;
    std::int64_t last_time = 0;
    std::u16string name;      // Name (the save's; empty in a new game, where the front end names the players)
    std::uint8_t colour = 0;
    std::int8_t team = -1;
    // The history's wall clock HistTime (u32[1000]), as saved; not simulation state, not hashed.
    std::vector<std::uint32_t> hist_time = std::vector<std::uint32_t>(1000);
    // Selection and control groups (UI state the save keeps): SelHasClass6, SelHasClass7, NumSelectedHouses,
    // g_wNumSelectedHouse, NumSelectedUnits, and the 10 number-key groups (kind, unit ids as saved).
    std::uint8_t sel_class6 = 0, sel_class7 = 0;
    std::uint16_t sel_houses = 0, sel_house = 0xffff, num_selected = 0;
    struct ControlGroup {
        std::uint8_t kind = 0;
        std::vector<std::uint16_t> ids;
    };
    std::array<ControlGroup, 10> control_groups{};



    std::array<std::uint8_t, 30> research_9b3860{}, research_9b3770{};
    std::uint32_t special_shells = 0;
    float factor_9b33b8 = 1.0f, factor_9b3318 = 1.0f;
    float factor_9b32f8 =
        1.0f;
    // Not understood, kept as the save has them: field_20099 (1, 204, 253, 255: likely uninitialised),
    // field_20170. field_2cc9a is the mission stage (docs/missions.md 3.4).
    std::uint8_t field_20099 = 0, field_20170 = 0, field_2cc9a = 1;
    // BlobSize, Blob: the player's mission event blocks (docs/missions.md 3.2), with their running state (done,
    // countdown, the open prerequisites, and the timer and flags some conditions rewrite). Empty in a skirmish.
    std::vector<MisBlock> blocks;



    std::uint8_t net_ai = 0;
    std::array<std::uint8_t, 11> net_kept{};
    // scratch (rebuilt from the unit list on load)
    std::vector<std::uint32_t> id_slots;
    std::vector<std::uint32_t> free_slots;   // slots free for Unit_Add
    std::vector<std::uint32_t> freed_slots;  // slots freed in this tick; released to free_slots by step()
};



struct Bullet {
    float x = 0, y = 0, z = 0;    // +0, +4, +8 (z: the ground height, not read)
    std::uint16_t field_00c = 0;
    std::uint16_t delay = 0;
    UnitRef owner;
};


struct Shell {
    float x0 = 0, x1 = 0, y0 = 0, y1 = 0, z0 = 0, z1 = 0;
    float cx = 0, cy = 0, cz = 0;
    double g = 0;
    double vx = 0, vy = 0;
    double v0 = 0;
    double vz = 0;
    double flight = 0;
    float substep = 0;
    float range = 0;
    float a4 = 0;
    std::uint8_t fail = 0, player = 0xff;
    UnitRef owner;
    std::uint8_t gas = 0, no_effect = 0;
    std::int32_t radius = 0;
    std::uint8_t sound = 0;
    std::int32_t sound_tick = 0;
    std::int32_t damage = 0;
    std::uint16_t delay = 0;
    std::uint8_t kind = 0;
    std::uint8_t fragments = 0;
    std::int32_t flown = 0;
};



struct GasCloud {
    std::uint32_t kind = 0;
    std::int32_t x = 0, y = 0;
    std::int32_t life = 0;
    std::uint16_t max_radius = 0;
    std::uint16_t step = 0;
    std::uint8_t puffs = 0;
    std::int32_t damage = 0, damage_radius = 0;
    std::uint8_t type = 0;
    std::int16_t radius = 1;
};


struct PathRecord {
    std::int32_t sx = 0, sy = 0, ex = 0, ey = 0;
    std::uint32_t tick = 0;
    std::vector<std::uint8_t> path;
};


struct NeutralHouse {
    std::uint8_t type = 0;
    std::int32_t map_x = 0, map_y = 0;
};



struct SpecialObject {
    std::uint16_t x = 0xffff, y = 0xffff;  // +0, +2
    std::uint8_t kind = 0xff;              // +4
    std::uint8_t alive = 0;
    float f_a = 1000000.0f;
    float hp = 65535.0f;
};



struct ShipRec {
    UnitRef unit;
    std::uint16_t i = 0, j = 0;
};






struct CoarseWater {
    std::int32_t w = 0, h = 0;
    std::vector<std::uint8_t> g;
    std::vector<ShipRec> recs;
};


struct TerrainCell {
    std::uint16_t tile = 0;  // LandTextures id + 1 in bits 0..14, 0x8000 = has sub-tiles, 0xffff = not drawn
    std::uint8_t shade = 0;  // vertex grey
    std::uint8_t unused = 0;
    std::array<std::uint16_t, 9> sub{};  // 3 x 3 sub-tile ids row by row, 0 = none
};

struct Map {
    // saved
    std::vector<TerrainCell> terrain;
    CoarseWater water;
    std::uint16_t width = 0, height = 0;
    std::vector<std::uint8_t> heights;
    std::vector<std::uint16_t> objects;
    std::vector<std::uint8_t> flags;
    std::vector<std::uint16_t> obstacle;
    std::vector<NeutralHouse> neutral_houses;
    std::vector<SpecialObject> specials;
    std::uint16_t special_capacity = 0;            // records allocated (+6)
    std::vector<std::uint16_t> pass_land;
    std::vector<std::uint16_t> pass_large;
    std::vector<std::uint8_t> weight;
    std::vector<std::vector<UnitRef>> cell_units;
    // scratch
    std::vector<std::uint16_t> wave;
    // Counts the changes of the cells' objects, heights and flags other than the large-unit marks (PathGrid_Build,
    // PathGrid_UpdateRegion except for LargeCells_Set / Clear), so the shared route fields (FlowField, sim/flow.cpp),
    // which leave the marks out, look at the map again after a change that may matter.
    std::uint32_t pass_gen = 0;
};


struct HouseFootprint {
    std::uint8_t a = 0;
    std::uint8_t b = 0;
};
using HouseFootprints = std::array<std::array<HouseFootprint, 25>, 6>;  // g_HouseBasic[nation * 25 + type]

// Global game data that is not in a save: unit stats and Data/GameInf.txt settings.
struct GameData {
    std::array<std::array<UnitType, 40>, 6> unit_types{};  // g_BasicUnit[type + nation * 40]
    HouseFootprints house_footprints{};                    // from BasicParams<nation>.dat (nation 5: neutral)


    std::array<std::vector<std::uint8_t>, 3> object_blocking{};
    std::array<std::array<HouseType, 25>, 6> house_types{};  // g_HouseBasic[nation * 25 + type]
    std::uint8_t speed_ms = 53;
    std::uint8_t short_anims = 0;
};

// A player order applied between two ticks. Its canonical text form is the recorder's O line (tools/recorder/
// FORMAT.md "O: player order"): Command_Print writes one, Command_Parse reads one, and the two round-trip. The
// same struct carries the orders of a recording, of our own replays and UI, and later of lockstep packets.
// One kind per hooked order function of the recorder (FORMAT.md table, same order); per-kind argument layout =
// that table's "args" column (raw 32-bit values as the O line stores them, units and houses as handles).
struct OrderArg {
    enum class Tag : std::uint8_t {
        Raw,      // `7f`: the 32 raw bits (f32 arguments as float bits)
        Unit,     // `u<id>.<serial>` (player unknown in an O line: 0xff)
        NoUnit,   // `u-`
        House,    // `h<player>.<num>`
        NoHouse,  // `h-`
        Group,    // `g`: the formation group (ecx of group_order); members in Command::units
        Array,    // `a<v>/<v>/...`: i32 values read at call time
        Bytes,    // `b<hex>`: raw bytes, one per element of values
        Unknown,  // `?`: the recorder could not read it
    };
    Tag tag = Tag::Raw;
    std::uint32_t raw = 0;
    UnitRef unit;
    HouseRef house;
    std::vector<std::int32_t> values;
};

struct Command {
    enum class Kind : std::uint8_t {
        UiOrder,
        OrderMove,
        EnterHouse,
        AttackHouse,
        Attack,
        OrderXy,
        Board,
        OrderPoint,
        GroupOrder,
        Rally,
        Stop,
        NoFire,
        MayFire,
        GroupCreate,
        GroupDismiss,
        Unload,
        SetMode,
        Act_ba0a0,
        Act_b9fc0,
        Act_b9730,
        Act_b9c70,
        ReleaseUnit,
        Act_a5290,
        Act_a52c0,
        SellHouse,
        Act_baa60,
        Act_bbad0,
        Act_c0c20,
        Act_ea260,
        Act_c26e0,
        Act_c2d80,
        Act_b66a0,
        Act_a5380,
        Act_a53f0,
        Act_6cf80,
        // Appended 2026-10-13 (milestone 3 slice D): not a player order but a view input, the recorder's D line
        // (docs/view-dependence.md). args: house, house type, phase (0xff: both depth pointers null), stage.
        HouseDepth,
        // Appended 2026-10-13 (milestone 3 slice G): not a hooked order but the human build click, which queues a
        // class 2 cmd 7 node (docs/building.md 3). No O line of its own (the trace shows its C line). args: player,
        // x and y (f32 bits, the world point of the site cell), house type; units: the selection, head first.
        AddHouse,



        SelectHouses,



        ReleaseHouseUnit,



        ToggleAdvisor,





        MarketAuto,



        BindGroup,




        FacingOrder,





        SelectSummary,
        // Appended 2026-10-19 (slice MP9): not an order of the original but the network server's control change
        // (docs/multiplayer.md "The computer takes over"): the computer plays a player whose human left or dropped,
        // or gives the player back to its human, from the tick the server stamped. Only the server sends it (never a
        // client: net::Group_Authorize refuses it); a game that is not a network game ignores it. No O line of its
        // own. args: player, level (0: its human plays it again, 1..3: the computer at that level).
        NetControl,
        // Appended 2026-10-23 (Entente Engine Plus, docs/decisions.md 34): not an order of the original but the town
        // centre's call-in strike (sim/strike.hpp, Strike_Call), ours alone (address 0). No O line of its own. args:
        // player, strike (0 artillery barrage, 1 creeping barrage, 2 gas attack, 3 bombing run, 4 recon flight), x, y
        // (the target's world point), x2, y2 (the end point of the creeping barrage and the bombing run, else as x, y).
        PlusStrike,
    };
    static constexpr int kKinds = 46;
    Kind kind = Kind::UiOrder;
    std::uint32_t tick = 0;       // the tick the order is applied before
    std::uint32_t oseq = 0;       // order number (0 for an order not taken from a recording: the sim numbers it)
    std::uint32_t parent = 0;     // oseq of the enclosing order (children of ui_order), 0 = a top-level order
    std::int32_t player = -1;     // acting player, -1 unknown
    std::int32_t rng_index = -1;  // RngIndex on entry (written by the sim; ignored on input)
    std::int32_t cell_x = -1, cell_y = -1;
    std::vector<OrderArg> args;             // the function's arguments in call order
    std::vector<UnitRef> units;             // selection, group members or the unit argument (O field 10)
    std::vector<HouseRef> houses;           // the selected house (O field 11)
};


std::string_view Command_Name(Command::Kind k);
std::uint32_t Command_Address(Command::Kind k);
// One O line, without the line end. Fields after the eleventh (newer format versions) are ignored on parse.
std::string Command_Print(const Command& c);
std::optional<Command> Command_Parse(std::string_view line, std::string* error);
// A view input as the recorder's D line (tools/recorder/FORMAT.md, version 4): `D tick player num type phase
// stage`, phase and stage `-` for "no depth map". Command_ParseView gives a Command of kind HouseDepth (args: house,
// type, phase or 0xff, stage); Command_PrintView prints one without the line end.
std::string Command_PrintView(const Command& c);
std::optional<Command> Command_ParseView(std::string_view line, std::string* error);

// A call into original code the port does not have yet (or a branch of a ported function that is not ported):
// address -> how often and the first tick. Diagnostic: lets a trace divergence be pinned to the missing code.
struct UnportedHit {
    std::uint32_t count = 0;
    std::uint32_t first_tick = 0;  // g_GameTick at the first call (an order given before tick N shows N - 1)
};

// A shared route field (sim/flow.cpp, docs/movement.md 3.4, docs/bugs.md B88; our rules only): the cost of the
// cheapest walk from every cell to the nearest free cell of one block of 16 x 32 cells, over the land grid without
// the large-unit marks (Cell_LandStatic), built once and read by every long path request whose target lies in that
// block. weighted: the group wave's costs (StepCost + the weight of the cell entered) instead of the units'
// (StepCost). A pure cache of the map: brought up to date (repaired or built again) when the grid it reads changed,
// and the cheapest costs are unique, so what it gives never depends on whether it was kept.
struct FlowField {
    std::int32_t bx = 0, by = 0;  // the block (cell x / 16, cell y / 32)
    bool weighted = false;
    std::uint32_t ver = 0;            // GameState::flow_ver it is up to date with
    std::uint32_t used = 0;           // GameState::flow_clock at the last use (the least used one goes first)
    std::vector<std::uint16_t> dist;  // per cell, 0xffff: no walk to the block
};

// The cells of the route fields' land grid that changed (open or weight) on the way to version ver.
struct FlowChange {
    std::uint32_t ver = 0;
    std::vector<std::int32_t> cells;
};

struct GameState {
    // saved
    std::uint32_t tick = 0;
    std::uint8_t speed_ms = 53;
    std::uint8_t my_player = 0;
    std::uint8_t short_anims = 0;
    std::uint32_t rng_seed = 0;
    std::uint16_t rng_index = 0;
    std::uint32_t crt_holdrand = 1;  // MSVC CRT rand() state; the original's save does not restore it
    std::uint16_t map_width = 0, map_height = 0;
    std::vector<Player> players;
    std::array<std::array<UnitType, 40>, 6> unit_types{};
    HouseFootprints house_footprints{};                      // g_HouseBasic (a loaded save brings its own copy)
    std::array<std::array<HouseType, 25>, 6> house_types{};  // g_HouseBasic, production fields
    std::uint32_t next_serial = 0;
    std::int32_t climate = 0;
    Map map;
    std::vector<PathRecord> path_cache;
    std::int32_t path_budget_used = 0;
    std::int32_t path_budget_max = 30;
    std::int32_t deferred_path_count = 0;
    std::int32_t deferred_human_count = 0;
    std::uint32_t next_node_seq = 1;        // identity for queued command nodes (CommandNode::seq, Unit::field_160)
    std::uint8_t formation_budget = 0;




    std::uint8_t flag_8ab825 = 0, flag_8ab824 = 0, flag_8ef4d4 = 0, flag_8ef4d5 = 0, flag_8ef4d6 = 0;
    std::uint8_t flag_8e836e = 0;
    std::uint8_t skirmish_flag = 0;
    std::uint8_t land_map = 1;
    std::uint8_t flag_9b2df9 = 1;


    std::uint16_t sel_8ab774 = 0xfffe, sel_8ab770 = 0xfffe;
    std::string map_name;


    std::string mission_path;


    struct Reveal {
        std::int32_t x = 0, y = 0;
        std::uint8_t radius = 0;
    };
    std::array<Reveal, 10> reveal{};


    struct SlotRecord {
        std::u16string name;
        std::uint8_t nation = 0, colour = 0, team = 0, level = 0;
    };
    std::array<SlotRecord, 8> slots{};
    std::uint8_t slot_count = 0;
    std::vector<std::uint32_t> decaying_fields;


    std::vector<std::uint32_t> port_sites;



    std::array<std::array<std::int32_t, 2>, 256> mine_sites{};
    std::uint8_t mine_site_count = 0;


    std::int32_t grid_w = 0, grid_h = 0;
    std::vector<std::vector<UnitRef>> unit_cells;
    std::vector<std::vector<HouseRef>> house_cells;
    std::vector<Shell> shells;

    std::vector<GasCloud> gas_clouds;





    std::vector<std::array<std::int32_t, 2>> large_cells_clear;
    std::vector<std::array<std::int32_t, 2>> large_cells_set;
    std::vector<Bullet> bullets;


    std::vector<UnitRef> ships;




    std::map<std::uint16_t, std::uint32_t> graphic_refs;
    std::uint8_t fog_553ae0 = 0;
    // The original's balance slips of research 0x10 and 0x11 (docs/bugs.md B13): set only by a replay of the
    // original's recordings (antanta-headless --original-rules, the fidelity tests); a game the player plays keeps
    // false, the owner's fixed rules (2026-10-15).
    bool original_rules = false;
    // Entente Engine Plus (docs/your-version.md, docs/plus-balance.md): the owner's skirmish rule set, picked before
    // the game (SkirmishSetup::plus) and never switched during it. Every Plus change tests this flag; Classic (false)
    // and original_rules stay as they are. Never set together with original_rules. Hashed and saved only when set
    // (SIM version 12), so Classic hashes stay as they were.
    bool plus_rules = false;
    // A network game (docs/multiplayer.md): several human players (ControlType 4) share one state, so MyPlayer is
    // a view field there (each machine its own) and the sim decides by Player_IsYou. 0 in every single player game:
    // hashed and saved only when set, so their hashes and traces stay as they were.
    std::uint8_t net_game = 0;


    // and the end message the front end shows (0 none, 1 "Player <name> win!!!", 2 "!!! Your troops have been
    // destroyed !!!"); the message's close button opens the statistics.
    // A mission's win or lose block shows its own text as the end message (end_message 3, the text in end_text,
    // docs/missions.md 3.4); the close button opens the statistics as for the other two.

    std::uint8_t paused = 0;
    std::uint8_t game_result = 3;
    std::uint8_t end_message = 0;
    std::u16string end_text;





    struct ScriptTimer {
        std::string code;                      // run by lua_dostring when the timer fires
        float base = 0, period = 0;
        std::uint32_t repeats = 0, fired = 0;
    };
    struct ScriptGroup {
        std::uint8_t player = 0xff;
        std::map<std::uint16_t, UnitRef> members;  // map<u16 Id, TUnit*>; DeleteUnit's listener drops an id
    };
    std::uint8_t lua_mission = 0;
    std::uint8_t script_active = 0;
    std::uint32_t script_winner = 0;
    std::map<std::string, ScriptGroup> script_groups;
    std::map<std::string, ScriptTimer> script_timers;
    std::vector<std::string> script_keys;



    std::uint8_t show_fog_allies = 0;



    struct ShipSprite {
        std::uint32_t refs = 0;
        std::uint8_t state = 0;
        std::uint32_t ready_tick = 0;
    };
    std::vector<ShipSprite> ship_sprites = std::vector<ShipSprite>(6 * 4 * 18);
    // The wall clock (time(NULL) seconds) for PlayTime; the app sets the real one, tests a fixed one. Empty: the
    // clock stands still. Not simulation state.
    std::function<std::int64_t()> clock;
    // scratch
    std::vector<std::uint16_t> rng_table;
    std::array<std::vector<std::uint8_t>, 3> object_blocking{};  // GameData::object_blocking, copied at load


    std::vector<HouseDepthMap> house_depth_maps;
    std::uint32_t cmd_counter = 0;
    std::uint8_t cur_player = 0;
    // The kept route fields (sim/flow.cpp, at most a few dozen MB) and the count of their uses (FlowField::used).
    std::vector<FlowField> flow_fields;
    std::uint32_t flow_clock = 0;



    std::vector<std::uint8_t> flow_open, flow_weight;
    std::uint32_t flow_grid_gen = 0xffffffff, flow_ver = 0;
    std::vector<FlowChange> flow_log;
    std::vector<std::uint32_t> flow_stamp;
    std::uint32_t flow_stamp_id = 0;
    // The fog of war of MyPlayer's side (sim/fog.hpp): the sight circles of the last rebuild (every 30 ticks, a load,
    // a new game). Not hashed: nothing in the simulation reads it (docs/fog.md 4). The original rebuilds it after a
    // load; our own save keeps it, so a loaded game shows the same fog until the next rebuild (docs/save-own.md).
    Fog fog;
    // diagnostic
    std::map<std::uint32_t, UnportedHit> unported;  // Unported(): calls into code not ported yet
    std::vector<Command> orders;        // the orders the last step() applied, children included (for O lines)
    std::vector<CommandExec> executed;  // the commands the last step() executed (for C lines)
    std::vector<Command> view_inputs;   // the view inputs the last step() applied (for D lines)
    std::uint32_t order_seq = 0;        // last oseq given
    std::vector<UnitRef> deselected;
    std::vector<SimEvent> events;       // sounds, messages, marks the last step() raised (sim/events.hpp)
    bool log_moves = false;             // order_move calls write O lines (while an order is applied)
    std::uint32_t log_parent = 0;       // their parent oseq
};

// Counts a call into code not ported yet (GameState::unported). address: the original function that would run, or
// the ported function whose branch is not ported (the comment at the call says which).
void Unported(GameState& s, std::uint32_t address);



inline const Unit* Unit_Find(const GameState& s, const UnitRef& r) {
    if (r.player >= s.players.size()) return nullptr;
    const Player& p = s.players[r.player];
    if (r.id >= p.id_slots.size() || p.id_slots[r.id] == kNoSlot) return nullptr;
    const Unit& u = p.unit_slots[p.id_slots[r.id]];
    return u.serial == r.serial ? &u : nullptr;
}
inline Unit* Unit_Find(GameState& s, const UnitRef& r) {
    return const_cast<Unit*>(Unit_Find(static_cast<const GameState&>(s), r));
}


std::uint32_t Unit_Add(GameState& s, std::uint8_t player, const Unit& u, bool at_head);


void Unit_Delete(GameState& s, std::uint8_t player, std::uint32_t slot);
// The slots of the player's units in list order.
std::vector<std::uint32_t> Player_UnitSlots(const Player& p);

// HouseRef resolution: nullptr for a slot out of range or another generation (generation 0 accepts any).
House* House_Find(GameState& s, const HouseRef& r);

// The original's tests of p == MyPlayer that decide game state: in single player the local player, in a network game
// (GameState::net_game) every human player (ControlType 4), so every machine decides the same (docs/multiplayer.md).
inline bool Player_IsYou(const GameState& s, std::uint8_t p) {
    return s.net_game != 0 ? p < s.players.size() && s.players[p].control_type == 4 : p == s.my_player;
}

// The nation of a unit's or house's type record (TUnit::Basic, THouse::Basic): its own after a merge across nations
// (Unit::nation, House::nation), else its owner's. The original reads the record for the type's numbers, pictures
// and sounds; where it reads the owner's TStrateg::Nation instead (what the player builds or trains, the field kind
// of a farm, the dust of a vehicle and a few others), the port uses Player::nation.
inline std::uint8_t Unit_Nation(const GameState& s, std::uint8_t player, const Unit& u) {
    return u.nation != 0xff ? u.nation : s.players[player].nation;
}
inline std::uint8_t House_Nation(const GameState& s, std::uint8_t player, const House& h) {
    return h.nation != 0xff ? h.nation : s.players[player].nation;
}
inline const HouseType& House_Type(const GameState& s, std::uint8_t player, const House& h) {
    return s.house_types[House_Nation(s, player, h) % 6][h.type % 25];
}


inline std::uint16_t House_Durability(const GameState& s, std::uint8_t player, const House& h) {
    const std::uint16_t d = House_Type(s, player, h).field_148;
    return s.plus_rules && s.players[player].plus_build_level >= 2 ? static_cast<std::uint16_t>(d << 1) : d;
}



inline std::uint8_t House_TrainNation(const GameState& s, std::uint8_t player, std::uint16_t k) {
    const Player& pl = s.players[player];
    if (s.original_rules || k >= pl.houses.size()) return pl.nation;
    return House_Nation(s, player, pl.houses[k]);
}
inline const HouseFootprint& House_Footprint(const GameState& s, std::uint8_t player, const House& h) {
    return s.house_footprints[House_Nation(s, player, h) % 6][h.type % 25];
}

// GameState_Hash for the network game's desync check: the same fields without the per-machine view fields (MyPlayer,
// the pause, the result and the end message), with its own version, so two machines of one game give the same value.
std::uint64_t GameState_NetHash(const GameState& s);

// The state hash over the saved fields only, in a fixed order with fixed-width little-endian values and raw float
// bits (FNV-1a 64), so it is equal across compilers and targets. For replays, native versus web, save/load/continue
// and lockstep; the trace's unit_hash is the referee's CRC over U lines, not this.
std::uint64_t GameState_Hash(const GameState& s);


void Crt_srand(GameState& s, std::uint32_t seed);
int Crt_rand(GameState& s);


void RNG_FillTable(GameState& s);


std::optional<GameData> GameData_FromParams(std::span<const BasicParams> params, std::string* error);



std::uint8_t UnitType_NoMeleeStepsBack(const UnitType& t);


std::optional<GameState> GameState_FromSave(const SvgSave& save, const GameData& data, std::string* error);

// Reads byte 0 of the object records of Data/Objects/objects.les entries 0..2 into d.object_blocking.
bool GameData_LoadObjects(GameData& d, ByteSpan objects_les, std::string* error);




bool GameData_LoadShips(GameData& d, std::span<const BasicParams> params, ByteSpan towers_txt,
                        const std::function<std::optional<Bytes>(const std::string&)>& read_les, std::string* error);




std::optional<GameState> GameState_FromMap(const MapData& map, const GameData& data, std::string* error);

// Test bridge (not the original's behaviour): adds a unit the port could not create itself (production is not
// ported) at the end of the player's list, in its cell's list, with its ground height. Fields the caller does not
// know keep the Unit defaults.
void GameState_AdoptUnit(GameState& s, std::uint8_t player, const Unit& u);
// Test bridge: relinks the player's list in the given slot order (every listed slot exactly once).
void Player_SetUnitOrder(Player& p, std::span<const std::uint32_t> slots);

// The Lua VM of a mission (script/lua_manager.hpp). It lives outside GameState, which stays plain copyable data; the
// script data that belongs to the game (timers, groups, the active flag) are GameState fields, the Lua globals stay
// in the VM (their save is slice M6).
struct ScriptVm;



std::uint32_t ScriptVm_Pass(ScriptVm& vm, GameState& s);




void step(GameState& s, ScriptVm* vm, std::span<const Command> commands);

}  // namespace antanta::sim
