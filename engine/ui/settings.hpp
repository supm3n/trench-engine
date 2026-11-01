// Our own settings file (docs/frontend.md 5 and 9): the values of the original's Data/GameInf.txt that the Options
// and Game options screens change, kept in %APPDATA%\Antanta\settings.txt so the game folder is never written. The
// first start takes them from the game's GameInf.txt. No SDL here; the file is read and written by the app.
//
// Also the arithmetic of the Options screen's sliders, copied from the original so a value survives Options -> Ok
// exactly as it does there.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/gameinf.hpp"

namespace antanta::ui {

struct Settings {
    bool music_on = true;
    std::uint8_t music_volume = 93;
    bool sound_on = true;
    std::uint8_t sound_volume = 96;
    // The Options picture picker: 0..4 = 1024x768, 1152x864, 1280x960, 1280x1024, 1600x1200. In the original it is the
    // screen mode; here it is the window size (the menus are drawn on the 1024 x 768 canvas, letterboxed).
    std::uint8_t resolution = 0;
    bool fullscreen = false;     // ours: Alt+Enter and the picker's Full screen (resolution keeps the window's size)
    std::uint8_t speed_ms = 30;  // milliseconds per tick; the Options slider offers 30..90
    bool hints = false;
    std::uint8_t mouse_scroll = 95;      // 1..100
    std::uint8_t keyboard_scroll = 100;  // 1..100
    bool landscape_filter = true;
    bool short_anims = true;
    bool reflections = true;
    // Ours, the interface at high resolutions (PLAN.md 2026-10-15; Game options, docs/interface.md I1): the interface
    // scale (0 Auto, a whole number for the window, ui/iface_layout.hpp; 1..4 by hand), how its pictures are scaled
    // (0 "in between": whole multiples with softened edges, 1 crisp pixels, 2 smooth) and the world zoom of the game
    // view (0 Auto: the interface scale; 1..4 by hand). Text is always drawn from the font at full resolution.
    std::uint8_t ui_scale = 0;
    std::uint8_t ui_pictures = 0;
    std::uint8_t world_zoom = 0;
    // Ours (docs/download.md "The updater"): the release exe looks for a new version at every start. Game options
    // shows it only where the updater is built in (frontend_set_update_link).
    bool check_updates = true;
    // Ours, the host's network game settings (Game options from the menu, docs/multiplayer.md "The network settings
    // page"): every player's game autosaves every net_autosave minutes (1..15); a dropped player's seat is kept
    // net_seat_hold minutes (0: as long as the game runs); the computer level that plays a player who left or dropped
    // (0 nobody, 1 Easy, 2 Medium, 3 Difficult). The host's server takes them (net::NetConfig autosave_minutes,
    // seat_hold_s, ai_level); --net-set overrides them.
    std::uint8_t net_autosave = 5;
    std::uint8_t net_seat_hold = 5;
    std::uint8_t net_ai_level = 2;
    // Ours (docs/sim-thread.md): the simulation on a thread of its own beside the drawing, 0 off, 1 on, 2 auto (on from
    // 4 logical processors); --sim-thread overrides it, Game options shows it. Auto since slice S6 measured it (the
    // owner's decision of 2026-10-20); a file without the line gets auto too.
    std::uint8_t sim_thread = 2;
    // Ours (docs/languages.md 4): the display language, a folder name of lang/ ("en", "de"). Empty: follow the system's
    // language (SDL_GetPreferredLocales, in the browser navigator.languages), English when there is no folder for it.
    // Game options sets it; --lang overrides it for one run.
    std::string language;
    // Ours (docs/your-version.md 0): the skirmish screen's Rules row as chosen last, so the next start (and the browser
    // page) offers it again; written only when it is Entente Engine Plus.
    bool rules_plus = false;
    // Ours: the desktop window as the player left it (window_place below): its position and size when it was last a
    // plain window (not maximised, not full screen), and whether it was maximised. window_w 0: none yet, the window
    // opens at the Options resolution's size, centred. Only the windowed menu run reads and writes them.
    int window_x = 0, window_y = 0, window_w = 0, window_h = 0;
    bool window_maximized = false;



    std::array<bool, 20> economic{true, true, true, true, true, true, true, true, true, true,
                                  true, true, true, true, true, true, true, true, true, true};
    std::array<bool, 20> military{true, true, true, true, true, true, true, true, true, true,
                                  true, true, true, true, true, true, true, true, true, true};
};

Settings settings_from_gameinf(const GameInf& g);

// The file: one "name value" line per setting, '#' starts a comment line. Every setting must be present once. The
// advisor lines hold 20 switches; a file written before 2026-10-15 evening has 6 economic and 4 military, read with
// the others on. The three interface lines (ui_scale, ui_pictures, world_zoom), the three network lines
// (net_autosave, net_seat_hold, net_ai_level), sim_thread and the window lines (window_x, window_y, window_w,
// window_h, window_maximized) are newer: a file without them gets the defaults.
// "language <code>" is written only when a language was chosen; without it the game follows the system.
std::string settings_print(const Settings& s);
std::optional<Settings> settings_parse(std::string_view text, std::string* error);

// Window size for a resolution index (0..4).
struct Size {
    int w = 0, h = 0;
};
Size resolution_size(std::uint8_t resolution);

// Ours: where the desktop window opens (no SDL here; the app asks SDL for the displays). A rectangle in screen
// pixels; for the window it is the client area, as SDL_GetWindowPosition and SDL_GetWindowSize give it.
struct WindowRect {
    int x = 0, y = 0, w = 0, h = 0;
};
// `r` moved and shrunk into `usable` (a display's usable bounds), with `title_h` kept free above it for the title bar.
WindowRect window_clamp(WindowRect r, const WindowRect& usable, int title_h);
struct WindowPlace {
    WindowRect r;
    bool maximized = false;
};
// The remembered window (Settings::window_*) when the middle of its top edge is on one of `usable` (the displays'
// usable bounds, the primary first), clamped to that display; else the remembered size (or, with none, the Options
// resolution's) centred on the primary display and clamped to it. No display at all: the size at (0, 0).
WindowPlace window_place(const Settings& s, const std::vector<WindowRect>& usable, int title_h);


float volume_to_slider(std::uint8_t volume);



std::uint8_t volume_from_slider(float slider);

float speed_to_slider(std::uint8_t ms);


std::uint8_t speed_from_slider(float slider);


std::uint8_t scroll_from_slider(float slider);



float slider_value_at(int mouse_x, int x, int w);
// The SDL key code a Menu.txt hot key (virtual-key code) matches: letters are the lower case SDL keys.
int hotkey_sdl_key(std::uint8_t vk);
int slider_knob_x(float value, int x, int w, int knob_w);

}  // namespace antanta::ui
