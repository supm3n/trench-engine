// The lockstep driver of a network game (apps/common/net_game.hpp).
#include "net_game.hpp"

#include <ctime>

#include "net/lockstep.hpp"
#include "sim/ownsave.hpp"

namespace antanta {

std::string NetGame_Tick(sim::GameState& s, const std::vector<net::OrderGroup>& frame, const FileLoader& load,
                         const std::vector<BasicParams>& params, std::vector<std::string>* warnings, bool lines) {
    const std::uint32_t enter = s.tick;
    std::vector<sim::Command> now = view_inputs(view_box_whole_map(s), s);
    load_house_depth(load, params, now, s, warnings);
    std::vector<sim::Command> orders = net::Lockstep_Commands(s, frame);
    now.insert(now.end(), orders.begin(), orders.end());
    sim::step(s, nullptr, now);
    return lines ? net::NetTrace_Tick(s, enter) : std::string();
}

namespace {

// The seats' computer flags of a save's meta from the state (as the save is made).
void seats_ai(sim::OwnSaveMeta& meta, const sim::GameState& s) {
    if (!meta.net) return;
    for (sim::OwnSaveSeat& seat : meta.net->seats) {
        if (seat.player < s.players.size()) seat.ai = s.players[seat.player].net_ai;
    }
}

}  // namespace

NetTickResult NetGame_RunTick(sim::GameState& s, NetTick& t, const FileLoader& load,
                              const std::vector<BasicParams>& params, std::vector<std::string>* warnings) {
    NetTickResult r;
    const auto t0 = std::chrono::steady_clock::now();
    r.lines = NetGame_Tick(s, t.frame, load, params, warnings, t.lines);
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0);
    r.step_us = static_cast<std::uint64_t>(us.count());
    r.tick = s.tick;
    if (t.hash) r.hash = sim::GameState_NetHash(s);
    if (t.autosave) {
        const auto a0 = std::chrono::steady_clock::now();
        seats_ai(*t.autosave, s);
        r.autosave = sim::OwnSave_Prepare(s, *t.autosave);
        r.autosave_us = static_cast<std::uint32_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - a0).count());
    }
    return r;
}

std::uint64_t NetGame_DataHash(const FileLoader& load) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    const char* const files[] = {"Data/BasicParams0.dat",    "Data/BasicParams1.dat",     "Data/BasicParams2.dat",
                                 "Data/BasicParams3.dat",    "Data/BasicParams4.dat",     "Data/BasicParams5.dat",
                                 "Data/Objects/objects.les", "Data/ShipTowersParams.txt", "Data/Template4.tpl"};
    for (const char* f : files) {
        std::string err;
        const auto bytes = load(f, &err);
        const std::uint64_t n = bytes ? bytes->size() : 0xffffffffull;  // a missing file counts too
        for (int k = 0; k < 8; ++k) h = (h ^ ((n >> (8 * k)) & 0xff)) * 0x100000001b3ull;
        if (!bytes) continue;
        for (const std::uint8_t b : *bytes) h = (h ^ b) * 0x100000001b3ull;
    }
    return h;
}

NetGame::NetGame(net::Client& client, NetGameHooks hooks) : client_(client), hooks_(std::move(hooks)) {}

bool NetGame::begin() {
    if (auto r = client_.take_rejoin()) {
        // Back after a drop: another player's save after tick r->tick, MyPlayer ours; the view's end state is ours too.
        auto s = std::make_unique<sim::GameState>();
        sim::OwnSaveMeta meta;
        const std::string& b = r->bytes;
        const Bytes file(reinterpret_cast<const std::uint8_t*>(b.data()),
                         reinterpret_cast<const std::uint8_t*>(b.data()) + b.size());
        if (!sim::OwnSave_Read(file, s.get(), &meta, &error_)) return false;
        std::vector<std::string> warnings;
        load_saved_depth(hooks_.load, *hooks_.params, *s, &warnings);
        s->my_player = r->player, s->paused = 0, s->game_result = 0, s->end_message = 0;
        state_ = hooks_.adopt(std::move(s), std::move(meta.ground));
        if (!state_) {
            error_ = "the game could not take the saved state";
            return false;
        }
        loaded_at_ = r->tick, hash_every_ = r->hash_every, autosave_every_ = r->autosave;
        tick_ = next_ = state_->tick, my_player_ = state_->my_player;
        // Slice MP9: the loaded state's hash at once, so the server compares every player's (a resumed game: all of
        // them load the same save; a rejoin: the others' hashes of that tick when it was one of the hash ticks).
        client_.send_hash(state_->tick, sim::GameState_NetHash(*state_));
        report(true);
        return true;
    }
    if (state_ || !client_.start()) return true;
    const net::Message& st = *client_.start();
    state_ = hooks_.new_game(st.setup, &error_);
    if (!state_) return false;
    hash_every_ = st.hash_every, autosave_every_ = st.autosave;
    tick_ = next_ = state_->tick, my_player_ = state_->my_player;
    // Slice G8: the start's hash at once, before tick 1: it covers the ground (the map each machine loaded or built
    // from a generated map's code), so a different map stops the game before it runs.
    client_.send_hash(state_->tick, sim::GameState_NetHash(*state_));
    report(true);
    return true;
}

