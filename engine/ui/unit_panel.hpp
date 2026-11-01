// The unit panel of the original (docs/interface.md B.4, slice I4), without SDL: the pass over the selection, the


// change (select or drop a type) or an order for the next tick, the same sim::Command the recorder writes.
//

// the original's button, but a click only says so on the screen and sends nothing, so a trace never diverges silently.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "sim/state.hpp"
#include "ui/hud.hpp"
#include "ui/panel.hpp"

namespace antanta::ui {

// Data/StringTable_eng.st (formats/menu.hpp parse_string_table).
using StringTable = std::map<std::string, std::string>;


struct UnitPanelSummary {
    int count = 0;              // NumSelectedUnits (the units that still exist)
    double hp = 0, max_hp = 0;
    int stars = 0;              // the average stars, sum / count: per unit 5 from 250 experience, else exp * 0.02
    int grenades = 0;
    bool gas_unit = false;      // a mortar man (5), howitzer (21) or cannon (22)
    bool in_group = false;
    bool can_group = false;     // a unit that may join a group (B+9) and is in none
    bool fire_held = false;     // a unit with u+6 == 0
    bool may_fire = false;      // a unit with u+6 != 0
    int passengers = 0;
    bool one_mode = false;
    std::uint8_t mode = 0;
    struct TypeCount {
        std::uint16_t type = 0;
        int count = 0;
    };
    std::vector<TypeCount> types;  // in order of first appearance, at most 20
    std::uint8_t nation = 0;       // the player's nation
    std::uint8_t move_class = 0;   // of the first type
    bool ship = false;             // the first type is a ship
};
UnitPanelSummary unit_panel_summary(const sim::GameState& s, const Selection& sel);


int unit_stars(float experience);

// The type icons' grid (B.4): cols = n for n <= 5, 5 for n <= 14, else n / 3 + 1; step 150 / cols; icon k at
// (5 + (k % cols) * step, 7 + (k / cols) * 43).
IRect type_icon_rect(int n, int k);
// The icon's entry id in Icons/UnitsIcons.les (B.3): the type, except 14, 16 and 23: nation * 100 + type.
std::uint16_t unit_icon_id(std::uint8_t nation, std::uint16_t type);

// Fire at a point is offered for a selection of one type whose MoveClass is 5, 8, 3, 9 or 10 or which is a ship, but
// not the motor vehicle (24), the transport (17) or England's light tank (nation 1, type 11).
bool can_fire_at_point(const UnitPanelSummary& u);

// The actions of the unit panel's buttons (PanelWidget::action).
enum UnitPanelAction : int {
    kUpSelectType = 1,  // arg: type (left click; with Ctrl it drops the type)
    kUpDropType,        // arg: type (right click)
    kUpStop,
    kUpNoFire,
    kUpMayFire,
    kUpSetMode,  // arg: the mode 1, 2, 3, 0xff, 4
    kUpFirePoint,
    kUpFireGas,
    kUpGroupCreate,
    kUpGroupDismiss,
    kUpUnload,
    kUpBuild,  // arg: the house type (the worker build menu, ui/build_menu.hpp, slice I5)
};

// The unit panel for the selection (empty widgets when nothing is selected): panel window 610 x 200 at (10, H - 208).
// `strings`: the string table (missing keys show the key).
Panel unit_panel(const sim::GameState& s, const Selection& sel, const StringTable& strings);




struct UnitPanelResult {
    std::vector<sim::Command> order;
    std::string what;      // for the log
    std::string unported;  // set: tell the player this order is not in the new engine yet
    int build = -1;
    int fire_point = -1;   // fire at point (0) or gas (1): cursor mode 5 with that gas flag (ui/fire_point.hpp)
};
UnitPanelResult unit_panel_click(const sim::GameState& s, Selection& sel, PanelClick click, bool ctrl);

// The names the panel shows: IDS_UNIT_NAME_<NATION>_<type> and MPS_<NATION>_<n> (formats.md BasicParams).
std::string unit_type_name(const StringTable& strings, std::uint8_t nation, std::uint16_t type);
std::string nation_label(const StringTable& strings, std::uint8_t nation);
// Entente Engine Plus, supply with teeth (docs/plus-balance.md 3 E1 and 11), ours and only under plus_rules; empty
// under Classic and for a unit type of `nation` whose upkeep has no iron, gold or oil (only those slow a reload):
// - plus_upkeep: the upkeep a minute at the normal speed (UnitType::upkeep a tick x 60,000 / GameState::speed_ms,
//   rounded, the zeros left out) in the order gold, food, iron, oil, wood, as {name, amount} with the game's resource
//   names as they are (IDS_GOLD " Gold " and the others; the unit panel's lines trim them);
// - plus_upkeep_text: those as a tooltip's tail, as train_tooltip's costs: ". <plus.upkeep>:" and "<name> (n), " per
//   resource, the last two characters cut;
// - plus_reload_slow: the player's stock of a resource the type's upkeep uses is below 1, so its reload is slow (the
//   sim's Unit_ReloadHeld without its every-other-tick step); the panels say so with our plus.reload_slow and
//   plus.reload_slow_why ("Reloading slowly", "no iron, gold or oil"), plus_reload_hint joins them with ": ".
std::vector<std::pair<std::string, int>> plus_upkeep(const sim::GameState& s, std::uint8_t nation, std::uint16_t type,
                                                     const StringTable& strings);
std::string plus_upkeep_text(const sim::GameState& s, std::uint8_t nation, std::uint16_t type,
                             const StringTable& strings);
bool plus_reload_slow(const sim::GameState& s, std::uint8_t player, std::uint8_t nation, std::uint16_t type);
std::string plus_reload_hint(const sim::GameState& s, std::uint8_t player, std::uint8_t nation, std::uint16_t type,
                             const StringTable& strings);




std::string unit_info_line(const sim::GameState& s, const StringTable& strings, std::uint8_t me, const sim::UnitRef& r);




std::string house_info_line(const sim::GameState& s, const StringTable& strings, std::uint8_t me,
                            const sim::HouseRef& r);

}  // namespace antanta::ui
