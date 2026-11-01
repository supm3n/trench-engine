



#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bytes.hpp"
#include "mis.hpp"
#include "museum.hpp"

namespace antanta {




struct CmpFile {
    MisInfo header;  // only version, title, picture, field_f2 (= missions.size()) and text are in the file
    std::vector<std::string> file_names;  // char[0x50] fields kept as MisInfo's strings are: the name is mis_name(...)
    std::vector<MisInfo> missions;
    std::uint32_t stray_bytes = 0;  // as MisFile::stray_bytes
};
std::optional<CmpFile> parse_cmp(ByteSpan file, std::string* error);
Bytes write_cmp(const CmpFile& file);

// A .cmt: one name per line (CRLF), blank lines skipped: the title key, the info key, the picture, then one mission
// name per line; the mission is `<the .cmt's path without ".cmt">\<name>.mit`.
struct CmtFile {
    std::string title_key, info_key, picture;
    std::vector<std::string> missions;
    // The text between the lines (and before the first and after the last), so that write_cmt gives the file back
    // exactly; empty or of the wrong size means the usual CRLF after every line.
    std::vector<std::string> gaps;
};
std::optional<CmtFile> parse_cmt(ByteSpan file, std::string* error);
Bytes write_cmt(const CmtFile& file);

// One player of a .mit.
struct MitPlayer {
    std::int32_t ai_type = 0, team = 0;
    std::int32_t gold = 0, iron = 0, wood = 0, food = 0, oil = 0;  // the file's order, not the .mis's
};

// A .mit (ASCII, "Label<TAB><TAB>value" lines, read as label/value word pairs): Title, Info (text table keys), Map,
// MyPlayer, PlayersQuantity, then per player AIType, Team, Gold, Iron, Wood, Food, Oil.
struct MitFile {
    std::string title_key, info_key, map;
    std::int32_t my_player = 0;
    std::vector<MitPlayer> players;
    // The white space between the words (and before the first, after the last), as CmtFile::gaps; empty or of the
    // wrong size means the usual layout (two tabs after most labels, one after MyPlayer and PlayersQuantity, CRLF).
    std::vector<std::string> gaps;
};
std::optional<MitFile> parse_mit(ByteSpan file, std::string* error);
Bytes write_mit(const MitFile& file);




std::string mission_text_key(std::string_view folder, std::string_view stem);
std::string mission_block_key(std::string_view folder, std::string_view stem, unsigned player, unsigned block,
                              std::string_view kind);  // kind: "B", "C" or "UC"

// The record of a key, ignoring ASCII case (the original's lookup does; the shipped table's keys are all upper case).
const TextRecord* find_text(const TextTable& table, std::string_view key);

}  // namespace antanta
