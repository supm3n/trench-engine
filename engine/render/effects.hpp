// The effect list of the original (docs/render.md 14): explosions, smoke and fire, dust, muzzle flashes, the move




// first time a type is made.
//
// Render only: the records are view state, never saved, read by no sim code. The sim raises SimEvent::Effect where
// its code makes one (sim/events.hpp); the view adds those and its own (effects_emit: the original's drawing code
// makes some itself, with CRT rand()). Timing: ours steps once per tick (the original once per drawn frame, one per
// tick at normal speed, and on during a pause), so the list stands still while the game is paused.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "formats/bytes.hpp"
#include "render/iso.hpp"

namespace antanta {

struct Target;

constexpr int kEffectTypes = 50;


const char* effect_file(int type);

struct EffectGraphics {




    struct Frame {
        int x = 0, y = 0, tw = 0, th = 0, w = 0, h = 0;
        int tex_w = 0, tex_h = 0;
        std::vector<std::uint16_t> px;
    };
    struct Type {
        bool tried = false;
        int canvas_w = 0, canvas_h = 0;
        int anchor_x = 0, anchor_y = 0;
        int count = 0;
        std::vector<Frame> frames;
    };
    std::array<Type, kEffectTypes> types;
    std::vector<std::string> warnings;
};


std::optional<EffectGraphics::Type> decode_effect_type(ByteSpan entry, std::string* error);


void effect_type_load(EffectGraphics& g, int type, const GameFileLoader& load);



struct EffectSpawn {
    std::uint8_t kind = 3;
    std::uint8_t type = 0;
    std::int32_t x = 0, y = 0, z = 0;
    std::uint16_t delay = 0;
    std::int32_t rise = 0;
    std::int32_t rise_step = 1;
    float frame = 0;
    float speed = 1;
    std::uint8_t ground = 0;
};




struct TreeLeaves {
    struct Leaf {
        float x = 0, y = 0;    // +4, +8: from the cell's point, x 0 .. w - 1, y -h .. -1 at the start
        float fall = 0;
        float swing = 0;
        float phase = 0;
        float phase_step = 0;
        float u0 = 0, u1 = 0;
        float v0 = 0, v1 = 0;
    };
    int cell = 0;
    int x = 0, y = 0;
    int ground = 0;    // the cell's height (ours: the leaves' depth sits on the ground, docs/bugs.md B52)
    int w = 0, h = 0;
    int page = 0;
    int life = 800;
    std::vector<Leaf> leaves;
};

struct Effects {
    struct Item {
        EffectSpawn e;
        int end = 0;
        bool shown = false;
        int shown_frame = 0;
        int shown_height = 0;  // the draw item's height (short)(rise + z)
    };
    std::vector<Item> list;          // oldest first: the original's head is the back
    std::uint32_t stepped_tick = 0;  // the tick of the last step (no step while the tick stands)
    std::uint32_t rand_state = 1;    // the view's own generator for effects_emit (the original uses CRT rand())
    std::vector<TreeLeaves> leaves;
    std::unordered_map<std::uint32_t, std::uint32_t> dust_gate;
};


void effects_add(Effects& fx, EffectGraphics& g, const GameFileLoader& load, const EffectSpawn& e);



void effects_step(Effects& fx, const EffectGraphics& g, std::uint32_t tick);




struct EffectQuad {
    const EffectGraphics::Frame* frame = nullptr;
    int x0 = 0, y0 = 0, w = 0, h = 0;
    double key_bottom = 0, key_top = 0;
    bool ground = false;
    const Texture* leaf = nullptr;
    double lx = 0;
    float u0 = 0, u1 = 0, v0 = 0, v1 = 0;
};


void effects_quads(const Effects& fx, const EffectGraphics& g, ViewPos pos, int w, int h, std::vector<EffectQuad>* out);




void leaves_add(Effects& fx, const ObjectAtlas& trees, const MapData& map, int x, int y, int record);


void leaves_step(Effects& fx);



void leaves_quads(const Effects& fx, const ObjectAtlas& trees, ViewPos pos, int w, int h, std::vector<EffectQuad>* out);





void effect_draw(const EffectQuad& q, Target& rt, int band_top, int band_bottom);

}  // namespace antanta
