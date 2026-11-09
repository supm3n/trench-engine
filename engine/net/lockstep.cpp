// The lockstep rules (net/lockstep.hpp).
#include "lockstep.hpp"

#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "net/config.hpp"
#include "sim/ownsave.hpp"
#include "sim/trace.hpp"
#include "sim_hash.hpp"  // written at build time (scripts/sim-hash.cmake)

namespace antanta::net {

namespace {

using K = sim::Command::Kind;

// How a kind names its acting player: the first argument as a raw player number, or the first argument's house.
enum class Actor : std::uint8_t { None, RawArg, HouseArg, PlayerField };

// The kinds the game's interface sends as a top-level order (ui/ and the app), and where each has its player.
Actor TopActor(K k) {
    switch (k) {
        case K::UiOrder:
        case K::Stop:
        case K::NoFire:
        case K::MayFire:
        case K::GroupCreate:
        case K::GroupDismiss:
        case K::Unload:
        case K::SetMode:
        case K::Act_b9730:
        case K::Act_b9c70:
        case K::Act_baa60:
        case K::Act_ea260:
        case K::Act_c26e0:
        case K::Act_c2d80:
        case K::Act_b66a0:
        case K::Act_a5380:
        case K::Act_a53f0:
        case K::Act_6cf80:
        case K::AddHouse:
        case K::SelectHouses:
        case K::ToggleAdvisor:
        case K::MarketAuto:
        case K::BindGroup:
        case K::FacingOrder:
        case K::PlusStrike:
        case K::QueueOrder: return Actor::RawArg;
        case K::Rally:
        case K::Act_ba0a0:
        case K::Act_b9fc0:
        case K::ReleaseUnit:
        case K::Act_a5290:
        case K::Act_a52c0:
        case K::SellHouse:
        case K::Act_bbad0:
        case K::Act_c0c20:
        case K::ReleaseHouseUnit: return Actor::HouseArg;
        case K::OrderPoint: return Actor::PlayerField;  // its unit argument has no player: Command::player names it
        default: return Actor::None;  // the children of a ui_order, the view input, the recorder-only kinds
    }
}

bool ChildKind(K k) {
    return k == K::OrderMove || k == K::EnterHouse || k == K::AttackHouse || k == K::Attack || k == K::OrderXy ||
           k == K::Board || k == K::GroupOrder;
}

// The kinds that take no order number (no O line): they may come before a ui_order in a tick.
bool Unnumbered(K k) {
    return k == K::SelectHouses || k == K::AddHouse || k == K::ToggleAdvisor || k == K::MarketAuto ||
           k == K::BindGroup || k == K::FacingOrder || k == K::ReleaseHouseUnit || k == K::HouseDepth ||
           k == K::NetControl || k == K::PlusStrike || k == K::QueueOrder;
}

}  // namespace

bool Group_Authorize(OrderGroup& g, std::uint8_t p, std::string* why) {
    if (g.commands.empty() || g.commands.size() > kMaxGroupCommands) {
        *why = "an empty or oversized order";
        return false;
    }
    const bool ui = g.commands[0].kind == K::UiOrder;
    for (std::size_t i = 0; i < g.commands.size(); ++i) {
        sim::Command& c = g.commands[i];
        // As ui::take_tick_orders numbers them: the first and, after a ui_order, its May fire are top-level orders;
        // in a ui_order's group the rest are its children; in any other group every command is top-level.
        const bool child = ui && i != 0 && c.kind != K::MayFire;
        const std::string name(sim::Command_Name(c.kind));
        if (child) {
            if (!ChildKind(c.kind)) {
                *why = name + " is no child of a ui_order";
                return false;
            }
        } else {
            const Actor a = TopActor(c.kind);
            bool mine = false;
            if (a == Actor::RawArg) {
                mine = !c.args.empty() && c.args[0].tag == sim::OrderArg::Tag::Raw && c.args[0].raw == p;
            } else if (a == Actor::HouseArg) {
                mine = !c.args.empty() && c.args[0].tag == sim::OrderArg::Tag::House && c.args[0].house.player == p;
            } else if (a == Actor::PlayerField) {
                mine = c.player == -1 || c.player == p;
            }
            if (!mine) {
                *why = a == Actor::None ? name + " is not an order a player sends" : name + " for another player";
                return false;
            }
        }
        if (c.player != -1 && c.player != p) {
            *why = name + " names another player";
            return false;
        }
        for (const sim::UnitRef& r : c.units) {
            if (r.player != 0xff && r.player != p) {
                *why = name + " lists another player's unit";
                return false;
            }
        }
        for (const sim::HouseRef& r : c.houses) {
            if (r.player != p) {
                *why = name + " lists another player's house";
                return false;
            }
        }
        c.player = p;
        for (sim::UnitRef& r : c.units) r.player = p;
        c.tick = 0, c.oseq = 0, c.parent = 0, c.rng_index = -1;
    }
    return true;
}

bool Group_IsUi(const OrderGroup& g) { return !g.commands.empty() && g.commands[0].kind == K::UiOrder; }

bool Group_IsSelect(const OrderGroup& g) {
    for (const sim::Command& c : g.commands) {
        if (c.kind != K::SelectHouses) return false;
    }
    return !g.commands.empty();
}

bool Group_IsControl(const OrderGroup& g) { return g.commands.size() == 1 && g.commands[0].kind == K::NetControl; }

OrderGroup Group_Control(std::uint8_t player, std::uint8_t level) {
    sim::Command c;
    c.kind = K::NetControl, c.player = player;
    c.args.resize(2);
    c.args[0].raw = player, c.args[1].raw = level;
    return OrderGroup{player, {std::move(c)}};
}

std::vector<sim::Command> Lockstep_Commands(const sim::GameState& s, const std::vector<OrderGroup>& frame) {
    std::vector<sim::Command> out;
    bool numbered = false;  // a command that takes an order number came before
    for (const OrderGroup& g : frame) {
        if (g.commands.empty()) continue;
        const bool ui = Group_IsUi(g);
        if (ui && numbered) continue;  // its number would not be the sim's next one
        const std::uint32_t top = ui ? s.order_seq + 1 : 0;
        for (std::size_t i = 0; i < g.commands.size(); ++i) {
            sim::Command c = g.commands[i];
            c.tick = s.tick + 1;
            c.oseq = i == 0 ? top : top + static_cast<std::uint32_t>(i);
            c.parent = i == 0 || c.kind == K::MayFire ? 0 : top;
            numbered = numbered || (c.parent == 0 && !Unnumbered(c.kind));
            out.push_back(std::move(c));
        }
    }
    return out;
}

std::string NetTrace_Tick(const sim::GameState& s, std::uint32_t enter) {
    std::string t = sim::trace_tick(s, enter);
    // The T line's ninth field is MyPlayer (sim/trace.cpp): "T tick enter seed rng crt crt speed my_player ...".
    if (t.compare(0, 2, "T\t") == 0) {
        std::size_t at = 0;
        for (int k = 0; k < 8 && at != std::string::npos; ++k) at = t.find('\t', at + 1);
        const std::size_t end = at == std::string::npos ? at : t.find('\t', at + 1);
        if (end != std::string::npos) t.replace(at + 1, end - at - 1, "-");
    }
    char g[64];
    std::snprintf(g, sizeof g, "G\t%" PRIu32 "\t%016" PRIx64 "\n", s.tick, sim::GameState_NetHash(s));
    return sim::trace_orders(s) + sim::trace_view(s) + sim::trace_commands(s) + t + g;
}

std::string Net_BuildId() { return Net_BuildId(ANTANTA_SIM_HASH); }

std::string Net_BuildId(const std::string& sim_code_hash) {
    return "p" + std::to_string(kProtocolVersion) + "-h" + std::to_string(sim::GameState_HashVersion()) + "-s" +
           sim_code_hash;
}

std::string NetLog_Start(const sim::SkirmishSetup& setup) {
    Message m;
    m.type = MsgType::Start, m.setup = setup;
    static const char* d = "0123456789abcdef";
    std::string line = "S\t";
    for (const char c : Message_Encode(m)) {
        const auto b = static_cast<std::uint8_t>(c);
        line += d[b >> 4], line += d[b & 15];
    }
    return line + '\n';
}

namespace {

std::string HexBytes(const std::string& bytes) {
    static const char* d = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const char c : bytes) {
        const auto b = static_cast<std::uint8_t>(c);
        out += d[b >> 4], out += d[b & 15];
    }
    return out;
}

std::string FromHex(const std::string& line, std::size_t at) {
    std::string bytes;
    bytes.reserve((line.size() - at) / 2);
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10; };
    for (std::size_t k = at; k + 1 < line.size(); k += 2) bytes += static_cast<char>(v(line[k]) * 16 + v(line[k + 1]));
    return bytes;
}

}  // namespace

