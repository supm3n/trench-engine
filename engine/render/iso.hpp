// The original's isometric map view (docs/render.md), composited on the CPU: a static layer redrawn when the view
// moves (houses with .hzb depth, water depth, terrain, sub-tiles, map objects, shadows) and a per-frame pass
// (animated water texels, y-sorted units with the depth test, landscape filter). The caller only uploads the RGBA
// result. tools/render_proto.py is the reference; where render.md is guessed, the same guesses are made here and
// marked "guess (render.md 11)".
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "formats/map.hpp"
#include "formats/params.hpp"
#include "render/ground_fx.hpp"
#include "render/house_fx.hpp"
#include "render/raster.hpp"

namespace antanta {


constexpr int kViewWidth = 1024;
constexpr int kViewHeight = 768;
constexpr int kViewCols = 11;
constexpr int kViewRows = 34;

// Reads a game file by its path relative to the game folder, '/'-separated (e.g. "Data/Colors.txt").
using GameFileLoader = std::function<std::optional<Bytes>(const std::string& path, std::string* error)>;




struct SpecialDraw {
    int x = 0, y = 0, kind = 0;
    bool built = false;
    bool operator==(const SpecialDraw&) const = default;
};

// What the renderer reads from the game, read-only. Built from the map file for now (no sim yet); a GameState
// view can fill the same fields later.
struct WorldView {
    const MapData* map = nullptr;      // terrain, water grid, coast polygons
    std::span<const MapHouse> houses;  // drawn in MapY * 100 + MapX order
    std::span<const MapUnit> units;
    std::vector<int> nations;  // per player: BasicParams file index (0 Russia .. 4 Austria-Hungary)
    std::vector<int> colours;
    // Per house (same order as `houses`): the .les id of its picture, house_les_id(phase, stage, 0) of the house's
    // graphic. Empty: 0 for finished houses, 10 (build stage 0) otherwise, the map viewer's rule.
    std::vector<int> house_graphic;
    // Per house (same order): the nation of its record (THouse::Basic; a house that came over in a merge across
    // nations keeps its own). Empty or -1: its player's.
    std::vector<int> house_nation;


    bool see_through = false;
    std::vector<SpecialDraw> specials;  // in list order
    // The size of the picture the static layer is drawn for (the game app's view covers its window); the per-frame
    // passes take it from the static layer.
    int width = kViewWidth, height = kViewHeight;
};
// Houses and units of the map's players. Nations from the player names, else player % 5 (as the prototype).
WorldView world_view_from_map(const MapData& map);



struct ViewPos {
    int col = 0;
    int row = 0;
    int dx = 0, dy = 0;
};
// The view's top-left corner in world pixels.
inline int view_x(ViewPos p) { return p.col * 96 + p.dx; }
inline int view_y(ViewPos p) { return p.row * 24 + p.dy; }
// The ViewPos whose corner is world pixel (x, y).
ViewPos view_pos_at(int x, int y);
// Clamps to 0 .. the ScrollView bounds (render.md 2): row <= H - viewRows - 10, col <= W - viewCols - 2.
ViewPos clamp_view(const MapData& map, ViewPos p);

struct HouseGraphics {  // one phase of one house type
    int anchor_x = 0, anchor_y = 0;
    Image picture;                    // .img, alpha 255 inside the spans
    std::optional<Image> mask;        // _m.img, same size
    std::vector<std::int16_t> depth;  // _z.hzb, picture-sized, empty if missing; 0x7FFF = no depth


    std::vector<std::int16_t> blit_depth;
    std::optional<G4m> shadow;  // _sh.g4m
    Texture shadow_texture;
};

struct UnitFrame {
    bool ok = false;
    int off_x = 0, off_y = 0;
    Texture texture;
};
struct UnitGraphics {
    int width = 0, height = 0, anchor_x = 0, anchor_y = 0;
    std::array<UnitFrame, 5> sprite;  // frame 0 of NumWork 0 per direction
    std::array<UnitFrame, 5> mask;    // player-colour layer, if the .ugr has one
};

struct ObjectAtlas {  // tree.ogr / stone.ogr / miscs.ogr from Data/Objects/objects.les ids 0, 1, 2
    Bytes entries;    // 0x38-byte records
    std::vector<Texture> pages;
};

struct WorldGraphics {
    std::vector<Texture> tiles;   // LandTextures.dat, 64 x 64
    std::vector<Texture> splats;  // 128 x 128 sub-tile textures with the m44.bmp alpha
    std::array<ObjectAtlas, 3> objects;
    Texture water;  // Misc/Water/Anim0/Anim_0.les frame 0
    int filter_w = 0, filter_h = 0;
    std::vector<std::array<float, 4>> filter;         // Filter.tga per texel: ADDSMOOTH factor r, g, b and 1 - alpha
    std::vector<std::array<float, 3>> player_colour;  // Colors.txt / 255
    std::vector<std::array<std::uint8_t, 3>> player_rgb;  // Colors.txt
    std::array<std::optional<BasicParams>, 6> params;
    std::map<std::array<int, 3>, HouseGraphics> houses;  // (nation, type, phase base)
    std::map<std::array<int, 2>, UnitGraphics> units;    // (nation, type)
    GroundFxGraphics fx;       // craters and the animated cells (render/ground_fx); missing files only warn
    HouseFxGraphics house_fx;  // the houses' animations (render/house_fx); missing files only warn


