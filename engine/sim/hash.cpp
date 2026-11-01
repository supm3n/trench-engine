// GameState_Hash: FNV-1a 64 over the saved fields (state.hpp "Field classes") in a fixed order. Every value is fed
// as fixed-width little-endian bytes (floats and doubles as their raw bits, bools as one byte, lists with a u32
// count first), so the hash does not depend on the compiler, the target or struct layout. Units are taken in list
// order (slot numbers and the id table are scratch). A new saved field must be added here.
#include <bit>

#include "state.hpp"

namespace antanta::sim {

namespace {

constexpr std::uint32_t kHashVersion = 19;             // bump when the field order or encoding changes
constexpr std::uint32_t kNetHashVersion = 0x4e000002;  // GameState_NetHash's ("N", 1): bump with kHashVersion

struct Hasher {
    std::uint64_t h = 0xcbf29ce484222325ull;
    void byte(std::uint8_t b) { h = (h ^ b) * 0x100000001b3ull; }
    void u8(std::uint8_t v) { byte(v); }
    void u16(std::uint16_t v) { byte(static_cast<std::uint8_t>(v)), byte(static_cast<std::uint8_t>(v >> 8)); }
    void u32(std::uint32_t v) { u16(static_cast<std::uint16_t>(v)), u16(static_cast<std::uint16_t>(v >> 16)); }
    void u64(std::uint64_t v) { u32(static_cast<std::uint32_t>(v)), u32(static_cast<std::uint32_t>(v >> 32)); }
    void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    void f32(float v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
    void b(bool v) { u8(v ? 1 : 0); }
    void count(std::size_t n) { u32(static_cast<std::uint32_t>(n)); }
    void ref(const UnitRef& r) { u8(r.player), u16(r.id), u32(r.serial); }
    void bytes(const std::vector<std::uint8_t>& v) {
        count(v.size());
        for (std::uint8_t x : v) u8(x);
    }
    void words(const std::vector<std::uint16_t>& v) {
        count(v.size());
        for (std::uint16_t x : v) u16(x);
    }
};

void unit(Hasher& h, const Unit& u) {
    h.u8(u.num_work), h.u8(u.is_deleted), h.u8(u.field_006), h.u8(u.frame_slot), h.f32(u.field_024);
    h.f32(u.x), h.f32(u.y), h.f32(u.z), h.f32(u.vel_x), h.f32(u.vel_y), h.u16(u.map_x), h.u16(u.map_y);
    h.f32(u.dest_x), h.f32(u.dest_y), h.f32(u.final_x), h.f32(u.final_y), h.i32(u.field_060), h.i32(u.field_064);
    h.f32(u.num_sprite), h.u8(u.orient), h.u8(u.target_orient), h.f32(u.angle), h.u8(u.field_078);
    h.u16(u.field_07c), h.u32(u.serial), h.u8(u.field_086), h.u16(u.type), h.u16(u.id), h.u8(u.field_093);
    h.u8(u.field_09d), h.u8(u.is_stopped), h.u8(u.field_09f);
    for (const Unit::Waypoint& w : u.waypoints) h.f32(w.x), h.f32(w.y), h.u8(w.work), h.u16(w.field_0ca);
    h.u8(u.field_0bc), h.bytes(u.path);
    h.u16(u.path_index), h.u8(u.work), h.u16(u.field_0ca), h.u8(u.player_unit_attack), h.u16(u.attack_unit_id);
    h.b(u.has_attack_unit), h.f32(u.hp), h.u32(u.field_0e4), h.u16(u.field_0ee), h.b(u.in_house);
    h.u8(u.field_0f8), h.u8(u.player_house_attack), h.b(u.has_house_attack), h.b(u.in_group), h.u8(u.field_106);
    h.u8(u.field_107), h.u8(u.field_116), h.f32(u.field_118), h.u8(u.field_141), h.u16(u.field_142), h.u8(u.field_144);
    h.u8(u.field_145), h.u8(u.field_154), h.b(u.has_future_transporter), h.b(u.in_transport), h.u32(u.field_160);
    h.u32(u.field_174), h.u8(u.field_178), h.b(u.in_cell_list), h.u32(u.group), h.u16(u.group_index);
    h.u8(u.field_09c), h.f32(u.field_08c), h.f32(u.field_0d4), h.f32(u.field_0d8), h.ref(u.attack_ref);
    h.u8(u.house_attack_player), h.u16(u.house_attack_slot), h.u8(u.weapon_count), h.u8(u.field_158);
    h.u8(u.field_15c), h.b(u.weapon_target), h.f32(u.field_050), h.f32(u.field_054), h.f32(u.field_094),
        h.f32(u.field_098);
    h.u8(u.house_player), h.u16(u.house_slot);
    // Milestone 3 slice H: aircraft, vehicles, ships.
    h.u16(u.field_0ec), h.u8(u.field_092), h.ref(u.mg_target), h.b(u.has_mg_target), h.u8(u.mg_delay);
    h.u8(u.field_108);
    for (const auto* a : {&u.field_121, &u.field_129, &u.field_131}) {
        for (std::uint8_t v : *a) h.u8(v);
    }
    h.u8(u.field_139), h.count(u.passengers.size());
    for (const UnitRef& r : u.passengers) h.ref(r);
    h.f32(u.field_16c), h.f32(u.field_170), h.count(u.guns.size());
    for (const Unit::ShipGun& g : u.guns) h.f32(g.angle), h.f32(g.target), h.u16(g.reload);
    h.u8(u.field_0e8), h.u8(u.field_0e9);  // version 12
    // Version 15: the loader gaps of docs/save-audit.md 10.1.
    h.u32(u.field_11c), h.u8(u.field_120), h.ref(u.future_transporter), h.ref(u.transporter), h.u8(u.field_018);
    h.f32(u.field_058), h.f32(u.field_05c), h.u8(u.field_06d), h.u8(u.field_0ea);
}

void unit_type(Hasher& h, const UnitType& t) {
    h.b(t.present), h.u8(t.move_class), h.u8(t.is_ship), h.u8(t.is_air);
    for (float v : t.speed) h.f32(v);            // per player since version 17
    for (float v : t.waypoint_radius) h.f32(v);  // per player since version 17
    h.f32(t.turn_rate);
    for (float v : t.anim_speed) h.f32(v);
    for (std::uint8_t v : t.anim_size) h.u8(v);
    h.u8(t.field_006), h.u8(t.field_009), h.u8(t.field_468), h.u8(t.field_594), h.f32(t.field_598), h.i16(t.size);
    for (float v : t.upkeep) h.f32(v);
    for (float v : t.cost) h.f32(v);
    for (const auto* table : {&t.reload, &t.max_hp, &t.build_time, &t.range, &t.accuracy}) {
        for (std::uint16_t v : *table) h.u16(v);
    }
    h.u8(t.field_007), h.u8(t.field_008), h.u8(t.field_5e0);
    h.count(t.weapons.size());
    for (const WeaponType& w : t.weapons) {
        h.u8(w.kind), h.u16(w.attack), h.u16(w.reload), h.f32(w.fire_range), h.f32(w.min_range);
        h.u8(w.field_48), h.f32(w.point_a[0]), h.f32(w.point_a[1]), h.f32(w.point_a[2]);
    }
    for (const auto* table : {&t.attack, &t.defense}) {
        for (const auto& row : *table) {
            for (std::uint16_t v : row) h.u16(v);
        }
    }
    // Milestone 3 slice H: aircraft, vehicles, ships.
    h.u16(t.field_434), h.u16(t.field_48a), h.f32(t.turn_radius), h.u8(t.field_5d8), h.f32(t.field_5dc);
    h.u16(t.field_588), h.u8(t.field_00b);  // field_00b: version 12
    h.u8(t.field_00c);                      // version 16 (research, slice K)
    const ShipGraphic& g = t.ship;
    h.b(g.loaded), h.u8(g.frames), h.b(g.has_text), h.u8(g.sink_count), h.count(g.weapons.size());
    for (const ShipWeaponType& w : g.weapons) {
        for (float v : w.pos) h.f32(v);
        h.u8(w.arc_count);
        for (float v : w.arcs) h.f32(v);
        h.f32(w.f28), h.u8(w.barrels), h.f32(w.turn_step), h.u8(w.steps), h.u8(w.kind), h.u16(w.attack);
        h.u16(w.reload), h.f32(w.fire_range), h.f32(w.min_range), h.u8(w.slot), h.count(w.points.size());
        for (const auto& q : w.points) h.f32(q[0]), h.f32(q[1]), h.f32(q[2]);
    }
    h.count(g.hull.size());
    for (const auto& q : g.hull) h.f32(q[0]), h.f32(q[1]), h.f32(q[2]);
}

void group(Hasher& h, const Group& g) {
    h.u32(g.serial);
    h.count(g.members.size());
    for (const UnitRef& r : g.members) h.ref(r);
    h.count(g.slots.size());
    for (const auto& v : g.slots) h.i32(v[0]), h.i32(v[1]);
    h.count(g.wp.size());
    for (const auto& v : g.wp) h.i32(v[0]), h.i32(v[1]);
    h.bytes(g.wp_orient);
    h.u16(g.wp_count), h.u16(g.wp_next), h.u16(g.live), h.u16(g.quorum), h.u8(g.type), h.u8(g.spacing), h.u8(g.cols);
    h.u16(g.leader), h.u8(g.keep_spacing), h.f32(g.target_fx), h.f32(g.target_fy), h.u8(g.slow);
    h.u8(g.arrange_orient), h.f32(g.spacing_override), h.u16(g.wait), h.u8(g.queued), h.u8(g.selected);
    h.u8(g.ai_owned), h.i32(g.field_048), h.u8(g.large), h.u8(g.retries), h.u16(g.blocked), h.u8(g.has_mc7);
    h.u8(g.has_mc10), h.u8(g.arg3), h.u8(g.player), h.u8(g.kind), h.u8(g.target_player), h.u16(g.target_id);
    h.i32(g.tx), h.i32(g.ty), h.u8(g.stepping), h.u16(g.half), h.u8(g.dead), h.f32(g.speed), h.f32(g.speed_factor);
    h.u8(g.orient), h.u16(g.capacity), h.u8(g.field_069), h.bytes(g.field_0c);
    h.count(g.follow_up.size());
    for (const auto& f : g.follow_up) {
        for (std::uint8_t v : f) h.u8(v);
    }
}

// net: GameState_NetHash, which leaves out the fields each machine of a network game has its own (MyPlayer, the
// pause, the result, the end message) and starts with its own version; else the bytes are GameState_Hash's.
std::uint64_t Hash(const GameState& s, bool net) {
    Hasher h;
    h.u32(net ? kNetHashVersion : kHashVersion);
    h.u32(s.tick), h.u8(s.speed_ms);
    if (!net) h.u8(s.my_player);
    h.u8(s.short_anims), h.u32(s.rng_seed);
    h.u16(s.rng_index), h.u32(s.crt_holdrand), h.u16(s.map_width), h.u16(s.map_height), h.i32(s.climate);

    h.count(s.players.size());
    for (const Player& p : s.players) {
        h.u8(p.nation), h.u8(p.ai_type), h.u8(p.control_type), h.u8(p.is_lost);
        h.i32(p.units_alive), h.i32(p.units_lost), h.i32(p.units_killed), h.u16(p.unit_cap);
        h.f64(p.gold), h.f64(p.wood), h.f64(p.iron), h.f64(p.oil), h.f64(p.food);
        h.words(p.free_ids), h.u16(p.next_id);
        h.f32(p.power_used), h.f32(p.power_6c4), h.f32(p.power_6e4), h.f32(p.power_made);
        h.u8(p.field_9b2b3b), h.u8(p.field_9b2d94), h.u8(p.field_9b2b3c), h.u8(p.field_9b2d9c), h.u8(p.field_9b2b3d);
        h.u32(p.ai_next_serial), h.i32(p.ai_counter), h.u8(p.ai_kind), h.u8(p.ai_level), h.u16(p.history_index);
        h.i32(p.ai_retry), h.u8(p.ai_all), h.i32(p.ai_countdown);
        for (std::uint8_t v : p.field_9b2a98) h.u8(v);
        h.f64(p.gold_stat), h.f64(p.wood_stat), h.f64(p.iron_stat), h.f64(p.oil_stat), h.f64(p.food_stat);
        h.count(p.history.size());  // every sample: the save keeps the whole arrays (version 15)
        for (std::size_t k = 0; k < p.history.size(); ++k) {
            const HistSample& q = p.history[k];
            h.f64(q.gold), h.f64(q.wood), h.f64(q.iron), h.f64(q.oil), h.f64(q.food);
        }
        for (std::uint16_t v : p.unit_count) h.u16(v);
        for (std::uint8_t v : p.houses_done) h.u8(v);
        h.u8(p.field_9b2b38), h.u8(p.field_9b2b39), h.u8(p.field_9b2b3a);
        for (std::uint8_t v : p.research_flags) h.u8(v);
        for (std::uint8_t v : p.research_9b39f0) h.u8(v);
        for (std::uint8_t v : p.research_9b3950) h.u8(v);
        h.b(p.script.has_value());
        if (p.script) {
            const AIScript& a = *p.script;
            for (std::uint16_t v : a.want_u) h.u16(v);
            for (std::uint16_t v : a.want_h) h.u16(v);
            h.u8(a.nsteps), h.u8(a.step), h.u16(a.entry);
            for (const auto* lists : {&a.main, &a.list2}) {
                h.count(lists->size());
                for (const auto& l : *lists) {
                    h.count(l.size());
                    for (const ScriptEntry& e : l) h.u8(e.kind), h.u16(e.type), h.u16(e.count);
                }
            }
            h.i32(a.base_x), h.i32(a.base_y), h.i32(a.field_660), h.i32(a.field_664);
            h.count(a.items.size());
            for (std::uint64_t v : a.items)
                h.u32(static_cast<std::uint32_t>(v)), h.u32(static_cast<std::uint32_t>(v >> 32));
            h.i32(a.field_6f4), h.i32(a.field_6f8), h.i32(a.field_6fc), h.i32(a.field_704), h.i32(a.field_708);
            h.i32(a.field_70c);
        }
        h.bytes(p.zone_grid), h.u16(p.zone_width);
        for (std::uint8_t v : p.is_ally) h.u8(v);
        h.f32(p.shell_spread), h.f32(p.accuracy_factor), h.f32(p.factor_684), h.f32(p.factor_664), h.f32(p.factor_644),
            h.f32(p.factor_744);
        h.f32(p.factor_724), h.f32(p.factor_9b3338), h.f32(p.factor_9e28fc), h.f32(p.factor_9b32d8);
        h.u8(p.mint_done), h.i32(p.mint_count), h.f32(p.gold_factor);
        h.i32(p.houses_lost), h.i32(p.houses_destroyed), h.i32(p.houses_built), h.f32(p.score);
        for (double v : p.start_res) h.f64(v);
        for (double v : p.mined) h.f64(v);
        for (float v : p.res_sold) h.f32(v);
        for (float v : p.res_bought) h.f32(v);
        h.u8(p.house_array_freed);
        h.count(p.name.size());
        for (char16_t c : p.name) h.u16(static_cast<std::uint16_t>(c));
        h.u32(p.stat_colour);
        h.u8(p.colour), h.u8(static_cast<std::uint8_t>(p.team)), h.u8(p.sel_class6), h.u8(p.sel_class7);
        h.u16(p.sel_houses), h.u16(p.sel_house), h.u16(p.num_selected);
        for (const Player::ControlGroup& g : p.control_groups) h.u8(g.kind), h.words(g.ids);
        for (std::uint8_t v : p.research_9b3860) h.u8(v);
        for (std::uint8_t v : p.research_9b3770) h.u8(v);
        h.u32(p.special_shells), h.f32(p.factor_9b33b8), h.f32(p.factor_9b3318);
        h.f32(p.factor_9b32f8);  // version 16
        // The market's automatic trade (slice I7): only while it is on, so the hashes of every other state stay.
        if (p.market_auto != 0) {
            h.u8(p.market_auto), h.u8(p.market_buy), h.f32(p.market_received);
            for (float v : p.market_sell) h.f32(v);
        }
        h.u8(p.field_20099), h.u8(p.field_20170), h.u8(p.field_2cc9a);
        h.count(p.num_units);
        for (std::uint32_t k = p.first_unit; k != kNoSlot; k = p.unit_slots[k].next) unit(h, p.unit_slots[k]);
        h.count(p.houses.size());
        for (const House& o : p.houses) {
            h.u16(o.num), h.u16(o.type), h.i32(o.map_x), h.i32(o.map_y), h.f32(o.hp), h.u8(o.is_destroyed);
            h.u16(o.queue_count), h.words(o.queue), h.u32(o.generation);
            h.f32(o.x), h.f32(o.y), h.f32(o.exit_x), h.f32(o.exit_y), h.u16(o.field_020), h.u16(o.units_inside);
            h.f32(o.rally_x), h.f32(o.rally_y), h.f32(o.field_044), h.u16(o.field_05c), h.f32(o.field_060);
            h.u8(o.field_065), h.u16(o.max_hp), h.u32(o.field_0b4), h.b(o.has_field_0b0), h.u8(o.field_0b8);
            h.u8(o.selected), h.u8(o.field_030), h.u8(o.field_064), h.u16(o.capacity), h.u8(o.field_06a);
            h.u8(o.field_06c.kind), h.u8(o.field_06c.index), h.f32(o.field_070), h.u8(o.field_074), h.f32(o.field_078),
                h.f32(o.field_07c);
            h.f32(o.field_080), h.u8(o.field_084), h.ref(o.field_088), h.u8(o.field_08c), h.i32(o.field_0a8);
            h.u8(o.graphic_phase), h.u8(o.graphic_stage), h.u8(o.depth_phase), h.u8(o.depth_stage);
            h.count(o.inside.size());
            for (const UnitRef& r : o.inside) h.ref(r);
            h.u32(o.field_0ac), h.ref(o.field_0b0);
            h.u8(o.field_04a), h.u8(o.field_08f), h.u8(o.field_090), h.u8(o.field_0b9), h.count(o.upgrade_slots.size());
            for (std::uint32_t v : o.upgrade_slots) h.u32(v);
        }
        h.count(p.groups.size());
        for (const Group& g : p.groups) group(h, g);
        h.u32(p.next_group);
        h.count(p.commands.size());
        for (const CommandNode& n : p.commands) {
            h.u32(n.seq), h.u16(n.cls), h.u8(n.cmd), h.f32(n.x), h.f32(n.y), h.ref(n.unit);
            h.u32(n.group), h.count(n.list.size());
            for (const UnitRef& r : n.list) h.ref(r);
            h.u8(n.a3), h.u8(n.type), h.u8(n.cols), h.u8(n.kind), h.i32(n.px), h.i32(n.py), h.u8(n.tplayer);
            h.u16(n.tid);
            h.u32(n.a2c);
        }
    }
    for (const auto& nation : s.unit_types) {
        for (const UnitType& t : nation) unit_type(h, t);
    }
    for (const auto& nation : s.house_footprints) {
        for (const HouseFootprint& f : nation) h.u8(f.a), h.u8(f.b);
    }
    for (const auto& nation : s.house_types) {
        for (const HouseType& t : nation) {
            for (float v : t.upkeep) h.f32(v);
            h.f32(t.power_use), h.u8(t.field_1b0), h.u16(t.work_rate), h.u16(t.max_hp), h.u16(t.field_14c);
            h.u8(t.field_1b1), h.u8(t.field_1b3), h.u8(t.field_1f2), h.u8(t.field_2ae);
            h.u8(t.field_2ac);  // version 16
            h.u8(t.field_26e), h.b(t.present);
            for (float v : t.cost) h.f32(v);
            h.words(t.prereq);
            for (std::uint16_t v : t.produce) h.u16(v);
            h.u16(t.sprite_w), h.u16(t.sprite_h), h.u16(static_cast<std::uint16_t>(t.field_140));
            h.u16(static_cast<std::uint16_t>(t.field_142)), h.u16(t.field_148), h.u8(t.hit_count);
            h.u16(t.anchor_x), h.u16(t.anchor_y), h.u16(static_cast<std::uint16_t>(t.exit_x));
            h.u16(static_cast<std::uint16_t>(t.exit_y)), h.u32(t.field_190);
            for (std::uint16_t v : t.hit_x) h.u16(v);
            for (std::uint16_t v : t.hit_y) h.u16(v);
        }
    }
    h.u32(s.next_serial);

    const Map& m = s.map;
    h.u16(m.width), h.u16(m.height), h.bytes(m.heights), h.words(m.objects), h.bytes(m.flags), h.words(m.obstacle);
    h.count(m.terrain.size());
    for (const TerrainCell& c : m.terrain) {
        h.u16(c.tile), h.u8(c.shade), h.u8(c.unused);
        for (std::uint16_t v : c.sub) h.u16(v);
    }
    h.count(m.neutral_houses.size());
    for (const NeutralHouse& n : m.neutral_houses) h.u8(n.type), h.i32(n.map_x), h.i32(n.map_y);
    h.count(m.specials.size());
    for (const SpecialObject& o : m.specials) {
        h.u16(o.x), h.u16(o.y), h.u8(o.kind), h.u8(o.alive), h.f32(o.f_a), h.f32(o.hp);  // kind, alive, hp: version 19
    }
    h.u16(m.special_capacity), h.words(m.pass_land), h.words(m.pass_large);
    h.bytes(m.weight);
    h.count(m.cell_units.size());
    for (const std::vector<UnitRef>& list : m.cell_units) {
        h.count(list.size());
        for (const UnitRef& r : list) h.ref(r);
    }

    h.count(s.path_cache.size());
    for (const PathRecord& r : s.path_cache) {
        h.i32(r.sx), h.i32(r.sy), h.i32(r.ex), h.i32(r.ey), h.u32(r.tick), h.bytes(r.path);
    }
    h.i32(s.path_budget_used), h.i32(s.path_budget_max), h.i32(s.deferred_path_count);
    h.i32(s.deferred_human_count), h.u32(s.next_node_seq), h.u8(s.formation_budget);
    h.u8(s.flag_8ab825), h.u8(s.flag_8ab824), h.u8(s.flag_8ef4d4), h.u8(s.flag_8ef4d5), h.u8(s.flag_8ef4d6);
    h.u8(s.flag_8e836e), h.u8(s.skirmish_flag), h.u8(s.land_map), h.u8(s.flag_9b2df9), h.u16(s.sel_8ab774);
    h.u16(s.sel_8ab770), h.count(s.map_name.size());
    for (char c : s.map_name) h.u8(static_cast<std::uint8_t>(c));
    for (const GameState::SlotRecord& r : s.slots) {
        h.count(r.name.size());
        for (char16_t c : r.name) h.u16(static_cast<std::uint16_t>(c));
        h.u8(r.nation), h.u8(r.colour), h.u8(r.team), h.u8(r.level);
    }
    h.u8(s.slot_count);
    h.count(s.decaying_fields.size());
    for (std::uint32_t v : s.decaying_fields) h.u32(v);
    h.count(s.port_sites.size());
    for (std::uint32_t v : s.port_sites) h.u32(v);
    h.u8(s.mine_site_count);
    for (const auto& c : s.mine_sites) h.i32(c[0]), h.i32(c[1]);
    // The target-search grid (docs/combat.md 2): not in the original's save, but the scans read it until the next
    // rebuild, so it is state.
    h.i32(s.grid_w), h.i32(s.grid_h);
    h.count(s.unit_cells.size());
    for (const auto& list : s.unit_cells) {
        h.count(list.size());
        for (const UnitRef& r : list) h.ref(r);
    }
    h.count(s.house_cells.size());
    for (const auto& list : s.house_cells) {
        h.count(list.size());
        for (const HouseRef& r : list) h.u8(r.player), h.u16(r.slot), h.u32(r.generation);
    }
    h.count(s.shells.size());
    for (const Shell& v : s.shells) {
        h.f32(v.x0), h.f32(v.x1), h.f32(v.y0), h.f32(v.y1), h.f32(v.z0), h.f32(v.z1);
        h.f32(v.cx), h.f32(v.cy), h.f32(v.cz), h.f64(v.g), h.f64(v.vx), h.f64(v.vy), h.f64(v.v0), h.f64(v.vz);
        h.f64(v.flight), h.f32(v.substep), h.f32(v.range), h.f32(v.a4), h.u8(v.fail), h.u8(v.player), h.ref(v.owner);
        h.u8(v.gas), h.u8(v.no_effect), h.i32(v.radius), h.u8(v.sound), h.i32(v.sound_tick), h.i32(v.damage);
        h.u16(v.delay), h.u8(v.kind), h.u8(v.fragments), h.i32(v.flown);
    }
    // The gas clouds (2026-10-17), only when there is one: a state without any hashes as before (no version change).
    if (!s.gas_clouds.empty()) {
        h.count(s.gas_clouds.size());
        for (const GasCloud& g : s.gas_clouds) {
            h.u32(g.kind), h.i32(g.x), h.i32(g.y), h.i32(g.life), h.u16(g.max_radius), h.u16(g.step), h.u8(g.puffs);
            h.i32(g.damage), h.i32(g.damage_radius), h.u8(g.type), h.i16(g.radius);
        }
    }
    // Milestone 3 slice H: large-unit cell lists (empty between ticks), bullets, ships and their water grid.
    for (const auto* list : {&s.large_cells_clear, &s.large_cells_set}) {
        h.count(list->size());
        for (const auto& c : *list) h.i32(c[0]), h.i32(c[1]);
    }
    h.count(s.bullets.size());
    for (const Bullet& b : s.bullets) {
        h.f32(b.x), h.f32(b.y), h.f32(b.z), h.u16(b.field_00c), h.u16(b.delay), h.ref(b.owner);
    }
    h.count(s.ships.size());
    for (const UnitRef& r : s.ships) h.ref(r);
    if (!net) h.u8(s.paused), h.u8(s.game_result), h.u8(s.end_message);
    h.u8(s.fog_553ae0), h.b(s.original_rules), h.count(s.ship_sprites.size());  // original_rules: version 17
    for (const GameState::ShipSprite& v : s.ship_sprites) h.u32(v.refs), h.u8(v.state), h.u32(v.ready_tick);
    h.i32(m.water.w), h.i32(m.water.h), h.bytes(m.water.g), h.count(m.water.recs.size());
    for (const ShipRec& r : m.water.recs) h.ref(r.unit), h.u16(r.i), h.u16(r.j);
    // The mission path (2026-10-17, docs/missions.md), only in a mission: a skirmish hashes as before (no version
    // change).
    if (!s.mission_path.empty()) {
        h.count(s.mission_path.size());
        for (char c : s.mission_path) h.u8(static_cast<std::uint8_t>(c));
    }
    // The mission event blocks, the reveal list and the end text (2026-10-18, sim/mission_events.hpp), only in a
    // mission or with blocks: a skirmish hashes as before, also one loaded from a save whose reveal list the original
    // left set (the shipped No_name.svg has one; only an action of a block changes it). The blocks' fields that change
    // while they run, and what each one is.
    bool mission = !s.mission_path.empty() || !s.end_text.empty();
    for (const Player& p : s.players) mission = mission || !p.blocks.empty();
    if (mission) {
        for (const Player& p : s.players) {
            h.count(p.blocks.size());
            for (const MisBlock& b : p.blocks) {
                h.u8(b.done), h.u8(b.id), h.u8(b.open_prereq), h.u16(b.condition), h.i32(b.timer), h.u8(b.stage);
                for (std::uint8_t v : b.prereq) h.u8(v);
                h.u8(b.win), h.u8(b.lose), h.i32(b.countdown), h.u32(b.action.kind);
            }
        }
        for (const GameState::Reveal& r : s.reveal) h.i32(r.x), h.i32(r.y), h.u8(r.radius);
        h.count(s.end_text.size());
        for (char16_t c : s.end_text) h.u16(c);
    }
    // The Lua mission's manager data (2026-10-18, slice M5, script/lua_manager.hpp), only in a Lua mission.
    if (s.lua_mission != 0) {
        auto str = [&h](const std::string& v) {
            h.count(v.size());
            for (char c : v) h.u8(static_cast<std::uint8_t>(c));
        };
        h.u8(s.lua_mission), h.u8(s.script_active), h.u32(s.script_winner), h.count(s.script_groups.size());
        for (const auto& [name, g] : s.script_groups) {
            str(name), h.u8(g.player), h.count(g.members.size());
            for (const auto& [id, r] : g.members) h.u16(id), h.ref(r);
        }
        h.count(s.script_timers.size());
        for (const auto& [name, t] : s.script_timers) {
            str(name), str(t.code), h.f32(t.base), h.f32(t.period), h.u32(t.repeats), h.u32(t.fired);
        }
        h.count(s.script_keys.size());
        for (const std::string& k : s.script_keys) str(k);
    }
    // A network game (docs/multiplayer.md), only when it is one: single player games hash as before.
    // Slice MP9: who plays each player (its human or the computer for a human who left), with what the takeover kept.
    if (s.plus_rules) {  // Entente Engine Plus ("P"), only when on: Classic games hash as before
        h.u8(0x50);
        for (const Player& p : s.players) {
            for (float v : p.market_index) h.f32(v);               // E2, the market's price index
            h.u8(p.plus_delousing);                                // F6, the Delousing count
            h.u8(p.plus_build_level);                              // F7, the Building Academy's researches done
            for (std::uint32_t v : p.plus_strike_ready) h.u32(v);  // the call-in strikes' cooldowns
            h.i32(p.plus_recon_x), h.i32(p.plus_recon_y), h.u32(p.plus_recon_end);  // and the recon flight
        }
    }
    if (s.net_game != 0) {
        h.u8(s.net_game);
        for (const Player& p : s.players) {
            h.u8(p.net_ai);
            for (std::uint8_t v : p.net_kept) h.u8(v);
        }
    }
    // The units and houses of another nation's type record (a merge across nations, 2026-10-19), only when there is
    // one: every other game hashes as before (no version change). Per player its live units in list order, then its
    // houses by slot.
    std::uint32_t foreign = 0;
    for (const Player& p : s.players) {
        for (std::uint32_t k = p.first_unit; k != kNoSlot; k = p.unit_slots[k].next)
            foreign += p.unit_slots[k].nation != 0xff;
        for (const House& v : p.houses) foreign += v.nation != 0xff;
    }
    if (foreign != 0) {
        h.u32(foreign);
        for (std::size_t q = 0; q < s.players.size(); ++q) {
            const Player& p = s.players[q];
            for (std::uint32_t k = p.first_unit; k != kNoSlot; k = p.unit_slots[k].next) {
                const Unit& u = p.unit_slots[k];
                if (u.nation != 0xff) h.u8(static_cast<std::uint8_t>(q)), h.u16(u.id), h.u8(u.nation);
            }
            for (std::size_t k = 0; k < p.houses.size(); ++k) {
                if (p.houses[k].nation != 0xff)
                    h.u8(static_cast<std::uint8_t>(q)), h.count(k), h.u8(p.houses[k].nation);
            }
        }
    }
    // A fighter's ground target to go back to (our rules, docs/bugs.md B277), only when one is set: the original's
    // rules never set one, so their games hash as before (no version change). Per player its live units in list order.
    std::uint32_t returns = 0;
    for (const Player& p : s.players) {
        for (std::uint32_t k = p.first_unit; k != kNoSlot; k = p.unit_slots[k].next)
            returns += p.unit_slots[k].return_target.player != 0xff;
    }
    if (returns != 0) {
        h.u32(returns);
        for (std::size_t q = 0; q < s.players.size(); ++q) {
            const Player& p = s.players[q];
            for (std::uint32_t k = p.first_unit; k != kNoSlot; k = p.unit_slots[k].next) {
                const Unit& u = p.unit_slots[k];
                if (u.return_target.player != 0xff)
                    h.u8(static_cast<std::uint8_t>(q)), h.u16(u.id), h.ref(u.return_target);
            }
        }
    }
    return h.h;
}

}  // namespace

std::uint32_t GameState_HashVersion() { return kHashVersion; }

std::uint64_t GameState_Hash(const GameState& s) { return Hash(s, false); }

std::uint64_t GameState_NetHash(const GameState& s) { return Hash(s, true); }

}  // namespace antanta::sim
