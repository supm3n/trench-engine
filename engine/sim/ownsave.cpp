// Our own save files (sim/ownsave.hpp, docs/save-own.md). One template per struct walks its fields for both the
// writer and the reader, so the two cannot disagree on the order. A field added to a struct of sim/state.hpp must be
// added here too (tools/save_coverage.py checks that every member name appears; the gate runs it).
#include "sim/ownsave.hpp"

#include <zlib.h>

#include <bit>
#include <cstring>

namespace antanta::sim {

namespace {

constexpr std::uint8_t kMagic[16] = {'A', 'N', 'T', 'A', 'N', 'T', 'A', ' ', 'S', 'A', 'V', 'E', '\r', '\n', 0x1a, 0};

// Chunk versions this build writes and reads (a newer one is refused).
// SIM version 2 (2026-10-17): the gas clouds at the end; version 1 files load with none.
// SIM version 3 (2026-10-17): the special objects' kind, alive byte and hit points at the end (the pontoon bridge);
// older files load them as unused records have them.
// SIM version 4 (2026-10-17): the mission path at the end; older files load as a skirmish (empty path).
// SIM version 6 (2026-10-18, slice M5): the Lua mission's manager data at the end (not its Lua globals: slice M6).
// SIM version 7 (2026-10-18, slice MP1): the network game flag at the end; older files load as single player.
// SIM version 8 (2026-10-19): per player the nation byte of every unit slot and every house at the end (a merge
// across nations keeps the type record's nation, Unit::nation, House::nation); older files load with 0xff (the
// owner's nation) everywhere.
// SIM version 10 (2026-10-20, B277): per player the return target of every unit slot at the end (Unit::return_target,
// our rules); older files load with none.

// the wrong animation before 94ba4f5), so OwnSave_Read computes it again from the loaded types.
// SIM version 12 (2026-10-22): the Entente Engine Plus flag at the end (GameState::plus_rules); older files load as
// Classic.
// SIM version 13 (2026-10-22): each player's market price index (Player::market_index, 5 floats, Entente Engine Plus
// E2) at the end; older files load with 1.0.
// SIM version 14 (2026-10-22): each player's Delousing count (Player::plus_delousing, one byte, Entente Engine Plus
// F6) at the end; older files load with 0.
// SIM version 15 (2026-10-22): each player's Building Academy level (Player::plus_build_level, one byte, Entente Engine
// Plus F7) at the end; older files load with 0.
// SIM version 16 (2026-10-23): each player's call-in strike cooldowns (Player::plus_strike_ready, 5 x u32) and recon
// flight (plus_recon_x, plus_recon_y, plus_recon_end) at the end (Entente Engine Plus); older files load with 0 (every
// strike ready, no recon).
// SIM version 17 (2026-11-04, B317): per player the Shift-click order queue of every unit slot at the end
// (Unit::order_queue, our rules); older files load with none.
// SETU version 2 (2026-10-22): the setup's Plus choice at the end (SkirmishSetup::plus); older files load as Classic.
// LUA version 1 (2026-10-18, slice M6): a Lua mission's globals, one string of Lua lines.
// GRND version 1 (2026-11-04, slice G7): a generated map, whole (OwnSaveMeta::ground), zlib-compressed as SIM.
constexpr std::uint32_t kInfoVersion = 1, kViewVersion = 1, kSetupVersion = 2, kSimVersion = 17, kLuaVersion = 1,
                        kNetVersion = 1, kGroundVersion = 1;

// ---- the two walkers ----

struct Writer {
    Bytes& b;
    void u8(const std::uint8_t& v) { b.push_back(v); }
    void u16(const std::uint16_t& v) { u8(static_cast<std::uint8_t>(v)), u8(static_cast<std::uint8_t>(v >> 8)); }
    void u32(const std::uint32_t& v) { u16(static_cast<std::uint16_t>(v)), u16(static_cast<std::uint16_t>(v >> 16)); }
    void u64(const std::uint64_t& v) { u32(static_cast<std::uint32_t>(v)), u32(static_cast<std::uint32_t>(v >> 32)); }
    void i8(const std::int8_t& v) { u8(static_cast<std::uint8_t>(v)); }
    void i16(const std::int16_t& v) { u16(static_cast<std::uint16_t>(v)); }
    void i32(const std::int32_t& v) { u32(static_cast<std::uint32_t>(v)); }
    void i64(const std::int64_t& v) { u64(static_cast<std::uint64_t>(v)); }
    void f32(const float& v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void f64(const double& v) { u64(std::bit_cast<std::uint64_t>(v)); }
    void b8(const bool& v) { u8(v ? 1 : 0); }
    void count(std::size_t n) { u32(static_cast<std::uint32_t>(n)); }
    void str(const std::string& s) {
        count(s.size());
        for (char c : s) u8(static_cast<std::uint8_t>(c));
    }
    void u16str(const std::u16string& s) {
        count(s.size());
        for (char16_t c : s) u16(static_cast<std::uint16_t>(c));
    }
    template <class V, class F>
    void vec(const V& v, F&& f) {
        count(v.size());
        for (const auto& e : v) f(e);
    }
    template <class T, std::size_t N, class F>
    void arr(const std::array<T, N>& v, F&& f) {
        count(N);
        for (const auto& e : v) f(e);
    }
    template <class T, class F>
    void opt(const std::optional<T>& o, F&& f) {
        u8(o.has_value() ? 1 : 0);
        if (o) f(*o);
    }
    template <class K, class T, class FK, class FV>
    void map(const std::map<K, T>& m, FK&& fk, FV&& fv) {
        count(m.size());
        for (const auto& [k, v] : m) fk(k), fv(v);
    }
};

struct Reader {
    Cursor c;
    std::string what;  // the first problem
    bool good() const { return c.ok && what.empty(); }
    void fail(const std::string& w) {
        if (what.empty()) what = w;
        c.ok = false;
    }
    void u8(std::uint8_t& v) { v = c.u8(); }
    void u16(std::uint16_t& v) { v = c.u16(); }
    void u32(std::uint32_t& v) { v = c.u32(); }
    void u64(std::uint64_t& v) { v = c.u64(); }
    void i8(std::int8_t& v) { v = static_cast<std::int8_t>(c.u8()); }
    void i16(std::int16_t& v) { v = c.i16(); }
    void i32(std::int32_t& v) { v = c.i32(); }
    void i64(std::int64_t& v) { v = static_cast<std::int64_t>(c.u64()); }
    void f32(float& v) { v = std::bit_cast<float>(c.u32()); }
    void f64(double& v) { v = std::bit_cast<double>(c.u64()); }
    void b8(bool& v) {
        const std::uint8_t x = c.u8();
        if (x > 1) fail("a yes/no value is " + std::to_string(x));
        v = x != 0;
    }
    // A list count: every element takes at least one byte, so a count above the bytes left is damage (and never
    // allocates more than the file could fill).
    std::size_t count() {
        const std::uint32_t n = c.u32();
        if (!good()) return 0;
        if (n > c.data.size() - c.pos) {
            fail("a list of " + std::to_string(n) + " entries in the last " + std::to_string(c.data.size() - c.pos) +
                 " bytes");
            return 0;
        }
        return n;
    }
    void str(std::string& s) {
        const std::size_t n = count();
        s.assign(n, '\0');
        for (char& ch : s) ch = static_cast<char>(c.u8());
    }
    void u16str(std::u16string& s) {
        const std::size_t n = count();
        s.assign(n, u'\0');
        for (char16_t& ch : s) ch = static_cast<char16_t>(c.u16());
    }
    template <class V, class F>
    void vec(V& v, F&& f) {
        const std::size_t n = count();
        v.clear();
        v.resize(n);
        for (auto& e : v) {
            if (!good()) return;
            f(e);
        }
    }
    template <class T, std::size_t N, class F>
    void arr(std::array<T, N>& v, F&& f) {
        const std::uint32_t n = c.u32();
        if (good() && n != N)
            fail("a table of " + std::to_string(n) + " entries where " + std::to_string(N) + " belong");
        for (auto& e : v) {
            if (!good()) return;
            f(e);
        }
    }
    template <class T, class F>
    void opt(std::optional<T>& o, F&& f) {
        bool has = false;
        b8(has);
        o.reset();
        if (has && good()) f(o.emplace());
    }
    template <class K, class T, class FK, class FV>
    void map(std::map<K, T>& m, FK&& fk, FV&& fv) {
        const std::size_t n = count();
        m.clear();
        for (std::size_t i = 0; i < n && good(); ++i) {
            K k{};
            T v{};
            fk(k), fv(v);
            if (!m.emplace(k, v).second) fail("a key twice in a table");
        }
    }
};

// ---- the fields, one walker per struct (T is the struct or the const struct) ----

template <class A, class T>
void unit_ref(A& a, T& r) {
    a.u8(r.player), a.u16(r.id), a.u32(r.serial);
}
template <class A, class T>
void house_ref(A& a, T& r) {
    a.u8(r.player), a.u16(r.slot), a.u32(r.generation);
}
template <class A, class T>
void floats(A& a, T& v) {
    a.arr(v, [&](auto& x) { a.f32(x); });
}
template <class A, class T>
void words(A& a, T& v) {
    a.arr(v, [&](auto& x) { a.u16(x); });
}
template <class A, class T>
void bytes8(A& a, T& v) {
    a.arr(v, [&](auto& x) { a.u8(x); });
}
template <class A, class T>
void point(A& a, T& v) {  // std::array<std::int32_t, 2>
    a.arr(v, [&](auto& x) { a.i32(x); });
}
template <class A, class T>
void u8_list(A& a, T& v) {
    a.vec(v, [&](auto& x) { a.u8(x); });
}
template <class A, class T>
void u16_list(A& a, T& v) {
    a.vec(v, [&](auto& x) { a.u16(x); });
}
template <class A, class T>
void u32_list(A& a, T& v) {
    a.vec(v, [&](auto& x) { a.u32(x); });
}
template <class A, class T>
void ref_list(A& a, T& v) {
    a.vec(v, [&](auto& r) { unit_ref(a, r); });
}

template <class A, class T>
void unit_type(A& a, T& t) {
    a.b8(t.present), a.u16(t.graphic), a.u8(t.move_class), a.u8(t.is_ship), a.u8(t.is_air), a.u8(t.field_006);
    a.u8(t.field_007), a.u8(t.field_008), a.u8(t.field_009), a.u8(t.field_00b), a.u8(t.field_00c), a.u8(t.field_5e0);
    a.vec(t.weapons, [&](auto& w) {
        a.u8(w.kind), a.u16(w.attack), a.u16(w.reload), a.f32(w.fire_range), a.f32(w.min_range), a.u8(w.field_48);
        floats(a, w.point_a);
    });
    floats(a, t.speed), floats(a, t.waypoint_radius), a.f32(t.turn_rate);
    a.u16(t.field_434), a.u16(t.field_48a), a.f32(t.turn_radius), floats(a, t.anim_speed), bytes8(a, t.anim_size);
    a.u8(t.field_468), a.u8(t.field_594), a.f32(t.field_598), a.i16(t.size), a.u8(t.field_5d8), a.f32(t.field_5dc);
    a.u16(t.field_588), floats(a, t.upkeep), floats(a, t.cost);
    words(a, t.reload), words(a, t.max_hp), words(a, t.build_time), words(a, t.range), words(a, t.accuracy);
    a.vec(t.attack, [&](auto& row) { words(a, row); });
    a.vec(t.defense, [&](auto& row) { words(a, row); });
    auto& g = t.ship;
    a.b8(g.loaded), a.u8(g.frames);
    a.vec(g.weapons, [&](auto& w) {
        floats(a, w.pos), a.u8(w.arc_count), floats(a, w.arcs), a.f32(w.f28), a.u8(w.barrels), a.f32(w.turn_step);
        a.u8(w.steps), a.u8(w.kind), a.u16(w.attack), a.u16(w.reload), a.f32(w.fire_range), a.f32(w.min_range);
        a.u8(w.slot), a.vec(w.points, [&](auto& q) { floats(a, q); });
    });
    a.vec(g.hull, [&](auto& q) { floats(a, q); });
    a.b8(g.has_text), a.u8(g.sink_count);
}

template <class A, class T>
void house_type(A& a, T& t) {
    a.b8(t.present), floats(a, t.upkeep), floats(a, t.cost), u16_list(a, t.prereq), a.f32(t.power_use);
    a.u8(t.field_1b0), a.u8(t.field_1b1), a.u8(t.field_1b3), a.u8(t.field_1f2), a.u8(t.field_2ae), a.u8(t.field_2ac);
    a.u16(t.work_rate), a.u16(t.max_hp), a.u16(t.field_14c), a.u8(t.field_26e);
    words(a, t.fire_x), words(a, t.fire_y), words(a, t.flash_x), words(a, t.flash_y);
    a.u16(t.sprite_w), a.u16(t.sprite_h), a.i16(t.field_140), a.i16(t.field_142), a.u16(t.anchor_x), a.u16(t.anchor_y);
    a.i16(t.exit_x), a.i16(t.exit_y), a.u32(t.field_190), a.u16(t.field_148), a.u8(t.hit_count);
    words(a, t.hit_x), words(a, t.hit_y), words(a, t.produce);
}

template <class A, class T>
void unit(A& a, T& u) {
    a.u8(u.num_work), a.u8(u.is_deleted), a.u8(u.field_006), a.u8(u.frame_slot), a.f32(u.field_024);
    a.f32(u.x), a.f32(u.y), a.f32(u.z), a.u32(u.next), a.u32(u.prev), a.f32(u.vel_x), a.f32(u.vel_y);
    a.u16(u.map_x), a.u16(u.map_y), a.f32(u.dest_x), a.f32(u.dest_y), a.f32(u.final_x), a.f32(u.final_y);
    a.i32(u.field_060), a.i32(u.field_064), a.f32(u.num_sprite), a.u8(u.orient), a.u8(u.target_orient);
    a.f32(u.angle), a.u8(u.field_078), a.u16(u.field_07c), a.u32(u.serial), a.u8(u.field_086), a.u16(u.type);
    a.u16(u.id), a.u8(u.field_093), a.u8(u.field_09d), a.u8(u.is_stopped), a.u8(u.field_09f), a.u8(u.field_09c);
    a.f32(u.field_08c), a.f32(u.field_0d4), a.f32(u.field_0d8), unit_ref(a, u.attack_ref);
    a.u8(u.house_attack_player), a.u16(u.house_attack_slot), a.u8(u.weapon_count), a.u8(u.field_158);
    a.u8(u.field_15c), a.b8(u.weapon_target), a.f32(u.field_050), a.f32(u.field_054), a.f32(u.field_094);
    a.f32(u.field_098), a.u8(u.house_player), a.u16(u.house_slot);
    a.arr(u.waypoints, [&](auto& w) { a.f32(w.x), a.f32(w.y), a.u8(w.work), a.u16(w.field_0ca); });
    a.u8(u.field_0bc), u8_list(a, u.path), a.u16(u.path_index), a.u8(u.work), a.u16(u.field_0ca);
    a.u8(u.player_unit_attack), a.u16(u.attack_unit_id), a.b8(u.has_attack_unit), a.f32(u.hp), a.u32(u.field_0e4);
    a.u8(u.field_0e8), a.u8(u.field_0e9), a.u16(u.field_0ee), a.u16(u.field_0ec), a.u8(u.field_092);
    a.b8(u.in_house), a.u8(u.field_0f8), a.u8(u.player_house_attack), a.b8(u.has_house_attack), a.b8(u.in_group);
    a.u32(u.group), a.u16(u.group_index), a.u8(u.field_106), a.u8(u.field_107), a.u8(u.field_116);
    a.f32(u.field_118), a.u8(u.field_141), a.u16(u.field_142), a.u8(u.field_144), a.u8(u.field_145);
    a.u8(u.field_154), a.b8(u.has_future_transporter), unit_ref(a, u.future_transporter);
    unit_ref(a, u.transporter), a.u8(u.field_018), a.f32(u.field_058), a.f32(u.field_05c), a.u8(u.field_06d);
    a.u8(u.field_0ea), a.b8(u.in_transport), a.u32(u.field_160), a.u32(u.field_174), a.u8(u.field_178);
    unit_ref(a, u.mg_target), a.b8(u.has_mg_target), a.u8(u.mg_delay), a.b8(u.in_cell_list), a.u8(u.field_108);
    bytes8(a, u.field_121), bytes8(a, u.field_129), bytes8(a, u.field_131), a.u8(u.field_139);
    a.u32(u.field_11c), a.u8(u.field_120), ref_list(a, u.passengers), a.f32(u.field_16c), a.f32(u.field_170);
    a.vec(u.guns, [&](auto& g) { a.f32(g.angle), a.f32(g.target), a.u16(g.reload); });
}

template <class A, class T>
void house(A& a, T& h) {
    a.u8(h.field_04a), a.u8(h.field_08f), a.u8(h.field_090), a.u8(h.field_0b9), u32_list(a, h.upgrade_slots);
    a.u16(h.num), a.u16(h.type), a.i32(h.map_x), a.i32(h.map_y), a.f32(h.hp), a.u8(h.is_destroyed);
    a.u16(h.queue_count), u16_list(a, h.queue), a.u32(h.generation), a.f32(h.x), a.f32(h.y), a.f32(h.exit_x);
    a.f32(h.exit_y), a.u16(h.field_020), a.u16(h.units_inside), ref_list(a, h.inside), a.u8(h.field_030);
    a.f32(h.rally_x), a.f32(h.rally_y), a.f32(h.field_044), a.u16(h.field_05c), a.f32(h.field_060);
    a.u8(h.field_064), a.u8(h.field_065), a.u16(h.capacity), a.u16(h.max_hp), a.u8(h.field_06a);
    a.u8(h.field_06c.kind), a.u8(h.field_06c.index), a.f32(h.field_070), a.u8(h.field_074), a.f32(h.field_078);
    a.f32(h.field_07c), a.f32(h.field_080), a.u8(h.field_084), unit_ref(a, h.field_088), a.u8(h.field_08c);
    a.u32(h.field_0b4), a.b8(h.has_field_0b0), a.u8(h.field_0b8), a.i32(h.field_0a8), a.u8(h.selected);
    a.u8(h.graphic_phase), a.u8(h.graphic_stage), a.u8(h.depth_phase), a.u8(h.depth_stage), a.u32(h.field_0ac);
    unit_ref(a, h.field_0b0);
}

template <class A, class T>
void group(A& a, T& g) {
    a.u32(g.serial), ref_list(a, g.members);
    a.vec(g.slots, [&](auto& v) { point(a, v); });
    a.vec(g.wp, [&](auto& v) { point(a, v); });
    u8_list(a, g.wp_orient), a.u16(g.wp_count), a.u16(g.wp_next), a.u16(g.live), a.u16(g.quorum), a.u8(g.type);
    a.u8(g.spacing), a.u8(g.cols), a.u16(g.leader), a.u8(g.keep_spacing), a.f32(g.target_fx), a.f32(g.target_fy);
    a.u8(g.slow), a.u8(g.arrange_orient), a.f32(g.spacing_override), a.u16(g.wait), a.u8(g.queued), a.u8(g.selected);
    a.u8(g.ai_owned), a.i32(g.field_048), a.u8(g.large), a.u8(g.retries), a.u16(g.blocked), a.u8(g.has_mc7);
    a.u8(g.has_mc10), a.u8(g.arg3), a.u8(g.player), a.u8(g.kind), a.u8(g.target_player), a.u16(g.target_id);
    a.i32(g.tx), a.i32(g.ty), a.u8(g.stepping), a.u16(g.half), a.u8(g.dead), a.f32(g.speed), a.f32(g.speed_factor);
    a.u8(g.orient), a.u16(g.capacity), a.u8(g.field_069), u8_list(a, g.field_0c);
    a.vec(g.follow_up, [&](auto& f) { bytes8(a, f); });
}

template <class A, class T>
void command_node(A& a, T& n) {
    a.u32(n.seq), a.u16(n.cls), a.u8(n.cmd), a.f32(n.x), a.f32(n.y), unit_ref(a, n.unit), a.u32(n.group);
    ref_list(a, n.list), a.u8(n.a3), a.u8(n.type), a.u8(n.cols), a.u8(n.kind), a.i32(n.px), a.i32(n.py);
    a.u8(n.tplayer), a.u16(n.tid), a.u32(n.a2c);
}

template <class A, class T>
void ai_script(A& a, T& s) {
    words(a, s.want_u), words(a, s.want_h), a.u8(s.nsteps), a.u8(s.step), a.u16(s.entry);
    auto steps = [&](auto& lists) {
        a.vec(lists, [&](auto& l) { a.vec(l, [&](auto& e) { a.u8(e.kind), a.u16(e.type), a.u16(e.count); }); });
    };
    steps(s.main), steps(s.list2);
    a.i32(s.base_x), a.i32(s.base_y), a.i32(s.field_660), a.i32(s.field_664);
    a.vec(s.items, [&](auto& v) { a.u64(v); });
    a.i32(s.field_6f4), a.i32(s.field_6f8), a.i32(s.field_6fc), a.i32(s.field_704), a.i32(s.field_708);
    a.i32(s.field_70c);
}

// A mission event block (MisBlock, docs/missions.md 3.2) with all its fields, the texts and dialogs included.
template <class A, class T>
void mis_action(A& a, T& m) {
    a.u32(m.lead), a.u32(m.kind);
    for (auto& v : m.ints) a.i32(v);
    for (auto& v : m.floats) a.f32(v);
    a.u16(m.player);
}

template <class A, class T>
void mis_block(A& a, T& b) {
    a.u8(b.done), a.u16str(b.text_b), a.u16str(b.text_c), a.u16str(b.text_uc), a.u8(b.id);
    for (auto& v : b.prereq) a.u8(v);
    a.u8(b.open_prereq), a.u16(b.condition), a.f32(b.x), a.f32(b.y), a.u16(b.unit_type), a.u16(b.house_type);
    a.i32(b.timer), a.u8(b.advance), a.u8(b.stage), a.i32(b.n), a.u8(b.win), a.u8(b.lose);
    mis_action(a, b.action);
    for (auto& d : b.dialogs) {
        a.b8(d.on), a.u16str(d.title), a.u16str(d.text), a.u8(d.button1_picture), a.u8(d.button1_next);
        a.u8(d.button2_picture), a.u8(d.button2_next);
        mis_action(a, d.action);
    }
    a.i32(b.countdown);
}

template <class A, class T>
void player(A& a, T& p) {
    a.u8(p.nation), a.u8(p.ai_type), a.u8(p.control_type), a.u8(p.is_lost), a.i32(p.units_alive);
    a.i32(p.units_lost), a.i32(p.units_killed), a.u16(p.unit_cap);
    a.f64(p.gold), a.f64(p.wood), a.f64(p.iron), a.f64(p.oil), a.f64(p.food);
    a.f64(p.gold_stat), a.f64(p.wood_stat), a.f64(p.iron_stat), a.f64(p.oil_stat), a.f64(p.food_stat);
    a.vec(p.history, [&](auto& q) { a.f64(q.gold), a.f64(q.wood), a.f64(q.iron), a.f64(q.oil), a.f64(q.food); });
    words(a, p.unit_count), bytes8(a, p.houses_done);
    a.opt(p.script, [&](auto& s) { ai_script(a, s); });
    // The unit slots as they are (dead slots too), the list head and the slot tables: a load gives the same slots,
    // so a unit made after it takes the same slot as without the save.
    a.vec(p.unit_slots, [&](auto& u) { unit(a, u); });
    a.u32(p.first_unit), a.u32(p.num_units);
    a.vec(p.houses, [&](auto& h) { house(a, h); });
    a.vec(p.commands, [&](auto& n) { command_node(a, n); });
    a.vec(p.groups, [&](auto& g) { group(a, g); });
    a.u32(p.next_group), a.u32(p.ai_next_serial), a.i32(p.ai_counter), a.u8(p.ai_kind), a.u8(p.ai_level);
    a.i32(p.ai_retry), a.u8(p.ai_all), a.i32(p.ai_countdown), bytes8(a, p.field_9b2a98);
    u16_list(a, p.free_ids), a.u16(p.next_id);
    a.f32(p.power_used), a.f32(p.power_6c4), a.f32(p.power_6e4), a.f32(p.power_made);
    a.u8(p.field_9b2b38), a.u8(p.field_9b2b39), a.u8(p.field_9b2b3a), a.u8(p.field_9b2b3b), a.u8(p.field_9b2b3c);
    a.u8(p.field_9b2b3d), a.u16(p.history_index), u8_list(a, p.zone_grid), a.u16(p.zone_width);
    bytes8(a, p.is_ally), a.f32(p.shell_spread), a.f32(p.accuracy_factor), a.u8(p.field_9b2d94);
    a.u8(p.field_9b2d9c), bytes8(a, p.research_flags), bytes8(a, p.research_9b39f0), bytes8(a, p.research_9b3950);
    a.f32(p.factor_684), a.f32(p.factor_664), a.f32(p.factor_644), a.f32(p.factor_744), a.f32(p.factor_724);
    a.f32(p.factor_9b3338), a.f32(p.factor_9e28fc), a.f32(p.factor_9b32d8);
    a.u8(p.mint_done), a.i32(p.mint_count), a.f32(p.gold_factor);
    a.i32(p.houses_lost), a.i32(p.houses_destroyed), a.i32(p.houses_built);
    a.arr(p.start_res, [&](auto& v) { a.f64(v); });
    a.arr(p.mined, [&](auto& v) { a.f64(v); });
    floats(a, p.res_sold), floats(a, p.res_bought);
    a.u8(p.market_auto), a.u8(p.market_buy), floats(a, p.market_sell), a.f32(p.market_received);
    a.f32(p.score), a.u8(p.house_array_freed), a.u32(p.stat_colour), a.u32(p.play_time), a.i64(p.last_time);
    a.u16str(p.name), a.u8(p.colour), a.i8(p.team), u32_list(a, p.hist_time);
    a.u8(p.sel_class6), a.u8(p.sel_class7), a.u16(p.sel_houses), a.u16(p.sel_house), a.u16(p.num_selected);
    a.arr(p.control_groups, [&](auto& g) { a.u8(g.kind), u16_list(a, g.ids); });
    bytes8(a, p.research_9b3860), bytes8(a, p.research_9b3770), a.u32(p.special_shells);
    a.f32(p.factor_9b33b8), a.f32(p.factor_9b3318), a.f32(p.factor_9b32f8);
    a.u8(p.field_20099), a.u8(p.field_20170), a.u8(p.field_2cc9a);
    u32_list(a, p.id_slots), u32_list(a, p.free_slots), u32_list(a, p.freed_slots);
}

template <class A, class T>
void map_fields(A& a, T& m) {
    a.vec(m.terrain, [&](auto& c) {
        a.u16(c.tile), a.u8(c.shade), a.u8(c.unused);
        words(a, c.sub);
    });
    a.i32(m.water.w), a.i32(m.water.h), u8_list(a, m.water.g);
    a.vec(m.water.recs, [&](auto& r) { unit_ref(a, r.unit), a.u16(r.i), a.u16(r.j); });
    a.u16(m.width), a.u16(m.height), u8_list(a, m.heights), u16_list(a, m.objects), u8_list(a, m.flags);
    u16_list(a, m.obstacle);
    a.vec(m.neutral_houses, [&](auto& n) { a.u8(n.type), a.i32(n.map_x), a.i32(n.map_y); });
    a.vec(m.specials, [&](auto& o) { a.u16(o.x), a.u16(o.y), a.f32(o.f_a); });
    a.u16(m.special_capacity), u16_list(a, m.pass_land), u16_list(a, m.pass_large), u8_list(a, m.weight);
    a.vec(m.cell_units, [&](auto& l) { ref_list(a, l); });
    u16_list(a, m.wave);  // the search's labels: cleared by every search, kept so nothing can differ
}

template <class A, class T>
void shell(A& a, T& v) {
    a.f32(v.x0), a.f32(v.x1), a.f32(v.y0), a.f32(v.y1), a.f32(v.z0), a.f32(v.z1), a.f32(v.cx), a.f32(v.cy);
    a.f32(v.cz), a.f64(v.g), a.f64(v.vx), a.f64(v.vy), a.f64(v.v0), a.f64(v.vz), a.f64(v.flight), a.f32(v.substep);
    a.f32(v.range), a.f32(v.a4), a.u8(v.fail), a.u8(v.player), unit_ref(a, v.owner), a.u8(v.gas), a.u8(v.no_effect);
    a.i32(v.radius), a.u8(v.sound), a.i32(v.sound_tick), a.i32(v.damage), a.u16(v.delay), a.u8(v.kind);
    a.u8(v.fragments), a.i32(v.flown);
}

// Every field of GameState but four: the wall clock (`clock`, set by the caller), RngTable (filled again from the
// seed), the house depth maps (game files, loaded by the caller) and the lists the last step() wrote for the trace and
// the UI (orders, executed, view_inputs, deselected, events), which every step() empties before it writes them.
template <class A, class T>
void game_state(A& a, T& s, std::uint32_t version = kSimVersion) {
    a.u32(s.tick), a.u8(s.speed_ms), a.u8(s.my_player), a.u8(s.short_anims), a.u32(s.rng_seed), a.u16(s.rng_index);
    a.u32(s.crt_holdrand), a.u16(s.map_width), a.u16(s.map_height);
    a.vec(s.players, [&](auto& p) { player(a, p); });
    a.arr(s.unit_types, [&](auto& nation) { a.arr(nation, [&](auto& t) { unit_type(a, t); }); });
    a.arr(s.house_footprints, [&](auto& nation) { a.arr(nation, [&](auto& f) { a.u8(f.a), a.u8(f.b); }); });
    a.arr(s.house_types, [&](auto& nation) { a.arr(nation, [&](auto& t) { house_type(a, t); }); });
    a.u32(s.next_serial), a.i32(s.climate);
    map_fields(a, s.map);
    a.vec(s.path_cache, [&](auto& r) {
        a.i32(r.sx), a.i32(r.sy), a.i32(r.ex), a.i32(r.ey), a.u32(r.tick);
        u8_list(a, r.path);
    });
    a.i32(s.path_budget_used), a.i32(s.path_budget_max), a.i32(s.deferred_path_count);
    a.i32(s.deferred_human_count), a.u32(s.next_node_seq), a.u8(s.formation_budget);
    a.u8(s.flag_8ab825), a.u8(s.flag_8ab824), a.u8(s.flag_8ef4d4), a.u8(s.flag_8ef4d5), a.u8(s.flag_8ef4d6);
    a.u8(s.flag_8e836e), a.u8(s.skirmish_flag), a.u8(s.land_map), a.u8(s.flag_9b2df9), a.u16(s.sel_8ab774);
    a.u16(s.sel_8ab770), a.str(s.map_name);
    a.arr(s.slots, [&](auto& r) { a.u16str(r.name), a.u8(r.nation), a.u8(r.colour), a.u8(r.team), a.u8(r.level); });
    a.u8(s.slot_count), u32_list(a, s.decaying_fields), u32_list(a, s.port_sites);
    a.arr(s.mine_sites, [&](auto& c) { point(a, c); });
    a.u8(s.mine_site_count), a.i32(s.grid_w), a.i32(s.grid_h);
    a.vec(s.unit_cells, [&](auto& l) { ref_list(a, l); });
    a.vec(s.house_cells, [&](auto& l) { a.vec(l, [&](auto& r) { house_ref(a, r); }); });
    a.vec(s.shells, [&](auto& v) { shell(a, v); });
    a.vec(s.large_cells_clear, [&](auto& c) { point(a, c); });
    a.vec(s.large_cells_set, [&](auto& c) { point(a, c); });
    a.vec(s.bullets, [&](auto& b) {
        a.f32(b.x), a.f32(b.y), a.f32(b.z), a.u16(b.field_00c), a.u16(b.delay);
        unit_ref(a, b.owner);
    });
    ref_list(a, s.ships);
    a.map(s.graphic_refs, [&](auto& k) { a.u16(k); }, [&](auto& v) { a.u32(v); });
    a.u8(s.fog_553ae0), a.b8(s.original_rules), a.u8(s.paused), a.u8(s.game_result), a.u8(s.end_message);
    a.vec(s.ship_sprites, [&](auto& v) { a.u32(v.refs), a.u8(v.state), a.u32(v.ready_tick); });
    a.arr(s.object_blocking, [&](auto& t) { u8_list(a, t); });
    a.u32(s.cmd_counter), a.u8(s.cur_player);
    // The fog's circles of the last rebuild (what the player sees until the next one, tick % 30).
    a.b8(s.fog.built), a.b8(s.fog.on), a.u32(s.fog.tick);
    a.vec(s.fog.circles,
          [&](auto& c) { a.i32(c.map_x), a.i32(c.map_y), a.f32(c.x), a.f32(c.y), a.u8(c.sight), a.b8(c.unit); });
    // Diagnostics that the traces print (oseq numbers, the unported calls), so a run on from a load writes the same.
    a.map(s.unported, [&](auto& k) { a.u32(k); }, [&](auto& v) { a.u32(v.count), a.u32(v.first_tick); });
    a.u32(s.order_seq), a.b8(s.log_moves), a.u32(s.log_parent);
    if (version >= 2) {
        a.vec(s.gas_clouds, [&](auto& g) {
            a.u32(g.kind), a.i32(g.x), a.i32(g.y), a.i32(g.life), a.u16(g.max_radius), a.u16(g.step), a.u8(g.puffs);
            a.i32(g.damage), a.i32(g.damage_radius), a.u8(g.type), a.i16(g.radius);
        });
    }
    if (version >= 3) {
        for (auto& o : s.map.specials) a.u8(o.kind), a.u8(o.alive), a.f32(o.hp);
    }
    if (version >= 4) a.str(s.mission_path);
    if (version >= 5) {  // the mission event blocks, the reveal list and the end text (sim/mission_events.hpp)
        for (auto& p : s.players) a.vec(p.blocks, [&](auto& b) { mis_block(a, b); });
        for (auto& r : s.reveal) a.i32(r.x), a.i32(r.y), a.u8(r.radius);
        a.u16str(s.end_text);
    }
    if (version >= 6) {  // the Lua mission (script/lua_manager.hpp); the fog's allies flag beside the fog
        a.u8(s.lua_mission), a.u8(s.script_active), a.u32(s.script_winner);
        a.map(
            s.script_groups, [&](auto& k) { a.str(k); },
            [&](auto& g) {
                a.u8(g.player);
                a.map(g.members, [&](auto& k) { a.u16(k); }, [&](auto& r) { unit_ref(a, r); });
            });
        a.map(
            s.script_timers, [&](auto& k) { a.str(k); },
            [&](auto& t) { a.str(t.code), a.f32(t.base), a.f32(t.period), a.u32(t.repeats), a.u32(t.fired); });
        a.vec(s.script_keys, [&](auto& k) { a.str(k); });
        a.u8(s.show_fog_allies);
    }
    if (version >= 7) a.u8(s.net_game);  // docs/multiplayer.md
    if (version >= 8) {
        for (auto& p : s.players) {
            for (auto& u : p.unit_slots) a.u8(u.nation);
            for (auto& h : p.houses) a.u8(h.nation);
        }
    }
    if (version >= 9) {  // slice MP9: a network game's computer control of a human's player (docs/multiplayer.md)
        for (auto& p : s.players) a.u8(p.net_ai), bytes8(a, p.net_kept);
    }
    if (version >= 10) {  // B277: a fighter's ground target to go back to (our rules)
        for (auto& p : s.players) {
            for (auto& u : p.unit_slots) unit_ref(a, u.return_target);
        }
    }
    if (version >= 12) a.b8(s.plus_rules);  // Entente Engine Plus (docs/your-version.md)
    if (version >= 13) {                    // Plus's market price index (E2, docs/plus-balance.md 3)
        for (auto& p : s.players) {
            for (auto& v : p.market_index) a.f32(v);
        }
    }
    if (version >= 14) {  // Plus's Delousing count (F6, docs/plus-balance.md 2)
        for (auto& p : s.players) a.u8(p.plus_delousing);
    }
    if (version >= 15) {  // Plus's Building Academy researches (F7, docs/plus-balance.md 2)
        for (auto& p : s.players) a.u8(p.plus_build_level);
    }
    if (version >= 16) {  // Plus's call-in strikes (docs/decisions.md 34): the cooldowns and the recon flight
        for (auto& p : s.players) {
            for (auto& v : p.plus_strike_ready) a.u32(v);
            a.i32(p.plus_recon_x), a.i32(p.plus_recon_y), a.u32(p.plus_recon_end);
        }
    }
    if (version >= 17) {  // B317: the Shift-click order queue of every unit slot (our rules)
        for (auto& p : s.players) {
            for (auto& u : p.unit_slots) {
                a.vec(u.order_queue, [&](auto& e) {
                    a.u8(e.kind), a.f32(e.x), a.f32(e.y), a.i32(e.px), a.i32(e.py), unit_ref(a, e.unit);
                    house_ref(a, e.house);
                });
            }
        }
    }
}

template <class A, class T>
void info(A& a, T& i) {
    a.str(i.name), a.i64(i.saved_at), a.u32(i.tick), a.str(i.map_name), a.u8(i.my_player), a.u8(i.players);
    a.str(i.player), a.u32(i.play_time), a.u32(i.hash_version), a.u64(i.state_hash);
}

template <class A, class T>
void view(A& a, T& v) {
    a.f64(v.cam_x), a.f64(v.cam_y), a.i32(v.view_col), a.i32(v.view_row), a.u8(v.sel_player);
    ref_list(a, v.sel_units);
    a.vec(v.sel_flagged, [&](auto& r) { house_ref(a, r); });
    a.opt(v.sel_house, [&](auto& r) { house_ref(a, r); });
    a.u8(v.speed), a.u8(v.minimap_shown), a.u8(v.show_health), a.i32(v.tab_index);
}

template <class A, class T>
void setup(A& a, T& s, std::uint32_t version = kSetupVersion) {
    a.str(s.map), a.u8(s.players);
    a.arr(s.slots, [&](auto& l) { a.u8(l.level), a.u8(l.nation), a.u8(l.colour), a.u8(l.team); });
    a.u8(s.resources), a.u32(s.seed);
    a.arr(s.comp_names, [&](auto& n) { a.u16str(n); });
    a.arr(s.economic, [&](auto& v) { a.b8(v); });
    a.arr(s.military, [&](auto& v) { a.b8(v); });
    if (version >= 2) a.b8(s.plus);
}

template <class A, class T>
void net_seats(A& a, T& n) {
    a.u64(n.game_uid);
    a.vec(n.seats, [&](auto& s) {
        a.u8(s.slot), a.u8(s.player), a.str(s.name), a.u64(s.token_check), a.u8(s.left), a.u8(s.ai);
    });
}

// A generated map (chunk GRND): every field of MapData, in its order.
template <class A, class T>
void map_house(A& a, T& h) {
    a.i32(h.player), a.i32(h.cell_x), a.i32(h.cell_y), a.u8(h.type), a.u16(h.build_state), a.f32(h.hp);
    a.u8(h.field_1c), a.u32(h.port_side);
}

template <class A, class T>
void map_data(A& a, T& m) {
    a.i32(m.width), a.i32(m.height);
    a.vec(m.cells, [&](auto& c) {
        a.u8(c.height), a.u16(c.tile), a.u8(c.shade), a.u8(c.unknown4), words(a, c.subtile), a.u16(c.object);
    });
    a.vec(m.player_names, [&](auto& n) { a.str(n); });
    a.vec(m.player_nations, [&](auto& v) { a.u8(v); });
    a.vec(m.player_bytes29, [&](auto& v) { a.u8(v); });
    words(a, m.house_counts), words(a, m.unit_counts);
    a.vec(m.houses, [&](auto& h) { map_house(a, h); });
    a.vec(m.units,
          [&](auto& u) { a.i32(u.player), a.f32(u.world_x), a.f32(u.world_y), a.u16(u.type), a.u8(u.orient_plus1); });
    a.i32(m.extra_unit_records);
    a.vec(m.decorations, [&](auto& d) {
        a.u8(d.type), a.u16(d.f01), a.f32(d.f03), a.u8(d.f07), a.f32(d.f08), a.f32(d.f0c), a.u16(d.f10);
        a.u16(d.f12), a.u16(d.f14), a.u8(d.f16), a.u8(d.f17), a.f32(d.world_x), a.f32(d.world_y), bytes8(a, d.rest);
    });
    a.vec(m.obstacle_grid, [&](auto& v) { a.u16(v); });
    a.vec(m.routes, [&](auto& r) {
        a.u16(r.v);
        a.vec(r.lists, [&](auto& l) { a.vec(l, [&](auto& v) { a.u16(v); }); });
    });
    a.vec(m.neutral_houses, [&](auto& h) { map_house(a, h); });
    a.u16(m.ignored_unit_records), a.u16(m.special_capacity);
    a.vec(m.specials,
          [&](auto& sp) { a.u8(sp.kind), a.u16(sp.x), a.u16(sp.y), a.f32(sp.f_a), a.f32(sp.f_b), a.u8(sp.b); });
    a.vec(m.port_sites, [&](auto& v) { a.u32(v); });
}

// ---- chunks ----

void put_u32(Bytes& b, std::uint32_t v) { Writer{b}.u32(v); }

void put_chunk(Bytes& out, const char (&tag)[5], std::uint32_t version, const Bytes& payload) {
    out.insert(out.end(), tag, tag + 4);
    put_u32(out, version);
    put_u32(out, static_cast<std::uint32_t>(payload.size()));
    put_u32(out, static_cast<std::uint32_t>(crc32(0, payload.data(), static_cast<uInt>(payload.size()))));
    out.insert(out.end(), payload.begin(), payload.end());
}

struct Chunk {
    std::string tag;
    std::uint32_t version = 0;
    ByteSpan payload;
};

// The chunks of a file, checked: magic, format version, sizes, checksums.
bool split_chunks(ByteSpan file, std::vector<Chunk>* out, std::string* error) {
    if (!OwnSave_IsOwn(file)) {
        *error = "This is not a save file of Antanta.";
        return false;
    }
    Cursor c{file};
    c.skip(sizeof kMagic);
    const std::uint32_t format = c.u32(), n = c.u32();
    if (!c.ok) {
        *error = "This save file is incomplete.";
        return false;
    }
    if (format > kOwnSaveFormat) {
        *error = "This save was made by a newer version of Antanta (file format " + std::to_string(format) +
                 "; this version reads up to " + std::to_string(kOwnSaveFormat) + ").";
        return false;
    }
    if (format == 0) {
        *error = "This save file is damaged (file format 0).";
        return false;
    }
    for (std::uint32_t i = 0; i < n; ++i) {
        Chunk k;
        const ByteSpan tag = c.take(4);
        k.version = c.u32();
        const std::uint32_t size = c.u32(), crc = c.u32();
        k.payload = c.take(size);
        if (!c.ok) {
            *error = "This save file is incomplete (it ends inside part " + std::to_string(i + 1) + " of " +
                     std::to_string(n) + ").";
            return false;
        }
        k.tag.assign(tag.begin(), tag.end());
        if (crc32(0, k.payload.data(), static_cast<uInt>(k.payload.size())) != crc) {
            *error = "This save file is damaged (the check sum of part " + k.tag + " does not match).";
            return false;
        }
        out->push_back(std::move(k));
    }
    if (c.pos != file.size()) {
        *error = "This save file is damaged (" + std::to_string(file.size() - c.pos) + " bytes after its end).";
        return false;
    }
    return true;
}

const Chunk* find_chunk(const std::vector<Chunk>& chunks, const char* tag) {
    for (const Chunk& k : chunks) {
        if (k.tag == tag) return &k;
    }
    return nullptr;
}

// Reads one chunk's payload with walker `f`; the whole payload must be used.
template <class F>
bool read_chunk(const Chunk& k, std::uint32_t known, F&& f, std::string* error) {
    if (k.version > known) {
        *error = "This save was made by a newer version of Antanta (part " + k.tag + " version " +
                 std::to_string(k.version) + "; this version reads up to " + std::to_string(known) + ").";
        return false;
    }
    Reader r{Cursor{k.payload}, {}};
    f(r);
    if (r.good() && r.c.pos != k.payload.size())
        r.fail(std::to_string(k.payload.size() - r.c.pos) + " bytes left over");
    if (!r.good()) {
        *error = "This save file is damaged (part " + k.tag + ": " +
                 (r.what.empty() ? std::string("it ends too early") : r.what) + ").";
        return false;
    }
    return true;
}

bool read_info(const std::vector<Chunk>& chunks, OwnSaveInfo* out, std::string* error) {
    const Chunk* k = find_chunk(chunks, "INFO");
    if (k == nullptr) {
        *error = "This save file is damaged (it has no INFO part).";
        return false;
    }
    return read_chunk(*k, kInfoVersion, [&](Reader& r) { info(r, *out); }, error);
}

// Chunk GRND: the raw size, the zlib stream of map_data; the cells must fill the map.
bool read_ground(const Chunk& k, MapData* m, std::string* error) {
    if (k.version > kGroundVersion) {
        *error = "This save was made by a newer version of Antanta (part GRND version " + std::to_string(k.version) +
                 "; this version reads up to " + std::to_string(kGroundVersion) + ").";
        return false;
    }
    Cursor c{k.payload};
    const std::uint32_t raw_size = c.u32();
    std::string inflate_error;
    std::optional<Bytes> raw = c.ok ? inflate_exact(k.payload.subspan(4), raw_size, &inflate_error) : std::nullopt;
    if (!raw) {
        *error = "This save file is damaged (the generated map does not unpack" +
                 (inflate_error.empty() ? std::string() : ": " + inflate_error) + ").";
        return false;
    }
    if (!read_chunk(Chunk{k.tag, k.version, *raw}, kGroundVersion, [&](Reader& r) { map_data(r, *m); }, error))
        return false;
    if (m->width <= 0 || m->height <= 0 || m->width > 4096 || m->height > 4096 ||
        m->cells.size() != static_cast<std::size_t>(m->width) * static_cast<std::size_t>(m->height)) {
        *error = "This save file is damaged (the generated map has " + std::to_string(m->cells.size()) + " cells for " +
                 std::to_string(m->width) + " x " + std::to_string(m->height) + ").";
        return false;
    }
    return true;
}

}  // namespace

bool OwnSave_IsOwn(ByteSpan file) {
    return file.size() >= sizeof kMagic && std::memcmp(file.data(), kMagic, sizeof kMagic) == 0;
}

OwnSavePending OwnSave_Prepare(const GameState& s, const OwnSaveMeta& meta) {
    OwnSaveInfo in = meta.info;
    in.tick = s.tick, in.map_name = s.map_name, in.my_player = s.my_player;
    in.players = static_cast<std::uint8_t>(s.players.size());
    in.play_time = s.my_player < s.players.size() ? s.players[s.my_player].play_time : 0;
    in.hash_version = GameState_HashVersion(), in.state_hash = GameState_Hash(s);

    OwnSavePending p;
    Bytes& out = p.head;
    out.assign(kMagic, kMagic + sizeof kMagic);
    put_u32(out, kOwnSaveFormat);
    put_u32(out, 3 + (meta.setup ? 1 : 0) + (meta.ground ? 1 : 0) + (meta.lua ? 1 : 0) + (meta.net ? 1 : 0));
    Bytes part;
    Writer w{part};
    info(w, in);
    put_chunk(out, "INFO", kInfoVersion, part);
    part.clear();
    view(w, meta.view);
    put_chunk(out, "VIEW", kViewVersion, part);
    if (meta.setup) {
        part.clear();
        setup(w, *meta.setup);
        put_chunk(out, "SETU", kSetupVersion, part);
    }
    // The state, zlib-compressed: its size, then the stream (compress2 at a fixed level gives the same bytes for the
    // same state, so a save, load and save again is byte for byte the same file).
    Writer raw{p.raw};
    game_state(raw, s);
    if (meta.ground) {
        Writer ground{p.ground};
        map_data(ground, *meta.ground);
    }
    if (meta.lua) {
        part.clear();
        w.str(*meta.lua);
        put_chunk(p.tail, "LUA ", kLuaVersion, part);
    }
    if (meta.net) {
        part.clear();
        OwnSaveNet n = *meta.net;
        net_seats(w, n);
        put_chunk(p.tail, "NET ", kNetVersion, part);
    }
    return p;
}

namespace {

// The raw size, then the zlib stream (compress2 at a fixed level: the same bytes for the same input). Empty when zlib
// fails.
Bytes packed_part(const Bytes& raw) {
    uLongf packed_size = compressBound(static_cast<uLong>(raw.size()));
    Bytes packed(packed_size);
    if (compress2(packed.data(), &packed_size, raw.data(), static_cast<uLong>(raw.size()), 6) != Z_OK) return {};
    packed.resize(packed_size);
    Bytes out;
    put_u32(out, static_cast<std::uint32_t>(raw.size()));
    out.insert(out.end(), packed.begin(), packed.end());
    return out;
}

}  // namespace

Bytes OwnSave_Finish(const OwnSavePending& p) {
    const Bytes sim = packed_part(p.raw);
    if (sim.empty()) return {};
    Bytes out = p.head;
    put_chunk(out, "SIM ", kSimVersion, sim);
    if (!p.ground.empty()) {
        const Bytes ground = packed_part(p.ground);
        if (ground.empty()) return {};
        put_chunk(out, "GRND", kGroundVersion, ground);
    }
    out.insert(out.end(), p.tail.begin(), p.tail.end());
    return out;
}

Bytes OwnSave_Write(const GameState& s, const OwnSaveMeta& meta) { return OwnSave_Finish(OwnSave_Prepare(s, meta)); }

std::optional<OwnSaveNet> OwnSave_ReadNet(ByteSpan file, std::string* error) {
    std::vector<Chunk> chunks;
    error->clear();
    if (!split_chunks(file, &chunks, error)) return std::nullopt;
    const Chunk* k = find_chunk(chunks, "NET ");
    OwnSaveNet n;
    if (k == nullptr || !read_chunk(*k, kNetVersion, [&](Reader& r) { net_seats(r, n); }, error)) return std::nullopt;
    return n;
}

std::optional<OwnSaveInfo> OwnSave_ReadInfo(ByteSpan file, std::string* error) {
    std::vector<Chunk> chunks;
    OwnSaveInfo in;
    if (!split_chunks(file, &chunks, error) || !read_info(chunks, &in, error)) return std::nullopt;
    return in;
}

bool OwnSave_Read(ByteSpan file, GameState* s, OwnSaveMeta* meta, std::string* error) {
    std::vector<Chunk> chunks;
    if (!split_chunks(file, &chunks, error) || !read_info(chunks, &meta->info, error)) return false;
    meta->view = OwnSaveView{};
    if (const Chunk* k = find_chunk(chunks, "VIEW")) {
        if (!read_chunk(*k, kViewVersion, [&](Reader& r) { view(r, meta->view); }, error)) return false;
    }
    meta->setup.reset();
    if (const Chunk* k = find_chunk(chunks, "SETU")) {
        SkirmishSetup su;
        if (!read_chunk(*k, kSetupVersion, [&](Reader& r) { setup(r, su, k->version); }, error)) return false;
        meta->setup = std::move(su);
    }
    meta->net.reset();
    if (const Chunk* k = find_chunk(chunks, "NET ")) {
        OwnSaveNet n;
        if (!read_chunk(*k, kNetVersion, [&](Reader& r) { net_seats(r, n); }, error)) return false;
        meta->net = std::move(n);
    }
    meta->ground.reset();
    if (const Chunk* k = find_chunk(chunks, "GRND")) {
        MapData m;
        if (!read_ground(*k, &m, error)) return false;
        meta->ground = std::make_shared<const MapData>(std::move(m));
    }
    meta->lua.reset();
    if (const Chunk* k = find_chunk(chunks, "LUA ")) {
        std::string lua;
        if (!read_chunk(*k, kLuaVersion, [&](Reader& r) { r.str(lua); }, error)) return false;
        meta->lua = std::move(lua);
    }
    const Chunk* k = find_chunk(chunks, "SIM ");
    if (k == nullptr) {
        *error = "This save file is damaged (it has no game state).";
        return false;
    }
    if (k->version > kSimVersion) {
        *error = "This save was made by a newer version of Antanta (game state version " + std::to_string(k->version) +
                 "; this version reads up to " + std::to_string(kSimVersion) + ").";
        return false;
    }
    Cursor c{k->payload};
    const std::uint32_t raw_size = c.u32();
    if (!c.ok) {
        *error = "This save file is damaged (the game state is empty).";
        return false;
    }
    std::string inflate_error;
    std::optional<Bytes> raw = inflate_exact(k->payload.subspan(4), raw_size, &inflate_error);
    if (!raw) {
        *error = "This save file is damaged (the game state does not unpack: " + inflate_error + ").";
        return false;
    }
    Chunk sim{k->tag, k->version, *raw};
    // What the file does not hold starts empty (the reader sets every other field).
    s->clock = {}, s->house_depth_maps.clear(), s->orders.clear(), s->executed.clear(), s->view_inputs.clear();
    s->deselected.clear(), s->events.clear(), s->gas_clouds.clear();
    s->flow_fields.clear(), s->flow_open.clear(), s->flow_weight.clear(), s->flow_log.clear();  // the route fields'
    s->flow_grid_gen = 0xffffffff;                                                              // cache, built anew
    s->land_region.clear();  // Land_Region's cache, built anew
    if (!read_chunk(sim, kSimVersion, [&](Reader& r) { game_state(r, *s, k->version); }, error)) return false;

    const std::uint16_t index = s->rng_index;
    const std::uint32_t holdrand = s->crt_holdrand;
    RNG_FillTable(*s);
    s->rng_index = index, s->crt_holdrand = holdrand;
    // A Plus game of SIM version 12 was hashed without the market index (version 13), one of version 13 without the
    // Delousing count (version 14), one of version 14 without the Building Academy level (version 15), one of version
    // 15 without the strikes' cooldowns and recon flight (version 16), so its check value cannot match.
    const bool plus_before_index = s->plus_rules && k->version < 16;
    if (meta->info.hash_version == GameState_HashVersion() && !plus_before_index &&
        GameState_Hash(*s) != meta->info.state_hash) {
        *error = "This save file is damaged (the game state does not match its check value).";
        return false;
    }
    if (k->version < 11) {  // after the check: the file's hash was made with the old flag
        for (auto& nation : s->unit_types) {
            for (UnitType& t : nation) t.field_5e0 = UnitType_NoMeleeStepsBack(t);
        }
    }
    return true;
}

}  // namespace antanta::sim
