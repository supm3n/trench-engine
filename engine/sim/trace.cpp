#include "trace.hpp"

#include <zlib.h>

#include <algorithm>
#include <bit>
#include <cinttypes>
#include <cstdio>
#include <tuple>
#include <vector>

namespace antanta::sim {

namespace {

// Decimal (%.9g of the float widened to double, or nan/inf/-inf), TAB, raw bits.
std::string f32(float v) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(v);
    char buf[64];
    if ((bits & 0x7f800000u) == 0x7f800000u) {
        std::snprintf(buf, sizeof buf, "%s\t%08" PRIx32, (bits & 0x7fffffu) ? "nan" : (bits >> 31 ? "-inf" : "inf"),
                      bits);
    } else {
        std::snprintf(buf, sizeof buf, "%.9g\t%08" PRIx32, static_cast<double>(v), bits);
    }
    return buf;
}

std::string f64(double v) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(v);
    char buf[80];
    if ((bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull) {
        std::snprintf(buf, sizeof buf, "%s\t%016" PRIx64,
                      (bits & 0xfffffffffffffull) ? "nan" : (bits >> 63 ? "-inf" : "inf"), bits);
    } else {
        std::snprintf(buf, sizeof buf, "%.17g\t%016" PRIx64, v, bits);
    }
    return buf;
}

struct Line {
    std::string s;
    void add(const std::string& v) { s += '\t', s += v; }
    void add(long long v) { add(std::to_string(v)); }
};

}  // namespace

std::string trace_header() {
    return "H\tformat\tantanta-trace\nH\tversion\t" + std::to_string(kTraceVersion) +
           "\nH\tsource\tengine\nH\texe_sha256\t-\nH\tfrida\t-\nH\tstarted\t-\nH\tforced_seed\tnone\nH\tgame_args\t-\n";
}

std::string trace_rng_fill(const GameState& s, std::uint32_t tick, bool saveload) {
    Line l{"R"};
    l.add(tick), l.add(s.rng_seed), l.add(0), l.add(saveload ? 1 : 0), l.add(std::string("-"));
    for (int i = 0; i < 4; ++i) l.add(i < static_cast<int>(s.rng_table.size()) ? s.rng_table[i] : 0);
    l.add(1);  // the table is filled by RNG_FillTable, so it equals srand(seed); rand() x4 by construction
    l.add(0);  // RngIndex right after the fill
    return l.s + "\n";
}

std::string trace_orders(const GameState& s) {
    std::string out;
    for (const Command& c : s.orders) out += Command_Print(c) + '\n';
    return out;
}

std::string trace_view(const GameState& s) {
    std::string out;
    for (const Command& c : s.view_inputs) out += Command_PrintView(c) + '\n';
    return out;
}

std::string trace_commands(const GameState& s) {
    std::string out;
    const std::string dash = "-";
    for (const CommandExec& e : s.executed) {
        const CommandNode& n = e.node;
        Line l{"C"};
        l.add(s.tick), l.add(e.seq), l.add(e.player), l.add(e.rng_index), l.add(0), l.add(dash), l.add(0xffff);
        l.add(n.cls), l.add(n.cmd), l.add(f32(n.x)), l.add(f32(n.y));
        const bool unit = n.unit.player != 0xff;
        l.add(unit ? static_cast<long long>(n.unit.id) : -1), l.add(unit ? static_cast<long long>(n.unit.serial) : -1);
        l.add(n.cmd == 0x2d || n.cmd == 0x2f ? 1 : 0);
        int rest = 8;
        if (n.cls == 2 && n.cmd == 7) {
            l.add(0xffff), l.add(n.a2c);
            rest = 6;
        }
        for (int i = 0; i < rest; ++i) l.add(dash);
        out += l.s + "\n";
    }
    return out;
}

std::string trace_state_hash(const GameState& s) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "G\t%" PRIu32 "\t%016" PRIx64 "\n", s.tick, GameState_Hash(s));
    return buf;
}

