// The network game's server (net/server.hpp).
#include "server.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <random>

#include "net/lockstep.hpp"
#include "net/protocol.hpp"
#include "sim/ownsave.hpp"
#include "sim/skirmish.hpp"

namespace antanta::net {

namespace {

struct Conn {
    std::unique_ptr<Connection> link;
    std::int64_t opened_ms = 0;
    bool hello = false;
    std::string name;
    std::uint64_t data_hash = 0;
    std::uint32_t game = 0;  // 0: in the lobby
    std::uint8_t slot = 0xff;
    std::uint64_t bytes_out = 0, bytes_in = 0;  // the messages' bytes (slice MP8: the traffic per player)
    bool leaving = false;                       // slice MP9: it said Leave (on purpose), not a dropped link
};

struct Seat {
    std::uint8_t slot = 0;
    std::string name;
    std::uint64_t token = 0;
    std::uint64_t conn = 0;  // 0: nobody on it
    bool joined = false;
    std::uint8_t player = 0xff;  // the sim player of this slot (SkirmishSetup_SlotPlayers), never the client's word
    bool confirmed = false;      // the client's first report named the same player (else the game stops)
    std::uint32_t last_stamp = 0;
    std::uint32_t done_tick = 0;  // the last tick it reported as run
    std::uint32_t step_us = 0;
    std::uint32_t most_behind = 0;  // the most ticks its game was behind the clock (its reports and hashes)
    bool want_state = false;        // back after a drop: waits for a save
    // Slice MP9: the computer level playing the seat's player (0: its human), the seat given up for good (left on
    // purpose or not back in time: the computer to the end), until when a dropped player may come back, the token's
    // check value (a resumed game's seats come from the save) and, in a resumed game's lobby, a seat the host gave to
    // the computer.
    std::uint8_t ai = 0;
    bool left = false, computer = false;
    std::int64_t held_until = 0;
    std::uint64_t token_check = 0;
};

struct Game {
    std::uint32_t id = 0;
    std::string name;
    sim::SkirmishSetup setup;
    std::uint64_t data_hash = 0;
    std::string password;  // empty: anyone may join
    std::uint8_t host_slot = 0;
    std::vector<Seat> seats;  // one per human slot, in slot order
    bool started = false, paused = false, desynced = false;
    std::uint32_t clock = 0;  // the last frame sent
    std::int64_t next_at = 0;
    std::uint32_t tick_ms = 0, input_delay = 1, hash_every = 30;
    std::map<std::uint32_t, std::vector<OrderGroup>> pending;  // stamped, not sent
    std::vector<std::vector<OrderGroup>> sent;                 // frame of tick t at t - 1
    std::map<std::uint32_t, std::map<std::uint8_t, std::uint64_t>> hashes;
    bool save_asked = false;
    // Slice MP9: the game's identity (kept by its saves), a resumed game's save and the tick it starts after (sent[k]
    // is the frame of tick first_tick + k + 1), the autosave interval, the last control change's tick.
    std::uint64_t uid = 0;
    std::string resume;
    std::uint32_t first_tick = 0, autosave = 0, last_control = 0;
};

std::string Hex(std::uint64_t v) {
    static const char* d = "0123456789abcdef";
    std::string s(16, '0');
    for (int i = 15; i >= 0; --i, v >>= 4) s[static_cast<std::size_t>(i)] = d[v & 15];
    return s;
}

}  // namespace

struct Server::Impl {
    NetConfig cfg;
    std::string name, build;
    ServerHooks hooks;
    std::vector<std::unique_ptr<Listener>> listeners;
    std::map<std::uint64_t, Conn> conns;
    std::uint64_t next_conn = 1;
    std::map<std::uint32_t, Game> games;
    std::uint32_t next_game = 1;
    std::mt19937_64 tokens{std::random_device{}()};
    std::int64_t now = 0;

