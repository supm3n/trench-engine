// The debug HUD's logic (milestone 3, docs/architecture.md "ui/"): selection, picking, and the orders a click or a
// key makes. No SDL and no drawing here; nothing writes the GameState. Every order is a sim::Command for the next
// tick, the same struct and O line text the recorder writes for the original, so a session played here can be
// replayed by antanta-headless and later compared with the original.
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "render/iso.hpp"
#include "sim/state.hpp"

namespace antanta::ui {






struct Selection {
    std::uint8_t player = 0;
    std::vector<sim::UnitRef> units;
    std::vector<sim::HouseRef> flagged;
    std::optional<sim::HouseRef> house;


    std::uint32_t ring_resets = 0;
};

void select_unit(Selection& sel, const sim::UnitRef& u);
// select_unit for each unit of `list` in turn: the new ones end up in front in reverse list order, then the units
// selected before. In linear time (docs/bugs.md B65).
void select_units(Selection& sel, std::span<const sim::UnitRef> list);


bool toggle_unit(Selection& sel, const sim::UnitRef& u);

void clear_selection(Selection& sel);

// The world pixel under a screen pixel of the 1024 x 768 view (flat: the view offset added, no height).
struct WorldPoint {
    std::int32_t x = 0, y = 0;
};
WorldPoint screen_to_world(ViewPos pos, int sx, int sy);



bool pick_cell(const sim::GameState& s, std::int32_t x, std::int32_t y, std::int32_t* cx, std::int32_t* cy);





struct UnitBox {
    std::int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
UnitBox unit_click_box(const sim::GameState& s, const WorldGraphics* g, std::uint8_t player, const sim::Unit& u);






std::optional<sim::UnitRef> unit_at(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, int sx, int sy,
                                    int player);



std::optional<sim::UnitRef> enemy_at(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, int sx, int sy,
                                     std::uint8_t player);




std::optional<sim::UnitRef> info_unit_at(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, int sx, int sy,
                                         std::uint8_t player);
// Ours (docs/bugs.md B312): another player's aircraft whose click box holds screen point (sx, sy), the lowest on the
// screen, not in a hangar or transport, not in fog (B25). The click select lets it win over your own ground units
// under it, since the plane is drawn on top (owner, 2026-10-22).
std::optional<sim::UnitRef> other_aircraft_at(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, int sx,
                                              int sy, std::uint8_t player);


bool point_in_fog(const sim::GameState& s, ViewPos pos, int sx, int sy);
// The live units of `player` whose foot point is inside the screen rectangle, in the player's list order.
std::vector<sim::UnitRef> units_in_rect(const sim::GameState& s, ViewPos pos, int x0, int y0, int x1, int y1,
                                        std::uint8_t player);
// How far (pixels) a click may miss a house's drawn picture and still pick it: fills the small holes and rims.
inline constexpr int kHousePickReach = 3;
// A house (not a removed ruin) whose picture has an opaque pixel at the screen point; of several, the front one.
// g: the loaded house pictures (render/iso); null or not loaded: the THouseBasic sprite rectangle, lower 85 %.
std::optional<sim::HouseRef> house_at(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, int sx, int sy);
// The picture rectangle of a house on screen: x, y, w, h.
std::array<int, 4> house_rect(const sim::GameState& s, const WorldGraphics* g, ViewPos pos, const sim::House& h,
                              std::uint8_t player);

// After a step: drops units that no longer exist (dead and gone, inside a house) or that the step took out of the
// selection (GameState::deselected), flagged houses that were destroyed, and a removed ruin from the panel.
void selection_prune(const sim::GameState& s, Selection& sel);

// What a right click of `player` at screen point (sx, sy) points at, as right_click classifies it (the cursor uses the
// same, ui/cursor.hpp): the object of the picked cell (a house of any player), else an enemy unit drawn there
// (unit_at), else the picture of a house (house_at), moved to a point of its footprint cell. w, cx, cy: the point and
// its cell after that move; obj: the cell's object.
struct RightClickTarget {
    bool on_map = false;
    WorldPoint w;
    std::int32_t cx = -1, cy = -1;
    std::uint16_t obj = 0;
    std::optional<sim::HouseRef> house;
    std::optional<sim::UnitRef> target;
    // An enemy house or unit stood at the point but its cell is hidden in fog (the cursor's test): house and target
    // are then left empty, so the click is a move (docs/bugs.md B24, deliberately unlike the original).
    bool hidden_enemy = false;
};
RightClickTarget right_click_target(const sim::GameState& s, const WorldGraphics* g, std::uint8_t player, ViewPos pos,
                                    int sx, int sy);

// A right click at screen point (sx, sy) with the selection: one order (the top-level command first, then the

// is nothing to order. `what` gets a short description for the HUD. The kind follows the original's order: the
// object of the picked cell, then an enemy unit under the cursor, then the ground.
//   own house       ui_order + enter_house per unit (kind 1: repair, or enter)
//   enemy house     ui_order + attack_house per unit (kind 2)
//   enemy unit      ui_order + attack per unit (kind 4)
//   map object      ui_order + order_xy per unit (kind 5, 0x2580..0x25e3: a worker farms a field)
//   ground          ui_order + order_move per unit (kind 0)

std::vector<sim::Command> right_click(const sim::GameState& s, const WorldGraphics* g, const Selection& sel,
                                      ViewPos pos, int sx, int sy, std::string* what);

std::vector<sim::Command> stop_order(const Selection& sel);
std::vector<sim::Command> delete_key(const Selection& sel);


std::vector<sim::Command> build_order(const sim::GameState& s, const Selection& sel, std::int32_t wx, std::int32_t wy,
                                      std::uint16_t type);

std::optional<sim::Command> select_houses(const sim::GameState& s, const Selection& sel);


std::optional<sim::Command> select_summary(const sim::GameState& s, const Selection& sel);
std::vector<sim::Command> train_order(const sim::GameState& s, const Selection& sel, std::uint16_t unit_type);

// The orders queued by the UI (one entry per order: the top-level command, then its children). Takes the orders for
// the next tick from the front of the queue and numbers them: a ui_order gets the sim's next order number (its
// children point at it), so it must be the first order of its tick; a later one waits for the next tick. Other
// orders keep number 0 (the sim numbers them). Every command gets tick s.tick + 1.
std::vector<sim::Command> take_tick_orders(const sim::GameState& s, std::deque<std::vector<sim::Command>>& queue);

// Names for the HUD (consts.lua UNIT_* and HOUSE_*), and the Work codes seen so far.
std::string unit_name(int type);
std::string house_name(int type);
std::string work_name(int work);



bool house_point(const sim::GameState& s, ViewPos pos, std::uint8_t player, std::uint16_t slot, int* sx, int* sy);

// The house types a worker can be asked to build here: present for the nation, not a bridge.
std::vector<std::uint16_t> buildable_houses(const sim::GameState& s, std::uint8_t player);

bool can_place(const sim::GameState& s, std::uint8_t player, std::int32_t cx, std::int32_t cy, std::uint16_t type);



std::vector<std::array<std::int32_t, 2>> site_cells(const sim::GameState& s, std::uint8_t player, std::int32_t cx,
                                                    std::int32_t cy, std::uint16_t type);

}  // namespace antanta::ui
