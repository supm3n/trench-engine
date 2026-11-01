#include "ui/profiles.hpp"

#include <algorithm>
#include <cstdint>

namespace antanta::ui {

namespace {

void append_utf8(std::string& out, std::uint32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xc0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3f));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xe0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (c & 0x3f));
    } else {
        out += static_cast<char>(0xf0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (c & 0x3f));
    }
}

bool blank(std::string_view s) { return s.find_first_not_of(' ') == std::string_view::npos; }

// Control characters would break our one-line-per-name file; the edit field never produces them.
std::string clean(std::string_view name) {
    std::string out;
    for (char c : name) {
        if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7f) out += c;
    }
    return out;
}

}  // namespace

std::optional<Profiles> parse_splayers(ByteSpan file, std::string* error) {
    if (file.size() < 4) {
        if (!file.empty()) {
            *error = "splayers.dat is shorter than its count";
            return std::nullopt;
        }
        return Profiles{};
    }
    auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(file[at]) | static_cast<std::uint32_t>(file[at + 1]) << 8 |
               static_cast<std::uint32_t>(file[at + 2]) << 16 | static_cast<std::uint32_t>(file[at + 3]) << 24;
    };
    const std::uint32_t count = u32(0);
    if (count == 0) return Profiles{};
    if (count > 1000 || file.size() < 8 + static_cast<std::size_t>(count) * 0x54) {
        *error = "splayers.dat holds fewer records than its count says";
        return std::nullopt;
    }
    Profiles p;
    p.names.clear();
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t at = 8 + static_cast<std::size_t>(i) * 0x54;
        std::string name;
        for (std::size_t k = 0; k < 20; ++k) {
            std::uint32_t c = file[at + 2 * k] | file[at + 2 * k + 1] << 8;
            if (c == 0) break;
            if (c >= 0xd800 && c < 0xdc00 && k + 1 < 20) {  // a surrogate pair
                const std::uint32_t d = file[at + 2 * k + 2] | file[at + 2 * k + 3] << 8;
                if (d >= 0xdc00 && d < 0xe000) {
                    c = 0x10000 + ((c - 0xd800) << 10) + (d - 0xdc00);
                    ++k;
                }
            }
            append_utf8(name, c >= 0xd800 && c < 0xe000 ? '?' : c);
        }
        name = clean(name);
        p.names.push_back(blank(name) ? "Player" : name);
    }
    const std::uint32_t sel = u32(4);
    p.selected = sel < count ? static_cast<int>(sel) : 0;
    return p;
}

std::string profiles_print(const Profiles& p) {
    std::string out = "# Entente Engine profiles. Started from the game's Save/splayers.dat.\n";
    out += "selected " + std::to_string(p.selected) + "\n";
    for (const std::string& n : p.names) out += "name " + n + "\n";
    return out;
}

std::optional<Profiles> profiles_parse(std::string_view text, std::string* error) {
    Profiles p;
    p.names.clear();
    int selected = 0;
    bool have_selected = false;
    std::size_t start = 0;
    int line_no = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("selected ", 0) == 0) {
            const std::string_view v = line.substr(9);
            if (v.empty() || v.size() > 4 || v.find_first_not_of("0123456789") != std::string_view::npos) {
                *error = "profiles line " + std::to_string(line_no) + ": bad selected index";
                return std::nullopt;
            }
            selected = std::stoi(std::string(v));
            have_selected = true;
        } else if (line.rfind("name ", 0) == 0) {
            const std::string name = clean(profile_name_clip(line.substr(5)));
            if (blank(name)) {
                *error = "profiles line " + std::to_string(line_no) + ": empty name";
                return std::nullopt;
            }
            p.names.push_back(name);
        } else {
            *error = "profiles line " + std::to_string(line_no) + ": unknown line";
            return std::nullopt;
        }
    }
    if (p.names.empty() || !have_selected) {
        *error = "profiles file has no name or no selected line";
        return std::nullopt;
    }
    p.selected = selected < static_cast<int>(p.names.size()) ? selected : 0;
    return p;
}

std::string profile_name_clip(std::string_view name) {
    std::size_t chars = 0, i = 0;
    while (i < name.size()) {
        std::size_t len = 1;
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c >= 0xf0) {
            len = 4;
        } else if (c >= 0xe0) {
            len = 3;
        } else if (c >= 0xc0) {
            len = 2;
        }
        if (chars == kProfileNameMax) break;
        i = std::min(name.size(), i + len);
        ++chars;
    }
    return std::string(name.substr(0, i));
}

bool profile_add(Profiles& p, std::string_view name) {
    const std::string n = clean(profile_name_clip(name));
    if (blank(n)) return false;
    for (std::size_t i = 0; i < p.names.size(); ++i) {
        if (p.names[i] == n) {
            p.selected = static_cast<int>(i);
            return true;
        }
    }
    p.names.insert(p.names.begin(), n);
    p.selected = 0;
    return true;
}

bool profile_delete(Profiles& p) {
    if (p.names.size() < 2) return false;
    p.names.erase(p.names.begin() + p.selected);
    p.selected = std::max(0, p.selected - 1);
    return true;
}

void profile_select(Profiles& p, int index, std::string_view edit_text) {
    const std::string n = clean(profile_name_clip(edit_text));
    if (!blank(n) && p.selected >= 0 && p.selected < static_cast<int>(p.names.size())) p.names[p.selected] = n;
    if (index >= 0 && index < static_cast<int>(p.names.size())) p.selected = index;
}

}  // namespace antanta::ui