    void log(const std::string& line) const {
        if (hooks.log) hooks.log(line);
    }
    void put(Conn& c, const std::string& bytes) {
        c.bytes_out += bytes.size();
        c.link->send(bytes);
    }
    void send(Conn& c, const Message& m) { put(c, Message_Encode(m)); }
    void error(Conn& c, const std::string& text) {
        Message m;
        m.type = MsgType::Error, m.name = text;
        send(c, m);
        log(c.link->peer() + ": " + text);
        c.link->close();
    }
    Seat* seat_of(Game& g, std::uint8_t slot) {
        for (Seat& s : g.seats) {
            if (s.slot == slot) return &s;
        }
        return nullptr;
    }
    Conn* conn_of(const Seat& s) {
        const auto it = conns.find(s.conn);
        return s.conn != 0 && it != conns.end() ? &it->second : nullptr;
    }
    void broadcast(Game& g, const Message& m, bool also_waiting = false) {
        const std::string bytes = Message_Encode(m);
        for (Seat& s : g.seats) {
            if (Conn* c = conn_of(s); c && (also_waiting || !s.want_state)) put(*c, bytes);
        }
    }
    void roster(Game& g) {
        Message m;
        m.type = MsgType::Roster, m.tick = g.clock;
        for (const Seat& s : g.seats) {
            const std::uint8_t computer = s.left ? 2 : s.ai != 0 || s.computer ? 1 : 0;
            m.roster.push_back(RosterEntry{s.slot, s.name, s.conn != 0, computer});
        }
        broadcast(g, m, true);
    }
    std::uint32_t pace(const Game& g) const {
        std::uint32_t slowest = 0;
        for (const Seat& s : g.seats) {
            if (s.conn != 0 && !s.want_state) slowest = std::max(slowest, s.step_us);
        }
        const std::uint64_t capped = (static_cast<std::uint64_t>(slowest) * cfg.step_headroom_pct + 99999) / 100000;
        return std::max<std::uint32_t>(g.tick_ms, static_cast<std::uint32_t>(capped));
    }

    void accept() {
        for (auto& l : listeners) {
            while (auto link = l->accept()) {
                Conn c;
                log("connection " + std::to_string(next_conn) + " from " + link->peer());
                c.link = std::move(link), c.opened_ms = now;
                conns.emplace(next_conn++, std::move(c));
            }
        }
    }

    void on_message(std::uint64_t id, Conn& c, const Message& m) {
        if (!c.hello) {
            if (m.type != MsgType::Hello) return error(c, "the first message must be the hello");
            if (m.version != kProtocolVersion) {
                return error(c, "protocol version " + std::to_string(m.version) + ", the server has " +
                                    std::to_string(kProtocolVersion));
            }
            if (!build.empty() && m.build != build) return error(c, "build " + m.build + ", the server has " + build);
            c.hello = true, c.name = m.name.substr(0, 32), c.data_hash = m.data_hash;
            Message w;
            w.type = MsgType::Welcome, w.version = kProtocolVersion, w.name = name;
            return send(c, w);
        }
        switch (m.type) {
            case MsgType::ListGames: return list_games(c);
            case MsgType::CreateGame: return create_game(id, c, m);
            case MsgType::JoinGame: return join_game(id, c, m);
            case MsgType::Rejoin: return rejoin(id, c, m);
            case MsgType::StartGame: return start_game(c);
            case MsgType::Leave: c.leaving = true; return c.link->close();
            default: break;
        }
        const auto git = games.find(c.game);
        Seat* seat = git == games.end() ? nullptr : seat_of(git->second, c.slot);
        if (seat != nullptr && !git->second.started && m.type == MsgType::Request)
            return request(git->second, *seat, m);
        if (seat == nullptr || !git->second.started) return error(c, "not in a started game");
        Game& g = git->second;
        switch (m.type) {
            case MsgType::Order: return order(g, *seat, m);
            case MsgType::Hash: return hash(g, *seat, m.tick, m.hash);
            case MsgType::Report: return report(g, *seat, m);
            case MsgType::Request: return request(g, *seat, m);
            case MsgType::SaveData: return save_data(g, m);
            default: return error(c, "unexpected message");
        }
    }

