// Games on the same network (net/lan.hpp): UDP sockets, Winsock on Windows, BSD sockets elsewhere.
#include "lan.hpp"

#include <algorithm>
#include <cstring>
#include <set>

#include "net/config.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <iphlpapi.h>  // after winsock2.h
using Socket = SOCKET;
constexpr Socket kNoSocket = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socket = int;
constexpr Socket kNoSocket = -1;
#endif

namespace antanta::net {

namespace {

constexpr char kAsk[8] = {'A', 'N', 'T', 'A', 'N', 'T', 'A', '?'};
constexpr char kTell[8] = {'A', 'N', 'T', 'A', 'N', 'T', 'A', '!'};

void put32(std::string& b, std::uint32_t v) {
    for (int k = 0; k < 4; ++k) b += static_cast<char>(v >> (8 * k) & 0xff);
}
std::uint32_t get32(const std::string& b, std::size_t at) {
    std::uint32_t v = 0;
    for (int k = 0; k < 4; ++k) v |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(b[at + k])) << (8 * k);
    return v;
}

// One UDP socket, non-blocking. Winsock is started once per socket (its count makes that safe).
struct Udp {
    Socket s = kNoSocket;
    ~Udp() { close(); }
    void close() {
        if (s == kNoSocket) return;
#ifdef _WIN32
        closesocket(s);
        WSACleanup();
#else
        ::close(s);
#endif
        s = kNoSocket;
    }
    bool open(const std::string& bind_ip, std::uint16_t port, bool bind_it, bool broadcast, std::string* error) {
#ifdef _WIN32
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            *error = "no Winsock";
            return false;
        }
#endif
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == kNoSocket) {
#ifdef _WIN32
            WSACleanup();
#endif
            *error = "no UDP socket";
            return false;
        }
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
#else
        fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
        if (broadcast) {
            const int on = 1;
            setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&on), sizeof on);
        }
        if (!bind_it) return true;
        sockaddr_in a{};
        a.sin_family = AF_INET, a.sin_port = htons(port);
        if (bind_ip.empty() || inet_pton(AF_INET, bind_ip.c_str(), &a.sin_addr) != 1) a.sin_addr.s_addr = INADDR_ANY;
        if (::bind(s, reinterpret_cast<const sockaddr*>(&a), sizeof a) != 0) {
            *error =
                "UDP port " + std::to_string(port) + " on " + (bind_ip.empty() ? "0.0.0.0" : bind_ip) + " is taken";
            close();
            return false;
        }
        return true;
    }
    void send_to(const std::string& bytes, const sockaddr_in& to) const {
        sendto(s, bytes.data(), static_cast<int>(bytes.size()), 0, reinterpret_cast<const sockaddr*>(&to), sizeof to);
    }
    bool receive(std::string* bytes, sockaddr_in* from) const {
        char buf[512];
#ifdef _WIN32
        int len = sizeof *from;
#else
        socklen_t len = sizeof *from;
#endif
        const auto n = recvfrom(s, buf, static_cast<int>(sizeof buf), 0, reinterpret_cast<sockaddr*>(from), &len);
        if (n <= 0) return false;
        bytes->assign(buf, static_cast<std::size_t>(n));
        return true;
    }
};

sockaddr_in to_addr(const char* ip, std::uint16_t port) {
    sockaddr_in a{};
    a.sin_family = AF_INET, a.sin_port = htons(port);
    inet_pton(AF_INET, ip, &a.sin_addr);
    return a;
}

std::string ip_of(const sockaddr_in& a) {
    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a.sin_addr, text, sizeof text);
    return text;
}

}  // namespace

std::string Lan_Question() {
    std::string b(kAsk, sizeof kAsk);
    put32(b, kProtocolVersion);
    return b;
}

std::string Lan_Answer(const LanGame& game, std::uint16_t game_port) {
    std::string b(kTell, sizeof kTell);
    put32(b, kProtocolVersion);
    b += static_cast<char>(game_port & 0xff), b += static_cast<char>(game_port >> 8);
    b += static_cast<char>(game.humans), b += static_cast<char>(game.max_humans);
    b += static_cast<char>(game.locked ? 1 : 0);
    for (const std::string* s : {&game.name, &game.map}) {
        const std::string t = s->substr(0, 64);
        b += static_cast<char>(t.size());
        b += t;
    }
    return b;
}

std::optional<LanGame> Lan_ReadAnswer(const std::string& b, const std::string& from_ip) {
    if (b.size() < 19 || std::memcmp(b.data(), kTell, sizeof kTell) != 0 || get32(b, 8) != kProtocolVersion) {
        return std::nullopt;
    }
    LanGame g;
    const unsigned port = static_cast<unsigned>(static_cast<std::uint8_t>(b[12])) |
                          static_cast<unsigned>(static_cast<std::uint8_t>(b[13])) << 8;
    g.address = from_ip + ":" + std::to_string(port);
    g.humans = static_cast<std::uint8_t>(b[14]), g.max_humans = static_cast<std::uint8_t>(b[15]);
    g.locked = b[16] != 0;
    std::size_t at = 17;
    for (std::string* s : {&g.name, &g.map}) {
        if (at >= b.size()) return std::nullopt;
        const std::size_t n = static_cast<std::uint8_t>(b[at++]);
        if (at + n > b.size()) return std::nullopt;
        *s = b.substr(at, n);
        at += n;
    }
    return g;
}

