// The network game's messages (net/protocol.hpp).
#include "protocol.hpp"

namespace antanta::net {

namespace {

struct Out {
    std::string b;
    void u8(std::uint8_t v) { b += static_cast<char>(v); }
    void u16(std::uint16_t v) { u8(static_cast<std::uint8_t>(v)), u8(static_cast<std::uint8_t>(v >> 8)); }
    void u32(std::uint32_t v) { u16(static_cast<std::uint16_t>(v)), u16(static_cast<std::uint16_t>(v >> 16)); }
    void u64(std::uint64_t v) { u32(static_cast<std::uint32_t>(v)), u32(static_cast<std::uint32_t>(v >> 32)); }
    void str(const std::string& s) { u32(static_cast<std::uint32_t>(s.size())), b += s; }
    void str16(const std::u16string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        for (char16_t c : s) u16(static_cast<std::uint16_t>(c));
    }
};

struct In {
    const std::string& b;
    std::size_t at = 0;
    bool ok = true;
    bool need(std::size_t n) {
        if (!ok || b.size() - at < n) ok = false;
        return ok;
    }
    std::uint8_t u8() { return need(1) ? static_cast<std::uint8_t>(b[at++]) : 0; }
    std::uint16_t u16() {
        const std::uint16_t lo = u8();
        return static_cast<std::uint16_t>(lo | (u8() << 8));
    }
    std::uint32_t u32() {
        const std::uint32_t lo = u16();
        return lo | (static_cast<std::uint32_t>(u16()) << 16);
    }
    std::uint64_t u64() {
        const std::uint64_t lo = u32();
        return lo | (static_cast<std::uint64_t>(u32()) << 32);
    }
    // A count of items of at least `each` bytes: never more than the bytes left.
    std::uint32_t count(std::size_t each) {
        const std::uint32_t n = u32();
        if (ok && static_cast<std::uint64_t>(n) * each > b.size() - at) ok = false;
        return ok ? n : 0;
    }
    std::string str() {
        const std::uint32_t n = count(1);
        if (!ok) return {};
        std::string s = b.substr(at, n);
        at += n;
        return s;
    }
    std::u16string str16() {
        const std::uint32_t n = count(2);
        std::u16string s;
        for (std::uint32_t i = 0; i < n && ok; ++i) s += static_cast<char16_t>(u16());
        return s;
    }
};

void put_setup(Out& o, const sim::SkirmishSetup& s) {
    o.str(s.map), o.u8(s.players), o.u8(s.resources), o.u32(s.seed);
    for (const sim::SkirmishSlot& sl : s.slots) o.u8(sl.level), o.u8(sl.nation), o.u8(sl.colour), o.u8(sl.team);
    for (const std::u16string& n : s.comp_names) o.str16(n);
    for (bool v : s.economic) o.u8(v ? 1 : 0);
    for (bool v : s.military) o.u8(v ? 1 : 0);
    o.u8(s.net ? 1 : 0), o.u8(s.my_slot);
    for (const std::u16string& n : s.names) o.str16(n);
    o.u8(s.speed_ms), o.u8(s.short_anims);
    o.u8(s.plus ? 1 : 0);  // version 4: Entente Engine Plus (original_rules is never sent)
}

sim::SkirmishSetup get_setup(In& in) {
    sim::SkirmishSetup s;
    s.map = in.str(), s.players = in.u8(), s.resources = in.u8(), s.seed = in.u32();
    for (sim::SkirmishSlot& sl : s.slots)
        sl.level = in.u8(), sl.nation = in.u8(), sl.colour = in.u8(), sl.team = in.u8();
    for (std::u16string& n : s.comp_names) n = in.str16();
    for (std::size_t i = 0; i < s.economic.size(); ++i) s.economic[i] = in.u8() != 0;
    for (std::size_t i = 0; i < s.military.size(); ++i) s.military[i] = in.u8() != 0;
    s.net = in.u8() != 0, s.my_slot = in.u8();
    for (std::u16string& n : s.names) n = in.str16();
    s.speed_ms = in.u8(), s.short_anims = in.u8();
    s.plus = in.u8() != 0;
    return s;
}

void put_groups(Out& o, const std::vector<OrderGroup>& groups) {
    o.u32(static_cast<std::uint32_t>(groups.size()));
    for (const OrderGroup& g : groups) {
        o.u8(g.player), o.u32(static_cast<std::uint32_t>(g.commands.size()));
        for (const sim::Command& c : g.commands) o.str(sim::Command_Print(c));
    }
}

std::vector<OrderGroup> get_groups(In& in, std::string* error) {
    std::vector<OrderGroup> out;
    const std::uint32_t n = in.count(5);
    for (std::uint32_t i = 0; i < n && in.ok; ++i) {
        OrderGroup g;
        g.player = in.u8();
        const std::uint32_t k = in.count(4);
        for (std::uint32_t j = 0; j < k && in.ok; ++j) {
            const std::string line = in.str();
            if (!in.ok) break;
            std::optional<sim::Command> c = sim::Command_Parse(line, error);
            if (!c) {
                in.ok = false;
                break;
            }
            g.commands.push_back(std::move(*c));
        }
        out.push_back(std::move(g));
    }
    return out;
}

// Version 3: the game's identity, the autosave interval and the seats (Start, RejoinState).
void put_seats(Out& o, const Message& m) {
    o.u64(m.uid), o.u32(m.autosave), o.u32(static_cast<std::uint32_t>(m.seats.size()));
    for (const sim::OwnSaveSeat& s : m.seats) {
        o.u8(s.slot), o.u8(s.player), o.str(s.name), o.u64(s.token_check), o.u8(s.left), o.u8(s.ai);
    }
}

void get_seats(In& in, Message& m) {
    m.uid = in.u64(), m.autosave = in.u32();
    const std::uint32_t n = in.count(16);
    for (std::uint32_t i = 0; i < n && in.ok; ++i) {
        sim::OwnSaveSeat s;
        s.slot = in.u8(), s.player = in.u8(), s.name = in.str(), s.token_check = in.u64(), s.left = in.u8();
        s.ai = in.u8();
        m.seats.push_back(std::move(s));
    }
}

}  // namespace

std::uint64_t Token_Check(std::uint64_t token) {
    std::uint64_t h = 0xcbf29ce484222325ull;  // FNV-1a over the token's bytes and a tag, so it is not the token
    for (int k = 0; k < 8; ++k) h = (h ^ ((token >> (8 * k)) & 0xff)) * 0x100000001b3ull;
    for (const char c : std::string("antanta seat")) h = (h ^ static_cast<std::uint8_t>(c)) * 0x100000001b3ull;
    return h;
}

std::string Message_Encode(const Message& m) {
    Out o;
    o.u8(static_cast<std::uint8_t>(m.type));
    switch (m.type) {
        case MsgType::Hello: o.u32(m.version), o.str(m.build), o.u64(m.data_hash), o.str(m.name); break;
        case MsgType::ListGames:
        case MsgType::StartGame:
        case MsgType::Leave:
        case MsgType::SaveRequest: break;
        case MsgType::CreateGame:
            o.str(m.name);
            put_setup(o, m.setup);
            o.u8(m.positions), o.u32(m.value), o.str(m.password), o.str(m.bytes);
            break;
        case MsgType::JoinGame: o.u32(m.game), o.u8(m.slot), o.str(m.password), o.u64(m.token); break;
        case MsgType::Rejoin: o.u32(m.game), o.u8(m.slot), o.u64(m.token); break;
        case MsgType::Order: put_groups(o, m.groups); break;
        case MsgType::Hash: o.u32(m.tick), o.u64(m.hash); break;
        case MsgType::Report: o.u32(m.tick), o.u32(m.value), o.u8(m.player); break;
        case MsgType::Request: o.u8(m.what), o.u32(m.value); break;
        case MsgType::SaveData: o.u32(m.tick), o.str(m.bytes); break;
        case MsgType::Welcome: o.u32(m.version), o.str(m.name); break;
        case MsgType::Error: o.str(m.name); break;
        case MsgType::Games:
            o.u32(static_cast<std::uint32_t>(m.games.size()));
            for (const GameInfo& g : m.games) {
                o.u32(g.id), o.str(g.name), o.str(g.map), o.u8(g.humans), o.u8(g.max_humans), o.u8(g.started ? 1 : 0);
                o.u8(g.locked ? 1 : 0), o.u64(g.uid), o.u8(g.resume ? 1 : 0);
            }
            break;
        case MsgType::Joined:
            o.u32(m.game), o.u8(m.slot), o.u64(m.token), o.u8(m.what);
            put_setup(o, m.setup);
            o.u64(m.uid);
            break;
        case MsgType::Roster:
        case MsgType::Desync:
            o.u32(m.tick), o.u32(static_cast<std::uint32_t>(m.roster.size()));
            for (const RosterEntry& r : m.roster) {
                o.u8(r.slot), o.str(r.name), o.u8(r.connected ? 1 : 0), o.u8(r.computer);
            }
            break;
        case MsgType::Start:
            put_setup(o, m.setup);
            o.u32(m.value), o.u8(m.input_delay), o.u16(m.hash_every);
            put_seats(o, m);
            break;
        case MsgType::Frame:
            o.u32(m.tick);
            put_groups(o, m.groups);
            break;
        case MsgType::Pace: o.u32(m.value), o.u8(m.what); break;
        case MsgType::RejoinState:
            o.u32(m.tick), o.u8(m.player), o.str(m.bytes);
            put_setup(o, m.setup);
            o.u32(m.value), o.u8(m.input_delay), o.u16(m.hash_every);
            put_seats(o, m);
            break;
        case MsgType::PlayerLeft: o.u8(m.slot), o.u8(m.what); break;
    }
    return o.b;
}

std::optional<Message> Message_Decode(const std::string& data, std::string* error) {
    In in{data};
    Message m;
    const std::uint8_t t = in.u8();
    if (!in.ok) {
        *error = "empty message";
        return std::nullopt;
    }
    m.type = static_cast<MsgType>(t);
    std::string cmd_error;
    switch (m.type) {
        case MsgType::Hello: m.version = in.u32(), m.build = in.str(), m.data_hash = in.u64(), m.name = in.str(); break;
        case MsgType::ListGames:
        case MsgType::StartGame:
        case MsgType::Leave:
        case MsgType::SaveRequest: break;
        case MsgType::CreateGame:
            m.name = in.str();
            m.setup = get_setup(in);
            m.positions = in.u8(), m.value = in.u32(), m.password = in.str(), m.bytes = in.str();
            break;
        case MsgType::JoinGame: m.game = in.u32(), m.slot = in.u8(), m.password = in.str(), m.token = in.u64(); break;
        case MsgType::Rejoin: m.game = in.u32(), m.slot = in.u8(), m.token = in.u64(); break;
        case MsgType::Order: m.groups = get_groups(in, &cmd_error); break;
        case MsgType::Hash: m.tick = in.u32(), m.hash = in.u64(); break;
        case MsgType::Report: m.tick = in.u32(), m.value = in.u32(), m.player = in.u8(); break;
        case MsgType::Request: m.what = in.u8(), m.value = in.u32(); break;
        case MsgType::SaveData: m.tick = in.u32(), m.bytes = in.str(); break;
        case MsgType::Welcome: m.version = in.u32(), m.name = in.str(); break;
        case MsgType::Error: m.name = in.str(); break;
        case MsgType::Games: {
            const std::uint32_t n = in.count(25);
            for (std::uint32_t i = 0; i < n && in.ok; ++i) {
                GameInfo g;
                g.id = in.u32(), g.name = in.str(), g.map = in.str(), g.humans = in.u8(), g.max_humans = in.u8();
                g.started = in.u8() != 0;
                g.locked = in.u8() != 0;
                g.uid = in.u64(), g.resume = in.u8() != 0;
                m.games.push_back(std::move(g));
            }
            break;
        }
        case MsgType::Joined:
            m.game = in.u32(), m.slot = in.u8(), m.token = in.u64(), m.what = in.u8();
            m.setup = get_setup(in);
            m.uid = in.u64();
            break;
        case MsgType::Roster:
        case MsgType::Desync: {
            m.tick = in.u32();
            const std::uint32_t n = in.count(7);
            for (std::uint32_t i = 0; i < n && in.ok; ++i) {
                RosterEntry r;
                r.slot = in.u8(), r.name = in.str(), r.connected = in.u8() != 0, r.computer = in.u8();
                m.roster.push_back(std::move(r));
            }
            break;
        }
        case MsgType::Start:
            m.setup = get_setup(in);
            m.value = in.u32(), m.input_delay = in.u8(), m.hash_every = in.u16();
            get_seats(in, m);
            break;
        case MsgType::Frame:
            m.tick = in.u32();
            m.groups = get_groups(in, &cmd_error);
            break;
        case MsgType::Pace: m.value = in.u32(), m.what = in.u8(); break;
        case MsgType::RejoinState:
            m.tick = in.u32(), m.player = in.u8(), m.bytes = in.str();
            m.setup = get_setup(in);
            m.value = in.u32(), m.input_delay = in.u8(), m.hash_every = in.u16();
            get_seats(in, m);
            break;
        case MsgType::PlayerLeft: m.slot = in.u8(), m.what = in.u8(); break;
        default: *error = "unknown message type " + std::to_string(t); return std::nullopt;
    }
    if (!in.ok || in.at != data.size()) {
        *error = !cmd_error.empty() ? cmd_error : "damaged message of type " + std::to_string(t);
        return std::nullopt;
    }
    return m;
}

std::string Utf8(const std::u16string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        std::uint32_t c = s[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < s.size() && s[i + 1] >= 0xdc00 && s[i + 1] < 0xe000) {
            c = 0x10000 + ((c - 0xd800) << 10) + (s[i + 1] - 0xdc00u);
            ++i;
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xc0 | (c >> 6)), out += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xe0 | (c >> 12)), out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (c >> 18)), out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f)), out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

std::u16string Utf16(const std::string& s) {
    std::u16string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        std::uint32_t c = 0xfffd;
        std::size_t n = 1;
        if (b < 0x80) {
            c = b;
        } else if ((b & 0xe0) == 0xc0 && i + 1 < s.size()) {
            c = ((b & 0x1fu) << 6) | (s[i + 1] & 0x3fu), n = 2;
        } else if ((b & 0xf0) == 0xe0 && i + 2 < s.size()) {
            c = ((b & 0x0fu) << 12) | ((s[i + 1] & 0x3fu) << 6) | (s[i + 2] & 0x3fu), n = 3;
        } else if ((b & 0xf8) == 0xf0 && i + 3 < s.size()) {
            c = ((b & 0x07u) << 18) | ((s[i + 1] & 0x3fu) << 12) | ((s[i + 2] & 0x3fu) << 6) | (s[i + 3] & 0x3fu),
            n = 4;
        }
        if (c >= 0x10000) {
            c -= 0x10000;
            out += static_cast<char16_t>(0xd800 + (c >> 10)), out += static_cast<char16_t>(0xdc00 + (c & 0x3ff));
        } else {
            out += static_cast<char16_t>(c);
        }
        i += n;
    }
    return out;
}

}  // namespace antanta::net