    void list_games(Conn& c) {
        Message m;
        m.type = MsgType::Games;
        for (const auto& [id, g] : games) {
            GameInfo info{id,
                          g.name,
                          g.setup.map,
                          0,
                          static_cast<std::uint8_t>(g.seats.size()),
                          g.started,
                          !g.password.empty(),
                          g.uid,
                          !g.resume.empty()};
            for (const Seat& s : g.seats) info.humans += s.joined ? 1 : 0;
            m.games.push_back(std::move(info));
        }
        send(c, m);
    }

    void joined(Conn& c, const Game& g, const Seat& s) {
        Message m;
        m.type = MsgType::Joined, m.game = g.id, m.slot = s.slot, m.token = s.token, m.setup = g.setup;
        m.setup.my_slot = s.slot, m.what = s.slot == g.host_slot ? 1 : 0, m.uid = g.uid;
        send(c, m);
    }

    // Every seat for the Start and RejoinState messages (and so for every player's saves).
    std::vector<sim::OwnSaveSeat> seat_list(const Game& g) const {
        std::vector<sim::OwnSaveSeat> out;
        for (const Seat& s : g.seats) {
            out.push_back(
                sim::OwnSaveSeat{s.slot, s.player, s.name, s.token_check, static_cast<std::uint8_t>(s.left), s.ai});
        }
        return out;
    }

    void take_seat(std::uint64_t id, Conn& c, Game& g, Seat& s) {
        s.joined = true, s.conn = id, s.name = c.name, s.token = tokens(), s.computer = false;
        s.token_check = Token_Check(s.token);
        g.setup.names[s.slot] = Utf16(c.name);
        c.game = g.id, c.slot = s.slot;
        joined(c, g, s);
        log(c.name + " joined game " + std::to_string(g.id) + " in slot " + std::to_string(s.slot));
        roster(g);
    }

    void create_game(std::uint64_t id, Conn& c, const Message& m) {
        if (c.game != 0) return error(c, "already in a game");
        if (games.size() >= cfg.max_games) return error(c, "the server holds no more games");
        Game g;
        g.id = next_game++, g.name = m.name.substr(0, 64), g.setup = m.setup, g.data_hash = c.data_hash;
        g.uid = tokens() | 1;
        g.setup.net = true, g.host_slot = m.setup.my_slot, g.password = m.password.substr(0, 64);
        g.tick_ms = std::clamp(m.value, cfg.min_tick_ms, cfg.max_tick_ms);
        g.input_delay = std::clamp(cfg.input_delay, cfg.min_input_delay, cfg.max_input_delay);
        g.input_delay = std::max<std::uint32_t>(g.input_delay, 1),
        g.hash_every = std::max<std::uint32_t>(cfg.hash_every, 1);
        // Each human slot's sim player as every machine's start makes it: the server knows it from the setup and the
        // map's position count, so no client can name another player's number.
        const std::array<std::uint8_t, 8> players = sim::SkirmishSetup_SlotPlayers(g.setup, m.positions);
        for (std::uint8_t i = 0; i < 8; ++i) {
            g.setup.names[i].clear();
            const std::uint8_t level = g.setup.slots[i].level;
            if (i >= g.setup.players || (level != 4 && level != 5)) continue;
            if (players[i] == 0xff) return error(c, "a human slot the map has no position for");
            Seat seat;
            seat.slot = i, seat.player = players[i];
            g.seats.push_back(seat);
        }
        if (g.seats.size() > cfg.max_humans) return error(c, "more human players than the server allows");
        if (!m.bytes.empty()) {
            // Slice MP9: a saved network game to resume. Its seats, its identity and its tick come from the save; the
            // host's setup must be the save's (the seats' players were worked out from it above).
            std::string why;
            const ByteSpan file(reinterpret_cast<const std::uint8_t*>(m.bytes.data()), m.bytes.size());
            const auto info = sim::OwnSave_ReadInfo(file, &why);
            const auto net = info ? sim::OwnSave_ReadNet(file, &why) : std::nullopt;
            if (!net) return error(c, "not a network game's save" + (why.empty() ? std::string() : ": " + why));
            for (Seat& s : g.seats) {
                const auto it = std::find_if(net->seats.begin(), net->seats.end(),
                                             [&](const sim::OwnSaveSeat& o) { return o.slot == s.slot; });
                if (it == net->seats.end() || it->player != s.player) return error(c, "the save's seats differ");
                s.name = it->name, s.token_check = it->token_check, s.left = it->left != 0, s.ai = it->ai;
                g.setup.names[s.slot] = Utf16(s.name);
            }
            g.uid = net->game_uid, g.resume = m.bytes, g.first_tick = g.clock = info->tick;
        }
        Seat* host = seat_of(g, g.host_slot);
        if (host == nullptr || host->left) return error(c, "the host's slot is not a human slot");
        const std::uint32_t gid = g.id;
        Game& placed = games.emplace(gid, std::move(g)).first->second;
        log("game " + std::to_string(gid) + " '" + placed.name + "' on " + placed.setup.map + " by " + c.name +
            (placed.resume.empty() ? std::string() : ", resumed after tick " + std::to_string(placed.first_tick)));
        take_seat(id, c, placed, *seat_of(placed, placed.host_slot));
    }

