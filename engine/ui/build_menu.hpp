// The worker build menu and the build ghost of the original (docs/interface.md B.5 and D.2, slice I5), without SDL:


// house's picture goes under the cursor, whether it is refused (red), and the barn's field cells. The rules
// themselves are the sim's (House_CanPlace, House_CanBuild); nothing here decides a placement.
//
// The pontoon bridge (build type 0xfffe, docs/transports.md 19): two presses on the map, the second sends act_6cf80

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sim/movement.hpp"
#include "sim/state.hpp"
#include "ui/lang.hpp"
#include "ui/panel.hpp"
#include "ui/unit_panel.hpp"

namespace antanta::ui {

inline constexpr std::uint16_t kPontoonType = 0xfffe;



IRect build_button_rect(std::uint16_t type);

char build_key(std::uint16_t type);

// The house name: IDS_HOUSE_NAME_<NATION>_<type> (formats.md BasicParams).
std::string house_type_name(const StringTable& strings, std::uint8_t nation, std::uint16_t type);





std::string build_tooltip(const sim::GameState& s, std::uint8_t player, std::uint16_t type, const StringTable& strings);


bool shows_build_menu(const UnitPanelSummary& u);



void build_menu_add(Panel& p, const sim::GameState& s, std::uint8_t player, const StringTable& strings);
// The tooltip of the pontoon button: IDS_PONTON.
std::string pontoon_tooltip(const StringTable& strings);





sim::PontoonPlan pontoon_preview(const sim::GameState& s, std::uint8_t player, std::int32_t x0, std::int32_t y0,
                                 std::int32_t cx, std::int32_t cy);
// The event message that says why the second press laid nothing (empty when it lays the line), in the display language.
std::string pontoon_refusal_text(const Language& l, const sim::PontoonPlan& plan);

sim::Command pontoon_order(std::uint8_t player, std::int32_t x0, std::int32_t y0, std::int32_t cx, std::int32_t cy);


struct BuildGhost {
    bool refused = false;  // CanPlaceHouse refuses the cell, or (outside the editor) the player cannot pay: red



    int x = 0, y = 0, w = 0, h = 0;



    // fields, in the order the original draws them, with each sprite's centre bottom in world pixels (the cell's world
    // point + (66, 26), where render/ground_fx draws the planted field; the original's ghost: + (66, 24)).
    // Ours (B27): only the cells PlantFields will plant once the barn stands there; the original also shows fields
    // on the barn's own footprint, its door strip and the map's edge band, which never get any.
    std::uint16_t field_object = 0;
    struct Field {
        std::int32_t cx = 0, cy = 0;
        int x = 0, y = 0;
    };
    std::vector<Field> fields;
};
BuildGhost build_ghost(const sim::GameState& s, std::uint8_t player, std::uint16_t type, std::int32_t cx,
                       std::int32_t cy);




struct FitCells {
    std::uint8_t player = 0xff;
    std::uint16_t type = 0xffff;
    std::uint32_t tick = 0;
    std::int32_t x0 = 0, y0 = 0, x1 = -1, y1 = -1;  // the cached rectangle, inclusive; empty when x1 < x0
    std::vector<std::uint8_t> fit;                  // (x1 - x0 + 1) * (y1 - y0 + 1) answers, row by row
    bool fits(std::int32_t cx, std::int32_t cy) const;
};
// Makes `c` hold the answers for the cells (x0..x1, y0..y1) (clamped to the map) of `type` for `player`; true when it
// asked CanPlaceHouse again.
bool fit_cells_update(FitCells& c, const sim::GameState& s, std::uint8_t player, std::uint16_t type, std::int32_t x0,
                      std::int32_t y0, std::int32_t x1, std::int32_t y1);





std::uint32_t ghost_colour(const BuildGhost& g);

}  // namespace antanta::ui
