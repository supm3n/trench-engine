#include "ui/settings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "ui/lang.hpp"

namespace antanta::ui {

Settings settings_from_gameinf(const GameInf& g) {
    Settings s;
    s.music_on = g.music_on != 0;
    s.music_volume = g.music_volume;
    s.sound_on = g.sound_on != 0;
    s.sound_volume = g.sound_volume;
    s.resolution = g.resolution;
    s.speed_ms = g.speed_ms;
    s.hints = g.hints;
    s.mouse_scroll = g.mouse_scroll;
    s.keyboard_scroll = g.keyboard_scroll;
    s.landscape_filter = g.landscape_filter;
    s.short_anims = g.short_anims;
    s.reflections = g.reflections;
    s.economic = g.economic;
    s.military = g.military;
    return s;
}

namespace {

// Every setting: its name in the file, a pointer to the value in a Settings and the allowed range.
struct Field {
    const char* name;
    std::uint8_t Settings::* byte;
    bool Settings::* flag;
    int lo, hi;
    bool optional = false;             // added after the first files were written: missing means the default
    int Settings::* number = nullptr;  // a wider value than a byte (the window's position and size)
};

const Field kFields[] = {
    {"music_on", nullptr, &Settings::music_on, 0, 1},
    {"music_volume", &Settings::music_volume, nullptr, 0, 100},
    {"sound_on", nullptr, &Settings::sound_on, 0, 1},
    {"sound_volume", &Settings::sound_volume, nullptr, 0, 255},
    {"resolution", &Settings::resolution, nullptr, 0, 4},
    {"fullscreen", nullptr, &Settings::fullscreen, 0, 1},
    {"speed_ms", &Settings::speed_ms, nullptr, 0, 99},
    {"hints", nullptr, &Settings::hints, 0, 1},
    {"mouse_scroll", &Settings::mouse_scroll, nullptr, 1, 100},
    {"keyboard_scroll", &Settings::keyboard_scroll, nullptr, 1, 100},
    {"landscape_filter", nullptr, &Settings::landscape_filter, 0, 1},
    {"short_anims", nullptr, &Settings::short_anims, 0, 1},
    {"reflections", nullptr, &Settings::reflections, 0, 1},
    {"ui_scale", &Settings::ui_scale, nullptr, 0, 4, true},
    {"ui_pictures", &Settings::ui_pictures, nullptr, 0, 2, true},
    {"world_zoom", &Settings::world_zoom, nullptr, 0, 4, true},
    {"check_updates", nullptr, &Settings::check_updates, 0, 1, true},
    {"net_autosave", &Settings::net_autosave, nullptr, 1, 15, true},
    {"net_seat_hold", &Settings::net_seat_hold, nullptr, 0, 60, true},
    {"net_ai_level", &Settings::net_ai_level, nullptr, 0, 3, true},
    {"sim_thread", &Settings::sim_thread, nullptr, 0, 2, true},
    {"window_x", nullptr, nullptr, -32768, 32767, true, &Settings::window_x},
    {"window_y", nullptr, nullptr, -32768, 32767, true, &Settings::window_y},
    {"window_w", nullptr, nullptr, 0, 32767, true, &Settings::window_w},
    {"window_h", nullptr, nullptr, 0, 32767, true, &Settings::window_h},
    {"window_maximized", nullptr, &Settings::window_maximized, 0, 1, true},
};

int get(const Settings& s, const Field& f) {
    return f.number ? s.*(f.number) : f.byte ? s.*(f.byte) : (s.*(f.flag) ? 1 : 0);
}

void set(Settings& s, const Field& f, int v) {
    if (f.number) {
        s.*(f.number) = v;
    } else if (f.byte) {
        s.*(f.byte) = static_cast<std::uint8_t>(v);
    } else {
        s.*(f.flag) = v != 0;
    }
}

std::vector<std::string_view> words_of(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) ++i;
        const std::size_t start = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t' && line[i] != '\r') ++i;
        if (i > start) out.push_back(line.substr(start, i - start));
    }
    return out;
}