    void join_game(std::uint64_t id, Conn& c, const Message& m) {
        if (c.game != 0) return error(c, "already in a game");
        const auto it = games.find(m.game);
        if (it == games.end() || it->second.started) return error(c, "no such open game");
        Game& g = it->second;
        if (c.data_hash != g.data_hash) return error(c, "your game files differ from the host's");
        if (m.password != g.password) return error(c, "wrong password");
        Seat* free = nullptr;
        if (!g.resume.empty() && m.token != 0) {  // slice MP9: a resumed game's player back on his own seat
            for (Seat& s : g.seats) {
                if (!s.joined && !s.left && s.token_check == Token_Check(m.token)) free = &s;
            }
        }
        for (Seat& s : g.seats) {
            if (free != nullptr) break;
            if (!s.joined && !s.left && !s.computer && (m.slot == 0xff || s.slot == m.slot)) free = &s;
        }
        if (free == nullptr) return error(c, "no free player slot");
        take_seat(id, c, g, *free);
    }

    void rejoin(std::uint64_t id, Conn& c, const Message& m) {
        if (c.game != 0) return error(c, "already in a game");
        const auto it = games.find(m.game);
        Seat* s = it == games.end() ? nullptr : seat_of(it->second, m.slot);
        if (s == nullptr || !it->second.started || s->token != m.token || s->conn != 0) {
            return error(c, "no such seat to come back to");
        }
        if (s->left) return error(c, "the seat was given to the computer for the rest of the game");
        Game& g = it->second;
        if (c.data_hash != g.data_hash) return error(c, "your game files differ from the host's");
        s->conn = id, s->want_state = true, s->held_until = 0, c.game = g.id, c.slot = s->slot;
        log(c.name + " is back in game " + std::to_string(g.id) + " slot " + std::to_string(s->slot));
        roster(g);
        ask_save(g);
    }

    // A save for the players waiting for one, from the connected player furthest on.
    void ask_save(Game& g) {
        if (g.save_asked) return;
        Seat* donor = nullptr;
        for (Seat& s : g.seats) {
            if (s.conn != 0 && !s.want_state && (donor == nullptr || s.done_tick > donor->done_tick)) donor = &s;
        }
        if (donor == nullptr) {
            for (Seat& s : g.seats) {
                if (Conn* c = conn_of(s); c && s.want_state) error(*c, "nobody left in the game to send it");
            }
            return;
        }
        Message m;
        m.type = MsgType::SaveRequest;
        send(*conn_of(*donor), m);
        g.save_asked = true;
    }