std::string Lan_BroadcastOf(const std::string& ip, unsigned prefix_length) {
    in_addr a{};
    if (prefix_length == 0 || prefix_length > 30 || inet_pton(AF_INET, ip.c_str(), &a) != 1) return {};
    const std::uint32_t host = ntohl(a.s_addr), mask = 0xffffffffu << (32 - prefix_length);
    in_addr b{};
    b.s_addr = htonl(host | ~mask);
    char text[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &b, text, sizeof text);
    return text;
}

std::vector<std::string> Lan_BroadcastAddresses() {
    std::vector<std::string> out;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return out;
    ULONG size = 16 * 1024;
    std::vector<char> buf;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    for (int tries = 0; tries < 3 && rc == ERROR_BUFFER_OVERFLOW; ++tries) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                  nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (rc == NO_ERROR) {
        std::set<std::string> seen;
        for (const IP_ADAPTER_ADDRESSES* ad = reinterpret_cast<const IP_ADAPTER_ADDRESSES*>(buf.data()); ad;
             ad = ad->Next) {
            if (ad->OperStatus != IfOperStatusUp || ad->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            for (const IP_ADAPTER_UNICAST_ADDRESS* u = ad->FirstUnicastAddress; u; u = u->Next) {
                if (!u->Address.lpSockaddr || u->Address.lpSockaddr->sa_family != AF_INET) continue;
                const sockaddr_in& sa = *reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
                const std::string b = Lan_BroadcastOf(ip_of(sa), u->OnLinkPrefixLength);
                if (!b.empty() && seen.insert(b).second) out.push_back(b);
            }
        }
    }
    WSACleanup();
#endif
    return out;
}

struct LanHost::Impl {
    Udp udp;
    std::string answer;
    bool on = false;
};

LanHost::LanHost() : impl_(std::make_unique<Impl>()) {}
LanHost::~LanHost() = default;

bool LanHost::open(const std::string& bind, std::uint16_t port, std::string* error) {
    return impl_->udp.open(bind, port, true, false, error);
}

void LanHost::set(const LanGame& game, std::uint16_t game_port, bool on) {
    impl_->answer = Lan_Answer(game, game_port), impl_->on = on;
}

void LanHost::poll() {
    if (impl_->udp.s == kNoSocket) return;
    std::string bytes;
    sockaddr_in from{};
    const std::string ask = Lan_Question();
    while (impl_->udp.receive(&bytes, &from)) {
        if (impl_->on && bytes == ask) impl_->udp.send_to(impl_->answer, from);
    }
}

struct LanFinder::Impl {
    Udp udp;
    std::vector<sockaddr_in> targets;
    std::int64_t asked = -1000000;
    std::int64_t every = 1000;
    std::vector<LanGame> games;
};

LanFinder::LanFinder() : impl_(std::make_unique<Impl>()) {}
LanFinder::~LanFinder() = default;

bool LanFinder::open(const std::string& bind, std::uint16_t port, std::string* error) {
    const bool local = bind == "127.0.0.1";
    if (!impl_->udp.open(bind, 0, local, !local, error)) return false;
    impl_->targets = {to_addr("127.0.0.1", port)};
    if (!local) {
        impl_->targets.push_back(to_addr("255.255.255.255", port));  // the default adapter only
        for (const std::string& b : Lan_BroadcastAddresses()) impl_->targets.push_back(to_addr(b.c_str(), port));
    }
    return true;
}

bool LanFinder::poll(std::int64_t now_ms, std::int64_t ask_ms) {
    if (impl_->udp.s == kNoSocket) return false;
    bool changed = false;
    if (now_ms - impl_->asked >= ask_ms) {
        impl_->asked = now_ms, impl_->every = ask_ms;
        for (const sockaddr_in& t : impl_->targets) impl_->udp.send_to(Lan_Question(), t);
    }
    std::string bytes;
    sockaddr_in from{};
    while (impl_->udp.receive(&bytes, &from)) {
        auto g = Lan_ReadAnswer(bytes, ip_of(from));
        if (!g) continue;
        g->seen_ms = now_ms;
        auto it = std::find_if(impl_->games.begin(), impl_->games.end(),
                               [&](const LanGame& o) { return o.address == g->address; });
        if (it == impl_->games.end()) {
            impl_->games.push_back(*g);
            changed = true;
        } else {
            changed = changed || it->name != g->name || it->map != g->map || it->humans != g->humans ||
                      it->max_humans != g->max_humans || it->locked != g->locked;
            *it = *g;
        }
    }
    const auto old = impl_->games.size();
    impl_->games.erase(std::remove_if(impl_->games.begin(), impl_->games.end(),
                                      [&](const LanGame& o) { return now_ms - o.seen_ms > 3 * impl_->every; }),
                       impl_->games.end());
    return changed || impl_->games.size() != old;
}

const std::vector<LanGame>& LanFinder::games() const { return impl_->games; }

std::string Lan_LocalAddress() {
    Udp u;
    std::string err;
    if (!u.open("", 0, false, false, &err)) return {};
    const sockaddr_in to = to_addr("192.0.2.1", 9);  // a documentation address: connect() only picks the route
    if (connect(u.s, reinterpret_cast<const sockaddr*>(&to), sizeof to) != 0) return {};
    sockaddr_in me{};
#ifdef _WIN32
    int len = sizeof me;
#else
    socklen_t len = sizeof me;
#endif
    if (getsockname(u.s, reinterpret_cast<sockaddr*>(&me), &len) != 0) return {};
    const std::string ip = ip_of(me);
    return ip == "0.0.0.0" ? std::string() : ip;
}

}  // namespace antanta::net
