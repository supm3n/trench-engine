// The network game's messages (docs/multiplayer.md "Protocol"): one binary message per transport message, a type byte
// first, numbers little endian, strings and lists with a u32 count. The orders travel as their O line text
// (sim::Command_Print, the canonical form the recorder and the replays share), so a log of them is readable as is.
// Decoding checks every count against the bytes left and never crashes on bad input.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sim/ownsave.hpp"
#include "sim/skirmish.hpp"
#include "sim/state.hpp"

namespace antanta::net {

enum class MsgType : std::uint8_t {
    // client -> server
    Hello = 1,   // version, build, data_hash, name
    ListGames,   // -
    CreateGame,  // name (the game's), setup (the host's slot is setup.my_slot), positions (the map's player records),
                 // value (the host's tick_ms), password (empty: none), bytes (version 3: our own save of a network
                 // game to resume, empty for a new game)
    JoinGame,    // game, slot (wanted, 0xff: any free human slot), password, token (version 3: a resumed game's seat
                 // token from the player's folder, 0: none)
    Rejoin,      // game, slot, token: a player back after a dropped connection
    StartGame,   // - (host only)
    Order,       // groups (exactly one, player unset): one action's commands, without a tick
    Hash,        // tick, hash: GameState_NetHash after that tick
    Report,      // tick (the last one run), value (mean step time in microseconds), player (MyPlayer: checked against
                 // the server's own, SkirmishSetup_SlotPlayers)
    Request,     // what (RequestKind), value: pause, resume, speed; a resumed game's seats (host only)
    SaveData,    // tick, bytes: our own save after that tick, the answer to SaveRequest
    Leave,       // -
    // server -> client
    Welcome = 64,  // version, name (the server's)
    Error,         // name (the text); the server closes the connection after it
    Games,         // games
    Joined,        // game, slot, token, setup, what (1: you are the host), uid
    Roster,        // roster
    Start,         // setup, value (tick_ms), input_delay, hash_every, uid, autosave, seats
    Frame,         // tick, groups (in the order the sim applies them)
    Pace,          // value (tick_ms), what (1: paused)
    Desync,        // tick, roster (the slots whose hash differs from the first slot's)
    SaveRequest,   // - : send SaveData after your next tick
    RejoinState,   // tick, player, bytes, setup, value, input_delay, hash_every, uid, autosave, seats: the game as a
                   // save after that tick (and the pacing, as in Start); the frames after it follow. Also the start of
                   // a resumed game (version 3), the same for every player
    PlayerLeft,    // slot, what (1: may come back)
};

// SeatComputer (a resumed game's lobby, version 3): value = a slot; an open seat goes to the computer, a computer's
// seat opens again. SeatMove: value = from | to << 8, the player on seat `from` moves to the open seat `to`.
enum class RequestKind : std::uint8_t { Pause = 1, Resume = 2, Speed = 3, SeatComputer = 4, SeatMove = 5 };

// One player action: the top command and, for a ui_order, its children (as ui::take_tick_orders groups them).
struct OrderGroup {
    std::uint8_t player = 0xff;  // set by the server from the connection, never by the client
    std::vector<sim::Command> commands;
};

struct GameInfo {
    std::uint32_t id = 0;
    std::string name, map;
    std::uint8_t humans = 0, max_humans = 0;  // joined and open human slots
    bool started = false;
    bool locked = false;    // a password is needed to join
    std::uint64_t uid = 0;  // version 3: the game's identity (kept by its saves; a player's seat token is for it)
    bool resume = false;    // version 3: a saved game to resume (its seats from the save)
};

struct RosterEntry {
    std::uint8_t slot = 0;
    std::string name;
    bool connected = false;
    std::uint8_t computer = 0;  // version 3: the computer plays the seat (1), or, left for good, to the end (2)
};

struct Message {
    MsgType type = MsgType::Hello;
    std::uint32_t version = 0;
    std::string build;
    std::uint64_t data_hash = 0;
    std::string name;
    std::uint32_t game = 0;
    std::uint8_t slot = 0xff;
    std::uint8_t player = 0xff;
    std::uint64_t token = 0;
    sim::SkirmishSetup setup;
    std::uint32_t tick = 0;
    std::uint64_t hash = 0;
    std::uint32_t value = 0;
    std::uint8_t what = 0;
    std::uint8_t input_delay = 0;
    std::uint16_t hash_every = 0;
    std::vector<GameInfo> games;
    std::vector<RosterEntry> roster;
    std::vector<OrderGroup> groups;
    std::string bytes;
    std::uint8_t positions = 0;  // CreateGame: the map's player records (SkirmishSetup_SlotPlayers)
    std::string password;        // CreateGame, JoinGame
    // Version 3 (slice MP9): the game's identity, the autosave interval in ticks (0: none) and the seats (every human
    // slot: its player, name, the token's check value, left and computer); Start and RejoinState.
    std::uint64_t uid = 0;
    std::uint32_t autosave = 0;
    std::vector<sim::OwnSaveSeat> seats;
};

// The check value of a seat token that the seats list and the saves carry (the token itself stays with its player).
std::uint64_t Token_Check(std::uint64_t token);

std::string Message_Encode(const Message& m);
std::optional<Message> Message_Decode(const std::string& data, std::string* error);

// UTF-8 <-> UTF-16 for the player names (the sim keeps UTF-16 names).
std::string Utf8(const std::u16string& s);
std::u16string Utf16(const std::string& s);

}  // namespace antanta::net
