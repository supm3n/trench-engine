// What the game app keeps of its network games (slice MP9, docs/multiplayer.md "Rejoin, the computer, autosaves"):
// the seats it had (network-seats.txt in the user folder: a player comes back after a drop or to a resumed game with
// the seat's token, which only he has; the server keeps only its check) and the autosaves' ring (the last three of a
// game, in saves/network). The file reading and writing stays with the app (platform/files.hpp); this part is pure,
// so the tests check it.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "formats/bytes.hpp"
#include "sim/ownsave.hpp"

namespace antanta {

// One seat this player had: the game's identity (the server's uid), the server's address ("" for a game this PC
// hosted), the slot, the seat's token, and when it was last seen (seconds since 1970).
struct NetSeatRecord {
    std::uint64_t uid = 0;
    std::string address;
    std::uint8_t slot = 0xff;
    std::uint64_t token = 0;
    std::int64_t seen = 0;
};

// network-seats.txt: one line per seat, "<uid hex> <slot> <token hex> <seen> <address>", the newest first. Lines that
// do not read are left out.
std::vector<NetSeatRecord> NetSeats_Parse(const std::string& text);
std::string NetSeats_Format(const std::vector<NetSeatRecord>& seats);
// Puts r first, in place of the record of its game; keeps the newest 32.
void NetSeats_Put(std::vector<NetSeatRecord>& seats, const NetSeatRecord& r);
const NetSeatRecord* NetSeats_Find(const std::vector<NetSeatRecord>& seats, std::uint64_t uid);

// The name of an autosave in the ring: "autosave-<uid as 16 hex digits>-<k>.sav" with k = 1, 2, 3 in turn (the
// game's autosave number n = tick / every, k = (n - 1) % 3 + 1), so the fourth replaces the first. Every player's
// autosave of a tick has the same name. The player's own saves have other names and are never replaced.
std::string NetAutosave_Name(std::uint64_t uid, std::uint32_t tick, std::uint32_t every);
// True for a name NetAutosave_Name makes.
bool NetAutosave_Is(const std::string& name);

// Packs and writes the autosaves on a thread of its own (OwnSave_Finish and the file: 0.13 s for a normal game, up to
// 0.8 s for a huge one), so the game does not stand still. One at a time; a newer one that comes while two wait
// replaces the older waiting one. The web build (no threads) writes at once.
class NetAutosaveWriter {
  public:
    using Write = std::function<bool(const std::string& path, const Bytes& data, std::string* error)>;
    explicit NetAutosaveWriter(Write write);
    ~NetAutosaveWriter();  // writes what waits, then ends the thread
    NetAutosaveWriter(const NetAutosaveWriter&) = delete;
    NetAutosaveWriter& operator=(const NetAutosaveWriter&) = delete;
    void put(std::string path, sim::OwnSavePending save);
    // Waits until everything put so far is written.
    void flush();
    std::uint32_t written() const;
    std::string last_error() const;

  private:
    void work();

    Write write_;
    mutable std::mutex mu_;
    std::condition_variable cv_, done_;
    std::deque<std::pair<std::string, sim::OwnSavePending>> queue_;
    bool busy_ = false, stop_ = false;
    std::uint32_t written_ = 0;
    std::string error_;
#ifndef __EMSCRIPTEN__
    std::thread thread_;
#endif
};

}  // namespace antanta
