// The network game's server (docs/multiplayer.md "Protocol"): a lobby with several games in one process, and for each
// started game the lockstep relay. It stamps every order with its tick (the server's clock plus the input delay) and
// the player of the connection's slot, sends each tick's frame to every player, compares the players' hashes, paces
// the clock (the host's speed, capped by the slowest player's step time, held by a player too far behind) and lets a
// dropped player back in from another player's save. It runs no game itself (a pure relay, no game files): the
// referee comes in a later step. Used by apps/antanta-server and, in-process, by the desktop game for direct play.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "net/config.hpp"
#include "net/transport.hpp"

namespace antanta::net {

struct ServerHooks {
    // A line for the server's log (connections, games, refused orders, desyncs).
    std::function<void(const std::string&)> log;
    // The order log of a game as it grows: the NetLog_Frame lines of each frame sent (net/lockstep.hpp).
    std::function<void(std::uint32_t game, const std::string& lines)> frames;
};

class Server {
  public:
    // build: the build id a client must have (empty: any).
    Server(const NetConfig& config, std::string name, std::string build, ServerHooks hooks);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void listen(std::unique_ptr<Listener> listener);
    // Accepts, reads and answers every connection, and runs the started games' clocks up to now_ms.
    void poll(std::int64_t now_ms);

    struct GameStatus {
        std::uint32_t id = 0;
        bool started = false, paused = false, desynced = false;
        std::uint32_t tick = 0;     // the last frame sent
        std::uint32_t tick_ms = 0;  // the pace now (the host's speed or the slowest player's)
        std::uint8_t connected = 0;
    };
    std::vector<GameStatus> games() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace antanta::net
