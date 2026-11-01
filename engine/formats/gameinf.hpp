


#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

#include "bytes.hpp"

namespace antanta {




std::optional<std::uint8_t> parse_game_speed_ms(ByteSpan file, std::string* error);





std::optional<bool> parse_short_anims(ByteSpan file, std::string* error);

// Every value, with the loader's clamps. The fog of war value is read and then forced on, so it is not kept.
struct GameInf {
    std::uint8_t music_on = 1;
    std::uint8_t music_volume = 0;  // clamped to 100
    std::uint8_t sound_on = 1;
    std::uint8_t sound_volume = 0;  // not clamped
    std::uint8_t resolution = 0;    // 0..4 = 1024x768, 1152x864, 1280x960, 1280x1024, 1600x1200; over 4 becomes 0
    std::uint8_t speed_ms = 0;      // milliseconds per tick, clamped to 99
    bool hints = false;
    std::uint8_t mouse_scroll = 1;     // 1..100
    std::uint8_t keyboard_scroll = 1;  // 1..100
    bool landscape_filter = true;
    // The 40 numbers: 20 pairs (economic advisor switch k, military advisor switch k) for your player. The game
    // reads them only outside network games.
    std::array<bool, 20> economic{};
    std::array<bool, 20> military{};
    bool short_anims = false;
    bool reflections = true;
};
std::optional<GameInf> parse_gameinf(ByteSpan file, std::string* error);

}  // namespace antanta
