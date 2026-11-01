// WebSocket in the browser (net/transport.hpp): Emscripten's emscripten/websocket.h. The page's WebSocket calls back on
// the main thread, so the inbox needs no lock. A page cannot listen: Ws_Listen fails there (direct play is desktop
// only, docs/multiplayer.md).
#include <emscripten/websocket.h>

#include <deque>

#include "transport.hpp"

namespace antanta::net {

namespace {

class WsWeb final : public Connection {
  public:
    explicit WsWeb(std::string url) : url_(std::move(url)) {
        EmscriptenWebSocketCreateAttributes attr;
        emscripten_websocket_init_create_attributes(&attr);
        attr.url = url_.c_str();
        attr.protocols = nullptr;
        attr.createOnMainThread = EM_TRUE;
        ws_ = emscripten_websocket_new(&attr);
        if (ws_ <= 0) {
            state_ = LinkState::Closed;
            return;
        }
        emscripten_websocket_set_onopen_callback(ws_, this, &WsWeb::on_open);
        emscripten_websocket_set_onmessage_callback(ws_, this, &WsWeb::on_message);
        emscripten_websocket_set_onclose_callback(ws_, this, &WsWeb::on_close);
        emscripten_websocket_set_onerror_callback(ws_, this, &WsWeb::on_error);
    }
    ~WsWeb() override {
        if (ws_ > 0) {
            emscripten_websocket_close(ws_, 1000, "");
            emscripten_websocket_delete(ws_);
        }
    }
    void send(const std::string& message) override {
        if (state_ != LinkState::Open) return;
        emscripten_websocket_send_binary(ws_, const_cast<char*>(message.data()),
                                         static_cast<std::uint32_t>(message.size()));
    }
    bool receive(std::string* message) override {
        if (inbox_.empty()) return false;
        *message = std::move(inbox_.front());
        inbox_.pop_front();
        return true;
    }
    LinkState state() const override {
        return state_ == LinkState::Closed && !inbox_.empty() ? LinkState::Open : state_;
    }
    void close() override {
        if (ws_ > 0) emscripten_websocket_close(ws_, 1000, "");
        state_ = LinkState::Closed;
    }
    std::string peer() const override { return url_; }

  private:
    static EM_BOOL on_open(int, const EmscriptenWebSocketOpenEvent*, void* user) {
        static_cast<WsWeb*>(user)->state_ = LinkState::Open;
        return EM_TRUE;
    }
    static EM_BOOL on_message(int, const EmscriptenWebSocketMessageEvent* e, void* user) {
        static_cast<WsWeb*>(user)->inbox_.emplace_back(reinterpret_cast<const char*>(e->data), e->numBytes);
        return EM_TRUE;
    }
    static EM_BOOL on_close(int, const EmscriptenWebSocketCloseEvent*, void* user) {
        static_cast<WsWeb*>(user)->state_ = LinkState::Closed;
        return EM_TRUE;
    }
    static EM_BOOL on_error(int, const EmscriptenWebSocketErrorEvent*, void* user) {
        static_cast<WsWeb*>(user)->state_ = LinkState::Closed;
        return EM_TRUE;
    }
    std::string url_;
    EMSCRIPTEN_WEBSOCKET_T ws_ = 0;
    LinkState state_ = LinkState::Connecting;
    std::deque<std::string> inbox_;
};

}  // namespace

std::unique_ptr<Listener> Ws_Listen(const std::string&, int, std::string* error) {
    *error = "a web page cannot host a game (direct play is desktop only)";
    return nullptr;
}

std::unique_ptr<Connection> Ws_Connect(const std::string& url, std::string* error) {
    if (url.rfind("ws://", 0) != 0 && url.rfind("wss://", 0) != 0) {
        *error = "not a ws:// or wss:// address: " + url;
        return nullptr;
    }
    return std::make_unique<WsWeb>(url);
}

}  // namespace antanta::net
