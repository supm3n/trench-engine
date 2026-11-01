// Every number of the network game in one place (docs/multiplayer.md "Easy to tune"). The server reads its own copy;
// a lobby may override the game ones; the clients get the game's values in the start message, never from their own.
#pragma once

#include <cstdint>
#include <string>

namespace antanta::net {

// The protocol's version: a client and a server of different versions refuse each other at the hello.
// 2: the map's positions, the speed and a password (MP2); 3: the seats, the game's identity, the autosave interval and
// a resumed game's save (MP9); 4: the setup's Entente Engine Plus choice (2026-10-22); 5: the Plus call-in strike
// order plus_strike (2026-10-23).
inline constexpr std::uint32_t kProtocolVersion = 5;

struct NetConfig {
    // The lobby.
    std::uint32_t max_games = 8;             // games one server holds at once
    std::uint8_t max_humans = 8;             // human players in one game (the skirmish's 8 slots)
    std::uint32_t hello_timeout_ms = 10000;  // a connection that sends no hello in this time is closed
    // The game's pacing (the server's clock, docs/multiplayer.md "Pause and speed"). tick_ms: the host's speed (30
    // ms is the game's x1, Data/GameInf.txt); 0 runs as fast as the slowest player reports it can.
    std::uint32_t tick_ms = 30;
    std::uint32_t min_tick_ms = 0;    // the fastest speed the host may ask for
    std::uint32_t max_tick_ms = 500;  // the slowest
    // Input delay D: the server stamps an order with its clock (the last frame sent) + D. A frame goes out as soon as
    // the clock reaches it, so D = 1 is the shortest reaction; a larger D only delays every order (kept as a knob for
    // tests and for a later client-side play-out buffer).
    std::uint32_t input_delay = 1;
    std::uint32_t min_input_delay = 1;
    std::uint32_t max_input_delay = 8;
    // Desync check: every hash_every ticks each client sends GameState_NetHash after that tick; the server compares.
    std::uint32_t hash_every = 30;
    // Speed control: each client reports its mean step time every report_ms; the server's tick is at least the
    // slowest report times step_headroom_pct / 100.
    std::uint32_t report_ms = 1000;
    std::uint32_t step_headroom_pct = 125;
    // A connected client more than max_lag_ticks behind the server's clock (its last report or hash) holds the clock
    // until it catches up; 0 never holds. Three hash intervals: a player who keeps up is never held, and a fast pace
    // (tick_ms 0 before the first step times) cannot run far ahead of the players, which would delay their orders.
    std::uint32_t max_lag_ticks = 90;
    // Frames a client runs at once when it is behind (catching up after a stall or a rejoin) in one app frame.
    std::uint32_t catch_up_ticks = 50;
    // Slice MP9 (docs/multiplayer.md "Rejoin, the computer, autosave"). A dropped player's seat is kept this long for
    // a rejoin; after it the computer plays the seat to the end. 0: kept as long as the game runs.
    std::uint32_t seat_hold_s = 300;
    // The computer level (1 Easy, 2 Medium, 3 Difficult) that plays a player who left or dropped; 0: nobody (the
    // player's units stand, as before MP9).
    std::uint32_t ai_level = 2;
    // Every player's game saves the same tick every autosave_minutes (1 to 15; 0: no autosave); the ticks are worked
    // out from the host's speed at the start. autosave_ticks, when not 0, sets the ticks directly (the tests).
    std::uint32_t autosave_minutes = 5;
    std::uint32_t autosave_ticks = 0;
};

// The autosave interval in ticks for a game whose host's speed is tick_ms (30 when 0): autosave_ticks, else
// autosave_minutes (1 to 15) of the game's ticks; 0 when there is none.
std::uint32_t NetConfig_AutosaveTicks(const NetConfig& c, std::uint32_t tick_ms);

// Sets one number by its name as above ("tick_ms", "input_delay", ...). False for an unknown
// name or a value that is not a whole number.
bool NetConfig_Set(NetConfig& c, const std::string& key, const std::string& value);
// Every number as "name=value" lines, for the server's log.
std::string NetConfig_Text(const NetConfig& c);

}  // namespace antanta::net
