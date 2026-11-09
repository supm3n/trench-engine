// The lockstep driver of a network game (docs/multiplayer.md): one machine's game, stepped only when its tick's frame
// from the server has arrived. Shared by the game app (--host, --join), the tests' headless players and
// antanta-headless --net-log (the replay of a server's order log), so all three step a tick the same way: the view
// inputs of the whole map (every house drawn, the owner's rule for a new game), made locally and put first, then the
// frame's orders numbered by net::Lockstep_Commands.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "apps/common/view_input.hpp"
#include "formats/map.hpp"
#include "net/client.hpp"
#include "net/protocol.hpp"
#include "sim/ownsave.hpp"
#include "sim/state.hpp"

namespace antanta {

// One tick of a network game on s: the whole map's view inputs and the frame's orders, then step(). Returns the
// tick's net trace lines (net::NetTrace_Tick) when `lines` is set, else nothing: the lines (and the full state hash of
// their G line) cost more than the step itself in a big game (docs/performance.md "Several cores"), and only a trace
// file reads them.
std::string NetGame_Tick(sim::GameState& s, const std::vector<net::OrderGroup>& frame, const FileLoader& load,
                         const std::vector<BasicParams>& params, std::vector<std::string>* warnings, bool lines);

// Slice S3 (docs/sim-thread.md): one tick of a network game split in two. The main thread takes the frame from the
// client and fixes what the tick needs from it (NetGame::next_tick); the tick runs where the game's state is (the app's
// sim thread, or NetGame::run): the step, the hash of a hash tick, the in-memory part of an autosave
// (NetGame_RunTick); then the main thread sends and writes the results (NetGame::tick_done).
struct NetTick {
    std::uint32_t tick = 0;  // the tick it runs (the state's tick after it)
    std::vector<net::OrderGroup> frame;
    bool lines = false;                        // the net trace lines wanted
    bool hash = false;                         // a hash tick: the state's net hash after it
    std::optional<sim::OwnSaveMeta> autosave;  // an autosave tick: the save's meta (the seats' computer flags not yet)
};
struct NetTickResult {
    std::uint32_t tick = 0;
    std::string lines;
    std::optional<std::uint64_t> hash;
    std::optional<sim::OwnSavePending> autosave;
    std::uint32_t autosave_us = 0;  // its OwnSave_Prepare
    std::uint64_t step_us = 0;
};
NetTickResult NetGame_RunTick(sim::GameState& s, NetTick& t, const FileLoader& load,
                              const std::vector<BasicParams>& params, std::vector<std::string>* warnings);

struct NetGameHooks {
    // A new skirmish from the start's setup (setup.net set, my_slot this machine's slot). The caller keeps the state.
    std::function<sim::GameState*(const sim::SkirmishSetup& setup, std::string* error)> new_game;
    // Back after a drop: the state loaded from another player's save; the caller takes it over. ground: the save's
    // generated map (its GRND chunk, a resumed game's autosave); none: the map file, or the map built from the code.
    std::function<sim::GameState*(std::unique_ptr<sim::GameState> state, std::shared_ptr<const MapData> ground)> adopt;
    // Slice G8: the generated map the app holds and (code) the map code it was built from, for the GRND chunk of our
    // own saves of the game (the autosaves and the rejoin's save too); null: none. NetGame writes it only when the
    // code is the game's map name, so a ground left from an earlier game is never saved.
    std::function<std::shared_ptr<const MapData>(std::string* code)> ground;
    // After each tick (the app's sounds and messages, its trace file); may be empty. trace_lines is empty unless
    // `trace` is set (or run() was given a trace to fill).
    std::function<void(const std::string& trace_lines)> after_tick;
    bool trace = false;
    // Slice MP9: every autosave interval (the server's, in Start) the state after that tick, prepared for our own save
    // with the game's seats (OwnSave_Prepare: the in-memory part); the caller packs and writes it (OwnSave_Finish, on
    // another thread in the app). Empty: no autosaves.
    std::function<void(std::uint32_t tick, sim::OwnSavePending save)> autosave;
    FileLoader load;
    const std::vector<BasicParams>* params = nullptr;
    std::uint32_t report_ms = 1000;  // how often the step time goes to the server
};

// The game data hash compared at the join: FNV-1a over the data files the sim reads (the maps are checked by the
// desync hash: a different map shows on the first hash).
std::uint64_t NetGame_DataHash(const FileLoader& load);

class NetGame {
  public:
    NetGame(net::Client& client, NetGameHooks hooks);

    // Builds the game when the start (or a rejoin's save) has arrived, then runs up to max_ticks ticks whose frames
    // are there, answering the server (hashes, step time, a save it asked for). Appends each tick's net trace lines
    // to *trace when given. Returns the ticks run; -1 on an error (error() says it).
    int run(int max_ticks, std::string* trace);

    // Slice S3, the same driver for a game whose ticks run elsewhere (the app's sim thread). begin(): the game built
    // when the start or a rejoin's save has arrived (false on an error); while the caller holds the game.
    bool begin();
    // The next tick when its frame is there, its inputs fixed (lines: the net trace lines wanted); none otherwise.
    std::optional<NetTick> next_tick(bool lines);
    // After the tick ran: the hash sent, the autosave handed to the hooks, the step time counted.
    void tick_done(NetTickResult& r);
    // A save the server asked for (a player back after a drop) answered with s, the state after the last tick run;
    // false when none was asked for. The step time reported when due.
    bool save_asked() { return client_.take_save_request(); }
    void send_save(const sim::GameState& s);
    void report_due() { report(false); }

    sim::GameState* state() const { return state_; }
    const std::string& error() const { return error_; }
    // The tick the game was loaded at on a rejoin (0: it started at tick 0).
    std::uint32_t loaded_at() const { return loaded_at_; }
    // Slice MP9: what our own save of this game holds besides the state (the setup, the seats); for a manual save too.
    sim::OwnSaveMeta save_meta() const;
    // The microseconds the last autosave's in-memory part took (OwnSave_Prepare at the tick), and how many were made.
    std::uint32_t autosave_us() const { return autosave_us_; }
    std::uint32_t autosaves() const { return autosaves_; }

  private:
    void report(bool now);
    sim::OwnSaveMeta client_meta() const;
    std::shared_ptr<const MapData> ground() const;  // the hook's ground when it is this game's map

    net::Client& client_;
    NetGameHooks hooks_;
    sim::GameState* state_ = nullptr;
    std::string error_;
    std::uint32_t loaded_at_ = 0;
    std::uint32_t tick_ = 0;      // the last tick run (the state's tick when built), for the reports
    std::uint32_t next_ = 0;      // the last tick handed out by next_tick
    std::uint8_t my_player_ = 0;  // the state's my_player
    std::uint32_t hash_every_ = 0, autosave_every_ = 0, autosave_us_ = 0, autosaves_ = 0;
    std::chrono::steady_clock::time_point last_report_{};
    std::uint64_t step_us_sum_ = 0;
    std::uint32_t steps_ = 0;
};

}  // namespace antanta