std::string NetLog_Resume(std::uint32_t tick, const std::string& save) {
    return "R\t" + std::to_string(tick) + '\t' + HexBytes(save) + '\n';
}

std::string NetLog_Frame(std::uint32_t tick, const std::vector<OrderGroup>& frame) {
    std::string out;
    for (std::size_t k = 0; k < frame.size(); ++k) {
        for (const sim::Command& c : frame[k].commands) {
            out += "N\t" + std::to_string(tick) + '\t' + std::to_string(frame[k].player) + '\t' + std::to_string(k) +
                   '\t' + sim::Command_Print(c) + '\n';
        }
    }
    return out;
}

bool NetLog_Read(const std::string& text, sim::SkirmishSetup* setup,
                 std::vector<std::pair<std::uint32_t, std::vector<OrderGroup>>>* frames, std::string* error,
                 std::string* resume) {
    bool have_setup = false;
    if (resume) resume->clear();
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.compare(0, 2, "S\t") == 0) {
            std::string bytes;
            for (std::size_t k = 2; k + 1 < line.size(); k += 2) {
                bytes += static_cast<char>(std::strtoul(line.substr(k, 2).c_str(), nullptr, 16));
            }
            const auto m = Message_Decode(bytes, error);
            if (!m || m->type != MsgType::Start) {
                if (m) *error = "the S line is not a start";
                return false;
            }
            *setup = m->setup, have_setup = true;
            continue;
        }
        if (line.compare(0, 2, "R\t") == 0) {  // a resumed game's save (slice MP9)
            const std::size_t tab = line.find('\t', 2);
            if (tab == std::string::npos) {
                *error = "the R line has no save";
                return false;
            }
            if (resume) *resume = FromHex(line, tab + 1);
            continue;
        }
        // N <tick> <player> <group> <O line>
        std::size_t f[4];
        f[0] = line.find('\t');
        for (int k = 1; k < 4; ++k) f[k] = f[k - 1] == std::string::npos ? f[k - 1] : line.find('\t', f[k - 1] + 1);
        if (line.compare(0, 2, "N\t") != 0 || f[3] == std::string::npos) {
            *error = "not a log line: " + line;
            return false;
        }
        const auto tick = static_cast<std::uint32_t>(std::strtoul(line.c_str() + f[0] + 1, nullptr, 10));
        const auto player = static_cast<std::uint8_t>(std::strtoul(line.c_str() + f[1] + 1, nullptr, 10));
        const auto group = static_cast<std::size_t>(std::strtoul(line.c_str() + f[2] + 1, nullptr, 10));
        auto c = sim::Command_Parse(line.substr(f[3] + 1), error);
        if (!c) return false;
        if (frames->empty() || frames->back().first != tick) frames->emplace_back(tick, std::vector<OrderGroup>{});
        std::vector<OrderGroup>& fr = frames->back().second;
        if (group >= fr.size()) fr.resize(group + 1);
        fr[group].player = player;
        fr[group].commands.push_back(std::move(*c));
    }
    if (!have_setup) *error = "no S line (the game's setup)";
    return have_setup;
}

}  // namespace antanta::net
