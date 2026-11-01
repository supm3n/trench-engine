// Data/museum/Museum.mus, the table behind the original's museum (docs/formats.md "Museum.mus", docs/frontend.md 4.16),
// and Data/TextTable_eng.tt, the table of titles and texts it takes the entries' names and texts from.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "bytes.hpp"

namespace antanta {


struct MuseumEntry {
    std::uint8_t nation = 0;  // the nation filter's index (0 Russia ... 5 Universal)
    std::uint8_t type = 0;    // the type filter's index (0 Buildings ... 5 Soldiers)
    // The name and text the file holds. The original throws both away after reading and takes the entry's name and
    // text from the text table (museum_apply_text_table); ours keeps them as the fallback of a missing record.
    std::string name, text;
    std::vector<std::string> pictures;  // names in Pictures.les, in the order Prev and Next step through them
};

struct MuseumFile {
    std::vector<MuseumEntry> entries;
    std::vector<std::string> nations, types;  // the filter lists (the original takes their names from the string table)
};




std::optional<MuseumFile> parse_museum_mus(ByteSpan file, std::string* error);

// One record of the text table: the title line (empty when the record has none) and the text.
struct TextRecord {
    std::string title, text;
};
using TextTable = std::map<std::string, TextRecord>;





std::optional<TextTable> parse_text_table(ByteSpan file, std::string* error);





void museum_apply_texts(MuseumFile& file, const TextTable& table, const std::map<std::string, std::string>& strings);

}  // namespace antanta
