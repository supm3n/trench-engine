#include "museum.hpp"

#include <algorithm>
#include <cstring>
#include <string_view>

namespace antanta {

namespace {

std::vector<std::uint16_t> units_of(ByteSpan b) {
    std::vector<std::uint16_t> out(b.size() / 2);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = read_u16(b, i * 2);
    return out;
}

std::string utf8_of(const std::vector<std::uint16_t>& units, std::size_t count) {
    Bytes b;
    for (std::size_t i = 0; i < std::min(count, units.size()); ++i) {
        b.push_back(static_cast<std::uint8_t>(units[i]));
        b.push_back(static_cast<std::uint8_t>(units[i] >> 8));
    }
    return utf16le_to_utf8(b);
}



void cut_trailing_run(std::vector<std::uint16_t>& s, std::uint16_t pad, std::uint16_t pad2 = 0xffff) {
    std::size_t run = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == 0) {
            if (run == 0) return;
            s[run] = 0;
        } else if (s[i] == pad || s[i] == pad2) {
            if (run == 0) run = i;
        } else {
            run = 0;
        }
    }
    if (run != 0) s[run] = 0;
}

// The reads of the loader, with the bounds the original never checks. The names of the lists are read into a 200-
// character stack buffer there; here any length the file has room for is read.
std::string read_wide(Cursor& c, std::uint32_t chars) {
    // (chars + 1) characters, the last one the terminator
    if (chars > c.data.size()) {
        c.ok = false;
        return {};
    }
    const ByteSpan b = c.take((static_cast<std::size_t>(chars) + 1) * 2);
    return utf16le_to_utf8(b);
}

}  // namespace

std::optional<MuseumFile> parse_museum_mus(ByteSpan file, std::string* error) {
    Cursor c{file};
    const ByteSpan magic = c.take(7);
    if (!c.ok || std::string_view(reinterpret_cast<const char*>(magic.data()), 7) != std::string_view("MUSEUM\0", 7)) {
        *error = "Museum.mus: not a museum file (no MUSEUM marker)";
        return std::nullopt;
    }
    const std::uint32_t count = c.u32();
    constexpr std::size_t kEntryMin = 2 + 0x50 + 4 + 2;  // nation, type, name, text length, picture count
    if (!c.ok || count == 0 || count > c.data.size() / kEntryMin) {
        *error = "Museum.mus: bad entry count " + std::to_string(count);
        return std::nullopt;
    }
    MuseumFile out;
    out.entries.resize(count);
    for (std::uint32_t n = 0; n < count; ++n) {
        MuseumEntry& e = out.entries[n];
        e.nation = c.u8();
        e.type = c.u8();
        std::vector<std::uint16_t> name = units_of(c.take(0x50));  // 40 characters, the rest after the NUL is junk
        if (const auto nul = std::find(name.begin(), name.end(), 0); nul != name.end()) name.erase(nul, name.end());
        cut_trailing_run(name, 0x20);
        e.name = utf8_of(name, name.size());
        const std::uint32_t chars = c.u32();
        if (chars != 0) {  // no text: nothing follows the length
            if (chars > c.data.size()) {
                c.ok = false;
            } else {
                std::vector<std::uint16_t> text = units_of(c.take((static_cast<std::size_t>(chars) + 1) * 2));
                if (text.size() > chars) text.resize(chars);  // the loader looks at `chars` characters only
                cut_trailing_run(text, 0x0d, 0x0a);           // trailing carriage returns and line feeds
                e.text = utf8_of(text, text.size());
            }
        }
        const std::uint16_t pictures = c.u16();
        for (std::uint16_t k = 0; k < pictures && c.ok; ++k) {
            const std::uint32_t len = c.u32();
            if (len == 0) {
                e.pictures.emplace_back();
                continue;
            }
            if (len > c.data.size()) {
                c.ok = false;
                break;
            }
            const ByteSpan b = c.take(static_cast<std::size_t>(len) + 1);  // the name and its terminator
            e.pictures.emplace_back(reinterpret_cast<const char*>(b.data()), c.ok ? len : 0);
        }
        if (!c.ok) {
            *error = "Museum.mus ends inside entry " + std::to_string(n);
            return std::nullopt;
        }
    }
    for (std::vector<std::string>* list : {&out.nations, &out.types}) {
        const std::uint32_t n = c.u32();
        if (!c.ok || n > c.data.size() / 4) {
            *error = "Museum.mus: bad list count";
            return std::nullopt;
        }
        for (std::uint32_t i = 0; i < n; ++i) {
            const std::uint32_t chars = c.u32();
            list->push_back(read_wide(c, chars));
            if (!c.ok) {
                *error = "Museum.mus ends inside a list";
                return std::nullopt;
            }
        }
    }
    const ByteSpan end = c.take(10);
    if (!c.ok ||
        std::string_view(reinterpret_cast<const char*>(end.data()), 10) != std::string_view("ENDMUSEUM\0", 10)) {
        *error = "Museum.mus: no ENDMUSEUM marker";
        return std::nullopt;
    }
    if (out.nations.empty() || out.types.empty()) {
        *error = "Museum.mus has no nations or no types";
        return std::nullopt;
    }
    return out;
}