std::string trace_tick(const GameState& s, std::uint32_t tick_enter) {
    std::string out;
    std::size_t unit_count = 0;
    for (const Player& p : s.players) unit_count += p.num_units;

    std::string players, houses;
    std::vector<std::tuple<int, int, std::uint32_t, std::string>> units;  // (player, id, serial, line)
    units.reserve(unit_count);
    for (std::size_t pi = 0; pi < s.players.size(); ++pi) {
        const Player& p = s.players[pi];
        const auto pn = static_cast<long long>(pi);
        Line l{"P"};
        l.add(s.tick), l.add(pn), l.add(p.nation), l.add(p.ai_type), l.add(p.control_type), l.add(p.is_lost);
        l.add(p.units_alive), l.add(p.units_lost), l.add(p.units_killed);
        l.add(static_cast<long long>(p.num_units)), l.add(static_cast<long long>(p.houses.size()));
        l.add(p.unit_cap);
        l.add(f64(p.gold)), l.add(f64(p.wood)), l.add(f64(p.iron)), l.add(f64(p.oil)), l.add(f64(p.food));
        players += l.s + "\n";
        for (std::uint32_t k = p.first_unit; k != kNoSlot; k = p.unit_slots[k].next) {
            const Unit& u = p.unit_slots[k];
            Line ul{"U"};
            ul.add(s.tick), ul.add(pn), ul.add(u.id), ul.add(u.serial), ul.add(u.type);
            ul.add(f32(u.x)), ul.add(f32(u.y)), ul.add(u.map_x), ul.add(u.map_y), ul.add(f32(u.hp));
            ul.add(u.orient), ul.add(f32(u.angle)), ul.add(f32(u.num_sprite)), ul.add(u.work), ul.add(u.num_work);
            ul.add(f32(u.dest_x)), ul.add(f32(u.dest_y));
            ul.add(u.player_unit_attack), ul.add(u.attack_unit_id), ul.add(u.player_house_attack);
            ul.add(u.in_house ? 1 : 0), ul.add(u.in_transport ? 1 : 0), ul.add(u.is_deleted);
            // version 3 fields (FORMAT.md): z, velocity, final point, path length and index, turn, stop flag
            ul.add(f32(u.z)), ul.add(f32(u.vel_x)), ul.add(f32(u.vel_y)), ul.add(f32(u.final_x)),
                ul.add(f32(u.final_y));
            ul.add(static_cast<long long>(u.path.size())), ul.add(u.path_index), ul.add(u.target_orient);
            ul.add(u.field_078), ul.add(u.is_stopped);
            units.emplace_back(static_cast<int>(pi), u.id, u.serial, ul.s + "\n");
        }
        for (const House& h : p.houses) {
            Line hl{"B"};
            hl.add(s.tick), hl.add(pn), hl.add(h.num), hl.add(h.type), hl.add(h.map_x), hl.add(h.map_y);
            hl.add(f32(h.hp)), hl.add(h.is_destroyed), hl.add(h.queue_count);
            const bool has_head = h.queue_count > 0 && !h.queue.empty() && h.queue[0] != 0xffff;
            hl.add(has_head ? static_cast<long long>(h.queue[0]) : -1);
            houses += hl.s + "\n";
        }
    }
    // unit_hash: crc32 over the U lines (with their newline) taken in (player, id, serial) order.
    std::vector<std::size_t> order(units.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&units](std::size_t a, std::size_t b) {
        return std::tie(std::get<0>(units[a]), std::get<1>(units[a]), std::get<2>(units[a])) <
               std::tie(std::get<0>(units[b]), std::get<1>(units[b]), std::get<2>(units[b]));
    });
    uLong crc = crc32(0, nullptr, 0);
    for (std::size_t i : order) {
        const std::string& line = std::get<3>(units[i]);
        crc = crc32(crc, reinterpret_cast<const Bytef*>(line.data()), static_cast<uInt>(line.size()));
    }
    char hash[16];
    std::snprintf(hash, sizeof hash, "%08lx", static_cast<unsigned long>(crc));

    Line t{"T"};
    t.add(s.tick), t.add(tick_enter), t.add(s.rng_seed), t.add(s.rng_index);
    t.add(std::string("-")), t.add(std::string("-"));  // crt_enter, crt_exit
    t.add(s.speed_ms), t.add(s.my_player), t.add(static_cast<long long>(s.players.size()));
    t.add(static_cast<long long>(unit_count)), t.add(std::string(hash));
    out = t.s + "\n" + players;
    for (const auto& u : units) out += std::get<3>(u);
    out += houses;
    return out;
}

}  // namespace antanta::sim
