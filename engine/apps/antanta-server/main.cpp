// antanta-server: the network game's server (docs/multiplayer.md), several games in one process, a pure relay (it
// runs no game and reads no game files).
//
// usage: antanta-server [--port <n>] [--bind <address>] [--log-dir <dir>] [--set <name>=<value>]...
//                       [--exit-after-game] [--max-seconds <n>]
// --port: the WebSocket port (default 7777); --bind: the address to listen on (default 0.0.0.0, every interface).
// --log-dir: each game's order log as game-<id>.log there (net/lockstep.hpp NetLog; antanta-headless --net-log
//            replays it).
// --set: one number of net/config.hpp (tick_ms, input_delay, hash_every, ...), repeatable.
// --exit-after-game: ends (exit 0) once a game has started and every game is over (the tests' and the gate's runs).
// --max-seconds: ends with exit 1 after that long (a test's safety stop).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "net/config.hpp"
#include "net/lockstep.hpp"
#include "net/server.hpp"
#include "net/transport.hpp"

using namespace antanta;

namespace {

int fail(const std::string& message) {
    std::fprintf(stderr, "antanta-server: %s\n", message.c_str());
    return 2;
}

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

int main(int argc, char** argv) {
    net::NetConfig cfg;
    int port = 7777;
    std::string bind = "0.0.0.0", log_dir;
    bool exit_after_game = false;
    long max_seconds = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (a == "--bind" && i + 1 < argc) {
            bind = argv[++i];
        } else if (a == "--log-dir" && i + 1 < argc) {
            log_dir = argv[++i];
        } else if (a == "--set" && i + 1 < argc) {
            const std::string kv = argv[++i];
            const std::size_t eq = kv.find('=');
            if (eq == std::string::npos || !net::NetConfig_Set(cfg, kv.substr(0, eq), kv.substr(eq + 1))) {
                return fail("--set: not a setting: " + kv);
            }
        } else if (a == "--exit-after-game") {
            exit_after_game = true;
        } else if (a == "--max-seconds" && i + 1 < argc) {
            max_seconds = std::atol(argv[++i]);
        } else {
            return fail("unknown argument " + a +
                        "\nusage: antanta-server [--port <n>] [--bind <address>] [--log-dir <dir>] "
                        "[--set <name>=<value>]... [--exit-after-game] [--max-seconds <n>]");
        }
    }
    net::ServerHooks hooks;
    hooks.log = [](const std::string& line) { std::fprintf(stderr, "antanta-server: %s\n", line.c_str()); };
    if (!log_dir.empty()) {
        hooks.frames = [log_dir](std::uint32_t game, const std::string& lines) {
            const std::string path = log_dir + "/game-" + std::to_string(game) + ".log";
            if (std::FILE* f = std::fopen(path.c_str(), "ab")) {
                std::fwrite(lines.data(), 1, lines.size(), f);
                std::fclose(f);
            }
        };
    }
    net::Server server(cfg, "antanta-server", net::Net_BuildId(), hooks);
    std::string err;
    auto listener = net::Ws_Listen(bind, port, &err);
    if (!listener) return fail("cannot listen on " + bind + ":" + std::to_string(port) + ": " + err);
    server.listen(std::move(listener));
    std::fprintf(stderr, "antanta-server: build %s, listening on ws://%s:%d/\n%s", net::Net_BuildId().c_str(),
                 bind.c_str(), port, net::NetConfig_Text(cfg).c_str());
    const std::int64_t start = now_ms();
    bool had_game = false;
    for (;;) {
        server.poll(now_ms());
        const auto games = server.games();
        for (const auto& g : games) had_game = had_game || g.started;
        if (exit_after_game && had_game && games.empty()) return 0;
        if (max_seconds > 0 && now_ms() - start > max_seconds * 1000) return fail("--max-seconds reached");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
