// Games on the same network (docs/multiplayer.md "Built in step 2"): a host answers a small UDP question on the
// discovery port with its game; a player looking for games sends the question to the network's broadcast address
// (and to this PC) every second and lists the answers. Desktop only: a browser page has no UDP.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace antanta::net {

inline constexpr std::uint16_t kLanPort = 47624;   // the question's port (UDP)
inline constexpr std::uint16_t kGamePort = 47625;  // a host's game (WebSocket) unless the player gives another

struct LanGame {
    std::string address;  // "ip:port" to join, from the answer's sender and the port it names
    std::string name, map;
    std::uint8_t humans = 0, max_humans = 0;  // joined and open human slots
    bool locked = false;                      // a password is needed
    std::int64_t seen_ms = 0;                 // when the last answer came (LanFinder)
};

// The question and the answer as bytes (the protocol's version inside: another version gets no answer).
std::string Lan_Question();
std::string Lan_Answer(const LanGame& game, std::uint16_t game_port);
// An answer from the address `from_ip`; nothing when it is not one of ours.
std::optional<LanGame> Lan_ReadAnswer(const std::string& bytes, const std::string& from_ip);

// The host's side: answers every question that reaches bind:port.
class LanHost {
  public:
    LanHost();
    ~LanHost();
    LanHost(const LanHost&) = delete;
    LanHost& operator=(const LanHost&) = delete;
    // False and why when the port cannot be had (another host on this PC has it: then this game is not listed).
    bool open(const std::string& bind, std::uint16_t port, std::string* error);
    // The game it tells about; game_port: its WebSocket port. Off: no answers (the game started).
    void set(const LanGame& game, std::uint16_t game_port, bool on);
    // Answers the questions waiting.
    void poll();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// A player's side: asks every ask_ms, collects the answers, forgets a game it has not heard of for 3 asks.
class LanFinder {
  public:
    LanFinder();
    ~LanFinder();
    LanFinder(const LanFinder&) = delete;
    LanFinder& operator=(const LanFinder&) = delete;
    // bind: the address to send from ("" or "0.0.0.0": the system's choice, the question goes to the broadcast
    // address and to this PC; "127.0.0.1": this PC only, the tests); port: the hosts' discovery port.
    bool open(const std::string& bind, std::uint16_t port, std::string* error);
    // True when the list changed.
    bool poll(std::int64_t now_ms, std::int64_t ask_ms = 1000);
    const std::vector<LanGame>& games() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// The broadcast address of an adapter's address with its network prefix length (192.168.1.20 and 24: 192.168.1.255);
// empty for an address that is not IPv4 or a prefix without hosts to ask (0, 31, 32).
std::string Lan_BroadcastOf(const std::string& ip, unsigned prefix_length);
// The broadcast address of every IPv4 adapter that is up, loopback left out, without duplicates. 255.255.255.255 goes
// out on one adapter only, so the finder asks these too. Empty on other systems than Windows.
std::vector<std::string> Lan_BroadcastAddresses();

// This PC's address on its network, as others would reach it (the interface of the default route; no packet is sent).
// Empty when there is none.
std::string Lan_LocalAddress();
// Decision 55: this PC's global IPv6 address, which others reach over the internet (the default route's, as above;
// no packet is sent). Empty when there is none (no IPv6, or only link-local and unique local fc00::/7 addresses).
std::string Lan_LocalAddress6();

}  // namespace antanta::net
