#include "menu.hpp"

#include <cctype>
#include <string_view>

namespace antanta {

namespace {

struct Reader {
    Words words;
    bool ok = true;
    std::string_view word() {
        const std::string_view w = words.value();
        if (w.empty()) ok = false;
        return w;
    }
    std::string str() { return std::string(word()); }
    std::uint16_t u16() { return static_cast<std::uint16_t>(word_to_long(word())); }
    std::uint8_t u8() { return static_cast<std::uint8_t>(word_to_long(word())); }


    std::uint8_t hotkey() {
        const std::string_view w = word();
        if (!w.empty() && std::isalpha(static_cast<unsigned char>(w[0]))) {
            return static_cast<std::uint8_t>(std::toupper(static_cast<unsigned char>(w[0])));
        }
        return static_cast<std::uint8_t>(word_to_long(w));
    }
};

}  // namespace

std::optional<std::vector<MenuWindow>> parse_menu_txt(ByteSpan file, std::string* error) {
    Reader r{Words{std::string_view(reinterpret_cast<const char*>(file.data()), file.size())}};
    const std::uint8_t count = r.u8();
    std::vector<MenuWindow> windows(count);
    for (std::size_t n = 0; n < windows.size() && r.ok; ++n) {
        MenuWindow& win = windows[n];
        win.archive = r.str();
        win.background = r.str();
        win.id = r.u16();
        win.x = r.u16(), win.y = r.u16(), win.w = r.u16(), win.h = r.u16();
        win.elements.resize(r.u8());
        for (MenuElement& e : win.elements) {
            e.type = r.u8();
            e.file = r.str();
            e.id = r.u16();
            e.x = r.u16(), e.y = r.u16(), e.w = r.u16(), e.h = r.u16();
            e.handler = r.u16();
            e.hotkey = r.hotkey();
        }
        if (!r.ok) {
            *error = "Menu.txt ends inside window " + std::to_string(n);
            return std::nullopt;
        }
    }
    if (!r.ok) {
        *error = "Menu.txt has no window count";
        return std::nullopt;
    }
    return windows;
}

std::string menu_archive_path(const std::string& archive) {
    std::string out;
    for (char c : archive) {
        if (c != '\\') {
            out += c;
        } else if (out.empty() || out.back() != '/') {
            out += '/';
        }
    }
    return out;
}

std::optional<std::map<std::string, std::string>> parse_string_table(ByteSpan file, std::string* error) {
    if (file.size() < 2 || file[0] != 0xFF || file[1] != 0xFE) {
        *error = "string table: no UTF-16LE byte order mark";
        return std::nullopt;
    }
    const std::string text = utf16le_to_utf8(file.subspan(2));
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find("\r\n", start);
        if (end == std::string::npos) end = text.size();
        lines.push_back(text.substr(start, end - start));
        start = end + 2;
    }
    std::size_t i = 0;
    while (i < lines.size() && lines[i].empty()) ++i;
    std::map<std::string, std::string> out;
    for (; i + 1 < lines.size(); i += 2) out.emplace(lines[i], lines[i + 1]);
    if (i < lines.size() && !lines[i].empty()) {
        *error = "string table: key '" + lines[i] + "' has no value line";
        return std::nullopt;
    }
    return out;
}

}  // namespace antanta
