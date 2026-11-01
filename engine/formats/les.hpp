// Lesta .les archives (docs/formats.md ".les"): a table of named zlib streams.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bytes.hpp"

namespace antanta {

struct LesEntry {
    std::string name;  // raw CP1251 bytes, as stored
    std::uint16_t id = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint32_t offset = 0;  // absolute data offset, already corrected by LesTable::offset_shift
};

struct LesTable {
    std::uint32_t capacity = 0;
    std::vector<LesEntry> entries;
    // Correction applied to every stored offset. Nonzero only for Menu/multiplay.les, whose header says
    // capacity 50 but whose table area is laid out for 500 (docs/bugs.md, data defects): the stored offsets are
    // 21,600 bytes short. Found, like tools/les_unpack.py does, by locating the first zlib stream (78 9C) after
    // the used table.
    std::int64_t offset_shift = 0;
};

// Parses the header and entry table. Empty or truncated input is an error.
std::optional<LesTable> parse_les(ByteSpan file, std::string* error);

// Case-insensitive lookup by name; nullptr if absent.
const LesEntry* find_entry(const LesTable& table, std::string_view name);

// Lookup by the u16 id (the game finds unit and house graphics by id); the first entry with that id, or nullptr.
const LesEntry* find_entry_id(const LesTable& table, std::uint16_t id);

// Inflates one entry. Corrupt streams (3 in multiplay.les) and size mismatches are errors.
std::optional<Bytes> read_entry(ByteSpan file, const LesEntry& entry, std::string* error);

}  // namespace antanta
