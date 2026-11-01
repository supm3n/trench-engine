

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bytes.hpp"

namespace antanta {

struct HouseParams {                // 0x2EC-byte record
    std::string data_path;          // +0c8 resource path (CP1251, "\\" collapsed to "\") or "1NN" = nation N-1 type NN
    std::uint16_t width = 0;        // +12c sprite W
    std::uint16_t height = 0;       // +12e sprite H
    std::uint8_t footprint_a = 0;
    std::uint8_t footprint_b = 0;
    std::uint16_t anchor_x = 0;
    std::uint16_t anchor_y = 0;
    std::int16_t exit_x = 0;
    std::int16_t exit_y = 0;
    std::array<float, 5> upkeep{};
    float power_use = 0;
    std::array<std::uint16_t, 10> produce{};
    std::uint8_t field_188 = 0;
    std::uint8_t field_189 = 0;
    std::uint16_t work = 0;
    std::uint16_t max_hp = 0;
    std::uint16_t capacity = 0;
    std::uint8_t point_count_b = 0;
    std::int16_t offset_x = 0;
    std::int16_t offset_y = 0;
    std::uint16_t field_142 = 0;
    std::uint8_t field_1c8 = 0;


    std::uint8_t anim_count = 0;
    std::array<std::uint16_t, 10> anim_kind{}, anim_x{}, anim_y{};
    std::uint16_t fire_y0 = 0;



    std::array<std::uint16_t, 10> fire_x{}, fire_y{}, flash_x{}, flash_y{};
    std::array<std::array<std::uint8_t, 4>, 10> fire_level{};
    std::uint8_t field_2c0 = 0;
    std::uint8_t field_2c1 = 0;
    std::uint8_t hit_count = 0;
    std::array<std::uint16_t, 10> hit_x{};
    std::array<std::uint16_t, 10> hit_y{};

    std::array<std::uint32_t, 5> cost{};
    std::array<std::uint16_t, 10> prereq{};
    std::uint32_t field_2bc = 0;
};


struct WeaponParams {
    std::uint16_t attack = 0, reload = 0, field_3e = 0;
    float fire_range = 0, min_range = 0;
    std::uint8_t kind = 0;

    std::uint8_t field_48 = 0;
    std::array<float, 3> point_a{};


    std::array<float, 3> point_b{};
};



struct UnitSound {
    std::string name;
    std::uint8_t loop = 0, positioned = 0, count = 0;
};

struct UnitParams {               // 0x9C0-byte record plus its weapon blocks
    std::string data_path;        // +0c8 resource path or "NTT" = nation N-1 type TT
    std::uint8_t move_class = 0;  // +12c
    std::uint16_t width = 0;      // +12e sprite canvas W
    std::uint16_t height = 0;     // +130 sprite canvas H
    std::int16_t anchor_x = 0;
    std::int16_t anchor_y = 0;
    // Movement and animation fields (sim/unittype.cpp derives the TBasicUnit values from them).
    float speed = 0;                           // +134 -> Speed
    std::array<float, 6> upkeep{};             // +18c Food, Wood, Iron, Gold, Oil, Power
    std::array<std::uint32_t, 6> cost{};
    std::uint16_t build_time = 0;
    std::uint16_t field_170 = 0;
    std::uint16_t field_1bc = 0;
    float turn_degrees = 0;                    // +1c0
    std::array<float, 30> anim_speed{};        // +1c4 AnimationSpeed[30]
    std::array<std::uint8_t, 30> anim_size{};  // +23c AnimationSize[30]
    std::int16_t size = 0;                     // +788 -> field_59c
    std::uint16_t field_786 = 0;               // +786 -> field_588 (the ships' target box with fog of war)

    std::uint16_t accuracy = 0;
    std::uint16_t range = 0;
    std::uint16_t reload = 0;
    std::uint16_t max_hp = 0;
    std::array<std::uint16_t, 10> attack{};
    std::array<std::uint16_t, 10> defense{};
    std::vector<WeaponParams> weapons;
    std::array<UnitSound, 10> sounds;         // +268: select, command, weapon, second weapon, death, engine, 4 unused
};

struct BasicParams {
    std::vector<HouseParams> houses;
    std::vector<UnitParams> units;
};

// Unit records are followed by weapon blocks (count = record[0x138]) whose size depends on the movement class
// (ships, classes 13..15, skip part 0); see tools/params_dump.py load. Truncated input is an error.
std::optional<BasicParams> parse_basic_params(ByteSpan file, std::string* error);

// The owner's decision (2026-10-19, docs/bugs.md B262): Russia's cruiser (BasicParams0 unit type 19) names
// Antanta\Ship_Goeben, which no release ships; while that file is missing the cruiser takes Russia\Ship_Rurik, as the
// fan 1.2 version does. The stand-in DataPath for (nation, type, path), or empty when none applies; the caller uses
// it only when the named file cannot be read, so a Ship_Goeben that a patch or mod adds is used as it is.
std::string unit_data_path_stand_in(int nation, int type, const std::string& path);



std::optional<std::vector<std::array<std::uint8_t, 3>>> parse_player_colours(ByteSpan file, std::string* error);

}  // namespace antanta