    void start_game(Conn& c) {
        const auto it = games.find(c.game);
        if (it == games.end() || it->second.started || c.slot != it->second.host_slot) {
            return error(c, "only the host starts a game, once");
        }
        Game& g = it->second;
        for (const Seat& s : g.seats) {
            if (!s.joined && !s.left && !s.computer) return error(c, "not every player slot is taken");
        }
        g.started = true, g.clock = g.first_tick, g.next_at = now + g.tick_ms;
        g.autosave = NetConfig_AutosaveTicks(cfg, g.tick_ms);
        log("game " + std::to_string(g.id) + " starts" +
            (g.resume.empty() ? std::string() : " from its save after tick " + std::to_string(g.first_tick)));
        if (hooks.frames) {
            sim::SkirmishSetup logged = g.setup;
            logged.my_slot = g.host_slot;
            hooks.frames(g.id, NetLog_Start(logged) +
                                   (g.resume.empty() ? std::string() : NetLog_Resume(g.first_tick, g.resume)));
        }
        // A resumed game: who plays each seat now. A seat without its human goes to the computer, a human back on a
        // seat the computer played gets it back, both stamped on the first tick (every game switches together).
        for (Seat& s : g.seats) {
            if (g.resume.empty() || s.left) continue;
            const std::uint8_t want = s.joined ? 0 : static_cast<std::uint8_t>(cfg.ai_level);
            if (want != s.ai) control(g, s, want);
        }
        for (Seat& s : g.seats) {
            Conn* sc = conn_of(s);
            if (sc == nullptr) continue;
            Message m;
            m.type = g.resume.empty() ? MsgType::Start : MsgType::RejoinState;
            m.setup = g.setup, m.value = g.tick_ms, m.setup.my_slot = s.slot;
            m.input_delay = static_cast<std::uint8_t>(g.input_delay),
            m.hash_every = static_cast<std::uint16_t>(g.hash_every);
            m.uid = g.uid, m.autosave = g.autosave, m.seats = seat_list(g);
            m.tick = g.first_tick, m.player = s.player, m.bytes = g.resume;
            send(*sc, m);
            s.done_tick = g.first_tick;
        }
        g.resume.clear(), g.resume.shrink_to_fit();  // only needed for the start (a rejoin asks a player for a save)
    }

    // Slice MP9: the computer takes over the seat's player (level 1..3) or gives it back to its human (0), from a tick
    // the server stamps like an order, so every game switches on the same tick.
    void control(Game& g, Seat& s, std::uint8_t level) {
        const std::uint32_t t = std::max(g.clock + g.input_delay, g.last_control);
        g.pending[t].push_back(Group_Control(s.player, level));
        g.last_control = t, s.ai = level;
        log("game " + std::to_string(g.id) + ": " +
            (level != 0 ? "the computer plays slot " + std::to_string(s.slot) + " (" + s.name + ")"
                        : s.name + " plays slot " + std::to_string(s.slot) + " again") +
            " from tick " + std::to_string(t));
    }

    // Slice MP9: a resumed game's lobby, the host's seat changes: an open seat to the computer and back, a player to
    // another open seat.
    void seat_request(Game& g, const Message& m) {
        if (g.started) return;
        if (m.what == static_cast<std::uint8_t>(RequestKind::SeatComputer)) {
            Seat* s = seat_of(g, static_cast<std::uint8_t>(m.value));
            if (s == nullptr || s->joined || s->left) return;
            s->computer = !s->computer;
        } else {
            Seat* from = seat_of(g, static_cast<std::uint8_t>(m.value & 0xff));
            Seat* to = seat_of(g, static_cast<std::uint8_t>((m.value >> 8) & 0xff));
            if (from == nullptr || to == nullptr || !from->joined || to->joined || to->left || from == to) return;
            if (from->slot == g.host_slot) return;  // the host's own seat stays (the slot of his game)
            const auto cit = conns.find(from->conn);
            if (cit == conns.end()) return;
            Conn& c = cit->second;
            to->joined = true, to->conn = from->conn, to->name = from->name, to->computer = false;
            to->token = tokens(), to->token_check = Token_Check(to->token);
            g.setup.names[to->slot] = Utf16(to->name);
            from->joined = false, from->conn = 0, from->token = 0;
            g.setup.names[from->slot] = Utf16(from->name);  // the save's name stays on the empty seat
            c.slot = to->slot;
            joined(c, g, *to);
            log(to->name + " moved to slot " + std::to_string(to->slot) + " by the host");
        }
        roster(g);
    }

