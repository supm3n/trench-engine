





#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "render/iso.hpp"
#include "render/raster.hpp"
#include "sim/events.hpp"
#include "sim/state.hpp"

namespace antanta {

// Data/Misc/Shots/Info.txt lists 8 sets: 0 SmallCalibre_Shot, 1 MainCalibre_Shot, 2 MineThrower_Shot, 3 Mortira_Shot,
// 4 Small_Flash, 5 Big_Flash, 6 FlameThrower_Shot, 7 Mashine_Gas.
constexpr int kShotSets = 8;

struct UnitFxGraphics {



    struct ShotSet {
        bool tried = false;
        int count = 0;
        int w = 0, h = 0;
        int dx = 0, dy = 0;
        std::vector<std::optional<Texture>> frames;
    };
    bool listed = false;
    std::vector<std::string> names;  // Data/Misc/Shots/Info.txt after its count
    std::array<ShotSet, kShotSets> shots;
    bool stills_tried = false;
    std::optional<Texture> rifle;
    std::array<std::optional<Texture>, 8> burnt;  // BurntStains.les ids 0..7: burn_1_0..3, burn_2_0..3
    std::vector<std::string> warnings;
};

void shot_set_load(UnitFxGraphics& g, int set, const GameFileLoader& load);

void unit_fx_stills_load(UnitFxGraphics& g, const GameFileLoader& load);



struct ShotAnim {
    int set = -1;
    float px = 0, py = 0;                // +4, +8: the pivot, world pixels
    std::array<float, 4> x{}, y{}, z{};
    float angle = 0;
    float frame = 0;
    std::uint16_t end = 0;
    std::uint16_t loops = 1;
    bool done = false;
    bool shown = false;                  // ours: the last step showed `frame`
};

struct UnitFx {

    struct Flash {
        sim::UnitRef unit;
        std::uint8_t weapon = 0, barrel = 0;
        bool flag = false;
        bool before = false;
        std::array<ShotAnim, 2> obj;
    };
    std::vector<Flash> flashes;
    std::vector<sim::UnitRef> rifle;
    std::uint32_t stepped_tick = 0;
};



void unit_fx_mute(const UnitFx& fx, std::vector<sim::SimEvent>& events);



void unit_fx_tick(UnitFx& fx, UnitFxGraphics& ug, const GameFileLoader& load, const WorldGraphics& g,
                  const sim::GameState& s, const std::vector<sim::SimEvent>& events, ViewPos pos, int w, int h);

// A quad drawn with its unit in render_live_units: view pixels with the unit where it stands at the tick (the draw
// adds the unit's blend offset), keys as the original's.
struct UnitFxQuad {
    enum class Order : std::uint8_t {
        Dead,          // the burnt stain: in the dead list's pass, before the corpse
        WeaponBefore,  // a flash when the unit faces away (angle below pi/2 or from 3pi/2): before its sprite
        Rifle,         // the rifle flash, after the sprite and the mask
        WeaponAfter,   // a flash otherwise: after the sprite, the rifle flash and the marks
    };
    std::uint32_t serial = 0;
    Order order = Order::WeaponAfter;
    const Texture* texture = nullptr;
    std::array<Vertex, 4> v{};  // top-left, top-right, bottom-left, bottom-right
    float tint = 1;
    float alpha = 1;            // the texture factor's alpha (the dead list's fade)
};
// The shown flash pictures and this tick's rifle flashes with the view at pos.
void unit_fx_quads(const UnitFx& fx, const UnitFxGraphics& ug, const sim::GameState& s, ViewPos pos,
                   std::vector<UnitFxQuad>* out);



void burnt_stain_quads(const UnitFxGraphics& ug, const MapData* map, const sim::Unit& u, int anchor_x, ViewPos pos,
                       std::vector<UnitFxQuad>* out);

// What render_live_units draws of this module.
struct UnitFxDraw {
    std::span<const UnitFxQuad> quads;         // unit_fx_quads
    const UnitFxGraphics* graphics = nullptr;  // the stains' pictures
    const MapData* map = nullptr;              // the stains' ground
};
// One quad with its depth test, no z write, blended (render_live_units).
void unit_fx_draw(const UnitFxQuad& q, float ox, float oy, Target& rt, int band_top, int band_bottom);

}  // namespace antanta