namespace {



std::string next_line(const std::u16string& text, std::size_t& at) {
    std::string line;
    Bytes b;
    while (at < text.size() && text[at] != u'\r' && text[at] != u'\n') {
        b.push_back(static_cast<std::uint8_t>(text[at]));
        b.push_back(static_cast<std::uint8_t>(text[at] >> 8));
        ++at;
    }
    line = utf16le_to_utf8(b);
    if (at < text.size()) {
        const char16_t first = text[at++];
        if (first == u'\r' && at < text.size() && text[at] == u'\n') ++at;
    }
    return line;
}

void replace_all(std::string& s, std::string_view from, std::string_view to) {
    for (std::size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size())
        s.replace(at, from.size(), to);
}

}  // namespace

std::optional<TextTable> parse_text_table(ByteSpan file, std::string* error) {
    if (file.size() < 2 || file[0] != 0xFF || file[1] != 0xFE) {
        *error = "text table: no UTF-16LE byte order mark";
        return std::nullopt;
    }
    std::u16string text;
    for (std::size_t i = 2; i + 1 < file.size(); i += 2) text += static_cast<char16_t>(read_u16(file, i));
    TextTable table;
    std::size_t at = 0;
    next_line(text, at);  // the first line, empty in the shipped file
    while (at < text.size()) {
        if (next_line(text, at) != "##NEW_REC##") continue;
        const std::string key = next_line(text, at);
        TextRecord rec;
        std::string line = next_line(text, at);
        if (line != "##TEXT_BEGIN##") rec.title = line;


        std::string body;
        while (at < text.size()) {
            line = next_line(text, at);
            if (line == "##TEXT_BEGIN##") continue;
            if (line == "##NEW_REC##" || line == "##TEXT_END##") break;
            if (!line.empty()) body += line + "\n";
        }
        if (!body.empty()) body.pop_back();
        replace_all(body, "\xc2\xa0", " ");  // U+00A0 becomes a plain space
        rec.text = std::move(body);
        table.emplace(key, std::move(rec));  // the first record of a key stays
    }
    if (table.empty()) {
        *error = "text table: no record";
        return std::nullopt;
    }
    return table;
}

void museum_apply_texts(MuseumFile& file, const TextTable& table, const std::map<std::string, std::string>& strings) {
    auto string_of = [&](const std::string& key, std::string& into) {
        if (const auto it = strings.find(key); it != strings.end()) into = it->second;
    };
    for (std::size_t i = 0; i < file.nations.size(); ++i)
        string_of("IDS_MUSEUM_NATION_" + std::to_string(i), file.nations[i]);
    for (std::size_t i = 0; i < file.types.size(); ++i)
        string_of("IDS_MUSEUM_TYPE_" + std::to_string(i), file.types[i]);
    for (std::size_t i = 0; i < file.entries.size(); ++i) {
        const auto it = table.find("IDS_MUSEUM_EQUIPMENT_" + std::to_string(i));
        if (it == table.end()) continue;
        MuseumEntry& e = file.entries[i];
        if (!it->second.title.empty()) {
            std::string name = it->second.title;
            std::size_t chars = 0, end = 0;  // 0x28 characters copied, the last one replaced by the terminator
            while (end < name.size() && chars < 39) {
                ++chars;
                ++end;
                while (end < name.size() && (static_cast<unsigned char>(name[end]) & 0xc0) == 0x80) ++end;
            }
            e.name = name.substr(0, end);
        }
        e.text = it->second.text;
    }
}

}  // namespace antanta