    std::map<int, Texture> specials;
};

// Loads everything that does not depend on the map. Missing or broken files are an error.
std::optional<WorldGraphics> load_world_graphics(const GameFileLoader& load, std::string* error);
// Loads the house and unit graphics the view refers to (once per type). Missing archives or records are skipped
// with a line in `warnings`, as the prototype does.
void load_view_graphics(WorldGraphics& g, const WorldView& view, const GameFileLoader& load,
                        std::vector<std::string>* warnings);


void render_static_layer(const WorldGraphics& g, const WorldView& view, ViewPos pos, Target* out);
// The static layer in world-anchored tiles, for a camera that moves by single pixels (ours: smooth scrolling). A tile
// is the static layer drawn for its own corner, so the view at any pixel is a copy of the tiles under it; moving the
// view draws only the tiles it newly reaches, as the original redraws only the strip a scroll step exposes
// (docs/bugs.md "How a frame is built").
struct StaticTiles {
    static constexpr int kSize = 256;            // pixels, square
    std::map<std::array<int, 2>, Target> tiles;  // by tile column and row
    int drawn = 0;                               // tiles drawn so far (for measurements)
};
// Forget every tile (the houses or the map objects changed).
void static_tiles_clear(StaticTiles& t);

// A rectangle in world pixels (x1, y1 exclusive), and the static pixels a house or a map cell can touch: when it
// changes, only the tiles under its rectangle as it was and as it is are drawn again.
struct WorldRect {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
WorldRect house_static_rect(const WorldGraphics& g, const MapData& map, const MapHouse& h, int nation, int graphic);



int house_draw_height(const MapData& map, const MapHouse& h);
// The height of its depth keys (+ 2 * height): 90 for a port, else house_draw_height.
int house_key_height(const MapData& map, const MapHouse& h);
WorldRect cell_static_rect(const WorldGraphics& g, const MapData& map, int cx, int row, int object);
WorldRect special_static_rect(const SpecialDraw& o);
void static_tiles_invalidate(StaticTiles& t, WorldRect r);
// The static layer under the w x h view at pos into `work` (keys as render_static_layer gives them for that corner),
// drawing the tiles it lacks, then the animated water as render_frame_begin (water false: the caller draws the
// water itself, render/water). Tiles more than one tile away from the view are dropped; up to `prefetch` of the tiles
// in that ring are drawn ahead.
void render_frame_begin_tiles(const WorldGraphics& g, const WorldView& view, StaticTiles& tiles, ViewPos pos, int w,
                              int h, Target* work, int prefetch = 0, bool water = true);


void render_frame(const WorldGraphics& g, const WorldView& view, ViewPos pos, const Target& static_layer, Target* work,
                  std::vector<std::uint8_t>* rgba);
// render_frame in its parts, for callers that draw their own units: the copy of the static layer and the animated
// water (begin), then the landscape filter and the 8-bit output (finish). render_frame = begin, map units, finish.
void render_frame_begin(const WorldGraphics& g, ViewPos pos, const Target& static_layer, Target* work);
void render_frame_finish(const WorldGraphics& g, ViewPos pos, Target& work, std::vector<std::uint8_t>* rgba);



struct SubtileLattice {
    std::array<float, 23> h{}, light{};
    std::array<std::uint16_t, 23> id{};
};
SubtileLattice subtile_lattice(const MapData& map, int cx, int row);

// "Data/Units/<DataPath>.les" of a unit type (a numeric path "NTT" is nation N-1 type TT), empty if unknown.
std::string unit_graphic_path(const WorldGraphics& g, int nation, int type);
// The house record whose graphics a house type uses (a numeric path "1NN" is nation N-1 type NN), or nullptr.
const HouseParams* house_graphic_params(const WorldGraphics& g, int nation, int type);
// The nation whose pictures house i of the view uses: WorldView::house_nation, else its player's (5 for player 8).
int view_house_nation(const WorldView& v, std::size_t i);

}  // namespace antanta
