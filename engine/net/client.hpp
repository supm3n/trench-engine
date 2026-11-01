// A player's end of the network game (docs/multiplayer.md "Protocol"): the hello, the lobby, and in a started game
// the frames by tick, the pacing, the desync notice and the server's requests. It runs no game: the driver
// (apps/common/net_game.hpp) takes the frames and steps the sim. Polled from one thread.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "net/protocol.hpp"
#include "net/transport.hpp"

namespace antanta::net {

enum class ClientPhase : std::uint8_t {
    Connecting,  // the link is opening or the hello is not answered yet
    Lobby,       // welcomed, in no game
    Joined,      // in a game that has not started
    Playing,     // the game started (or a rejoin's save arrived)
    Closed,      // the link closed or the server refused (error() says why)
};

// error() of a join whose link never opened starts with this, then the address as typed ("host:port"): the menu shows
// its own text for it (key net.no_answer) with the address.
inline constexpr const char* kNoAnswerPrefix = "no game answered at ";

class Client {
  public:
    Client(std::unique_ptr<Connection> link, std::string name, std::string build, std::uint64_t data_hash);

    // Reads every message waiting; sends the hello once the link is open.
    void poll();

    void list_games();
    // positions: the map's player records (the server works out each slot's player from them); tick_ms: the host's
    // speed (the server keeps it within its limits); password: empty for none.
    // resume (slice MP9): our own save of a network game to resume (its seats; setup must be its SETU chunk with the
    // host's slot), empty for a new game.
    void create_game(const std::string& name, const sim::SkirmishSetup& setup, std::uint8_t positions,
                     std::uint32_t tick_ms, const std::string& password = {}, std::string resume = {});
    // token (slice MP9): for a resumed game, the seat token this player kept for it (0: none, the host places him).
    void join_game(std::uint32_t game, std::uint8_t slot = 0xff, const std::string& password = {},
                   std::uint64_t token = 0);
    void rejoin(std::uint32_t game, std::uint8_t slot, std::uint64_t token);
    void start_game();
    void leave();

    // In a started game: one action's commands (net/lockstep.hpp groups), the hash after a tick, the step time.
    void send_order(std::vector<sim::Command> group);
    void send_hash(std::uint32_t tick, std::uint64_t hash);
    void send_report(std::uint32_t tick, std::uint32_t step_us, std::uint8_t player);
    void send_request(RequestKind kind, std::uint32_t value = 0);
    void send_save(std::uint32_t tick, std::string bytes);
    // Drops the link without a goodbye (the tests' dropped client).
    void drop();

    ClientPhase phase() const { return phase_; }
    const std::string& error() const { return error_; }
    const std::vector<GameInfo>& games() const { return games_; }
    const std::vector<RosterEntry>& roster() const { return roster_; }
    std::uint32_t game() const { return game_; }
    std::uint8_t slot() const { return slot_; }
    std::uint64_t token() const { return token_; }
    bool host() const { return host_; }
    std::uint64_t uid() const { return uid_; }  // the game's identity (Joined, Start, RejoinState; slice MP9)
    const sim::SkirmishSetup& setup() const { return setup_; }

    // The started game: Start's message (setup with my_slot, pace, input delay, hash interval).
    const std::optional<Message>& start() const { return start_; }
    // A rejoin's save (RejoinState), taken once by the driver.
    std::optional<Message> take_rejoin();
    // The server asked for a save after the next tick (taken once).
    bool take_save_request();
    // The frame of tick t when it has arrived (taken once, in order).
    bool has_frame(std::uint32_t t) const { return frames_.count(t) != 0; }
    std::vector<OrderGroup> take_frame(std::uint32_t t);
    std::uint32_t last_frame() const { return frames_.empty() ? 0 : frames_.rbegin()->first; }
    std::uint32_t tick_ms() const { return tick_ms_; }
    bool paused() const { return paused_; }
    // A desync the server found: the tick and the slots out of step (empty: none).
    std::optional<Message> desync() const { return desync_; }
    // Slots the server said left (PlayerLeft), in order.
    std::vector<std::uint8_t> take_left();

  private:
    void send(const Message& m);
    void on(const Message& m);

    std::unique_ptr<Connection> link_;
    std::string name_, build_;
    std::uint64_t data_hash_ = 0;
    bool hello_sent_ = false;
    ClientPhase phase_ = ClientPhase::Connecting;
    std::string error_;
    std::vector<GameInfo> games_;
    std::vector<RosterEntry> roster_;
    std::uint32_t game_ = 0;
    std::uint8_t slot_ = 0xff;
    std::uint64_t token_ = 0, uid_ = 0;
    bool host_ = false;
    sim::SkirmishSetup setup_;
    std::optional<Message> start_, rejoin_, desync_;
    bool save_request_ = false;
    std::map<std::uint32_t, std::vector<OrderGroup>> frames_;
    std::uint32_t tick_ms_ = 0;
    bool paused_ = false;
    std::vector<std::uint8_t> left_;
};

}  // namespace antanta::net
