// The houses' animations (docs/render.md 13): chimney smoke, flags, the barn's sails, the sawmill's log, the mine
// wheels, the stable's horse and the like. Each house type lists up to 10 Animation.anf records at points of its



//
// Timing: the original steps a slot's frame once per drawn frame (one per tick at normal speed); ours once per tick
// for the slots drawn in the last frame, so they stand still while the game is paused (as render/ground_fx).
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "formats/bytes.hpp"

namespace antanta {

struct WorldGraphics;
struct WorldView;
struct ViewPos;
struct Target;

struct HouseFxGraphics {


    struct Frame {
        int w = 0, h = 0;
        bool argb4444 = false;
        std::vector<std::uint16_t> px;
    };

    struct Record {
        std::string name;
        float speed = 0;
        int frames = 0;
        int w = 0, h = 0;
        int ox = 0, oy = 0;
        std::vector<Frame> pics;
    };
    std::vector<Record> records;
    std::vector<std::string> warnings;
};

using HouseFxLoader = std::function<std::optional<Bytes>(const std::string& path, std::string* error)>;


void load_house_fx_graphics(HouseFxGraphics& g, const HouseFxLoader& load);

std::optional<HouseFxGraphics::Frame> decode_house_fx_frame(ByteSpan entry, std::string* error);


struct HouseFx {
    struct Slot {
        int record = -1;
        float frame = 0;     // CreateHouse starts it at 0
        bool drawn = false;  // drawn in the last frame: the next step advances it
    };
    std::map<std::array<int, 5>, Slot> slots;
};

// One slot's quad in view pixels: x0, y0 the top left (whole pixels), w x h; the key of its bottom edge (the top edge's
// is that + h, it leans back as a unit does).
struct HouseFxQuad {
    const HouseFxGraphics::Frame* pic = nullptr;
    int x0 = 0, y0 = 0, w = 0, h = 0;
    double key = 0;
};



void house_fx_quads(const WorldGraphics& g, const WorldView& view, ViewPos pos, int w, int h, HouseFx& fx,
                    std::vector<HouseFxQuad>* out);


void house_fx_step(HouseFx& fx, const HouseFxGraphics& g);
// Draws one quad into the rows band_top <= y < band_bottom: depth test, no depth write; a .444 frame alpha blended, a
// BMP frame opaque but for its colour key.
void house_fx_draw(const HouseFxQuad& q, Target& rt, int band_top, int band_bottom);

}  // namespace antanta
