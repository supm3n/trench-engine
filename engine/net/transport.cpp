// The loopback transport (net/transport.hpp): an in-process network with per-link delays on the hub's clock.
#include "transport.hpp"

namespace antanta::net {

namespace {

class LoopbackEnd final : public Connection {
  public:
    LoopbackEnd(LoopbackHub& hub, LoopbackPipe& pipe, bool server) : hub_(hub), pipe_(pipe), server_(server) {}
    ~LoopbackEnd() override { pipe_.closed = true; }
    void send(const std::string& message) override {
        if (pipe_.closed) return;
        const std::int64_t delay = server_ ? pipe_.faults.down_delay_ms : pipe_.faults.up_delay_ms;
        (server_ ? pipe_.down : pipe_.up).emplace_back(hub_.clock() + delay, message);
    }
    bool receive(std::string* message) override {
        auto& q = server_ ? pipe_.up : pipe_.down;
        if (q.empty() || q.front().first > hub_.clock()) return false;
        *message = std::move(q.front().second);
        q.pop_front();
        return true;
    }
    LinkState state() const override {
        // A closed link still hands out what arrived before the close (as a socket does), then reads as closed.
        const auto& q = server_ ? pipe_.up : pipe_.down;
        return pipe_.closed && q.empty() ? LinkState::Closed : LinkState::Open;
    }
    void close() override { pipe_.closed = true; }  // what was sent before still arrives (a socket's orderly close)
    std::string peer() const override { return server_ ? "loopback client" : "loopback server"; }

  private:
    LoopbackHub& hub_;
    LoopbackPipe& pipe_;
    bool server_;
};

class LoopbackListener final : public Listener {
  public:
    LoopbackListener(LoopbackHub& hub, std::string name) : hub_(hub), name_(std::move(name)) { hub_.waiting[name_]; }
    ~LoopbackListener() override { hub_.waiting.erase(name_); }
    std::unique_ptr<Connection> accept() override {
        std::deque<LoopbackPipe*>& q = hub_.waiting[name_];
        if (q.empty()) return nullptr;
        LoopbackPipe* p = q.front();
        q.pop_front();
        return std::make_unique<LoopbackEnd>(hub_, *p, true);
    }

  private:
    LoopbackHub& hub_;
    std::string name_;
};

}  // namespace

std::unique_ptr<LoopbackHub> Loopback_NewHub(std::function<std::int64_t()> clock) {
    auto hub = std::make_unique<LoopbackHub>();
    hub->clock = std::move(clock);
    return hub;
}

std::unique_ptr<Listener> Loopback_Listen(LoopbackHub& hub, const std::string& name) {
    return std::make_unique<LoopbackListener>(hub, name);
}

std::unique_ptr<Connection> Loopback_Connect(LoopbackHub& hub, const std::string& name, const LinkFaults& faults) {
    const auto it = hub.waiting.find(name);
    if (it == hub.waiting.end()) return nullptr;
    hub.pipes.push_back(std::make_unique<LoopbackPipe>());
    LoopbackPipe& p = *hub.pipes.back();
    p.faults = faults;
    it->second.push_back(&p);
    return std::make_unique<LoopbackEnd>(hub, p, false);
}

}  // namespace antanta::net