bool to_int(std::string_view w, int lo, int hi, int* out) {
    const bool minus = !w.empty() && w[0] == '-';
    if (minus) w.remove_prefix(1);
    if (w.empty() || w.size() > 5) return false;
    int v = 0;
    for (char c : w) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    if (minus) v = -v;
    if (v < lo || v > hi) return false;
    *out = v;
    return true;
}

std::string bits(const bool* b, std::size_t n) {
    std::string out;
    for (std::size_t i = 0; i < n; ++i) out += std::string(i ? " " : "") + (b[i] ? "1" : "0");
    return out;
}

}  // namespace

std::string settings_print(const Settings& s) {
    std::string out = "# Entente Engine settings. Started from the game's Data/GameInf.txt.\n";
    for (const Field& f : kFields) out += std::string(f.name) + " " + std::to_string(get(s, f)) + "\n";
    out += "economic_advisor " + bits(s.economic.data(), s.economic.size()) + "\n";
    out += "military_advisor " + bits(s.military.data(), s.military.size()) + "\n";
    if (!s.language.empty()) out += "language " + s.language + "\n";
    if (s.rules_plus) out += "rules plus\n";
    return out;
}

std::optional<Settings> settings_parse(std::string_view text, std::string* error) {
    Settings s;
    std::vector<bool> seen(std::size(kFields) + 2, false);
    std::size_t start = 0;
    int line_no = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        ++line_no;
        const std::vector<std::string_view> w = words_of(line);
        if (w.empty() || w[0][0] == '#') continue;
        auto fail = [&](const std::string& what) {
            *error = "settings line " + std::to_string(line_no) + ": " + what;
            return std::nullopt;
        };
        if (w[0] == "language") {  // ours, a text (docs/languages.md 4)
            if (!s.language.empty()) return fail("'language' given twice");
            if (w.size() != 2 || !lang_code_valid(w[1])) return fail("'language' needs one language code");
            s.language = std::string(w[1]);
            continue;
        }
        if (w[0] == "rules") {  // ours, the Rules row chosen last
            if (w.size() != 2 || (w[1] != "plus" && w[1] != "classic")) return fail("'rules' needs plus or classic");
            s.rules_plus = w[1] == "plus";
            continue;
        }
        std::size_t index = std::size(kFields);
        bool* flags = nullptr;
        std::size_t count = 1, short_count = 1;  // short_count: the advisor lines of the older files (6 and 4)
        if (w[0] == "economic_advisor") {
            flags = s.economic.data(), count = s.economic.size(), short_count = 6;
        } else if (w[0] == "military_advisor") {
            index += 1;
            flags = s.military.data(), count = s.military.size(), short_count = 4;
        } else {
            index = 0;
            while (index < std::size(kFields) && w[0] != kFields[index].name) ++index;
            if (index == std::size(kFields)) return fail("unknown setting '" + std::string(w[0]) + "'");
        }
        if (seen[index]) return fail("'" + std::string(w[0]) + "' given twice");
        seen[index] = true;
        if (flags && w.size() == short_count + 1) count = short_count;
        if (w.size() != count + 1)
            return fail("'" + std::string(w[0]) + "' needs " + std::to_string(count) + " values");
        for (std::size_t k = 0; k < count; ++k) {
            const int lo = flags ? 0 : kFields[index].lo, hi = flags ? 1 : kFields[index].hi;
            int v = 0;
            if (!to_int(w[k + 1], lo, hi, &v)) {
                return fail("'" + std::string(w[0]) + "' must be " + std::to_string(lo) + ".." + std::to_string(hi));
            }
            if (flags) {
                flags[k] = v != 0;
            } else {
                set(s, kFields[index], v);
            }
        }
    }
    for (std::size_t i = 0; i < seen.size(); ++i) {
        if (!seen[i] && !(i < std::size(kFields) && kFields[i].optional)) {
            *error = std::string("settings: '") +
                     (i < std::size(kFields)    ? kFields[i].name
                      : i == std::size(kFields) ? "economic_advisor"
                                                : "military_advisor") +
                     "' is missing";
            return std::nullopt;
        }
    }
    return s;
}

