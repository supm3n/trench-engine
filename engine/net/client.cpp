// A player's end of the network game (net/client.hpp).
#include "client.hpp"

#include "net/config.hpp"

namespace antanta::net {

Client::Client(std::unique_ptr<Connection> link, std::string name, std::string build, std::uint64_t data_hash)
    : link_(std::move(link)), name_(std::move(name)), build_(std::move(build)), data_hash_(data_hash) {
    if (!link_) phase_ = ClientPhase::Closed, error_ = "no connection";
}

void Client::send(const Message& m) {
    if (phase_ != ClientPhase::Closed) link_->send(Message_Encode(m));
}

void Client::poll() {
    if (phase_ == ClientPhase::Closed) return;
    if (!hello_sent_ && link_->state() == LinkState::Open) {
        Message h;
        h.type = MsgType::Hello, h.version = kProtocolVersion, h.build = build_, h.data_hash = data_hash_;
        h.name = name_;
        send(h);
        hello_sent_ = true;
    }
    std::string bytes, err;
    while (phase_ != ClientPhase::Closed && link_->receive(&bytes)) {
        const auto m = Message_Decode(bytes, &err);
        if (!m) {
            error_ = "bad message from the server: " + err;
            phase_ = ClientPhase::Closed;
            link_->close();
            return;
        }
        on(*m);
    }
    if (phase_ != ClientPhase::Closed && link_->state() == LinkState::Closed) {
        if (error_.empty()) {
            if (link_->never_opened()) {
                std::string at = link_->peer();  // "ws://host:port/" -> "host:port"
                if (at.rfind("ws://", 0) == 0) at.erase(0, 5);
                while (!at.empty() && at.back() == '/') at.pop_back();
                error_ = kNoAnswerPrefix + at;
            } else {
                error_ = "the connection closed";
            }
        }
        phase_ = ClientPhase::Closed;
    }
}

void Client::on(const Message& m) {
    switch (m.type) {
        case MsgType::Welcome: phase_ = ClientPhase::Lobby; break;
        case MsgType::Error:
            error_ = m.name, phase_ = ClientPhase::Closed;
            link_->close();
            break;
        case MsgType::Games: games_ = m.games; break;
        case MsgType::Joined:
            game_ = m.game, slot_ = m.slot, token_ = m.token, host_ = m.what == 1, setup_ = m.setup, uid_ = m.uid;
            phase_ = ClientPhase::Joined;
            break;
        case MsgType::Roster: roster_ = m.roster; break;
        case MsgType::Start:
            start_ = m, setup_ = m.setup, tick_ms_ = m.value, uid_ = m.uid, phase_ = ClientPhase::Playing;
            break;
        case MsgType::RejoinState:
            rejoin_ = m, start_ = m, setup_ = m.setup, tick_ms_ = m.value, uid_ = m.uid, phase_ = ClientPhase::Playing;
            frames_.clear();
            break;
        case MsgType::Frame: frames_[m.tick] = m.groups; break;
        case MsgType::Pace: tick_ms_ = m.value, paused_ = m.what == 1; break;
        case MsgType::Desync: desync_ = m; break;
        case MsgType::SaveRequest: save_request_ = true; break;
        case MsgType::PlayerLeft: left_.push_back(m.slot); break;
        default: break;
    }
}

void Client::list_games() {
    Message m;
    m.type = MsgType::ListGames;
    send(m);
}

void Client::create_game(const std::string& name, const sim::SkirmishSetup& setup, std::uint8_t positions,
                         std::uint32_t tick_ms, const std::string& password, std::string resume) {
    Message m;
    m.type = MsgType::CreateGame, m.name = name, m.setup = setup, m.positions = positions, m.value = tick_ms;
    m.password = password, m.bytes = std::move(resume);
    send(m);
}

void Client::join_game(std::uint32_t game, std::uint8_t slot, const std::string& password, std::uint64_t token) {
    Message m;
    m.type = MsgType::JoinGame, m.game = game, m.slot = slot, m.password = password, m.token = token;
    send(m);
}

void Client::rejoin(std::uint32_t game, std::uint8_t slot, std::uint64_t token) {
    Message m;
    m.type = MsgType::Rejoin, m.game = game, m.slot = slot, m.token = token;
    game_ = game, slot_ = slot, token_ = token;
    send(m);
}

void Client::start_game() {
    Message m;
    m.type = MsgType::StartGame;
    send(m);
}

void Client::leave() {
    Message m;
    m.type = MsgType::Leave;
    send(m);
}

void Client::send_order(std::vector<sim::Command> group) {
    Message m;
    m.type = MsgType::Order;
    m.groups.push_back(OrderGroup{0xff, std::move(group)});
    send(m);
}

void Client::send_hash(std::uint32_t tick, std::uint64_t hash) {
    Message m;
    m.type = MsgType::Hash, m.tick = tick, m.hash = hash;
    send(m);
}

void Client::send_report(std::uint32_t tick, std::uint32_t step_us, std::uint8_t player) {
    Message m;
    m.type = MsgType::Report, m.tick = tick, m.value = step_us, m.player = player;
    send(m);
}

void Client::send_request(RequestKind kind, std::uint32_t value) {
    Message m;
    m.type = MsgType::Request, m.what = static_cast<std::uint8_t>(kind), m.value = value;
    send(m);
}

void Client::send_save(std::uint32_t tick, std::string bytes) {
    Message m;
    m.type = MsgType::SaveData, m.tick = tick, m.bytes = std::move(bytes);
    send(m);
}

void Client::drop() {
    link_->close();
    phase_ = ClientPhase::Closed, error_ = "dropped";
}

std::optional<Message> Client::take_rejoin() {
    std::optional<Message> r = std::move(rejoin_);
    rejoin_.reset();
    return r;
}

bool Client::take_save_request() {
    const bool r = save_request_;
    save_request_ = false;
    return r;
}

std::vector<OrderGroup> Client::take_frame(std::uint32_t t) {
    const auto it = frames_.find(t);
    if (it == frames_.end()) return {};
    std::vector<OrderGroup> f = std::move(it->second);
    frames_.erase(it);
    return f;
}

std::vector<std::uint8_t> Client::take_left() {
    std::vector<std::uint8_t> r = std::move(left_);
    left_.clear();
    return r;
}

}  // namespace antanta::net
