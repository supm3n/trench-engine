// Byte buffers, a bounds-checked little-endian reader, zlib inflate and text decoding shared by the readers.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace antanta {

using Bytes = std::vector<std::uint8_t>;
using ByteSpan = std::span<const std::uint8_t>;

// Reads little-endian values. A read past the end returns 0 and clears `ok` for good, so a parser can read a
// whole section and check `ok` once.
struct Cursor {
    ByteSpan data;
    std::size_t pos = 0;
    bool ok = true;

    bool has(std::size_t n) const { return ok && pos <= data.size() && data.size() - pos >= n; }
    std::uint8_t u8();
    std::uint16_t u16();
    std::uint32_t u32();
    // Low word first. Never write it as `u32() | u64{u32()} << 32`: the operands of | are unsequenced, and optimised
    // MSVC reads the high word first.
    std::uint64_t u64();
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    float f32();
    // Returns the next n bytes and advances, or an empty span (and ok = false) if fewer remain.
    ByteSpan take(std::size_t n);
    void skip(std::size_t n) { take(n); }
};

std::uint16_t read_u16(ByteSpan b, std::size_t pos);  // caller has checked the bounds
std::uint32_t read_u32(ByteSpan b, std::size_t pos);

// Inflates one zlib stream that must produce exactly `size` bytes.
std::optional<Bytes> inflate_exact(ByteSpan compressed, std::uint32_t size, std::string* error);

// UTF-16LE up to the first NUL (or the end) as UTF-8. Unpaired surrogates become U+FFFD.
std::string utf16le_to_utf8(ByteSpan b);

// Case-insensitive ASCII comparison; other bytes (CP1251 names) must match exactly.
bool iequals_ascii(std::string_view a, std::string_view b);


struct Words {
    std::string_view text;
    std::size_t pos = 0;
    // Next whitespace-separated word, empty at the end.
    std::string_view next();



    std::string_view value();
};

// atol of a word (strtol base 10, defined on overflow), the number the value readers store.
long word_to_long(std::string_view word);

}  // namespace antanta