    void order(Game& g, Seat& s, const Message& m) {
        if (m.groups.size() != 1) return log(s.name + ": an order message without exactly one group");
        OrderGroup grp = m.groups[0];
        std::string why;
        if (!Group_Authorize(grp, s.player, &why)) return log(s.name + ": order refused: " + why);
        grp.player = s.player;
        std::uint32_t t = std::max(g.clock + g.input_delay, s.last_stamp);
        // A ui_order takes the sim's next order number: only select_houses may come before it in its frame.
        const bool ui = Group_IsUi(grp);
        for (; ui; ++t) {
            const auto it = g.pending.find(t);
            if (it == g.pending.end() || std::all_of(it->second.begin(), it->second.end(), [](const OrderGroup& o) {
                    return Group_IsSelect(o) || Group_IsControl(o);
                }))
                break;
        }
        g.pending[t].push_back(std::move(grp));
        s.last_stamp = t;
    }

    void hash(Game& g, Seat& s, std::uint32_t tick, std::uint64_t h) {
        s.done_tick = std::max(s.done_tick, tick);  // a hash says how far the player is, more often than a report
        s.most_behind = std::max(s.most_behind, g.clock - std::min(g.clock, s.done_tick));
        if (g.desynced) return;
        auto& at = g.hashes[tick];
        at[s.slot] = h;
        const std::uint64_t first = at.begin()->second;
        Message d;
        d.type = MsgType::Desync, d.tick = tick;
        for (const auto& [slot, v] : at) {
            if (v != first) d.roster.push_back(RosterEntry{slot, seat_of(g, slot)->name, true});
        }
        if (!d.roster.empty()) {
            std::string text = "game " + std::to_string(g.id) + " out of step after tick " + std::to_string(tick) + ":";
            for (const auto& [slot, v] : at) text += " slot " + std::to_string(slot) + " " + Hex(v);
            log(text);
            g.desynced = true;
            broadcast(g, d);
        }
        while (!g.hashes.empty() && g.hashes.begin()->first + 10 * g.hash_every < g.clock)
            g.hashes.erase(g.hashes.begin());
    }

    void report(Game& g, Seat& s, const Message& m) {
        s.done_tick = std::max(s.done_tick, m.tick), s.step_us = m.value;
        s.most_behind = std::max(s.most_behind, g.clock - std::min(g.clock, s.done_tick));
        if (s.confirmed || g.desynced) return;
        if (m.player == s.player) {
            s.confirmed = true;
            return;
        }
        // The client's game puts it on another player than the server's setup does: a forged claim, a host who gave
        // the wrong position count or another map. Its orders were only ever its slot's player's; the game cannot run.
        log("game " + std::to_string(g.id) + ": " + s.name + " reports player " + std::to_string(m.player) + ", slot " +
            std::to_string(s.slot) + " is player " + std::to_string(s.player) + ": stopped");
        g.desynced = true;
        Message d;
        d.type = MsgType::Desync, d.tick = g.clock;
        d.roster.push_back(RosterEntry{s.slot, s.name, true});
        broadcast(g, d);
    }

    void request(Game& g, const Seat& s, const Message& m) {
        if (s.slot != g.host_slot) return log(s.name + ": only the host pauses or sets the speed");
        if (m.what == static_cast<std::uint8_t>(RequestKind::SeatComputer) ||
            m.what == static_cast<std::uint8_t>(RequestKind::SeatMove)) {
            return seat_request(g, m);
        }
        if (m.what == static_cast<std::uint8_t>(RequestKind::Pause)) g.paused = true;
        if (m.what == static_cast<std::uint8_t>(RequestKind::Resume)) g.paused = false, g.next_at = now + pace(g);
        if (m.what == static_cast<std::uint8_t>(RequestKind::Speed)) {
            g.tick_ms = std::clamp(m.value, cfg.min_tick_ms, cfg.max_tick_ms);
        }
        Message p;
        p.type = MsgType::Pace, p.value = g.tick_ms, p.what = g.paused ? 1 : 0;
        broadcast(g, p, true);
    }

