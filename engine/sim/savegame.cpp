// Loading an original save (docs/save-format.md): what the original's loader does to the saved records before
// the first tick. Test bridge only; the engine's own save format comes later.
#include <bit>
#include <cstdint>
#include <map>

#include "mission_events.hpp"
#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

namespace {

float f32_at(const SvgUnit& u, std::size_t pos) { return std::bit_cast<float>(read_u32(u.raw, pos)); }

// The saved TUnit record (pointers inside are stale; only their non-zero-ness is used).
Unit unit_from_save(const SvgUnit& u) {
    const ByteSpan r = u.raw;
    Unit v;
    v.num_work = u.num_work, v.is_deleted = u.is_deleted, v.field_006 = r[0x06], v.frame_slot = r[0x07];
    v.field_024 = f32_at(u, 0x24);
    v.x = u.x, v.y = u.y, v.z = f32_at(u, 0x28);
    v.vel_x = f32_at(u, 0x34), v.vel_y = f32_at(u, 0x38);
    v.map_x = u.map_x, v.map_y = u.map_y;
    v.dest_x = u.dest_x, v.dest_y = u.dest_y, v.final_x = f32_at(u, 0x48), v.final_y = f32_at(u, 0x4c);
    v.field_060 = static_cast<std::int32_t>(read_u32(r, 0x60)),
    v.field_064 = static_cast<std::int32_t>(read_u32(r, 0x64));
    v.num_sprite = u.num_sprite, v.orient = u.orient, v.target_orient = r[0x6e], v.angle = u.angle;
    v.field_078 = r[0x78], v.field_07c = read_u16(r, 0x7c), v.serial = u.serial, v.field_086 = r[0x86];
    v.type = u.type, v.id = u.id, v.field_093 = r[0x93], v.field_09d = r[0x9d], v.is_stopped = r[0x9e];
    v.field_09f = r[0x9f], v.field_0bc = r[0xbc];
    v.field_050 = f32_at(u, 0x50), v.field_054 = f32_at(u, 0x54), v.field_094 = f32_at(u, 0x94);
    v.field_098 = f32_at(u, 0x98);
    v.field_09c = r[0x9c], v.field_08c = f32_at(u, 0x8c), v.field_0d4 = f32_at(u, 0xd4), v.field_0d8 = f32_at(u, 0xd8);
    v.weapon_count = r[0x114], v.field_158 = r[0x158], v.field_15c = r[0x15c];
    v.weapon_target = u.weapon_target;
    for (std::size_t k = 0; k < v.waypoints.size(); ++k) {
        Unit::Waypoint& w = v.waypoints[k];
        w.x = f32_at(u, 0xa4 + 12 * k), w.y = f32_at(u, 0xa8 + 12 * k);
        w.work = r[0xac + 12 * k], w.field_0ca = read_u16(r, 0xae + 12 * k);
    }
    v.path = u.path;
    v.path_index = read_u16(r, 0xc6);
    v.work = u.work, v.field_0ca = read_u16(r, 0xca);
    v.player_unit_attack = u.player_unit_attack, v.attack_unit_id = u.attack_unit_id;
    v.has_attack_unit = read_u32(r, 0xd0) != 0;
    v.hp = u.hp, v.field_0e4 = read_u32(r, 0xe4), v.field_0ee = read_u16(r, 0xee);
    v.field_0e8 = r[0xe8], v.field_0e9 = r[0xe9];
    v.field_0ec = read_u16(r, 0xec), v.field_092 = r[0x92];
    v.in_house = u.has_house, v.field_0f8 = r[0xf8], v.player_house_attack = u.player_house_attack;
    v.has_house_attack = read_u32(r, 0xfc) != 0, v.in_group = read_u32(r, 0x100) != 0;
    v.field_106 = r[0x106], v.field_107 = r[0x107], v.field_116 = r[0x116], v.field_118 = f32_at(u, 0x118);
    v.field_141 = r[0x141], v.field_142 = read_u16(r, 0x142), v.field_144 = r[0x144], v.field_145 = r[0x145];
    v.field_154 = r[0x154];
    v.group_index = read_u16(r, 0x104);  // groups (chunk 9) are not loaded
    v.has_future_transporter = read_u32(r, 0x14c) != 0, v.in_transport = u.has_transporter;
    v.field_160 = 0;  // a saved pending command node is not re-linked (see GameState_FromSave)
    v.field_174 = read_u32(r, 0x174), v.field_178 = r[0x178];
    v.mg_delay = r[0x168];


    v.field_139 = r[0x139], v.field_11c = read_u32(r, 0x11c), v.field_120 = r[0x120];
    v.field_16c = f32_at(u, 0x16c), v.field_170 = f32_at(u, 0x170);
    // Not understood, kept as saved.
    v.field_018 = r[0x18], v.field_058 = f32_at(u, 0x58), v.field_05c = f32_at(u, 0x5c), v.field_06d = r[0x6d];
    v.field_0ea = r[0xea];
    return v;
}


std::int32_t ftol(float v) {
    if (!(v > -2147483904.0f && v < 2147483648.0f)) return INT32_MIN;
    return static_cast<std::int32_t>(v);
}

}  // namespace



