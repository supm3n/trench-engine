// WebSocket on the desktop (net/transport.hpp): IXWebSocket 12.0.1 (engine/THIRD_PARTY.md), plain ws:// without TLS
// for now. IXWebSocket runs one thread per connection (and one for the server's accept loop); its callbacks only fill
// an inbox under a mutex, which the owner's thread empties with receive(). Sends go straight to the library (its send
// is thread safe). The library hands its sockets out as std::shared_ptr / std::weak_ptr: only this glue file holds
// them.
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

#include "transport.hpp"

namespace antanta::net {

namespace {

// What a connection's thread hands to the owner: the messages and the link state.
struct Inbox {
    mutable std::mutex m;
    std::deque<std::string> messages;
    std::atomic<LinkState> state{LinkState::Connecting};
    std::atomic<bool> opened{false};  // the handshake finished at least once
    std::string reason;               // why the link failed or closed (the library's words), under m
    void on(const ix::WebSocketMessagePtr& msg) {
        switch (msg->type) {
            case ix::WebSocketMessageType::Open:
                opened = true;
                state = LinkState::Open;
                break;
            case ix::WebSocketMessageType::Message: {
                const std::lock_guard<std::mutex> lock(m);
                messages.push_back(msg->str);
                break;
            }
            case ix::WebSocketMessageType::Close: {
                const std::lock_guard<std::mutex> lock(m);
                if (reason.empty()) reason = msg->closeInfo.reason;
                state = LinkState::Closed;
                break;
            }
            case ix::WebSocketMessageType::Error: {
                const std::lock_guard<std::mutex> lock(m);
                if (reason.empty()) reason = msg->errorInfo.reason;
                state = LinkState::Closed;
                break;
            }
            default: break;
        }
    }
    bool take(std::string* out) {
        const std::lock_guard<std::mutex> lock(m);
        if (messages.empty()) return false;
        *out = std::move(messages.front());
        messages.pop_front();
        return true;
    }
    std::string why() const {
        const std::lock_guard<std::mutex> lock(m);
        return reason;
    }
    // Closed reads as closed only once what arrived before the close is taken.
    LinkState now() const {
        const std::lock_guard<std::mutex> lock(m);
        const LinkState s = state;
        return s == LinkState::Closed && !messages.empty() ? LinkState::Open : s;
    }
};

// Windows lets a second socket bind a port another one listens on when both set SO_REUSEADDR, as IXWebSocket's server
// does: two hosts on one PC (or two test runs) then share the port and a client reaches either (slice MP8 found one
// test run's players in another run's game). A plain bind (no option) fails while anything listens there, so a taken
// port is refused first. Not on Linux, where a listener's port cannot be shared anyway and a plain bind would also
// refuse a port that only old connections still hold.
// An IPv6 host is checked as IPv6 (decision 55: "::" beside "0.0.0.0").
bool port_free(const std::string& host, int port) {
#ifdef _WIN32
    const bool v6 = host.find(':') != std::string::npos;
    const SOCKET s = ::socket(v6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return true;  // the listen itself says what is wrong
    bool ok;
    if (v6) {
        sockaddr_in6 a{};
        a.sin6_family = AF_INET6, a.sin6_port = htons(static_cast<u_short>(port));
        if (inet_pton(AF_INET6, host.c_str(), &a.sin6_addr) != 1) a.sin6_addr = in6addr_any;
        ok = ::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) == 0;
    } else {
        sockaddr_in a{};
        a.sin_family = AF_INET, a.sin_port = htons(static_cast<u_short>(port));
        if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) a.sin_addr.s_addr = INADDR_ANY;
        ok = ::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) == 0;
    }
    ::closesocket(s);
    return ok;
#else
    (void)host, (void)port;
    return true;
#endif
}

// A link that stays silent is closed after two to three of these intervals (IXWebSocket sends a ping and closes the
// link when no pong came before the next one is due; our change there, engine/THIRD_PARTY.md, waits on while bytes
// still come in or go out, as when a big save is ahead of the pong), so a host that vanished is noticed within 10 to 15
// seconds.
constexpr int kPingSeconds = 5;
// A join gives up after this long without an answer (the TCP connect and the handshake), not Windows' 21 s.
constexpr int kConnectSeconds = 9;

bool net_system() {
    static const bool ok = ix::initNetSystem();  // WSAStartup once per process; never undone (the process ends)
    return ok;
}

class WsClient final : public Connection {
  public:
    explicit WsClient(const std::string& url) : url_(url) {
        ws_.setUrl(url);
        ws_.disableAutomaticReconnection();
        ws_.disablePerMessageDeflate();
        ws_.setPingInterval(kPingSeconds);  // the network thread answers pings, a slow frame or a pause does not matter
        ws_.setHandshakeTimeout(kConnectSeconds);
        ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) { inbox_.on(msg); });
        ws_.start();
    }
    ~WsClient() override { ws_.stop(); }
    void send(const std::string& message) override { ws_.sendBinary(message); }
    bool receive(std::string* message) override { return inbox_.take(message); }
    LinkState state() const override { return inbox_.now(); }
    bool never_opened() const override { return !inbox_.opened; }
    std::string failure() const override { return inbox_.why(); }
    void close() override {
        ws_.close();
        inbox_.state = LinkState::Closed;
    }
    std::string peer() const override { return url_; }

  private:
    std::string url_;
    Inbox inbox_;
    ix::WebSocket ws_;  // after the inbox: destroyed (its thread stopped) first
};