    void save_data(Game& g, const Message& m) {
        if (!g.save_asked) return;
        g.save_asked = false;
        for (Seat& s : g.seats) {
            Conn* c = conn_of(s);
            if (c == nullptr || !s.want_state) continue;
            Message r;
            r.type = MsgType::RejoinState, r.tick = m.tick, r.player = s.player, r.bytes = m.bytes, r.setup = g.setup;
            r.setup.my_slot = s.slot, r.value = g.tick_ms;
            r.input_delay = static_cast<std::uint8_t>(g.input_delay),
            r.hash_every = static_cast<std::uint16_t>(g.hash_every);
            r.uid = g.uid, r.autosave = g.autosave, r.seats = seat_list(g);
            send(*c, r);
            for (std::uint32_t t = m.tick + 1; t <= g.clock; ++t) send(*c, frame_message(g, t));
            s.want_state = false, s.done_tick = m.tick;
            log(s.name + " has the game from tick " + std::to_string(m.tick));
            if (s.ai != 0) control(g, s, 0);  // slice MP9: his player back from the computer
        }
    }

    Message frame_message(const Game& g, std::uint32_t t) const {
        Message f;
        f.type = MsgType::Frame, f.tick = t, f.groups = g.sent[t - 1 - g.first_tick];
        return f;
    }

    void advance(Game& g) {
        ++g.clock;
        std::vector<OrderGroup> frame;
        if (const auto it = g.pending.find(g.clock); it != g.pending.end()) {
            frame = std::move(it->second);
            g.pending.erase(it);
        }
        g.sent.push_back(std::move(frame));
        broadcast(g, frame_message(g, g.clock));
        if (hooks.frames && !g.sent.back().empty()) hooks.frames(g.id, NetLog_Frame(g.clock, g.sent.back()));
    }

    void run_clock(Game& g) {
        if (!g.started || g.paused || g.desynced) return;
        for (const Seat& s : g.seats) {
            if (s.conn != 0 && !s.want_state && !s.confirmed) {
                g.next_at = now;  // held until every player's game has said it is on the server's player
                return;
            }
            if (s.conn != 0 && !s.want_state && cfg.max_lag_ticks != 0 && s.done_tick + cfg.max_lag_ticks < g.clock) {
                g.next_at = now;  // held: a player is too far behind
                return;
            }
        }
        const std::uint32_t p = pace(g);
        for (std::uint32_t n = 0; now >= g.next_at && n < cfg.catch_up_ticks; ++n) {
            advance(g);
            g.next_at += p;
        }
        if (now > g.next_at + 10 * static_cast<std::int64_t>(p)) g.next_at = now;  // the server itself was stalled
    }

