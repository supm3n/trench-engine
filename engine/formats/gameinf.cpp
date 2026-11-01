#include "gameinf.hpp"

#include <string_view>

namespace antanta {

namespace {

Words words_of(ByteSpan file) {
    return Words{std::string_view(reinterpret_cast<const char*>(file.data()), file.size())};
}


std::optional<std::uint8_t> read_value(Words& t) {
    const std::string_view tok = t.value();
    if (tok.empty()) return std::nullopt;
    return static_cast<std::uint8_t>(word_to_long(tok));
}

}  // namespace

std::optional<std::uint8_t> parse_game_speed_ms(ByteSpan file, std::string* error) {
    Words t = words_of(file);
    std::optional<std::uint8_t> v;
    for (int i = 0; i < 6; ++i) {
        v = read_value(t);
        if (!v) {
            *error = "GameInf.txt has fewer than 6 values";
            return std::nullopt;
        }
    }
    return *v > 99 ? std::uint8_t{99} : *v;
}

std::optional<bool> parse_short_anims(ByteSpan file, std::string* error) {
    Words t = words_of(file);
    for (int i = 0; i < 11; ++i) {
        if (!read_value(t)) {
            *error = "GameInf.txt has fewer than 11 values";
            return std::nullopt;
        }
    }
    for (int i = 0; i < 40; ++i) {
        if (t.next().empty()) {
            *error = "GameInf.txt: per-player numbers missing";
            return std::nullopt;
        }
    }
    const auto v = read_value(t);
    if (!v) {
        *error = "GameInf.txt: truncated-animation value missing";
        return std::nullopt;
    }
    return *v != 0;
}

std::optional<GameInf> parse_gameinf(ByteSpan file, std::string* error) {
    Words t = words_of(file);
    GameInf g;
    long v[11] = {};
    for (int i = 0; i < 11; ++i) {
        const std::string_view w = t.value();
        if (w.empty()) {
            *error = "GameInf.txt has " + std::to_string(i) + " of 11 values before the advisor switches";
            return std::nullopt;
        }
        v[i] = word_to_long(w);
    }
    auto byte = [](long x) { return static_cast<std::uint8_t>(x); };
    g.music_on = byte(v[0]);
    g.music_volume = byte(v[1]) > 100 ? std::uint8_t{100} : byte(v[1]);
    g.sound_on = byte(v[2]);
    g.sound_volume = byte(v[3]);  // not clamped by the loader
    g.resolution = byte(v[4]) > 4 ? std::uint8_t{0} : byte(v[4]);
    g.speed_ms = byte(v[5]) > 99 ? std::uint8_t{99} : byte(v[5]);
    // v[6], fog of war: read, then forced to 1 by the loader.
    g.hints = v[7] != 0;
    auto scroll = [&byte](long x) {
        return byte(x) == 0 ? std::uint8_t{1} : byte(x) > 100 ? std::uint8_t{100} : byte(x);
    };
    g.mouse_scroll = scroll(v[8]);
    g.keyboard_scroll = scroll(v[9]);
    g.landscape_filter = v[10] != 0;
    for (int k = 0; k < 20; ++k) {
        const std::string_view e = t.next(), m = t.next();
        if (m.empty()) {
            *error = "GameInf.txt: advisor switches missing";
            return std::nullopt;
        }
        g.economic[k] = word_to_long(e) != 0;
        g.military[k] = word_to_long(m) != 0;
    }
    const std::string_view anims = t.value(), reflections = t.value();
    if (reflections.empty()) {
        *error = "GameInf.txt: the last two values are missing";
        return std::nullopt;
    }
    g.short_anims = word_to_long(anims) != 0;
    g.reflections = word_to_long(reflections) != 0;
    return g;
}

}  // namespace antanta
