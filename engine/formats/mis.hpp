



#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bytes.hpp"

namespace antanta {

constexpr std::size_t kMisInfoSize = 0x924;    // the first part of the header; a .cmp keeps one per mission
constexpr std::size_t kMisHeaderSize = 0xd08;  // header, before the blocks
constexpr std::size_t kMisBlockSize = 0x27e4;  // one event block
constexpr std::size_t kMisMaxPlayers = 8;      // the header's tables
constexpr std::size_t kMisDialogs = 10;        // dialogs per block

// Every string field below holds its whole fixed-size field up to the last nonzero character. The original's buffers
// are not cleared, so after the first NUL many files hold leftovers of an earlier, longer text (some of it Russian):
// they are kept so the file writes back as it was. The text itself is what mis_text / mis_name return.
std::u16string_view mis_text(const std::u16string& field);
std::string_view mis_name(const std::string& field);

// The first 0x924 bytes of a .mis, also the record a .cmp keeps for each of its missions. The title and the
// briefing are replaced by the text table's at load (docs/missions.md 2.1), so these are only the fallback.
struct MisInfo {
    std::uint16_t version = 1;
    std::u16string title;        // wchar[80] at +2
    std::string picture;
    std::uint16_t field_f2 = 0;  // 0 in a .mis; the mission count in a .cmp header (the header shares this layout)
    std::u16string text;
    std::uint16_t field_8c4 = 10, field_8c6 = 10;  // 10 and 10 in every .mis; in a .cmp record they differ (unknown)
    std::uint8_t prereq_count = 0;
    std::array<std::uint8_t, 10> prereq{};
    std::string map;
};


struct MisAction {
    std::uint32_t lead = 0;              // +0, never read
    std::uint32_t kind = 0;              // +4, 0 to 24
    std::array<std::int32_t, 5> ints{};  // +8
    std::array<float, 5> floats{};
    std::uint16_t player = 0;
};


struct MisDialog {
    bool on = false;
    std::u16string title;                                // wchar[40] at +1
    std::u16string text;
    std::uint8_t button1_picture = 0, button1_next = 0;
    std::uint8_t button2_picture = 0, button2_next = 0;
    MisAction action;
};

// One event block (0x27e4 bytes, docs/missions.md 3.2). `done` and `countdown` are state of a running mission and
// 0 in a file.
struct MisBlock {
    std::uint8_t done = 0;
    std::u16string text_b;   // "B", the objective, wchar[512] at +2
    std::u16string text_c;
    std::u16string text_uc;
    std::uint8_t id = 0;     // block index + 1
    std::array<std::uint8_t, 5> prereq{};
    std::uint8_t open_prereq = 0;
    std::uint16_t condition = 0;
    float x = 0, y = 0;
    std::uint16_t unit_type = 0, house_type = 0;
    std::int32_t timer = 0;  // in checks (50 ticks each)
    std::uint8_t advance = 0, stage = 0;
    std::int32_t n = 0;
    std::uint8_t win = 0, lose = 0;
    MisAction action;
    std::array<MisDialog, kMisDialogs> dialogs;
    std::int32_t countdown = 0;
};

// One player of the header's tables, with that player's blocks.
struct MisPlayer {
    std::uint8_t ai_type = 0;  // 0 Passive, 1 Defend, 2 DefendAttack, 3 Attack, 4 Air, 5 Sea
    float gold = 0, iron = 0, food = 0, oil = 0, wood = 0;
    std::int8_t team = 0;  // 0-based, passed to SetTeam as is
    std::vector<MisBlock> blocks;
};

struct MisFile {
    MisInfo info;
    std::uint8_t player_count = 0;
    std::array<MisPlayer, kMisMaxPlayers> players;
    std::optional<std::uint8_t> my_player;  // the optional last byte; no shipped file has it (the human is player 0)
    // Number of bytes of the file that no field above keeps (nonzero bytes in the unused parts of the header, a block
    // or a dialog, and so on). 0 for every shipped file; if not, write_mis does not reproduce them.
    std::uint32_t stray_bytes = 0;
};

// Parses a whole .mis: the size must be 0xd08 + blocks * 0x27e4, plus one byte for my_player. Fails on a size that
// fits neither or on more than 8 players.
std::optional<MisFile> parse_mis(ByteSpan file, std::string* error);

// The bytes of a file. Strings longer than their field are cut; the block counts come from the players' vectors.
Bytes write_mis(const MisFile& file);

// The first 0x924 bytes only (the .cmp mission record and the .cmp header, which is 0x8c4 of them). `bytes` must have
// 0x924 bytes; it does not touch `stray`.
MisInfo parse_mis_info(ByteSpan bytes);
void write_mis_info(const MisInfo& info, std::uint8_t* out);  // writes 0x924 bytes, which must be zero already



MisBlock parse_mis_block(ByteSpan bytes);
void write_mis_block(const MisBlock& block, std::uint8_t* out);

}  // namespace antanta