    void dropped(std::uint64_t id, Conn& c) {
        const auto it = games.find(c.game);
        Seat* s = it == games.end() ? nullptr : seat_of(it->second, c.slot);
        // What the link carried (slice MP8): the bytes each way and, in a started game, how far it fell behind.
        const std::int64_t ms = std::max<std::int64_t>(now - c.opened_ms, 1);
        std::string what = "connection " + std::to_string(id) + " (" + c.name + ") closed after " +
                           std::to_string(ms / 1000) + " s: " + std::to_string(c.bytes_out) + " bytes sent (" +
                           std::to_string(c.bytes_out * 1000 / static_cast<std::uint64_t>(ms)) + " a second), " +
                           std::to_string(c.bytes_in) + " received";
        if (s != nullptr && s->conn == id && it->second.started) {
            what += ", tick " + std::to_string(s->done_tick) + " of " + std::to_string(it->second.clock) +
                    ", at most " + std::to_string(s->most_behind) + " ticks behind, last step " +
                    std::to_string(s->step_us) + " us";
        }
        log(what);
        if (it == games.end()) return;
        Game& g = it->second;
        if (s == nullptr || s->conn != id) return;
        s->conn = 0, s->want_state = false;
        if (!g.started) {
            s->joined = false;
            if (g.resume.empty()) s->name.clear(), g.setup.names[s->slot].clear();  // a resumed game keeps the save's
            if (s->slot == g.host_slot) {
                for (Seat& o : g.seats) {
                    if (Conn* oc = conn_of(o)) error(*oc, "the host left");
                    o.conn = 0;
                }
                for (auto& [cid, cc] : conns) {
                    if (cc.game == g.id) cc.game = 0;
                }
                log("game " + std::to_string(g.id) + " closed (the host left the lobby)");
                games.erase(it);
                return;
            }
            return roster(g);
        }
        // Slice MP9: a player who said Leave gives his seat up; a dropped one keeps it seat_hold_s for a rejoin. The
        // computer plays either one's player meanwhile (from a stamped tick).
        if (c.leaving) {
            s->left = true;
        } else {
            s->held_until = cfg.seat_hold_s == 0 ? std::numeric_limits<std::int64_t>::max()
                                                 : now + static_cast<std::int64_t>(cfg.seat_hold_s) * 1000;
        }
        if (cfg.ai_level != 0 && s->ai == 0) control(g, *s, static_cast<std::uint8_t>(cfg.ai_level));
        Message m;
        m.type = MsgType::PlayerLeft, m.slot = s->slot, m.what = s->left ? 0 : 1;
        broadcast(g, m);
        roster(g);
        if (std::none_of(g.seats.begin(), g.seats.end(), [](const Seat& o) { return o.conn != 0; })) {
            log("game " + std::to_string(g.id) + " ended at tick " + std::to_string(g.clock) + " (everyone left)");
            games.erase(it);
            return;
        }
        if (g.save_asked) g.save_asked = false, ask_save(g);  // the donor may have been this one
    }

    void poll(std::int64_t now_ms) {
        now = now_ms;
        accept();
        std::vector<std::uint64_t> closed;
        for (auto& [id, c] : conns) {
            std::string bytes, err;
            while (c.link->state() != LinkState::Closed && c.link->receive(&bytes)) {
                c.bytes_in += bytes.size();
                const auto m = Message_Decode(bytes, &err);
                if (!m) {
                    error(c, "bad message: " + err);
                    break;
                }
                on_message(id, c, *m);
            }
            if (!c.hello && now - c.opened_ms > static_cast<std::int64_t>(cfg.hello_timeout_ms)) c.link->close();
            if (c.link->state() == LinkState::Closed) closed.push_back(id);
        }
        for (const std::uint64_t id : closed) {
            dropped(id, conns.at(id));
            conns.erase(id);
        }
        for (auto& [id, g] : games) {
            expire_seats(g);
            run_clock(g);
        }
    }

    // Slice MP9: a dropped player not back within seat_hold_s gives the seat up (the computer plays it to the end).
    void expire_seats(Game& g) {
        if (!g.started) return;
        bool changed = false;
        for (Seat& s : g.seats) {
            if (s.conn != 0 || s.left || s.held_until == 0 || now < s.held_until) continue;
            s.left = true, s.held_until = 0, changed = true;
            log("game " + std::to_string(g.id) + ": " + s.name + " did not come back in time, slot " +
                std::to_string(s.slot) + " stays with the computer");
        }
        if (changed) roster(g);
    }
};

Server::Server(const NetConfig& config, std::string name, std::string build, ServerHooks hooks)
    : impl_(std::make_unique<Impl>()) {
    impl_->cfg = config, impl_->name = std::move(name), impl_->build = std::move(build);
    impl_->hooks = std::move(hooks);
}

Server::~Server() = default;

void Server::listen(std::unique_ptr<Listener> listener) { impl_->listeners.push_back(std::move(listener)); }

void Server::poll(std::int64_t now_ms) { impl_->poll(now_ms); }

std::vector<Server::GameStatus> Server::games() const {
    std::vector<GameStatus> out;
    for (const auto& [id, g] : impl_->games) {
        GameStatus st{id, g.started, g.paused, g.desynced, g.clock, impl_->pace(g), 0};
        for (const Seat& s : g.seats) st.connected += s.conn != 0 ? 1 : 0;
        out.push_back(st);
    }
    return out;
}

}  // namespace antanta::net
