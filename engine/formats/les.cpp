#include "les.hpp"

#include <algorithm>

namespace antanta {
namespace {

constexpr std::size_t kHeaderSize = 12;
constexpr std::size_t kEntrySize = 48;
constexpr std::size_t kNameSize = 32;

}  // namespace

std::optional<LesTable> parse_les(ByteSpan file, std::string* error) {
    if (file.empty()) {
        *error = "empty archive file";
        return std::nullopt;
    }
    if (file.size() < kHeaderSize) {
        *error = "archive shorter than its 12-byte header";
        return std::nullopt;
    }
    LesTable table;
    table.capacity = read_u32(file, 0);
    const std::uint32_t count = read_u32(file, 4);
    const std::uint32_t table_offset = read_u32(file, 8);
    const std::uint64_t table_end = std::uint64_t{table_offset} + std::uint64_t{count} * kEntrySize;
    if (table_end > file.size()) {
        *error = "entry table runs past the end of the file";
        return std::nullopt;
    }
    table.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t p = table_offset + std::size_t{i} * kEntrySize;
        LesEntry e;
        const ByteSpan name = file.subspan(p, kNameSize);
        const auto nul = std::find(name.begin(), name.end(), std::uint8_t{0});
        e.name.assign(name.begin(), nul);
        e.id = read_u16(file, p + 32);
        e.compressed_size = read_u32(file, p + 36);
        e.uncompressed_size = read_u32(file, p + 40);
        e.offset = read_u32(file, p + 44);
        table.entries.push_back(e);
    }
    if (!table.entries.empty()) {
        // tools/les_unpack.py searches from 12 + count*48 regardless of the table offset; same here.
        const std::size_t from = std::min<std::size_t>(kHeaderSize + std::size_t{count} * kEntrySize, file.size());
        constexpr std::uint8_t kMagic[2] = {0x78, 0x9C};
        const auto it = std::search(file.begin() + static_cast<std::ptrdiff_t>(from), file.end(), kMagic, kMagic + 2);
        if (it != file.end()) {
            const std::int64_t first = it - file.begin();
            table.offset_shift = first - std::int64_t{table.entries[0].offset};
        }
        for (LesEntry& e : table.entries) {
            const std::int64_t shifted = std::int64_t{e.offset} + table.offset_shift;
            // A shift that pushes an offset out of range leaves it pointing past the end; read_entry reports it.
            e.offset = shifted < 0 || shifted > 0xFFFFFFFF ? 0xFFFFFFFF : static_cast<std::uint32_t>(shifted);
        }
    }
    return table;
}

const LesEntry* find_entry(const LesTable& table, std::string_view name) {
    for (const LesEntry& e : table.entries) {
        if (iequals_ascii(e.name, name)) return &e;
    }
    return nullptr;
}

const LesEntry* find_entry_id(const LesTable& table, std::uint16_t id) {
    for (const LesEntry& e : table.entries) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

std::optional<Bytes> read_entry(ByteSpan file, const LesEntry& entry, std::string* error) {
    if (entry.offset > file.size()) {
        *error = entry.name + ": data offset is past the end of the file";
        return std::nullopt;
    }
    // A stream cut short by the end of the file is passed on; zlib reports it as truncated.
    const std::size_t avail = std::min<std::size_t>(entry.compressed_size, file.size() - entry.offset);
    auto data = inflate_exact(file.subspan(entry.offset, avail), entry.uncompressed_size, error);
    if (!data) *error = entry.name + ": " + *error;
    return data;
}

}  // namespace antanta
