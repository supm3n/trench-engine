// Our own save files (milestone 4 slice H, docs/save-own.md). A file holds the whole game state, everything the
// simulation reads, including what the original's .svg loses or rebuilds wrongly (docs/save-audit.md section 7), and
// beside it what the player sees (camera, selection, speed) and the skirmish's choices (for Restart), which stay
// outside the state hash. Loading the original's .svg files is a different path (GameState_FromSave) and is not
// changed by this.
//
// Layout: a 16-byte magic, the format version (u32), the chunk count (u32), then chunks, each with a 4-byte tag, its
// own version (u32), its size (u32), the CRC-32 of its payload (u32) and the payload. Every list carries its count,
// also the fixed-size tables (so a file whose tables have another size is refused, not misread). All numbers are
// little-endian, floats and doubles their raw bits, so a file is the same on every target. Chunks: INFO (what the
// load and save screens list), VIEW (camera, selection, speed), SETU (the skirmish's choices, optional), SIM (the
// game state, zlib-compressed), GRND (a generated map's ground, zlib-compressed, optional), LUA (a Lua mission's
// globals, optional). A chunk with an unknown tag is skipped; a
// newer format or chunk version is refused with a message. The SIM chunk's state hash is checked after the load.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "formats/bytes.hpp"
#include "formats/map.hpp"
#include "sim/skirmish.hpp"
#include "sim/state.hpp"

namespace antanta::sim {

inline constexpr std::uint32_t kOwnSaveFormat = 1;

// Chunk INFO: what the screens list. tick, map, players, the hash and its version are filled by OwnSave_Write.
struct OwnSaveInfo {
    std::string name;             // as the player typed it (UTF-8)
    std::int64_t saved_at = 0;    // wall clock, seconds since 1970 (0 in tests)
    std::uint32_t tick = 0;       // GameState::tick
    std::string map_name;         // GameState::map_name
    std::uint8_t my_player = 0;   // GameState::my_player
    std::uint8_t players = 0;     // players in the game
    std::string player;           // your name (UTF-8; the app fills it, the load screen shows it)
    std::uint32_t play_time = 0;  // your Player::play_time, seconds (the load screen shows it)
    std::uint32_t hash_version = 0;
    std::uint64_t state_hash = 0;  // GameState_Hash at the save; the load checks it
};

// Chunk VIEW: the game the player sees (never read by the simulation, not in the state hash).
struct OwnSaveView {
    double cam_x = 0, cam_y = 0;              // the smooth camera's corner (world pixels)
    std::int32_t view_col = 0, view_row = 0;  // the cell the view's corner is on
    std::uint8_t sel_player = 0;
    std::vector<UnitRef> sel_units;  // the selection, head first
    std::vector<HouseRef> sel_flagged;
    std::optional<HouseRef> sel_house;
    std::uint8_t speed = 1;  // x1, x2, x4
    std::uint8_t minimap_shown = 1;
    std::uint8_t show_health = 0;  // the `~` switch
    std::int32_t tab_index = 0;    // the unit Tab reached
};

// One human seat of a network game: its slot, the sim player, the player's name, a check value of the seat's token
// (net::Token_Check; the token itself stays in its player's own folder), and how the seat ended: left on purpose (the
// computer plays it to the end) and the computer level playing it at the save (0: its human).
struct OwnSaveSeat {
    std::uint8_t slot = 0, player = 0xff;
    std::string name;
    std::uint64_t token_check = 0;
    std::uint8_t left = 0, ai = 0;
};
struct OwnSaveNet {
    std::uint64_t game_uid = 0;  // the game's identity (the server's, at its creation; kept over resumes)
    std::vector<OwnSaveSeat> seats;
};

struct OwnSaveMeta {
    OwnSaveInfo info;
    OwnSaveView view;
    std::optional<SkirmishSetup> setup;  // chunk SETU: the choices of the skirmish (Restart); none for an .svg load
    // Chunk LUA (slice M6): a Lua mission's globals as Lua lines (script/lua_save.hpp ScriptVm_DumpGlobals); none
    // outside a Lua mission. Not in the state hash: the VM is not GameState (the chunk's CRC guards it).
    std::optional<std::string> lua;
    // Chunk NET (slice MP9): a network game's seats, so any player's save can resume the game (docs/multiplayer.md
    // "Autosave and resume"); none outside a network game. Not in the state hash.
    std::optional<OwnSaveNet> net;
    // Chunk GRND (slice G7, docs/map-generator-plan.md 6): the map a generated game started from, whole, as
    // MapGen_Build gave it (a map file's game reads its ground from Maps/<map name> instead). With it the save loads
    // with any later release, whatever the generator then makes of the code. Not in the state hash. Shared, so a
    // network autosave writes the game's map without copying it.
    std::shared_ptr<const MapData> ground;
};

// A save in two steps (slice MP9, the network game's autosave): OwnSave_Prepare takes what must be read at the tick
// (INFO with the state hash, the other chunks, the state's raw bytes); OwnSave_Finish packs the state (zlib) and joins
// the file, and may run on another thread. OwnSave_Write(s, meta) is OwnSave_Finish(OwnSave_Prepare(s, meta)).
struct OwnSavePending {
    Bytes head;    // the magic, the format, the chunk count and the chunks before SIM
    Bytes raw;     // the state, not packed yet
    Bytes ground;  // the generated map (GRND), not packed yet; empty: none
    Bytes tail;    // the chunks after SIM and GRND
};
OwnSavePending OwnSave_Prepare(const GameState& s, const OwnSaveMeta& meta);
Bytes OwnSave_Finish(const OwnSavePending& p);

// The whole file for state `s`. meta.info's tick, map, players, hash and hash version are taken from `s`. Empty only
// when zlib fails (out of memory).
Bytes OwnSave_Write(const GameState& s, const OwnSaveMeta& meta);

// True when the file starts with our magic (else it may be an original .svg).
bool OwnSave_IsOwn(ByteSpan file);

// Reads a whole file into *s and *meta. The state comes back as it was saved, with RngTable filled again from the
// seed (RngIndex and the CRT state as saved); the caller adds what comes from the game files (the house depth maps,
// apps/common/view_input.hpp load_saved_depth) and the wall clock (Game_SetClock). On any problem: false and a
// message the player can read; *s and *meta are then unspecified.
bool OwnSave_Read(ByteSpan file, GameState* s, OwnSaveMeta* meta, std::string* error);

// Only the INFO chunk (the screens' lists), without reading the state.
std::optional<OwnSaveInfo> OwnSave_ReadInfo(ByteSpan file, std::string* error);
// Only the NET chunk (slice MP9; the resume list and the server's seats). nullopt with an empty *error when the file
// has none (not a network game's save).
std::optional<OwnSaveNet> OwnSave_ReadNet(ByteSpan file, std::string* error);

// The version of GameState_Hash's field order (sim/hash.cpp).
std::uint32_t GameState_HashVersion();

}  // namespace antanta::sim