sim::OwnSaveMeta NetGame::save_meta() const {
    sim::OwnSaveMeta meta = client_meta();
    if (state_) seats_ai(meta, *state_);
    return meta;
}

sim::OwnSaveMeta NetGame::client_meta() const {
    sim::OwnSaveMeta meta;
    meta.info.name = "network game";
    meta.setup = client_.setup();
    sim::OwnSaveNet net;
    net.game_uid = client_.uid();
    if (client_.start()) net.seats = client_.start()->seats;
    for (sim::OwnSaveSeat& seat : net.seats) {
        for (const net::RosterEntry& r : client_.roster()) {
            if (r.slot == seat.slot && r.computer == 2) seat.left = 1;  // as this player last heard from the server
        }
        // The players' names for the lists (the load screen's player line; the resume list).
        if (!seat.name.empty()) meta.info.player += (meta.info.player.empty() ? "" : ", ") + seat.name;
    }
    meta.net = std::move(net);
    meta.ground = ground();  // slice G8: a generated map's ground (GRND)
    return meta;
}

std::shared_ptr<const MapData> NetGame::ground() const {
    if (!hooks_.ground || !state_) return nullptr;
    std::string code;
    std::shared_ptr<const MapData> g = hooks_.ground(&code);
    return g && code == state_->map_name ? g : nullptr;
}

void NetGame::report(bool now) {
    const auto t = std::chrono::steady_clock::now();
    if (!now && t - last_report_ < std::chrono::milliseconds(hooks_.report_ms)) return;
    last_report_ = t;
    client_.send_report(tick_, steps_ == 0 ? 0 : static_cast<std::uint32_t>(step_us_sum_ / steps_), my_player_);
    step_us_sum_ = 0, steps_ = 0;
}

std::optional<NetTick> NetGame::next_tick(bool lines) {
    if (!state_ || !client_.has_frame(next_ + 1)) return std::nullopt;
    NetTick t;
    t.tick = ++next_;
    t.frame = client_.take_frame(t.tick);
    t.lines = lines;
    t.hash = hash_every_ != 0 && t.tick % hash_every_ == 0;
    if (hooks_.autosave && autosave_every_ != 0 && t.tick % autosave_every_ == 0) {
        sim::OwnSaveMeta meta = client_meta();
        meta.info.name = "network autosave";
        meta.info.saved_at = static_cast<std::int64_t>(std::time(nullptr));
        t.autosave = std::move(meta);
    }
    return t;
}

void NetGame::tick_done(NetTickResult& r) {
    tick_ = r.tick;
    step_us_sum_ += r.step_us, ++steps_;
    if (r.hash) client_.send_hash(r.tick, *r.hash);
    if (r.autosave) {
        autosave_us_ = r.autosave_us;
        ++autosaves_;
        hooks_.autosave(r.tick, std::move(*r.autosave));
    }
}

void NetGame::send_save(const sim::GameState& s) {
    sim::OwnSaveMeta meta;
    meta.info.name = "rejoin";
    meta.setup = client_.setup();
    meta.ground = ground();  // slice G8: the player who comes back takes the same ground at once
    const Bytes file = sim::OwnSave_Write(s, meta);
    client_.send_save(s.tick, std::string(file.begin(), file.end()));
}

int NetGame::run(int max_ticks, std::string* trace) {
    if (!begin()) return -1;
    if (!state_) return 0;
    int ran = 0;
    std::vector<std::string> warnings;
    const bool want = trace != nullptr || hooks_.trace;
    while (ran < max_ticks) {
        std::optional<NetTick> t = next_tick(want);
        if (!t) break;
        NetTickResult r = NetGame_RunTick(*state_, *t, hooks_.load, *hooks_.params, &warnings);
        if (hooks_.after_tick) hooks_.after_tick(r.lines);
        if (trace) *trace += r.lines;
        ++ran;
        tick_done(r);
    }
    // A save for a player back after a drop: the state after the last tick run.
    if (save_asked()) send_save(*state_);
    report(false);
    return ran;
}

}  // namespace antanta