Size resolution_size(std::uint8_t resolution) {
    static const Size kSizes[] = {{1024, 768}, {1152, 864}, {1280, 960}, {1280, 1024}, {1600, 1200}};
    return kSizes[resolution < 5 ? resolution : 0];
}

WindowRect window_clamp(WindowRect r, const WindowRect& usable, int title_h) {
    r.w = std::max(1, std::min(r.w, usable.w));
    r.h = std::max(1, std::min(r.h, usable.h - title_h));
    r.x = std::max(usable.x, std::min(r.x, usable.x + usable.w - r.w));
    r.y = std::max(usable.y + title_h, std::min(r.y, usable.y + usable.h - r.h));
    return r;
}

WindowPlace window_place(const Settings& s, const std::vector<WindowRect>& usable, int title_h) {
    WindowPlace out;
    out.maximized = s.window_maximized;
    const bool remembered = s.window_w > 0 && s.window_h > 0;
    if (remembered) {
        out.r = {s.window_x, s.window_y, s.window_w, s.window_h};
    } else {
        const Size size = resolution_size(s.resolution);
        out.r.w = size.w, out.r.h = size.h;
    }
    if (usable.empty()) return out;
    if (remembered) {
        const int mx = s.window_x + s.window_w / 2;  // the middle of the top edge: the title bar can be reached
        for (const WindowRect& d : usable) {
            if (mx >= d.x && mx < d.x + d.w && s.window_y >= d.y && s.window_y < d.y + d.h) {
                out.r = window_clamp(out.r, d, title_h);
                return out;
            }
        }
    }
    const WindowRect& primary = usable.front();
    out.r.x = primary.x + (primary.w - out.r.w) / 2;
    out.r.y = primary.y + title_h + (primary.h - title_h - out.r.h) / 2;
    out.r = window_clamp(out.r, primary, title_h);
    return out;
}

// The x87 arithmetic of the original, done in double. Changed on purpose (owner, 2026-10-15): the original truncates
// on the way back (ftol), which loses one step on many values (96 comes back as 95); here the way back rounds to the
// nearest step, so a value always comes back as set. The stored format and the ranges are the original's.

float volume_to_slider(std::uint8_t volume) {
    return static_cast<float>(static_cast<double>(volume) * static_cast<double>(0.01f));
}

std::uint8_t volume_from_slider(float slider) {
    return static_cast<std::uint8_t>(std::lround(static_cast<double>(slider) * 100.0));
}

float speed_to_slider(std::uint8_t ms) { return static_cast<float>((90.0 - ms) / (90.0 - 30.0)); }

std::uint8_t speed_from_slider(float slider) {
    return static_cast<std::uint8_t>(std::lround((1.0 - static_cast<double>(slider)) * (90.0 - 30.0) + 30.0));
}

std::uint8_t scroll_from_slider(float slider) {
    const std::uint8_t v = volume_from_slider(slider);
    return v == 0 ? std::uint8_t{1} : v;
}

// A Menu.txt hot key (a Windows virtual-key code: Esc 27, Enter 13, A..Z 65..90) as the SDL key code it matches.
// Letters are the lower case SDL keys, so a key matches whatever the case of the letter in Menu.txt.
int hotkey_sdl_key(std::uint8_t vk) { return vk >= 'A' && vk <= 'Z' ? vk + 32 : vk; }

float slider_value_at(int mouse_x, int x, int w) {
    if (w <= 0) return 0.0f;
    const float v = static_cast<float>(static_cast<double>(mouse_x - x) / static_cast<double>(w));
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

int slider_knob_x(float value, int x, int w, int knob_w) {
    return x + static_cast<int>(std::trunc(static_cast<double>(w - knob_w) * static_cast<double>(value)));
}

}  // namespace antanta::ui