std::optional<GameState> GameState_FromSave(const SvgSave& save, const GameData& data, std::string* error) {
    if (save.players.size() != save.num_players || save.houses.size() != save.num_players ||
        save.units.size() != save.num_players) {
        *error = "save record lists do not match its player count";
        return std::nullopt;
    }
    GameState s;
    s.speed_ms = data.speed_ms;
    s.short_anims = data.short_anims;
    s.unit_types = data.unit_types;
    s.house_types = data.house_types;

    for (std::size_t n = 0; n < save.unit_stats.size(); ++n) {
        for (std::size_t t = 0; t < save.unit_stats[n].size() && t < s.unit_types[n].size(); ++t) {
            s.unit_types[n][t].max_hp = save.unit_stats[n][t].max_hp;
            s.unit_types[n][t].reload = save.unit_stats[n][t].reload;
            s.unit_types[n][t].range = save.unit_stats[n][t].range;
            s.unit_types[n][t].accuracy = save.unit_stats[n][t].accuracy;
            s.unit_types[n][t].attack.assign(save.unit_stats[n][t].attack.begin(), save.unit_stats[n][t].attack.end());
            s.unit_types[n][t].defense.assign(save.unit_stats[n][t].defense.begin(),
                                              save.unit_stats[n][t].defense.end());
        }
    }
    s.tick = save.tick;
    s.my_player = save.my_player;
    s.map_width = save.map_width, s.map_height = save.map_height;
    const std::size_t cells = static_cast<std::size_t>(save.map_width) * save.map_height;
    if (save.heights.size() != cells || save.objects.size() != cells || save.flags.size() != cells) {
        *error = "save map arrays do not match the map size";
        return std::nullopt;
    }
    s.map.width = save.map_width, s.map.height = save.map_height;
    s.map.heights = save.heights, s.map.objects = save.objects, s.map.flags = save.flags;

    if (save.cell_data.size() != cells * 22) {
        *error = "save tile layer has the wrong size";
        return std::nullopt;
    }
    s.map.terrain.resize(cells);
    for (std::size_t i = 0; i < cells; ++i) {
        const std::uint8_t* r = &save.cell_data[i * 22];
        TerrainCell& c = s.map.terrain[i];
        c.tile = static_cast<std::uint16_t>(r[0] | r[1] << 8), c.shade = r[2], c.unused = r[3];
        for (std::size_t k = 0; k < 9; ++k) c.sub[k] = static_cast<std::uint16_t>(r[4 + 2 * k] | r[5 + 2 * k] << 8);
    }
    s.map.cell_units.assign(cells, {});
    s.map.wave.assign(cells, 0);
    if (save.obstacle_grid.size() != cells * 2 || save.house_basic.size() != 150 * 0x2b0) {
        *error = "save obstacle grid or house table has the wrong size";
        return std::nullopt;
    }
    s.map.obstacle = save.obstacle_grid;
    s.object_blocking = data.object_blocking;

    for (std::size_t k = 0; k < 150; ++k) {
        s.house_footprints[k / 25][k % 25] =
            HouseFootprint{save.house_basic[k * 0x2b0 + 0x136], save.house_basic[k * 0x2b0 + 0x137]};

        for (std::size_t i = 0; i < 10; ++i) {
            s.house_types[k / 25][k % 25].produce[i] = read_u16(save.house_basic, k * 0x2b0 + 0x14e + 2 * i);
        }

        HouseType& ht = s.house_types[k / 25][k % 25];
        ht.field_190 = read_u32(save.house_basic, k * 0x2b0 + 0x190);  // one of the fields LoadGame copies


        ht.field_2ac = save.house_basic[k * 0x2b0 + 0x2ac];  // the type's level
        for (std::size_t i = 0; i < 5; ++i) {
            ht.cost[i] = std::bit_cast<float>(read_u32(save.house_basic, k * 0x2b0 + 0x164 + 4 * i));
        }
        ht.prereq.clear();
        for (std::size_t i = 0x298; i + 2 <= 0x2b0; i += 2) {
            const std::uint16_t v = read_u16(save.house_basic, k * 0x2b0 + i);
            if (v == 0xffff) break;
            ht.prereq.push_back(v);
        }
    }

    s.rng_seed = save.rng_seed;
    RNG_FillTable(s);
    s.rng_index = save.rng_index;
    s.decaying_fields = save.decaying_fields;
    for (std::size_t i = 0; i < s.reveal.size(); ++i) {
        s.reveal[i] = GameState::Reveal{save.reveal[i].x, save.reveal[i].y, save.reveal[i].radius};
    }
    s.port_sites = save.port_sites;
    s.climate = save.climate;
    s.fog_553ae0 = save.fog_553ae0;  // ships (milestone 3 slice H)
    s.flag_8ab825 = save.flag_8ab825, s.flag_8ab824 = save.flag_8ab824, s.flag_8ef4d4 = save.flag_8ef4d4;
    s.flag_8ef4d5 = save.flag_8ef4d5, s.flag_8ef4d6 = save.flag_8ef4d6;
    s.flag_8e836e = save.flag_8e836e, s.skirmish_flag = save.skirmish_flag;
    s.land_map = static_cast<std::uint8_t>(save.field_553aec), s.flag_9b2df9 = save.flag_9b2df9;
    s.sel_8ab774 = save.sel_8ab774, s.sel_8ab770 = save.sel_8ab770, s.formation_budget = save.formation_budget;
    s.map_name = save.map_name, s.slot_count = save.slot_count;
    s.mission_path = save.mission_path;
    for (std::size_t i = 0; i < s.slots.size(); ++i) {
        const auto& r = save.slots[i];
        GameState::SlotRecord& v = s.slots[i];
        for (std::size_t k = 0; k + 1 < 0x54; k += 2) {
            const auto ch = static_cast<char16_t>(r[k] | r[k + 1] << 8);
            if (ch == 0) break;
            v.name.push_back(ch);
        }
        v.nation = r[0x54], v.colour = r[0x55], v.team = r[0x56], v.level = r[0x5c];
    }

    for (std::size_t p = 0; p < save.num_players; ++p) {
        const SvgPlayer& sp = save.players[p];
        Player pl;
        pl.nation = sp.nation, pl.ai_type = sp.ai_type, pl.control_type = sp.control_type;
        pl.is_lost = sp.is_lost;
        pl.units_alive = sp.units_alive, pl.units_lost = sp.units_lost, pl.units_killed = sp.units_killed;
        pl.unit_cap = sp.unit_cap;
        pl.gold = sp.gold, pl.wood = sp.wood, pl.iron = sp.iron, pl.oil = sp.oil, pl.food = sp.food;
        pl.gold_stat = sp.gold_stat, pl.wood_stat = sp.wood_stat, pl.iron_stat = sp.iron_stat;
        pl.oil_stat = sp.oil_stat, pl.food_stat = sp.food_stat;
        for (std::size_t k = 0; k < 24; ++k) pl.research_flags[k] = save.research_flags[k][p];
        for (std::size_t r = 0; r < 20; ++r) {
            pl.research_9b39f0[r] = save.research_9b39f0[p * 20 + r];
            pl.research_9b3950[r] = save.research_9b3950[p * 20 + r];
        }
        if (sp.has_script) {
            const SvgScript& sc = sp.script;
            AIScript a;
            a.want_u = sc.want_u, a.want_h = sc.want_h, a.nsteps = sc.nsteps, a.step = sc.step, a.entry = sc.entry;
            auto conv = [](const std::vector<SvgScriptEntry>& v) {
                std::vector<ScriptEntry> out;
                for (const SvgScriptEntry& e : v) out.push_back(ScriptEntry{e.kind, e.type, e.count});
                return out;
            };
            for (const auto& v : sc.main) a.main.push_back(conv(v));
            for (const auto& v : sc.list2) a.list2.push_back(conv(v));
            a.base_x = sc.base_x, a.base_y = sc.base_y, a.field_660 = sc.field_660, a.field_664 = sc.field_664;
            a.items = sc.items, a.field_704 = sc.field_704, a.field_708 = sc.field_708;
            pl.script = std::move(a);
        }
        pl.free_ids = sp.free_ids, pl.next_id = sp.next_id;
        pl.power_used = save.power_used[p], pl.power_6c4 = save.power_6c4[p];
        pl.power_6e4 = save.power_6e4[p], pl.power_made = save.power_made[p];
        pl.field_9b2d94 = save.field_9b2d94[p], pl.field_9b2d9c = save.field_9b2d9c[p];
        pl.factor_684 = save.factor_684[p], pl.factor_664 = save.factor_664[p], pl.factor_644 = save.factor_644[p];
        pl.factor_744 = save.factor_744[p], pl.factor_724 = save.factor_724[p];
        pl.factor_9e28fc = save.factor_9e28fc[p], pl.factor_9b32d8 = save.factor_9b32d8[p];
        pl.mint_done = save.mint_done[p];
        pl.houses_lost = sp.houses_lost, pl.houses_destroyed = sp.houses_destroyed, pl.houses_built = sp.houses_built;
        pl.start_res = sp.start_res, pl.mined = sp.mined, pl.score = sp.score, pl.play_time = sp.play_time;
        pl.house_array_freed = sp.house_array_freed, pl.name = sp.name;
        pl.team = sp.team;

        {
            const std::uint32_t argb = save.colour[p];
            const std::uint32_t bgr = (argb & 0xff) << 16 | (argb & 0xff00) | (argb >> 16 & 0xff);
            pl.colour = static_cast<std::uint8_t>(p & 7);
            for (std::size_t c = 0; c < save.choosable_colour.size(); ++c) {
                if (save.choosable_colour[c] == bgr) {
                    pl.colour = static_cast<std::uint8_t>(c);
                    break;
                }
            }
        }
        // The history samples (HistFood, HistWood, HistOil, HistIron, HistGold as saved) and HistTime. HistoryIndex
        // is not saved (0 after the load, docs/save-audit.md 7 Q7), so the AI reads none of these before new samples
        // overwrite them.
        for (std::size_t i = 0; i < pl.history.size(); ++i) {
            HistSample& h = pl.history[i];
            auto at = [&sp, i](std::size_t k) { return i < sp.hist[k].size() ? sp.hist[k][i] : 0.0; };
            h.food = at(0), h.wood = at(1), h.oil = at(2), h.iron = at(3), h.gold = at(4);
            if (i < sp.hist_time.size()) pl.hist_time[i] = sp.hist_time[i];
        }
        pl.sel_class6 = sp.sel_class6, pl.sel_class7 = sp.sel_class7, pl.sel_houses = sp.sel_houses;
        pl.sel_house = sp.sel_house, pl.num_selected = sp.num_selected;
        for (std::size_t k = 0; k < pl.control_groups.size(); ++k) {
            pl.control_groups[k].kind = sp.control_groups[k].kind;
            pl.control_groups[k].ids = sp.control_groups[k].ids;
        }
        for (std::size_t r = 0; r < 30; ++r) {
            pl.research_9b3860[r] = save.research_9b3860[p * 30 + r];
            pl.research_9b3770[r] = save.research_9b3770[p * 30 + r];
        }
        pl.special_shells = save.special_shells[p];
        if (p < save.factor_9b33b8.size()) pl.factor_9b33b8 = save.factor_9b33b8[p];
        if (p < save.factor_9b3318.size()) pl.factor_9b3318 = save.factor_9b3318[p];
        pl.field_20099 = sp.field_20099, pl.field_20170 = sp.field_20170, pl.field_2cc9a = sp.field_2cc9a;
        pl.blocks = sp.blocks;  // the mission event blocks (Blob)
        pl.res_sold = save.res_sold[p], pl.res_bought = save.res_bought[p], pl.stat_colour = save.stat_colour[p];
        if (p < save.factor_9b3338.size()) pl.factor_9b3338 = save.factor_9b3338[p];
        if (p < save.accuracy_factor.size()) pl.accuracy_factor = save.accuracy_factor[p];
        if (p < save.shell_spread.size()) pl.shell_spread = save.shell_spread[p];
        pl.zone_grid = save.players[p].zone_grid, pl.zone_width = save.players[p].zone_width;
        pl.is_ally = save.players[p].is_ally;

        for (const SvgChunk& ch : save.chunks) {
            if (ch.tag == 7 && ch.data.size() >= 0xa0) {
                pl.field_9b2b38 = ch.data[p * 0x14 + 0];
                pl.field_9b2b39 = ch.data[p * 0x14 + 1];
                pl.field_9b2b3a = ch.data[p * 0x14 + 2];
                pl.field_9b2b3b = ch.data[p * 0x14 + 3];
                pl.field_9b2b3c = ch.data[p * 0x14 + 4];
                pl.field_9b2b3d = ch.data[p * 0x14 + 5];
            }
            if (ch.tag == 7 && ch.data.size() >= 0x140) {
                for (std::size_t b = 0; b < 4; ++b) pl.field_9b2a98[b] = ch.data[0xa0 + p * 0x14 + b];
            }
        }




        for (const SvgHouse& sh : save.houses[p]) {
            House h;
            h.type = sh.type, h.map_x = sh.map_x, h.map_y = sh.map_y, h.is_destroyed = sh.is_destroyed;
            h.hp = sh.hp;
            if (sh.map_x < 0 || sh.map_y < 0 || sh.map_x >= save.map_width || sh.map_y >= save.map_height) {
                h.hp = -100000.0f;
            }
            const bool live = h.hp > -10000.0f;
            if (live) {
                const ByteSpan r = sh.raw;
                auto f = [&r](std::size_t pos) { return std::bit_cast<float>(read_u32(r, pos)); };
                h.num = sh.num, h.queue_count = sh.queue_count, h.queue = sh.queue;
                h.x = f(0x10), h.y = f(0x14), h.exit_x = f(0x18), h.exit_y = f(0x1c), h.field_020 = sh.field_020;
                h.units_inside = read_u16(r, 0x2a), h.rally_x = f(0x34), h.rally_y = f(0x38), h.field_044 = f(0x44);
                h.field_05c = read_u16(r, 0x5c), h.field_060 = f(0x60), h.field_064 = r[0x64], h.field_065 = r[0x65];
                h.capacity = read_u16(r, 0x66), h.field_06a = r[0x6a];
                h.field_06c = {};
                h.field_070 = f(0x70), h.field_030 = r[0x30];
                h.field_074 = r[0x74], h.field_078 = f(0x78), h.field_07c = f(0x7c), h.field_080 = f(0x80);
                h.field_084 = r[0x84], h.field_08c = r[0x8c];
                h.max_hp = read_u16(r, 0x68), h.field_0b4 = read_u32(r, 0xb4), h.has_field_0b0 = read_u32(r, 0xb0) != 0;
                h.field_0b8 = r[0xb8], h.selected = r[0x48];
                h.field_0ac = read_u32(r, 0xac);  // a port's water side (ships)
                h.field_0a8 = static_cast<std::int32_t>(read_u32(r, 0xa8));
                h.field_04a = r[0x4a], h.field_08f = r[0x8f], h.field_090 = r[0x90], h.field_0b9 = r[0xb9];
                h.upgrade_slots = sh.upgrade_slots;
            }





            if (sh.field_020 == 0 && sh.is_destroyed == 0 && live && (sh.type == 6 || sh.type == 9)) {
                pl.unit_cap = UnitCap_Add(s, pl.unit_cap, static_cast<std::uint16_t>(ftol(sh.capacity)));
            }
            if (sh.field_020 == 0 && sh.is_destroyed == 0 && sh.type < 25) ++pl.houses_done[sh.type];


            if (sh.field_020 == 0 && sh.is_destroyed == 0 && sh.type == 14) ++pl.mint_count;


            const HouseType& ht = s.house_types[pl.nation % 6][sh.type % 25];
            if (sh.field_020 == 0) {
                if (sh.is_destroyed == 0) {
                    House_SetGraphic(h, 0, 0);
                } else {
                    House_SetGraphic(h, 2, ht.field_1b0 >= 2 && sh.hp < -500.0f ? 1 : 0);
                }
            } else {
                House_SetGraphic(h, 1, House_BuildStage(ht, sh.field_020));
            }
            pl.houses.push_back(std::move(h));
        }
        s.players.push_back(std::move(pl));
    }





    {
        const std::uint16_t index = s.rng_index;
        for (std::size_t p = 0; p < s.players.size(); ++p) {
            for (const House& h : s.players[p].houses) {
                if (h.field_020 == 0 && h.is_destroyed == 0 && h.type == 15 && h.hp > -10000.0f) {
                    House_PlantFields(s, static_cast<std::uint8_t>(p), h);
                }
            }
        }
        s.rng_index = index;
    }




    for (std::size_t p = 0; p < save.num_players; ++p) {
        for (std::size_t k = 0; k < s.players[p].houses.size(); ++k) {
            const House& h = s.players[p].houses[k];
            if (h.hp <= -10000.0f) continue;
            House_PlaceOnMap(s, static_cast<std::uint8_t>(p), static_cast<std::uint16_t>(k), s.players[p].nation,
                             h.type, h.map_x, h.map_y);
        }
    }
    PathGrid_Build(s);
    for (const SvgNeutralHouse& h : save.neutral) NeutralHouse_Place(s, h.type, h.map_x, h.map_y);
    std::vector<SpecialObject> specials;
    for (const SvgSpecial& r : save.specials) specials.push_back(SpecialObject{r.x, r.y, r.kind, r.b, r.f_a, r.f_b});
    SpecialObjects_Load(s, save.special_capacity, specials);



    const std::size_t np = save.num_players;
    for (std::size_t p = 0; p < np; ++p) {
        for (const SvgUnit& su : save.units[p]) {
            Unit u = unit_from_save(su);


            if (read_u32(su.raw, 0x164) != 0) Unported(s, orig::kLoadGameLinks);
            if (su.attack.player == 0xff || su.is_deleted == 1 || su.attack.player >= np) u.player_unit_attack = 0xff;
            if (su.has_house) {
                const SvgLink& l = su.house;
                if (l.player < np && l.id <= save.houses[l.player].size()) {
                    // The original also accepts slot == NumHouse (one past the end); no slot is written then.
                    if (l.id < s.players[l.player].houses.size()) {
                        s.players[l.player].houses[l.id].num = l.id;
                    }
                    u.house_player = l.player, u.house_slot = l.id;
                } else {
                    u.in_house = false;
                    if (u.work == 3) u.work = 0xff;
                }
            }
            const SvgLink& ha = su.house_attack;
            if (ha.player == 0xff || ha.player >= np || ha.id >= save.houses[ha.player].size()) {
                u.player_house_attack = 0xff;
            } else {
                u.house_attack_player = ha.player, u.house_attack_slot = ha.id;
            }
            if (su.has_transporter && su.transporter.player >= np) u.in_transport = false;
            u.transporter = UnitRef{}, u.future_transporter = UnitRef{};  // resolved once all units exist
            if (u.is_deleted == 0 && u.type < 40) ++s.players[p].unit_count[u.type];
            Unit_Add(s, static_cast<std::uint8_t>(p), u, false);
        }
    }



    for (std::size_t p = 0; p < np; ++p) {
        for (const std::uint32_t k : Player_UnitSlots(s.players[p])) {
            Unit& u = s.players[p].unit_slots[k];
            if (u.player_unit_attack == 0xff) continue;
            const Unit* t =
                u.player_unit_attack < np ? Unit_FindById(s, u.player_unit_attack, u.attack_unit_id) : nullptr;
            if (t == nullptr) {
                u.has_attack_unit = false;
            } else if (u.has_attack_unit) {
                u.attack_ref = Unit_Ref(u.player_unit_attack, *t);
            }
        }
    }

    for (std::size_t p = 0; p < np; ++p) {
        const std::vector<std::uint32_t> slots = Player_UnitSlots(s.players[p]);
        for (std::size_t i = 0; i < slots.size(); ++i) {
            for (const SvgLink& l : save.units[p][i].passengers) {
                const Unit* t = l.player < np ? Unit_FindById(s, l.player, l.id) : nullptr;
                if (t != nullptr) s.players[p].unit_slots[slots[i]].passengers.push_back(Unit_Ref(l.player, *t));
            }
        }
    }

    for (std::size_t p = 0; p < np; ++p) {
        const std::vector<std::uint32_t> slots = Player_UnitSlots(s.players[p]);
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const SvgUnit& su = save.units[p][i];
            Unit& u = s.players[p].unit_slots[slots[i]];
            auto resolve = [&s, np](const SvgLink& l, bool set) {
                const Unit* t = set && l.player < np ? Unit_FindById(s, l.player, l.id) : nullptr;
                return t != nullptr ? Unit_Ref(l.player, *t) : UnitRef{};
            };
            u.transporter = resolve(su.transporter, su.has_transporter);
            u.future_transporter = resolve(su.future_transporter, read_u32(su.raw, 0x14c) != 0);
        }
    }

    Grid_Init(s);
    // The houses' UnitsInside lists (chunk 3 "Houses" part, resolved by GetUnitById once the units exist).
    for (std::size_t p = 0; p < np; ++p) {
        for (std::size_t k = 0; k < s.players[p].houses.size(); ++k) {
            House& h = s.players[p].houses[k];
            if (h.hp <= -10000.0f) continue;
            const SvgLink& a = save.houses[p][k].attack;
            if (a.player < np) {
                if (const Unit* t = Unit_FindById(s, a.player, a.id)) h.field_088 = Unit_Ref(a.player, *t);
            }
            const SvgLink& sl = save.houses[p][k].ship;
            if (h.has_field_0b0 && sl.player < np) {
                if (const Unit* t = Unit_FindById(s, sl.player, sl.id)) h.field_0b0 = Unit_Ref(sl.player, *t);
            }
            for (const SvgLink& l : save.houses[p][k].inside) {
                Unit* u = l.player < np ? Unit_FindById(s, l.player, l.id) : nullptr;
                h.inside.push_back(u != nullptr ? Unit_Ref(l.player, *u) : UnitRef{});
            }
        }
    }



    for (std::size_t p = 0; p < np; ++p) {
        const SvgPlayer& sp = save.players[p];
        Player& pl = s.players[p];
        pl.ai_next_serial = sp.ai_next_serial, pl.ai_counter = sp.ai_counter;
        pl.ai_kind = sp.ai_kind, pl.ai_level = sp.ai_level;
        for (const SvgGroup& sg : sp.groups) {
            const ByteSpan r = sg.raw;
            auto f = [&r](std::size_t pos) { return std::bit_cast<float>(read_u32(r, pos)); };
            Group g;
            g.serial = pl.next_group++;
            g.player = static_cast<std::uint8_t>(p);
            g.capacity = read_u16(r, 0x1e), g.live = read_u16(r, 0x20), g.quorum = read_u16(r, 0x22);
            g.type = r[0x24], g.spacing = r[0x25], g.cols = r[0x26], g.leader = read_u16(r, 0x28);
            g.keep_spacing = r[0x2a], g.target_fx = f(0x2c), g.target_fy = f(0x30), g.slow = r[0x34];
            g.arrange_orient = r[0x35], g.spacing_override = f(0x38), g.wait = read_u16(r, 0x3c);
            g.queued = r[0x3e], g.selected = r[0x44], g.ai_owned = r[0x45], g.field_048 = 0;
            g.large = r[0x4c], g.retries = r[0x4d], g.blocked = read_u16(r, 0x4e), g.has_mc7 = r[0x50];
            g.has_mc10 = r[0x51], g.arg3 = r[0x52], g.kind = r[0x54], g.target_player = r[0x55];
            g.target_id = read_u16(r, 0x56), g.tx = static_cast<std::int32_t>(read_u32(r, 0x58));
            g.ty = static_cast<std::int32_t>(read_u32(r, 0x5c)), g.stepping = r[0x64], g.half = read_u16(r, 0x66);
            g.dead = r[0x68], g.field_069 = r[0x69], g.speed = f(0x6c), g.speed_factor = f(0x70), g.orient = r[0x74];
            for (std::size_t k = 0; k + 16 <= sg.orders.size(); k += 16) {
                std::array<std::uint8_t, 16> o{};
                for (std::size_t i = 0; i < 16; ++i) o[i] = sg.orders[k + i];
                g.follow_up.push_back(o);
            }
            g.slots = sg.slots, g.field_0c = sg.field_0c;
            if (g.slots.size() < g.capacity) g.slots.resize(g.capacity, {0, 0});
            g.wp_count = sg.wp_count, g.wp_next = sg.wp_next, g.wp = sg.wp, g.wp_orient = sg.wp_orient;
            g.members.assign(sg.member_ids.size(), UnitRef{});
            for (std::size_t k = 0; k < sg.member_ids.size(); ++k) {
                if (sg.member_ids[k] == 0xffff) continue;
                Unit* u = Unit_FindById(s, static_cast<std::uint8_t>(p), sg.member_ids[k]);
                if (u == nullptr) continue;  // the original would crash on a null unit here
                g.members[k] = Unit_Ref(static_cast<std::uint8_t>(p), *u);
                u->in_group = true;
                u->group = g.serial;
            }
            pl.groups.push_back(std::move(g));
        }
    }




    for (std::size_t p = 0; p < np; ++p) {
        const SvgPlayer& sp = save.players[p];
        for (const SvgCommand& sc : sp.commands) {
            const ByteSpan r = sc.raw;
            CommandNode n;
            n.seq = s.next_node_seq++;
            n.cls = read_u16(r, 0x0a), n.cmd = r[0x22];
            n.x = std::bit_cast<float>(read_u32(r, 0x0c)), n.y = std::bit_cast<float>(read_u32(r, 0x10));
            n.a2c = read_u32(r, 0x2c);
            if (r[0x1c] < np) {
                if (const Unit* t = Unit_FindById(s, r[0x1c], read_u16(r, 0x1e))) n.unit = Unit_Ref(r[0x1c], *t);
            }
            if (n.cmd == 0x2f) Unported(s, orig::kFormationLoad);  // the deferred order's record is not in the save
            if (sc.has_record) {
                const ByteSpan rec = sc.record;
                const std::uint8_t gp = sc.group_player;
                if (gp < np) {
                    for (std::size_t k = 0; k < save.players[gp].groups.size(); ++k) {
                        if (read_u32(save.players[gp].groups[k].raw, 0x60) == sc.group_id) {
                            n.group = s.players[gp].groups[k].serial;
                            break;
                        }
                    }
                }
                for (const SvgLink& l : sc.list) {
                    const Unit* t = l.player < np ? Unit_FindById(s, l.player, l.id) : nullptr;
                    n.list.push_back(t != nullptr ? Unit_Ref(l.player, *t) : UnitRef{});
                }
                n.a3 = rec[0xa], n.type = rec[0xb], n.cols = rec[0xc], n.kind = rec[0xd];
                n.px = static_cast<std::int32_t>(read_u32(rec, 0x10)),
                n.py = static_cast<std::int32_t>(read_u32(rec, 0x14));
                n.tplayer = rec[0x18], n.tid = read_u16(rec, 0x1a);
            }
            s.players[p].commands.push_back(std::move(n));
        }
    }


    for (const SvgChunk& ch : save.chunks) {
        if (ch.tag != 9) continue;
        Cursor c{ch.data};
        for (std::size_t p = 0; p < np && c.ok; ++p) {
            Player& pl = s.players[p];
            if (c.u8() != 0) {
                pl.ai_all = c.u8();
                pl.ai_countdown = c.i32();
            }
            if (c.u8() != 0) {
                if (pl.is_lost == 0 && pl.script) {
                    pl.script->field_6f4 = c.i32(), pl.script->field_6f8 = c.i32(), pl.script->field_6fc = c.i32();
                    c.skip(1000);
                } else {
                    c.skip(12);
                }
            }
        }
        if (!c.ok) {
            *error = "chunk 9 is truncated";
            return std::nullopt;
        }
    }



    for (std::size_t p = 0; p < np; ++p) {
        if (save.players[p].is_lost == 0) s.next_serial += 1 + static_cast<std::uint32_t>(save.units[p].size());
    }


    for (std::size_t p = 0; p < np; ++p) {
        for (const std::uint32_t k : Player_UnitSlots(s.players[p])) {
            Unit& u = s.players[p].unit_slots[k];
            const Cell c = WorldToCell(u.x, u.y);
            const auto pl = static_cast<std::uint8_t>(p);
            TUnit_SetCell(s, pl, u, c);
            TUnit_UpdateGroundHeight(s, pl, u);
        }
    }






    if (data.short_anims != 0) {
        std::map<std::uint16_t, std::uint32_t>& refs = s.graphic_refs;
        auto ref = [&s, &refs](std::uint8_t nation, std::uint16_t type) {
            if (nation >= s.unit_types.size() || type >= 40) return std::uint32_t{0};
            UnitType& t = s.unit_types[nation][type];
            const std::uint32_t n = ++refs[t.graphic];
            if (n == 1) t.anim_size[4] = 0;
            return n;
        };
        for (std::size_t p = 0; p < np; ++p) {
            for (const SvgHouse& h : save.houses[p]) {
                if (read_u16(h.raw, 0x36) == 0x18 && read_u16(h.raw, 0x34) != 0) ref(s.players[p].nation, 0x17);
            }
        }
        for (std::size_t p = 0; p < np; ++p) {
            for (const SvgUnit& u : save.units[p]) {
                if (u.type == 0) ref(s.players[p].nation, 1);
            }
        }
        for (std::size_t p = 0; p < np; ++p) {
            const std::uint8_t nation = s.players[p].nation;
            for (const SvgUnit& u : save.units[p]) {
                if (ref(nation, u.type) == 1 && s.unit_types[nation][u.type].move_class == 6 && u.type != 1)
                    ref(nation, 1);
            }
        }
    }



    for (const SvgBullet& r : save.bullet_records) {
        Bullet v;
        const ByteSpan b{r.raw.data(), r.raw.size()};
        v.x = std::bit_cast<float>(read_u32(b, 0)), v.y = std::bit_cast<float>(read_u32(b, 4));
        v.z = std::bit_cast<float>(read_u32(b, 8));
        v.field_00c = read_u16(b, 0xc), v.delay = read_u16(b, 0xe);
        if (r.has_owner && r.owner.player < np) {
            if (const Unit* o = Unit_FindById(s, r.owner.player, r.owner.id)) v.owner = Unit_Ref(r.owner.player, *o);
        }
        s.bullets.push_back(v);
    }
    for (const SvgShell& r : save.shells) {
        Shell v;
        const ByteSpan b{r.raw.data(), r.raw.size()};
        auto f = [&b](std::size_t pos) { return std::bit_cast<float>(read_u32(b, pos)); };
        auto d = [&b](std::size_t pos) {
            return std::bit_cast<double>(static_cast<std::uint64_t>(read_u32(b, pos)) |
                                         static_cast<std::uint64_t>(read_u32(b, pos + 4)) << 32);
        };
        v.x0 = f(0xd8), v.x1 = f(0xdc), v.y0 = f(0xe0), v.y1 = f(0xe4), v.z0 = f(0xe8), v.z1 = f(0xec);
        v.cx = f(0xf0), v.cy = f(0xf4), v.cz = f(0xf8);
        v.g = d(0x20), v.vx = d(0x28), v.vy = d(0x30), v.v0 = d(0x48), v.vz = d(0x70), v.flight = d(0x60);
        v.substep = f(0x9c), v.range = f(0xa0), v.a4 = f(0xa4);
        v.fail = b[0xac], v.player = b[0xad], v.gas = b[0xb4], v.no_effect = b[0xb5];
        v.radius = static_cast<std::int32_t>(read_u32(b, 0xb8)), v.sound = b[0xbc];
        v.sound_tick = static_cast<std::int32_t>(read_u32(b, 0xc0)),
        v.damage = static_cast<std::int32_t>(read_u32(b, 0xc4));
        v.delay = read_u16(b, 0xc8), v.kind = b[0xca], v.fragments = b[0xcc];
        v.flown = static_cast<std::int32_t>(read_u32(b, 0xd4));
        if (r.owner.player < np && save.players[r.owner.player].is_lost == 0) {
            if (const Unit* o = Unit_FindById(s, r.owner.player, r.owner.id)) v.owner = Unit_Ref(r.owner.player, *o);
        }
        s.shells.insert(s.shells.begin(), v);
    }
    // The mine sites (GameState::mine_sites); the u8 count wraps as in the original.
    for (std::size_t i = 0; i < s.map.objects.size(); ++i) {
        const std::uint16_t o = s.map.objects[i];
        if (o < 0x2581 || o > 0x2583) continue;
        s.mine_sites[s.mine_site_count++] = {static_cast<std::int32_t>(i % s.map.width),
                                             static_cast<std::int32_t>(i / s.map.width)};
    }
    for (const Player& pl : s.players) {
        if (pl.is_lost != 0) continue;
        for (const House& h : pl.houses) {
            if (h.is_destroyed != 0 && h.hp < 10000.0f) continue;
            if (h.type == 0xd || h.type == 0xb || h.type == 10) s.mine_sites[s.mine_site_count++] = {h.map_x, h.map_y};
        }
    }
    CoarseWater_Init(s);



    for (std::size_t p = 0; p < np; ++p) {
        const std::vector<std::uint32_t> slots = Player_UnitSlots(s.players[p]);
        for (std::size_t i = 0; i < slots.size(); ++i) {
            Unit& u = s.players[p].unit_slots[slots[i]];
            if (Unit_Type(s, static_cast<std::uint8_t>(p), u).is_ship == 0) continue;
            const std::vector<SvgWeapon>& w = save.units[p][i].weapons;
            for (std::size_t k = 0; k < u.guns.size() && k < w.size(); ++k) {
                u.guns[k].angle = w[k].angle, u.guns[k].target = w[k].target, u.guns[k].reload = w[k].reload;
            }
            Ship_SetupSprites(s, static_cast<std::uint8_t>(p), u);
        }
    }
    Fog_Rebuild(s);
    MissionEvents_Start(s);
    return s;
}

void GameState_AdoptUnit(GameState& s, std::uint8_t player, const Unit& u) {
    Unit& v = s.players[player].unit_slots[Unit_Add(s, player, u, false)];
    v.in_cell_list = false;
    TUnit_SetCell(s, player, v, WorldToCell(v.x, v.y));
    TUnit_UpdateGroundHeight(s, player, v);
}

void Player_SetUnitOrder(Player& p, std::span<const std::uint32_t> slots) {
    p.first_unit = slots.empty() ? kNoSlot : slots[0];
    for (std::size_t i = 0; i < slots.size(); ++i) {
        Unit& u = p.unit_slots[slots[i]];
        u.prev = i == 0 ? kNoSlot : slots[i - 1];
        u.next = i + 1 == slots.size() ? kNoSlot : slots[i + 1];
    }
}

}  // namespace antanta::sim
