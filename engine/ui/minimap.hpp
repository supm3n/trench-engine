// The minimap of the original (docs/interface.md A.6, slice I2), the part without SDL: where it goes for a window and
// an interface scale, the static layer from the map, the unit dots, the red view rectangle, what a click and a right
// click on it mean, and the battle marks. Drawing is in ui/minimap_draw.hpp.
//
// Positions follow the original's rules (A.2, A.5, A.6) in window pixels with every offset and size times the
// interface scale k, so k = 1 gives the original's numbers. The minimap itself is n = 200 k pixels square and is
// computed at that size from the map (full resolution, not a 200 x 200 picture scaled up).
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "formats/map.hpp"
#include "ui/hud.hpp"
#include "ui/iface_layout.hpp"
#include "ui/unit_panel.hpp"

namespace antanta::ui {

constexpr int kMinimapSize = 200;

struct MinimapLayout {
    int scale = 1;
    bool shown = true;
    int x = 0, y = 0;
    int size = 0;      // 200 k
    IRect plate;       // MiniMapBevelUp.bmp (208 x 40) at (mmx - 8, mmy - 36), also while hidden
    IRect left;        // MiniMapBevelleft.bmp (11 x 212) at (mmx - 10, mmy - 8), only while shown
    IRect arrow;       // the hide / show button (mmx + 168, mmy - 36) .. (mmx + 201, mmy - 6), minimaparrow.bmp
    int width = 0, height = 0;
};
MinimapLayout minimap_layout(int w, int h, int k, bool shown);



bool minimap_contains(const MinimapLayout& l, int x, int y);


bool minimap_hud_contains(const MinimapLayout& l, int x, int y);

bool minimap_arrow_contains(const MinimapLayout& l, int x, int y);






std::array<int, 4> minimap_view_rect(const MinimapLayout& l, int map_w, int map_h, double cam_x, double cam_y,
                                     int view_w, int view_h);




ViewPos minimap_camera_cell(const MinimapLayout& l, int map_w, int map_h, int x, int y, int view_w, int view_h);
// The same point for the smooth camera (ours): the view's top-left corner in world pixels that centres the view on
// the clicked point, for camera_jump (which keeps it inside the view limits).
std::array<double, 2> minimap_camera_point(const MinimapLayout& l, int map_w, int map_h, int x, int y, int view_w,
                                           int view_h);
// The world point of a right click on the minimap (A.6): ((mx - mmx) * sx * 96 + 48, (my - mmy) * sy * 24 + 12),
// truncated.
WorldPoint minimap_world_point(const MinimapLayout& l, int map_w, int map_h, int x, int y);




enum class HudButton : std::uint8_t { Objective, Economic, Military, Help, PlayMenu, Arrow };
constexpr int kHudButtons = 6;
// A button's rectangle in window pixels, the one the hover and click test uses (strict `<` on every edge, as
// minimap_arrow_contains). The Objective button's picture is wider (33 x 29) than its rectangle (28 x 29), see below.
IRect hud_button_rect(const MinimapLayout& l, HudButton b);
// The button under window pixel (x, y), or -1; the index is the HudButton value.
int hud_button_hit(const MinimapLayout& l, int x, int y);
// IDS_SHOW_ZADANIE, IDS_ECONOM_ASSISTANT, IDS_WAR_ASSISTANT, IDS_HELP, IDS_OPTIONS, IDS_MINIMAP_HIDE while the
// minimap is shown and IDS_MINIMAP_SHOW while hidden. Objective opens the mission window (F1) and Help the hints
// (slice I8), Play menu the in-game menu (ui/game_menu.hpp).
std::string hud_button_tooltip(const StringTable& strings, HudButton b, bool minimap_shown);

struct HudButtonsState {
    bool economic = false, military = false;
    int hover = -1;                           // hud_button_hit of the mouse
    bool left_down = false;
};




struct HudButtonDraw {
    HudButton button = HudButton::Objective;
    int frame = 0;
    IRect dst;  // window pixels, the picture's own size times k (26 x 23, the arrow 33 x 30, the objective 33 x 29)
};
std::array<HudButtonDraw, kHudButtons> hud_buttons_draw(const MinimapLayout& l, const HudButtonsState& st);
// The hover tooltip as one screen text entry in interface pixels at (mmx + 24, mmy - 54) (every button the same place);
// empty text: none. Help and Play menu show none while the left button is held.
struct HudTooltip {
    std::string text;
    int x = 0, y = 0;
};
HudTooltip hud_button_tooltip_line(const MinimapLayout& l, const StringTable& strings, const HudButtonsState& st);
// The advisor buttons' order (the app's toggle_advisor input, no O line): player, which (0 economic, 1 military).
sim::Command hud_advisor_order(std::uint8_t player, bool military);
// The advisor switches of Game options in a game (docs/frontend.md 4.4): the six economic and four military check
// boxes of your player (save chunk 7) and the conduct template AI[6] (0 Passive .. 5 Naval battle).
struct GameAdvisors {
    std::array<std::uint8_t, 6> economic{};
    std::array<std::uint8_t, 4> military{};
    std::uint8_t conduct = 2;
};


std::vector<sim::Command> game_options_orders(std::uint8_t player, const GameAdvisors& a);

// ---- what one frame draws (ui/minimap_draw.hpp) ----

struct MinimapDot {  // a unit: a square of `size` pixels at (x, y) of the minimap
    int x = 0, y = 0, size = 1;
    std::uint32_t rgb = 0;
};
constexpr int kBattleFrames = 4;  // Battle.les 0.444 .. 3.444, 8 x 8
struct MinimapMarkDraw {          // a battle mark frame (16 x 16) with its top-left corner at (x, y) of the minimap
    int x = 0, y = 0, frame = 0;
};



std::uint32_t rgb565(std::uint32_t rgb);




struct MinimapPalette {
    std::vector<std::array<std::uint8_t, 3>> tiles;
    std::array<std::uint32_t, 8> players{};  // 0xRRGGBB
};


// ftol(W * py * sy + px * sx); map objects 101..999 0xE4F6; a standing house its player's colour; else water (the

// tile plus its first four sub-tiles, averaged, times height * 0.0019608 + 0.5; then every mine site white.
// `tile_slip`: the original reads the base tile's colour one texture too far (T[tile] for tile id = texture + 1, so
// the last texture is black); ours takes the tile's own colour (docs/bugs.md B14). At k > 1 the rows and columns are
// the cells under each pixel's corner (the original's sum lets rows bleed when H is not a multiple of 200).
// `houses` false: a house cell shows the ground under it, as in the save's preview below.
std::vector<std::uint32_t> minimap_static(const sim::GameState& s, const MinimapPalette& pal, int k,
                                          bool tile_slip = false, bool houses = true);



Minimap save_preview(const sim::GameState& s, const MinimapPalette& pal, bool tile_slip = false);




std::vector<MinimapDot> minimap_dots(const sim::GameState& s, const MinimapPalette& pal, const Selection& sel, int k);



std::vector<MinimapDot> minimap_house_marks(const sim::GameState& s, const Selection& sel, int k);






struct MinimapMark {
    float x = 0, y = 0;  // original minimap pixels
    float frame = 0;
    int loops = 0;
};
void minimap_mark_add(std::vector<MinimapMark>& marks, int map_w, int map_h, std::int32_t cell_x, std::int32_t cell_y);

void minimap_marks_advance(std::vector<MinimapMark>& marks);
// The frames to draw at scale k.
std::vector<MinimapMarkDraw> minimap_marks_draw(const std::vector<MinimapMark>& marks, int k);

}  // namespace antanta::ui
