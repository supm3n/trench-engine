// The transport under the network game (docs/multiplayer.md "Transport"): whole messages, in order, both ways. Three
// kinds behind one interface: the in-process loopback (tests, and the desktop host's own player), WebSocket on the
// desktop (IXWebSocket, net/ws_native.cpp) and WebSocket in the browser (emscripten/websocket.h, net/ws_web.cpp).
// Connections are polled from one thread; a WebSocket's own thread only fills its inbox.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace antanta::net {

enum class LinkState : std::uint8_t { Connecting, Open, Closed };

class Connection {
  public:
    virtual ~Connection() = default;
    virtual void send(const std::string& message) = 0;
    // The next whole message received, false when none is waiting.
    virtual bool receive(std::string* message) = 0;
    virtual LinkState state() const = 0;
    virtual void close() = 0;
    virtual std::string peer() const = 0;  // for the log
    // True when the link never opened (a join to an address nobody answers); only the WebSocket client knows.
    virtual bool never_opened() const { return false; }
    // The transport's own words for why the link failed or closed, "" when it gave none (for the log).
    virtual std::string failure() const { return {}; }
};

class Listener {
  public:
    virtual ~Listener() = default;
    // A connection opened since the last call, or nullptr.
    virtual std::unique_ptr<Connection> accept() = 0;
};

// ---- loopback: an in-process network with a clock, for the tests and the desktop host's own player ----

// Faults of one loopback link (the tests' delayed client): each message arrives delay_ms after it was sent.
struct LinkFaults {
    std::int64_t up_delay_ms = 0;    // client -> server
    std::int64_t down_delay_ms = 0;  // server -> client
};

// One loopback link: the messages on their way with the time they arrive, and the closed flag either end sets.
struct LoopbackPipe {
    std::deque<std::pair<std::int64_t, std::string>> up, down;  // client -> server, server -> client
    LinkFaults faults;
    bool closed = false;
};

struct LoopbackHub {
    std::function<std::int64_t()> clock;  // milliseconds (a test may pass a clock it moves itself)
    std::vector<std::unique_ptr<LoopbackPipe>> pipes;
    std::map<std::string, std::deque<LoopbackPipe*>> waiting;  // per listener name: links not accepted yet
};

std::unique_ptr<LoopbackHub> Loopback_NewHub(std::function<std::int64_t()> clock);
// The hub must outlive every listener and connection made from it.
std::unique_ptr<Listener> Loopback_Listen(LoopbackHub& hub, const std::string& name);
// nullptr when no listener has that name.
std::unique_ptr<Connection> Loopback_Connect(LoopbackHub& hub, const std::string& name, const LinkFaults& faults);

// ---- WebSocket (plain ws://, no TLS yet: a LAN or VPN; TLS comes with the server step) ----

// Listens on ws://<host>:<port>/ (host "0.0.0.0": every interface, on IPv6 ("::") too where the PC has it, decision
// 55; an IPv6 host such as "::1": IPv6 only). nullptr and the reason when it cannot.
std::unique_ptr<Listener> Ws_Listen(const std::string& host, int port, std::string* error);
// Connects to a ws:// URL; the connection starts as Connecting. nullptr and the reason for a bad URL.
std::unique_ptr<Connection> Ws_Connect(const std::string& url, std::string* error);

}  // namespace antanta::net
