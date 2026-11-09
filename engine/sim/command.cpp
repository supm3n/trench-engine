// Command <-> O line (tools/recorder/FORMAT.md "O: player order"). Text only: no sim behaviour here.
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "original_ids.hpp"
#include "state.hpp"

namespace antanta::sim {

namespace {

struct KindInfo {
    std::string_view name;
    std::uint32_t address;
};

// FORMAT.md "Hooked functions", in Command::Kind order; house_depth is a view input (the recorder's D line) and
// add_house (the build click), select_houses (the selected-house flags), release_house (a unit let out of a house
// through the panel), toggle_advisor (the advisor buttons), market_auto (the market's automatic trade) and bind_group
// (Ctrl+digit), facing_order (the right drag) and select_summary (the selection summary of the mission conditions)
// are inputs of the new engine's app that the recorder writes no line for, not hooked orders; net_control (slice
// MP9) is the network server's control change, plus_strike (Entente Engine Plus) the town centre's call-in strike and
// queue_order (docs/bugs.md B317) the Shift + right click order queue, ours alone (address 0).
constexpr KindInfo kKindInfo[Command::kKinds] = {
    {"ui_order", orig::kUiOrderToPoint},
    {"order_move", orig::kOrderMove},
    {"enter_house", orig::kSendToHouse},
    {"attack_house", orig::kAttackHouse},
    {"attack", orig::kOnAttackedBy},
    {"order_xy", orig::kOrderXy},
    {"board", orig::kBoard},
    {"order_point", orig::kOrderPoint},
    {"group_order", orig::kGroupOrder},
    {"rally", orig::kHouseSetRally},
    {"stop", orig::kOrderStop},
    {"nofire", orig::kOrderNoFire},
    {"mayfire", orig::kOrderMayFire},
    {"group_create", orig::kUiGroupCreate},
    {"group_dismiss", orig::kOrderGroupDismiss},
    {"unload", orig::kUnload},
    {"set_mode", orig::kSetStance},
    {"act_ba0a0", orig::kHouseMayFire},
    {"act_b9fc0", orig::kHouseHoldFire},
    {"act_b9730", orig::kOrderTrain},
    {"act_b9c70", orig::kCancelTraining},
    {"release_unit", orig::kReleaseFromHangar},
    {"act_a5290", orig::kResearchStart},
    {"act_a52c0", orig::kResearchCancel},
    {"sell_house", orig::kSellHouse},
    {"act_baa60", orig::kHouseAttackType},
    {"act_bbad0", orig::kDestroyOwnHouse},
    {"act_c0c20", orig::kBarnSow},
    {"act_ea260", orig::kDeleteKey},
    {"act_c26e0", orig::kMarketExchange},
    {"act_c2d80", orig::kMarketGift},
    {"act_b66a0", orig::kDiplomacyApply},
    {"act_a5380", orig::kSetEconomicAdvisor},
    {"act_a53f0", orig::kSetMilitaryAdvisor},
    {"act_6cf80", orig::kPontoonBridge},
    {"house_depth", orig::kRenderHouses},
    {"add_house", orig::kAddHouseToPoint},
    {"select_houses", orig::kClearSelection},
    {"release_house", orig::kUiReleaseUnitFromHouse},
    {"toggle_advisor", orig::kAdvisorButtons},
    {"market_auto", orig::kMarketAutoTrade},
    {"bind_group", orig::kBindGroup},
    {"facing_order", orig::kFacingOrder},
    {"select_summary", orig::kSelectOnRelease},
    {"net_control", 0},
    {"plus_strike", 0},
    {"queue_order", 0},
};

std::vector<std::string_view> split(std::string_view s, char sep) {
    std::vector<std::string_view> out;
    for (;;) {
        const std::size_t end = s.find(sep);
        out.push_back(s.substr(0, end));
        if (end == std::string_view::npos) return out;
        s.remove_prefix(end + 1);
    }
}

// A whole field as an unsigned number in the given base; false on empty input or trailing characters.
bool to_u64(std::string_view s, int base, std::uint64_t* v) {
    if (s.empty() || s.size() > 20 || s[0] == '-' || s[0] == '+' || s[0] == ' ') return false;
    const std::string t(s);
    char* end = nullptr;
    *v = std::strtoull(t.c_str(), &end, base);
    return end == t.c_str() + t.size();
}

bool to_i32(std::string_view s, std::int32_t* v) {
    const bool neg = !s.empty() && s[0] == '-';
    std::uint64_t u = 0;
    if (!to_u64(neg ? s.substr(1) : s, 10, &u) || u > (neg ? 0x80000000ull : 0x7fffffffull)) return false;
    *v = static_cast<std::int32_t>(neg ? -static_cast<std::int64_t>(u) : static_cast<std::int64_t>(u));
    return true;
}

bool to_u32(std::string_view s, int base, std::uint32_t* v) {
    std::uint64_t u = 0;
    if (!to_u64(s, base, &u) || u > 0xffffffffull) return false;
    *v = static_cast<std::uint32_t>(u);
    return true;
}

// "<a>.<b>" with decimal parts.
bool pair(std::string_view s, std::uint32_t* a, std::uint32_t* b) {
    const std::size_t dot = s.find('.');
    return dot != std::string_view::npos && to_u32(s.substr(0, dot), 10, a) && to_u32(s.substr(dot + 1), 10, b);
}

bool unit_ref(std::string_view s, std::uint8_t player, UnitRef* r) {
    std::uint32_t id = 0, serial = 0;
    if (!pair(s, &id, &serial) || id > 0xffff) return false;
    *r = UnitRef{player, static_cast<std::uint16_t>(id), serial};
    return true;
}

bool house_ref(std::string_view s, HouseRef* r) {
    std::uint32_t player = 0, num = 0;
    if (!pair(s, &player, &num) || player > 0xff || num > 0xffff) return false;
    *r = HouseRef{static_cast<std::uint8_t>(player), static_cast<std::uint16_t>(num), 0};
    return true;
}

// The arguments the recorder prints as a number array (`aN`) or as bytes (`b8`), by the decoders of agent.js's
// ORDERS. Any other argument that starts with a or b is raw hex: a research id 0xb prints as `b`, 0xa as `a`.
bool list_arg(Command::Kind k, std::size_t i) {
    using K = Command::Kind;
    if (k == K::GroupOrder) return i == 2;
    return i == 1 &&
           (k == K::Act_a52c0 || k == K::SellHouse || k == K::Act_b66a0 || k == K::Act_a5380 || k == K::Act_a53f0);
}

bool parse_arg(std::string_view s, OrderArg* a, bool list) {
    using Tag = OrderArg::Tag;
    const Tag fixed = s == "?" ? Tag::Unknown : s == "g" ? Tag::Group : s == "u-" ? Tag::NoUnit : Tag::NoHouse;
    if (s == "?" || s == "g" || s == "u-" || s == "h-") {
        a->tag = fixed;
        return true;
    }
    if (s.empty()) return false;
    if (s[0] == 'u') {
        a->tag = Tag::Unit;
        return unit_ref(s.substr(1), 0xff, &a->unit);
    }
    if (s[0] == 'h') {
        a->tag = Tag::House;
        return house_ref(s.substr(1), &a->house);
    }
    if (list && s[0] == 'a') {
        a->tag = Tag::Array;
        if (s.size() == 1) return true;
        for (std::string_view v : split(s.substr(1), '/')) {
            std::int32_t x = 0;
            if (!to_i32(v, &x)) return false;
            a->values.push_back(x);
        }
        return true;
    }
    if (list && s[0] == 'b') {
        a->tag = Tag::Bytes;
        s.remove_prefix(1);
        if (s.size() % 2 != 0) return false;
        for (std::size_t i = 0; i < s.size(); i += 2) {
            std::uint32_t x = 0;
            if (!to_u32(s.substr(i, 2), 16, &x)) return false;
            a->values.push_back(static_cast<std::int32_t>(x));
        }
        return true;
    }
    a->tag = Tag::Raw;
    // Raw hex as the recorder prints it: lowercase, no leading zeros (a canonical form, so the print round-trips).
    if (s.size() > 1 && s[0] == '0') return false;
    for (char ch : s) {
        if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
    }
    return to_u32(s, 16, &a->raw);
}

void print_arg(std::string& out, const OrderArg& a) {
    char buf[32];
    switch (a.tag) {
        case OrderArg::Tag::Raw:
            std::snprintf(buf, sizeof buf, "%" PRIx32, a.raw);
            out += buf;
            break;
        case OrderArg::Tag::Unit: out += 'u' + std::to_string(a.unit.id) + '.' + std::to_string(a.unit.serial); break;
        case OrderArg::Tag::NoUnit: out += "u-"; break;
        case OrderArg::Tag::House:
            out += 'h' + std::to_string(a.house.player) + '.' + std::to_string(a.house.slot);
            break;
        case OrderArg::Tag::NoHouse: out += "h-"; break;
        case OrderArg::Tag::Group: out += 'g'; break;
        case OrderArg::Tag::Array:
            out += 'a';
            for (std::size_t i = 0; i < a.values.size(); ++i) {
                if (i != 0) out += '/';
                out += std::to_string(a.values[i]);
            }
            break;
        case OrderArg::Tag::Bytes:
            out += 'b';
            for (std::int32_t v : a.values) {
                std::snprintf(buf, sizeof buf, "%02x", static_cast<unsigned>(v & 0xff));
                out += buf;
            }
            break;
        case OrderArg::Tag::Unknown: out += '?'; break;
    }
}

}  // namespace

std::string_view Command_Name(Command::Kind k) { return kKindInfo[static_cast<int>(k)].name; }
std::uint32_t Command_Address(Command::Kind k) { return kKindInfo[static_cast<int>(k)].address; }

std::string Command_Print(const Command& c) {
    std::string out = "O\t" + std::to_string(c.tick) + '\t' + std::to_string(c.oseq) + '\t' + std::to_string(c.parent) +
                      '\t' + std::to_string(c.player) + '\t';
    out += Command_Name(c.kind);
    out +=
        '\t' + std::to_string(c.rng_index) + '\t' + std::to_string(c.cell_x) + '\t' + std::to_string(c.cell_y) + '\t';
    if (c.args.empty()) out += '-';
    for (std::size_t i = 0; i < c.args.size(); ++i) {
        if (i != 0) out += ',';
        print_arg(out, c.args[i]);
    }
    out += '\t';
    if (c.units.empty()) out += '-';
    for (std::size_t i = 0; i < c.units.size(); ++i) {
        if (i != 0) out += ',';
        out += std::to_string(c.units[i].id) + '.' + std::to_string(c.units[i].serial);
    }
    out += '\t';
    if (c.houses.empty()) out += '-';
    for (std::size_t i = 0; i < c.houses.size(); ++i) {
        if (i != 0) out += ',';
        out += std::to_string(c.houses[i].player) + '.' + std::to_string(c.houses[i].slot);
    }
    return out;
}

std::optional<Command> Command_Parse(std::string_view line, std::string* error) {
    const std::vector<std::string_view> f = split(line, '\t');
    auto bad = [&](const char* what) {
        *error = std::string("O line: bad ") + what + ": " + std::string(line);
        return std::nullopt;
    };
    if (f.size() < 12 || f[0] != "O") return bad("record (need O and 11 fields)");
    Command c;
    if (!to_u32(f[1], 10, &c.tick)) return bad("tick");
    if (!to_u32(f[2], 10, &c.oseq)) return bad("oseq");
    if (!to_u32(f[3], 10, &c.parent)) return bad("parent");
    if (!to_i32(f[4], &c.player)) return bad("player");
    int k = 0;
    while (k < Command::kKinds && kKindInfo[k].name != f[5]) ++k;
    if (k == Command::kKinds) return bad("fn");
    c.kind = static_cast<Command::Kind>(k);
    if (!to_i32(f[6], &c.rng_index)) return bad("rng_index");
    if (!to_i32(f[7], &c.cell_x) || !to_i32(f[8], &c.cell_y)) return bad("cell");
    if (f[9] != "-") {
        for (std::string_view a : split(f[9], ',')) {
            OrderArg arg;
            if (!parse_arg(a, &arg, list_arg(c.kind, c.args.size()))) return bad("argument");
            c.args.push_back(std::move(arg));
        }
    }
    const auto up = static_cast<std::uint8_t>(c.player >= 0 && c.player <= 0xff ? c.player : 0xff);
    if (f[10] != "-") {
        for (std::string_view u : split(f[10], ',')) {
            UnitRef r;
            if (!unit_ref(u, up, &r)) return bad("unit");
            c.units.push_back(r);
        }
    }
    if (f[11] != "-") {
        for (std::string_view h : split(f[11], ',')) {
            HouseRef r;
            if (!house_ref(h, &r)) return bad("house");
            c.houses.push_back(r);
        }
    }
    return c;
}

std::string Command_PrintView(const Command& c) {
    std::string out = "D\t" + std::to_string(c.tick);
    if (c.args.size() != 4 || c.args[0].tag != OrderArg::Tag::House) return out + "\t-\t-\t-\t-\t-";
    out += '\t' + std::to_string(c.args[0].house.player) + '\t' + std::to_string(c.args[0].house.slot) + '\t' +
           std::to_string(c.args[1].raw);
    if (c.args[2].raw == 0xff) return out + "\t-\t-";
    return out + '\t' + std::to_string(c.args[2].raw) + '\t' + std::to_string(c.args[3].raw);
}

std::optional<Command> Command_ParseView(std::string_view line, std::string* error) {
    const std::vector<std::string_view> f = split(line, '\t');
    auto bad = [&](const char* what) {
        *error = std::string("D line: bad ") + what + ": " + std::string(line);
        return std::nullopt;
    };
    if (f.size() < 7 || f[0] != "D") return bad("record (need D and 6 fields)");
    Command c;
    c.kind = Command::Kind::HouseDepth;
    std::uint32_t player = 0, num = 0, type = 0, phase = 0xff, stage = 0;
    if (!to_u32(f[1], 10, &c.tick)) return bad("tick");
    if (!to_u32(f[2], 10, &player) || player > 0xff) return bad("player");
    if (!to_u32(f[3], 10, &num) || num > 0xffff) return bad("num");
    if (!to_u32(f[4], 10, &type) || type > 0xffff) return bad("type");
    if (f[5] != "-" || f[6] != "-") {
        if (!to_u32(f[5], 10, &phase) || phase > 2) return bad("phase");
        if (!to_u32(f[6], 10, &stage) || stage > 0xff) return bad("stage");
    }
    c.player = static_cast<std::int32_t>(player);
    OrderArg h;
    h.tag = OrderArg::Tag::House;
    h.house = HouseRef{static_cast<std::uint8_t>(player), static_cast<std::uint16_t>(num), 0};
    OrderArg t, p, st;
    t.raw = type, p.raw = phase, st.raw = stage;
    c.args = {h, t, p, st};
    return c;
}

}  // namespace antanta::sim
