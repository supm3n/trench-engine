


#pragma once

#include <cstdint>

#include "ui/hud.hpp"

namespace antanta::ui {


enum class Cursor : std::uint8_t {
    Default = 0,      // Cursor_Default.bmp, the hand
    Patrol = 1,       // Cursor_Patrol.bmp, never set by the original
    Build = 2,        // Cursor_Build.bmp, the trowel: build or repair an own house
    SetExit = 3,      // Way_point2.bmp, the white flag: the house panel's Set_Exit, until the next left press
    Attack = 4,       // Cursor_Attack.bmp, the sabre
    AttackPoint = 5,  // Cursor_AttackPoint.bmp, fire at a point, until the right click
    Food = 6,         // Cursor_Food.bmp, the sickle: farm a field
    GoIntoHouse = 7,  // Cursor_GoIntoHouse.bmp, the yellow arrow: enter an own house
};
inline constexpr int kCursorCount = 8;

const char* cursor_file(Cursor c);

// What the cursor's choice needs to know about the point under the mouse and the selection.
struct CursorFacts {
    enum class Target : std::uint8_t { Ground, OwnHouse, EnemyHouse, EnemyUnit, Field };
    Target target = Target::Ground;
    bool fogged = false;
    bool units = false;      // units are selected
    bool workers = false;    // a selected unit has MoveClass 6 (TStrateg SelHasClass6)
    bool can_enter = false;  // a selected unit may go into a house: its type's +6 byte, or MoveClass 6
    // The house of OwnHouse and EnemyHouse:
    bool destroyed = false;   // THouse IsDestroyed
    bool unfinished = false;
    bool damaged = false;     // HP below MaxHP
    bool room = false;        // fewer units inside than places
};

// The per-frame state around the choice.
struct CursorState {
    Cursor kept = Cursor::Default;  // a mode that lasts between frames: SetExit or AttackPoint, else Default
    bool paused = false;
    bool over_hud = false;
    bool building = false;          // a house type is in the build cursor: a right click only cancels it
};






Cursor cursor_choice(const CursorState& st, const CursorFacts& f);

// The facts for screen point (sx, sy) of the view: the target from right_click_target (the same hit tests and order as
// the right click), the fog at the picked cell, the selection's units.
CursorFacts cursor_facts(const sim::GameState& s, const WorldGraphics* g, const Selection& sel, ViewPos pos, int sx,
                         int sy);

}  // namespace antanta::ui
