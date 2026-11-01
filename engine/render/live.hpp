// The isometric view of a running game (docs/render.md): the map viewer's compositor (render/iso) fed from a
// sim::GameState instead of a map file. Read only: nothing here writes the state.
//
// Terrain, water and map objects come from the save's map chunk (objects follow GameState::map.objects, so planted
// fields and cut trees show); houses come from the players' house slots with their graphic (standing, build stage,


#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "formats/svg.hpp"
#include "render/effects.hpp"
#include "render/iso.hpp"
#include "render/sel_ring.hpp"
#include "render/shells.hpp"
#include "render/unit_fx.hpp"
#include "render/water.hpp"
#include "sim/state.hpp"

namespace antanta {

// The renderer's map from a save: heights, the 22-byte tile records (tile, shade, sub-tiles), objects, the water
// grid and the coast polygons. No houses or units (they come from the GameState).
MapData map_from_save(const SvgSave& save);

struct LiveView {
    MapData map;
    std::vector<MapHouse> houses;           // the houses drawn (hp >= -149000, on the map), in slot order
    std::vector<sim::HouseRef> house_refs;  // the slot of each
    WorldView view;
    std::uint64_t signature = 0;  // 0 until the first live_view_sync (which redraws all)
    // What the last live_view_sync changed, so the static tiles redraw only there (ours, smooth scrolling): every
    // house drawn before or after whose look changed, as it was and as it is (nation, graphic), and every cell
    // whose object, height or ground changed (with its object before). all: the first sync, or a player's nation or
    // colour changed.
    struct HouseLook {
        MapHouse house;
        int nation = 0, graphic = 0;
    };
    std::vector<HouseLook> changed_houses;
    std::vector<std::array<int, 3>> changed_cells;  // cell x, row, object before
    std::vector<SpecialDraw> changed_specials;      // special objects as they were and as they are
    bool changed_all = false;
    GroundFx fx;       // the craters and the cell animations of this game (render/ground_fx)
    HouseFx house_fx;  // the frames of the houses' animations (render/house_fx)
    // The effect list of this game and the pictures of the types it used (render/effects, loaded on first use).
    Effects effects;
    EffectGraphics effect_graphics;
    ShellTrails shell_trails;  // the smoke trails behind the shells (render/shells)
    // The muzzle and rifle flashes and the burnt stains (render/unit_fx, pictures loaded on first use).
    UnitFx unit_fx;
    UnitFxGraphics unit_fx_graphics;
    Water water;  // the water's frames, the ships' waves and the bubbles (render/water)
};
// Builds the view's houses and objects from the state; true when the static layer has to be redrawn.
bool live_view_sync(LiveView& lv, const sim::GameState& s);
// The static tiles a sync's changes touch (house_static_rect, cell_static_rect), or all of them.
void static_tiles_apply_changes(StaticTiles& tiles, const WorldGraphics& g, const LiveView& lv);

// One animation (NumWork entry) of one unit graphic, loaded on first use.
struct UnitAnim {
    bool ok = false;
    int frames_per_dir = 0;
    // direction-major, frames_per_dir * 5 each (mask and shadow empty if the .ugr has none)
    std::vector<UnitFrame> sprite, mask, shadow;
};



struct ShipEntry {
    bool ok = false;
    std::vector<UnitFrame> singles;
    std::vector<std::vector<UnitFrame>> groups;
    std::vector<UnitFrame> reflection;
};


struct ShipGraphics {
    bool ok = false;
    int parts = 0;                                   // G+6 hull pieces per direction
    int frames = 0;
    int launch_frames = 0;
    int death_frames = 0;
    std::vector<std::uint8_t> turret_steps;
    std::vector<std::uint8_t> turret_base;
    std::array<std::vector<std::uint8_t>, 4> order;
    std::map<int, ShipEntry> entries;                // .les entry id -> frames, loaded on first use
    std::vector<std::array<float, 3>> smoke;
};

struct UnitSprites {
    std::map<std::string, std::map<int, UnitAnim>> anims;  // .les path -> NumWork -> frames
    std::map<std::string, ShipGraphics> ships;             // ship .les path -> graphic
    ShellGraphics shells;                                  // the shells' pictures (render/shells)
    std::vector<std::string> warnings;                     // archives or entries that could not be read
};


struct UnitPose {
    bool visible = false;
    int nation = 0, type = 0;  // the type whose graphic and sizes are used (type 1 while Work is 0xb)
    int num_work = 0, frame = 0, dir = 0;
    bool mirror = false;
};
UnitPose unit_pose(const sim::GameState& s, std::uint8_t player, const sim::Unit& u);






std::optional<std::array<float, 4>> unit_mark_frame(const WorldGraphics& g, UnitSprites& sprites,
                                                    const GameFileLoader& load, const sim::GameState& s,
                                                    std::uint8_t player, const sim::Unit& u);

// The ground point of a unit on the screen of view position pos (X - viewX, Y - viewY - height - altitude).
std::array<float, 2> unit_screen_point(const sim::Unit& u, ViewPos pos);

// Moving things drawn between the last two ticks (the game window only; docs/interface.md D.5): where each unit and
// shell was before the last tick, and how far the clock is into the next one. The state is not touched; a unit is
// matched by its serial, a shell by its start, target and owner one tick of flight earlier. A jump of more than
// kTeleport pixels (a unit put somewhere else) is drawn at once.
struct DrawBlend {
    static constexpr float kTeleport = 150;
    struct ShellWas {
        float x0, y0, x1, y1;
        std::int32_t flown;
        std::uint8_t owner_player;
        std::uint32_t owner_serial;
        float cx, cy, cz;
    };
    // By serial: x, y - height - altitude, x NaN for a serial with no unit (an array, not a hash map: it is filled for
    // every unit before every tick, docs/bugs.md B59).
    std::vector<std::array<float, 2>> units;
    std::vector<ShellWas> shells;
    float alpha = 1;  // 0: as before the last tick, 1: as now
};
// Before a tick: remember where everything is.
void draw_blend_snapshot(DrawBlend& b, const sim::GameState& s);
// The screen offset to add to unit_screen_point for a unit (0, 0 without a blend).
std::array<float, 2> draw_blend_offset(const DrawBlend* b, const sim::Unit& u);

// One frame of the running game into rgba (the static layer's size). static_layer must be up to date
// (render_static_layer with lv.view after live_view_sync said so).
void render_live_frame(const WorldGraphics& g, UnitSprites& sprites, const GameFileLoader& load,
                       const sim::GameState& s, ViewPos pos, const Target& static_layer, Target* work,
                       std::vector<std::uint8_t>* rgba);



struct UnitMarkQuad {
    std::uint32_t serial = 0;              // the unit's
    bool before = false;                   // the corners: before the unit's sprite; the bars and digits after it
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // whole view pixels
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // atlas pixels (flipped when u1 < u0 or v1 < v0)
    std::uint32_t argb = 0xffffffff;       // times the texel
    float key = 0;
};
struct UnitMarks {
    const std::uint8_t* atlas = nullptr;  // RGBA8, atlas_w x atlas_h
    int atlas_w = 0, atlas_h = 0;
    std::span<const UnitMarkQuad> quads;
};

// render_live_frame after its first part: `work` already holds the static layer under the view at pos
// (render_frame_begin, or render_frame_begin_tiles for the smooth camera).
// blend: units and shells between the last two ticks (DrawBlend), nullptr as they are (batch runs, tests).
// anims: the houses' animations (house_fx_quads), drawn among the units in the order of their bottom edges.
// effects: the effect list's quads (effects_quads): the ground ones before the units, the others after all of them.
// marks: the units' marks (UnitMarkQuad), each drawn with its unit.
// shell_trails: the trails behind the shells (render/shells), nullptr: the shells without their trails.

// unit_fx: the flashes drawn with their units and the burnt stains of the dead list (render/unit_fx); nullptr: none.

void render_live_units(const WorldGraphics& g, UnitSprites& sprites, const GameFileLoader& load,
                       const sim::GameState& s, ViewPos pos, Target& work, std::vector<std::uint8_t>* rgba,
                       const DrawBlend* blend = nullptr, std::span<const HouseFxQuad> anims = {},
                       std::span<const EffectQuad> effects = {}, const UnitMarks* marks = nullptr,
                       const ShellTrails* shell_trails = nullptr, const RingTexture* ring_tex = nullptr,
                       std::span<const RingDot> house_ring = {}, const UnitFxDraw* unit_fx = nullptr);


// pixels: for every ship whose hull the view draws (as render_live_units), the hull frame's reflection picture
// mirrored about the waterline on a 6 x 6 grid that wobbles with the tick (as the bridges' reflections), its colour

// only, after the water and the ships' selection rings and before the units (the original draws each in the unit
// pass, over the units drawn before it where they stand on water: docs/bugs.md B76).
// ship_slots: players_with_ship_slots of this state, or nullptr (then it is made here).
void ship_reflections_draw(const WorldGraphics& g, UnitSprites& sprites, const GameFileLoader& load,
                           const sim::GameState& s, ViewPos pos, Target& rt, const DrawBlend* blend = nullptr,
                           const std::vector<bool>* ship_slots = nullptr);






void ship_smoke(Effects& fx, EffectGraphics& eg, const WorldGraphics& g, UnitSprites& sprites,
                const GameFileLoader& load, const sim::GameState& s, ViewPos pos, int w, int h,
                const std::vector<bool>* ship_slots = nullptr);

// Per player: whether any of its unit slots (in memory order, free ones too) holds a ship type, by the unit
// parameters' move class or by the game's types (UnitType::is_ship). A player without one has no ship in its list,
// so the per-tick ship passes (the smoke, the waves) skip its list walk, slow on long lists (docs/bugs.md B59). One
// scan for both passes.
std::vector<bool> players_with_ship_slots(const WorldGraphics& g, const sim::GameState& s);

}  // namespace antanta
