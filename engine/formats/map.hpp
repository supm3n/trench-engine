// Maps (docs/formats.md "Pack wrapper", ".min", ".dat (maps)", "Tail") and Data/LandTextures/LandTextures.dat.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bytes.hpp"
#include "sprites.hpp"

namespace antanta {


std::optional<Bytes> unpack_pack(ByteSpan file, std::string* error);

// .min: pack wrapper around a 200 x 200 RGB565 image.
struct Minimap {
    static constexpr int kSide = 200;
    std::vector<std::uint16_t> rgb565;  // kSide * kSide, row-major
};
std::optional<Minimap> parse_minimap(ByteSpan file, std::string* error);


std::optional<Minimap> parse_svg_preview(ByteSpan file, std::string* error);
// RGB8 (3 bytes per pixel), channels scaled as tools/map_dump.py rgb565: c * 255 / max (integer division).
Bytes minimap_to_rgb(const Minimap& m);

struct MapCell {                             // 25 bytes
    std::uint8_t height = 0;                 // +00 screen y = row * 24 - height
    std::uint16_t tile = 0;                  // +01 LandTextures id, 1-based; 0x8000 = has sub-tiles
    std::uint8_t shade = 0;                  // +03 unknown, smooth 75..250 (lighting?)
    std::uint8_t unknown4 = 0;               // +04 unknown, 205 in the maps checked
    std::array<std::uint16_t, 9> subtile{};  // +05 3x3 sub-tile ids, 0 = none
    std::uint16_t object = 0;                // +17 object id (houses, decoration, markers)
};

// 0x84-byte house record (player houses and neutral houses).
struct MapHouse {
    int player = 0;  // 0..7, or 8 for neutral houses
    std::int32_t cell_x = 0;
    std::int32_t cell_y = 0;
    std::uint8_t type = 0;          // +08 HOUSE_* (consts.lua)
    std::uint16_t build_state = 0;  // +0a 0 = finished
    float hp = 0;                   // +0c
    std::uint8_t field_1c = 0;
    std::uint32_t port_side = 0;
};

// 0x70-byte unit record.
struct MapUnit {
    int player = 0;
    float world_x = 0;              // +00 world pixels
    float world_y = 0;              // +04
    std::uint16_t type = 0;         // +08 UNIT_* (consts.lua)
    std::uint8_t orient_plus1 = 0;  // +0a TUnit.Orient + 1, 0 = random
};



struct MapDecoration {
    std::uint8_t type = 0;  // +00 sprite type: Craters.les entries, then Vertical.les, then burst.les (Craters_Init)
    std::uint16_t f01 = 0;  // +01 cycles left (0: forever); a load sets 5000 for a flat type and 0 for a standing one
    float f03 = 0;          // +03 frames per step
    std::uint8_t f07 = 0;
    float f08 = 0;  // +08 frame
    float f0c = 0;
    std::uint16_t f10 = 0;                // +10 first frame of the cycle
    std::uint16_t f12 = 0;                // +12 end of the cycle (0: the type's frame count)
    std::uint16_t f14 = 0;                // +14 delay in steps
    std::uint8_t f16 = 0;                 // +16 backwards
    std::uint8_t f17 = 0;                 // +17 done
    float world_x = 0;                    // +18 screen point (y raised by the ground height)
    float world_y = 0;                    // +1c
    std::array<std::uint8_t, 48> rest{};  // +20 the quad: 4 corners (top-left, top-right, bottom-left, bottom-right)
                                          // of f32 x, y, ground height
};

MapDecoration parse_decoration(ByteSpan record);

// Tail B route record: u8 n, u16 v, n lists of { u8 m, u16[m] }. Meaning unknown ("route cache").
struct MapRoute {
    std::uint16_t v = 0;
    std::vector<std::vector<std::uint16_t>> lists;
};

// Tail D special cell object, 14 bytes packed: u8, u16 x, u16 y, f32, f32, u8. Never present in shipped maps.
struct MapSpecial {
    std::uint8_t kind = 0;
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    float f_a = 0;
    float f_b = 0;
    std::uint8_t b = 0;
};

struct MapData {
    int width = 0;                             // cells per row
    int height = 0;                            // rows
    std::vector<MapCell> cells;                // index y * width + x
    std::vector<std::string> player_names;     // UTF-8, from UTF-16LE wchar[22]
    std::vector<std::uint8_t> player_nations;
    std::vector<std::uint8_t> player_bytes29;
    std::array<std::uint16_t, 8> house_counts{};
    std::array<std::uint16_t, 8> unit_counts{};
    std::vector<MapHouse> houses;  // player-major
    std::vector<MapUnit> units;    // player-major
    // Unit records stored beyond the declared counts (Brit_3M_3 declares too few; formats.md). They are units of the
    // first player and unit_counts[0] includes them, as in tools/map_dump.py; the original adds only the declared
    // counts (docs/bugs.md, data defects).
    int extra_unit_records = 0;
    std::vector<MapDecoration> decorations;    // tail A
    std::vector<std::uint16_t> obstacle_grid;  // tail B, (2 * width) * height, index (wy/24) * 2W + wx/48
    std::vector<MapRoute> routes;              // tail B
    std::vector<MapHouse> neutral_houses;      // tail C
    std::uint16_t ignored_unit_records = 0;    // tail C, read and discarded by the game (0 everywhere)
    std::uint16_t special_capacity = 0;        // tail D
    std::vector<MapSpecial> specials;          // tail D
    std::vector<std::uint32_t> port_sites;     // tail E, cellX | cellY << 16; absent in older maps
};

// Parses a whole .dat file (pack wrapper included). Gold's Fran_2r_2.dat (corrupt zlib stream) is an error.
std::optional<MapData> parse_map(ByteSpan file, std::string* error);
// Parses the inflated stream (starts with "~AMp").
std::optional<MapData> parse_map_stream(ByteSpan data, std::string* error);



struct LandTextures {
    static constexpr int kSide = 64;
    std::vector<std::array<std::uint8_t, 3>> flags;
    std::vector<Image> textures;
};
std::optional<LandTextures> parse_land_textures(ByteSpan file, std::string* error);

}  // namespace antanta
