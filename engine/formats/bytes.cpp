#include "bytes.hpp"

#include <zlib.h>

#include <algorithm>
#include <bit>
#include <cstdlib>
#include <cstring>

namespace antanta {

std::uint16_t read_u16(ByteSpan b, std::size_t pos) { return static_cast<std::uint16_t>(b[pos] | (b[pos + 1] << 8)); }

std::uint32_t read_u32(ByteSpan b, std::size_t pos) {
    return read_u16(b, pos) | (static_cast<std::uint32_t>(read_u16(b, pos + 2)) << 16);
}

ByteSpan Cursor::take(std::size_t n) {
    if (!has(n)) {
        ok = false;
        return {};
    }
    ByteSpan s = data.subspan(pos, n);
    pos += n;
    return s;
}

std::uint8_t Cursor::u8() {
    ByteSpan s = take(1);
    return s.empty() ? 0 : s[0];
}

std::uint16_t Cursor::u16() {
    ByteSpan s = take(2);
    return s.empty() ? 0 : read_u16(s, 0);
}

std::uint32_t Cursor::u32() {
    ByteSpan s = take(4);
    return s.empty() ? 0 : read_u32(s, 0);
}

std::uint64_t Cursor::u64() {
    const std::uint64_t low = u32();
    return low | std::uint64_t{u32()} << 32;
}

float Cursor::f32() { return std::bit_cast<float>(u32()); }

std::optional<Bytes> inflate_exact(ByteSpan compressed, std::uint32_t size, std::string* error) {
    // zlib cannot expand more than about 1032:1, so a larger claim is corrupt; checking it first avoids
    // allocating gigabytes for a damaged size field.
    constexpr std::uint64_t kMaxRatio = 1032;
    if (size > compressed.size() * kMaxRatio + 64) {
        *error = "uncompressed size " + std::to_string(size) + " is impossible for " +
                 std::to_string(compressed.size()) + " compressed bytes";
        return std::nullopt;
    }
    Bytes out(std::max<std::size_t>(size, 1));
    uLongf produced = static_cast<uLongf>(out.size());
    const int rc = uncompress(out.data(), &produced, compressed.data(), static_cast<uLong>(compressed.size()));
    if (rc != Z_OK) {
        *error = "zlib error " + std::to_string(rc) + " (" + zError(rc) + ")";
        return std::nullopt;
    }
    if (produced != size) {
        *error = "inflated " + std::to_string(produced) + " bytes, expected " + std::to_string(size);
        return std::nullopt;
    }
    out.resize(size);
    return out;
}

std::string utf16le_to_utf8(ByteSpan b) {
    std::string out;
    auto put = [&out](std::uint32_t c) {
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    };
    const std::size_t n = b.size() / 2;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t c = read_u16(b, i * 2);
        if (c == 0) break;
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) {
            const std::uint32_t d = read_u16(b, (i + 1) * 2);
            if (d >= 0xDC00 && d < 0xE000) {
                put(0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00));
                ++i;
                continue;
            }
        }
        put(c >= 0xD800 && c < 0xE000 ? 0xFFFD : c);
    }
    return out;
}

bool iequals_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        unsigned char x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
        if (x >= 'A' && x <= 'Z') x = static_cast<unsigned char>(x + 32);
        if (y >= 'A' && y <= 'Z') y = static_cast<unsigned char>(y + 32);
        if (x != y) return false;
    }
    return true;
}

std::string_view Words::next() {
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
    while (pos < text.size() && space(text[pos])) ++pos;
    const std::size_t start = pos;
    while (pos < text.size() && !space(text[pos])) ++pos;
    return text.substr(start, pos - start);
}

std::string_view Words::value() {
    std::string_view w = next();
    while (!w.empty() && w.front() == '{') {
        while (w.front() != '}' && w.back() != '}') {
            w = next();
            if (w.empty()) return w;
        }
        w = next();
    }
    return w;
}

long word_to_long(std::string_view word) { return std::strtol(std::string(word).c_str(), nullptr, 10); }

}  // namespace antanta