class WsServerConn final : public Connection {
  public:
    WsServerConn(std::weak_ptr<ix::WebSocket> ws, Inbox& inbox, std::string peer)
        : ws_(std::move(ws)), inbox_(inbox), peer_(std::move(peer)) {}
    ~WsServerConn() override { close(); }
    void send(const std::string& message) override {
        if (auto ws = ws_.lock()) ws->sendBinary(message);
    }
    bool receive(std::string* message) override { return inbox_.take(message); }
    LinkState state() const override { return inbox_.now(); }
    std::string failure() const override { return inbox_.why(); }
    void close() override {
        if (auto ws = ws_.lock()) ws->close();
        inbox_.state = LinkState::Closed;
    }
    std::string peer() const override { return peer_; }

  private:
    std::weak_ptr<ix::WebSocket> ws_;
    Inbox& inbox_;
    std::string peer_;
};

class WsListener final : public Listener {
  public:
    // The other arguments are the library's defaults; the ping interval is the one that matters, and a send that
    // cannot finish for a minute (a big save on a slow line) closes the link, not one that takes longer than a ping.
    // An IPv6 host ("::", "::1") listens on IPv6 (decision 55), any other on IPv4.
    WsListener(const std::string& host, int port)
        : server_(port, host, ix::SocketServer::kDefaultTcpBacklog, ix::SocketServer::kDefaultMaxConnections,
                  ix::WebSocketServer::kDefaultHandShakeTimeoutSecs,
                  host.find(':') != std::string::npos ? AF_INET6 : AF_INET, kPingSeconds, 60) {
        server_.disablePerMessageDeflate();
        server_.setOnConnectionCallback(
            [this](std::weak_ptr<ix::WebSocket> ws, std::shared_ptr<ix::ConnectionState> state) {
                const std::lock_guard<std::mutex> lock(m_);
                inboxes_.push_back(std::make_unique<Inbox>());
                Inbox* inbox = inboxes_.back().get();
                inbox->state = LinkState::Open;  // the handshake is done when the first message can come
                if (auto sock = ws.lock()) {
                    sock->setOnMessageCallback([inbox](const ix::WebSocketMessagePtr& msg) { inbox->on(msg); });
                }
                const std::string ip = state->getRemoteIp();  // an IPv6 peer in brackets, as typed with a port
                fresh_.push_back({std::move(ws), inbox,
                                  (ip.find(':') != std::string::npos ? "[" + ip + "]" : ip) + ":" +
                                      std::to_string(state->getRemotePort())});
            });
    }
    bool start(std::string* error) {
        const auto r = server_.listen();
        if (!r.first) {
            *error = r.second;
            return false;
        }
        server_.start();
        return true;
    }
    ~WsListener() override { server_.stop(); }  // joins the connection threads before the inboxes go
    std::unique_ptr<Connection> accept() override {
        const std::lock_guard<std::mutex> lock(m_);
        if (fresh_.empty()) return nullptr;
        Fresh f = std::move(fresh_.front());
        fresh_.pop_front();
        return std::make_unique<WsServerConn>(std::move(f.ws), *f.inbox, std::move(f.peer));
    }

  private:
    struct Fresh {
        std::weak_ptr<ix::WebSocket> ws;
        Inbox* inbox;
        std::string peer;
    };
    std::mutex m_;
    std::vector<std::unique_ptr<Inbox>> inboxes_;  // one per connection ever accepted, freed with the listener
    std::deque<Fresh> fresh_;
    ix::WebSocketServer server_;  // last: stopped and destroyed first
};

// Decision 55: a host on "0.0.0.0" listens on "::" too, so players reach it over IPv6 as well (each IPv4 and IPv6 its
// own socket, as Windows keeps them apart). accept() takes the connections of both.
class WsListeners final : public Listener {
  public:
    std::vector<std::unique_ptr<Listener>> parts;
    std::unique_ptr<Connection> accept() override {
        for (const std::unique_ptr<Listener>& l : parts) {
            if (auto c = l->accept()) return c;
        }
        return nullptr;
    }
};

std::unique_ptr<Listener> listen_one(const std::string& host, int port, std::string* error) {
    if (!port_free(host, port)) {
        *error = "port " + std::to_string(port) + " on " + host + " is taken";
        return nullptr;
    }
    auto l = std::make_unique<WsListener>(host, port);
    if (!l->start(error)) return nullptr;
    return l;
}

}  // namespace

std::unique_ptr<Listener> Ws_Listen(const std::string& host, int port, std::string* error) {
    if (!net_system()) {
        *error = "the network system did not start";
        return nullptr;
    }
    auto l = listen_one(host, port, error);
    if (!l || host != "0.0.0.0") return l;
    // IPv6 beside it: a taken port refuses the host as on IPv4; a PC without IPv6 (or a system whose "::" takes IPv4
    // too, where the second bind fails) keeps IPv4 alone.
    if (!port_free("::", port)) {
        *error = "port " + std::to_string(port) + " on :: is taken";
        return nullptr;
    }
    std::string why;
    auto v6 = listen_one("::", port, &why);
    if (!v6) return l;
    auto both = std::make_unique<WsListeners>();
    both->parts.push_back(std::move(l)), both->parts.push_back(std::move(v6));
    return both;
}

std::unique_ptr<Connection> Ws_Connect(const std::string& url, std::string* error) {
    if (!net_system()) {
        *error = "the network system did not start";
        return nullptr;
    }
    if (url.rfind("ws://", 0) != 0) {
        *error = "not a ws:// address: " + url;
        return nullptr;
    }
    return std::make_unique<WsClient>(url);
}

}  // namespace antanta::net
