// Data/Menu.txt, the table behind the original's main menu and in-game menu (docs/frontend.md 2.3, features.md 1.1),
// and the string table Data/StringTable_eng.st that names the labels of the other screens.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "bytes.hpp"

namespace antanta {



struct MenuElement {
    std::uint8_t type = 0;
    std::string file;  // picture in the window's archive, or a slide show's name prefix
    std::uint16_t id = 0;
    std::uint16_t x = 0, y = 0, w = 0, h = 0;  // in the window
    std::uint16_t handler = 0;
    // A Windows virtual-key code (27 Esc, 13 Enter, 'A'..'Z' for a letter in either case). The original's loader
    // reads it with atol, so there the letters ("e", "n", ...) become 0: no key. Changed on purpose (owner,
    // 2026-10-15): here they work.
    std::uint8_t hotkey = 0;
};

struct MenuWindow {
    std::string archive;     // as written, e.g. data\\interface\\Menu\\basic.les
    std::string background;  // picture in the archive
    std::uint16_t id = 0;
    std::uint16_t x = 0, y = 0, w = 0, h = 0;  // on the 1024 x 768 screen
    std::vector<MenuElement> elements;
};





std::optional<std::vector<MenuWindow>> parse_menu_txt(ByteSpan file, std::string* error);

// The archive path of a window as a path below the game folder: the doubled and single backslashes become '/'.
std::string menu_archive_path(const std::string& archive);

// StringTable_eng.st: UTF-16LE with a byte order mark, CRLF lines, an empty first line, then a key line and its
// value line for every string. Returns key -> value (UTF-8); a repeated key keeps its first value. Format from the
// data (features.md); the game's reader was not read.
std::optional<std::map<std::string, std::string>> parse_string_table(ByteSpan file, std::string* error);

}  // namespace antanta
