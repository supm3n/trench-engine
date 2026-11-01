// Sprite formats (docs/formats.md "Sprites"): .444, .ugr, .egr, .img, .ogr, .g4m/.g4, .hzb, .shp/.twr.
// Pixels come out as RGBA8 (ARGB4444 nibbles expanded x17; .img RGB with alpha 255 inside spans, 0 outside).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bytes.hpp"

namespace antanta {

struct Image {
    int width = 0;
    int height = 0;
    Bytes rgba;  // width * height * 4
};

struct SpriteFrame {
    int off_x = 0;  // top-left on the animation canvas
    int off_y = 0;
    bool has_pixels = false;  // false for the "empty" frames whose data offset points past the file end
    Image image;
};

enum class SpriteKind { None, Tex444, Ugr, Egr, Img, Ogr, G4m, Hzb, Shp };

// Format by file name, as tools/sprite_dump.py handler_for: by extension, and extensionless files whose size
// fits a .444 (Misc/TrenchAndPonton) are .444. .g4 is .g4m, .twr is .shp.
SpriteKind sprite_kind(std::string_view name, ByteSpan data);

// `pixels` must hold at least width * height * 2 bytes.
Image decode_argb4444(ByteSpan pixels, int width, int height);

std::optional<Image> decode_444(ByteSpan file, std::string* error);

struct Ugr {
    int frames_per_dir = 0;
    int flags = 0;  // bit 1: mask layer, bit 0: shadow layer
    // layers[0] sprite, then mask, then shadow as flagged; each frames_per_dir * 5 frames, direction-major.
    std::vector<std::vector<SpriteFrame>> layers;
};
std::optional<Ugr> decode_ugr(ByteSpan file, std::string* error);

struct Egr {
    std::uint32_t canvas_w = 0;
    std::uint32_t canvas_h = 0;
    std::uint16_t anchor_x = 0;
    std::uint16_t anchor_y = 0;
    std::vector<SpriteFrame> frames;
};
std::optional<Egr> decode_egr(ByteSpan file, std::string* error);

std::optional<Image> decode_img(ByteSpan file, std::string* error);

struct Ogr {
    std::uint16_t entry_count = 0;  // 56-byte entries, layout not decoded; kept raw
    Bytes entries;
    std::vector<Image> pages;  // 256 x 256 atlases
};
std::optional<Ogr> decode_ogr(ByteSpan file, std::string* error);

struct G4m {
    std::uint16_t canvas_w = 0;
    std::uint16_t canvas_h = 0;
    std::uint16_t off_x = 0;
    std::uint16_t off_y = 0;
    std::uint16_t tex_w = 0;
    std::uint16_t tex_h = 0;
    Image image;  // cols x rows
};
std::optional<G4m> decode_g4m(ByteSpan file, std::string* error);

struct DepthMap {
    int width = 0;
    int height = 0;
    bool spans = false;                // "~cHMap" variant (true) or raw u16 array (false)
    std::vector<std::uint16_t> depth;  // width * height, 0x7FFF = no building



    std::vector<std::int16_t> blit;
};
constexpr std::uint16_t kNoDepth = 0x7FFF;


std::optional<DepthMap> decode_hzb(ByteSpan file, std::string* error, std::uint16_t gap = kNoDepth);




std::uint16_t house_les_id(int phase, int stage, int part);

// .shp and .twr: inline frames and u8 group counts (formats.md). Offsets are raw (not normalised).
std::optional<std::vector<SpriteFrame>> decode_shp(ByteSpan file, std::string* error);

}  // namespace antanta
