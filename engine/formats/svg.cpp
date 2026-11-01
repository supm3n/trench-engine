#include "svg.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

#include "map.hpp"

namespace antanta {

namespace {

// Skips count * size bytes; a product that does not fit what is left fails the cursor.
void skip_n(Cursor& c, std::uint64_t count, std::uint64_t size) {
    const std::uint64_t n = count * size;
    if (!c.ok || n > c.data.size() - c.pos) {
        c.ok = false;
        return;
    }
    c.skip(static_cast<std::size_t>(n));
}

bool tag_is(Cursor& c, const char* tag, std::size_t n) {
    ByteSpan s = c.take(n);
    return c.ok && std::memcmp(s.data(), tag, n) == 0;
}

float f32_at(ByteSpan b, std::size_t pos) { return std::bit_cast<float>(read_u32(b, pos)); }


SvgPlayer read_player(Cursor& c) {
    SvgPlayer p;
    const std::uint16_t zone_grid_size = c.u16();
    p.zone_width = c.u16();
    for (int i = 0; i < zone_grid_size && c.ok; ++i) p.zone_grid.push_back(c.u8());
    p.sel_class6 = c.u8(), p.sel_class7 = c.u8();   // SelHasClass6, SelHasClass7
    p.sel_houses = c.u16(), p.sel_house = c.u16();  // NumSelectedHouses, g_wNumSelectedHouse
    p.nation = c.u8();
    p.field_20099 = c.u8();
    p.ai_type = c.u8();
    {
        const std::uint32_t n = c.u32();  // name length, UTF-16 name without its NUL
        if (n > (c.data.size() - c.pos) / 2) c.ok = false;
        for (std::uint32_t i = 0; i < n && c.ok; ++i) p.name.push_back(static_cast<char16_t>(c.u16()));
    }
    p.units_alive = c.i32();
    p.units_lost = c.i32();
    p.units_killed = c.i32();
    auto f64 = [&c] { return std::bit_cast<double>(c.u64()); };
    p.food_stat = f64(), p.gold_stat = f64(), p.iron_stat = f64(), p.oil_stat = f64(), p.wood_stat = f64();
    p.gold = f64(), p.wood = f64(), p.iron = f64(), p.food = f64(), p.oil = f64();
    if (5 * 8000 + 4000 > c.data.size() - c.pos) c.ok = false;
    for (auto& h : p.hist) {  // HistFood, HistWood, HistOil, HistIron, HistGold (f64[1000])
        for (int i = 0; i < 1000 && c.ok; ++i) h.push_back(f64());
    }
    for (int i = 0; i < 1000 && c.ok; ++i) p.hist_time.push_back(c.u32());  // HistTime
    p.score = std::bit_cast<float>(c.u32());
    p.field_20170 = c.u8();
    p.unit_cap = c.u16();
    p.field_2cc9a = c.u8();
    {
        const std::uint8_t n = c.u8();  // BlobSize, Blob: the mission event blocks
        for (std::uint8_t i = 0; i < n && c.ok; ++i) {
            const ByteSpan b = c.take(kMisBlockSize);
            if (c.ok) p.blocks.push_back(parse_mis_block(b));
        }
    }
    p.control_type = c.u8();
    p.player_index = c.u8();
    p.is_lost = c.u8();
    p.team = static_cast<std::int8_t>(c.u8());  // Team (a char), then 3 bytes of padding
    c.skip(3);
    p.num_selected = c.u16();  // NumSelectedUnits
    if (p.is_lost == 0) {

        SvgScript& sc = p.script;
        p.has_script = true;
        for (auto& v : sc.want_u) v = c.u16();
        for (auto& v : sc.want_h) v = c.u16();
        sc.nsteps = c.u8();
        sc.step = c.u8();
        sc.entry = c.u16();
        auto entries = [&c] {
            std::vector<SvgScriptEntry> out;
            const int m = c.u16();
            for (int j = 0; j < m && c.ok; ++j) {
                SvgScriptEntry e;
                e.kind = c.u8();
                c.u8();  // unused
                e.type = c.u16();
                e.count = c.u16();
                out.push_back(e);
            }
            return out;
        };
        for (int i = 0; i < sc.nsteps && c.ok; ++i) {
            sc.main.push_back(entries());
            sc.list2.push_back(entries());
        }
        sc.base_x = c.i32(), sc.base_y = c.i32(), sc.field_660 = c.i32(), sc.field_664 = c.i32();
        const std::uint32_t items = c.u32();
        for (std::uint32_t i = 0; i < items && c.ok; ++i) sc.items.push_back(c.u64());
        sc.field_704 = c.i32();
        sc.field_708 = c.i32();
        sc.field_708 = c.i32();
    }
    p.houses_lost = c.i32(), p.houses_destroyed = c.i32(), p.houses_built = c.i32();
    p.play_time = c.u32();
    for (auto& v : p.start_res) v = f64();  // Gold, Food, Wood, Iron, Oil
    for (auto& v : p.mined) v = f64();      // GoldMined, FoodMined, WoodMined, IronMined, OilMined
    for (auto& v : p.is_ally) v = c.u8();
    p.house_array_freed = c.u8();
    return p;
}

SvgHouse decode_house(ByteSpan r) {
    SvgHouse h;
    std::memcpy(h.raw.data(), r.data(), h.raw.size());
    h.num = read_u16(r, 0x04);
    h.map_x = static_cast<std::int32_t>(read_u32(r, 0x08));
    h.map_y = static_cast<std::int32_t>(read_u32(r, 0x0c));
    h.field_020 = read_u16(r, 0x20);
    h.type = read_u16(r, 0x22);
    h.hp = f32_at(r, 0x24);
    h.is_destroyed = r[0x28];
    h.queue_count = read_u16(r, 0x3c);
    h.capacity = f32_at(r, 0x70);
    return h;
}

SvgUnit decode_unit(ByteSpan r) {
    SvgUnit u;
    std::memcpy(u.raw.data(), r.data(), u.raw.size());
    u.num_work = r[0x04];
    u.is_deleted = r[0x05];
    u.x = f32_at(r, 0x1c), u.y = f32_at(r, 0x20);
    u.map_x = read_u16(r, 0x3c), u.map_y = read_u16(r, 0x3e);
    u.dest_x = f32_at(r, 0x40), u.dest_y = f32_at(r, 0x44);
    u.num_sprite = f32_at(r, 0x68);
    u.orient = r[0x6c];
    u.angle = f32_at(r, 0x74);
    u.player = r[0x7a];
    u.serial = read_u32(r, 0x80);
    u.type = read_u16(r, 0x88);
    u.id = read_u16(r, 0x90);
    u.work = r[0xc8];
    u.player_unit_attack = r[0xcc];
    u.attack_unit_id = read_u16(r, 0xce);
    u.hp = f32_at(r, 0xdc);
    u.has_house = read_u32(r, 0xf4) != 0;
    u.player_house_attack = r[0xf9];
    u.has_transporter = read_u32(r, 0x150) != 0;
    return u;
}


void read_player_units(Cursor& c, int count, SvgPlayer& player, std::vector<SvgUnit>& out) {
    const std::uint16_t n_free = c.u16();  // FreeIdCount, FreeIds
    for (int i = 0; i < n_free && c.ok; ++i) player.free_ids.push_back(c.u16());
    player.next_id = c.u16();
    for (int i = 0; i < count && c.ok; ++i) {
        ByteSpan r = c.take(0x17c);
        if (!c.ok) return;
        SvgUnit u = decode_unit(r);
        ByteSpan path = c.take(read_u16(r, 0xc4));
        u.path.assign(path.begin(), path.end());


        for (int w = 0; w < r[0x114] && c.ok; ++w) {
            SvgWeapon v;
            v.index = c.u8(), v.barrels = c.u8(), v.flash = c.u32();
            v.angle = c.f32(), v.target = c.f32();
            v.unit_player = c.u8(), v.unit_id = c.u16();
            if (v.unit_player != 0xff) u.weapon_target = true;
            v.house_player = c.u8();
            if (v.house_player != 0xff) v.house_slot = c.u16(), u.weapon_target = true;
            v.reload = c.u16(), v.field_38 = c.u32();
            u.weapons.push_back(v);
        }
        out.push_back(std::move(u));
    }
    std::array<std::uint16_t, 10> group_size{};
    for (auto& g : group_size) g = c.u16();
    for (std::size_t k = 0; k < group_size.size() && c.ok; ++k) {  // the control groups: u8 kind, then the ids
        if (group_size[k] == 0) continue;
        player.control_groups[k].kind = c.u8();
        for (int i = 0; i < group_size[k] && c.ok; ++i) player.control_groups[k].ids.push_back(c.u16());
    }
    const std::uint32_t n_cmds = c.u32();
    if (n_cmds > c.data.size()) {
        c.ok = false;
        return;
    }
    for (std::uint32_t i = 0; i < n_cmds && c.ok; ++i) {
        ByteSpan node = c.take(0x5c);
        if (!c.ok) return;
        SvgCommand cmd;
        std::memcpy(cmd.raw.data(), node.data(), cmd.raw.size());
        cmd.has_record = node[0x22] == 0x2d;  // cmd 0x2d has a formation record after the AI groups
        player.commands.push_back(std::move(cmd));
    }
    player.ai_next_serial = c.u32();
    player.ai_counter = static_cast<std::int32_t>(c.u32());
    player.ai_kind = c.u8();
    player.ai_level = c.u8();
    const int n_ai = c.u16();
    for (int i = 0; i < n_ai && c.ok; ++i) {
        SvgGroup g;
        ByteSpan rec = c.take(0x80);
        if (!c.ok) return;
        std::memcpy(g.raw.data(), rec.data(), g.raw.size());
        g.queued_orders = c.u32();
        if (g.queued_orders > c.data.size()) {
            c.ok = false;
            return;
        }
        ByteSpan orders = c.take(static_cast<std::size_t>(g.queued_orders) * 16);
        g.orders.assign(orders.begin(), orders.end());
        const std::uint16_t n = read_u16(rec, 0x1c);
        for (int k = 0; k < n && c.ok; ++k) g.member_ids.push_back(c.u16());
        for (int k = 0; k < n && c.ok; ++k) {
            const auto x = static_cast<std::int32_t>(c.u32());
            g.slots.push_back({x, static_cast<std::int32_t>(c.u32())});
        }
        for (int k = 0; k < n && c.ok; ++k) g.field_0c.push_back(c.u8());
        g.wp_count = c.u16();
        g.wp_next = c.u16();
        for (int k = 0; k < g.wp_count && c.ok; ++k) {
            const auto x = static_cast<std::int32_t>(c.u32());
            g.wp.push_back({x, static_cast<std::int32_t>(c.u32())});
        }
        for (int k = 0; k < g.wp_count && c.ok; ++k) g.wp_orient.push_back(c.u8());
        player.groups.push_back(std::move(g));
    }
    for (SvgCommand& cmd : player.commands) {
        if (!cmd.has_record || !c.ok) continue;
        ByteSpan rec = c.take(0x1c);
        if (!c.ok) return;
        std::memcpy(cmd.record.data(), rec.data(), cmd.record.size());
        cmd.group_id = c.u32();
        cmd.group_player = c.u8();
        for (int k = read_u16(rec, 8); k != 0 && c.ok; --k) {
            SvgLink l;
            l.id = c.u16();
            l.player = c.u8();
            cmd.list.push_back(l);
        }
    }
}


bool read_game_map(ByteSpan data, const UnitTypeCounts& unit_types, SvgSave& s, std::string* error) {
    Cursor c{data};
    if (!tag_is(c, "GameMap", 8)) {
        *error = "chunk 1 does not start with \"GameMap\"";
        return false;
    }
    s.num_players = c.u16();
    s.map_width = c.u16();
    s.map_height = c.u16();
    s.view_x = c.u16();
    s.view_y = c.u16();
    if (s.num_players > 8) {
        *error = "chunk 1: " + std::to_string(s.num_players) + " players (at most 8)";
        return false;
    }
    const std::uint64_t cells = std::uint64_t{s.map_width} * s.map_height;
    if (cells * 26 > c.data.size() - c.pos) {
        *error = "chunk 1: map arrays truncated";
        return false;
    }
    const auto ncells = static_cast<std::size_t>(cells);
    ByteSpan b = c.take(ncells);
    s.heights.assign(b.begin(), b.end());
    b = c.take(ncells * 22);
    s.cell_data.assign(b.begin(), b.end());
    s.objects.resize(ncells);
    for (auto& o : s.objects) o = c.u16();
    b = c.take(ncells);
    s.flags.assign(b.begin(), b.end());

    if (cells * 4 > c.data.size() - c.pos) {
        *error = "chunk 1: obstacle grid truncated";
        return false;
    }
    s.obstacle_grid.resize(ncells * 2);
    for (auto& o : s.obstacle_grid) o = c.u16();
    const int routes = c.u16();
    for (int i = 0; i < routes && c.ok; ++i) {
        MapRoute route;
        const int n = c.u8();
        route.v = c.u16();
        for (int k = 0; k < n && c.ok; ++k) {
            std::vector<std::uint16_t> list(c.u8());
            for (std::uint16_t& v : list) v = c.u16();
            route.lists.push_back(std::move(list));
        }
        s.routes.push_back(std::move(route));
    }

    for (std::uint16_t n = c.u16(), i = 0; i < n && c.ok; ++i) {
        const ByteSpan rec = c.take(80);
        if (c.ok) s.decorations.push_back(parse_decoration(rec));
    }
    b = c.take(0x19320);  // g_HouseBasic, 150 x 0x2b0
    if (c.ok) s.house_basic.assign(b.begin(), b.end());
    for (int p = 0; p < s.num_players && c.ok; ++p) s.players.push_back(read_player(c));
    std::array<std::uint16_t, 8> num_house{};
    for (auto& n : num_house) n = c.u16();
    s.houses.resize(s.num_players);
    for (int p = 0; p < s.num_players && c.ok; ++p) {
        for (int i = 0; i < num_house[p] && c.ok; ++i) {
            ByteSpan r = c.take(0xc0);
            if (c.ok) s.houses[p].push_back(decode_house(r));
        }
    }
    for (std::size_t n = 0; n < unit_types.size(); ++n) {  // per-type upgrade tables
        for (int t = 0; t < unit_types[n] && c.ok; ++t) {
            SvgUnitTypeStats st;
            for (auto* table : {&st.attack, &st.defense}) {
                for (auto& row : *table) {
                    for (auto& v : row) v = c.u16();
                }
            }
            for (auto* table : {&st.range, &st.max_hp, &st.accuracy, &st.reload}) {
                for (auto& v : *table) v = c.u16();
            }
            s.unit_stats[n].push_back(st);
        }
    }
    for (auto& n : s.num_units) n = c.u16();
    s.units.resize(s.num_players);
    for (int p = 0; p < s.num_players && c.ok; ++p) {
        if (s.players[p].is_lost == 0) read_player_units(c, s.num_units[p], s.players[p], s.units[p]);
    }

    if (c.ok && c.pos + 1 < data.size()) {
        s.neutral_houses = c.u16();
        for (int i = 0; i < s.neutral_houses && c.ok; ++i) {
            ByteSpan r = c.take(0x84);
            if (c.ok)
                s.neutral.push_back(
                    {static_cast<std::int32_t>(read_u32(r, 0)), static_cast<std::int32_t>(read_u32(r, 4)), r[8]});
        }
        if (c.ok && c.pos + 1 < data.size()) skip_n(c, c.u16(), 0x70);
    }
    if (!c.ok || c.pos != data.size()) {
        *error = "chunk 1 is truncated or has " + std::to_string(data.size() - c.pos) +
                 " unread bytes (wrong unit type counts?)";
        return false;
    }
    return true;
}






bool read_links(ByteSpan data, SvgSave& s, std::string* error) {
    Cursor c{data};
    if (!tag_is(c, "Houses", 6)) {
        *error = "chunk 3 does not start with \"Houses\"";
        return false;
    }
    for (auto& houses : s.houses) {
        for (SvgHouse& h : houses) {
            if (h.hp > -10000.0f) {
                const int n = h.queue_count == 0xffff ? 1 : h.queue_count;
                for (int i = 0; i < n && c.ok; ++i) h.queue.push_back(c.u16());
                c.skip(10);                                               // field_051
                if (read_u32(h.raw, 0x88) != 0 && h.raw[0x84] != 0xff) {  // AttackUnit player, id
                    h.attack.player = c.u8();
                    h.attack.id = c.u16();
                }
                for (int i = 0; i < read_u16(h.raw, 0x2a) && c.ok; ++i) {  // UnitsInside
                    SvgLink l;
                    l.player = c.u8();
                    l.id = c.u16();
                    h.inside.push_back(l);
                }
                for (int i = 0; i < h.raw[0x50] && c.ok; ++i) h.upgrade_slots.push_back(c.u32());  // Slots
                if (read_u32(h.raw, 0xb0) != 0) {                                                  // a port's ship
                    h.ship.player = c.u8();
                    h.ship.id = c.u16();
                }
            }
            if (c.u8() != '#' || !c.ok) {
                *error = "chunk 3: house list out of step";
                return false;
            }
        }
    }
    if (!tag_is(c, "Units", 5)) {
        *error = "chunk 3 has no \"Units\" part";
        return false;
    }
    auto link = [&c] {
        SvgLink l;
        l.player = c.u8();
        l.id = c.u16();
        return l;
    };
    for (auto& units : s.units) {
        for (SvgUnit& u : units) {
            const ByteSpan r = u.raw;
            if (read_u32(r, 0xd0) != 0 && r[0xcc] != 0xff) u.attack = link();
            if (read_u32(r, 0xf4) != 0) u.house = link();
            if (read_u32(r, 0xfc) != 0 && r[0xf9] != 0xff) u.house_attack = link();
            if (read_u32(r, 0x14c) != 0) u.future_transporter = link();  // MyFutureTransporter
            if (read_u32(r, 0x150) != 0) u.transporter = link();
            for (std::uint16_t k = read_u16(r, 0x146); k != 0 && c.ok; --k) u.passengers.push_back(link());
            std::uint8_t b = c.u8();
            while (c.ok && b != '#') {  // field_158, weapon targets, field_164
                c.skip(2);
                b = c.u8();
            }
            if (!c.ok) {
                *error = "chunk 3: unit list truncated";
                return false;
            }
        }
    }
    if (c.pos != data.size()) {
        *error = "chunk 3 has " + std::to_string(data.size() - c.pos) + " unread bytes";
        return false;
    }
    return true;
}


bool read_game_stat(ByteSpan data, SvgSave& s, std::string* error) {
    Cursor c{data};
    if (!tag_is(c, "GameStat", 9)) {
        *error = "chunk 4 does not start with \"GameStat\"";
        return false;
    }
    s.rng_seed = c.u32();
    s.rng_index = c.u16();


    {
        ByteSpan b = c.take(0x104);
        for (std::size_t i = 0; c.ok && i < b.size() && b[i] != 0; ++i) {
            s.mission_path.push_back(static_cast<char>(b[i]));
        }
    }
    c.skip(0xa0);
    for (auto& a : s.research_flags) {
        for (auto& v : a) v = c.u8();
    }
    for (auto& v : s.research_9b39f0) v = c.u8();
    for (auto& v : s.research_9b3950) v = c.u8();
    for (auto& v : s.research_9b3860) v = c.u8();                         // Military Academy started [player][30]
    for (auto& v : s.research_9b3770) v = c.u8();                         // Military Academy done
    for (auto& v : s.shell_spread) v = std::bit_cast<float>(c.u32());
    for (auto& v : s.accuracy_factor) v = std::bit_cast<float>(c.u32());
    for (auto& v : s.factor_9b33b8) v = std::bit_cast<float>(c.u32());
    for (auto& v : s.factor_9b3338) v = std::bit_cast<float>(c.u32());
    for (auto& v : s.factor_9b3318) v = std::bit_cast<float>(c.u32());
    s.fog_553ae0 = c.u8();
    auto floats = [&c](std::array<float, 8>& out) {
        for (auto& v : out) v = std::bit_cast<float>(c.u32());
    };
    floats(s.factor_744);
    floats(s.factor_724);
    floats(s.power_made);
    floats(s.power_6e4);
    floats(s.power_6c4);
    floats(s.power_used);
    floats(s.factor_684);
    floats(s.factor_664);
    floats(s.factor_644);
    for (auto& v : s.field_9b2d9c) v = c.u8();
    for (auto& v : s.field_9b2d94) v = c.u8();


    for (auto& a : s.res_sold) {
        for (auto& v : a) v = std::bit_cast<float>(c.u32());
    }
    for (auto& a : s.res_bought) {
        for (auto& v : a) v = std::bit_cast<float>(c.u32());
    }


    s.flag_8ab825 = c.u8(), s.flag_8ab824 = c.u8();
    s.sel_8ab774 = c.u16(), s.sel_8ab770 = c.u16();
    s.flag_8ef4d4 = c.u8(), s.flag_8ef4d5 = c.u8(), s.flag_8ef4d6 = c.u8();
    s.formation_budget = c.u8();
    c.skip(1);
    s.climate = c.i32();
    s.field_553aec = c.u32();
    for (auto& v : s.special_shells) v = c.u32();
    floats(s.factor_9e28fc);
    c.skip(0x8c4 + 0x80 + 0x28 + 0x54 + 2000);  // zero blocks, the player's name, the objective text
    floats(s.factor_9b32d8);
    s.flag_9b2df9 = c.u8();
    const std::uint32_t n_sites = c.u32();
    for (std::uint32_t i = 0; i < n_sites && c.ok && c.pos + 4 <= data.size(); ++i) s.port_sites.push_back(c.u32());
    if (s.port_sites.size() != n_sites) c.ok = false;
    for (auto& v : s.mint_done) v = c.u8();
    for (auto& v : s.colour) v = c.u32();
    for (auto& v : s.minimap_colour) v = c.u16();
    for (auto& v : s.stat_colour) v = c.u32();
    for (auto& v : s.choosable_colour) v = c.u32();
    for (auto& r : s.reveal) r.x = c.i32(), r.y = c.i32();
    for (auto& r : s.reveal) r.radius = c.u8();
    const std::uint32_t n_fields = c.u32();
    for (std::uint32_t i = 0; i < n_fields && c.ok && c.pos + 4 <= data.size(); ++i)
        s.decaying_fields.push_back(c.u32());
    if (s.decaying_fields.size() != n_fields) c.ok = false;
    s.flag_8e836e = c.u8();
    s.skirmish_flag = c.u8();
    for (auto& r : s.slots) {
        ByteSpan b = c.take(r.size());
        if (c.ok) std::memcpy(r.data(), b.data(), r.size());
    }
    s.slot_count = c.u8();
    {
        ByteSpan b = c.take(0x100);  // the map file name, NUL-terminated
        for (std::size_t i = 0; c.ok && i < b.size() && b[i] != 0; ++i) s.map_name.push_back(static_cast<char>(b[i]));
    }
    s.my_player = c.u8();
    if (!c.ok || c.pos != data.size()) {
        *error = "chunk 4 (GameStat) has an unexpected size";
        return false;
    }
    return true;
}

}  // namespace

std::optional<SvgSave> parse_svg(ByteSpan file, const UnitTypeCounts& unit_types, std::string* error) {
    auto data = unpack_pack(file, error);
    if (!data) return std::nullopt;
    return parse_svg_stream(*data, unit_types, error);
}

std::optional<SvgSave> parse_svg_stream(ByteSpan data, const UnitTypeCounts& unit_types, std::string* error) {
    SvgSave s;
    Cursor c{data};

    while (c.ok && c.pos + 1 < data.size()) {
        SvgChunk ch;
        ch.tag = c.u16();
        ByteSpan body = c.take(c.u32());
        if (!c.ok) {
            *error = "chunk " + std::to_string(ch.tag) + " is truncated";
            return std::nullopt;
        }
        ch.data.assign(body.begin(), body.end());
        s.chunks.push_back(std::move(ch));
    }
    auto find = [&s](std::uint16_t tag) -> const SvgChunk* {
        for (const SvgChunk& ch : s.chunks) {
            if (ch.tag == tag) return &ch;
        }
        return nullptr;
    };
    const SvgChunk* map = find(1);
    const SvgChunk* links = find(3);
    const SvgChunk* stat = find(4);
    const SvgChunk* tick = find(8);
    if (!map || !links || !stat || !tick) {
        *error = "save lacks one of the chunks 1, 3, 4, 8";
        return std::nullopt;
    }
    if (tick->data.size() != 4) {
        *error = "chunk 8 is not 4 bytes";
        return std::nullopt;
    }
    s.tick = read_u32(tick->data, 0);

    if (const SvgChunk* sp = find(6)) {
        Cursor c6{sp->data};
        s.special_capacity = c6.u16();
        const int n = c6.u16();
        for (int i = 0; i < n && c6.ok; ++i) {
            SvgSpecial r;
            r.kind = c6.u8();
            r.x = c6.u16();
            r.y = c6.u16();
            r.f_a = c6.f32();
            r.f_b = c6.f32();
            r.b = c6.u8();
            if (c6.ok) s.specials.push_back(r);
        }
        if (!c6.ok) {
            *error = "chunk 6 (special cell objects) is truncated";
            return std::nullopt;
        }
    }


    if (const SvgChunk* sh = find(2)) {
        Cursor c2{sh->data};
        s.bullets = c2.u32();
        for (std::uint32_t i = 0; i < s.bullets && c2.ok; ++i) {
            ByteSpan r = c2.take(0x14);
            if (!c2.ok) break;
            SvgBullet v;
            std::copy(r.begin(), r.end(), v.raw.begin());
            if (read_u32(r, 0x10) != 0) v.has_owner = true, v.owner.player = c2.u8(), v.owner.id = c2.u16();
            s.bullet_records.push_back(v);
        }
        const std::uint32_t m = c2.ok ? c2.u32() : 0;
        for (std::uint32_t i = 0; i < m && c2.ok; ++i) {
            ByteSpan r = c2.take(0x110);
            if (!c2.ok) break;
            SvgShell v;
            std::copy(r.begin(), r.end(), v.raw.begin());
            if (read_u32(r, 0xb0) != 0) v.owner.player = c2.u8(), v.owner.id = c2.u16();
            s.shells.push_back(v);
        }
        if (!c2.ok || c2.pos != sh->data.size()) {
            *error = "chunk 2 (bullets and shells) has an unexpected size";
            return std::nullopt;
        }
    }
    if (!read_game_map(map->data, unit_types, s, error) || !read_links(links->data, s, error) ||
        !read_game_stat(stat->data, s, error)) {
        return std::nullopt;
    }
    return s;
}

}  // namespace antanta
