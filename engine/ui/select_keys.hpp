


#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "render/iso.hpp"
#include "sim/state.hpp"
#include "ui/hud.hpp"
#include "ui/iface_layout.hpp"

namespace antanta::ui {

// ---- the key table ----

// A key as the game reads it (the app turns SDL's into these).
enum class Key : std::uint8_t {
    Other,
    Letter,  // ch: 'a'..'z'
    Digit,   // ch: '0'..'9' (the top row)
    Tab,
    Space,
    Escape,
    Pause,
    Delete,
    Grave,  // the key left of 1 (VK_OEM_3, `~`)
    F1,
    F2,
    F5,
    F6,
    F12,
    KpPlus,
    KpMinus,
    Left,
    Right,
    Up,
    Down,
    Period,
    Minus,      // the main row's - (ours: zoom out)
    Plus,       // the main row's = or + (ours: zoom in)
    Backspace,  // (ours: the Options zoom again)
};

struct KeyPress {
    Key key = Key::Other;
    char ch = 0;
    bool ctrl = false, shift = false;
};

enum class KeyAct : std::uint8_t {
    None,
    ScrollLeft,  // the arrows: one step in batch runs (a window scrolls smoothly while they are held)
    ScrollRight,
    ScrollUp,
    ScrollDown,
    SelectAllMap,
    SelectAllScreen,
    SelectWorkersScreen,
    SelectTypesMap,
    SelectTypesScreen,
    IdleWorkers,
    HouseKey,
    Market,
    Minimap,
    Tab,
    Centre,
    Pause,
    Delete,
    Health,
    SeeThrough,
    Mission,
    QuickSave,
    QuickLoad,
    Escape,               // Esc (WM_CHAR 0x1b)
    GroupSelect,
    GroupBind,
    SpeedUp,              // numpad + (ours: x1, x2, x4; the original's is a debug key)
    SpeedDown,            // numpad -
    DevSwitch,            // F12 (ours): the debug HUD and back
    ZoomIn,               // the main row's = or + (ours, docs/decisions.md 41); not with Ctrl (the browser's)
    ZoomOut,              // the main row's -
    ZoomReset,            // Backspace: the Game options zoom
    // The debug HUD's own keys (developer switch on): they clash with the original's.
    DevPause,    // Space, P
    DevStep,     // N, .
    DevSpeed,    // 1, 2, 3, 4; arg: x1, x2, x4
    DevStop,     // S
    DevBuild,    // B
    DevPlayers,  // Tab
    DevHelp,     // F1, H
    DevEscape,   // Esc: cancel the build, else clear the selection
};

struct KeyAction {
    KeyAct act = KeyAct::None;
    int arg = 0;
    bool repeats = false;  // a held key's repeats do it again
};

// What a key does: the original's table (dev false), or with the developer switch on (the debug HUD) its old keys
// first where they clash. Conditions on the game (a panel shown, a market selected, ...) are the caller's.
KeyAction game_key(const KeyPress& k, bool dev);


int house_key_type(char letter, bool ctrl, bool shift);

// ---- the selections (all as the original's except that each replaces the whole selection, docs/bugs.md B26) ----

// The view's rectangle in world pixels: what "on screen" means for A, Shift+A and Z.
struct ScreenBox {
    std::int32_t x = 0, y = 0, w = 0, h = 0;
};




bool key_selectable(const sim::GameState& s, std::uint8_t player, const sim::Unit& u);

void select_all_map(const sim::GameState& s, Selection& sel);                        // Ctrl+A
void select_all_screen(const sim::GameState& s, Selection& sel, ScreenBox box);      // A
void select_workers_screen(const sim::GameState& s, Selection& sel, ScreenBox box);  // Shift+A
// Ctrl+Z: the units of the selected types on the map are added. Nothing without a selected unit.
void select_types_map(const sim::GameState& s, Selection& sel);
// Z and the double click: the units of the selected types on the screen; keep (Shift): added to the selection.
void select_types_screen(const sim::GameState& s, Selection& sel, ScreenBox box, bool keep);
// Ctrl+F and the city centre's Workers button: the idle workers on the map; false (selection kept) when there are none.
bool select_idle_workers(const sim::GameState& s, Selection& sel);

// ---- control groups ----



std::optional<sim::Command> bind_group_order(const sim::GameState& s, const Selection& sel, int digit);



bool select_group(const sim::GameState& s, Selection& sel, int digit, bool keep);

// ---- Tab, Space, the house keys ----



bool tab_key(const sim::GameState& s, Selection& sel, int* index, bool back);


std::optional<std::array<std::int32_t, 2>> centre_cell(const sim::GameState& s, const Selection& sel, int* index);


ViewPos centre_view(ViewCells cells, std::int32_t x, std::int32_t y);
// The house keys: the player's first standing house of the type, selected alone. False when there is none.
bool select_house_type(const sim::GameState& s, Selection& sel, int type);

}  // namespace antanta::ui
