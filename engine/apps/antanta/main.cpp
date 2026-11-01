// antanta: the new engine's first playable build (milestone 3 debug HUD, docs/architecture.md "apps/"). Loads an
// original save, runs the simulation live with step() at the game's tick (Data/GameInf.txt, 30 ms shipped), draws
// the GameState with the CPU compositor (render/live) and takes orders through a plain HUD (ui/hud). Every order
// is a sim::Command, the struct and O line text the recorder writes for the original.
//
// Desktop: antanta [save.svg] [--game <game folder>] [--commands-out <file>] [--trace <file>]
//                  [--script <file>] [--frames <ticks>] [--png <file>] [--size <W>x<H>] [--speed 1|2|4] [--paused]
//   The save defaults to the project's out/saves/Test1_2026-10-13.svg (developer builds; the release exe opens the
//   main menu, docs/release.md). The game folder: --game, else ANTANTA_GAME_DIR, else the exe's own folder, the Steam
//   installs, the folder remembered in %APPDATA%\EntenteEngine\game_folder.txt, and in a run with a window the folder
//   picker (platform/game_folder.hpp); batch, PNG and bench runs never show the picker.
//   --commands-out: the commands applied before each tick (O lines of the orders, D lines of the view inputs), the
//                   input antanta-headless --orders replays.
//   --trace:        the trace antanta-headless writes for the same commands (H, R, then O/D/C/T/P/U/B/G per tick).
//   --trace-hashes: with --trace, only the G line (the state hash) of each tick after H and R: the soak's long runs on
//                   the huge saves, whose whole trace is too big (docs/sim-thread.md 6, harness 4).
//   --script:       timed input, one event per line: `<tick> <event> <args>`, tick absolute or `+n` from the
//                   save's tick; events at tick t run once the game has reached tick t, before the next step.
//                   Events: camera <col> <row> | click <x> <y> [shift] | drag <x0> <y0> <x1> <y1> |
//                   rclick <x> <y> | rdrag <x0> <y0> <x1> <y1> (a right drag: the order at the press point, then the
//                   facing of the drag, docs/interface.md D.1) | rhold <x0> <y0> <x1> <y1> (the right button still
//                   held: the facing arrow) | mouse <x> <y> | build <house type> | press <x> <y> [shift] |
//                   release <x> <y> (the left button held between them; with a build type a moving mouse may place
//                   more houses, build_hold) | click_unit <player> <id> [shift] |
//                   rclick_unit <player> <id> | click_house <player> <slot> [shift] | rclick_house <player> <slot> |
//                   key <SDL key name> | train <unit type> | order <O line, fields by spaces> |
//                   message <text> (an event message of the screen text list, docs/interface.md A.7) |
//                   hold <x0> <y0> <x1> <y1> (a box drag not released yet: the live selection, D.1) |
//                   mark <cell x> <cell y> (a battle mark on the minimap, A.6) |
//                   select <player> <id> ... (the recorder's selection step: these units, head first) |
//                   select_house <player> <slot> (that house selected, as a click on it; slice I6) |
//                   select_house_type <player> <type> (the first standing house of that type, as the house keys) |
//                   zoom <factor> (the world zoom at once, about the screen's centre, within the wheel's limits). A
//                   click or key on the selection panel presses its button (slice I4); `click <x> <y> ctrl` holds Ctrl,
//                   `shift` Shift (both may follow the point, in any order; also for `rclick`). `ctrl down|up` holds

//                   Coordinates are world pixels of the view (the 1024 x 768 view of the debug HUD, or the view that
//                   covers --size divided by the world zoom). An order line with a parent (its 4th field) is a child
//                   of the order line before it and goes into the same step.
//   --frames n:     batch mode without a window: runs n ticks as fast as possible (the script drives the input),
//                   then draws one frame with the interface into --png (size --size, default 1024x768) and quits. A
//                   game that ends on the way stops there (the pause) and the frame shows the end message over the
//                   view.
//   --end-statistics <s>: batch mode, a game that ended: Enter on the end message, then the statistics screen into
//                   --png, with a fixed clock that reads <s> seconds after the load when the screen opens (the play
//                   time of the living players; the live game uses the real clock).
//   --bench-scroll <s>: batch mode, before the PNG: frame times of <s> seconds of scrolling, the old whole-step way
//                   and the smooth camera (bench_scroll; docs/interface.md D.5).
//   --bench-frame <s>: window mode, a hidden window without vsync: <s> seconds of the running game scrolling at full
//                   edge speed, the whole frame timed in parts (ticks, world picture, upload, interface, present);
//                   prints the times and quits (docs/interface.md D.5). With --png: the 60th frame's picture as the
//                   window's renderer drew it (the zoom's pixel-art filter, which the batch's software renderer lacks).
//   --ui-scale auto|1..4, --ui-pictures between|crisp|smooth, --zoom auto|1..4: the interface settings for this run
//                   (batch runs use these and the defaults, never the settings file); --debug-hud: the debug HUD.
//   --lang <code>:  the display language for this run (docs/languages.md 4: "en", a folder of lang/ in the user
//                   folder, or "xx", the pseudo-language whose marker check logs every drawn text that did not come
//                   through the lookup, or "xl", the same 40 % longer with long glued words, for the fitting of
//                   docs/languages.md 2.5). Without it: the setting, else the system's language, else English; batch
//                   runs take English. --lang-setting <code>: chooses and stores the language as Game options does (the
//                   browser's start page passes it). Only the interface changes: traces and hashes stay the same.
//   --display-font <file>: another TrueType font for the translated tile headings instead of the built-in one (the
//                   samples for the font choice, docs/languages.md 9 item 2).
//   The game view covers the window with the original's interface (docs/interface.md part A); F12 switches to the
//   debug HUD of milestone 3 (the 1024 x 768 view with its panels) and back.
// Network game (docs/multiplayer.md, slice MP1): antanta --host <port> --net-skirmish <choices> [--net-name <name>]
//   hosts a game (the server in this process, WebSocket on every interface) on the setup "players=3;resources=1;
//   map=Aust_3r_3.dat;slots=4/0/0/1,4/1/1/2,1/2/2/3;seed=7" (level 4 or 5: a human slot; the host takes the first);
//   antanta --join <host:port> [--net-name <name>] joins the first game there. The game starts once every human slot
//   is taken. --net-set <name>=<value>: a number of net/config.hpp for the host's server (tick_ms, input_delay, ...).
//   With --frames n: batch mode, the game runs to tick n (--script with `me` for your player in select,
//   select_house(_type), click_unit and rclick_unit), --trace writes the network game's trace (net::NetTrace_Tick, the
//   same on every machine), --net-timeout <s> (default 120) ends a run that hangs. Slice MP2: --net-password <p> the
//   game's password (the host sets it, a joiner gives it), --net-bind <address> where the host listens (default
//   0.0.0.0, every interface; the tests take 127.0.0.1), --net-log-dir <dir> the host's server writes each game's order
//   log there as antanta-server --log-dir does (antanta-headless --net-log replays it). From the menu (the Network game
//   button, slice MP2) the same game is hosted and joined on the pages of Multiplay.les; the games on this network are
//   found over UDP on --net-lan-port <port> (default 47624). antanta --menu --net-menu-script <file> --user-dir <dir>
//   [--net-bind 127.0.0.1] --frames n [--script ...] [--trace ...] goes through the menu by the file's lines (click x
//   y, key <SDL key name> [times], text <chars>, setup <skirmish choices>, wait lan|lobby <n>, wait game), then plays
//   as
//   --frames does with --host or --join (tests\menu_net_host.txt, menu_net_join.txt). Slice MP8: --join <host:port>
//   with --net-skirmish creates that game on the server there (antanta-server) instead of joining one; a script's
//   `click_site` is the build click on the free cell nearest the first selected unit. Slice MP9: --host or --join with
//   --net-resume <our own save of a network game> resumes that game (the seats of the save; each player comes back to
//   his by the token kept in network-seats.txt of the user folder); --join to a game we had a seat in takes it back
//   (a started game: the rejoin after a drop); --net-drop-at <tick> (batch) drops the link there and ends the run, as a
//   crash would. Every player's game writes the autosaves to <saves>/network (the last three of a game). The menu's
//   page 2 lists our saved network games (Join hosts one again) and the games we had a seat in; the net menu script's
//   `png <file>` writes a picture of the pages as they are.
// Front-end: antanta --menu [--game <game folder>] opens the main menu (docs/frontend.md, slice FE1). Settings are
//   read from and written to %APPDATA%\EntenteEngine\settings.txt; the first start takes them from Data/GameInf.txt.
//   --menu --screen main|options|game_options|network|profiles|single_player|skirmish --png <file> [--size <W>x<H>]:
//   draws that screen without a window into the PNG and quits (the settings then come from the game's GameInf.txt,
//   the profiles from its Save/splayers.dat, nothing is written). "skirmish:players=4;resources=2;map=Aust_3r_4.dat;
//   slots=4/0/0/0,3/1/1/0" sets the choices of the setup screen (level/nation/colour/team per row).
//   Profiles are read from and written to %APPDATA%\EntenteEngine\profiles.txt (the first start reads the game's
//   Save/splayers.dat, which is never written). --seed <n>: every skirmish started from the menu uses that seed
//   (tests); without it each game gets a random one.
//   Missions on the single player menu (slice M4, docs/frontend.md 4.9): the list of Data/Campaigns/Missions/*.mis;
//   Load starts the selected one (sim::GameState_NewMission, as antanta-headless --new-mission). A won mission is kept
//   in %APPDATA%\EntenteEngine\progress.txt (the game's Save/<profile>.dat is read once, never written). --user-dir
//   <folder> takes the settings, profiles, progress and saves from that folder instead (tests; batch runs then also
//   store the progress). --menu --screen missions[:pick=<row>] draws the list; with --start, Load on it starts that
//   mission in batch mode as skirmish --start does. Script `mission_message [end won|lost] <text>`: a game message
//   window (a test tool; the real ones come from the mission events of slice M3). Campaigns on the single player menu
//   (slice M7, docs/frontend.md 4.8): the campaign select (Data/Campaigns/*.cmp, then *.cmt), its "ok" the campaign's
//   mission list (locked missions left out, won ones in salmon); after a campaign's mission the statistics (or Return
//   to Menu) lead back to that list. --menu --screen campaigns[:pick=<row>] draws the campaign select,
//   campaign_missions[:pick=<row>] that campaign's mission list; with
//   --start, "ok" and Load start the selected mission in batch mode. The Lua missions (.mit) run their script (M5).
//   Videos (docs/sound.md section 5): with --menu in a window, Buka and the intro play before the menu and the credits
//   on the Medal button, on Windows from the game's own Media/*.wmv (Media Foundation), else from the converted files
//   in <repo>/out/videos (--videos <folder>); Esc skips one. --video 0 (or +video 0) turns them
//   off. A missing folder or file is logged and skipped. Batch runs never play them.
//   Updates (docs/INSTALL.md "Updating"): on Windows the menu asks GitHub which release of Entente Engine is the
//   newest, at every start (unless Game options' "Check for updates" is off) and on the menu's "Check for updates".
//   When a newer one is out, a box says so and its button opens the release page in the browser. Nothing is ever
//   downloaded or installed by the program (platform/release_check.hpp). Batch runs never check.
//   Our own menu texts come in English, German, Italian and Russian (engine/lang/); this build downloads no
//   language pack for the game's own texts (briefings, hints, the museum).
//   Start on the setup screen builds the game (sim::GameState_NewSkirmish) and enters the game view with the same
//   HUD as a save. --menu --screen skirmish:<choices> --start presses Start on that screen and goes on in batch mode
//   (--frames, --png, --trace, --commands-out, --script as above): the test that a game started from the menu
//   gives the trace of antanta-headless --new-skirmish with the same choices and seed.
//   --plus: every new skirmish this run starts (the setup screen's Start, the in-game Restart) uses Entente Engine
//   Plus (SkirmishSetup::plus, docs/your-version.md), as antanta-headless --plus, whatever the setup screen's rule set
//   box says; a network host's choices take "plus=1" instead. A save keeps the rules it was played with.
// Web (docs/web.md): the page writes the game's files into /game/ (lower-case paths) and keeps /libsdl/ (our
//      settings, profiles and saves) in the browser's storage, then starts the app: the front-end as with --menu
//      (no videos), or with a save path as its argument that save (--bench-frame works there too). --frame-log: every
//      5 seconds the mean and worst time of a frame in the browser's console.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "apps/antanta/sim_host.hpp"
#include "apps/common/net_game.hpp"
#include "apps/common/net_store.hpp"
#include "apps/common/view_input.hpp"
#include "formats/campaign.hpp"
#include "formats/gameinf.hpp"
#include "formats/map.hpp"
#include "formats/menu.hpp"
#include "formats/mis.hpp"
#include "formats/museum.hpp"
#include "formats/params.hpp"
#include "formats/svg.hpp"
#include "net/config.hpp"
#include "net/lockstep.hpp"
#include "net/server.hpp"
#include "net/transport.hpp"
#include "platform/audio.hpp"
#include "platform/files.hpp"
#include "platform/game_folder.hpp"
#include "platform/session_log.hpp"
#include "render/bridges.hpp"
#include "render/effects_emit.hpp"
#include "render/iso.hpp"
#include "render/live.hpp"
#include "render/raster.hpp"
#include "script/lua_manager.hpp"
#include "script/lua_save.hpp"
#include "sim/endgame.hpp"
#include "sim/mission.hpp"
#include "sim/ownsave.hpp"
#include "sim/skirmish.hpp"
#include "sim/state.hpp"
#include "sim/strike.hpp"
#include "sim/trace.hpp"
#include "ui/build_ghost_draw.hpp"
#include "ui/build_menu.hpp"
#include "ui/camera.hpp"
#include "ui/cursor_draw.hpp"
#include "ui/facing.hpp"
#include "ui/fire_point.hpp"
#include "ui/fog_draw.hpp"
#include "ui/frontend.hpp"
#include "ui/game_menu.hpp"
#include "ui/game_windows.hpp"
#include "ui/house_panel.hpp"
#include "ui/hud.hpp"
#include "ui/iface_draw.hpp"
#include "ui/iface_layout.hpp"
#include "ui/lang.hpp"
#include "ui/minimap_draw.hpp"
#include "ui/mission_list.hpp"
#include "ui/panel_draw.hpp"
#include "ui/play_clock.hpp"
#include "ui/progress.hpp"
#include "ui/rally_flag_draw.hpp"
#include "ui/select_keys.hpp"
#include "ui/selection_rings.hpp"
#include "ui/settings.hpp"
#include "ui/sound.hpp"
#include "ui/unit_panel.hpp"
#include "ui/video_host.hpp"
#include "ui/world_marks_draw.hpp"
#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#include <emscripten/heap.h>
#else
#include "net/lan.hpp"
#endif
#ifdef ANTANTA_UPDATER
#include "platform/lang_pack_win.hpp"
#include "platform/private_hosts.hpp"
#include "platform/update_key.hpp"
#include "platform/update_win.hpp"
#endif
#ifdef ANTANTA_RELEASE_CHECK
#include "platform/release_check_win.hpp"
#endif

using namespace antanta;

#ifdef __EMSCRIPTEN__
// The page's full screen (apply_fullscreen): asked for and left by the page, not by SDL's canvas mode.
EM_JS(void, web_fullscreen, (int on), {
    if (on && !document.fullscreenElement) document.documentElement.requestFullscreen().catch(function(){});
    if (!on && document.fullscreenElement) document.exitFullscreen().catch(function(){});
});
EM_JS(int, web_is_fullscreen, (), { return document.fullscreenElement ? 1 : 0; });
// The page asks before closing only while a game runs (shell.html, beforeunload).
EM_JS(void, web_in_game, (int on), { Module.antantaInGame = on; });
#endif

namespace {

constexpr float kW = kViewWidth, kH = kViewHeight;
constexpr float kPanelTop = 640;   // the bottom panel: selection, buttons, unported calls
constexpr float kLine = 10;        // text line height (8 x 8 font)
constexpr int kPrefetchTiles = 4;  // static tiles of the ring around the view drawn ahead per frame

struct ScriptEvent {
    std::uint32_t tick = 0;
    std::vector<std::string> words;
};

enum class Action { None, Stop, Delete, BuildMenu, Build, Train, Pause, Step, Speed, Help, Players };



struct GameMessage {
    std::string text;
    bool end = false;
};

struct Button {
    SDL_FRect r{};
    std::string label;
    Action action = Action::None;
    int arg = 0;
    bool enabled = true;
};

struct App {
    SessionLogPtr session_log;  // first, so it is closed last (a run with a window only, platform/session_log.hpp)
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Surface* surface = nullptr;  // batch mode: the software renderer's target
    SDL_Texture* world_tex = nullptr;
    std::string game_dir;
    bool lower_case_paths = false;
    // game
    std::vector<BasicParams> params;
    std::optional<SvgSave> save;
    std::optional<WorldGraphics> graphics;
    LiveView live;
    std::uint32_t live_tick = 0;  // the tick of the state when draw_world last had it compared (live_view_sync)
    UnitSprites sprites;
    // The view: pos is the corner drawn this frame and the one every click maps through; in a window the smooth camera
    // `cam` moves it (ui/camera.hpp), batch runs keep whole cell steps. The static layer is kept in world tiles.
    ViewPos pos;
    ui::Camera cam;
    bool static_dirty = true;  // a new game or a load: all tiles are drawn again
    StaticTiles tiles;
    Target work;
    std::vector<std::uint8_t> frame;
    std::vector<std::uint8_t> marks_atlas;  // slice N: the atlas's pixels for the units' marks in the world picture
    std::vector<UnitMarkQuad> unit_marks;   // slice N: this frame's unit marks (drawn by render_live_units)
    ui::SelectionRings rings;               // the selection rings of your house and ships (docs/render.md 17)
    std::optional<RingTexture> ring_tex;    // Data\Misc\HouseSelTexture.bmp
    bool ring_tex_tried = false;
    std::vector<std::string> warnings;
    // time
    bool paused = false;
    int speed = 1;  // x1, x2, x4
    Uint64 last_ns = 0;
    ui::TickClock clock;
    DrawBlend blend;  // units and shells between the last two ticks (the game window only)
    // UI
    ui::Selection sel;
    std::deque<std::vector<sim::Command>> queue;
    bool dragging = false;
    // The box's first corner in world pixels: it stays on the ground while the view scrolls under the drag.
    float drag_x0 = 0, drag_y0 = 0, mouse_x = 0, mouse_y = 0;
    // The right button held on the world (slice I9, the facing arrow): where it went down, in world pixels.
    bool right_held = false;
    float rdrag_x0 = 0, rdrag_y0 = 0;
    // Middle-mouse drag: the ground under the cursor follows it (where the drag began, the camera then).
    bool panning = false;
    float pan_x0 = 0, pan_y0 = 0;
    double pan_cam_x = 0, pan_cam_y = 0;
    bool build_menu = false;
    int build_type = -1;

    std::optional<std::array<std::int32_t, 2>> pontoon_start;
    bool show_help = false, show_players = true;
    std::string message;
    std::uint32_t message_tick = 0;
    std::vector<Button> buttons;
    SDL_FRect popup{};  // the build menu's box (w 0: closed)
    // batch and logs
    std::string save_path, script_path, png_path, trace_path, commands_path;
    int batch_ticks = -1;
    bool trace_hashes = false;  // --trace-hashes
    double bench_seconds = 0;   // --bench-scroll
    // --bench-frame: seconds to run, the scroll direction, and per frame the parts in ms (ticks, world, upload,
    // interface, present, whole).
    double bench_frame_seconds = 0, bench_frame_t = 0;
    int bench_dx = 1, bench_dy = 1;
    std::vector<std::array<double, 6>> bench_frames;
    std::vector<int> bench_ticks;  // the ticks each frame ran
    int bench_tick_n = 0;
    double world_ms = 0, upload_ms = 0;  // this frame's draw_world parts
    int out_w = kViewWidth, out_h = kViewHeight;
    std::vector<ScriptEvent> script;
    std::size_t script_next = 0;
    bool started = false;
    // A network game (slice MP1, --host / --join): the server when hosting (its loopback for our player), the link,
    // the lockstep driver, the setup the host creates the game with, and the select_houses flags last sent.
    int net_port = 0;
    std::string net_join, net_name = "Player", net_choices, net_error;
    std::vector<std::string> net_settings;
    // Slice MP2: the game's password, the address the host listens on (--net-bind), the folder of the host's order log
    // (--net-log-dir), the map's position count and the host's tick_ms the game is created with.
    std::string net_password, net_bind = "0.0.0.0", net_log_dir;
    std::uint8_t net_positions = 0;
    std::uint32_t net_tick_ms = 30;
    // Slice MP2, the network game from the menu: whether it was opened there (the host starts it with Start), the
    // lobby last shown and its joined humans, this PC's address for the host's lobby, the games on this network (page
    // 2 asks, a host's lobby answers, on net_lan_port), the pre-game script of a batch run (--net-menu-script).
    bool net_menu = false;
    std::string net_lobby_shown, net_my_address, net_menu_script;
    int net_lobby_joined = 0;
    std::uint16_t net_lan_port = 47624;
#ifndef __EMSCRIPTEN__
    std::unique_ptr<net::LanFinder> lan_finder;
    std::unique_ptr<net::LanHost> lan_host;
#endif
    int net_timeout_s = 120;
    sim::SkirmishSetup net_setup;
    std::unique_ptr<net::LoopbackHub> net_hub;
    std::unique_ptr<net::Server> net_server;
    std::unique_ptr<net::Client> net_client;
    std::unique_ptr<NetGame> net_game;
    std::FILE* net_trace = nullptr;
    std::int64_t net_wait_ms = 0;
    bool net_asked = false, net_start_sent = false, net_desync_said = false;
    std::vector<sim::HouseRef> net_flagged;
    // Slice MP9: the save of a network game to resume (--net-resume, the network page's Resume) and its bytes, the
    // batch run's drop (--net-drop-at, as a crash), the seat token last written to network-seats.txt, who the computer
    // played at the last look (the lines it says), the autosaves' writer.
    std::string net_resume, net_resume_bytes;
    std::uint32_t net_drop_at = 0;
    std::uint64_t net_seat_saved = 0;
    std::vector<std::uint8_t> net_ai_seen;
#ifndef __EMSCRIPTEN__
    std::unique_ptr<NetAutosaveWriter> net_writer;
#endif
    // Slice MP9, the network pages: page 2's rows of our saved network games and of the games we had a seat in (read
    // when the page opens), the player the host picked in a resumed game's lobby to move to another seat.
    std::vector<ui::NetGameRow> net_saved;
    bool net_saved_read = false;
    std::uint8_t net_seat_pick = 0xff;
    // front-end (--menu)
    bool menu_mode = false;
    std::string menu_screen;  // batch: the screen to draw
    ui::FrontendPtr menu;
    ui::Settings settings;
    std::string settings_path;  // empty: the settings are not stored (batch)
    // Ours: the windowed menu run (the desktop) remembers its window (place_window); a move or resize is stored once it
    // has rested a second (window_store_ns, 0: nothing to store) and at the end.
    bool remember_window = false;
    Uint64 window_store_ns = 0;
    std::string profiles_path;  // empty: the profiles are not stored (batch)
    ui::Profiles profiles;
    std::optional<std::uint32_t> seed;  // --seed
    // The videos (docs/sound.md section 5): Buka, the intro, then the menu; the credits on the Medal button. Never in
    // batch or headless runs. --video 0 or +video 0 turns them off. On desktop Windows the game's Media/*.wmv play
    // first; --videos <folder> names the converted files, the fallback (on the web the only source: /game/videos/,
    // which the page fills).
    ui::VideoHost video;
    bool videos_on = true;
    std::string videos_dir;
#ifdef ANTANTA_UPDATER
    std::string update_url;                      // --update-url (docs/download.md)
    std::unique_ptr<update::Updater> updater;    // the windowed menu of the release exe, or with --update-url
    std::string lang_url;                        // --lang-url (docs/languages.md, slice L3b)
    std::unique_ptr<lang_pack::Sync> lang_sync;  // the game texts of de, it, ru from the owner's server
    bool lang_asked = false;                     // the box "download the game texts?" was shown this run
#endif
#ifdef ANTANTA_RELEASE_CHECK
    std::unique_ptr<release_check::Checker> releases;  // the public build's Releases notice (decisions.md 40)
#endif
#ifdef __EMSCRIPTEN__
    bool credits_wait = false;    // the Medal button was pressed; the page is fetching the credits (80 MB, on demand)
    bool credits_loaded = false;  // the credits file is in /game/videos/ and goes again when they are over
    std::string lang_wait;        // the Language row chose de, it or ru: the page is getting its game texts (L3b)
#endif
    bool start_game = false;  // --start: press Start on the --screen skirmish screen, then batch mode
    // A new game (from the setup screen), not a save: the view inputs follow the owner's answer 7 (every house counts
    // as drawn, docs/view-dependence.md) as in antanta-headless --new-skirmish, and the trace has its two R lines.
    bool new_game = false;
    std::string game_source;  // what the session log's first line names: "save <path>" or the skirmish's choices
    std::optional<sim::SkirmishSetup> last_setup;  // the choices of the skirmish being played (the in-game Restart)
    bool plus = false;  // --plus: new skirmishes with Entente Engine Plus (the setup screen's box aside)





    std::uint32_t mission_seed = 0;
    std::string mission_title;
    std::optional<TextTable> text_table;
    std::deque<GameMessage> messages;
    bool message_shown = false;
    std::string user_dir;
    std::string progress_path;
    ui::Progress progress;
    // Slice H, our own saves (docs/save-own.md): a game from one counts every house as drawn, as a new game; the
    // folder of the save files (--save-dir, else %APPDATA%\EntenteEngine\saves).
    bool own_game = false;
    std::string save_dir;
    // The end of a game (docs/endgame.md): the end message was handed to the front-end; batch --end-statistics.
    bool end_shown = false;
    int end_clock = -1;
    ui::PlayClock play_clock;  // the time the game ran, for the play time (docs/save-audit.md Q8)
    std::int64_t fixed_clock = 0;
    std::atomic<std::int64_t> clock_s{0};  // the clock the step reads: fixed_clock or play_clock (publish_clock)
    // --sim-thread off|on|auto (0, 1, 2; -1: the setting), docs/sim-thread.md; "pictures": the pictures without the
    // thread (slice S1, for tests).
    int sim_thread = -1;
    bool sim_pictures = false;
    // The original's interface (docs/interface.md part A, slice I1): the game view covers the window (view_w x view_h
    // world pixels, drawn `zoom` times), the frame, the resource bar, the thermometer and the screen texts over it.
    // F12 (ours, a developer key) switches to the debug HUD: the 1024 x 768 view letterboxed with the milestone 3
    // panels. Script coordinates are world pixels of the view in both.
    bool debug_hud = false;
#ifdef __EMSCRIPTEN__
    bool frame_log = false;  // --frame-log
    double frame_log_t = 0, frame_log_sum = 0, frame_log_max = 0;
    int frame_log_n = 0;
    bool menu_drawn = false;    // the page is told once (Module.antantaReady)
    bool page_in_game = false;  // what the page was last told (Module.antantaInGame: warn before leaving a game)
#endif
    std::optional<std::uint8_t> cli_ui_scale, cli_ui_pictures, cli_zoom;  // --ui-scale, --ui-pictures, --zoom
    // The display language (docs/languages.md, slice L1; ui/lang.hpp): what the interface shows, never the sim. --lang
    // for this run; --lang-setting stores a choice as Game options does (the browser's start page). `interactive`: a
    // run with a window, which may follow the system's language and read the user folder's languages; batch runs take
    // English unless --lang names another. `display_tt`: the text table as shown (text_table_of stays English).
    ui::Language lang;
    std::string cli_lang, cli_lang_setting;
    bool interactive = false;
    std::optional<TextTable> display_tt;
    ui::GameInterfacePtr iface;
    bool iface_failed = false;
    ui::ScreenTexts texts;
    double text_ms = 0;  // real time toward the next frame of the screen texts (one per tick period)
    int view_w = kViewWidth, view_h = kViewHeight, ui_scale = 1;
    // The world zoom (ui/camera.hpp, docs/decisions.md 41): the one drawn this frame, the Game options one (whole: a
    // game's first and the reset key's), the one the wheel and the keys asked for (0: the Options one) and the screen
    // point in output pixels it is anchored on. Batch runs keep the Options zoom (a script's `zoom` changes it).
    double zoom = 1, zoom_target = 0, zoom_ax = 0, zoom_ay = 0;
    int start_zoom = 1;
    int tex_w = 0, tex_h = 0;            // the size world_tex was made for: the view at the smallest zoom
    int view_max_w = 0, view_max_h = 0;  // that size for the current output
    // The selection panel (docs/interface.md B, slice I4): rebuilt from the selection whenever it is needed, its
    // pictures and fonts, the string table it shows, and the press and hover state of its buttons.
    ui::PanelGraphicsPtr panel_gfx;
    bool panel_failed = false;
    std::optional<ui::StringTable> strings;
    ui::PanelInput panel_in;
    bool panel_down = false;  // the left or right press went down on the panel
    // The build ghost (slice I5, ui/build_menu.hpp): its textures.
    ui::GhostGraphicsPtr ghost_gfx;


    ui::FitCells fit_cells;
    bool script_ctrl = false;
    // The held build press (build_hold): the left button went down on the map with a build type, Shift at the press
    // (batch scripts; a window reads the keyboard), and the cell of the last accepted add_house of this press.
    bool build_held = false;
    bool build_hold_shift = false;
    std::array<std::int32_t, 2> build_held_cell{-1, -1};


    bool show_health = false;
    int tab_index = 0;
    ui::MarksGraphicsPtr marks_gfx;
    ui::HouseBoxCache house_boxes;
    std::vector<ui::MarkQuad> marks;
    double frame_ms = 0;  // real time since the last drawn frame (the ghost's pulse steps by it; batch frames: 0)
    // Slice I6: the house panel's Set_Exit cursor mode 3 (the next left press on the map sets the rally point), the
    // rally flag's pictures and phase.
    bool set_exit = false;
    ui::RallyGraphicsPtr rally_gfx;


    int fire_point = -1;
    // Entente Engine Plus: the call-in strike's target mode after a town centre's strike button (sim::StrikeId, -1
    // off): the next left press on the map is the target, or for the creeping barrage and the bombing run the first
    // press the start (strike_start) and the second the end; a right press or Esc cancels it.
    int strike = -1;
    std::optional<std::array<std::int32_t, 2>> strike_start;
    // Slice I9: the cursor pictures; draw_cursor: batch pictures get the cursor at the mouse point (script `cursor`).
    ui::CursorGraphicsPtr cursor_gfx;
    bool draw_cursor = false;
    ui::FlagPhase flag_phase;
    // Slice I7: the market panel's amounts (the original's UI globals, set to 0 for a new game or a load).
    ui::MarketUi market;



    bool group_bar_shown = true;
    std::array<ui::PanelInput, 2> window_in;
    int window_down = -1;
    std::optional<std::array<int, 2>> pause_at;  // the pause window moved by its strip (original pixels), else centred
    ui::WindowDrag pause_drag;
    ui::MissionLog mission;
    std::optional<std::vector<std::string>> hints;
    int hint_index = 0;
    // Sound and music (slice J, platform/audio.hpp): null in batch runs, with --no-audio and without a device.
    audio::AudioPtr audio;
    bool audio_off = false;    // --no-audio
    int group_alert_wait = 0;
    // The minimap (slice I2, ui/minimap.hpp): shown or hidden (M, the arrow button), its pictures, the colours and
    // the static layer (made again for a new game, a changed house or map object, or a new scale: minimap_k 0), the
    // battle marks, and a left press that went down on it (the view follows the mouse while it is held).
    bool minimap_shown = true;
    ui::MinimapGraphicsPtr minimap_gfx;
    bool minimap_failed = false;
    std::optional<ui::MinimapPalette> minimap_pal;
    std::vector<std::uint32_t> minimap_rgb;
    std::uint64_t minimap_version = 0;
    int minimap_k = 0;
    std::vector<ui::MinimapMark> minimap_marks;
    bool minimap_drag = false;


    int hud_armed = -1;
    bool left_down = false;
    // The fog of war (docs/fog.md): the minimap's layer and the view's samples, made again when the sim rebuilt its
    // circles (fog_key) or the view moved; the edge ramp of SuperFog.tga.
    std::vector<std::uint8_t> minimap_fog;
    std::uint64_t minimap_fog_version = 0, minimap_fog_key = ~std::uint64_t{0};
    std::optional<ui::FogRamp> fog_ramp;
    bool fog_ramp_failed = false;
    ui::FogView fog_view;
    ui::FogPicks fog_picks;  // the fog samples' screen picks, kept while the view scrolls
    std::array<std::int64_t, 5> fog_view_key{};
    ui::FogTexture fog_tex;
    std::uint64_t fog_serial = 0;  // counts the rebuilds the app has seen (a new game starts a new count)
    // The minimap's unit dots, made again only when the game (fog_serial), the tick, the scale or the selection
    // changed: the units move only in a step.
    std::vector<ui::MinimapDot> minimap_dots;
    std::uint64_t minimap_dots_serial = ~std::uint64_t{0};
    std::uint64_t minimap_dots_version = 0;  // counted on with each new list (the minimap's dots layer)
    std::uint32_t minimap_dots_tick = 0;
    int minimap_dots_k = 0;
    std::uint8_t minimap_dots_player = 0, minimap_dots_sel_player = 0;
    std::vector<sim::UnitRef> minimap_dots_sel;
    // players_with_ship_slots once a tick for the ships' reflections (the slots change only in a step).
    std::vector<bool> ship_slots;
    std::uint64_t ship_slots_serial = ~std::uint64_t{0};
    std::uint32_t ship_slots_tick = 0;
    const WorldGraphics* ship_slots_graphics = nullptr;
    // The game itself (docs/sim-thread.md): its live state, Lua script and session files, and the ticks. Last, so it
    // goes first when the app ends.
    SimHost host;
    // The state to draw and to build orders from (const: the app changes the state only through the host).
    const sim::GameState* view() const { return host.view(); }
};

// The play time's clock as the step reads it (sim::Game_SetClock in enter_game), after each change of either clock.
void publish_clock(App& app) {
    app.clock_s.store(app.end_clock >= 0 ? app.fixed_clock : app.play_clock.seconds(), std::memory_order_relaxed);
}

void after_tick(App& app, TickRecord& r);

// How a game runs its ticks (docs/sim-thread.md): on the sim thread when --sim-thread or the setting says so ("auto":
// from 4 logical processors on), batch runs and test scripts in lockstep (each tick waits for its record). The threaded
// web page as the desktop (slice S4); the one-thread page keeps them on the main thread.
SimHost::Mode sim_mode(const App& app) {
    if (app.sim_pictures) return SimHost::Mode::Pictures;
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    return SimHost::Mode::Off;
#else
    // Batch runs and test scripts keep the one-thread loop unless --sim-thread asks (section 2): the setting's default
    // is auto since slice S6, and the gate's app checks compare the two ways.
    const bool batch = app.batch_ticks >= 0 || !app.script.empty();
    const int want = app.sim_thread >= 0 ? app.sim_thread : batch ? 0 : app.settings.sim_thread;
    const bool on = want == 1 || (want == 2 && SDL_GetNumLogicalCPUCores() >= 4);
    return on ? SimHost::Mode::Thread : SimHost::Mode::Off;
#endif
}

// The live game for a change outside a tick (a load's script, a save, the end statistics, a batch script's result):
// the host runs no tick of it until the next grant, and the ticks it ran are handled first, so the view is the live
// state (docs/sim-thread.md section 2, "stopped").
SimGame& held(App& app) {
    app.host.hold();
    TickRecord r;
    while (app.host.take(&r)) after_tick(app, r);
    return app.host.game();
}

GameFileLoader loader(const App& app) {
    return [&app](const std::string& path, std::string* error) {
        std::string p = path;
        if (app.lower_case_paths) {  // the web: also the Windows separators of the game's own names (ui/sound.cpp)
            for (char& c : p) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c == '\\' ? '/' : c);
        }
        return load_file(app.game_dir + "/" + p, error);
    };
}

void open_game_menu(App& app);  // the in-game menu (Esc, the Play menu button), in the front-end part below
const ui::StringTable& panel_strings(App& app);
void open_mission(App& app);    // slice I8: the mission window (F1, the Objective button)
void open_hints(App& app);      // slice I8: the hints window (the Help button)
void open_diplomacy(App& app);  // slice I8: the diplomacy window (the city centre's button)
void quick_save(App& app);      // slice H: F5
void quick_load(App& app);      // slice H: F6
void menu_presentation(App& app);
SDL_AppResult menu_request(App& app, ui::FrontendRequest request);

// The camera for the sounds: the view's top left corner in world pixels and its size.
ui::Listener listener(const App& app) {
    return ui::Listener{static_cast<double>(view_x(app.pos)), static_cast<double>(view_y(app.pos)), app.view_w,
                        app.view_h};
}

// Sound and music (slice J): only with a window; --no-audio or no audio device leaves the game silent.
void open_audio(App& app) {
    if (app.audio_off) return;
    std::string err;
    app.audio = audio::audio_open(loader(app), app.settings, &err);
    if (!app.audio) SDL_Log("sound: %s (the game runs without sound)", err.c_str());
    if (app.view()) audio::audio_game_data(app.audio.get(), app.params);
}

[[maybe_unused]] std::vector<std::string> words_of(const std::string& line) {  // script lines (desktop)
    std::vector<std::string> out;
    std::string w;
    for (char c : line) {
        if (c == ' ' || c == '\t' || c == '\r') {
            if (!w.empty()) out.push_back(w), w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty()) out.push_back(w);
    return out;
}

void say(App& app, const std::string& text) {  // the debug HUD's line and the log: English (docs/languages.md 6)
    app.message = text;
    app.message_tick = app.view() ? app.view()->tick : 0;
    SDL_Log("tick %u: %s", app.message_tick, text.c_str());
}

// Our text of `key` in the display language (engine/lang/en/ui.txt, docs/languages.md slice L2).
std::string ours(const App& app, std::string_view key,
                 const std::vector<std::pair<std::string, std::string>>& args = {}) {
    return ui::lang_text(app.lang, key, args);
}

// A reason that a part without a language gave as English text (the loading, the network, the updater): its sentence
// in the display language when it is one of ours (ui::lang_reverse, the "reason." keys), else as it is (a technical
// detail stays English). The marker check of `xx` leaves it out either way.
std::string reason(const App& app, const std::string& english) {
    std::string shown = ui::lang_reverse(app.lang, english, "reason.");
    ui::lang_data(&app.lang, shown);
    return shown;
}


ViewPos clamp_pos(const App& app, ViewPos p) { return ui::clamp_camera(app.live.map, app.view_w, app.view_h, p); }

// In a window the camera scrolls smoothly (ours, ui/camera.hpp) and the simulation sees no camera at all (the owner's
// fixed rule for every game, docs/view-dependence.md 4.5). Batch runs (--frames) keep the whole-cell camera of the
// scripts and, for a save, its view inputs, so their traces stay as they were.
bool smooth_camera(const App& app) { return app.window != nullptr; }

// Puts the view at p (a cell, as the save, the start view, a script or a step key give it), at rest.
void set_view(App& app, ViewPos p) {
    app.pos = clamp_pos(app, p);
    if (smooth_camera(app)) {  // the original's limits (column and row 1 at the top-left)
        ui::camera_jump(app.cam, view_x(app.pos), view_y(app.pos),
                        ui::camera_bounds(app.live.map, app.view_w, app.view_h));
        app.pos = ui::camera_view(app.cam);
    } else {
        app.cam = ui::Camera{static_cast<double>(view_x(app.pos)), static_cast<double>(view_y(app.pos))};
    }
}

// Only the debug HUD has a bottom panel; the original's interface leaves the bottom to the world until the selection
// panels come (slice I4).
bool in_panel(const App& app, float y) { return app.debug_hud && y >= kPanelTop; }

// The zoom's network rule (the cap for every player) in a window; batch runs keep their whole zoom (traces).
bool zoom_network(const App& app) { return app.net_client != nullptr && smooth_camera(app); }

// The size of the game view for the renderer's output: the whole output divided by the world zoom (the interface), or
// the 1024 x 768 view of the debug HUD. A new size redraws the static layer and keeps the camera inside its bounds.
// The zoom is the Options one until the wheel or a key changes it (a new Options zoom or a new game starts from it
// again), within the limits of ui/camera.hpp; the world texture is made once for the largest view the limits allow.
void update_view(App& app) {
    int w = kViewWidth, h = kViewHeight;
    app.view_max_w = w, app.view_max_h = h;
    if (!app.debug_hud && app.renderer) {
        int ow = 0, oh = 0;
        SDL_GetRenderOutputSize(app.renderer, &ow, &oh);
        if (ow > 0 && oh > 0) {
            app.ui_scale = ui::ui_scale_for(app.settings.ui_scale, ow, oh);
            const int start = ui::world_zoom_for(app.settings.world_zoom, app.settings.ui_scale, ow, oh);
            if (start != app.start_zoom || app.zoom_target <= 0) app.zoom = app.zoom_target = app.start_zoom = start;
            const double zmin = ui::zoom_min(ow, oh, start, zoom_network(app));
            app.zoom_target = ui::zoom_clamp(app.zoom_target, zmin);
            app.zoom = ui::zoom_clamp(app.zoom, zmin);
            w = ui::zoom_view_size(ow, app.zoom), h = ui::zoom_view_size(oh, app.zoom);
            const double zfar = ui::zoom_clamp(0, zmin);
            app.view_max_w = std::max(w, ui::zoom_view_size(ow, zfar));
            app.view_max_h = std::max(h, ui::zoom_view_size(oh, zfar));
        }
    }
    if (w == app.view_w && h == app.view_h) return;
    app.view_w = w, app.view_h = h;
    if (!app.started) return;
    if (smooth_camera(app)) {  // the camera keeps its sub-pixel place and its speed (a zoom moves it every frame)
        ui::camera_clamp(app.cam, ui::camera_bounds(app.live.map, app.view_w, app.view_h));
        app.pos = ui::camera_view(app.cam);
    } else {
        set_view(app, app.pos);
    }
}

// The zoom eases toward its target, the camera keeps the anchor's world point under it (a window only).
void zoom_frame(App& app, double dt) {
    if (app.zoom_target <= 0 || app.zoom == app.zoom_target || app.debug_hud) return;
    const double z0 = app.zoom;
    app.zoom = ui::zoom_ease(z0, app.zoom_target, dt);
    ui::camera_zoom_about(app.cam, app.zoom_ax, app.zoom_ay, z0, app.zoom);
    update_view(app);
    ui::camera_clamp(app.cam, ui::camera_bounds(app.live.map, app.view_w, app.view_h));
    app.pos = ui::camera_view(app.cam);
}

// The wheel or a key: `steps` zoom steps (in > 0) about the output point (ax, ay); reset: the Options zoom about the
// screen's centre. `now`: at once (a script), else eased by zoom_frame.
void zoom_by(App& app, double steps, double ax, double ay, bool reset, bool now) {
    if (app.debug_hud || !app.renderer) return;
    update_view(app);  // the limits for this output
    int ow = 0, oh = 0;
    SDL_GetRenderOutputSize(app.renderer, &ow, &oh);
    const double zmin = ui::zoom_min(ow, oh, app.start_zoom, zoom_network(app));
    app.zoom_target = ui::zoom_clamp(reset ? app.start_zoom : ui::zoom_step(app.zoom_target, steps), zmin);
    app.zoom_ax = ax, app.zoom_ay = ay;
    if (!now) return;
    ui::Camera c = app.cam;
    ui::camera_zoom_about(c, ax, ay, app.zoom, app.zoom_target);
    app.zoom = app.zoom_target;
    update_view(app);
    if (smooth_camera(app)) {
        app.cam.x = c.x, app.cam.y = c.y;
        ui::camera_clamp(app.cam, ui::camera_bounds(app.live.map, app.view_w, app.view_h));
        app.pos = ui::camera_view(app.cam);
    } else {  // batch: the corner at the whole pixel, as a script's camera puts it
        set_view(app, view_pos_at(static_cast<int>(std::floor(c.x + 0.5)), static_cast<int>(std::floor(c.y + 0.5))));
    }
}

// A game starts at the Options zoom (before its start view is worked out, which depends on the view's size).
void zoom_reset(App& app) {
    app.zoom_target = 0;
    update_view(app);
}

// ---- game loading ----

// The save's camera can look at somebody else's corner (the game's own No_name.svg opens on the computer player's
// base while the human's army stands 100 cells away), so nothing of the player's would be on the screen to click.
// If no unit and no house of the human player is in the opening view, centre the view on their first unit
// (else their first standing house).
void show_own_side(App& app) {
    const sim::GameState& s = *app.view();
    if (app.sel.player >= s.players.size()) return;
    const sim::Player& pl = s.players[app.sel.player];
    const int c0 = app.pos.col - 1, c1 = app.pos.col + kViewWidth / 96 + 1;
    const int r0 = app.pos.row, r1 = app.pos.row + static_cast<int>(kPanelTop) / 24;
    auto inside = [&](int x, int y) { return x >= c0 && x <= c1 && y >= r0 && y <= r1; };
    int fx = -1, fy = -1;
    for (std::uint32_t k = pl.first_unit; k != sim::kNoSlot; k = pl.unit_slots[k].next) {
        const sim::Unit& u = pl.unit_slots[k];
        if (u.is_deleted != 0) continue;
        if (inside(u.map_x, u.map_y)) return;
        if (fx < 0) fx = u.map_x, fy = u.map_y;
    }
    for (const sim::House& h : pl.houses) {
        if (h.is_destroyed != 0 || h.hp <= 0.0f) continue;
        if (inside(h.map_x, h.map_y)) return;
        if (fx < 0) fx = h.map_x, fy = h.map_y;
    }
    if (fx < 0) return;
    set_view(app, ViewPos{fx - kViewWidth / 192, fy - static_cast<int>(kPanelTop) / 48});
    SDL_Log("the saved camera shows none of player %u: view moved to %d %d", static_cast<unsigned>(app.sel.player),
            app.pos.col, app.pos.row);
}

// GameData and app.params from the game folder, as antanta-headless loads them (BasicParams, objects, ships,
// GameInf.txt's speed and truncated animations).
std::optional<sim::GameData> load_game_data(App& app, UnitTypeCounts* types_out, std::string* err) {
    const GameFileLoader load = loader(app);
    UnitTypeCounts types{};
    app.params.clear();
    for (int n = 0; n < 6; ++n) {
        const std::string path = "Data/BasicParams" + std::to_string(n) + ".dat";
        auto file = load(path, err);
        if (!file) return std::nullopt;
        auto p = parse_basic_params(*file, err);
        if (!p) {
            *err = path + ": " + *err;
            return std::nullopt;
        }
        if (n < 5) types[n] = static_cast<std::uint16_t>(p->units.size());
        app.params.push_back(std::move(*p));
    }
    auto data = sim::GameData_FromParams(app.params, err);
    if (!data) return std::nullopt;
    auto objects = load("Data/Objects/objects.les", err);
    if (!objects) return std::nullopt;
    if (!sim::GameData_LoadObjects(*data, *objects, err)) return std::nullopt;
    {  // ships: ShipTowersParams.txt and the ship graphics (milestone 3 slice H), as antanta-headless
        auto towers = load("Data/ShipTowersParams.txt", err);
        if (!towers) return std::nullopt;
        auto read_les = [&load](const std::string& path) {
            std::string e;
            return load("Data/Units/" + path + ".les", &e);
        };
        if (!sim::GameData_LoadShips(*data, app.params, *towers, read_les, err)) return std::nullopt;
    }
    std::string gi_err;
    if (auto file = load("Data/GameInf.txt", &gi_err)) {  // compiled-in fallbacks: 53 ms, full animations
        if (auto v = parse_game_speed_ms(*file, &gi_err)) data->speed_ms = *v;
        if (auto v = parse_short_anims(*file, &gi_err)) data->short_anims = *v ? 1 : 0;
    }
    if (types_out) *types_out = types;
    return data;
}

// GameData and GameState are about 150 KB each: they go to the heap, and the optional each loader returns stays in
// these helpers' frames, so load_game's and start_new_skirmish's frames stay small (the MSVC Debug stack is 1 MB), as
// in antanta-headless.
std::unique_ptr<sim::GameData> data_on_heap(App& app, UnitTypeCounts* types_out, std::string* err) {
    auto d = load_game_data(app, types_out, err);
    return d ? std::make_unique<sim::GameData>(std::move(*d)) : nullptr;
}
template <class Load>
std::unique_ptr<sim::GameState> state_on_heap(Load&& load) {
    auto s = load();
    return s ? std::make_unique<sim::GameState>(std::move(*s)) : nullptr;
}

const TextTable& text_table_of(App& app);                                 // slice M4, below
const TextTable& display_text_table(App& app);                            // below
std::string display_tt_text(App& app, const std::string& english);        // below
void mission_events(App& app, const std::vector<sim::SimEvent>& events);  // slice M3, below

// The game view on app.view(): the ground of `map` (houses and units come from the state), the camera at `pos`, your
// player selected, the trace and the session log opened. A save (load_game) and a new skirmish (start_new_skirmish)
// both enter the game here.
bool enter_game(App& app, MapData map, ViewPos pos, std::string* err) {
    app.host.game().lua.reset(), app.host.game().lua_logged = 0;  // a new mission opens its script after this
    if (!app.graphics) {
        app.graphics = load_world_graphics(loader(app), err);
        if (!app.graphics) return false;
    }
    app.live = LiveView{};
    app.live.map = std::move(map);
    zoom_reset(app);  // a game starts at the Options zoom
    set_view(app, pos);
    app.sel = ui::Selection{};
    app.sel.player = app.view()->my_player;
    app.market = ui::MarketUi{};
    app.group_bar_shown = true;  // slice I8
    if (app.view()->mission_path.empty()) {
        app.mission = ui::mission_log_start(panel_strings(app)), app.mission_title.clear();
    } else {
        const ui::MissionTexts t = ui::mission_texts(app.view()->mission_path, display_text_table(app));
        app.mission = ui::MissionLog{}, app.mission_title = t.title;
        ui::mission_log_add(app.mission, t.text);
    }
    app.messages.clear(), app.message_shown = false;
    {
        std::vector<sim::SimEvent> start;
        start.swap(app.host.game().state->events);
        mission_events(app, start);
    }
    if (!app.new_game && !app.own_game) show_own_side(app);
    app.static_dirty = true;
    app.blend = DrawBlend{};
    SimGame& g = app.host.game();
    if (!app.trace_path.empty()) {
        g.trace = std::fopen(app.trace_path.c_str(), "wb");
        g.trace_hashes = app.trace_hashes;
        if (!g.trace) {
            *err = "cannot write " + app.trace_path;
            return false;
        }
        // A new skirmish: InitGame's and RandomMap_Setup's fills, as antanta-headless --new-skirmish; a new mission:
        // InitGame's (--new-mission); a save: the load's.
        std::string head = sim::trace_header();
        if (app.new_game && app.view()->mission_path.empty()) head += sim::trace_rng_fill(*app.view(), 0, false);
        head += sim::trace_rng_fill(*app.view(), 0, !app.new_game);
        std::fwrite(head.data(), 1, head.size(), g.trace);
    }
    if (!app.commands_path.empty()) {
        g.commands = std::fopen(app.commands_path.c_str(), "wb");
        if (!g.commands) {
            *err = "cannot write " + app.commands_path;
            return false;
        }
        std::fprintf(g.commands, "# antanta session: %s, first tick %u\n", app.game_source.c_str(),
                     app.view()->tick + 1);
    }





    app.host.set_mode(sim_mode(app), app.batch_ticks >= 0 || (!app.script.empty() && app.bench_frame_seconds <= 0));
    // The drawing leaves the sim thread its processor (docs/sim-thread.md 4): one band less where all would draw.
    parallel_bands_spare(app.host.mode() == SimHost::Mode::Thread ? 1 : 0);
    if (app.end_clock >= 0) {
        app.fixed_clock = 0;
    } else {  // owner's decision 2026-10-17 (docs/bugs.md B41): a pause or a menu does not count as play time
        app.play_clock = {};
    }
    publish_clock(app);
    sim::Game_SetClock(*g.state, [&app] { return app.clock_s.load(std::memory_order_relaxed); });
    app.end_shown = false;
    audio::audio_game_data(app.audio.get(), app.params);
    app.group_alert_wait = 0;
    app.show_health = false, app.house_boxes.clear();
    app.minimap_k = 0, app.minimap_marks.clear(), app.minimap_pal.reset();  // slice I2
    ++app.fog_serial;                                                       // the fog: a new game draws its own
    app.texts = ui::ScreenTexts{};
    app.started = true;
    SDL_Log("%s: tick %u, %zu players, my player %u, map %d x %d, camera %d %d, tick %u ms", app.game_source.c_str(),
            app.view()->tick, app.view()->players.size(), app.view()->my_player, app.live.map.width,
            app.live.map.height, app.pos.col, app.pos.row, app.view()->speed_ms);
    return true;
}

bool load_own_game(App& app, const Bytes& file, std::string* err);  // slice H, below
sim::ScriptVmPtr reopen_script(App& app, const std::optional<std::string>* own, const std::string& svg_path);  // M6

bool load_game(App& app, std::string* err) {
    UnitTypeCounts types{};
    const std::unique_ptr<sim::GameData> data = data_on_heap(app, &types, err);
    if (!data) return false;
    auto file = load_file(app.save_path, err);
    if (!file) return false;
    if (sim::OwnSave_IsOwn(*file)) return load_own_game(app, *file, err);  // slice H
    app.save = parse_svg(*file, types, err);
    if (!app.save) {
        *err = app.save_path + ": " + *err;
        return false;
    }
    std::unique_ptr<sim::GameState> state =
        state_on_heap([&] { return sim::GameState_FromSave(*app.save, *data, err); });
    if (!state) {
        app.host.replace(nullptr);
        *err = app.save_path + ": " + *err;
        return false;
    }
    app.host.replace(std::move(state));
    app.new_game = false, app.own_game = false;
    app.last_setup.reset();  // the original's save keeps no skirmish setup
    app.game_source = "save " + app.save_path;
    sim::ScriptVmPtr lua = reopen_script(app, nullptr, app.save_path);  // slice M6
    if (!enter_game(app, map_from_save(*app.save),
                    ViewPos{static_cast<std::int16_t>(app.save->view_x), static_cast<std::int16_t>(app.save->view_y)},
                    err))
        return false;
    app.host.game().lua = std::move(lua);
    return true;
}





ViewPos start_view(const App& app, const sim::GameState& s, const MapData& map) {
    const ui::ViewCells cells = ui::view_cells(app.view_w, app.view_h);
    ViewPos p{3, 3};
    if (s.my_player < s.players.size()) {
        const sim::Player& me = s.players[s.my_player];
        if (me.is_lost == 0 && me.first_unit != sim::kNoSlot) {
            const sim::Unit& u = me.unit_slots[me.first_unit];
            p.col = u.map_x > cells.cols + 2 ? u.map_x - cells.cols / 2 : 2;
            p.row = u.map_y > cells.rows + 2 ? u.map_y - cells.rows / 2 : 2;
            if (p.col % 2 == 0) --p.col;
            if (p.row % 2 == 0) --p.row;
        }
    }
    return ui::clamp_camera(map, app.view_w, app.view_h, p);
}

// Start on the setup screen: the game folder's data, the map Maps/<setup.map>, Data/Template4.tpl and
// GameState_NewSkirmish, as antanta-headless --new-skirmish; the game speed and the truncated animations come from
// the settings (Options and Game options set them in the original before a game starts). On an error nothing of the
// game is kept and the caller stays on the setup screen.
// A network game's setup (net) comes whole from the host: its computers' names stay.
bool start_new_skirmish(App& app, const sim::SkirmishSetup& setup, std::string* err, bool net = false) {
    const GameFileLoader load = loader(app);
    const std::unique_ptr<sim::GameData> data = data_on_heap(app, nullptr, err);
    if (!data) return false;
    data->speed_ms = app.settings.speed_ms;
    data->short_anims = app.settings.short_anims ? 1 : 0;
    const std::string path = "Maps/" + setup.map;
    auto file = load(path, err);
    if (!file) return false;
    auto map = parse_map(*file, err);
    if (!map) {
        *err = path + ": " + *err;
        return false;
    }

    std::optional<AiTemplate> tpl;
    std::string tpl_err;
    if (auto t = load("Data/Template4.tpl", &tpl_err)) {
        tpl = parse_ai_template(*t, err);
        if (!tpl) {
            *err = "Data/Template4.tpl: " + *err;
            return false;
        }
    }

    sim::SkirmishSetup named = setup;
    if (app.plus && !net) named.plus = true;  // --plus
    std::string st_err;
    if (auto st = net ? std::nullopt : load("Data/StringTable_eng.st", &st_err)) {
        if (auto table = parse_string_table(*st, &st_err)) sim::SkirmishSetup_CompNames(&named, *table);
    }
    std::unique_ptr<sim::GameState> state =
        state_on_heap([&] { return sim::GameState_NewSkirmish(named, *map, *data, tpl ? &*tpl : nullptr, err); });
    if (!state) return false;
    app.host.replace(std::move(state));
    app.save.reset();
    app.new_game = true, app.own_game = false;
    app.last_setup = setup;
    app.last_setup->plus = named.plus;  // the save's setup and the Restart keep the rule set
    app.game_source = "new skirmish on " + path + ", " + std::to_string(setup.players) + " players, resources " +
                      std::to_string(setup.resources) + ", seed " + std::to_string(setup.seed);
    MapData ground = std::move(*map);  // the view draws houses and units from the state
    ground.houses.clear();
    ground.units.clear();
    ground.neutral_houses.clear();
    zoom_reset(app);  // the start view depends on the view's size
    const ViewPos pos = start_view(app, *app.view(), ground);
    if (!enter_game(app, std::move(ground), pos, err)) {
        app.host.replace(nullptr);
        app.started = false;
        return false;
    }
    return true;
}

// What a Lua mission's script reads (slice M5): `require`'s files from the game folder, the text table.
sim::ScriptHost script_host(App& app) {
    sim::ScriptHost host;
    host.read = [load = loader(app)](const std::string& p) {
        std::string q = p, e;
        for (char& c : q) c = c == '\\' ? '/' : c;
        return load(q, &e);
    };
    host.texts = text_table_of(app);
    return host;
}




sim::ScriptVmPtr reopen_script(App& app, const std::optional<std::string>* own, const std::string& svg_path) {
    sim::GameState& s = *held(app).state;
    if (own != nullptr ? s.lua_mission == 0 : s.mission_path.size() <= 3) return nullptr;
    const std::string name = sim::Mission_ScriptPath(s.mission_path);
    std::string file = name, err;
    for (char& c : file) c = c == '\\' ? '/' : c;
    auto lua = loader(app)(file, &err);
    if (!lua) {
        if (own != nullptr) SDL_Log("the mission's script: %s", err.c_str());
        return nullptr;
    }
    if (own != nullptr) return sim::ScriptVm_Reload(s, *lua, name, script_host(app), *own);
    std::optional<sim::LuaSaveFile> saved;
    if (auto bytes = load_file(sim::Mission_ScriptPath(svg_path), &err)) {
        saved = sim::parse_lua_save(*bytes, &err);
        if (!saved) SDL_Log("%s: %s", sim::Mission_ScriptPath(svg_path).c_str(), err.c_str());
    }
    return sim::ScriptVm_OpenSaved(s, *lua, name, script_host(app), saved ? &*saved : nullptr);
}





bool start_new_mission(App& app, const std::string& path, std::uint32_t seed, std::string* err) {
    const GameFileLoader load = loader(app);
    const std::unique_ptr<sim::GameData> data = data_on_heap(app, nullptr, err);
    if (!data) return false;
    data->speed_ms = app.settings.speed_ms;
    data->short_anims = app.settings.short_anims ? 1 : 0;
    std::string file_path = path;
    for (char& c : file_path) c = c == '\\' ? '/' : c;
    auto file = load(file_path, err);
    if (!file) return false;
    const bool mit = file_path.size() > 4 && (file_path.compare(file_path.size() - 4, 4, ".mit") == 0 ||
                                              file_path.compare(file_path.size() - 4, 4, ".MIT") == 0);
    sim::MissionSetup ms;
    if (mit) {
        auto m = parse_mit(*file, err);
        if (!m) {
            *err = path + ": " + *err;
            return false;
        }
        ms = sim::MissionSetup_FromMit(*m, path);
    } else {
        auto m = parse_mis(*file, err);
        if (!m) {
            *err = path + ": " + *err;
            return false;
        }
        ms = sim::MissionSetup_FromMis(*m, path);
        sim::MissionSetup_SetTexts(ms, text_table_of(app));  // the blocks' texts (slice M3)
    }
    ms.seed = seed, ms.economic = app.settings.economic, ms.military = app.settings.military;
    const std::string map_path = "Maps/" + ms.map;
    auto map_file = load(map_path, err);
    if (!map_file) return false;
    auto map = parse_map(*map_file, err);
    if (!map) {
        *err = map_path + ": " + *err;
        return false;
    }
    std::optional<AiTemplate> tpl;
    std::string tpl_err;
    if (auto t = load("Data/Template4.tpl", &tpl_err)) {
        tpl = parse_ai_template(*t, err);
        if (!tpl) {
            *err = "Data/Template4.tpl: " + *err;
            return false;
        }
    }
    std::unique_ptr<sim::GameState> state =
        state_on_heap([&] { return sim::GameState_NewMission(ms, *map, *data, tpl ? &*tpl : nullptr, err); });
    if (!state) return false;
    app.host.replace(std::move(state));
    app.save.reset();
    app.new_game = true, app.own_game = false;
    app.last_setup.reset();
    app.mission_seed = seed;
    app.game_source = "new mission " + path + " on " + map_path + ", seed " + std::to_string(seed);
    MapData ground = std::move(*map);  // the view draws houses and units from the state
    ground.houses.clear();
    ground.units.clear();
    ground.neutral_houses.clear();
    zoom_reset(app);  // the start view depends on the view's size
    const ViewPos pos = start_view(app, *app.view(), ground);
    if (!enter_game(app, std::move(ground), pos, err)) {
        app.host.replace(nullptr);
        app.started = false;
        return false;
    }
    // Slice M5: Mission_Setup's Lua script, the mission's path with "lua" for its extension (docs/missions.md 5).
    std::string lua_path = sim::Mission_ScriptPath(file_path), lua_err;
    if (auto lua = load(lua_path, &lua_err)) {
        app.host.game().lua = sim::ScriptVm_Open(*app.host.game().state, *lua, lua_path, script_host(app));
    }
    return true;
}

// ---- the tick ----

void game_events(App& app, std::vector<sim::SimEvent> events);
void build_hold(App& app);

// One tick: a held build press sends its next add_house, then the host steps the game with the orders queued since the
// last tick, the selection and the view inputs of the camera (sim_host.cpp), then the tick's work here (after_tick).
void do_tick(App& app) {
    build_hold(app);  // slice I5: a held build press sends its next add_house
    TickGrant g;
    g.orders.swap(app.queue);
    g.sel = app.sel;
    g.blend = smooth_camera(app);  // the window draws units and shells between the last two ticks
    if (app.new_game || app.own_game || smooth_camera(app)) {
        // The owner's fixed rule (answer 7, docs/view-dependence.md 4.4 and 4.5): every house counts as drawn,
        // wherever the camera is; for a new game always, for a save in a window too.
        g.view = TickGrant::View::WholeMap;
    } else if (view_box_for(kViewWidth, kViewHeight, app.pos.col, app.pos.row, &g.box)) {
        // The houses the original draws for its 1024 x 768 view at this camera, whatever the size of our view: the
        // simulation's camera rule does not change with the window (docs/view-dependence.md, docs/interface.md I1).
        g.view = TickGrant::View::Box;
    }
    app.host.grant(std::move(g));
    TickRecord r;
    while (app.host.take(&r)) after_tick(app, r);
}

// After a tick, with the state it left: the selection without the units the step took out, the sounds and messages.
void after_tick(App& app, TickRecord& r) {
    // A network game's tick on the sim thread (slice S3): its trace lines first, as the hook of NetGame::run.
    if (r.net && app.net_trace) std::fwrite(r.net->lines.data(), 1, r.net->lines.size(), app.net_trace);
    if (DrawBlend* b = app.host.blend_of(r)) {  // where the units and shells were before it (the buffers swapped)
        std::swap(app.blend.units, b->units);
        std::swap(app.blend.shells, b->shells);
    }
    ui::selection_prune(*app.view(), app.sel);
    game_events(app, std::move(r.events));  // slice J: the sounds and messages the step raised
#ifndef __EMSCRIPTEN__
    if (r.net) app.net_game->tick_done(*r.net);  // the hash and the autosave to the server and the writer
#endif
}

// ---- input ----

// A whole step of the arrow keys in a batch script (in a window the keys move the smooth camera, SDL_AppIterate).
void scroll(App& app, int dc, int dr) { set_view(app, {app.pos.col + dc, app.pos.row + dr}); }

bool own_worker_selected(const App& app) {
    for (const sim::UnitRef& r : app.sel.units) {
        const sim::Unit* u = sim::Unit_Find(*app.view(), r);
        if (u != nullptr && (u->type == 0 || u->type == 1)) return true;
    }
    return false;
}

void queue_orders(App& app, std::vector<sim::Command> order, const std::string& what) {
    if (!what.empty()) say(app, what);
    if (order.empty()) return;
    app.queue.push_back(std::move(order));
}

void do_action(App& app, Action a, int arg) {
    if (!app.view()) return;
    switch (a) {
        case Action::Stop: queue_orders(app, ui::stop_order(app.sel), "stop"); break;
        case Action::Delete: queue_orders(app, ui::delete_key(app.sel), "Delete key"); break;
        case Action::BuildMenu:
            app.build_menu = !app.build_menu && own_worker_selected(app);
            app.build_type = -1;
            break;
        case Action::Build:
            app.build_type = arg;
            app.build_menu = false;
            say(app, "place the " + ui::house_name(arg) + ": left click on the map, right click or Esc cancels");
            break;
        case Action::Train:
            queue_orders(app, ui::train_order(*app.view(), app.sel, static_cast<std::uint16_t>(arg)),
                         "train " + ui::unit_name(arg));
            break;
        case Action::Pause:
            if (app.net_client) {  // the server's pacing, the host's only (docs/multiplayer.md)
                app.net_client->send_request(app.net_client->paused() ? net::RequestKind::Resume
                                                                      : net::RequestKind::Pause);
                break;
            }
            app.paused = !app.paused;
            app.pause_at.reset(), app.pause_drag.on = false;
            break;
        case Action::Step:
            if (app.net_client) break;
            app.paused = true;
            ++app.clock.step_once;
            break;
        case Action::Speed:
            if (app.net_client && app.view()) {
                app.net_client->send_request(net::RequestKind::Speed,
                                             std::max<std::uint32_t>(1, app.view()->speed_ms / std::max(1, arg)));
                break;
            }
            app.speed = arg;
            break;
        case Action::Help: app.show_help = !app.show_help; break;
        case Action::Players: app.show_players = !app.show_players; break;
        case Action::None: break;
    }
}

// ---- the selection panel (docs/interface.md B.1 to B.7, slice I4; ui/panel.hpp, ui/unit_panel.hpp) ----

void event_message(App& app, const std::string& text, std::uint32_t rgb);

// The string table the panels show, read once from the game folder (empty when it cannot be read: keys show).
const ui::StringTable& panel_strings(App& app) {
    if (!app.strings) {
        std::string err;
        auto file = loader(app)("Data/StringTable_eng.st", &err);
        auto table = file ? parse_string_table(*file, &err) : std::nullopt;
        if (!table) SDL_Log("panel: Data/StringTable_eng.st: %s", err.c_str());
        app.strings = table ? std::move(*table) : ui::StringTable{};
        ui::lang_localize(app.lang, *app.strings);  // shown only, never handed to the sim (docs/languages.md 5.3)
        for (const std::string& key : ui::plus_panel_keys()) {  // our texts under Entente Engine Plus (house_panel.hpp)
            (*app.strings)[key] = ours(app, key);
        }
    }
    return *app.strings;
}



ui::MarketUi& market_ui(App& app);

ui::Panel current_panel(App& app) {
    if (app.debug_hud || !app.view() || !app.renderer) return {};
    if (app.sel.units.empty()) {  // slice I6; the market's amounts slice I7
        return ui::house_panel(*app.view(), app.sel, panel_strings(app), &market_ui(app));
    }
    return ui::unit_panel(*app.view(), app.sel, panel_strings(app));
}

// The window size and the mouse point of a world point in window pixels (the panels live in window pixels).
void panel_space(const App& app, float x, float y, int* w, int* h, int* px, int* py) {
    SDL_GetRenderOutputSize(app.renderer, w, h);
    *px = static_cast<int>(x * static_cast<float>(app.zoom)), *py = static_cast<int>(y * static_cast<float>(app.zoom));
}

// ---- slice I8: the pause window and the group bar (docs/interface.md C.5, C.8; ui/game_windows.hpp) ----

void centre_on_selection(App& app);



ui::Panel window_panel(App& app, int which) {
    if (app.debug_hud || !app.view() || !app.renderer) return {};
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app.renderer, &w, &h);
    const int k = std::max(1, app.ui_scale);
    if (which == 0) {
        if (!app.paused) return ui::Panel{};
        ui::Panel p = ui::pause_window(w, h, k);
        if (app.pause_at) p.x = (*app.pause_at)[0], p.bottom = h / k - (*app.pause_at)[1];
        return p;
    }
    return ui::group_bar(*app.view(), app.sel, panel_strings(app), app.group_bar_shown, w, h, k);
}

bool over_window(App& app, float x, float y) {
    int w = 0, h = 0, px = 0, py = 0;
    for (int which = 0; which < 2; ++which) {
        const ui::Panel p = window_panel(app, which);
        if (p.widgets.empty()) continue;
        panel_space(app, x, y, &w, &h, &px, &py);
        if (ui::panel_contains(p, w, h, std::max(1, app.ui_scale), px, py)) return true;
    }
    return false;
}

// A press (down) or release on the two windows, the pause window first (it is the top one). True when one took it.
bool window_mouse(App& app, float x, float y, bool down, bool right, bool shift) {
    int w = 0, h = 0, px = 0, py = 0;
    const int k = std::max(1, app.ui_scale);
    if (down) {
        app.window_down = -1;
        for (int which = 0; which < 2; ++which) {
            const ui::Panel p = window_panel(app, which);
            if (p.widgets.empty()) continue;
            panel_space(app, x, y, &w, &h, &px, &py);
            if (ui::panel_press(p, app.window_in[which], w, h, k, px, py, right)) {
                app.window_down = which;
                if (which == 0 && !right && app.window_in[0].pressed < 0) {  // no child took it: the strip
                    ui::window_drag_press(app.pause_drag, p.x, h / k - p.bottom, p.w, ui::kDragStripPause, px / k,
                                          py / k);
                }
                return true;
            }
        }
        return false;
    }
    const int which = app.window_down;
    if (which < 0) return false;
    app.window_down = -1;
    const ui::Panel p = window_panel(app, which);
    if (p.widgets.empty()) return true;
    panel_space(app, x, y, &w, &h, &px, &py);
    const auto c = ui::panel_release(p, app.window_in[which], w, h, k, px, py, right);
    if (!c) return true;
    switch (c->action) {
        case ui::kGwPauseClose: app.paused = false, app.pause_at.reset(); break;
        case ui::kGwGroupToggle: app.group_bar_shown = !app.group_bar_shown; break;
        case ui::kGwGroup:
            app.build_menu = false;
            if (ui::group_bar_click(*app.view(), app.sel, static_cast<std::uint32_t>(c->arg), shift)) {
                centre_on_selection(app);
            }
            break;
        default: break;
    }
    return true;
}



void pause_drag_move(App& app, float x, float y) {
    if (!app.pause_drag.on) return;
    const ui::Panel p = window_panel(app, 0);
    if (!app.left_down || p.widgets.empty()) {
        app.pause_drag.on = false;
        return;
    }
    int w = 0, h = 0, px = 0, py = 0;
    const int k = std::max(1, app.ui_scale);
    panel_space(app, x, y, &w, &h, &px, &py);
    app.pause_at = ui::window_drag_to(app.pause_drag, p.w, p.h, w / k, h / k, px / k, py / k);
}

bool over_panel(App& app, float x, float y) {
    if (over_window(app, x, y)) return true;  // slice I8
    const ui::Panel p = current_panel(app);
    if (p.widgets.empty()) return false;
    int w = 0, h = 0, px = 0, py = 0;
    panel_space(app, x, y, &w, &h, &px, &py);
    return ui::panel_contains(p, w, h, app.ui_scale, px, py);
}

// ---- the minimap (docs/interface.md A.6, slice I2; ui/minimap.hpp) ----

// The minimap's place for this output, and a world point (as the clicks get them) in window pixels. Only the game
// view's interface has it, not the debug HUD.
bool minimap_on(const App& app) { return !app.debug_hud && app.renderer && app.view(); }
ui::MinimapLayout minimap_place(const App& app, float x, float y, int* px, int* py) {
    int w = 0, h = 0;
    panel_space(app, x, y, &w, &h, px, py);
    return ui::minimap_layout(w, h, std::max(1, app.ui_scale), app.minimap_shown);
}

bool over_minimap(const App& app, float x, float y) {
    int px = 0, py = 0;
    return minimap_on(app) && ui::minimap_hud_contains(minimap_place(app, x, y, &px, &py), px, py);
}



void minimap_jump(App& app, float x, float y) {
    int px = 0, py = 0;
    const ui::MinimapLayout l = minimap_place(app, x, y, &px, &py);
    const sim::Map& m = app.view()->map;
    if (!smooth_camera(app)) {
        set_view(app, ui::minimap_camera_cell(l, m.width, m.height, px, py, app.view_w, app.view_h));
        return;
    }
    const auto c = ui::minimap_camera_point(l, m.width, m.height, px, py, app.view_w, app.view_h);
    ui::camera_jump(app.cam, c[0], c[1], ui::camera_bounds(app.live.map, app.view_w, app.view_h));
    app.pos = ui::camera_view(app.cam);
}

// The state of the six buttons for the mouse point (slice I3): the advisors' switches of MyPlayer, the button under
// the mouse, the left button.
ui::HudButtonsState hud_state(const App& app, const ui::MinimapLayout& l, int px, int py) {
    ui::HudButtonsState st;
    const sim::GameState& s = *app.view();
    if (s.my_player < s.players.size()) {
        st.economic = s.players[s.my_player].field_9b2d94 != 0;
        st.military = s.players[s.my_player].field_9b2d9c != 0;
    }
    st.hover = ui::hud_button_hit(l, px, py);
    st.left_down = app.left_down;
    return st;
}



void hud_release(App& app, float x, float y) {
    const int armed = app.hud_armed;
    app.hud_armed = -1;
    if (armed < 0 || !minimap_on(app)) return;
    int px = 0, py = 0;
    const ui::MinimapLayout l = minimap_place(app, x, y, &px, &py);
    if (ui::hud_button_hit(l, px, py) != armed || app.paused) return;
    switch (static_cast<ui::HudButton>(armed)) {
        case ui::HudButton::Economic:
        case ui::HudButton::Military: {
            const bool military = armed == static_cast<int>(ui::HudButton::Military);
            const sim::GameState& s = *app.view();
            const bool on = s.my_player < s.players.size() &&
                            (military ? s.players[s.my_player].field_9b2d9c : s.players[s.my_player].field_9b2d94) != 0;
            queue_orders(app, {ui::hud_advisor_order(s.my_player, military)},
                         std::string(military ? "military" : "economic") + " advisor " + (on ? "off" : "on"));
            break;
        }
        case ui::HudButton::Arrow: app.minimap_shown = !app.minimap_shown; break;
        case ui::HudButton::PlayMenu: open_game_menu(app); break;                  // MakeMenu(4), as Esc
        case ui::HudButton::Objective: open_mission(app); break;
        case ui::HudButton::Help: open_hints(app); break;
    }
}



bool minimap_left(App& app, float x, float y) {
    if (!minimap_on(app)) return false;
    int px = 0, py = 0;
    const ui::MinimapLayout l = minimap_place(app, x, y, &px, &py);
    app.hud_armed = ui::hud_button_hit(l, px, py);
    if (app.hud_armed >= 0) return true;
    if (ui::minimap_contains(l, px, py)) minimap_jump(app, x, y);
    return ui::minimap_hud_contains(l, px, py);
}

// A right click on the minimap: the order of a right click on the world at the map point under it (ui_order, or the
// rally point of a selected house), sent once (whether the original sends it again on the release is open,
// docs/interface.md H.3).
bool minimap_right(App& app, float x, float y) {
    if (!minimap_on(app)) return false;
    int px = 0, py = 0;
    const ui::MinimapLayout l = minimap_place(app, x, y, &px, &py);
    if (!ui::minimap_contains(l, px, py)) return ui::minimap_hud_contains(l, px, py);
    const ui::WorldPoint w = ui::minimap_world_point(l, app.view()->map.width, app.view()->map.height, px, py);
    std::string what;
    auto order =
        ui::right_click(*app.view(), app.graphics ? &*app.graphics : nullptr, app.sel, ViewPos{}, w.x, w.y, &what);
    queue_orders(app, std::move(order), what + " (minimap)");
    return true;
}




bool fire_point_click(App& app, float x, float y) {
    if (app.fire_point < 0 || app.sel.units.empty() || !app.view()) return false;
    ui::WorldPoint w = ui::screen_to_world(app.pos, static_cast<int>(x), static_cast<int>(y));
    if (minimap_on(app)) {
        int px = 0, py = 0;
        const ui::MinimapLayout l = minimap_place(app, x, y, &px, &py);
        if (ui::minimap_contains(l, px, py)) {
            w = ui::minimap_world_point(l, app.view()->map.width, app.view()->map.height, px, py);
        } else if (ui::minimap_hud_contains(l, px, py)) {
            return false;
        }
    }
    const auto gas = static_cast<std::uint8_t>(app.fire_point);
    app.fire_point = -1;
    queue_orders(app, ui::fire_point_orders(*app.view(), app.sel, w.x, w.y, gas),
                 gas ? "gas charge" : "fire at a point");
    return true;
}

void panel_act(App& app, ui::PanelClick c, bool ctrl, bool shift = false) {
    if (c.action >= ui::kHpDestroy) {  // slice I6: the house panel
        ui::HousePanelResult hr =
            ui::house_panel_click(*app.view(), app.sel, c, shift, ctrl, &market_ui(app), &panel_strings(app));
        if (hr.set_exit) app.set_exit = true;
        if (hr.strike >= 0) app.strike = hr.strike, app.strike_start.reset();      // Entente Engine Plus
        if (hr.diplomacy) open_diplomacy(app);                                     // slice I8
        for (const std::string& m : hr.messages) event_message(app, m, 0xffffff);  // the market's trade lines
        if (!hr.unported.empty()) {
            say(app, hr.unported);
            event_message(app, hr.unported, 0xffffff);
            return;
        }
        queue_orders(app, std::move(hr.order), hr.what);
        return;
    }
    ui::UnitPanelResult r = ui::unit_panel_click(*app.view(), app.sel, c, ctrl);
    if (r.build >= 0) app.pontoon_start.reset();
    if (r.build >= 0) app.build_type = r.build;
    if (r.fire_point >= 0) app.fire_point = r.fire_point;
    if (!r.unported.empty()) {                             // an order the engine has not ported: say so, send nothing
        say(app, r.unported);
        event_message(app, r.unported, 0xffffff);
        return;
    }
    queue_orders(app, std::move(r.order), r.what);
}

// A press (down) or release on the panel, x and y in world pixels as the clicks get them. True when the panel took it.
bool panel_mouse(App& app, float x, float y, bool down, bool right, bool ctrl, bool shift = false) {
    const ui::Panel p = current_panel(app);
    int w = 0, h = 0, px = 0, py = 0;
    if (!p.widgets.empty()) panel_space(app, x, y, &w, &h, &px, &py);
    if (down) {
        app.panel_down = !p.widgets.empty() && ui::panel_press(p, app.panel_in, w, h, app.ui_scale, px, py, right);
        return app.panel_down;
    }
    if (!app.panel_down) return false;
    app.panel_down = false;
    if (p.widgets.empty()) return true;
    if (auto c = ui::panel_release(p, app.panel_in, w, h, app.ui_scale, px, py, right)) {
        panel_act(app, *c, ctrl, shift);
    }
    return true;
}

// A letter key reaches the panel's buttons first (WM_CHAR as event 3, docs/interface.md D.3), not with Ctrl. The
// original matches the lower case letter only, so Shift + letter pressed nothing there; here it presses the button as
// a click with Shift held (docs/bugs.md B26).
bool panel_key_press(App& app, char letter, bool ctrl, bool shift) {
    if (ctrl) return false;
    const ui::Panel p = current_panel(app);
    const auto c = ui::panel_key(p, letter);
    if (!c) return false;
    panel_act(app, *c, false, shift);
    return true;
}

// Slice I5: the build click at screen point (sx, sy), add_house (class 2 cmd 7) for the next tick. The original clears
// the build type when the house was placed and Shift is up (ExecuteCommand cmd 7), so a refused click keeps it; here
// judged by the ghost's rule (docs/interface.md B.5). An accepted cell is kept in build_held_cell.
void send_build(App& app, int sx, int sy, bool shift) {
    const sim::GameState& s = *app.view();
    const ui::WorldPoint w = ui::screen_to_world(app.pos, sx, sy);
    const auto type = static_cast<std::uint16_t>(app.build_type);
    std::int32_t cx = 0, cy = 0;
    const bool ok = ui::pick_cell(s, w.x, w.y, &cx, &cy) && ui::can_place(s, app.sel.player, cx, cy, type);
    queue_orders(app, ui::build_order(s, app.sel, w.x, w.y, type),
                 ui::house_name(type) + (ok ? ": building" : ": the site looks blocked, the game decides"));
    if (ok && !ui::build_ghost(s, app.sel.player, type, cx, cy).refused) {
        app.build_held_cell = {cx, cy};
        if (!shift) app.build_type = -1, app.build_held = false;
    }
    if (app.debug_hud) app.build_type = -1, app.build_held = false;
}






void build_hold(App& app) {
    if (!app.build_held || app.build_type < 0 || app.debug_hud || !app.view()) return;
    const sim::GameState& s = *app.view();
    if (app.sel.units.empty() || app.sel.player >= s.players.size()) return;
    const float x = app.mouse_x, y = app.mouse_y;
    if (in_panel(app, y) || over_panel(app, x, y) || over_minimap(app, x, y)) return;
    const bool shift = app.window ? (SDL_GetModState() & SDL_KMOD_SHIFT) != 0 : app.build_hold_shift;
    const auto type = static_cast<std::uint16_t>(app.build_type);
    const ui::WorldPoint w = ui::screen_to_world(app.pos, static_cast<int>(x), static_cast<int>(y));
    std::int32_t cx = 0, cy = 0;
    if (!ui::pick_cell(s, w.x, w.y, &cx, &cy)) return;
    if (std::array<std::int32_t, 2>{cx, cy} == app.build_held_cell) return;
    if (ui::build_ghost(s, app.sel.player, type, cx, cy).refused) return;
    send_build(app, static_cast<int>(x), static_cast<int>(y), shift);
}

void left_click(App& app, float x, float y, bool shift) {
    if (window_mouse(app, x, y, true, false, shift)) return;  // slice I8: the pause window, the group bar
    if (panel_mouse(app, x, y, true, false, false)) return;   // slice I4: the panel's press
    if (minimap_left(app, x, y)) return;                      // slice I2
    for (const Button& b : app.buttons) {
        if (b.enabled && x >= b.r.x && x < b.r.x + b.r.w && y >= b.r.y && y < b.r.y + b.r.h) {
            do_action(app, b.action, b.arg);
            return;
        }
    }
    const SDL_FPoint pt{x, y};
    if (in_panel(app, y) || SDL_PointInRectFloat(&pt, &app.popup)) return;
    app.fire_point = -1;
    const sim::GameState& s = *app.view();
    const int sx = static_cast<int>(x), sy = static_cast<int>(y);
    if (app.strike >= 0) {  // Entente Engine Plus: the call-in strike's target (one press, or two for a line)
        const ui::WorldPoint w = ui::screen_to_world(app.pos, sx, sy);
        if (sim::Strike_Info(static_cast<std::uint8_t>(app.strike)).two_points && !app.strike_start) {
            app.strike_start = {w.x, w.y};
            say(app, "strike: now left click where it ends");
            return;
        }
        const std::array<std::int32_t, 2> a = app.strike_start ? *app.strike_start : std::array{w.x, w.y};
        queue_orders(app, {ui::strike_order(app.sel.player, app.strike, a[0], a[1], w.x, w.y)}, "call-in strike");
        app.strike = -1, app.strike_start.reset();
        return;
    }
    if (app.set_exit) {
        app.set_exit = false;
        std::string what;
        auto order = ui::right_click(s, app.graphics ? &*app.graphics : nullptr, app.sel, app.pos, sx, sy, &what);
        queue_orders(app, std::move(order), what);
        return;
    }
    if (app.build_type == ui::kPontoonType) {  // the pontoon bridge's two presses (docs/transports.md 19)


        const ui::WorldPoint w = ui::screen_to_world(app.pos, sx, sy);
        std::int32_t cx = 0, cy = 0;
        if (!app.pontoon_start) {
            app.pontoon_start = {w.x, w.y};
            say(app, "pontoon bridge: now left click where it ends");
        } else if (w.x != (*app.pontoon_start)[0] && ui::pick_cell(s, w.x, w.y, &cx, &cy)) {
            const auto [x0, y0] = *app.pontoon_start;
            const sim::PontoonPlan plan = ui::pontoon_preview(s, app.sel.player, x0, y0, cx, cy);
            if (plan.refusal != sim::PontoonPlan::Refusal::None) {  // ours (B93, B94): the mode stays for a new line
                event_message(app, ui::pontoon_refusal_text(app.lang, plan), 0xffffff);
                app.pontoon_start.reset();
                return;
            }
            queue_orders(app, {ui::pontoon_order(app.sel.player, x0, y0, cx, cy)},
                         "pontoon bridge: " + std::to_string(plan.laid.size()) + " sections");
            app.build_type = -1, app.pontoon_start.reset();
        }
        return;
    }
    if (app.build_type >= 0) {  // the build click: class 2 cmd 7 for the next tick (docs/building.md 3)
        app.build_held = true, app.build_hold_shift = shift, app.build_held_cell = {-1, -1};
        send_build(app, sx, sy, shift);
        return;
    }




    if (!shift) ui::clear_selection(app.sel);
    app.build_menu = false;
    const WorldGraphics* g = app.graphics ? &*app.graphics : nullptr;
    if (const auto u = ui::unit_at(s, g, app.pos, sx, sy, app.sel.player)) {


        const sim::Unit* mine = sim::Unit_Find(s, *u);
        if (mine && sim::Unit_Type(s, u->player, *mine).is_air == 0) {
            if (const auto plane = ui::other_aircraft_at(s, g, app.pos, sx, sy, app.sel.player)) {
                if (app.sel.units.empty()) {
                    event_message(app, ui::unit_info_line(s, panel_strings(app), app.sel.player, *plane), 0xffffff);
                }
                return;
            }
        }
        ui::toggle_unit(app.sel, *u);
        app.sel.house.reset();
        const sim::Unit* su = sim::Unit_Find(s, *u);
        if (su && app.sel.player == s.my_player) {
            audio::audio_event(app.audio.get(), ui::unit_sound_event(s, app.sel.player, *su, 0, 6), listener(app));
        }
        return;
    }

    if (const auto o = ui::info_unit_at(s, g, app.pos, sx, sy, app.sel.player)) {
        if (app.sel.units.empty()) {
            event_message(app, ui::unit_info_line(s, panel_strings(app), app.sel.player, *o), 0xffffff);
        }
        return;
    }
    const auto h = ui::house_at(s, g, app.pos, sx, sy);
    if (!h) return;


    if (h->player != app.sel.player) {
        if (ui::point_in_fog(s, app.pos, sx, sy)) return;
        if (app.sel.units.empty()) {
            event_message(app, ui::house_info_line(s, panel_strings(app), app.sel.player, *h), 0xffffff);
        }
    }
    app.sel.house = h;
    const sim::House& house = s.players[h->player].houses[h->slot];
    if (h->player == s.my_player && house.is_destroyed == 0 && app.sel.units.empty()) {
        audio::audio_event(app.audio.get(), ui::house_sound_event(s, h->player, house, 1, 8), listener(app));
    }
    if (h->player == app.sel.player && house.is_destroyed == 0 && app.sel.units.empty() &&
        std::none_of(app.sel.flagged.begin(), app.sel.flagged.end(),
                     [&](const sim::HouseRef& r) { return r.slot == h->slot; })) {
        app.sel.flagged.push_back(*h);
        ++app.sel.ring_resets;
    }
}

void box_select(App& app, float x0, float y0, float x1, float y1, bool shift, bool live = false) {
    auto list = ui::units_in_rect(*app.view(), app.pos, static_cast<int>(x0), static_cast<int>(y0),
                                  static_cast<int>(x1), static_cast<int>(y1), app.sel.player);

    if (list.empty() && !shift) {
        if (auto u = ui::unit_at(*app.view(), app.graphics ? &*app.graphics : nullptr, app.pos, static_cast<int>(x1),
                                 static_cast<int>(y1), app.sel.player)) {
            list.push_back(*u);
        }
    }


    if (!shift) ui::clear_selection(app.sel);
    ui::select_units(app.sel, list);
    app.sel.house.reset();
    app.build_menu = false;
    if (!live) say(app, std::to_string(app.sel.units.size()) + " units selected");
}




void box_live(App& app) {
    if (!app.dragging || !app.started) return;
    const float x0 = app.drag_x0 - static_cast<float>(view_x(app.pos)),
                y0 = app.drag_y0 - static_cast<float>(view_y(app.pos));
    if (std::abs(app.mouse_x - x0) < 5 && std::abs(app.mouse_y - y0) < 5) return;
    box_select(app, x0, y0, app.mouse_x, app.mouse_y, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0, true);
}

// A right press. True when it went to the world (the order at the press point is queued); the caller may then start
// a facing drag (slice I9, right_release).
bool right_click(App& app, float x, float y) {
    if (window_mouse(app, x, y, true, true, false)) return false;  // slice I8
    if (panel_mouse(app, x, y, true, true, false)) return false;   // slice I4: the panel's press
    if (app.strike >= 0) {                                         // Entente Engine Plus: the strike's target mode
        app.strike = -1, app.strike_start.reset();
        say(app, "strike cancelled");
        return false;
    }
    if (app.build_type >= 0) {
        app.build_type = -1;
        say(app, "building cancelled");
        return false;
    }
    if (!in_panel(app, y) && fire_point_click(app, x, y)) return false;  // cursor mode 5: order_point
    if (in_panel(app, y) || minimap_right(app, x, y)) return false;      // slice I2: the minimap
    std::string what;
    auto order = ui::right_click(*app.view(), app.graphics ? &*app.graphics : nullptr, app.sel, app.pos,
                                 static_cast<int>(x), static_cast<int>(y), &what);
    queue_orders(app, std::move(order), what);
    return true;
}



void right_press(App& app, float x, float y) {
    app.right_held = right_click(app, x, y);
    app.rdrag_x0 = x + static_cast<float>(view_x(app.pos)), app.rdrag_y0 = y + static_cast<float>(view_y(app.pos));
}




void right_release(App& app, float x, float y) {
    if (!app.right_held) return;
    app.right_held = false;
    if (!app.view() || app.build_type >= 0) return;
    const float mx = x + static_cast<float>(view_x(app.pos)), my = y + static_cast<float>(view_y(app.pos));
    if (auto c = ui::facing_order(*app.view(), app.sel, app.rdrag_x0, app.rdrag_y0, mx, my)) {
        queue_orders(app, {std::move(*c)}, "facing order");
    }
}

// An SDL key as the game reads it: letters by the layout (as the virtual keys), the top row's digits and `~` by their
// place (VK_OEM_3 is the key left of 1 on any layout).
ui::KeyPress key_press(SDL_Keycode k, SDL_Scancode sc, SDL_Keymod mod) {
    ui::KeyPress p;
    p.ctrl = (mod & SDL_KMOD_CTRL) != 0, p.shift = (mod & SDL_KMOD_SHIFT) != 0;
    if (k >= SDLK_A && k <= SDLK_Z) {
        p.key = ui::Key::Letter, p.ch = static_cast<char>('a' + (k - SDLK_A));
    } else if (k >= SDLK_0 && k <= SDLK_9) {
        p.key = ui::Key::Digit, p.ch = static_cast<char>('0' + (k - SDLK_0));
    } else if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) {
        p.key = ui::Key::Digit, p.ch = sc == SDL_SCANCODE_0 ? '0' : static_cast<char>('1' + (sc - SDL_SCANCODE_1));
    } else if (sc == SDL_SCANCODE_GRAVE || k == SDLK_GRAVE) {
        p.key = ui::Key::Grave;
    } else {
        switch (k) {
            case SDLK_TAB: p.key = ui::Key::Tab; break;
            case SDLK_SPACE: p.key = ui::Key::Space; break;
            case SDLK_ESCAPE: p.key = ui::Key::Escape; break;
            case SDLK_PAUSE: p.key = ui::Key::Pause; break;
            case SDLK_DELETE: p.key = ui::Key::Delete; break;
            case SDLK_F1: p.key = ui::Key::F1; break;
            case SDLK_F2: p.key = ui::Key::F2; break;
            case SDLK_F5: p.key = ui::Key::F5; break;  // slice H
            case SDLK_F6: p.key = ui::Key::F6; break;
            case SDLK_F12: p.key = ui::Key::F12; break;
            case SDLK_KP_PLUS: p.key = ui::Key::KpPlus; break;
            case SDLK_KP_MINUS: p.key = ui::Key::KpMinus; break;
            case SDLK_LEFT: p.key = ui::Key::Left; break;
            case SDLK_RIGHT: p.key = ui::Key::Right; break;
            case SDLK_UP: p.key = ui::Key::Up; break;
            case SDLK_DOWN: p.key = ui::Key::Down; break;
            case SDLK_PERIOD: p.key = ui::Key::Period; break;
            case SDLK_MINUS: p.key = ui::Key::Minus; break;  // ours, the zoom (docs/decisions.md 41)
            case SDLK_EQUALS:
            case SDLK_PLUS: p.key = ui::Key::Plus; break;
            case SDLK_BACKSPACE: p.key = ui::Key::Backspace; break;
            default: break;
        }
    }
    return p;
}

// After a key changed the selection: the debug HUD's build menu closes, the HUD says how many.
void key_selected(App& app, const std::string& what) {
    app.build_menu = false;
    say(app, what + ": " + std::to_string(app.sel.units.size()) + " units selected");
}


void centre_on_selection(App& app) {
    if (const auto c = ui::centre_cell(*app.view(), app.sel, &app.tab_index)) {
        set_view(app, ui::centre_view(ui::view_cells(app.view_w, app.view_h), (*c)[0], (*c)[1]));
    }
}

// Esc with nothing to cancel (WM_CHAR 0x1b): a paused game gets the pause window's Close (unpause); otherwise the
// original opens the in-game menu (MakeMenu 4) and pauses.
void escape_menu(App& app) {
    if (app.paused) {
        do_action(app, Action::Pause, 0);
    } else {
        open_game_menu(app);
    }
}

// The game's keys (docs/interface.md D.3, ui/select_keys.hpp): a letter reaches the panel's buttons first, then the
// original's table; with the developer switch (F12, the debug HUD) the milestone 3 keys come first where they clash.
// Keys do nothing while a menu is up (SDL_AppEvent hands them to the front-end then).
void key(App& app, const ui::KeyPress& k, bool repeat) {
    if (k.key == ui::Key::Letter && panel_key_press(app, k.ch, k.ctrl, k.shift)) return;  // slice I4
    const ui::KeyAction a = ui::game_key(k, app.debug_hud);
    if (repeat && !a.repeats) return;
    const sim::GameState& s = *app.view();
    const ui::ScreenBox box{view_x(app.pos), view_y(app.pos), app.view_w, app.view_h};
    using A = ui::KeyAct;
    switch (a.act) {
        case A::ScrollLeft:
            if (!smooth_camera(app)) scroll(app, -1, 0);
            break;
        case A::ScrollRight:
            if (!smooth_camera(app)) scroll(app, 1, 0);
            break;
        case A::ScrollUp:
            if (!smooth_camera(app)) scroll(app, 0, -2);
            break;
        case A::ScrollDown:
            if (!smooth_camera(app)) scroll(app, 0, 2);
            break;
        case A::SelectAllMap:
            ui::select_all_map(s, app.sel);
            key_selected(app, "Ctrl+A");
            break;
        case A::SelectAllScreen:
            ui::select_all_screen(s, app.sel, box);
            key_selected(app, "A");
            break;
        case A::SelectWorkersScreen:
            ui::select_workers_screen(s, app.sel, box);
            key_selected(app, "Shift+A");
            break;
        case A::SelectTypesMap:
            ui::select_types_map(s, app.sel);
            key_selected(app, "Ctrl+Z");
            break;
        case A::SelectTypesScreen:
            ui::select_types_screen(s, app.sel, box, a.arg != 0);
            key_selected(app, "Z");
            break;
        case A::IdleWorkers:
            if (ui::select_idle_workers(s, app.sel)) key_selected(app, "Ctrl+F");
            break;
        case A::HouseKey:
            if (app.sel.units.empty() && !app.sel.house && app.sel.flagged.empty() &&
                ui::select_house_type(s, app.sel, a.arg)) {
                say(app, ui::house_name(a.arg) + " selected");
            }
            break;
        case A::Market:
            if (const auto h = ui::house_panel_house(s, app.sel); h && s.players[h->player].houses[h->slot].type == 8) {
                panel_act(app, ui::PanelClick{ui::kHpMarketClear, 0}, false);
            }
            break;
        case A::Minimap:  // slice I2: not while the panel shows workers (`m` is the market's build key there)
            if (app.debug_hud || !own_worker_selected(app)) app.minimap_shown = !app.minimap_shown;
            break;
        case A::Tab:
            if (ui::tab_key(s, app.sel, &app.tab_index, a.arg != 0)) centre_on_selection(app);
            break;
        case A::Centre: centre_on_selection(app); break;
        case A::Pause:
        case A::DevPause: do_action(app, Action::Pause, 0); break;
        case A::Delete:
            if (!app.paused || app.debug_hud) do_action(app, Action::Delete, 0);
            break;
        case A::ZoomIn:  // ours (docs/decisions.md 41): about the screen's centre
        case A::ZoomOut:
        case A::ZoomReset: {
            int ow = 0, oh = 0;
            SDL_GetRenderOutputSize(app.renderer, &ow, &oh);
            zoom_by(app, a.act == A::ZoomIn ? 1 : -1, ow / 2.0, oh / 2.0, a.act == A::ZoomReset, !smooth_camera(app));
            break;
        }
        case A::Health: app.show_health = !app.show_health; break;
        case A::SeeThrough:
            app.live.view.see_through = !app.live.view.see_through;
            app.static_dirty = true;
            break;
        case A::Mission: open_mission(app); break;
        case A::QuickSave: quick_save(app); break;  // slice H: F5
        case A::QuickLoad: quick_load(app); break;  // slice H: F6
        case A::DevHelp: do_action(app, Action::Help, 0); break;
        case A::Escape:
                         // selection (that was the debug HUD's Esc).
            if (app.build_type >= 0 || app.set_exit || app.fire_point >= 0 || app.strike >= 0) {
                app.build_type = -1;
                app.set_exit = false;                       // ours: the Set_Exit cursor mode too
                app.fire_point = -1;                        // and cursor mode 5
                app.strike = -1, app.strike_start.reset();  // and Plus's strike target mode
            } else {
                escape_menu(app);
            }
            break;
        case A::GroupSelect:
            if (ui::select_group(s, app.sel, a.arg % 10, a.arg >= 10)) centre_on_selection(app);
            key_selected(app, "group " + std::to_string(a.arg % 10));
            break;
        case A::GroupBind:
            if (auto c = ui::bind_group_order(s, app.sel, a.arg)) {
                queue_orders(app, {std::move(*c)}, "group " + std::to_string(a.arg) + " bound");
            }
            break;
        case A::SpeedUp:  // ours: the original's numpad + and - are debug keys (tick period - / + 10 ms)
        case A::SpeedDown:
            app.speed = a.act == A::SpeedUp ? std::min(4, app.speed * 2) : std::max(1, app.speed / 2);
            say(app, "game speed x" + std::to_string(app.speed));
            if (app.net_client) {  // a request to the server (the host's only)
                app.net_client->send_request(net::RequestKind::Speed,
                                             std::max(1u, app.view()->speed_ms / static_cast<unsigned>(app.speed)));
            }
            break;
        case A::DevSwitch:  // ours, a developer key: the debug HUD and back (the original has no F12)
            app.debug_hud = !app.debug_hud;
            break;
        case A::DevStep: do_action(app, Action::Step, 0); break;
        case A::DevSpeed: do_action(app, Action::Speed, a.arg); break;
        case A::DevStop: do_action(app, Action::Stop, 0); break;
        case A::DevBuild: do_action(app, Action::BuildMenu, 0); break;
        case A::DevPlayers: do_action(app, Action::Players, 0); break;
        case A::DevEscape:
            app.set_exit = false;  // slice I6
            app.strike = -1, app.strike_start.reset();
            if (app.build_type >= 0 || app.build_menu) {
                app.build_type = -1;
                app.build_menu = false;
            } else {
                ui::clear_selection(app.sel);
            }
            break;
        case A::None: break;
    }
}

// ---- drawing ----

// The debug HUD's text (F12, a developer screen) in SDL's debug font, which has ASCII only: English on purpose, as the
// logs and the command line help (docs/languages.md 6, decisions.md 1); not in engine/lang.
void text(SDL_Renderer* r, float x, float y, const std::string& s, Uint8 cr = 230, Uint8 cg = 230, Uint8 cb = 230) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderDebugText(r, x, y, s.c_str());
}

void panel(SDL_Renderer* r, float x, float y, float w, float h, Uint8 alpha = 200) {
    SDL_SetRenderDrawColor(r, 16, 18, 24, alpha);
    const SDL_FRect f{x, y, w, h};
    SDL_RenderFillRect(r, &f);
}

std::string fmt(const char* f, double v) {
    char b[64];
    std::snprintf(b, sizeof b, f, v);
    return b;
}

const char* nation_name(int n) {
    static const char* const kNames[] = {"Russia", "England", "France", "Germany", "Austria-Hungary", "neutral"};
    return n >= 0 && n < 6 ? kNames[n] : "?";
}

// Buttons flow left to right from x0 and wrap before xmax; each is as wide as its label.
struct Flow {
    float x0, x, y, xmax;
};

void add_button(App& app, Flow& f, const std::string& label, Action a, int arg = 0) {
    const float w = 8.0f * static_cast<float>(label.size()) + 10;
    if (f.x > f.x0 && f.x + w > f.xmax) f.x = f.x0, f.y += 18;
    app.buttons.push_back(Button{SDL_FRect{f.x, f.y, w, 15}, label, a, arg, true});
    f.x += w + 4;
}

void new_row(Flow& f) {
    if (f.x > f.x0) f.x = f.x0, f.y += 18;
}

// The buttons of this frame (rebuilt every frame from the selection).
void layout_buttons(App& app) {
    app.buttons.clear();
    const sim::GameState& s = *app.view();
    Flow f{340, 340, kPanelTop + 16, 790};
    add_button(app, f, app.paused ? "Run (Space)" : "Pause (Space)", Action::Pause);
    add_button(app, f, "Step (N)", Action::Step);
    for (int k : {1, 2, 4}) add_button(app, f, "x" + std::to_string(k), Action::Speed, k);
    new_row(f);
    if (!app.sel.units.empty()) {
        add_button(app, f, "Stop (S)", Action::Stop);
        add_button(app, f, "Delete (Del)", Action::Delete);
        if (own_worker_selected(app))
            add_button(app, f, app.build_menu ? "Build (B): close" : "Build (B)", Action::BuildMenu);
        new_row(f);
    }
    app.popup = SDL_FRect{};
    if (app.build_menu && own_worker_selected(app)) {  // a box above the panel and the message line
        const std::size_t first = app.buttons.size();
        Flow m{346, 346, 0, 1010};
        for (std::uint16_t t : ui::buildable_houses(s, app.sel.player)) {
            add_button(app, m, ui::house_name(t), Action::Build, t);
        }
        const float h = m.y + 15 + 22, top = kPanelTop - 16 - h;
        for (std::size_t i = first; i < app.buttons.size(); ++i) app.buttons[i].r.y += top + 18;
        app.popup = SDL_FRect{340, top, kW - 340, h};
    }
    if (app.sel.house && app.sel.house->player == app.sel.player) {
        const sim::House& h = s.players[app.sel.player].houses[app.sel.house->slot];
        add_button(app, f, "Delete (Del)", Action::Delete);
        new_row(f);
        if (h.field_020 == 0 && h.is_destroyed == 0) {
            const auto& ht = sim::House_Type(s, app.sel.player, h);
            for (std::uint16_t t : ht.produce) {
                if (t == 0xffff) break;
                add_button(app, f, "Train " + ui::unit_name(t), Action::Train, t);
            }
        }
    }
}

// The marks in the world (slice I9, docs/interface.md D.2, ui/world_marks) over the finished picture: with the `~`
// switch the house bars, and the box select rectangle, in one batch. The units' corners, health, reload and group
// marks are drawn into the world picture with their units (draw_world, slice N).
void draw_world_marks(App& app) {
    if (!app.marks_gfx) app.marks_gfx = ui::marks_graphics_open(app.renderer, loader(app));
    app.marks.clear();  // the units' marks are in the world picture (draw_world, slice N)
    if (app.show_health) {
        ui::world_marks_houses(*app.graphics, app.live, *app.view(), app.pos, app.view_w, app.view_h, app.house_boxes,
                               &app.marks);
    }
    if (app.dragging) {
        ui::world_marks_box(app.drag_x0 - static_cast<float>(view_x(app.pos)),
                            app.drag_y0 - static_cast<float>(view_y(app.pos)), app.mouse_x, app.mouse_y, &app.marks);
    }
    if (app.right_held && !app.sel.units.empty() && !app.debug_hud) {
        const float px = app.rdrag_x0 - static_cast<float>(view_x(app.pos)),
                    py = app.rdrag_y0 - static_cast<float>(view_y(app.pos));
        if (ui::facing_drag_long(px, py, app.mouse_x, app.mouse_y)) {
            ui::world_marks_facing_arrow(px, py, app.mouse_x, app.mouse_y,
                                         ui::facing_selection_in_group(*app.view(), app.sel), &app.marks);
        }
    }
    ui::marks_draw(*app.marks_gfx, app.marks);
}






void draw_fit_cells(App& app) {
    const sim::GameState& s = *app.view();
    const sim::Map& m = s.map;
    const bool ctrl = app.window ? (SDL_GetModState() & SDL_KMOD_CTRL) != 0 : app.script_ctrl;
    if (!ctrl || app.build_type < 0 || app.build_type == ui::kPontoonType || app.debug_hud || app.sel.units.empty() ||
        app.sel.player >= s.players.size() || m.width == 0 || m.height == 0) {
        return;
    }
    const int vx = view_x(app.pos), vy = view_y(app.pos);
    // The cells whose diamond may show: columns of 96 pixels, rows of 24, a cell's picture lifted by its height (255).
    ui::fit_cells_update(app.fit_cells, s, app.sel.player, static_cast<std::uint16_t>(app.build_type), vx / 96 - 1,
                         vy / 24 - 2, (vx + app.view_w) / 96 + 1, (vy + app.view_h + 255) / 24 + 2);
    const std::size_t w = m.width, count = m.heights.size();
    auto height = [&](std::size_t i, std::size_t fallback) {
        return static_cast<float>(m.heights[i < count ? i : fallback]);
    };
    std::vector<SDL_Vertex> v;
    std::vector<int> idx;
    const SDL_FColor green{0x33 / 255.0f, 0xcc / 255.0f, 0x33 / 255.0f, 1.0f};
    for (std::int32_t cy = std::max(0, app.fit_cells.y0); cy <= app.fit_cells.y1; ++cy) {
        for (std::int32_t cx = app.fit_cells.x0; cx <= app.fit_cells.x1; ++cx) {
            if (!app.fit_cells.fits(cx, cy)) continue;
            const std::size_t i = static_cast<std::size_t>(cy) * w + static_cast<std::size_t>(cx);
            const std::size_t up =
                i + static_cast<std::size_t>(cy & 1) >= w ? i + static_cast<std::size_t>(cy & 1) - w : i;
            const float x = static_cast<float>(cx * 96 + (cy & 1 ? 48 : 0) - vx);
            const float y = static_cast<float>(cy * 24 - vy);
            const int base = static_cast<int>(v.size());
            v.push_back({{x - 48, y - height(i, i)}, green, {}});
            v.push_back({{x, y - 24 - height(up, i)}, green, {}});
            v.push_back({{x, y + 24 - height(i + w + static_cast<std::size_t>(cy & 1), i)}, green, {}});
            v.push_back({{x + 48, y - height(i + 1, i)}, green, {}});
            for (int k : {0, 1, 2, 1, 2, 3}) idx.push_back(base + k);
        }
    }
    if (v.empty()) return;
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_MUL);
    SDL_RenderGeometry(app.renderer, nullptr, v.data(), static_cast<int>(v.size()), idx.data(),
                       static_cast<int>(idx.size()));
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
}

void draw_selection(App& app) {
    SDL_Renderer* r = app.renderer;
    const sim::GameState& s = *app.view();
    draw_fit_cells(app);
    draw_world_marks(app);
    if (!ui::house_panel_house(s, app.sel)) {  // slice I6: Set_Exit needs the house panel (and Plus's strike mode)
        app.set_exit = false;
        app.strike = -1, app.strike_start.reset();
    }
    if (const auto flag = ui::rally_flag_point(s, app.sel); flag && !app.debug_hud) {
        if (!app.rally_gfx) app.rally_gfx = ui::rally_graphics_open(r, loader(app));
        const std::size_t c = s.players[s.my_player].colour;
        const auto& rgb = app.graphics->player_rgb;
        const std::uint32_t tint =
            c < rgb.size() ? std::uint32_t{rgb[c][0]} << 16 | rgb[c][1] << 8 | rgb[c][2] : 0xffffff;
        ui::rally_flag_draw(*app.rally_gfx, (*flag)[0] - static_cast<float>(view_x(app.pos)),
                            (*flag)[1] - static_cast<float>(view_y(app.pos)), app.flag_phase.sprite(), tint);
        app.flag_phase.advance(app.frame_ms, std::max<double>(1, s.speed_ms));
    }
    if (app.build_type >= 0 && app.sel.units.empty()) app.build_type = -1;
    if (app.build_type != ui::kPontoonType) app.pontoon_start.reset();
    if (app.build_type == ui::kPontoonType && !app.debug_hud && app.view()) {
        const ui::WorldPoint w =
            ui::screen_to_world(app.pos, static_cast<int>(app.mouse_x), static_cast<int>(app.mouse_y));
        std::int32_t cx = 0, cy = 0;
        if (!app.ghost_gfx) app.ghost_gfx = ui::ghost_graphics_open(r);
        if (app.pontoon_start && ui::pick_cell(s, w.x, w.y, &cx, &cy)) {
            const auto [x0, y0] = *app.pontoon_start;
            const sim::PontoonPlan plan = ui::pontoon_preview(s, app.sel.player, x0, y0, cx, cy);
            if (plan.refusal == sim::PontoonPlan::Refusal::None) {
                ui::pontoon_preview_draw(*app.ghost_gfx, *app.graphics, plan.laid, view_x(app.pos), view_y(app.pos));
            } else {  // ours (B93, B94): the line it would be, the cells that refuse it red
                std::vector<sim::PontoonCell> ok;
                for (const sim::PontoonCell& c : plan.would) {
                    if (std::none_of(plan.refused.begin(), plan.refused.end(),
                                     [&](const sim::PontoonCell& r) { return r.x == c.x && r.y == c.y; }))
                        ok.push_back(c);
                }
                ui::pontoon_preview_draw(*app.ghost_gfx, *app.graphics, ok, view_x(app.pos), view_y(app.pos));
                ui::pontoon_preview_draw(*app.ghost_gfx, *app.graphics, plan.refused, view_x(app.pos), view_y(app.pos),
                                         true);
            }
        }
    } else if (app.build_type >= 0 && !app.debug_hud && app.view()) {
        const ui::WorldPoint w =
            ui::screen_to_world(app.pos, static_cast<int>(app.mouse_x), static_cast<int>(app.mouse_y));
        std::int32_t cx = 0, cy = 0;
        if (!app.ghost_gfx) app.ghost_gfx = ui::ghost_graphics_open(r);
        if (ui::pick_cell(s, w.x, w.y, &cx, &cy) && app.sel.player < s.players.size()) {
            const auto type = static_cast<std::uint16_t>(app.build_type);
            const ui::BuildGhost g = ui::build_ghost(s, app.sel.player, type, cx, cy);
            ui::ghost_draw(*app.ghost_gfx, *app.graphics, loader(app), g, s.players[app.sel.player].nation, type,
                           ui::ghost_colour(g), view_x(app.pos), view_y(app.pos));
        }
    }
    if (app.build_type >= 0 && app.debug_hud && !in_panel(app, app.mouse_y)) {  // the cell, green where it fits
        const ui::WorldPoint w =
            ui::screen_to_world(app.pos, static_cast<int>(app.mouse_x), static_cast<int>(app.mouse_y));
        std::int32_t cx = 0, cy = 0;
        if (ui::pick_cell(s, w.x, w.y, &cx, &cy)) {
            const bool ok = ui::can_place(s, app.sel.player, cx, cy, static_cast<std::uint16_t>(app.build_type));
            auto diamond = [&](std::int32_t qx, std::int32_t qy) {
                const std::int32_t hx = std::clamp(qx, 0, s.map.width - 1), hy = std::clamp(qy, 0, s.map.height - 1);
                const float x = static_cast<float>(qx * 96 + (qy % 2 ? 48 : 0) - view_x(app.pos));
                const float y = static_cast<float>(qy * 24 - view_y(app.pos) -
                                                   s.map.heights[static_cast<std::size_t>(hy) * s.map.width + hx]);
                const SDL_FPoint d[5] = {{x - 48, y}, {x, y - 24}, {x + 48, y}, {x, y + 24}, {x - 48, y}};
                SDL_RenderLines(r, d, 5);
                return SDL_FPoint{x, y};
            };
            SDL_SetRenderDrawColor(r, ok ? 40 : 160, ok ? 160 : 40, 40, 255);
            for (const auto& c :
                 ui::site_cells(s, app.sel.player, cx, cy, static_cast<std::uint16_t>(app.build_type))) {
                diamond(c[0], c[1]);
            }
            SDL_SetRenderDrawColor(r, ok ? 60 : 255, ok ? 255 : 60, 60, 255);
            const auto [x, y] = diamond(cx, cy);
            text(r, x + 52, y - 4, ok ? "can build" : "cannot build", ok ? 120 : 255, ok ? 255 : 120, 120);
        }
    }
}

// F1 or H in the debug HUD: its keys and the mouse. The game view has the original's windows instead (slice I8: F1 the
// mission window, the pause window; our key list is the last page of the hints window).
void draw_help(App& app) {
    SDL_Renderer* r = app.renderer;
    if (!app.show_help || !app.debug_hud) return;
    static const char* const kDebug[] = {
        "How to play (the new engine, debug HUD; F12 back)",
        "",
        "Left click a unit: select it (Shift: add)",
        "Left drag: select your units in the box",
        "Left click a house: select it (train buttons below)",
        "Right click the ground: move there",
        "Right click your house: go there (build, repair, go in)",
        "Right click an enemy: attack",
        "Right click with only a house selected: rally point",
        "",
        "Arrow keys or mouse at the window edge: scroll",
        "Space or P: pause / run      N: one tick",
        "1, 2, 3: speed x1, x2, x4",
        "S: stop      Delete: the Delete key      B: build menu",
        "Esc: cancel / clear the selection",
        "Tab: player table      F1 or H: this help",
        "The original's A, Z, Ctrl+digit, house keys work here too.",
        "",
        "Unported calls (bottom right) count the original's code",
        "that this game needed and the new engine does not have yet.",
    };
    const std::size_t n = std::size(kDebug);
    const char* const* lines = kDebug;
    const float x = 212.0f, y = 120.0f;
    panel(r, x, y, 600, 16.0f * static_cast<float>(n) + 10, 235);
    for (std::size_t i = 0; i < n; ++i) {
        text(r, x + 10, y + 8 + 16.0f * static_cast<float>(i), lines[i], i == 0 ? 255 : 230, 230, i == 0 ? 160 : 230);
    }
}

void draw_hud(App& app) {
    SDL_Renderer* r = app.renderer;
    const sim::GameState& s = *app.view();
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    // Top bar: tick, speed, my resources.
    panel(r, 0, 0, kW, 14);
    const sim::Player& me = s.players[app.sel.player];
    std::string top = "Tick " + std::to_string(s.tick) + (app.paused ? "  PAUSED" : "  x" + std::to_string(app.speed)) +
                      "   Player " + std::to_string(app.sel.player) + " (" + nation_name(me.nation) + ")" + "  Gold " +
                      fmt("%.0f", me.gold) + "  Wood " + fmt("%.0f", me.wood) + "  Iron " + fmt("%.0f", me.iron) +
                      "  Oil " + fmt("%.0f", me.oil) + "  Food " + fmt("%.0f", me.food) + "   F1 help";
    text(r, 4, 3, top);
    if (app.show_players) {
        const float h = 4 + kLine * static_cast<float>(s.players.size() + 1);
        panel(r, 0, 14, 580, h, 170);
        char head[160];
        std::snprintf(head, sizeof head, "%-17s %6s %6s %6s %6s %6s %5s %6s", "Player", "Gold", "Wood", "Iron", "Oil",
                      "Food", "Units", "Houses");
        text(r, 14, 17, head, 180, 200, 255);
        for (std::size_t p = 0; p < s.players.size(); ++p) {
            const sim::Player& pl = s.players[p];
            int houses = 0;
            for (const sim::House& h2 : pl.houses) houses += h2.hp > 0.0f ? 1 : 0;
            char line[160];
            std::snprintf(line, sizeof line, "%zu %-15s %6.0f %6.0f %6.0f %6.0f %6.0f %5u %6d%s", p,
                          nation_name(pl.nation), pl.gold, pl.wood, pl.iron, pl.oil, pl.food, pl.num_units, houses,
                          pl.is_lost            ? " lost"
                          : p == app.sel.player ? " (you)"
                                                : "");
            const auto& rgb = app.graphics->player_rgb;
            if (p < rgb.size()) {
                SDL_SetRenderDrawColor(r, rgb[p][0], rgb[p][1], rgb[p][2], 255);
                const SDL_FRect sw{4, 27 + kLine * static_cast<float>(p), 6, 7};
                SDL_RenderFillRect(r, &sw);
            }
            const Uint8 c = pl.is_lost ? 130 : 230;
            text(r, 14, 27 + kLine * static_cast<float>(p), line, c, c, c);
        }
    }

    // Bottom panel: selection on the left, buttons in the middle, unported calls on the right.
    panel(r, 0, kPanelTop, kW, kH - kPanelTop);
    float y = kPanelTop + 4;
    if (!app.sel.units.empty()) {
        if (app.sel.units.size() == 1) {
            const sim::Unit* u = sim::Unit_Find(s, app.sel.units[0]);
            if (u != nullptr) {
                const sim::UnitType& t = s.unit_types[me.nation % 6][u->type % 40];
                text(r, 6, y, ui::unit_name(u->type) + "  #" + std::to_string(u->id), 255, 255, 160);
                text(r, 6, y + kLine, "HP " + fmt("%.0f", u->hp) + " / " + std::to_string(t.max_hp[app.sel.player]));
                text(r, 6, y + 2 * kLine, "Work: " + ui::work_name(u->work));
                text(r, 6, y + 3 * kLine,
                     "Anim " + std::to_string(u->num_work) + " frame " + fmt("%.2f", u->num_sprite) + " orient " +
                         std::to_string(u->orient));
                text(r, 6, y + 4 * kLine, "Cell " + std::to_string(u->map_x) + ", " + std::to_string(u->map_y));
            }
        } else {
            std::map<std::uint16_t, int> count;
            for (const sim::UnitRef& ref : app.sel.units) {
                if (const sim::Unit* u = sim::Unit_Find(s, ref)) ++count[u->type];
            }
            text(r, 6, y, std::to_string(app.sel.units.size()) + " units selected", 255, 255, 160);
            float yy = y + kLine;
            for (const auto& [t, n] : count) {
                if (yy > kH - 12) break;
                text(r, 6, yy, std::to_string(n) + " x " + ui::unit_name(t));
                yy += kLine;
            }
        }
    } else if (app.sel.house) {
        const sim::HouseRef hr = *app.sel.house;
        const sim::House& h = s.players[hr.player].houses[hr.slot];
        text(
            r, 6, y,
            ui::house_name(h.type) + (hr.player == app.sel.player ? "" : " (player " + std::to_string(hr.player) + ")"),
            255, 255, 160);
        text(r, 6, y + kLine, "HP " + fmt("%.0f", h.hp) + " / " + std::to_string(h.max_hp));
        std::string state = h.is_destroyed     ? "ruin"
                            : h.field_020 != 0 ? "being built, work left " + std::to_string(h.field_020)
                                               : "standing";
        text(r, 6, y + 2 * kLine, state);
        std::string q = "Queue:";
        q += " " + std::to_string(h.queue_count);
        if (h.queue_count > 0 && !h.queue.empty() && h.queue[0] != 0xffff) q += ", next " + ui::unit_name(h.queue[0]);
        text(r, 6, y + 3 * kLine, q);
        text(r, 6, y + 4 * kLine, "Units inside " + std::to_string(h.units_inside));
    } else {
        text(r, 6, y, "Nothing selected", 160, 160, 160);
        text(r, 6, y + kLine, "Left click or drag: select your units", 160, 160, 160);
        text(r, 6, y + 2 * kLine, "Right click: order them", 160, 160, 160);
    }
    if (!app.message.empty()) {
        const std::string m = app.message.substr(0, 126);
        panel(r, 0, kPanelTop - 14, 8.0f * static_cast<float>(m.size()) + 12, 14);
        text(r, 6, kPanelTop - 11, m, 255, 220, 120);
    }

    if (app.popup.w > 0) {
        panel(r, app.popup.x, app.popup.y, app.popup.w, app.popup.h, 225);
        text(r, app.popup.x + 6, app.popup.y + 5, "Build: pick a house, then a place on the map (Esc closes)", 180, 200,
             255);
    }
    for (const Button& b : app.buttons) {
        SDL_SetRenderDrawColor(r, b.enabled ? 60 : 40, b.enabled ? 70 : 40, b.enabled ? 96 : 40, 255);
        SDL_RenderFillRect(r, &b.r);
        SDL_SetRenderDrawColor(r, 140, 150, 190, 255);
        SDL_RenderRect(r, &b.r);
        text(r, b.r.x + 5, b.r.y + 4, b.label);
    }
    text(r, 340, kPanelTop + 4, "Orders and game speed", 180, 200, 255);

    // Unported calls: the original's code the run needed and the new engine does not have yet.
    std::uint64_t total = 0;
    for (const auto& [addr, hit] : s.unported) total += hit.count;
    text(r, 800, kPanelTop + 4, "Unported calls: " + std::to_string(total), total ? 255 : 160, total ? 160 : 200, 120);
    float uy = kPanelTop + 4 + kLine;
    for (const auto& [addr, hit] : s.unported) {
        if (uy > kH - 12) break;
        char line[64];
        std::snprintf(line, sizeof line, "0x%06x x%u (tick %u)", addr, hit.count, hit.first_tick);
        text(r, 800, uy, line, 255, 190, 150);
        uy += kLine;
    }

    draw_help(app);
}

std::vector<std::string> menu_fonts();
std::vector<std::string> sans_fonts();

// The world picture of the view into world_tex, at its top left. The texture is made for the largest view the zoom
// allows (view_max_w x view_max_h, made again only for a new output size), so a zoom does not make it every frame.
bool draw_world(App& app) {
    SDL_Renderer* r = app.renderer;
    const int tw = std::max(app.view_w, app.view_max_w), th = std::max(app.view_h, app.view_max_h);
    if (!app.world_tex || app.tex_w != tw || app.tex_h != th) {
        SDL_DestroyTexture(app.world_tex);
        app.world_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, tw, th);
        app.tex_w = tw, app.tex_h = th;
        if (!app.world_tex) {
            SDL_Log("cannot create the view texture: %s", SDL_GetError());
            return false;
        }
    }
    // A house or a map object changed: the tiles it touches (as it was and as it is) are drawn again; a new game or
    // a load draws all of them. Compared once a tick, not every frame (docs/performance.md "Map changes once a tick"):
    // what the comparison reads changes only in a tick (sim::step, which always counts the tick on) or with a new
    // state (a new game, a load, a rejoin or a resume of a network game: enter_game gives it a new LiveView, which
    // compares at once).
    const bool sync = app.live.signature == 0 || app.live_tick != app.view()->tick;
    const bool changed = sync && live_view_sync(app.live, *app.view());
    app.live_tick = app.view()->tick;
    if (changed || app.static_dirty) load_view_graphics(*app.graphics, app.live.view, loader(app), &app.warnings);
    if (changed || app.static_dirty) app.minimap_k = 0;  // slice I2: the minimap's static layer too
    if (app.static_dirty) {
        static_tiles_clear(app.tiles);
    } else if (changed) {
        static_tiles_apply_changes(app.tiles, *app.graphics, app.live);
    }
    app.static_dirty = false;
    const Uint64 t0 = SDL_GetTicksNS();
    // The static layer under the view from the world tiles (drawn when the view first reaches them, a few of the ring
    // around it ahead while it scrolls), then the units at the same corner.
    // A larger view (zoomed out) has a longer ring: more of it ahead per frame.
    const int prefetch = kPrefetchTiles * std::max(1, (app.view_w + app.view_h) / 2000);
    render_frame_begin_tiles(*app.graphics, app.live.view, app.tiles, app.pos, app.view_w, app.view_h, &app.work,
                             smooth_camera(app) ? prefetch : 0, false);


    water_draw(app.live.water, app.live.map, app.pos, app.work);


    if (app.settings.reflections)
        bridge_reflections_draw(*app.graphics, app.live.view, app.view()->tick, app.pos, app.work);
    ground_fx_draw(app.graphics->fx, app.live.fx, app.live.map, view_x(app.pos), view_y(app.pos), app.work);


    if (!app.ring_tex_tried) {
        std::string err;
        app.ring_tex = ring_texture(loader(app), &err);
        if (!app.ring_tex) std::fprintf(stderr, "antanta: selection rings: %s\n", err.c_str());
        app.ring_tex_tried = true;
    }
    std::vector<RingDot> house_ring, ship_rings;
    ui::rings_dots(app.rings, *app.view(), app.sel, app.pos, smooth_camera(app) ? &app.blend : nullptr, &house_ring,
                   &ship_rings);
    if (app.ring_tex) ring_dots_draw(*app.ring_tex, ship_rings, app.work);

    if (app.settings.reflections) {
        const sim::GameState& s = *app.view();
        if (app.ship_slots_serial != app.fog_serial || app.ship_slots_tick != s.tick ||
            app.ship_slots_graphics != &*app.graphics) {
            app.ship_slots = players_with_ship_slots(*app.graphics, s);
            app.ship_slots_serial = app.fog_serial, app.ship_slots_tick = s.tick;
            app.ship_slots_graphics = &*app.graphics;
        }
        ship_reflections_draw(*app.graphics, app.sprites, loader(app), s, app.pos, app.work,
                              smooth_camera(app) ? &app.blend : nullptr, &app.ship_slots);
    }
    std::vector<HouseFxQuad> anims;  // the houses' animations (render/house_fx), drawn among the units
    house_fx_quads(*app.graphics, app.live.view, app.pos, app.view_w, app.view_h, app.live.house_fx, &anims);
    std::vector<EffectQuad> effects;  // the falling leaves and the effect list (render/effects)
    leaves_quads(app.live.effects, app.graphics->objects[0], app.pos, app.view_w, app.view_h, &effects);
    effects_quads(app.live.effects, app.live.effect_graphics, app.pos, app.view_w, app.view_h, &effects);


    if (app.marks_atlas.empty()) app.marks_atlas = ui::marks_atlas(loader(app));
    {
        ui::MarksInput in;
        in.pos = app.pos, in.view_w = app.view_w, in.view_h = app.view_h;
        in.blend = smooth_camera(app) ? &app.blend : nullptr;
        in.selected = &app.sel.units;
        in.show_health = app.show_health;
        std::vector<ui::MarkQuad> q;
        ui::world_marks_units(*app.graphics, app.sprites, loader(app), *app.view(), in, &q);
        app.unit_marks.clear();
        for (const ui::MarkQuad& m : q) {
            app.unit_marks.push_back({m.unit, m.before, m.x0, m.y0, m.x1, m.y1, m.u0, m.v0, m.u1, m.v1, m.argb, m.key});
        }
    }
    const UnitMarks unit_marks{app.marks_atlas.data(), ui::kMarksAtlasW, ui::kMarksAtlasH, app.unit_marks};
    std::vector<UnitFxQuad> unit_fx;  // the muzzle and rifle flashes (render/unit_fx), drawn with their units
    unit_fx_quads(app.live.unit_fx, app.live.unit_fx_graphics, *app.view(), app.pos, &unit_fx);
    const UnitFxDraw unit_fx_draw{unit_fx, &app.live.unit_fx_graphics, &app.live.map};
    render_live_units(*app.graphics, app.sprites, loader(app), *app.view(), app.pos, app.work, &app.frame,
                      smooth_camera(app) ? &app.blend : nullptr, anims, effects, &unit_marks, &app.live.shell_trails,
                      app.ring_tex ? &*app.ring_tex : nullptr, house_ring, &unit_fx_draw);
    const Uint64 t1 = SDL_GetTicksNS();
    const SDL_Rect part{0, 0, app.view_w, app.view_h};
    SDL_UpdateTexture(app.world_tex, &part, app.frame.data(), app.view_w * 4);
    app.world_ms = static_cast<double>(t1 - t0) / 1e6;
    app.upload_ms = static_cast<double>(SDL_GetTicksNS() - t1) / 1e6;
    return true;
}

// What the interface shows of your player this frame (A.3, A.4, A.7).
ui::InterfaceFrame interface_frame(const App& app) {
    const sim::GameState& s = *app.view();
    ui::InterfaceFrame f;
    if (app.sel.player < s.players.size()) {
        const sim::Player& me = s.players[app.sel.player];
        const double stock[5] = {me.gold, me.food, me.wood, me.iron, me.oil};
        for (int i = 0; i < 5; ++i) f.numbers[i] = ui::resource_text(stock[i]);
        f.numbers[5] = std::to_string(me.units_alive);
        f.fill = ui::thermometer_fill(me.power_made, me.power_used, me.power_6c4, me.power_6e4);
        f.build_more_houses = me.units_alive >= me.unit_cap;
    }
    f.version = ui::version_visible(!app.sel.units.empty(), app.sel.house.has_value());
    return f;
}

// The interface, opened on first use; a missing picture or font is logged once and the game view goes on without it.
ui::GameInterface* interface_of(App& app) {
    if (!app.iface && !app.iface_failed) {
        std::string err;
        app.iface = ui::interface_open(app.renderer, loader(app), menu_fonts(), sans_fonts(), &err, &app.lang);
        if (!app.iface) {
            SDL_Log("interface: %s", err.c_str());
            app.iface_failed = true;
        }
    }
    return app.iface.get();
}


void event_message(App& app, const std::string& text, std::uint32_t rgb) {
    ui::GameInterface* g = interface_of(app);
    if (!g) return;
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app.renderer, &w, &h);
    const int k = std::max(1, app.ui_scale);
    ui::screen_text_event(app.texts, text, rgb, w / k, h / k, app.minimap_shown,
                          [g](const std::string& t) { return ui::interface_text_width(*g, t); });
}



void mission_timer_line(App& app, std::int32_t countdown, std::int32_t timer) {
    ui::GameInterface* g = interface_of(app);
    if (!g) return;
    const ui::StringTable& st = panel_strings(app);
    auto text = [&](const char* key) {
        const auto it = st.find(key);
        return it != st.end() ? it->second : std::string(key);
    };
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app.renderer, &w, &h);
    const int k = std::max(1, app.ui_scale);
    ui::screen_text_add(app.texts, w / k - 200, 30,
                        text("IDS_TIMER") + std::to_string(countdown) + text("IDS_FROM") + std::to_string(timer), 49,
                        0xfa0a0a, false, w / k, h / k, app.minimap_shown,
                        [g](const std::string& t) { return ui::interface_text_width(*g, t); });
}

bool show_messages(App& app);  // slice M4, below




void queue_game_message(App& app, const std::string& text, bool end) {
    if (text.empty()) return;
    ui::mission_log_add(app.mission, text);
    app.messages.push_back(GameMessage{text, end});
    if (app.renderer) show_messages(app);  // a game being loaded has no renderer yet: run_batch or the frame shows it
}

// Slice M3: the mission events of the sim (sim/mission_events.hpp, docs/missions.md 3.7). A block's text goes into the
// queue of message windows (a window without the end flag pauses the game while it waits or shows, the queue's job);
// the end message comes through check_end (end_message 3, end_text), so it is not queued twice. The countdown has a


// the screen texts' Times New Roman Bold 16 with the shadow; the next check, 50 ticks on, replaces it. Actions 8 and 20

void mission_events(App& app, const std::vector<sim::SimEvent>& events) {
    for (const sim::SimEvent& e : events) {
        // The text by its key in the display language (docs/languages.md 5.3); the event's text is the English.
        const std::string text = e.kind == sim::SimEvent::Kind::MissionMessage && !e.mission.key.empty()
                                     ? ui::lang_game(app.lang, e.mission.key, e.text)
                                     : e.text;
        if (e.kind == sim::SimEvent::Kind::MissionMessage && e.mission.log_only) {
            ui::mission_log_add(app.mission, text);  // slice M6: a loaded Lua mission's earlier texts
        } else if (e.kind == sim::SimEvent::Kind::MissionMessage && !e.mission.end) {
            queue_game_message(app, text, false);
        }
        if (e.kind == sim::SimEvent::Kind::MissionLine) {  // Lua AddMessage (M5), its literals by our keys (lua.)
            event_message(app, ui::lang_reverse(app.lang, e.text, "lua."), 0xffffff);
        }
        if (e.kind == sim::SimEvent::Kind::MissionTimer) mission_timer_line(app, e.mission.countdown, e.mission.timer);
        if (e.kind == sim::SimEvent::Kind::MissionCamera) {
            int col = e.cell_x, row = e.cell_y;
            if (e.mission.centred) col -= app.view_w / 96 / 2, row -= app.view_h / 24 / 2;
            if (col % 2 == 0) --col;
            if (row % 2 == 0) --row;
            set_view(app, ViewPos{col, row, 0, 0});
        }
    }
}

// Slice J: the events a step raised (sim/events.hpp, taken out of the state after the step). The sounds go to the
// device, the event messages of A.9 to the screen texts with their text from the string table, battle marks to the
// minimap (slice I2).
void game_events(App& app, std::vector<sim::SimEvent> events) {
    unit_fx_mute(app.live.unit_fx, events);
    audio::audio_events(app.audio.get(), events, listener(app));
    if (app.graphics) {  // the craters this tick made, then a step of the ground's pictures (render/ground_fx)
        std::vector<GroundFxEvent> craters;
        for (const sim::SimEvent& e : events) {
            if (e.kind == sim::SimEvent::Kind::Crater) {
                craters.push_back({static_cast<std::int32_t>(e.x), static_cast<std::int32_t>(e.y),
                                   static_cast<std::uint8_t>(e.type)});
            }
        }
        ground_fx_step(app.live.fx, app.graphics->fx, app.live.map, craters);
        house_fx_step(app.live.house_fx, app.graphics->house_fx);
        // The effect list (render/effects): the records the step made, then its step.
        effects_tick(app.live, *app.graphics, loader(app), *app.view(), events, app.pos, app.view_w, app.view_h,
                     ui::house_panel_house(*app.view(), app.sel), &app.sprites);
    }
    auto say_key = [&app](const std::string& key, std::uint32_t rgb) {
        const ui::StringTable& t = panel_strings(app);
        const auto it = t.find(key);
        event_message(app, it != t.end() ? it->second : key, rgb);
    };
    mission_events(app, events);
    for (const sim::SimEvent& e : events) {
        if (e.kind == sim::SimEvent::Kind::Message) say_key(e.text, e.colour);
        if (e.kind == sim::SimEvent::Kind::BattleMark) {
            ui::minimap_mark_add(app.minimap_marks, app.view()->map.width, app.view()->map.height, e.cell_x, e.cell_y);
        }
        if (e.kind != sim::SimEvent::Kind::GroupAlert) continue;


        if (e.player == app.view()->my_player && e.flag && app.group_alert_wait < 1) {
            app.group_alert_wait = 20;
            say_key("IDS_GROUP_UNDER_ATTACK", 0xffffff);
            ui::minimap_mark_add(app.minimap_marks, app.view()->map.width, app.view()->map.height, e.cell_x, e.cell_y);
        } else {
            --app.group_alert_wait;
        }
    }
}

// Slice I4, before the interface draws: the panel of this frame, its hover button (under the mouse) and that button's
// tooltip as a one-frame entry of the screen text list (B.7; white, as the other one-frame texts of A.7, medium).
ui::Panel panel_before_draw(App& app) {
    ui::Panel p = current_panel(app);
    app.panel_in.hover = -1;
    ui::GameInterface* g = interface_of(app);
    int w = 0, h = 0, px = 0, py = 0;
    const int k = std::max(1, app.ui_scale);
    bool over = false;  // slice I8: the pause window and the group bar are over the panel, the top one hovers
    for (const int which : {0, 1}) {
        ui::PanelInput& in = app.window_in[static_cast<std::size_t>(which)];
        in.hover = -1;
        const ui::Panel wp = window_panel(app, which);
        if (wp.widgets.empty() || !g || over) continue;
        panel_space(app, app.mouse_x, app.mouse_y, &w, &h, &px, &py);
        over = ui::panel_contains(wp, w, h, k, px, py);
        in.hover = ui::panel_hit(wp, w, h, k, px, py);
        for (const ui::TooltipLine& t : ui::panel_tooltip(wp, in, h, k)) {
            ui::screen_text_add(app.texts, t.x, t.y, t.text, 1, 0xffffff, false, w / k, h / k, app.minimap_shown,
                                [g](const std::string& s) { return ui::interface_text_width(*g, s); });
        }
    }
    if (p.widgets.empty() || !g || over) return p;
    panel_space(app, app.mouse_x, app.mouse_y, &w, &h, &px, &py);
    app.panel_in.hover = ui::panel_hit(p, w, h, k, px, py);
    for (const ui::TooltipLine& t : ui::panel_tooltip(p, app.panel_in, h, k)) {
        ui::screen_text_add(app.texts, t.x, t.y, t.text, 1, 0xffffff, false, w / k, h / k, app.minimap_shown,
                            [g](const std::string& s) { return ui::interface_text_width(*g, s); });
    }
    return p;
}

// Slice I4, after the interface: the panel window over the frame (A.1: the windows come after the frame). `in`: the
// press and hover of another panel (slice I8's windows), else the selection panel's.
void panel_after_draw(App& app, const ui::Panel& p, ui::PictureFilter filter, const ui::PanelInput* in = nullptr) {
    if (p.widgets.empty()) return;
    if (!app.panel_gfx && !app.panel_failed) {
        std::string err;
        app.panel_gfx = ui::panel_graphics_open(app.renderer, loader(app), menu_fonts(), sans_fonts(), &err, &app.lang);
        if (!app.panel_gfx) SDL_Log("panel: %s", err.c_str());
        app.panel_failed = !app.panel_gfx;
    }
    if (!app.panel_gfx) return;
    int w = 0, h = 0;
    SDL_GetRenderOutputSize(app.renderer, &w, &h);
    ui::panel_draw(*app.panel_gfx, p, in ? *in : app.panel_in, w, h, std::max(1, app.ui_scale), filter);
}

// The ground's minimap colours: the bytes at the head of LandTextures.dat (u16 n, n x 3 bytes).
ui::MinimapPalette minimap_ground_colours(App& app) {
    ui::MinimapPalette pal;
    std::string err;
    if (auto file = loader(app)("Data/LandTextures/LandTextures.dat", &err)) {
        const std::size_t n = file->size() >= 2 ? (*file)[0] | (*file)[1] << 8 : 0;
        for (std::size_t i = 0; i < n && 2 + 3 * i + 2 < file->size(); ++i) {
            pal.tiles.push_back({(*file)[2 + 3 * i], (*file)[3 + 3 * i], (*file)[4 + 3 * i]});
        }
    } else {
        SDL_Log("minimap: %s", err.c_str());
    }
    return pal;
}

// Slice I2: the minimap's colours (the minimap colour bytes at the head of LandTextures.dat, each player's Colors.txt
// line) and its static layer at the interface scale, made when missing.
bool minimap_prepare(App& app, int k) {
    if (!app.minimap_gfx && !app.minimap_failed) {
        std::string err;
        app.minimap_gfx = ui::minimap_graphics_open(app.renderer, loader(app), &err);
        if (!app.minimap_gfx) SDL_Log("minimap: %s", err.c_str());
        app.minimap_failed = !app.minimap_gfx;
    }
    if (!app.minimap_gfx || !app.graphics) return false;
    const sim::GameState& s = *app.view();
    if (!app.minimap_pal) {
        ui::MinimapPalette pal = minimap_ground_colours(app);
        const auto& rgb = app.graphics->player_rgb;
        for (std::size_t p = 0; p < s.players.size() && p < pal.players.size(); ++p) {
            const std::size_t c = s.players[p].colour;
            if (c < rgb.size()) pal.players[p] = std::uint32_t{rgb[c][0]} << 16 | rgb[c][1] << 8 | rgb[c][2];
        }
        app.minimap_pal = std::move(pal);
        app.minimap_k = 0;
    }
    if (app.minimap_k != k) {
        app.minimap_rgb = ui::minimap_static(s, *app.minimap_pal, k);
        app.minimap_k = k, ++app.minimap_version;
    }
    return true;
}


void minimap_frame(App& app, const ui::MinimapLayout& l, ui::PictureFilter filter) {
    if (!minimap_prepare(app, l.scale)) return;
    const sim::GameState& s = *app.view();
    ui::MinimapFrame f;
    f.static_rgb = &app.minimap_rgb;
    f.static_version = app.minimap_version;
    const auto same_ref = [](const sim::UnitRef& a, const sim::UnitRef& b) {
        return a.player == b.player && a.id == b.id && a.serial == b.serial;
    };
    if (app.minimap_dots_serial != app.fog_serial || app.minimap_dots_tick != s.tick || app.minimap_dots_k != l.scale ||
        app.minimap_dots_player != s.my_player || app.minimap_dots_sel_player != app.sel.player ||
        !std::equal(app.sel.units.begin(), app.sel.units.end(), app.minimap_dots_sel.begin(),
                    app.minimap_dots_sel.end(), same_ref)) {
        app.minimap_dots = ui::minimap_dots(s, *app.minimap_pal, app.sel, l.scale);
        app.minimap_dots_serial = app.fog_serial, app.minimap_dots_tick = s.tick, app.minimap_dots_k = l.scale;
        app.minimap_dots_player = s.my_player, app.minimap_dots_sel_player = app.sel.player;
        app.minimap_dots_sel = app.sel.units;
        ++app.minimap_dots_version;
    }
    f.dots.swap(app.minimap_dots);  // lent to the frame, taken back after it is drawn
    f.dots_version = app.minimap_dots_version;
    f.house_dots = ui::minimap_house_marks(s, app.sel, l.scale);
    f.view = ui::minimap_view_rect(l, s.map.width, s.map.height, app.cam.x, app.cam.y, app.view_w, app.view_h);
    f.marks = ui::minimap_marks_draw(app.minimap_marks, l.scale);
    if (s.fog.built && s.fog.on) {  // the fog layer of the last rebuild (docs/fog.md 7.1)
        const std::uint64_t key =
            (app.fog_serial << 40) ^ (std::uint64_t{s.fog.tick} << 8) ^ static_cast<std::uint64_t>(l.scale);
        if (key != app.minimap_fog_key || app.minimap_fog.size() != static_cast<std::size_t>(l.size) * l.size) {
            app.minimap_fog = sim::Fog_MinimapMask(s.fog, s.map.width, s.map.height, l.size);
            app.minimap_fog_key = key, ++app.minimap_fog_version;
        }
        f.fog = &app.minimap_fog;
        f.fog_version = app.minimap_fog_version;
    }
    ui::minimap_draw(*app.minimap_gfx, l, filter, f);
    app.minimap_dots.swap(f.dots);
}

// The fog of war over the game view (docs/fog.md 7.2 to 7.4; ours lined up with the world, docs/bugs.md B15): after
// the selection marks, before the interface; made again when the sim rebuilt its circles or the view moved.
void draw_fog(App& app) {
    const sim::GameState& s = *app.view();
    if (!s.fog.built || !s.fog.on) return;
    if (!app.fog_ramp && !app.fog_ramp_failed) {
        std::string err;
        app.fog_ramp = ui::fog_ramp_load(loader(app), &err);
        if (!app.fog_ramp) SDL_Log("fog: %s (a straight ramp instead)", err.c_str());
        app.fog_ramp_failed = !app.fog_ramp;
    }
    const ui::FogRamp ramp = app.fog_ramp ? *app.fog_ramp : ui::fog_ramp({}, 0, 0);
    const int vx = view_x(app.pos), vy = view_y(app.pos);
    const std::array<std::int64_t, 5> key{static_cast<std::int64_t>(app.fog_serial), s.fog.tick, vx, vy,
                                          std::int64_t{app.view_w} << 20 | app.view_h};
    const bool changed = key != app.fog_view_key;
    if (changed) {
        app.fog_view = ui::fog_view(s, ramp, vx, vy, app.view_w, app.view_h, ui::kFogStep, &app.fog_picks);
        app.fog_view_key = key;
    }
    app.fog_tex.draw(app.renderer, app.fog_view, changed, vx, vy, app.zoom);
}

void draw_frame(App& app) {
    SDL_Renderer* r = app.renderer;
    // The debug HUD (and the web page's prompt) draw on the 1024 x 768 canvas, the interface in output pixels.
    const bool canvas = !app.started || app.debug_hud;
    SDL_SetRenderLogicalPresentation(r, canvas ? kViewWidth : 0, canvas ? kViewHeight : 0,
                                     canvas ? SDL_LOGICAL_PRESENTATION_LETTERBOX : SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderDrawColor(r, canvas ? 10 : 0, canvas ? 10 : 0, canvas ? 14 : 0, 255);
    SDL_RenderClear(r);
    if (!app.started) {
        text(r, 40, 40, "Pick your game folder and a save (.svg) above.");
        return;
    }
    update_view(app);
    if (app.debug_hud) {  // the milestone 3 debug HUD on the 1024 x 768 canvas
        if (!draw_world(app)) return;
        const SDL_FRect part{0, 0, static_cast<float>(app.view_w), static_cast<float>(app.view_h)};
        SDL_RenderTexture(r, app.world_tex, &part, nullptr);
        layout_buttons(app);
        draw_selection(app);
        draw_hud(app);
        return;
    }
    // The original's look: the view over the whole output, the interface on top in output pixels.
    app.buttons.clear();
    app.popup = SDL_FRect{};
    if (!draw_world(app)) return;
    // Whole zooms: hard pixels (or Smooth's blur); in between the wheel's steps: SDL's pixel-art filter, sharp pixels
    // with only their edges blended, so they do not come out unevenly wide (docs/decisions.md 41).
    const auto filter = static_cast<ui::PictureFilter>(std::min<int>(app.settings.ui_pictures, 2));
    const bool whole = app.zoom == std::floor(app.zoom);
    SDL_SetTextureScaleMode(app.world_tex, filter == ui::PictureFilter::Smooth ? SDL_SCALEMODE_LINEAR
                                           : whole                             ? SDL_SCALEMODE_NEAREST
                                                                               : SDL_SCALEMODE_PIXELART);
    const float z = static_cast<float>(app.zoom);
    const SDL_FRect part{0, 0, static_cast<float>(app.view_w), static_cast<float>(app.view_h)};
    const SDL_FRect dst{0, 0, static_cast<float>(app.view_w) * z, static_cast<float>(app.view_h) * z};
    SDL_RenderTexture(r, app.world_tex, &part, &dst);
    SDL_SetRenderScale(r, z, z);  // the selection marks are drawn in world pixels
    draw_selection(app);
    SDL_SetRenderScale(r, 1, 1);
    draw_fog(app);                                   // the fog of war over the world, under the interface
    const ui::Panel panel = panel_before_draw(app);  // slice I4
    if (ui::GameInterface* g = interface_of(app)) {
        int w = 0, h = 0;
        SDL_GetRenderOutputSize(r, &w, &h);
        const ui::MinimapLayout mm = ui::minimap_layout(w, h, std::max(1, app.ui_scale), app.minimap_shown);
        const int k = std::max(1, app.ui_scale);
        int mx = 0, my = 0, pw = 0, ph = 0;
        panel_space(app, app.mouse_x, app.mouse_y, &pw, &ph, &mx, &my);
        const ui::HudButtonsState hud = hud_state(app, mm, mx, my);  // slice I3: the six buttons and their tooltip
        ui::HudTooltip tip = ui::hud_button_tooltip_line(mm, panel_strings(app), hud);
        if (!tip.text.empty()) {
            // The disabled buttons' note makes the text longer than the room right of the tooltip's place: move it
            // left until it ends before the right bevel (the original's short texts keep their place).
            tip.x = std::min(tip.x, w / k - 36 - ui::interface_text_width(*g, tip.text));
            ui::screen_text_add(app.texts, tip.x, tip.y, tip.text, 1, 0xffffff, false, w / k, h / k, app.minimap_shown,
                                [g](const std::string& str) { return ui::interface_text_width(*g, str); });
        }
        // Entente Engine Plus: the economic advisor's last pick on a second line above its tooltip
        if (hud.hover == static_cast<int>(ui::HudButton::Economic) && !tip.text.empty()) {
            const std::string status = ui::plus_advisor_status(*app.view(), app.view()->my_player, panel_strings(app));
            if (!status.empty()) {
                const int x = std::min(tip.x, w / k - 36 - ui::interface_text_width(*g, status));
                ui::screen_text_add(app.texts, x, tip.y - 16, status, 1, 0xffffff, false, w / k, h / k,
                                    app.minimap_shown,
                                    [g](const std::string& str) { return ui::interface_text_width(*g, str); });
            }
        }
        ui::interface_draw(*g, ui::hud_layout(w, h, app.ui_scale), filter, interface_frame(app), app.texts,
                           [&] { minimap_frame(app, mm, filter); });  // slice I2
        if (app.minimap_gfx) ui::minimap_draw_buttons(*app.minimap_gfx, mm, filter, hud);
    }
    panel_after_draw(app, panel, filter);  // slice I4
    for (const int which : {1, 0}) {       // slice I8: the group bar, then the pause window over it
        panel_after_draw(app, window_panel(app, which), filter, &app.window_in[static_cast<std::size_t>(which)]);
    }
}

// One frame of the screen texts per tick period at normal speed (the original counts GameFrames, one per tick, and
// they run down while paused too).
void run_screen_texts(App& app, double ms) {
    const double period = std::max<double>(1, app.view()->speed_ms);
    app.text_ms += ms;
    while (app.text_ms >= period) {
        ui::screen_texts_frame(app.texts);
        if (app.minimap_shown) ui::minimap_marks_advance(app.minimap_marks);
        ui::rings_advance(app.rings, *app.view(), app.sel);
        app.text_ms -= period;
    }
}

// ---- script ----

#ifndef __EMSCRIPTEN__  // the web build has no command line
bool load_script(App& app, std::string* err) {
    auto file = load_file(app.script_path, err);
    if (!file) return false;
    const std::string all(file->begin(), file->end());
    std::size_t start = 0;
    int n = 0;
    while (start < all.size()) {
        std::size_t end = all.find('\n', start);
        if (end == std::string::npos) end = all.size();
        const std::string line = all.substr(start, end - start);
        start = end + 1;
        ++n;
        std::vector<std::string> w = words_of(line);
        if (w.empty() || w[0][0] == '#') continue;
        if (w.size() < 2) {
            *err = app.script_path + ":" + std::to_string(n) + ": expected <tick> <event> ...";
            return false;
        }
        ScriptEvent e;
        const bool rel = w[0][0] == '+';
        e.tick = static_cast<std::uint32_t>(std::strtoul(w[0].c_str() + (rel ? 1 : 0), nullptr, 10)) +
                 (rel ? app.view()->tick : 0);
        e.words.assign(w.begin() + 1, w.end());
        app.script.push_back(std::move(e));
    }
    std::stable_sort(app.script.begin(), app.script.end(),
                     [](const ScriptEvent& a, const ScriptEvent& b) { return a.tick < b.tick; });
    return true;
}

#endif

float num(const std::vector<std::string>& w, std::size_t i) {
    return i < w.size() ? static_cast<float>(std::atof(w[i].c_str())) : 0.0f;
}

void record_progress(App& app);  // slice M4, below
bool campaign_return(App& app);  // slice M7, below

// A script's player argument: a number, or `me` (MyPlayer: a network game's scripts, whose player the shuffle picks).
float script_player(const App& app, const std::vector<std::string>& w, std::size_t i) {
    return i < w.size() && w[i] == "me" ? static_cast<float>(app.view()->my_player) : num(w, i);
}

void run_event(App& app, const ScriptEvent& e) {
    const std::vector<std::string>& w = e.words;
    const std::string& ev = w[0];
    if (ev == "camera") {
        set_view(app, {static_cast<int>(num(w, 1)), static_cast<int>(num(w, 2))});
    } else if (ev == "zoom" && w.size() > 1 && num(w, 1) > 0) {  // ours: that world zoom at once (within the limits)
        update_view(app);
        int ow = 0, oh = 0;
        SDL_GetRenderOutputSize(app.renderer, &ow, &oh);
        const double steps = ui::kZoomStepsPerDouble * std::log2(num(w, 1) / app.zoom_target);
        zoom_by(app, steps, ow / 2.0, oh / 2.0, false, true);
        SDL_Log("script: zoom %.3f, view %d x %d", app.zoom, app.view_w, app.view_h);
    } else if (ev == "click") {
        auto held = [&](const char* m) {
            return std::find(w.begin() + std::min<std::size_t>(3, w.size()), w.end(), m) != w.end();
        };
        app.mouse_x = num(w, 1), app.mouse_y = num(w, 2);
        left_click(app, num(w, 1), num(w, 2), held("shift"));
        app.build_held = false;                                                             // a press and release
        hud_release(app, num(w, 1), num(w, 2));                                             // slice I3
        window_mouse(app, num(w, 1), num(w, 2), false, false, held("shift"));               // slice I8: release
        panel_mouse(app, num(w, 1), num(w, 2), false, false, held("ctrl"), held("shift"));  // slice I4: release
        say(app, "script click: " + std::to_string(app.sel.units.size()) + " units selected" +
                     (app.sel.house ? ", a house" : ""));
    } else if (ev == "press" || ev == "release") {  // slice I5: the left button held (the build press, build_hold)
        app.mouse_x = num(w, 1), app.mouse_y = num(w, 2);
        if (ev == "press") {
            left_click(app, num(w, 1), num(w, 2), std::find(w.begin() + 1, w.end(), "shift") != w.end());
        } else {
            app.build_held = false;
        }
    } else if (ev == "select" && w.size() > 2) {  // slice I4: the recorder's `select <player> <ids>`, head first
        ui::clear_selection(app.sel);
        const auto p = static_cast<std::size_t>(script_player(app, w, 1));
        if (p != app.sel.player) SDL_Log("script: select for player %zu, you are %u", p, unsigned{app.sel.player});
        for (std::size_t i = 2; i < w.size() && p == app.sel.player; ++i) {
            const sim::Player& pl = app.view()->players[p];
            for (std::uint32_t k : sim::Player_UnitSlots(pl)) {
                if (pl.unit_slots[k].id == static_cast<std::int32_t>(num(w, i))) {
                    app.sel.units.push_back(sim::UnitRef{app.sel.player, pl.unit_slots[k].id, pl.unit_slots[k].serial});
                }
            }
        }
    } else if (ev == "build") {
        do_action(app, Action::Build, static_cast<int>(num(w, 1)));
    } else if (ev == "ctrl" && w.size() > 1) {  // `ctrl down|up`: Ctrl held (the cells where a building fits)
        app.script_ctrl = w[1] == "down";
    } else if (ev == "mouse") {
        app.mouse_x = num(w, 1), app.mouse_y = num(w, 2);
    } else if (ev == "cursor") {  // slice I9: the PNG shows the cursor at the mouse point
        app.draw_cursor = true;
    } else if (ev == "drag") {
        box_select(app, num(w, 1), num(w, 2), num(w, 3), num(w, 4), false);
    } else if (ev == "wdrag" && w.size() > 4) {  // the pause window: a press at (x0, y0), held to (x1, y1), released
        app.mouse_x = num(w, 1), app.mouse_y = num(w, 2), app.left_down = true;
        window_mouse(app, num(w, 1), num(w, 2), true, false, false);
        app.mouse_x = num(w, 3), app.mouse_y = num(w, 4);
        pause_drag_move(app, num(w, 3), num(w, 4));
        window_mouse(app, num(w, 3), num(w, 4), false, false, false);
        app.left_down = false, app.pause_drag.on = false;
    } else if (ev == "hold" && w.size() > 4) {  // a box drag still held: the live selection and the rubber band
        app.dragging = true;
        app.drag_x0 = num(w, 1) + static_cast<float>(view_x(app.pos));
        app.drag_y0 = num(w, 2) + static_cast<float>(view_y(app.pos));
        app.mouse_x = num(w, 3), app.mouse_y = num(w, 4);
        box_live(app);
    } else if (ev == "rclick") {
        auto held = [&](const char* m) {
            return std::find(w.begin() + std::min<std::size_t>(3, w.size()), w.end(), m) != w.end();
        };
        right_click(app, num(w, 1), num(w, 2));
        window_mouse(app, num(w, 1), num(w, 2), false, true, held("shift"));               // slice I8: release
        panel_mouse(app, num(w, 1), num(w, 2), false, true, held("ctrl"), held("shift"));  // slice I4: release
    } else if ((ev == "rdrag" || ev == "rhold") && w.size() > 4) {  // slice I9: the right button dragged (and released)
        app.mouse_x = num(w, 3), app.mouse_y = num(w, 4);
        right_press(app, num(w, 1), num(w, 2));
        if (ev == "rdrag") right_release(app, num(w, 3), num(w, 4));
    } else if ((ev == "select_house" || ev == "select_house_type") && w.size() > 2) {


        const auto hp = static_cast<std::uint8_t>(script_player(app, w, 1));
        auto slot = static_cast<std::uint16_t>(num(w, 2));
        if (ev == "select_house_type") {
            if (hp >= app.view()->players.size()) return;
            const auto& houses = app.view()->players[hp].houses;
            const auto it = std::find_if(houses.begin(), houses.end(),
                                         [&](const sim::House& x) { return x.is_destroyed == 0 && x.type == slot; });
            if (it == houses.end()) {
                SDL_Log("script: player %s has no house of type %s", w[1].c_str(), w[2].c_str());
                return;
            }
            slot = static_cast<std::uint16_t>(it - houses.begin());
        }
        ui::clear_selection(app.sel);
        const sim::HouseRef h{hp, slot, 0};
        app.sel.house = h;
        if (h.player == app.sel.player) app.sel.flagged.push_back(h);
    } else if ((ev == "click_unit" || ev == "rclick_unit") && w.size() > 2) {
        // A click on the body of unit <player> <id> where it stands now (18 pixels above its foot point).
        const auto p = static_cast<std::size_t>(script_player(app, w, 1));
        const auto id = static_cast<std::int32_t>(num(w, 2));
        const sim::Unit* u = nullptr;
        if (p < app.view()->players.size()) {
            const sim::Player& pl = app.view()->players[p];
            for (std::uint32_t k : sim::Player_UnitSlots(pl)) {
                if (pl.unit_slots[k].id == id) u = &pl.unit_slots[k];
            }
        }
        if (u == nullptr) {
            SDL_Log("script: no unit %s %s", w[1].c_str(), w[2].c_str());
            return;
        }
        const auto [fx, fy] = unit_screen_point(*u, app.pos);
        if (ev == "click_unit") {
            left_click(app, fx, fy - 18, w.size() > 3 && w[3] == "shift");
            say(app, "script click: " + std::to_string(app.sel.units.size()) + " units selected");
        } else {
            right_click(app, fx, fy - 18);
        }
    } else if ((ev == "click_cell" || ev == "rclick_cell") && w.size() > 2) {


        const sim::Cell c = sim::CellToWorldPoint(
            app.view()->map, sim::Cell{static_cast<std::int32_t>(num(w, 1)), static_cast<std::int32_t>(num(w, 2))});
        const float fx = static_cast<float>(c.x - view_x(app.pos)), fy = static_cast<float>(c.y - view_y(app.pos));
        if (ev == "click_cell") {
            left_click(app, fx, fy, w.size() > 3 && w[3] == "shift");
        } else {
            right_click(app, fx, fy);
        }
    } else if (ev == "click_site" && app.build_type >= 0 && !app.sel.units.empty()) {
        // Slice MP8: the build click on the free cell nearest the first selected unit (rings of 2 to 12 cells) on the
        // open map view, for a network game's scripts, whose start position the shuffle picks.
        const sim::Unit* u = sim::Unit_Find(*app.view(), app.sel.units[0]);
        const auto type = static_cast<std::uint16_t>(app.build_type);
        for (std::int32_t r = 2; u != nullptr && r <= 12; ++r) {
            for (std::int32_t dy = -r; dy <= r; ++dy) {
                for (std::int32_t dx = -r; dx <= r; ++dx) {
                    if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                    const sim::Cell c = sim::CellToWorldPoint(app.view()->map, sim::Cell{u->map_x + dx, u->map_y + dy});
                    const auto sx = static_cast<float>(c.x - view_x(app.pos)),
                               sy = static_cast<float>(c.y - view_y(app.pos));
                    std::int32_t cx = -1, cy = -1;
                    if (sx < 0 || sy < 0 || sx >= static_cast<float>(app.view_w) ||
                        sy >= static_cast<float>(app.view_h) || over_panel(app, sx, sy) || over_minimap(app, sx, sy) ||
                        !ui::pick_cell(*app.view(), c.x, c.y, &cx, &cy) ||
                        !ui::can_place(*app.view(), app.sel.player, cx, cy, type)) {
                        continue;  // off the open map view, or no house fits there
                    }
                    left_click(app, sx, sy, false);
                    app.build_held = false;  // a press and release
                    return;
                }
            }
        }
        SDL_Log("script: click_site found no free cell");
    } else if ((ev == "click_house" || ev == "rclick_house") && w.size() > 2) {
        // A click on house <player> <slot>: the world point of one of its footprint cells, 48 px right.
        int sx = 0, sy = 0;
        if (!ui::house_point(*app.view(), app.pos, static_cast<std::uint8_t>(num(w, 1)),
                             static_cast<std::uint16_t>(num(w, 2)), &sx, &sy)) {
            SDL_Log("script: no house %s %s on screen", w[1].c_str(), w[2].c_str());
            return;
        }
        if (ev == "click_house") {
            left_click(app, static_cast<float>(sx), static_cast<float>(sy), w.size() > 3 && w[3] == "shift");
        } else {
            right_click(app, static_cast<float>(sx), static_cast<float>(sy));
        }
    } else if (ev == "menu" && w.size() > 2) {  // the front-end's windows: `menu <x> <y> [click]` at a canvas point,
        if (!app.menu) return;                  // `menu key <SDL key name>`
        menu_presentation(app);
        auto send = [&](SDL_EventType type) {
            SDL_Event e{};
            e.type = type;
            if (type == SDL_EVENT_KEY_DOWN) {
                e.key.key = SDL_GetKeyFromName(w[2].c_str());
            } else if (type == SDL_EVENT_MOUSE_MOTION) {
                e.motion.x = num(w, 1), e.motion.y = num(w, 2);
            } else {
                e.button.button = SDL_BUTTON_LEFT, e.button.x = num(w, 1), e.button.y = num(w, 2);
            }
            const ui::FrontendRequest r = app.menu ? ui::frontend_event(*app.menu, e) : ui::FrontendRequest::None;
            if (r == ui::FrontendRequest::BackToMenu || r == ui::FrontendRequest::StatisticsClosed) {
                // Slice M4: the progress as menu_request writes it; the front-end shows the main menu, the run ends
                // there and keeps the state for its report.
                record_progress(app);
                if (campaign_return(app)) {  // slice M7: a campaign's mission ends on its list
                    SDL_Log("script: the menu left the game for the campaign's mission list; the batch run ends");
                } else {
                    SDL_Log("script: the menu left the game for the main menu; the batch run ends");
                }
                app.started = false;
            } else if (r == ui::FrontendRequest::Quit) {
                SDL_Log("script: the menu left the game; the batch run keeps it for its report");
            } else {
                menu_request(app, r);
            }
        };
        if (w[1] == "key") {
            send(SDL_EVENT_KEY_DOWN);
        } else if (w[1] == "text" && app.menu) {  // slice H: typed characters (the save name field)
            SDL_Event te{};
            te.type = SDL_EVENT_TEXT_INPUT;
            te.text.text = w[2].c_str();
            menu_request(app, ui::frontend_event(*app.menu, te));
        } else if (w.size() > 5 && w[3] == "drag") {  // `menu <x0> <y0> drag <x1> <y1>`: a window by its strip
            auto at = [&](SDL_EventType type, float x, float y) {
                SDL_Event e{};
                e.type = type;
                if (type == SDL_EVENT_MOUSE_MOTION) {
                    e.motion.x = x, e.motion.y = y;
                } else {
                    e.button.button = SDL_BUTTON_LEFT, e.button.x = x, e.button.y = y;
                }
                menu_request(app, ui::frontend_event(*app.menu, e));
            };
            at(SDL_EVENT_MOUSE_MOTION, num(w, 1), num(w, 2));
            at(SDL_EVENT_MOUSE_BUTTON_DOWN, num(w, 1), num(w, 2));
            at(SDL_EVENT_MOUSE_MOTION, num(w, 4), num(w, 5));
            at(SDL_EVENT_MOUSE_BUTTON_UP, num(w, 4), num(w, 5));
        } else {
            send(SDL_EVENT_MOUSE_MOTION);
            if (w.size() > 3 && w[3] == "click") send(SDL_EVENT_MOUSE_BUTTON_DOWN), send(SDL_EVENT_MOUSE_BUTTON_UP);
        }
    } else if (ev == "key" && w.size() > 1) {  // `key <SDL key name> [ctrl] [shift]` (slice I9: modifiers)
        const bool ctrl = std::find(w.begin() + 2, w.end(), "ctrl") != w.end();
        const bool kshift = std::find(w.begin() + 2, w.end(), "shift") != w.end();
        const SDL_Keymod mod = static_cast<SDL_Keymod>((ctrl ? SDL_KMOD_LCTRL : 0) | (kshift ? SDL_KMOD_LSHIFT : 0));
        const SDL_Keycode k = SDL_GetKeyFromName(w[1].c_str());
        key(app, key_press(k, SDL_GetScancodeFromKey(k, nullptr), mod), false);
    } else if (ev == "message" && w.size() > 1) {
        std::string line;
        for (std::size_t i = 1; i < w.size(); ++i) line += (i > 1 ? " " : "") + w[i];
        event_message(app, line, 0xffffff);
    } else if (ev == "mission_message" && w.size() > 1) {  // slice M4: `mission_message [end won|lost] <text>`


        const bool end = w[1] == "end" && w.size() > 2;
        if (end) held(app).state->game_result = w[2] == "won" ? 0 : 1;
        std::string line;
        for (std::size_t i = end ? 3 : 1; i < w.size(); ++i) line += (line.empty() ? "" : " ") + w[i];
        queue_game_message(app, line, end);
    } else if (ev == "mark" && w.size() > 2) {
        ui::minimap_mark_add(app.minimap_marks, app.view()->map.width, app.view()->map.height,
                             static_cast<std::int32_t>(num(w, 1)), static_cast<std::int32_t>(num(w, 2)));
    } else if (ev == "train") {
        do_action(app, Action::Train, static_cast<int>(num(w, 1)));
    } else if (ev == "order") {
        std::string line;
        for (std::size_t i = 1; i < w.size(); ++i) line += (i > 1 ? "\t" : "") + w[i];
        std::string err;
        auto c = sim::Command_Parse(line, &err);
        if (c && c->parent != 0 && !app.queue.empty()) {
            app.queue.back().push_back(*c);  // a child line (parent set) belongs to the order before it
        } else if (c) {
            app.queue.push_back({*c});
        } else {
            SDL_Log("script: bad order (%s): %s", err.c_str(), line.c_str());
        }
    } else {
        SDL_Log("script: unknown event '%s'", ev.c_str());
    }
}

void run_script_events(App& app) {
    while (app.script_next < app.script.size() && app.script[app.script_next].tick <= app.view()->tick) {
        run_event(app, app.script[app.script_next++]);
    }
}

// ---- the end of a game (docs/endgame.md 3 and 4) ----

std::string utf8(const std::u16string& w) {
    std::string out;
    for (std::size_t i = 0; i < w.size(); ++i) {
        std::uint32_t c = w[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < w.size() && w[i + 1] >= 0xdc00 && w[i + 1] < 0xe000) {
            c = 0x10000 + ((c - 0xd800) << 10) + (w[++i] - 0xdc00);
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xc0 | c >> 6), out += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xe0 | c >> 12), out += static_cast<char>(0x80 | (c >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | c >> 18), out += static_cast<char>(0x80 | (c >> 12 & 0x3f));
            out += static_cast<char>(0x80 | (c >> 6 & 0x3f)), out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

// A player's name: the save's, else (a new game) yours is the profile's; computer players stay empty (the front-end
// names them IDS_COMP<slot + 1>, as AddPlayer does).
std::string player_name(const App& app, std::size_t p);

// The market panel's amounts with today's player names (its list shows them).
ui::MarketUi& market_ui(App& app) {
    app.market.names.clear();
    for (std::size_t p = 0; app.view() && p < app.view()->players.size(); ++p) {
        app.market.names.push_back(player_name(app, p));
    }
    return app.market;
}



std::string player_name(const App& app, std::size_t p) {
    const sim::Player& pl = app.view()->players[p];
    if (!pl.name.empty()) return ui::lang_data(&app.lang, utf8(pl.name));
    if (p != app.view()->my_player) return {};
    const ui::Profiles& pr = app.profiles;
    return ui::lang_data(&app.lang, pr.selected >= 0 && static_cast<std::size_t>(pr.selected) < pr.names.size()
                                        ? pr.names[pr.selected]
                                        : std::string("Player"));
}

void open_end_statistics(App& app);  // below

// After a tick: a game that just ended (TStrateg_Die set the end message and the pause) shows its message window over
// the view, through the front-end (opened for it when the game was started from a save).
bool check_end(App& app) {
    const sim::GameState& s = *app.view();
    if (app.end_shown || s.paused == 0 || s.end_message == 0) return true;
    app.end_shown = true;      // the horn came with the step (TStrateg_Die's sound event 9, game_events)
    if (s.end_message == 4) {
        std::string err;
        if (!app.menu) {
            app.menu = ui::frontend_open(app.renderer, loader(app), menu_fonts(), app.settings, app.profiles, app.seed,
                                         &err, &app.lang);
            if (!app.menu) {
                SDL_Log("end statistics: %s", err.c_str());
                return false;
            }
        }
        app.messages.clear(), app.message_shown = false;
        open_end_statistics(app);
        return true;
    }

    const ui::StringTable& st = panel_strings(app);
    auto text = [&](const char* key) {
        const auto it = st.find(key);
        return it != st.end() ? it->second : std::string(key);
    };
    // Slice M3: end_message 3 is a mission block's end text (a win or lose block, sim/mission_events.cpp).
    const std::string message = s.end_message == 3 ? display_tt_text(app, utf8(s.end_text))
                                : s.end_message == 2
                                    ? text("IDS_YOU_DEAD")
                                    : text("IDS_PLAYER") + player_name(app, s.my_player) + text("IDS_WIN");
    ui::mission_log_add(app.mission, message);
    // Slice M4: the end message goes before the waiting ones and replaces the window that is up (TStrateg_Die's goes
    // on top of the others; closing it opens the statistics).
    app.messages.push_front(GameMessage{message, true});
    app.message_shown = false;
    return show_messages(app);
}



void open_end_statistics(App& app) {
    ui::clear_selection(app.sel);
    app.sel.house.reset();
    sim::GameState& s = *held(app).state;
    sim::Statistics_Open(s);
    std::vector<ui::EndPlayer> players;
    for (const std::uint8_t p : sim::Statistics_Players(s)) {
        players.push_back(ui::EndPlayer{p, player_name(app, p), s.players[p].stat_colour, sim::Statistics_Of(s, p)});
    }
    ui::frontend_statistics(*app.menu, std::move(players));
}





bool show_messages(App& app) {
    if (app.messages.empty() || app.message_shown) return true;
    const GameMessage& m = app.messages.front();
    if (app.menu && !m.end && !ui::frontend_game_runs(*app.menu)) return true;
    std::string err;
    if (!app.menu) {
        app.menu = ui::frontend_open(app.renderer, loader(app), menu_fonts(), app.settings, app.profiles, app.seed,
                                     &err, &app.lang);
        if (!app.menu) {
            SDL_Log("game message: %s", err.c_str());
            return false;
        }
    }
    if (!ui::frontend_game_message(*app.menu, m.text, m.end, &err)) {
        SDL_Log("game message: %s", err.c_str());
        return false;
    }
    app.message_shown = true;
    app.dragging = app.panning = app.minimap_drag = app.left_down = false;  // the world gets no release now
    app.right_held = false;
    app.hud_armed = -1, app.window_down = -1, app.panel_down = false;
    if (app.view()) SDL_Log("game message at tick %u: %s", app.view()->tick, m.text.c_str());
    return true;
}



ui::Cursor game_cursor(App& app) {
    const sim::GameState& s = *app.view();
    ui::CursorState st;
    st.kept = app.set_exit                             ? ui::Cursor::SetExit
              : app.fire_point >= 0 || app.strike >= 0 ? ui::Cursor::AttackPoint
                                                       : ui::Cursor::Default;
    st.paused = app.paused || s.paused != 0;
    const float x = app.mouse_x, y = app.mouse_y;
    st.over_hud = in_panel(app, y) || over_panel(app, x, y) || over_minimap(app, x, y);
    st.building = app.build_type >= 0;
    ui::CursorFacts f;
    if (!st.paused && !st.over_hud && !st.building && st.kept == ui::Cursor::Default) {
        f = ui::cursor_facts(s, app.graphics ? &*app.graphics : nullptr, app.sel, app.pos, static_cast<int>(x),
                             static_cast<int>(y));
    }
    return ui::cursor_choice(st, f);
}

#ifndef __EMSCRIPTEN__
// ---- batch mode ----

bool save_png(App& app) {
    if (app.draw_cursor && app.started) {  // slice I9: the cursor as the original blits it, top left at the mouse
        if (!app.cursor_gfx) app.cursor_gfx = ui::cursor_graphics_open(app.renderer, loader(app));
        const float z = app.debug_hud ? 1.0f : static_cast<float>(app.zoom);
        SDL_SetRenderScale(app.renderer, z, z);
        ui::cursor_draw(*app.cursor_gfx, game_cursor(app), app.mouse_x, app.mouse_y);
        SDL_SetRenderScale(app.renderer, 1, 1);
    }
    SDL_Surface* shot = SDL_RenderReadPixels(app.renderer, nullptr);
    const bool ok = shot && SDL_SavePNG(shot, app.png_path.c_str());
    SDL_DestroySurface(shot);
    if (!ok) SDL_Log("cannot write %s: %s", app.png_path.c_str(), SDL_GetError());
    return ok;
}

void report(App& app) {
    const sim::GameState& s = *held(app).state;  // the live state: the hash covers the sim's caches too
    std::uint64_t calls = 0;
    for (const auto& [address, hit] : s.unported) calls += hit.count;
    std::fprintf(stderr, "antanta: tick %u, state hash %016llx, unported calls %llu\n", s.tick,
                 static_cast<unsigned long long>(sim::GameState_Hash(s)), static_cast<unsigned long long>(calls));
    for (const auto& [address, hit] : s.unported) {
        std::fprintf(stderr, "antanta: unported 0x%06x: %u calls, first at tick %u\n", address, hit.count,
                     hit.first_tick);
    }
}

// Batch mode draws with the software renderer into a surface of --size, the view letterboxed on it.
bool batch_renderer(App& app) {
    app.surface = SDL_CreateSurface(app.out_w, app.out_h, SDL_PIXELFORMAT_RGBA32);
    app.renderer = app.surface ? SDL_CreateSoftwareRenderer(app.surface) : nullptr;
    if (!app.renderer) {
        SDL_Log("cannot create the software renderer: %s", SDL_GetError());
        return false;
    }
    SDL_SetRenderLogicalPresentation(app.renderer, kViewWidth, kViewHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    return true;
}

// --bench-scroll <seconds> (batch, a measurement for docs/interface.md D.3 and D.5): the frame times of scrolling
// across the view at full edge speed in simulated 60 Hz frames, the world picture only (static layer, units, filter,
// the upload to the renderer's texture). Passes: as before smooth scrolling (a whole step every 80 ms, the static
// layer drawn again for each); the smooth camera with the static tiles; then twice with a house or a tree near the
// view's centre taken away or put back four times a second, the tiles all drawn again on a change (as before) and
// only those the change touches (draw_world). The camera turns at the view limits.
void bench_scroll(App& app, double seconds) {
    sim::GameState& live = *held(app).state;  // it changes houses and map objects, and puts them back
    const ui::CameraBounds b = ui::camera_bounds(app.live.map, app.view_w, app.view_h);
    if (!app.world_tex) {
        app.world_tex = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, app.view_w,
                                          app.view_h);
    }
    live_view_sync(app.live, live);
    load_view_graphics(*app.graphics, app.live.view, loader(app), &app.warnings);
    const ui::Camera start = app.cam;
    static constexpr const char* kPass[4] = {"steps ", "smooth", "churn, all tiles", "churn, local"};
    for (int pass = 0; pass < 4; ++pass) {
        const bool smooth = pass > 0, churn = pass > 1;
        ui::Camera cam = start;
        ui::camera_jump(cam, cam.x, cam.y, b);
        Target static_layer;
        ViewPos drawn{-1, -1};
        static_tiles_clear(app.tiles);
        app.tiles.drawn = 0;
        int dir_x = 1, dir_y = 1, changes = 0;
        double t = 0, next_step = 0;
        std::vector<double> ms;
        // The change in place: a house (player, slot, its hp) or a cell (index, its object); -1 none.
        int house_p = -1, house_k = -1, cell = -1;
        float house_hp = 0;
        std::uint16_t cell_object = 0;
        auto undo = [&] {
            if (house_p >= 0) live.players[house_p].houses[house_k].hp = house_hp;
            if (cell >= 0) live.map.objects[cell] = cell_object;
            house_p = house_k = cell = -1;
        };
        for (int frame = 0; t < seconds; t += 1.0 / 60, ++frame) {
            const double x0 = cam.x, y0 = cam.y;
            bool moved = smooth;
            if (smooth) {
                ui::camera_scroll(cam, dir_x, dir_y, ui::scroll_speed(app.settings.mouse_scroll, live.speed_ms),
                                  1.0 / 60, b);
            } else if (t >= next_step) {  // the old edge scroll: one step every 80 ms
                const ViewPos p = clamp_pos(
                    app, view_pos_at(static_cast<int>(cam.x) + 96 * dir_x, static_cast<int>(cam.y) + 48 * dir_y));
                cam.x = view_x(p), cam.y = view_y(p);
                next_step += 0.08;
                moved = true;
            }
            if (moved && cam.x == x0) dir_x = -dir_x;  // at a limit: turn
            if (moved && cam.y == y0) dir_y = -dir_y;
            const ViewPos pos =
                smooth ? ui::camera_view(cam) : view_pos_at(static_cast<int>(cam.x), static_cast<int>(cam.y));
            if (churn && frame % 15 == 14) {  // a change: put the last one back, or make a new one near the centre
                if (house_p >= 0 || cell >= 0) {
                    undo();
                } else {
                    const int cc = pos.col + app.view_w / 192, cr = pos.row + app.view_h / 48;
                    auto dist = [&](int x, int y) { return std::abs(x - cc) * 4 + std::abs(y - cr); };
                    int best = 1 << 30;
                    if (changes % 4 == 0) {
                        for (std::size_t p = 0; p < live.players.size(); ++p) {
                            for (std::size_t k = 0; k < live.players[p].houses.size(); ++k) {
                                const sim::House& sh = live.players[p].houses[k];
                                if (sh.hp >= -149000.0f && dist(sh.map_x, sh.map_y) < best) {
                                    best = dist(sh.map_x, sh.map_y);
                                    house_p = static_cast<int>(p), house_k = static_cast<int>(k);
                                }
                            }
                        }
                        if (house_p >= 0) {
                            house_hp = live.players[house_p].houses[house_k].hp;
                            live.players[house_p].houses[house_k].hp = -150001.0f;  // as a removed ruin
                        }
                    } else {
                        const int mw = live.map.width;
                        for (std::size_t i = 0; i < live.map.objects.size(); ++i) {
                            const int o = live.map.objects[i] & 0x7FFF;
                            const int x = static_cast<int>(i) % mw, y = static_cast<int>(i) / mw;
                            if (o >= 9000 && o < 0x251C && dist(x, y) < best) {
                                best = dist(x, y);
                                cell = static_cast<int>(i);
                            }
                        }
                        if (cell >= 0) {
                            cell_object = live.map.objects[cell];
                            live.map.objects[cell] = 0;
                        }
                    }
                }
                ++changes;
            }
            const Uint64 t0 = SDL_GetTicksNS();
            if (smooth) {
                if (live_view_sync(app.live, live)) {
                    load_view_graphics(*app.graphics, app.live.view, loader(app), &app.warnings);
                    if (pass == 2) {
                        static_tiles_clear(app.tiles);
                    } else {
                        static_tiles_apply_changes(app.tiles, *app.graphics, app.live);
                    }
                }
                render_frame_begin_tiles(*app.graphics, app.live.view, app.tiles, pos, app.view_w, app.view_h,
                                         &app.work, kPrefetchTiles);
                render_live_units(*app.graphics, app.sprites, loader(app), live, pos, app.work, &app.frame);
            } else {
                if (pos.col != drawn.col || pos.row != drawn.row) {
                    app.live.view.width = app.view_w, app.live.view.height = app.view_h;
                    render_static_layer(*app.graphics, app.live.view, pos, &static_layer);
                    drawn = pos;
                }
                render_live_frame(*app.graphics, app.sprites, loader(app), live, pos, static_layer, &app.work,
                                  &app.frame);
            }
            const SDL_Rect part{0, 0, app.view_w, app.view_h};
            SDL_UpdateTexture(app.world_tex, &part, app.frame.data(), app.view_w * 4);
            ms.push_back(static_cast<double>(SDL_GetTicksNS() - t0) / 1e6);
        }
        undo();
        live_view_sync(app.live, live);
        const double first = ms.front();  // the whole view drawn from nothing: reported apart
        ms.erase(ms.begin());
        std::vector<double> sorted = ms;
        std::sort(sorted.begin(), sorted.end());
        double sum = 0;
        for (double v : ms) sum += v;
        const auto over = std::count_if(ms.begin(), ms.end(), [](double v) { return v > 1000.0 / 60; });
        std::fprintf(stderr,
                     "bench %s: view %d x %d, %zu frames, mean %.2f ms, median %.2f, p95 %.2f, p99 %.2f, max %.2f, "
                     "over 16.7 ms %lld, tiles drawn %d, changes %d, first frame %.2f ms\n",
                     kPass[pass], app.view_w, app.view_h, ms.size(), sum / static_cast<double>(ms.size()),
                     sorted[sorted.size() / 2], sorted[sorted.size() * 95 / 100], sorted[sorted.size() * 99 / 100],
                     sorted.back(), static_cast<long long>(over), app.tiles.drawn, changes, first);
    }
    static_tiles_clear(app.tiles);
}

SDL_AppResult run_batch(App& app) {
    if (!app.renderer && !batch_renderer(app)) return SDL_APP_FAILURE;
    if (!show_messages(app)) return SDL_APP_FAILURE;  // slice M6: the text a loaded Lua mission shows again
    update_view(app);
    for (int i = 0; i < app.batch_ticks && app.started && app.view()->paused == 0; ++i) {
        run_script_events(app);
        if (!app.started) break;              // slice M4: the script left the game for the main menu
        if (!app.messages.empty()) continue;  // slice M4: a message window is up, the frame passes without a tick
        do_tick(app);
        run_screen_texts(app, app.view()->speed_ms);
        if (!show_messages(app)) return SDL_APP_FAILURE;
    }
    // Slice M3: a game that ended in the last tick (a mission's end text, TStrateg_Die) shows its message before the
    // script's steps at that tick, which may answer it.
    if (app.started && !check_end(app)) return SDL_APP_FAILURE;
    run_script_events(app);
    if (app.bench_seconds > 0) bench_scroll(app, app.bench_seconds);
    bool ok = check_end(app);
    if (ok && app.end_shown && app.end_clock >= 0 &&
        app.view()->end_message != 4) {  // --end-statistics: Enter on the message
        app.fixed_clock = app.end_clock;
        publish_clock(app);
        open_end_statistics(app);
    }
    if (ok && !app.png_path.empty()) {
        if (app.menu && !ui::frontend_overlay(*app.menu)) {
            menu_presentation(app);
            ui::frontend_draw(*app.menu);
        } else {
            draw_frame(app);
            if (app.menu) {
                menu_presentation(app);
                ui::frontend_draw(*app.menu);
            }
        }
        SDL_RenderPresent(app.renderer);
        ok = save_png(app);
        if (ok) SDL_Log("wrote %s", app.png_path.c_str());
    }
    report(app);
    for (const std::string& w : app.sprites.warnings) SDL_Log("sprites: %s", w.c_str());
    return ok ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
}

// ---- the network game (docs/multiplayer.md, slice MP1) ----
// --host <port> runs the server in this process (WebSocket on every interface, our own player on its loopback) and
// creates the game from --net-skirmish; --join <host:port> joins the first game there. The game starts when every
// human slot is taken. Ticks run when their frame from the server has arrived (NetGame); the local pause, the speed
// keys and the message windows do not stop them: pause and speed are requests to the server (the host's only).

std::int64_t net_ms() { return static_cast<std::int64_t>(SDL_GetTicks()); }

std::vector<std::string> split_text(const std::string& s, char sep) {
    std::vector<std::string> out;
    for (std::size_t start = 0; start <= s.size();) {
        std::size_t end = s.find(sep, start);
        if (end == std::string::npos) end = s.size();
        if (end > start) out.push_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

bool same_houses(const std::vector<sim::HouseRef>& a, const std::vector<sim::HouseRef>& b) {
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const sim::HouseRef& x, const sim::HouseRef& y) {
        return x.player == y.player && x.slot == y.slot;
    });
}

// "players=3;resources=1;map=Aust_3r_3.dat;slots=4/0/0/1,4/1/1/2,1/2/2/3;seed=7": the host's setup choices.
bool net_setup_from(const std::string& spec, sim::SkirmishSetup* s, std::string* err) {
    for (const std::string& item : split_text(spec, ';')) {
        const std::size_t eq = item.find('=');
        if (eq == std::string::npos) {
            *err = "--net-skirmish: '" + item + "' has no '='";
            return false;
        }
        const std::string key = item.substr(0, eq), value = item.substr(eq + 1);
        if (key == "players") {
            s->players = static_cast<std::uint8_t>(std::atoi(value.c_str()));
        } else if (key == "resources") {
            s->resources = static_cast<std::uint8_t>(std::atoi(value.c_str()));
        } else if (key == "map") {
            s->map = value;
        } else if (key == "seed") {
            s->seed = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
        } else if (key == "plus") {  // Entente Engine Plus (docs/your-version.md)
            s->plus = value == "1";
        } else if (key == "slots") {
            std::size_t row = 0;
            for (const std::string& slot : split_text(value, ',')) {
                int v[4] = {0, 0, 0, 0};
                if (row >= 8 || std::sscanf(slot.c_str(), "%d/%d/%d/%d", &v[0], &v[1], &v[2], &v[3]) != 4) {
                    *err = "--net-skirmish: slot '" + slot + "' is not level/nation/colour/team";
                    return false;
                }
                s->slots[row++] = sim::SkirmishSlot{static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]),
                                                    static_cast<std::uint8_t>(v[2]), static_cast<std::uint8_t>(v[3])};
            }
        } else {
            *err = "--net-skirmish: unknown choice '" + key + "'";
            return false;
        }
    }
    return true;
}

// ---- slice MP9: the seats we kept, the autosaves, a game from a save (docs/multiplayer.md) ----

std::optional<std::string> app_folder(const App& app, std::string* err);  // below
std::string saves_folder(App& app);                                       // below

// network-seats.txt in the user folder: the seats this player had (apps/common/net_store.hpp). A batch run without
// --user-dir neither reads nor writes it (the tests never touch the player's folder), nor writes autosaves without
// --user-dir or --save-dir.
std::string net_seats_path(const App& app) {
    if (app.batch_ticks >= 0 && app.user_dir.empty()) return {};
    std::string err;
    const auto folder = app_folder(app, &err);
    return folder ? *folder + "network-seats.txt" : std::string();
}

std::vector<NetSeatRecord> net_seats(const App& app) {
    std::string err;
    const std::string path = net_seats_path(app);
    const auto file = path.empty() ? std::nullopt : load_file(path, &err);
    return file ? NetSeats_Parse(std::string(file->begin(), file->end())) : std::vector<NetSeatRecord>{};
}

// Our seat (the game's uid, the slot, its token) into network-seats.txt whenever the server gave us a new one.
void net_remember_seat(App& app) {
    const net::Client& c = *app.net_client;
    if (c.uid() == 0 || c.token() == 0 || c.token() == app.net_seat_saved) return;
    app.net_seat_saved = c.token();
    if (net_seats_path(app).empty()) return;
    std::vector<NetSeatRecord> seats = net_seats(app);
    NetSeats_Put(seats, NetSeatRecord{c.uid(), app.net_join, c.slot(), c.token(), std::time(nullptr)});
    const std::string text = NetSeats_Format(seats), path = net_seats_path(app);
    std::string err;
    if (path.empty() || !save_file_atomic(path, Bytes(text.begin(), text.end()), &err)) {
        SDL_Log("network: the seat is not kept: %s", err.c_str());
    }
}

// The autosave of `tick` (prepared by NetGame at the tick) packed and written on the writer's thread into the ring of
// <saves>/network.
void net_autosave(App& app, std::uint32_t tick, sim::OwnSavePending save) {
    if (app.batch_ticks >= 0 && app.user_dir.empty() && app.save_dir.empty()) return;
    const std::string folder = saves_folder(app) + "/network";
    std::string err;
    if (saves_folder(app).empty() || !make_folder(folder, &err)) {
        SDL_Log("network game: no autosave: %s", err.c_str());
        return;
    }
    if (!app.net_writer) {
        app.net_writer = std::make_unique<NetAutosaveWriter>(
            [](const std::string& path, const Bytes& data, std::string* e) { return save_file_atomic(path, data, e); });
    }
    const std::uint32_t every = app.net_client->start() ? app.net_client->start()->autosave : 0;
    const std::string path = folder + "/" + NetAutosave_Name(app.net_client->uid(), tick, every);
    SDL_Log("network game: autosave at tick %u (%u us at the tick) to %s", tick, app.net_game->autosave_us(),
            path.c_str());
    app.net_writer->put(path, std::move(save));
}

// A network game from a save (a rejoin after a drop): the state as another player saved it, the ground of its map, the
// camera on our own player.
bool enter_net_save(App& app, std::unique_ptr<sim::GameState> state, std::string* err) {
    const std::string path = "Maps/" + state->map_name;
    auto map_file = loader(app)(path, err);
    if (!map_file) return false;
    auto map = parse_map(*map_file, err);
    if (!map) {
        *err = path + ": " + *err;
        return false;
    }
    MapData ground = std::move(*map);  // houses and units come from the state
    ground.houses.clear();
    ground.units.clear();
    ground.neutral_houses.clear();
    app.host.replace(std::move(state));
    app.save.reset();
    app.new_game = false, app.own_game = true;
    app.last_setup = app.net_client->setup();
    app.game_source = "network game from a save at tick " + std::to_string(app.view()->tick);
    zoom_reset(app);  // the start view depends on the view's size
    const ViewPos pos = start_view(app, *app.view(), ground);
    if (!enter_game(app, std::move(ground), pos, err)) return false;
    app.net_ai_seen.clear();  // net_control_lines: who the computer plays as the save has it, said only when it changes
    for (const sim::Player& p : app.view()->players) app.net_ai_seen.push_back(p.net_ai);
    return app.script_path.empty() || load_script(app, err);
}

// --net-resume (the network page's Resume): the save's setup, and our seat in it: the one whose token we kept, else
// the first not given to the computer for good.
bool net_resume_setup(App& app, std::string* err) {
    auto file = load_file(app.net_resume, err);
    if (!file) return false;
    auto state = std::make_unique<sim::GameState>();
    sim::OwnSaveMeta meta;
    if (!sim::OwnSave_Read(*file, state.get(), &meta, err)) {
        *err = app.net_resume + ": " + *err;
        return false;
    }
    if (!meta.setup || !meta.net || meta.net->seats.empty()) {
        *err = app.net_resume + " is not the save of a network game";
        return false;
    }
    std::uint8_t slot = 0xff;
    const std::vector<NetSeatRecord> kept = net_seats(app);
    if (const NetSeatRecord* r = NetSeats_Find(kept, meta.net->game_uid)) {
        for (const sim::OwnSaveSeat& s : meta.net->seats) {
            if (s.slot == r->slot && s.left == 0 && s.token_check == net::Token_Check(r->token)) slot = s.slot;
        }
    }
    for (const sim::OwnSaveSeat& s : meta.net->seats) {
        if (slot == 0xff && s.left == 0) slot = s.slot;
    }
    if (slot == 0xff) {
        *err = "every seat of this game was given to the computer";
        return false;
    }
    app.net_setup = *meta.setup;
    app.net_setup.my_slot = slot;
    app.net_resume_bytes.assign(file->begin(), file->end());
    return true;
}

// The lines a player sees when the computer takes over a side or gives it back (the server's net_control orders).
void net_control_lines(App& app) {
    const sim::GameState& s = *app.view();
    if (app.net_ai_seen.size() != s.players.size()) app.net_ai_seen.assign(s.players.size(), 0);
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        const std::uint8_t ai = s.players[p].net_ai;
        if (ai == app.net_ai_seen[p]) continue;
        app.net_ai_seen[p] = ai;
        std::string who = player_name(app, p);
        if (who.empty()) who = "Player " + std::to_string(p + 1);
        const std::string line =
            ai != 0 ? "The computer plays " + who + "'s side now" : who + " is back and plays again";
        SDL_Log("network game: %s (tick %u)", line.c_str(), s.tick);
        say(app, line);
    }
}

// The link to the server (and the server itself when hosting), the lobby's first steps, the lockstep driver.
bool net_open(App& app, std::string* err) {
    net::NetConfig cfg;  // the host's: the settings of Game options, then --net-set
    cfg.autosave_minutes = app.settings.net_autosave, cfg.seat_hold_s = app.settings.net_seat_hold * 60u;
    cfg.ai_level = app.settings.net_ai_level;
    for (const std::string& kv : app.net_settings) {
        const std::size_t eq = kv.find('=');
        if (eq == std::string::npos || !net::NetConfig_Set(cfg, kv.substr(0, eq), kv.substr(eq + 1))) {
            *err = "--net-set: not a setting: " + kv;
            return false;
        }
    }
    std::unique_ptr<net::Connection> link;
    // The game's creator: the host, or (slice MP8) --join with --net-skirmish, which creates the game on that server
    // (slice MP9: or resumes the game of --net-resume there).
    if (app.net_port > 0 || !app.net_choices.empty() || !app.net_resume.empty()) {
        // The host's setup: the choices, the computers' names, the advisor switches and speed of GameInf.txt.
        sim::SkirmishSetup& s = app.net_setup;
        if (!net_setup_from(app.net_choices, &s, err)) return false;
        const GameFileLoader load = loader(app);
        std::string e;
        if (auto st = load("Data/StringTable_eng.st", &e)) {
            if (auto table = parse_string_table(*st, &e)) sim::SkirmishSetup_CompNames(&s, *table);
        }
        const std::unique_ptr<sim::GameData> data = data_on_heap(app, nullptr, err);
        if (!data) return false;
        s.speed_ms = data->speed_ms, s.short_anims = data->short_anims;
        if (auto file = load("Data/GameInf.txt", &e)) {
            if (auto v = parse_gameinf(*file, &e)) s.economic = v->economic, s.military = v->military;
        }
        s.net = true;
        s.my_slot = 0;
        while (s.my_slot < 8 && s.slots[s.my_slot].level != 4 && s.slots[s.my_slot].level != 5) ++s.my_slot;
        if (!app.net_resume.empty() && !net_resume_setup(app, err)) return false;  // slice MP9: the save's setup, seat
        // The map's position count: the server works out each slot's player from it (SkirmishSetup_SlotPlayers).
        auto map_file = load("Maps/" + s.map, err);
        auto map = map_file ? parse_map(*map_file, err) : std::nullopt;
        if (!map) return false;
        app.net_positions = static_cast<std::uint8_t>(map->player_nations.size());
        if (!app.net_menu) app.net_tick_ms = cfg.tick_ms;  // the menu's: the host's setup screen chose it
    }
    if (app.net_port > 0) {
        app.net_hub = net::Loopback_NewHub(net_ms);
        net::ServerHooks hooks;
        hooks.log = [](const std::string& line) { SDL_Log("server: %s", line.c_str()); };
        if (!app.net_log_dir.empty()) {  // each game's order log, as antanta-server --log-dir writes it
            hooks.frames = [dir = app.net_log_dir](std::uint32_t game, const std::string& lines) {
                const std::string path = dir + "/game-" + std::to_string(game) + ".log";
                if (std::FILE* f = std::fopen(path.c_str(), "ab")) {
                    std::fwrite(lines.data(), 1, lines.size(), f);
                    std::fclose(f);
                }
            };
        }
        SDL_Log("network: hosting with autosave every %u min, a dropped seat kept %u s, the computer level %u",
                cfg.autosave_minutes, cfg.seat_hold_s, cfg.ai_level);
        app.net_server = std::make_unique<net::Server>(cfg, "antanta", net::Net_BuildId(), hooks);
        app.net_server->listen(net::Loopback_Listen(*app.net_hub, "own"));
        auto ws = net::Ws_Listen(app.net_bind, app.net_port, err);
        if (!ws) return false;
        app.net_server->listen(std::move(ws));
        link = net::Loopback_Connect(*app.net_hub, "own", {});
    } else {
        link = net::Ws_Connect("ws://" + app.net_join + "/", err);
        if (!link) return false;
    }
    if (!app.params.empty() || data_on_heap(app, nullptr, err)) {  // app.params for the hash and the view inputs
        app.net_client = std::make_unique<net::Client>(std::move(link), app.net_name, net::Net_BuildId(),
                                                       NetGame_DataHash(loader(app)));
    } else {
        return false;
    }
    NetGameHooks hooks;
    hooks.new_game = [&app](const sim::SkirmishSetup& setup, std::string* e) -> sim::GameState* {
        if (!start_new_skirmish(app, setup, e, true)) return nullptr;
        if (!app.script_path.empty() && !load_script(app, e)) return nullptr;
        return &*app.host.game().state;
    };
    hooks.adopt = [&app](std::unique_ptr<sim::GameState> s) -> sim::GameState* {  // slice MP9: back after a drop
        std::string e;
        if (!enter_net_save(app, std::move(s), &e)) {
            SDL_Log("network game: %s", e.c_str());
            return nullptr;
        }
        return &*app.host.game().state;
    };
    hooks.autosave = [&app](std::uint32_t tick, sim::OwnSavePending save) { net_autosave(app, tick, std::move(save)); };
    hooks.after_tick = [&app](const std::string& lines) {
        if (app.net_trace) std::fwrite(lines.data(), 1, lines.size(), app.net_trace);
        TickRecord r;
        r.tick = app.view()->tick;
        r.events.swap(app.host.game().state->events);
        after_tick(app, r);
    };
    hooks.load = loader(app);
    hooks.params = &app.params;
    hooks.trace = !app.trace_path.empty();  // the lines only for a trace file (docs/performance.md "Several cores")
    app.net_game = std::make_unique<NetGame>(*app.net_client, std::move(hooks));
    if (!app.trace_path.empty()) {  // the network game's trace (net::NetTrace_Tick), not the single player one
        app.net_trace = std::fopen(app.trace_path.c_str(), "wb");
        if (!app.net_trace) {
            *err = "cannot write " + app.trace_path;
            return false;
        }
        app.trace_path.clear();
    }
    app.net_wait_ms = net_ms();
    return true;
}

// At most max_ticks ticks whose frames are here: on the main thread (NetGame::run) or, with the sim thread (slice S3,
// docs/sim-thread.md), granted to it with what the tick needs from the client; a lockstep run waits for each, else
// no more than the ring takes are on their way (their records come at the next frames). Then the save the server asked
// for (the state after the last tick run) and the step time. False on an error.
bool net_ticks(App& app, int max_ticks) {
    NetGame& ng = *app.net_game;
    if (app.host.mode() != SimHost::Mode::Thread) return ng.run(max_ticks, nullptr) >= 0;
    if (!ng.begin()) return false;
    if (!ng.state()) return true;
    app.host.resume();  // ticks granted before a hold (a save) go on
    for (int n = 0; n < max_ticks && (app.host.lockstep() || app.host.in_flight() < 2); ++n) {
        std::optional<NetTick> t = ng.next_tick(app.net_trace != nullptr);
        if (!t) break;
        TickGrant g;
        g.net = std::move(*t);
        app.host.grant(std::move(g));
        TickRecord r;
        while (app.host.take(&r)) after_tick(app, r);
    }
    if (ng.save_asked()) ng.send_save(*held(app).state);
    ng.report_due();
    return true;
}

// One round of the network game: the server (when hosting) and the link polled, the lobby steps, our orders sent,
// then at most max_ticks ticks whose frames are here. False when the game cannot go on (error in app.net_error).
bool net_pump(App& app, int max_ticks) {
    net::Client& c = *app.net_client;
    if (app.net_server) app.net_server->poll(net_ms());
    c.poll();
    if (c.phase() == net::ClientPhase::Closed) {
        app.net_error = "network: " + c.error();
        return false;
    }
    const bool creates = app.net_port > 0 || !app.net_choices.empty() || !app.net_resume.empty();  // see net_open
    if (creates && c.phase() == net::ClientPhase::Lobby && !app.net_asked) {
        c.create_game(ui::lang_text(ui::lang_english(), "net.game_title", {{"name", app.net_name}}), app.net_setup,
                      app.net_positions, app.net_tick_ms, app.net_password, app.net_resume_bytes);
        app.net_asked = true;
    }
    if (!creates && c.phase() == net::ClientPhase::Lobby) {
        if (!c.games().empty() && !app.net_asked) {
            // Slice MP9: a game we had a seat in (network-seats.txt) is joined with that seat's token: a started one
            // takes us back (the rejoin), a resumed one gives us our seat in its lobby.
            const std::vector<NetSeatRecord> kept = net_seats(app);
            const net::GameInfo* mine = nullptr;
            const NetSeatRecord* seat = nullptr;
            for (const net::GameInfo& g : c.games()) {
                const NetSeatRecord* r = NetSeats_Find(kept, g.uid);
                if (mine == nullptr && r != nullptr) mine = &g, seat = r;
            }
            if (mine != nullptr && mine->started) {
                SDL_Log("network: back to the game %s (slot %u)", mine->name.c_str(), unsigned{seat->slot});
                c.rejoin(mine->id, seat->slot, seat->token);
            } else {
                c.join_game(mine ? mine->id : c.games()[0].id, 0xff, app.net_password, mine ? seat->token : 0);
            }
            app.net_asked = true;
        } else if (c.games().empty() && net_ms() - app.net_wait_ms > 300) {
            c.list_games();
            app.net_wait_ms = net_ms();
        }
    }
    if (!app.net_menu && c.host() && c.phase() == net::ClientPhase::Joined && !app.net_start_sent) {
        std::size_t humans = 0, joined = 0;
        for (std::size_t i = 0; i < 8 && i < app.net_setup.players; ++i) {
            humans += app.net_setup.slots[i].level == 4 || app.net_setup.slots[i].level == 5 ? 1 : 0;
        }
        for (const net::RosterEntry& r : c.roster()) joined += r.connected ? 1 : 0;
        if (!app.net_resume_bytes.empty()) {  // slice MP9: a resumed game's seats, each back or the computer's
            humans = c.roster().size(), joined = 0;
            for (const net::RosterEntry& r : c.roster()) joined += r.connected || r.computer != 0 ? 1 : 0;
        }
        if (joined == humans && humans != 0) {
            c.start_game();
            app.net_start_sent = true;
        }
    }
    if (app.started) {
        // Our orders go to the server; select_houses only when the flags we want changed since we sent them.
        build_hold(app);
        if (auto sel = ui::select_houses(*app.view(), app.sel)) {
            if (!same_houses(sel->houses, app.net_flagged)) {
                app.net_flagged = sel->houses;
                c.send_order({std::move(*sel)});
            }
        }
        while (!app.queue.empty()) {
            if (!app.queue.front().empty()) c.send_order(std::move(app.queue.front()));
            app.queue.pop_front();
        }
    }
    if (app.net_server) app.net_server->poll(net_ms());
    c.poll();
    if (c.phase() == net::ClientPhase::Joined || c.phase() == net::ClientPhase::Playing) net_remember_seat(app);
    for (const std::uint8_t slot : c.take_left()) {  // slice MP8: a player who left or dropped (his units stay)
        std::string who = "A player";
        for (const net::RosterEntry& r : c.roster()) {
            if (r.slot == slot && !r.name.empty()) who = r.name;
        }
        SDL_Log("network game: %s left (slot %u)", who.c_str(), unsigned{slot});
        if (app.started) say(app, who + " left the game");
    }
    if (!net_ticks(app, max_ticks)) {
        app.net_error = "network game: " + app.net_game->error();
        return false;
    }
    if (app.started) net_control_lines(app);
    if (const auto d = c.desync(); d && !app.net_desync_said) {
        app.net_desync_said = true;
        SDL_Log("network game out of step after tick %u (the server stopped it)", d->tick);
        if (app.started) say(app, "the network game is out of step: stopped");
    }
    if (app.started && !app.end_shown) {
        // The end for this machine's player (TStrateg_Die keeps no result in a network game): a line and the log.
        const std::uint8_t outcome = sim::NetGame_Outcome(*app.view(), app.view()->my_player);
        if (outcome != 3) {
            app.end_shown = true;
            const ui::StringTable& st = panel_strings(app);
            const auto it = st.find(outcome == 1 ? "IDS_YOU_DEAD" : "IDS_WIN");
            const std::string text = it != st.end() ? it->second : outcome == 1 ? "You lost" : "You won";
            ui::mission_log_add(app.mission, text);
            say(app, text);
            SDL_Log("network game: %s at tick %u", outcome == 1 ? "lost" : "won", app.view()->tick);
        }
    }
    return true;
}

// Batch mode (--frames n): the game runs to tick n with the script's events, one tick at a time, then leaves. The
// host keeps its server up until every other player has left. A run longer than --net-timeout seconds fails.
SDL_AppResult run_net_batch(App& app) {
    if (!app.renderer && !batch_renderer(app)) return SDL_APP_FAILURE;
    const std::int64_t t0 = net_ms(), limit = static_cast<std::int64_t>(app.net_timeout_s) * 1000;
    auto fail = [](const std::string& why) {
        SDL_Log("%s", why.c_str());
        return SDL_APP_FAILURE;
    };
    while (!app.started || app.view()->tick < static_cast<std::uint32_t>(app.batch_ticks)) {
        if (app.started) run_script_events(app);
        const std::uint32_t before = app.started ? app.view()->tick : 0;
        if (app.net_drop_at != 0 && app.started && before >= app.net_drop_at) {  // slice MP9: as a crash would
            SDL_Log("network game: the link dropped at tick %u (--net-drop-at)", before);
            app.net_client->drop();
            if (app.net_trace) std::fflush(app.net_trace);
            if (app.net_writer) app.net_writer->flush();
            return SDL_APP_SUCCESS;
        }
        if (!net_pump(app, 1)) return fail(app.net_error);
        if (net_ms() - t0 > limit) return fail("network game: --net-timeout reached");
        if (!app.started || app.view()->tick == before) SDL_Delay(1);
    }
    if (app.net_trace) std::fflush(app.net_trace);
    app.net_client->leave();
    while (net_ms() - t0 <= limit) {  // the goodbye out; the host serves the others to their end
        if (app.net_server) app.net_server->poll(net_ms());
        app.net_client->poll();
        const bool gone = app.net_client->phase() == net::ClientPhase::Closed;
        if (gone && (!app.net_server || app.net_server->games().empty())) break;
        SDL_Delay(1);
    }
    if (app.net_server && !app.net_server->games().empty()) return fail("network game: the others did not finish");
    if (app.net_writer) {
        app.net_writer->flush();
        SDL_Log("network game: %u autosaves written, the last took %u us at its tick%s", app.net_writer->written(),
                app.net_game->autosave_us(), app.net_writer->last_error().empty() ? "" : " (an autosave failed)");
    }
    SDL_Log("network game: %u ticks, net hash %016llx", app.view()->tick,
            static_cast<unsigned long long>(sim::GameState_NetHash(*held(app).state)));
    report(app);
    return SDL_APP_SUCCESS;
}

#ifndef __EMSCRIPTEN__
// ---- the network game from the menu (slice MP2, docs/multiplayer.md "Built in step 2") ----
// The Network game button's pages (ui/frontend.hpp) host and join the same game as --host and --join: the same link,
// server and lobby steps (net_open, net_pump), but the host starts it with Start in the lobby. Page 2 lists the games
// on this network (net/lan.hpp); a host's lobby answers for its game.

// Leaves the network game: the server's game, our server and the answers on this network; the lobby is forgotten.
void net_close(App& app) {
    if (app.net_client) {
        app.net_client->leave();
        app.net_client->poll();
    }
    if (app.net_server) app.net_server->poll(net_ms());
    app.net_writer.reset();  // the autosave being written is finished first
    app.net_game.reset();
    app.net_client.reset();
    app.net_server.reset();
    app.net_hub.reset();
    app.lan_host.reset();
    if (app.net_trace) std::fclose(app.net_trace), app.net_trace = nullptr;
    app.net_port = 0, app.net_join.clear();
    app.net_asked = app.net_start_sent = app.net_desync_said = false;
    app.net_flagged.clear();
    app.net_menu = false;
    app.net_lobby_shown.clear(), app.net_lobby_joined = 0;
    app.net_setup = sim::SkirmishSetup{};
    app.net_resume.clear(), app.net_resume_bytes.clear(), app.net_seat_saved = 0, app.net_ai_seen.clear();
    app.net_seat_pick = 0xff;
}

// Create on the host's setup (host) or Join on page 2: the link (and our server), then the lobby comes. Slice MP9: Join
// on a saved game's row hosts that game again (resume: the save's file).
void net_menu_open(App& app, bool host, const std::string& resume = {}) {
    const ui::NetChoices ch = ui::frontend_net_choices(*app.menu);
    net_close(app);
    app.net_menu = true;
    app.net_resume = resume;
    app.net_name = ch.name, app.net_password = ch.password;
    if (host) {
        app.net_port = ch.port, app.net_tick_ms = ch.tick_ms;
        app.net_setup = ui::frontend_skirmish_setup(*app.menu);
    } else {
        app.net_join = ch.address;
    }
    std::string err;
    if (!net_open(app, &err)) {
        SDL_Log("network: %s", err.c_str());
        net_close(app);
        ui::frontend_net_back(*app.menu,
                              ours(app, host ? "net.host_failed" : "net.join_failed", {{"reason", reason(app, err)}}));
        return;
    }
    if (host) {
        app.net_my_address = net::Lan_LocalAddress();
        app.lan_host = std::make_unique<net::LanHost>();
        if (!app.lan_host->open(app.net_bind, app.net_lan_port, &err)) {
            SDL_Log("network: the game is not listed on this network: %s", err.c_str());
            app.lan_host.reset();
        }
    } else {
        ui::frontend_message(*app.menu, ours(app, "net.joining", {{"address", ui::lang_data(&app.lang, ch.address)}}));
    }
}

// The lobby as the front-end shows it: the setup's open slots with the joined players' names, and what is going on.
ui::NetLobby net_lobby(App& app) {
    const net::Client& c = *app.net_client;
    const sim::SkirmishSetup& s = c.setup();
    ui::NetLobby l;
    l.host = c.host(), l.map = s.map, l.plus = s.plus;
    l.title = ours(app, "net.game_title", {{"name", ui::lang_data(&app.lang, app.net_name)}});
    for (const net::GameInfo& g : c.games()) {  // the name the host gave, in English: shown in the display language
        if (g.id == c.game()) l.title = ui::lang_data(&app.lang, ui::lang_reverse(app.lang, g.name, "net.game_title"));
    }
    int humans = 0, joined = 0;
    const char* levels[] = {"", "net.slot_easy", "net.slot_medium", "net.slot_difficult"};
    // Slice MP9: a resumed game's seats all come with the save (a name before anyone is back): each is back, waits for
    // its player, or the computer plays it; the host clicks the open ones.
    bool resumed = false;
    for (const net::RosterEntry& r : c.roster()) resumed = resumed || (!r.connected && !r.name.empty());
    resumed = resumed || !app.net_resume_bytes.empty();
    std::string picked;
    for (std::size_t i = 0; i < s.players && i < 8; ++i) {
        const sim::SkirmishSlot& sl = s.slots[i];
        if (sl.level == 0) continue;
        ui::NetLobbySlot row;
        row.nation = sl.nation, row.colour = sl.colour, row.team = sl.team, row.slot = static_cast<int>(i);
        row.human = sl.level == 4 || sl.level == 5;
        row.player = ours(app, row.human ? "net.slot_open" : levels[std::min<int>(sl.level, 3)]);
        for (const net::RosterEntry& r : c.roster()) {
            if (!row.human || r.slot != i) continue;
            const std::string& name = ui::lang_data(&app.lang, r.name);
            if (r.connected) {
                row.player = name, row.joined = true;
            } else if (r.computer != 0) {
                row.player = ours(app, "net.slot_computer_seat", {{"name", name}}), row.computer = true;
            } else if (!r.name.empty()) {
                row.player = ours(app, "net.slot_open_for", {{"name", name}});
            }
            if (r.slot == app.net_seat_pick) picked = r.name;
        }
        humans += row.human ? 1 : 0;
        joined += row.joined || row.computer ? 1 : 0;
        l.slots.push_back(row);
    }
    app.net_lobby_joined = joined;
    l.can_start = l.host && joined == humans;
    l.seats = l.host && resumed;
    if (joined < humans) {
        const int n = humans - joined;
        l.lines.push_back(ui::lang_plural(app.lang, "net.waiting", n));
    } else {
        l.lines.push_back(ours(app, l.host ? "net.all_in_host" : "net.all_in"));
    }
    if (l.seats) {
        l.lines.push_back(picked.empty() ? ours(app, "net.seat_help") : ours(app, "net.seat_move", {{"name", picked}}));
    }
    if (l.host) {
        l.lines.push_back(ours(app, "net.join_at",
                               {{"address", app.net_my_address.empty() ? ours(app, "net.this_pc")
                                                                       : ui::lang_data(&app.lang, app.net_my_address)},
                                {"port", std::to_string(app.net_port)}}));
        if (!app.net_password.empty()) l.lines.push_back(ours(app, "net.has_password"));
    }
    return l;
}

std::string lobby_key(const ui::NetLobby& l) {
    std::string k = l.title + "|" + l.map + "|" + (l.can_start ? "1" : "0") + (l.seats ? "s" : "");
    for (const ui::NetLobbySlot& s : l.slots) k += "|" + s.player + (s.joined ? "+" : "-") + (s.computer ? "c" : "");
    for (const std::string& line : l.lines) k += "|" + line;
    return k;
}

// A network game from the menu whose link ended (the host left, the server stopped it): the game is dropped and the
// main menu says why, instead of the app ending as a --host or --join game does.
// The text of a network game that ended: "no game answered at <address>" for a join that never connected (net/client.hpp
// kNoAnswerPrefix), else the reason.
std::string net_ended_text(const App& app, const std::string& why) {
    const std::string r = why.substr(why.find(':') + 2);
    const std::string p = net::kNoAnswerPrefix;
    if (r.rfind(p, 0) == 0) return ours(app, "net.no_answer", {{"address", r.substr(p.size())}});
    return ours(app, "net.ended", {{"reason", reason(app, r)}});
}

void net_game_lost(App& app, const std::string& why) {
    SDL_Log("%s", why.c_str());
    net_close(app);
    app.messages.clear(), app.message_shown = false;
    app.last_setup.reset();
    app.started = false;
    app.host.replace(nullptr);
    app.save.reset();
    std::string err;
    if (!app.menu) {
        app.menu = ui::frontend_open(app.renderer, loader(app), menu_fonts(), app.settings, app.profiles, app.seed,
                                     &err, &app.lang);
    }
    if (app.menu && ui::frontend_show(*app.menu, "main", &err)) {
        ui::frontend_message(*app.menu,
                             net_ended_text(app, why));
    }
    menu_presentation(app);
    audio::audio_music_play(app.audio.get(), 0);
}

// Slice MP9: page 2's rows of our own: the saved network games (the autosaves of <saves>/network and the saves of
// <saves> with a NET chunk, the newest first, at most 8; Join hosts one again) and the games on other servers we had a
// seat in during the last day (Join goes back to it: the rejoin, or our seat in its resumed lobby).
std::vector<ui::NetGameRow> net_saved_rows(App& app) {
    struct Found {
        std::int64_t at;
        ui::NetGameRow row;
    };
    std::vector<Found> found;
    const std::string folder = saves_folder(app);
    for (const std::string& dir : {folder + "/network", folder}) {
        if (folder.empty()) break;
        for (const std::string& file : list_files(dir, ".sav")) {
            std::string err;
            const std::string path = dir + "/" + file;
            const auto bytes = load_file(path, &err);
            const auto info = bytes ? sim::OwnSave_ReadInfo(*bytes, &err) : std::nullopt;
            const auto net = info ? sim::OwnSave_ReadNet(*bytes, &err) : std::nullopt;
            if (!net) continue;
            ui::NetGameRow r;
            r.save = r.address = path, r.map = info->map_name, r.players = info->player;
            char when[48] = "";
            const std::time_t t = static_cast<std::time_t>(info->saved_at);
            if (const std::tm* tm = std::localtime(&t)) {
                std::snprintf(when, sizeof when, "%02d.%02d.%02d %02d:%02d", tm->tm_mday, tm->tm_mon + 1,
                              tm->tm_year % 100, tm->tm_hour, tm->tm_min);
            }
            r.when = when;
            r.name = ours(app, "net.saved_row",
                          {{"map", ui::lang_data(&app.lang, info->map_name.size() > 4
                                                                ? info->map_name.substr(0, info->map_name.size() - 4)
                                                                : info->map_name)},
                           {"when", r.when}});
            found.push_back({info->saved_at, std::move(r)});
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.at > b.at; });
    std::vector<ui::NetGameRow> out;
    const std::int64_t now = std::time(nullptr);
    for (const NetSeatRecord& s : net_seats(app)) {
        if (!s.address.empty() && now - s.seen < 24 * 3600) {
            ui::NetGameRow r;
            r.address = s.address,
            r.name = ours(app, "net.back_to", {{"address", ui::lang_data(&app.lang, s.address)}});
            out.push_back(std::move(r));
        }
    }
    for (std::size_t k = 0; k < found.size() && k < 8; ++k) out.push_back(std::move(found[k].row));
    return out;
}

// Slice MP9: the host's click on a seat of a resumed game's lobby: an open seat goes to the computer (or back), a
// joined player is picked and goes to the open seat clicked next.
void net_seat_click(App& app, int slot) {
    net::Client& c = *app.net_client;
    for (const net::RosterEntry& r : c.roster()) {
        if (r.slot != slot) continue;
        if (r.connected) {
            app.net_seat_pick = slot == c.slot() || app.net_seat_pick == slot ? 0xff : static_cast<std::uint8_t>(slot);
        } else if (app.net_seat_pick != 0xff && r.computer == 0) {
            c.send_request(net::RequestKind::SeatMove, static_cast<std::uint32_t>(app.net_seat_pick | (slot << 8)));
            app.net_seat_pick = 0xff;
        } else {
            c.send_request(net::RequestKind::SeatComputer, static_cast<std::uint32_t>(slot));
        }
    }
    app.net_lobby_shown.clear();  // the lines change
}

// One frame of the network pages: the games on this network while a page is up, our link and the lobby, the game's
// start (the menu closes), a failure back to page 2 with a message. Over a running network game (the in-game menu)
// only its ticks.
SDL_AppResult net_menu_frame(App& app) {
    if (app.started) {
        if (app.net_client && !ui::frontend_game_runs(*app.menu) && !net_pump(app, 50)) {
            if (!app.net_menu) {
                SDL_Log("%s", app.net_error.c_str());
                return SDL_APP_FAILURE;
            }
            net_game_lost(app, app.net_error);
        }
        return SDL_APP_CONTINUE;
    }
    if (ui::frontend_net_screen(*app.menu) == 0 && !app.net_client) {
        app.lan_finder.reset();
        app.net_saved_read = false;
        return SDL_APP_CONTINUE;
    }
    bool rows = false;
    if (!app.net_saved_read && ui::frontend_net_screen(*app.menu) == 2) {  // slice MP9: read once a visit
        app.net_saved = net_saved_rows(app), app.net_saved_read = rows = true;
    }
    if (!app.lan_finder) {
        app.lan_finder = std::make_unique<net::LanFinder>();
        std::string err;
        if (!app.lan_finder->open(app.net_bind == "127.0.0.1" ? app.net_bind : std::string(), app.net_lan_port, &err)) {
            SDL_Log("network: no search for games on this network: %s", err.c_str());
        }
    }
    if (app.lan_finder->poll(net_ms()) || rows) {
        std::vector<ui::NetGameRow> found;
        for (const net::LanGame& g : app.lan_finder->games()) {
            // The name the host gave, in English (the network's): shown in the display language.
            found.push_back({ui::lang_reverse(app.lang, g.name, "net.game_title"),
                             g.map,
                             g.address,
                             g.humans,
                             g.max_humans,
                             g.locked,
                             {},
                             {},
                             {}});
        }
        found.insert(found.end(), app.net_saved.begin(), app.net_saved.end());
        ui::frontend_net_games(*app.menu, found);
    }
    if (!app.net_client) return SDL_APP_CONTINUE;
    if (!net_pump(app, 50)) {
        const std::string why = app.net_error;
        SDL_Log("%s", why.c_str());
        net_close(app);
        ui::frontend_net_back(*app.menu,
                              net_ended_text(app, why));
        return SDL_APP_CONTINUE;
    }
    if (app.started) {  // the server started the game: the menu goes, the game takes the window
        app.menu.reset();
        app.lan_host.reset(), app.lan_finder.reset();
        app.last_ns = SDL_GetTicksNS();
        return SDL_APP_CONTINUE;
    }
    const net::Client& c = *app.net_client;
    if (app.lan_host) {
        net::LanGame g;
        g.name =
            ui::lang_text(ui::lang_english(), "net.game_title", {{"name", app.net_name}});  // English: the network's
        g.map = app.net_setup.map, g.locked = !app.net_password.empty();
        for (const net::GameInfo& info : c.games()) {
            if (info.id == c.game()) g.name = info.name;
        }
        int humans = 0, joined = 0;
        for (std::size_t i = 0; i < app.net_setup.players && i < 8; ++i) {
            const std::uint8_t level = app.net_setup.slots[i].level;
            humans += level == 4 || level == 5 ? 1 : 0;
        }
        for (const net::RosterEntry& r : c.roster()) joined += r.connected ? 1 : 0;
        g.humans = static_cast<std::uint8_t>(joined), g.max_humans = static_cast<std::uint8_t>(humans);
        app.lan_host->set(g, static_cast<std::uint16_t>(app.net_port), c.phase() == net::ClientPhase::Joined);
        app.lan_host->poll();
    }
    if (c.phase() == net::ClientPhase::Joined) {
        const ui::NetLobby l = net_lobby(app);
        const std::string key = lobby_key(l);
        if (key != app.net_lobby_shown) {
            app.net_lobby_shown = key;
            std::string err;
            if (!ui::frontend_net_lobby(*app.menu, l, &err)) SDL_Log("front-end: %s", err.c_str());
        }
    }
    return SDL_APP_CONTINUE;
}
#endif

#ifndef ANTANTA_RELEASE  // developer builds only: the release exe has no paths of the project in it
std::string default_save() {
    const char* base = SDL_GetBasePath();  // engine/build/<config>/
    return std::string(base ? base : "") + "../../../out/saves/Test1_2026-10-13.svg";
}
#endif

#endif

// ---- front-end (--menu) ----

// Times New Roman Bold: the owner's own copy in the Windows font folder (read, never copied), else the free Liberation
// Serif Bold vendored in third_party (PLAN.md 2026-10-15). The web build embeds the Liberation files at
// /third_party/... (CMakeLists.txt), which the same path reaches from its base path "/".
std::vector<std::string> menu_fonts() {
    std::vector<std::string> out;
    if (const char* windir = std::getenv("WINDIR")) out.push_back(std::string(windir) + "/Fonts/timesbd.ttf");
    const char* base = SDL_GetBasePath();  // engine/build/<config>/
    out.push_back(std::string(base ? base : "") +
                  "../../third_party/liberation-fonts-ttf-2.1.5/LiberationSerif-Bold.ttf");
    return out;
}

// The face Windows gives "BankGothic RUSS" (Arial, docs/frontend.md 7): the owner's own arial.ttf (read, never copied),
// else the free Liberation Sans vendored in third_party.
std::vector<std::string> sans_fonts() {
    std::vector<std::string> out;
    if (const char* windir = std::getenv("WINDIR")) out.push_back(std::string(windir) + "/Fonts/arial.ttf");
    const char* base = SDL_GetBasePath();  // engine/build/<config>/
    out.push_back(std::string(base ? base : "") +
                  "../../third_party/liberation-fonts-ttf-2.1.5/LiberationSans-Regular.ttf");
    return out;
}

// Our folder (settings, profiles, progress, saves): --user-dir, else %APPDATA%\EntenteEngine\ (platform/files.hpp).
// Ends with a separator.
std::optional<std::string> app_folder(const App& app, std::string* err) {
    if (app.user_dir.empty()) return user_folder(err);
    if (!make_folder(app.user_dir, err)) return std::nullopt;
    const char last = app.user_dir.back();
    return last == '/' || last == '\\' ? app.user_dir : app.user_dir + "/";
}

// The first settings: the game's Data/GameInf.txt, else the shipped values.
ui::Settings first_settings(const App& app) {
    std::string err;
    auto file = loader(app)("Data/GameInf.txt", &err);
    std::optional<GameInf> g;
    if (file) g = parse_gameinf(*file, &err);
    if (!g) {
        SDL_Log("settings: %s; using the shipped values", err.c_str());
        return ui::Settings{};
    }
    return ui::settings_from_gameinf(*g);
}


ui::Profiles first_profiles(const App& app) {
    std::string err;
    auto file = loader(app)("Save/splayers.dat", &err);
    std::optional<ui::Profiles> p;
    if (file) p = ui::parse_splayers(*file, &err);
    if (!p) {
        SDL_Log("profiles: %s; starting with the profile \"Player\"", err.c_str());
        return ui::Profiles{};
    }
    return *p;
}

void load_profiles(App& app) {
    std::string err;
    auto folder = app_folder(app, &err);
    if (!folder) {
        SDL_Log("%s: the profiles will not be stored", err.c_str());
        app.profiles = first_profiles(app);
        return;
    }
    app.profiles_path = *folder + "profiles.txt";
    if (!SDL_GetPathInfo(app.profiles_path.c_str(), nullptr)) {
        SDL_Log("profiles: %s does not exist yet, taking Save/splayers.dat", app.profiles_path.c_str());
        app.profiles = first_profiles(app);
        return;
    }
    auto file = load_file(app.profiles_path, &err);
    std::optional<ui::Profiles> p;
    if (file) p = ui::profiles_parse(std::string(file->begin(), file->end()), &err);
    if (!p) {
        SDL_Log("%s: %s; taking Save/splayers.dat", app.profiles_path.c_str(), err.c_str());
        app.profiles = first_profiles(app);
        return;
    }
    app.profiles = *p;
}

// The display language (docs/languages.md 4): --lang, else the setting, else the first of the system's languages
// (SDL_GetPreferredLocales; in the browser navigator.languages) that has a folder, else English. A run with a window
// lists the built-in languages and those of the user folder's lang/; a batch run takes English unless --lang names
// another, never the system's or the user folder's (tests and the gate draw the same everywhere); only a batch run
// given both --lang and --user-dir reads that folder's lang/ (a language's game texts in a test). A new language takes
// effect at once: the tables shown are read again, the open windows keep their texts (ui/frontend.hpp). `reload`: the
// same language read again, its game texts just came from the server (slice L3b).
void choose_language(App& app, bool reload = false) {
    std::string dir, err;
    if (app.interactive || (!app.cli_lang.empty() && !app.user_dir.empty())) {
        if (auto folder = app_folder(app, &err)) dir = *folder + "lang";
    }
    std::vector<std::string> warnings;
    std::vector<ui::LangInfo> offered = ui::lang_list(dir, &warnings);
    std::string code = !app.cli_lang.empty() ? app.cli_lang : app.settings.language;
    if (code.empty() && app.interactive) {
        std::vector<std::string> preferred;
        int n = 0;
        if (SDL_Locale** locales = SDL_GetPreferredLocales(&n)) {
            for (int i = 0; i < n; ++i) {
                if (locales[i] && locales[i]->language) preferred.push_back(locales[i]->language);
            }
            SDL_free(locales);
        }
        code = ui::lang_pick(preferred, offered);
    }
    if (code.empty()) code = "en";
    if (!reload && code == app.lang.info.code && !app.lang.offered.empty()) return;  // chosen before, no change
    std::optional<ui::Language> l = ui::lang_load(code, dir, &warnings, &err);
    if (!l) {
        SDL_Log("language: %s, English instead", err.c_str());
        l = ui::lang_load("en", dir, &warnings, &err);
    }
    for (const std::string& w : warnings) SDL_Log("language: %s", w.c_str());
    if (!l) return;
    const bool changed = reload || l->info.code != app.lang.info.code;
    app.lang = std::move(*l);
    app.lang.offered = std::move(offered);
    {  // the release exe's crash box (platform/session_log.hpp): its text ready before a crash
        const std::string box = ours(app, "crash.box");
        const std::size_t at = box.find("{path}");
        session_log_set_crash_box(box.substr(0, at), at == std::string::npos ? std::string() : box.substr(at + 6));
    }
    SDL_Log("language: %s (%s)%s", app.lang.info.code.c_str(), app.lang.info.name.c_str(),
            app.lang.pseudo ? ", the marker check is on" : "");
    if (changed) {
        app.strings.reset(), app.display_tt.reset();
        if (app.menu) ui::frontend_language_changed(*app.menu);
    }
}

void store_settings(const App& app);  // below
void load_settings_file(App& app);    // below

void load_settings(App& app) {
    load_settings_file(app);
    // --lang-setting (the browser's start page): a language chosen there, kept as Game options' Language row does.
    if (!app.cli_lang_setting.empty()) {
        app.settings.language = app.cli_lang_setting;
        app.cli_lang_setting.clear();
        store_settings(app);
    }
    choose_language(app);
}

// Only the language of the settings file (and --lang-setting's), for a game that starts from a save with a window: it
// is entered before load_settings reads the rest, and its mission log and windows are to show the chosen language, not
// the system's (docs/languages.md, slice L2). The other settings stay as they are until load_settings.
void load_language_setting(App& app) {
    const ui::Settings before = app.settings;
    load_settings_file(app);
    std::string language = app.cli_lang_setting.empty() ? app.settings.language : app.cli_lang_setting;
    app.settings = before;
    app.settings.language = std::move(language);
    choose_language(app);
}

void load_settings_file(App& app) {
    std::string err;
    auto folder = app_folder(app, &err);
    if (!folder) {
        SDL_Log("%s: the settings will not be stored", err.c_str());
        app.settings = first_settings(app);
        return;
    }
    app.settings_path = *folder + "settings.txt";
    if (!SDL_GetPathInfo(app.settings_path.c_str(), nullptr)) {
        SDL_Log("settings: %s does not exist yet, taking Data/GameInf.txt", app.settings_path.c_str());
        app.settings = first_settings(app);
        return;
    }
    auto file = load_file(app.settings_path, &err);
    std::optional<ui::Settings> s;
    if (file) s = ui::settings_parse(std::string(file->begin(), file->end()), &err);
    if (!s) {
        SDL_Log("%s: %s; taking Data/GameInf.txt", app.settings_path.c_str(), err.c_str());
        app.settings = first_settings(app);
        return;
    }
    app.settings = *s;
}

// The interface options of the command line over the settings (batch runs and tests).
void apply_cli_settings(App& app) {
    if (app.cli_ui_scale) app.settings.ui_scale = *app.cli_ui_scale;
    if (app.cli_ui_pictures) app.settings.ui_pictures = *app.cli_ui_pictures;
    if (app.cli_zoom) app.settings.world_zoom = *app.cli_zoom;
}

// The web: our folder /libsdl/ is the browser's IndexedDB (IDBFS, mounted by the page); a written file reaches it
// only when the page copies the folder over, which this asks for (in the background). Nothing on the desktop.
void web_store() {
#ifdef __EMSCRIPTEN__
    EM_ASM({
        if (Module.antantaStore) Module.antantaStore();
    });
#endif
}

void store_settings(const App& app) {
    if (app.settings_path.empty()) return;
    const std::string text = ui::settings_print(app.settings);
    std::string err;
    if (!save_file(app.settings_path, ByteSpan(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()),
                   &err)) {
        SDL_Log("%s", err.c_str());
    }
    web_store();
}

void store_profiles(const App& app) {
    if (app.profiles_path.empty()) return;
    const std::string text = ui::profiles_print(app.profiles);
    std::string err;
    if (!save_file(app.profiles_path, ByteSpan(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()),
                   &err)) {
        SDL_Log("%s", err.c_str());
    }
    web_store();
}

// Slice M4, the progress (ui/progress.hpp): progress.txt in our folder; missing or unreadable, none yet.
void load_progress(App& app) {
    std::string err;
    auto folder = app_folder(app, &err);
    if (!folder) {
        SDL_Log("%s: the progress will not be stored", err.c_str());
        return;
    }
    app.progress_path = *folder + "progress.txt";
    if (!SDL_GetPathInfo(app.progress_path.c_str(), nullptr)) return;
    auto file = load_file(app.progress_path, &err);
    std::optional<ui::Progress> p;
    if (file) p = ui::progress_parse(std::string(file->begin(), file->end()), &err);
    if (!p) {
        SDL_Log("%s: %s; starting without progress", app.progress_path.c_str(), err.c_str());
        return;
    }
    app.progress = std::move(*p);
}

void store_progress(const App& app) {
    if (app.progress_path.empty()) return;
    const std::string text = ui::progress_print(app.progress);
    std::string err;
    if (!save_file(app.progress_path, ByteSpan(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()),
                   &err)) {
        SDL_Log("%s", err.c_str());
    }
    web_store();
}




std::string progress_profile(App& app);  // slice M7, below
void record_progress(App& app) {
    if (!app.view() || app.view()->game_result != 0) return;
    const auto key = ui::progress_key(app.view()->mission_path);
    if (!key) return;
    const std::string profile = progress_profile(app);
    if (ui::progress_add(app.progress, profile, key->first, key->second)) {
        SDL_Log("progress: %s won %s/%s", profile.c_str(), key->first.c_str(), key->second.c_str());
    }
    store_progress(app);
}

#ifndef __EMSCRIPTEN__
// The height of the window's title bar, kept free above it on the display (40 when the system does not say).
int title_bar_height(SDL_Window* window) {
    int top = 0;
    return SDL_GetWindowBordersSize(window, &top, nullptr, nullptr, nullptr) && top > 0 ? top : 40;
}

ui::WindowRect usable_bounds(SDL_DisplayID display) {
    SDL_Rect r{};
    if (!SDL_GetDisplayUsableBounds(display, &r)) return {};
    return {r.x, r.y, r.w, r.h};
}

// A size picked in the Options resolution picker: the window gets that size (ours; the player may then change it by
// hand). A maximised window is restored first and the window is kept on its display. Called after leaving full screen.
void apply_resolution(App& app) {
    const ui::Size size = ui::resolution_size(app.settings.resolution);
    if (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MAXIMIZED) {
        SDL_RestoreWindow(app.window);
        SDL_SyncWindow(app.window);
    }
    ui::WindowRect r{0, 0, size.w, size.h};
    SDL_GetWindowPosition(app.window, &r.x, &r.y);
    const ui::WindowRect usable = usable_bounds(SDL_GetDisplayForWindow(app.window));
    if (usable.w > 0) r = ui::window_clamp(r, usable, title_bar_height(app.window));
    SDL_SetWindowSize(app.window, r.w, r.h);
    SDL_SetWindowPosition(app.window, r.x, r.y);
}
#endif

// Full screen or the window (Alt+Enter, the Options resolution picker's Full screen). Ours on the desktop: borderless
// full screen at the desktop's mode; leaving it gives back the window as it was. The web: the canvas fills the page
// (SDL_WINDOW_FILL_DOCUMENT) and full screen is the page's, which the browser allows only shortly after a click or a
// key (Play, Alt+Enter); the setting follows the page (SDL_AppEvent).
void apply_fullscreen(App& app) {
#ifdef __EMSCRIPTEN__
    web_fullscreen(app.settings.fullscreen ? 1 : 0);
#else
    SDL_SetWindowFullscreen(app.window, app.settings.fullscreen);
    SDL_SyncWindow(app.window);
#endif
}

// Ours (desktop): the window of the last run (Settings::window_*, ui::window_place): its place when that is still on a
// display, else centred on the primary display; maximised again if it was; full screen only then, so leaving full
// screen gives back that window. The window was created hidden and is shown here. The web has only the page.
void place_window(App& app) {
#ifdef __EMSCRIPTEN__
    apply_fullscreen(app);
#else
    std::vector<ui::WindowRect> usable;
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    if (const ui::WindowRect r = usable_bounds(primary); r.w > 0) usable.push_back(r);
    int n = 0;
    if (SDL_DisplayID* ids = SDL_GetDisplays(&n)) {
        for (int i = 0; i < n; ++i) {
            if (ids[i] == primary) continue;
            if (const ui::WindowRect r = usable_bounds(ids[i]); r.w > 0) usable.push_back(r);
        }
        SDL_free(ids);
    }
    const ui::WindowPlace p = ui::window_place(app.settings, usable, title_bar_height(app.window));
    SDL_SetWindowSize(app.window, p.r.w, p.r.h);
    SDL_SetWindowPosition(app.window, p.r.x, p.r.y);
    if (p.maximized) SDL_MaximizeWindow(app.window);
    if (app.settings.fullscreen) SDL_SetWindowFullscreen(app.window, true);
    SDL_ShowWindow(app.window);
    app.remember_window = !app.settings_path.empty();
#endif
}

// Ours (desktop): the window was moved, resized, maximised or restored: its place goes into the settings (a plain
// window's rectangle; maximised or not) and is stored once it has rested (SDL_AppIterate) or at the end.
#ifndef __EMSCRIPTEN__
void note_window(App& app) {
    const SDL_WindowFlags flags = SDL_GetWindowFlags(app.window);
    if (flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) return;
    ui::Settings& s = app.settings;
    s.window_maximized = (flags & SDL_WINDOW_MAXIMIZED) != 0;
    if (!s.window_maximized) {
        SDL_GetWindowPosition(app.window, &s.window_x, &s.window_y);
        SDL_GetWindowSize(app.window, &s.window_w, &s.window_h);
        s.window_x = std::clamp(s.window_x, -32768, 32767), s.window_y = std::clamp(s.window_y, -32768, 32767);
        s.window_w = std::clamp(s.window_w, 0, 32767), s.window_h = std::clamp(s.window_h, 0, 32767);
    }
    app.window_store_ns = SDL_GetTicksNS() + SDL_NS_PER_SECOND;
}
#endif

// The videos' sound follows the music setting, on / off and volume (ours: no volume call was found in the original's
// video code, so there they play at the system level; the music is what they replace).
float video_volume(const ui::Settings& s) {
    return s.music_on ? static_cast<float>(std::min<int>(s.music_volume, 100)) / 100.0f : 0.0f;
}

// The canvas of the front-end's windows: 1024 x 768 letterboxed, except for the in-game menu over the interface (not
// the debug HUD), which gets the game view at the interface's scale, so it sits 1:1 in the middle of the view as the
// original's (centred on its 1280 x 1024 view). At least 1024 x 768, so the full-screen Game options fits.
void menu_presentation(App& app) {
    int cw = kViewWidth, ch = kViewHeight;
    if (app.started && !app.debug_hud && ui::frontend_game_window_open(*app.menu)) {  // slice I8: and its windows
        int ow = 0, oh = 0;
        SDL_GetRenderOutputSize(app.renderer, &ow, &oh);
        const int k = std::max(1, app.ui_scale);
        cw = std::max(cw, ow / k), ch = std::max(ch, oh / k);
    }
    ui::frontend_set_canvas(*app.menu, cw, ch);
    SDL_SetRenderLogicalPresentation(app.renderer, cw, ch, SDL_LOGICAL_PRESENTATION_LETTERBOX);
}




void open_game_menu(App& app) {
    if (!app.started || app.paused || app.menu || !app.view() || app.view()->paused != 0) return;
    std::string err;
    app.menu = ui::frontend_open(app.renderer, loader(app), menu_fonts(), app.settings, app.profiles, app.seed, &err,
                                 &app.lang);
    if (app.menu && app.batch_ticks >= 0) ui::frontend_set_animate(*app.menu, false);  // the load screen's door
    const bool can_restart =  // slice M4; not in a network game (slice MP2)
        !app.net_client && (app.last_setup.has_value() || !app.view()->mission_path.empty());
    if (!app.menu || !ui::frontend_game_menu(*app.menu, can_restart, &err)) {
        SDL_Log("in-game menu: %s", err.c_str());
        app.menu.reset();
        return;
    }
    app.dragging = app.panning = app.minimap_drag = app.left_down = false;  // the world gets no release now
    app.right_held = false;
    app.hud_armed = -1;
    if (app.view()->my_player < app.view()->players.size()) {
        const sim::Player& me = app.view()->players[app.view()->my_player];
        ui::GameAdvisors adv;
        adv.economic = {me.field_9b2b38, me.field_9b2b39, me.field_9b2b3a,
                        me.field_9b2b3b, me.field_9b2b3c, me.field_9b2b3d};
        adv.military = me.field_9b2a98, adv.conduct = me.ai_kind;
        ui::frontend_set_game_advisors(*app.menu, adv);
    }
    menu_presentation(app);
    SDL_Log("in-game menu open at tick %u", app.view()->tick);
}

// Slice I8: an in-game window through the front-end, as the in-game menu: only while the game runs and no other window
// is up. `open` fills the front-end.
template <class Open>
void open_game_window(App& app, const char* what, Open&& open) {
    if (!app.started || app.paused || app.menu || !app.view() || app.view()->paused != 0) return;
    std::string err;
    app.menu = ui::frontend_open(app.renderer, loader(app), menu_fonts(), app.settings, app.profiles, app.seed, &err,
                                 &app.lang);
    if (!app.menu || !open(*app.menu, &err)) {
        SDL_Log("%s: %s", what, err.c_str());
        app.menu.reset();
        return;
    }
    app.dragging = app.panning = app.minimap_drag = app.left_down = false;  // the world gets no release now
    app.right_held = false;
    app.hud_armed = -1, app.window_down = -1, app.panel_down = false;
    menu_presentation(app);
    SDL_Log("%s open at tick %u", what, app.view()->tick);
}


void open_mission(App& app) {
    const ui::StringTable& st = panel_strings(app);
    const auto it = st.find("IDS_RANDOM_MAP");
    std::string title = it != st.end() ? it->second : "IDS_RANDOM_MAP";
    if (!app.mission_title.empty()) title = app.mission_title;
    open_game_window(app, "mission window", [&](ui::Frontend& f, std::string* err) {
        return ui::frontend_mission(f, title, ui::mission_log_paragraphs(app.mission), err);
    });
}



void open_hints(App& app) {
    if (!app.hints) {
        std::string err;
        auto file = loader(app)("Data/Interface/Hints/Hints.uni", &err);
        auto hints = file ? ui::parse_hints(*file, &err) : std::nullopt;
        if (!hints) SDL_Log("hints: %s", err.c_str());
        app.hints = hints ? std::move(*hints) : std::vector<std::string>{};
    }
    std::vector<std::string> shown = *app.hints;  // Hints.uni's hint n is the text table's IDS_HINT_<n>
    for (std::size_t i = 0; i < shown.size(); ++i)
        shown[i] = ui::lang_game(app.lang, "IDS_HINT_" + std::to_string(i), shown[i]);
    open_game_window(app, "hints window", [&](ui::Frontend& f, std::string* err) {
        return ui::frontend_hints(f, shown, app.hint_index, app.settings.hints, err);
    });
}



void open_diplomacy(App& app) {
    open_game_window(app, "diplomacy window", [&](ui::Frontend& f, std::string* err) {
        return ui::frontend_diplomacy(f, ui::diplomacy_rows(*app.view(), market_ui(app).names, panel_strings(app)),
                                      err);
    });
}

// ---- our own saves (slice H, docs/save-own.md, docs/frontend.md 4.10, 4.11) ----

// The folder of our save files: --save-dir, else %APPDATA%\EntenteEngine\saves (made when the first save is written).
std::string saves_folder(App& app) {
    if (app.save_dir.empty()) {
        std::string err;
        if (auto folder = app_folder(app, &err)) {
            app.save_dir = *folder + "saves";
        } else {
            SDL_Log("saves: %s", err.c_str());
        }
    }
    return app.save_dir;
}

// A game from our own save: the state as saved (the house depth maps from the game files again), the ground of its
// map file (the cells follow the state, live_view_sync), the camera, the selection, the speed and the screen switches
// as they were; the skirmish setup it keeps makes Restart work. The running game stays when the file cannot be read.
bool load_own_game(App& app, const Bytes& file, std::string* err) {
    auto state = std::make_unique<sim::GameState>();
    sim::OwnSaveMeta meta;
    if (!sim::OwnSave_Read(file, state.get(), &meta, err)) return false;
    std::vector<std::string> warnings;
    load_saved_depth(loader(app), app.params, *state, &warnings);
    for (const std::string& w : warnings) SDL_Log("own save: %s", w.c_str());
    const std::string path = "Maps/" + state->map_name;
    auto map_file = loader(app)(path, err);
    if (!map_file) return false;
    auto map = parse_map(*map_file, err);
    if (!map) {
        *err = path + ": " + *err;
        return false;
    }
    if (map->width != state->map.width || map->height != state->map.height) {
        *err = path + " does not have the size of this save's map";
        return false;
    }
    MapData ground = std::move(*map);  // houses and units come from the state
    ground.houses.clear();
    ground.units.clear();
    ground.neutral_houses.clear();
    app.host.replace(std::move(state));
    app.save.reset();
    app.new_game = false, app.own_game = true;
    app.last_setup = meta.setup;
    app.game_source = "own save " + app.save_path;
    sim::ScriptVmPtr lua = reopen_script(app, &meta.lua, {});  // slice M6
    const sim::OwnSaveView& v = meta.view;
    if (!enter_game(app, std::move(ground), ViewPos{v.view_col, v.view_row}, err)) return false;
    app.host.game().lua = std::move(lua);
    if (smooth_camera(app)) {
        ui::camera_jump(app.cam, v.cam_x, v.cam_y, ui::camera_bounds(app.live.map, app.view_w, app.view_h));
        app.pos = ui::camera_view(app.cam);
    }
    if (v.sel_player < app.view()->players.size()) {
        app.sel.player = v.sel_player;
        app.sel.units = v.sel_units, app.sel.flagged = v.sel_flagged, app.sel.house = v.sel_house;
        ui::selection_prune(*app.view(), app.sel);
    }
    app.speed = v.speed >= 4 ? 4 : v.speed >= 2 ? 2 : 1;
    app.minimap_shown = v.minimap_shown != 0, app.show_health = v.show_health != 0, app.tab_index = v.tab_index;
    SDL_Log("loaded own save %s (\"%s\", tick %u)", app.save_path.c_str(), meta.info.name.c_str(), app.view()->tick);
    return true;
}

// Writes the running game as <saves folder>/<name>.sav (through a temporary file, platform/files.hpp): the state, the
// screen (camera, selection, speed, switches) and the skirmish setup when there is one. The web: the browser's storage
// (web_store).
bool save_own_game(App& app, const std::string& name, std::string* err) {
    if (!app.view() || !app.started) {
        *err = "there is no game to save";
        return false;
    }
    const std::string folder = saves_folder(app);
    if (folder.empty()) {
        *err = "there is no folder for the saves";
        return false;
    }
    if (!make_folder(folder, err)) return false;
    SimGame& g = held(app);
    sim::OwnSaveMeta meta;
    meta.info.name = name;
    meta.info.saved_at = static_cast<std::int64_t>(std::time(nullptr));
    meta.info.player = player_name(app, app.view()->my_player);
    sim::OwnSaveView& v = meta.view;
    v.cam_x = app.cam.x, v.cam_y = app.cam.y, v.view_col = app.pos.col, v.view_row = app.pos.row;
    v.sel_player = app.sel.player;
    v.sel_units = app.sel.units, v.sel_flagged = app.sel.flagged, v.sel_house = app.sel.house;
    v.speed = static_cast<std::uint8_t>(app.speed);
    v.minimap_shown = app.minimap_shown ? 1 : 0, v.show_health = app.show_health ? 1 : 0, v.tab_index = app.tab_index;
    meta.setup = app.last_setup;
#ifndef __EMSCRIPTEN__
    if (app.net_game) meta.net = app.net_game->save_meta().net;  // slice MP9: a network game's seats (Resume takes it)
#endif
    if (g.lua) meta.lua = sim::ScriptVm_DumpGlobals(*g.lua);      // slice M6: the Lua mission's globals
    for (std::uint8_t p = 0; p < g.state->players.size(); ++p) {
        if (g.state->players[p].is_lost == 0) sim::Player_UpdatePlayTime(*g.state, p);
    }
    const Bytes data = sim::OwnSave_Write(*g.state, meta);
    if (data.empty()) {
        *err = "the game could not be packed";
        return false;
    }
    const std::string path = folder + "/" + name + ".sav";
    if (!save_file_atomic(path, data, err)) return false;
    SDL_Log("saved the game at tick %u to %s (%zu bytes)", app.view()->tick, path.c_str(), data.size());
    web_store();
    return true;
}

// The load screen's list: our saves (name order), then the original's saves in the game folder's Save\ (loaded the
// original's way), each with the five lines the screen shows when it is selected.
std::vector<ui::SaveEntry> save_entries(App& app) {
    std::vector<ui::SaveEntry> out;
    auto date_lines = [](std::int64_t t, ui::SaveEntry& e) {
        const std::time_t tt = static_cast<std::time_t>(t);
        if (const std::tm* tm = std::localtime(&tt)) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%02d.%02d.%02d", tm->tm_mday, tm->tm_mon + 1, tm->tm_year % 100);
            e.info[0] = buf;
            std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
            e.info[1] = buf;
        }
    };
    const std::string folder = saves_folder(app);
    for (const std::string& file : list_files(folder, ".sav")) {
        ui::SaveEntry e;
        e.name = ui::lang_data(&app.lang, file.substr(0, file.size() - 4));  // the file's name
        e.path = folder + "/" + file;
        std::string err;
        auto bytes = load_file(e.path, &err);
        auto info = bytes ? sim::OwnSave_ReadInfo(*bytes, &err) : std::nullopt;
        if (info) {
            date_lines(info->saved_at, e);
            e.info[2] = info->player;
            e.info[3] =
                info->map_name.size() > 4 ? info->map_name.substr(0, info->map_name.size() - 4) : info->map_name;
            char buf[48];
            std::snprintf(buf, sizeof buf, "%u:%02u:%02u", info->play_time / 3600, info->play_time / 60 % 60,
                          info->play_time % 60);
            e.info[4] = ours(app, "load.time_played", {{"time", buf}});
            ui::lang_data(&app.lang, e.info[2]),
                ui::lang_data(&app.lang, e.info[3]);  // the player's and the map's names


            e.preview = [&app, path = e.path]() -> std::optional<Minimap> {
                std::string err;
                auto file = load_file(path, &err);
                auto state = std::make_unique<sim::GameState>();
                sim::OwnSaveMeta meta;
                if (!file || !sim::OwnSave_Read(*file, state.get(), &meta, &err)) {
                    SDL_Log("load screen: no picture for %s: %s", path.c_str(), err.c_str());
                    return std::nullopt;
                }
                return ui::save_preview(*state, minimap_ground_colours(app));
            };
        } else {
            e.info[2] = ours(app, "load.unreadable");
        }
        out.push_back(std::move(e));
    }
    const std::string original = app.game_dir + (app.lower_case_paths ? "/save" : "/Save");
    for (const std::string& file : list_files(original, ".svg")) {
        ui::SaveEntry e;
        e.name = ours(app, "load.original", {{"name", ui::lang_data(&app.lang, file.substr(0, file.size() - 4))}});
        e.path = original + "/" + file;
        SDL_PathInfo pi;
        if (SDL_GetPathInfo(e.path.c_str(), &pi)) date_lines(SDL_NS_TO_SECONDS(pi.modify_time), e);
        e.info[2] = ours(app, "load.original_info");
        e.preview = [path = e.path]() -> std::optional<Minimap> {
            std::string err;
            auto file = load_file(path, &err);
            auto mm = file ? parse_svg_preview(*file, &err) : std::nullopt;
            if (!mm) SDL_Log("load screen: no picture for %s: %s", path.c_str(), err.c_str());
            return mm;
        };
        out.push_back(std::move(e));
    }
    return out;
}

// Loads `path` (our own save or an original .svg) in place of the running game: the trace and commands files start
// again. On an error in our own save the running game goes on.
bool load_save_file(App& app, const std::string& path, std::string* err) {
    const std::string old = app.save_path;
    app.host.close_files();
    app.build_type = -1, app.build_menu = false, app.set_exit = false, app.fire_point = -1;
    app.strike = -1, app.strike_start.reset();
    app.save_path = path;
    if (load_game(app, err)) return true;
    app.save_path = old;
    return false;
}



void quick_save(App& app) {
    if (app.paused || !app.view() || app.view()->paused != 0) return;
    std::string err;
    const bool ok = save_own_game(app, "QuickSave", &err);
    if (!ok) SDL_Log("quick save: %s", err.c_str());
    const ui::StringTable& st = panel_strings(app);
    const auto it = st.find(ok ? "IDS_QUICKSAVE_SUCCESS" : "IDS_QUICKSAVE_FAILED");
    event_message(app, it != st.end() ? it->second : "QuickSave", 0xffffff);
}

// F6 (case 0x75): the Confirm window with IDS_CONFIRM_QUICK_LOAD; Yes loads QuickSave (FrontendRequest::QuickLoad).
void quick_load(App& app) {
    if (app.net_client) return;  // slice MP2: no loading in a network game
    open_game_window(app, "quick load question",
                     [](ui::Frontend& f, std::string* err) { return ui::frontend_confirm_quick_load(f, err); });
}

// ---- the stand-alone missions (slice M4, docs/missions.md 2 and 4, docs/frontend.md 4.9) ----

// Data/TextTable_eng.tt (the missions' titles and briefings), read once; unreadable: empty (titles are the stems).
const TextTable& text_table_of(App& app) {
    if (!app.text_table) {
        std::string err;
        auto file = loader(app)("Data/TextTable_eng.tt", &err);
        auto table = file ? parse_text_table(*file, &err) : std::nullopt;
        if (!table) SDL_Log("text table: %s", err.c_str());
        app.text_table = table ? std::move(*table) : TextTable{};
    }
    return *app.text_table;
}

// The text table as shown (docs/languages.md 5.3): text_table_of through the display language. text_table_of itself
// stays English, for the sim and the scripts.
const TextTable& display_text_table(App& app) {
    if (!app.lang.pseudo && app.lang.game.empty()) return text_table_of(app);
    if (!app.display_tt) {
        app.display_tt = text_table_of(app);
        ui::lang_localize(app.lang, *app.display_tt);
    }
    return *app.display_tt;
}

// A text of the sim's English text table as shown: its record's key looked up (the end text, docs/languages.md 5.3,
// which the state keeps in English only); a text without a record stays as it is.
std::string display_tt_text(App& app, const std::string& english) {
    if (!app.lang.pseudo && app.lang.game.empty()) return english;
    for (const auto& [key, r] : text_table_of(app)) {
        if (r.text == english) return ui::lang_game(app.lang, key, english);
    }
    return english;
}



std::vector<ui::MissionEntry> mission_entries(App& app) {
    const std::string folder =
        app.game_dir + (app.lower_case_paths ? "/data/campaigns/missions/" : "/Data/Campaigns/Missions/");
    std::vector<ui::MissionEntry> out;
    for (const std::string& name : list_files(folder, ".mis")) {
        std::string err;
        auto file = load_file(folder + name, &err);
        auto entry = file ? ui::standalone_mission_entry(name, *file, display_text_table(app), &err) : std::nullopt;
        if (!entry) {
            SDL_Log("missions: %s", err.c_str());
            continue;
        }
        out.push_back(std::move(*entry));
    }
    SDL_Log("missions: %zu in %s", out.size(), folder.c_str());
    return out;
}

#ifndef __EMSCRIPTEN__


std::vector<ui::MissionEntry> scenario_entries(App& app) {
    const std::string folder =
        app.game_dir + (app.lower_case_paths ? "/data/campaigns/multiplay/" : "/Data/Campaigns/multiplay/");
    std::vector<ui::MissionEntry> out;
    for (const std::string& name : list_files(folder, ".mis")) {
        std::string err;
        auto file = load_file(folder + name, &err);
        auto entry = file ? ui::multiplay_scenario_entry(name, *file, display_text_table(app), &err) : std::nullopt;
        if (!entry) {
            SDL_Log("scenarios: %s", err.c_str());
            continue;
        }
        out.push_back(std::move(*entry));
    }
    SDL_Log("scenarios: %zu in %s", out.size(), folder.c_str());
    return out;
}
#endif

// ---- the campaigns (slice M7, docs/missions.md 4.1 and 4.2, docs/frontend.md 4.8 and 4.9) ----

bool same_name(std::string_view a, std::string_view b) {  // ASCII letters compared without case
    return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}




std::vector<ui::CampaignEntry> campaign_entries(App& app) {
    const std::string folder = app.game_dir + (app.lower_case_paths ? "/data/campaigns/" : "/Data/Campaigns/");
    std::vector<ui::CampaignEntry> out;
    for (const char* kind : {".cmp", ".cmt"}) {
        for (const std::string& name : list_files(folder, kind)) {
            std::string err;
            auto file = load_file(folder + name, &err);
            auto entry = file ? ui::campaign_entry(name, *file, display_text_table(app), &err) : std::nullopt;
            if (!entry) {
                SDL_Log("campaigns: %s", err.c_str());
                continue;
            }
            out.push_back(std::move(*entry));
        }
    }
    SDL_Log("campaigns: %zu in %s", out.size(), folder.c_str());
    return out;
}



std::string progress_profile(App& app) {
    const ui::Profiles& pr = app.profiles;
    const std::string profile =
        pr.names[static_cast<std::size_t>(std::clamp(pr.selected, 0, static_cast<int>(pr.names.size()) - 1))];
    if (ui::progress_needs_import(app.progress, profile)) {
        std::string err;
        auto file = loader(app)("Save/" + profile + ".dat", &err);
        const int n = ui::progress_import(
            app.progress, profile,
            file ? ui::parse_progress_dat(*file) : std::vector<std::pair<std::string, std::string>>{});
        SDL_Log("progress: took %d won missions of %s from Save/%s.dat", n, profile.c_str(), profile.c_str());
        store_progress(app);
    }
    return profile;
}



std::vector<ui::MissionEntry> campaign_missions(App& app, const ui::CampaignEntry& c, int* selected) {
    *selected = -1;
    const std::string profile = progress_profile(app);
    const GameFileLoader load = loader(app);
    std::string err;
    std::vector<ui::MissionEntry> all, rows;
    std::vector<bool> done, present;
    ui::CampaignRows r;
    auto file = load(c.path, &err);
    if (c.lua) {
        auto cmt = file ? parse_cmt(*file, &err) : std::nullopt;
        if (!cmt) {
            SDL_Log("campaign %s: %s", c.path.c_str(), err.c_str());
            return rows;
        }
        for (const std::string& name : cmt->missions) {
            const std::string path = "Data\\Campaigns\\" + c.stem + "\\" + name + ".mit";
            std::string file_path = path;
            std::replace(file_path.begin(), file_path.end(), '\\', '/');
            auto bytes = load(file_path, &err);
            auto e = bytes ? ui::mit_mission_entry(path, *bytes, display_text_table(app), &err) : std::nullopt;
            if (!e) {
                SDL_Log("campaign %s: %s", c.stem.c_str(), err.c_str());
                e = ui::MissionEntry{name, "", "", path, false};
            }
            all.push_back(std::move(*e));
            done.push_back(ui::progress_done(app.progress, profile, c.stem, name + ".mit"));
        }
        r = ui::cmt_rows(all.size(), done);
    } else {
        auto cmp = file ? parse_cmp(*file, &err) : std::nullopt;
        if (!cmp) {
            SDL_Log("campaign %s: %s", c.path.c_str(), err.c_str());
            return rows;
        }
        for (const std::string& field : cmp->file_names) {
            const std::string name(mis_name(field));
            auto bytes = load("Data/Campaigns/" + c.stem + "/" + name, &err);
            auto e =
                bytes ? ui::campaign_mission_entry(c.stem, name, *bytes, display_text_table(app), &err) : std::nullopt;
            if (!e) SDL_Log("campaign %s: %s", c.stem.c_str(), err.c_str());
            present.push_back(e.has_value());
            all.push_back(e ? std::move(*e) : ui::MissionEntry{});
            done.push_back(ui::progress_done(app.progress, profile, c.stem, name));
        }
        r = ui::cmp_rows(cmp->missions, done, present);
    }
    for (std::size_t i : r.missions) {
        all[i].done = done[i];
        rows.push_back(all[i]);
    }
    *selected = r.selected;
    SDL_Log("campaign %s: %zu of %zu missions listed, %zu won, row %d selected", c.stem.c_str(), rows.size(),
            all.size(), static_cast<std::size_t>(std::count(done.begin(), done.end(), true)), *selected);
    return rows;
}


bool open_campaign_missions(App& app, const ui::CampaignEntry& c, std::string* err) {
    int selected = -1;
    std::vector<ui::MissionEntry> rows = campaign_missions(app, c, &selected);
    return ui::frontend_campaign_missions(*app.menu, std::move(rows), selected, err);
}


void campaign_music(App& app) {
    const std::uint8_t track = ui::campaign_track(ui::frontend_campaign_row(*app.menu));
    if (audio::audio_music_track(app.audio.get()) != track) audio::audio_music_play(app.audio.get(), track);
}





bool campaign_return(App& app) {
    if (!app.menu || !app.view()) return false;
    const auto key = ui::progress_key(app.view()->mission_path);
    if (!key || same_name(key->first, "missions")) return false;
    for (const ui::CampaignEntry& c : campaign_entries(app)) {
        if (!same_name(c.stem, key->first)) continue;
        if (app.batch_ticks >= 0) ui::frontend_set_animate(*app.menu, false);  // batch: the door drawn open
        std::string err;
        if (!open_campaign_missions(app, c, &err)) {
            SDL_Log("campaign %s: %s", c.stem.c_str(), err.c_str());
            return false;
        }
        return true;
    }
    SDL_Log("campaign: no campaign file for %s", app.view()->mission_path.c_str());
    return false;
}

// Slice M7: the Lua missions (.mit, the Entente and Triple Alliance campaigns) need the script engine of slice M5,
// which runs them since 2026-10-18 (their save and load is slice M6). Off: Load on such a mission says so.
constexpr bool kLuaMissions = true;

bool mission_available(const std::string& path) {
    const bool mit = path.size() > 4 && same_name(std::string_view(path).substr(path.size() - 4), ".mit");
    if (!mit || kLuaMissions) return true;
    SDL_Log("missions: %s is a Lua mission; the Lua missions are not available yet (slice M5)", path.c_str());
    return false;
}





bool restart_game(App& app, std::string* err) {
    if (!app.last_setup && app.view() && !app.view()->mission_path.empty()) {
        const std::string path = app.view()->mission_path;
        const std::uint32_t seed =
            app.seed ? app.mission_seed + 1 : static_cast<std::uint32_t>(SDL_rand_bits() ^ SDL_GetTicksNS());
        app.host.close_files();
        app.build_type = -1, app.build_menu = false, app.set_exit = false, app.fire_point = -1;
        app.strike = -1, app.strike_start.reset();
        if (!start_new_mission(app, path, seed, err)) return false;
        SDL_Log("restart: %s", app.game_source.c_str());
        return true;
    }
    if (!app.last_setup || !app.view()) {
        *err = "no skirmish to restart";
        return false;
    }
    const std::uint32_t seed =
        app.seed ? app.last_setup->seed + 1 : static_cast<std::uint32_t>(SDL_rand_bits() ^ SDL_GetTicksNS());
    const sim::SkirmishSetup setup = ui::game_menu_restart_setup(*app.last_setup, *app.view(), seed);
    app.host.close_files();
    app.build_type = -1, app.build_menu = false, app.set_exit = false, app.fire_point = -1;
    app.strike = -1, app.strike_start.reset();
    if (!start_new_skirmish(app, setup, err)) return false;
    std::string slots;  // as antanta-headless --slots takes them: level/nation/colour/team per row
    for (std::size_t i = 0; i < setup.players; ++i) {
        const sim::SkirmishSlot& sl = setup.slots[i];
        slots += (i ? "," : "") + std::to_string(sl.level) + "/" + std::to_string(sl.nation) + "/" +
                 std::to_string(sl.colour) + "/" + std::to_string(sl.team);
    }
    SDL_Log("restart: %s, slots %s", app.game_source.c_str(), slots.c_str());
    return true;
}

// What the app does for a request of the front-end (its events, and the script's `menu` lines of a batch run).
SDL_AppResult menu_request(App& app, ui::FrontendRequest request) {
    switch (request) {
        case ui::FrontendRequest::Quit: return SDL_APP_SUCCESS;
        case ui::FrontendRequest::SettingsChanged: {
            const ui::Settings before = app.settings;
            const std::string language = app.settings.language;
            app.settings = ui::frontend_settings(*app.menu);
            // The window's place is the app's (note_window); the front-end's copy is from when it opened.
            app.settings.window_x = before.window_x, app.settings.window_y = before.window_y;
            app.settings.window_w = before.window_w, app.settings.window_h = before.window_h;
            app.settings.window_maximized = before.window_maximized;
            store_settings(app);
            if (app.settings.language != language) {  // Game options' Language row: at once, over --lang
                app.cli_lang.clear();
                choose_language(app);
#ifdef ANTANTA_UPDATER
                if (app.lang_sync) app.lang_sync->start(app.lang.info.code, true);  // the login may open (L3b)
#endif
#ifdef __EMSCRIPTEN__
                const std::string& code = app.lang.info.code;
                if (code == "de" || code == "it" || code == "ru") app.lang_wait = code;  // web_lang_frame
#endif
            }
            audio::audio_settings(app.audio.get(), app.settings);
            app.video.set_volume(video_volume(app.settings));
            if (app.settings.fullscreen != before.fullscreen) apply_fullscreen(app);  // the picker's Full screen
#ifndef __EMSCRIPTEN__
            if (app.settings.resolution != before.resolution) apply_resolution(app);  // the Options resolution
#endif
            if (auto adv = ui::frontend_take_game_advisors(*app.menu); adv && app.view()) {  // Game options in a game
                for (sim::Command& c : ui::game_options_orders(app.view()->my_player, *adv)) {
                    queue_orders(app, {std::move(c)}, "");  // two orders of their own (O lines 1 and 2 of the tick)
                }
            }
            break;
        }
        case ui::FrontendRequest::ProfilesChanged:
            app.profiles = ui::frontend_profiles(*app.menu);
            store_profiles(app);
            break;
        case ui::FrontendRequest::StartSkirmish: {
            std::string err;
            if (!start_new_skirmish(app, ui::frontend_skirmish_setup(*app.menu), &err)) {
                SDL_Log("cannot start the game: %s", err.c_str());
                ui::frontend_message(*app.menu, ours(app, "game.start_failed", {{"reason", reason(app, err)}}));
                break;
            }
            // The game view takes the window. No way back to the menu yet: the in-game menu is slice FE3.
            app.menu.reset();
            app.last_ns = SDL_GetTicksNS();
            if (app.window && app.settings.hints) open_hints(app);
            break;
        }
#ifndef __EMSCRIPTEN__
        case ui::FrontendRequest::PlayCredits: app.video.play_credits(app.renderer); break;  // the menu stays under it
#else
        case ui::FrontendRequest::PlayCredits:
            app.credits_wait = app.videos_on && !app.video.active();
            break;  // web_credits
#endif
        case ui::FrontendRequest::EndMessageClosed:
            app.messages.clear(), app.message_shown = false;
            open_end_statistics(app);
            break;
        case ui::FrontendRequest::MessageClosed:
            if (!app.messages.empty()) app.messages.pop_front();
            app.message_shown = false;
            app.menu.reset();
            app.last_ns = SDL_GetTicksNS();
            show_messages(app);
            break;
        case ui::FrontendRequest::OpenMissions: {
            std::string err;
            if (!ui::frontend_mission_list(*app.menu, mission_entries(app), 0, &err)) {
                SDL_Log("missions: %s", err.c_str());
                ui::frontend_message(*app.menu, ours(app, "missions.list_failed", {{"reason", reason(app, err)}}));
            }
            break;
        }
        case ui::FrontendRequest::OpenCampaigns: {
            std::string err;
            if (!ui::frontend_campaign_select(*app.menu, campaign_entries(app), 0, &err)) {
                SDL_Log("campaigns: %s", err.c_str());
                ui::frontend_message(*app.menu, ours(app, "campaigns.failed", {{"reason", reason(app, err)}}));
                break;
            }
            campaign_music(app);
            break;
        }
        case ui::FrontendRequest::CampaignSelected: campaign_music(app); break;
        case ui::FrontendRequest::OpenCampaignMissions: {
            std::string err;
            if (!open_campaign_missions(app, ui::frontend_campaign(*app.menu), &err)) {
                SDL_Log("campaign: %s", err.c_str());
                ui::frontend_message(*app.menu, ours(app, "campaign.missions_failed", {{"reason", reason(app, err)}}));
            }
            break;
        }
        case ui::FrontendRequest::StartMission: {
            const std::uint32_t seed =
                app.seed ? *app.seed : static_cast<std::uint32_t>(SDL_rand_bits() ^ SDL_GetTicksNS());
            std::string err;
            if (!mission_available(ui::frontend_chosen_mission(*app.menu).path)) {  // slice M7: a Lua mission
                ui::frontend_message(*app.menu, ours(app, "campaign.not_available"));
                break;
            }
            if (!start_new_mission(app, ui::frontend_chosen_mission(*app.menu).path, seed, &err)) {
                SDL_Log("cannot start the mission: %s", err.c_str());
                ui::frontend_message(*app.menu, ours(app, "mission.start_failed", {{"reason", reason(app, err)}}));
                break;
            }
            app.menu.reset();
            app.last_ns = SDL_GetTicksNS();
            if (app.window && app.settings.hints) open_hints(app);  // as a new skirmish
            break;
        }
        case ui::FrontendRequest::GameMenuClosed:
            app.menu.reset();
            app.last_ns = SDL_GetTicksNS();
            break;
        case ui::FrontendRequest::HintsClosed:
            app.hint_index = ui::frontend_hint_index(*app.menu);
            if (ui::frontend_hints_startup(*app.menu) != app.settings.hints) {
                app.settings.hints = ui::frontend_hints_startup(*app.menu);
                store_settings(app);
            }
            app.menu.reset();
            app.last_ns = SDL_GetTicksNS();
            break;
        case ui::FrontendRequest::DiplomacyOk:
            queue_orders(app, {ui::diplomacy_order(*app.view(), ui::frontend_diplomacy_rows(*app.menu))}, "diplomacy");
            app.menu.reset();
            break;
        case ui::FrontendRequest::Restart: {
            std::string err;
            if (!restart_game(app, &err)) {
                SDL_Log("cannot restart the game: %s", err.c_str());
                if (!app.started) ui::frontend_show(*app.menu, "main", &err);  // the old game is gone too
                ui::frontend_message(*app.menu, ours(app, "game.restart_failed", {{"reason", reason(app, err)}}));
                break;
            }
            app.menu.reset();
            app.last_ns = SDL_GetTicksNS();
            if (app.window && app.settings.hints) open_hints(app);  // slice I8: as a new game
            break;
        }
        case ui::FrontendRequest::OpenLoad:
        case ui::FrontendRequest::OpenSave: {
            std::string err;
            bool ok = false;
            if (request == ui::FrontendRequest::OpenLoad && app.net_client) {  // slice MP2
                ui::frontend_message(*app.menu, ours(app, "load.not_in_network"));
                break;
            }
            if (request == ui::FrontendRequest::OpenLoad) {
                ok = ui::frontend_load_screen(*app.menu, save_entries(app), app.started, &err);
            } else {
                std::vector<std::string> names;
                for (const std::string& f : list_files(saves_folder(app), ".sav"))
                    names.push_back(f.substr(0, f.size() - 4));
                ok = ui::frontend_save_screen(*app.menu, names, &err);
            }
            if (!ok) SDL_Log("front-end: %s", err.c_str());
            menu_presentation(app);  // the screens are 1024 x 768 in a game too
            break;
        }
        case ui::FrontendRequest::SaveGame: {
            std::string err;
            const std::string name = ui::frontend_save_name(*app.menu);
            if (!save_own_game(app, name, &err)) {
                SDL_Log("cannot save the game as %s: %s", name.c_str(), err.c_str());
                ui::frontend_message(*app.menu, ours(app, "save.failed", {{"reason", reason(app, err)}}));
                break;
            }
            app.menu.reset();
            app.last_ns = SDL_GetTicksNS();
            break;
        }
        case ui::FrontendRequest::LoadGame:
        case ui::FrontendRequest::QuickLoad: {
            const bool quick = request == ui::FrontendRequest::QuickLoad;
            const std::string path =
                quick ? saves_folder(app) + "/QuickSave.sav" : ui::frontend_chosen_save(*app.menu).path;
            std::string err;
            const bool ok = load_save_file(app, path, &err);
            if (!ok) SDL_Log("cannot load %s: %s", path.c_str(), err.c_str());
            if (!ok && (!app.view() || !app.started)) {  // the old game is gone too: the main menu
                app.started = false, app.host.replace(nullptr);
                ui::frontend_show(*app.menu, "main", &err);
                ui::frontend_message(*app.menu, ours(app, "load.failed"));
                menu_presentation(app);
                break;
            }
            if (!ok && !quick) {
                ui::frontend_message(*app.menu, ours(app, "load.failed_reason", {{"reason", reason(app, err)}}));
                break;
            }
            app.menu.reset();
            app.last_ns = SDL_GetTicksNS();
            if (quick) {
                const ui::StringTable& st = panel_strings(app);
                const auto it = st.find(ok ? "IDS_QUICKLOAD_SUCCESS" : "IDS_QUICKLOAD_FAILED");
                event_message(app, it != st.end() ? it->second : "QuickLoad", 0xffffff);
            }
            break;
        }
        case ui::FrontendRequest::BackToMenu:
        case ui::FrontendRequest::StatisticsClosed:  // the main menu: the game is over
            record_progress(app);
            campaign_return(app);
            app.messages.clear(), app.message_shown = false;
            app.last_setup.reset();
#ifndef __EMSCRIPTEN__
            net_close(app);  // slice MP2: a network game is left
#endif
            app.started = false;
            app.host.close_files();
            app.host.replace(nullptr);
            app.save.reset();
            audio::audio_music_play(app.audio.get(), 0);  // the main menu's music from the start
            break;
#ifndef __EMSCRIPTEN__
        case ui::FrontendRequest::NetScenarios: {
            std::string err;
            if (!ui::frontend_net_scenarios(*app.menu, scenario_entries(app), 0, &err))
                SDL_Log("scenarios: %s", err.c_str());
            break;
        }
        case ui::FrontendRequest::NetHost: net_menu_open(app, true); break;  // slice MP2
        case ui::FrontendRequest::NetJoin: {
            const std::string resume = ui::frontend_net_choices(*app.menu).resume;  // slice MP9: a saved game's row
            net_menu_open(app, !resume.empty(), resume);
            break;
        }
        case ui::FrontendRequest::NetSeat:  // slice MP9
            if (app.net_client && app.net_client->host()) net_seat_click(app, ui::frontend_net_seat(*app.menu));
            break;
        case ui::FrontendRequest::NetStart:
            if (app.net_client && app.net_client->host() && !app.net_start_sent) {
                app.net_client->start_game();
                app.net_start_sent = true;
            }
            break;
        case ui::FrontendRequest::NetLeave:
            net_close(app);
            ui::frontend_net_back(*app.menu, "");
            break;
#else
        case ui::FrontendRequest::NetScenarios:
        case ui::FrontendRequest::NetHost:
        case ui::FrontendRequest::NetJoin:
        case ui::FrontendRequest::NetStart:
        case ui::FrontendRequest::NetLeave:
        case ui::FrontendRequest::NetSeat: break;
#endif
        case ui::FrontendRequest::CheckUpdates:  // docs/download.md: the menu's "Check for updates"
#ifdef ANTANTA_UPDATER
            if (app.updater) app.updater->check(true);
#endif
#ifdef ANTANTA_RELEASE_CHECK
            if (app.releases) app.releases->check(true);
#endif
            break;
        case ui::FrontendRequest::None: break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult menu_event(App& app, SDL_Event* e) {
    if (e->type == SDL_EVENT_KEY_DOWN && (e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER) &&
        (e->key.mod & SDL_KMOD_ALT) != 0) {  // ours: Alt+Enter switches between window and full screen
        app.settings.fullscreen = !app.settings.fullscreen;
        apply_fullscreen(app);
        store_settings(app);
        ui::frontend_set_fullscreen(*app.menu, app.settings.fullscreen);  // an open Options picker follows
        return SDL_APP_CONTINUE;
    }
    SDL_ConvertEventToRenderCoordinates(app.renderer, e);
    return menu_request(app, ui::frontend_event(*app.menu, *e));
}

#ifndef __EMSCRIPTEN__
// --menu --screen: one screen into the PNG, with the software renderer and no window.
SDL_AppResult run_menu_batch(App& app) {
    app.settings = first_settings(app);
    apply_cli_settings(app);
    if (app.user_dir.empty()) {
        app.profiles = first_profiles(app);
    } else {
        load_profiles(app);  // slice M7: a test's own profile (profiles.txt in --user-dir)
    }
    if (!batch_renderer(app)) return SDL_APP_FAILURE;
    std::string err;
    app.menu = ui::frontend_open(app.renderer, loader(app), menu_fonts(), app.settings, app.profiles, app.seed, &err,
                                 &app.lang);
    const bool load_screen = app.menu_screen.rfind("load", 0) == 0;          // "load:pick=<row>"
    const bool file_screen = load_screen || app.menu_screen == "save";       // slice H
    const bool missions_screen = app.menu_screen.rfind("missions", 0) == 0;  // slice M4
    // slice M7: "campaigns[:pick=<row>]" the campaign select, "campaign_missions[:pick=<row>]" that row's mission list
    const bool campaigns_screen = app.menu_screen.rfind("campaign", 0) == 0;
    const bool campaign_list = app.menu_screen.rfind("campaign_missions", 0) == 0;
    const bool scenarios_screen = app.menu_screen.rfind("network_scenarios", 0) == 0;  // "[:pick=<row>]", page 3
    if (!app.user_dir.empty()) load_progress(app);
    if (!app.menu || !ui::frontend_show(*app.menu,
                                        file_screen                           ? "main"
                                        : missions_screen || campaigns_screen ? "single_player"
                                        : scenarios_screen                    ? "network_games"
                                                                              : app.menu_screen,
                                        &err)) {
        SDL_Log("menu: %s", err.c_str());
        return SDL_APP_FAILURE;
    }
    const std::size_t pick_at = app.menu_screen.find("pick=");
    const int pick = pick_at == std::string::npos ? 0 : std::atoi(app.menu_screen.c_str() + pick_at + 5);
    if (missions_screen) {  // "missions:pick=<row>" selects that row
        if (!ui::frontend_mission_list(*app.menu, mission_entries(app), pick, &err)) {
            SDL_Log("menu: %s", err.c_str());
            return SDL_APP_FAILURE;
        }
    }
    if (scenarios_screen && !ui::frontend_net_scenarios(*app.menu, scenario_entries(app), pick, &err)) {
        SDL_Log("menu: %s", err.c_str());
        return SDL_APP_FAILURE;
    }
    if (campaigns_screen) {
        if (!ui::frontend_campaign_select(*app.menu, campaign_entries(app), pick, &err) ||
            (campaign_list && !open_campaign_missions(app, ui::frontend_campaign(*app.menu), &err))) {
            SDL_Log("menu: %s", err.c_str());
            return SDL_APP_FAILURE;
        }
    }
    if (file_screen) {
        std::vector<std::string> names;
        for (const std::string& f : list_files(saves_folder(app), ".sav")) names.push_back(f.substr(0, f.size() - 4));
        if (!(load_screen ? ui::frontend_load_screen(*app.menu, save_entries(app), false, &err)
                          : ui::frontend_save_screen(*app.menu, names, &err))) {
            SDL_Log("menu: %s", err.c_str());
            return SDL_APP_FAILURE;
        }
        if (const std::size_t at = app.menu_screen.find("pick="); load_screen && at != std::string::npos) {
            // a click on that row of the list (664, 417, rows of 22 from 8 down): its five lines and its picture
            const int row = std::atoi(app.menu_screen.c_str() + at + 5);
            for (SDL_EventType type :
                 {SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
                SDL_Event e{};
                e.type = type;
                const float x = 700, y = static_cast<float>(417 + 8 + 22 * row + 11);
                if (type == SDL_EVENT_MOUSE_MOTION) {
                    e.motion.x = x, e.motion.y = y;
                } else {
                    e.button.button = SDL_BUTTON_LEFT, e.button.x = x, e.button.y = y;
                }
                ui::frontend_event(*app.menu, e);
            }
        }
    }
    if (app.start_game && (missions_screen || campaigns_screen)) {  // slice M4, --start: Enter (Load) on the list
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = SDLK_RETURN;
        ui::FrontendRequest r = ui::frontend_event(*app.menu, e);
        if (r == ui::FrontendRequest::OpenCampaignMissions) {  // slice M7: "ok" on the campaign select, then Load
            if (!open_campaign_missions(app, ui::frontend_campaign(*app.menu), &err)) {
                SDL_Log("menu: %s", err.c_str());
                return SDL_APP_FAILURE;
            }
            r = ui::frontend_event(*app.menu, e);
        }
        if (r != ui::FrontendRequest::StartMission) {
            SDL_Log("--start: Load did not ask for a mission");
            return SDL_APP_FAILURE;
        }
        if (!mission_available(ui::frontend_chosen_mission(*app.menu).path)) return SDL_APP_FAILURE;
        if (!start_new_mission(app, ui::frontend_chosen_mission(*app.menu).path, app.seed.value_or(12345), &err)) {
            SDL_Log("cannot start the mission: %s", err.c_str());
            return SDL_APP_FAILURE;
        }
    } else if (app.start_game) {  // --start: a click on Start (345, 689), then the game in batch mode
        auto mouse = [&](SDL_EventType type) {
            SDL_Event e{};
            e.type = type;
            if (type == SDL_EVENT_MOUSE_MOTION) {
                e.motion.x = 445, e.motion.y = 700;
            } else {
                e.button.button = SDL_BUTTON_LEFT, e.button.x = 445, e.button.y = 700;
            }
            return ui::frontend_event(*app.menu, e);
        };
        mouse(SDL_EVENT_MOUSE_MOTION);
        mouse(SDL_EVENT_MOUSE_BUTTON_DOWN);
        if (mouse(SDL_EVENT_MOUSE_BUTTON_UP) != ui::FrontendRequest::StartSkirmish) {
            SDL_Log("--start: Start did not ask for a game (is --screen the skirmish screen?)");
            return SDL_APP_FAILURE;
        }
        if (!start_new_skirmish(app, ui::frontend_skirmish_setup(*app.menu), &err)) {
            SDL_Log("cannot start the game: %s", err.c_str());
            return SDL_APP_FAILURE;
        }
    }
    if (app.start_game) {
        app.menu.reset();
        // Slice M7: a message the mission's start queued (russia_1_1's opening one) while the list was still up gets
        // its window now, as the app's frame does; else the batch would wait for it without a tick.
        if (!show_messages(app)) return SDL_APP_FAILURE;
        if (!app.script_path.empty() && !load_script(app, &err)) {
            SDL_Log("%s", err.c_str());
            return SDL_APP_FAILURE;
        }
        app.batch_ticks = std::max(app.batch_ticks, 0);
        return run_batch(app);
    }
    ui::frontend_draw(*app.menu);
    SDL_RenderPresent(app.renderer);
    if (app.png_path.empty() || !save_png(app)) return SDL_APP_FAILURE;
    SDL_Log("wrote %s", app.png_path.c_str());
    return SDL_APP_SUCCESS;
}

// --menu --net-menu-script (slice MP2): the way through the network pages as a player takes it, from the file's
// lines, then the game in batch mode as --host or --join plays it (run_net_batch). Lines: `click <x> <y>` (a press
// and release on the 1024 x 768 menu), `key <SDL key name> [times]`, `text <characters>` (typed into the field with
// the focus), `setup <choices>` (the open skirmish screen's, as --screen skirmish:<choices>), `wait lan <n>` (page 2
// lists n games found on this network), `wait lobby <n>` (n humans in the lobby), `wait game` (the game started).
// '#' starts a comment. Each line runs when the one before it is done; --net-timeout ends a run that hangs.
SDL_AppResult run_net_menu_batch(App& app) {
    auto fail = [](const std::string& why) {
        SDL_Log("net menu script: %s", why.c_str());
        return SDL_APP_FAILURE;
    };
    if (app.user_dir.empty()) return fail("--user-dir is needed (the profiles the pages store go there)");
    std::string err;
    auto file = load_file(app.net_menu_script, &err);
    if (!file) return fail(err);
    app.settings = first_settings(app);
    apply_cli_settings(app);
    load_profiles(app);
    if (!batch_renderer(app)) return SDL_APP_FAILURE;
    app.menu = ui::frontend_open(app.renderer, loader(app), menu_fonts(), app.settings, app.profiles, app.seed, &err,
                                 &app.lang);
    if (!app.menu) return fail(err);
    ui::frontend_set_animate(*app.menu, false);
    const std::int64_t t0 = net_ms(), limit = static_cast<std::int64_t>(app.net_timeout_s) * 1000;
    const std::string all(file->begin(), file->end());
    std::size_t n = 0;
    for (std::size_t start = 0; start < all.size();) {
        std::size_t end = all.find('\n', start);
        if (end == std::string::npos) end = all.size();
        std::string line = all.substr(start, end - start);
        start = end + 1;
        ++n;
        line = line.substr(0, line.find('#'));
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        const std::vector<std::string> w = split_text(line, ' ');
        if (w.empty()) continue;
        const std::string where = app.net_menu_script + " line " + std::to_string(n) + ": ";
        auto send = [&](SDL_Event e) {
            if (app.menu && menu_request(app, ui::frontend_event(*app.menu, e)) != SDL_APP_CONTINUE) {
                return false;
            }
            return true;
        };
        bool ok = true;
        if (w[0] == "click" && w.size() == 3) {
            const float x = static_cast<float>(std::atof(w[1].c_str())),
                        y = static_cast<float>(std::atof(w[2].c_str()));
            SDL_Event e{};
            e.type = SDL_EVENT_MOUSE_MOTION, e.motion.x = x, e.motion.y = y;
            ok = send(e);
            for (SDL_EventType type : {SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_EVENT_MOUSE_BUTTON_UP}) {
                SDL_Event b{};
                b.type = type, b.button.button = SDL_BUTTON_LEFT, b.button.x = x, b.button.y = y;
                ok = ok && send(b);
            }
        } else if (w[0] == "key" && (w.size() == 2 || w.size() == 3)) {
            const int times = w.size() == 3 ? std::max(1, std::atoi(w[2].c_str())) : 1;
            for (int k = 0; k < times && ok; ++k) {
                SDL_Event e{};
                e.type = SDL_EVENT_KEY_DOWN, e.key.key = SDL_GetKeyFromName(w[1].c_str());
                if (e.key.key == SDLK_UNKNOWN) return fail(where + "no key " + w[1]);
                ok = send(e);
            }
        } else if (w[0] == "text" && w.size() == 2) {
            SDL_Event e{};
            e.type = SDL_EVENT_TEXT_INPUT, e.text.text = w[1].c_str();
            ok = send(e);
        } else if (w[0] == "setup" && w.size() == 2) {
            if (!app.menu || !ui::frontend_skirmish_choices(*app.menu, w[1], &err)) return fail(where + err);
        } else if (w[0] == "png" && w.size() == 2) {  // slice MP9: a picture of the pages as they are now
            if (!app.menu) return fail(where + "no menu to draw");
            ui::frontend_draw(*app.menu);
            SDL_RenderPresent(app.renderer);
            const std::string keep = app.png_path;
            app.png_path = w[1];
            ok = save_png(app);
            app.png_path = keep;
        } else if (w[0] == "wait" && (w.size() == 3 || (w.size() == 2 && w[1] == "game"))) {
            const int want = w.size() == 3 ? std::atoi(w[2].c_str()) : 0;
            auto done = [&] {
                if (w[1] == "game") return app.started;
                if (w[1] == "lan") return app.lan_finder && static_cast<int>(app.lan_finder->games().size()) >= want;
                return w[1] == "lobby" && app.net_client && !app.net_lobby_shown.empty() &&
                       app.net_lobby_joined >= want;
            };
            while (!done()) {
                if (!app.menu || net_menu_frame(app) == SDL_APP_FAILURE) return fail(where + "the network game failed");
                if (net_ms() - t0 > limit) return fail(where + "--net-timeout reached while waiting");
                SDL_Delay(1);
            }
        } else {
            return fail(where + "not a line of the network menu script: " + line);
        }
        if (!ok) return fail(where + "the menu ended the run");
        if (app.menu && !app.started && net_menu_frame(app) == SDL_APP_FAILURE)
            return fail(where + "the network failed");
        SDL_Log("net menu script: %s done", line.c_str());
    }
    if (!app.started) return fail("the script ended before the game started (wait game)");
    app.menu.reset();
    return run_net_batch(app);
}
#endif

// Where the videos are and what the video player does to the music: the player calls pause when the first video
// starts and resume after the last one (or at once if none could be opened), docs/sound.md section 5. On desktop
// Windows the game's own .wmv files in its Media folder come first (Media Foundation), the converted files second.
void configure_videos(App& app) {
    std::string media;
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
    media = app.game_dir + "/Media";
#endif
    std::string dir = app.videos_dir;
#ifdef __EMSCRIPTEN__
    dir = app.game_dir + "/videos";  // the page unpacks them with the game's files (tools/web_game_files.py)
#else
    if (dir.empty()) {
        const char* base = SDL_GetBasePath();  // engine/build/<config>/
#ifdef ANTANTA_RELEASE
        dir = std::string(base ? base : "") + "videos";  // the release exe: a "videos" folder beside it, if any
#else
        dir = std::string(base ? base : "") + "../../../out/videos";
#endif
    }
#endif
    ui::VideoHooks hooks;
    hooks.pause_music = [&app] { audio::audio_video_pause(app.audio.get()); };
    hooks.resume_music = [&app] { audio::audio_video_resume(app.audio.get()); };
    app.video.configure(media, dir, app.videos_on, std::move(hooks));
    app.video.set_volume(video_volume(app.settings));
}

#ifdef __EMSCRIPTEN__
// The credits are 80 MB, so the page fetches them only when the Medal button asks (shell.html, Module.antantaNeed):
// 1 the file is in /game/videos/, 0 the page is still getting it, -1 it could not (the page tells the visitor).
EM_JS(int, web_need_file, (const char* path),
      { return Module.antantaNeed ? Module.antantaNeed(UTF8ToString(path)) : -1; });
constexpr const char* kCreditsPath = "videos/credits.mpg";

// The game texts of a language chosen in Game options (docs/languages.md, slice L3b): the page fetches them from its
// own server (shell.html, Module.antantaLang): 1 they are in /libsdl/Antanta/lang/, 0 still coming, -1 they could not
// come (English stays for the game texts). The start page fetches them already when Play is pressed.
EM_JS(int, web_need_lang, (const char* code),
      { return Module.antantaLang ? Module.antantaLang(UTF8ToString(code)) : -1; });

void web_lang_frame(App& app) {
    if (app.lang_wait.empty()) return;
    const int state = web_need_lang(app.lang_wait.c_str());
    if (state == 0) return;
    SDL_Log("language pack %s: the page says %d", app.lang_wait.c_str(), state);
    if (state > 0 && app.lang_wait == app.lang.info.code) choose_language(app, true);
    app.lang_wait.clear();
}

// The Medal button on the web: wait for the file, then play it as on the desktop. When they are over the 80 MB are
// dropped from memory again (the next press takes them from the browser's storage).
void web_credits(App& app) {
    if (app.credits_wait) {
        const int state = web_need_file(kCreditsPath);
        if (state != 0) {
            SDL_Log("credits: the page says %d", state);
            app.credits_wait = false;
            if (state > 0) {
                app.credits_loaded = true;
                app.video.play_credits(app.renderer);
            }
        }
    } else if (app.credits_loaded && !app.video.active()) {
        app.credits_loaded = false;
        SDL_Log("credits: dropped from memory");
        std::remove((app.game_dir + "/" + kCreditsPath).c_str());
    }
}
#endif

// The web: the canvas fills the page and follows its size, as a window on the desktop (docs/web.md). High pixel
// density: the canvas has as many pixels as the screen shows (CSS size times devicePixelRatio, 150 % display scaling
// gives 1.5), as the desktop build draws at real pixels; SDL maps the mouse back and follows zoom and monitor changes.
#ifdef __EMSCRIPTEN__
constexpr SDL_WindowFlags kWindowFlags =
    SDL_WINDOW_RESIZABLE | SDL_WINDOW_FILL_DOCUMENT | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#else
constexpr SDL_WindowFlags kWindowFlags = SDL_WINDOW_RESIZABLE;
#endif

#ifdef ANTANTA_UPDATER



void start_updater(App& app) {
    std::string url = app.update_url;
#ifdef ANTANTA_RELEASE
    if (url.empty()) url = hosts::kUpdateUrl;
#endif
    if (url.empty()) return;
    if (url.back() != '/') url += '/';
    update::PublicKey key{};
    update::from_hex(update::kUpdatePublicKeyHex, key.data(), key.size());
    app.updater = std::make_unique<update::Updater>(url, ANTANTA_VERSION, key, update::own_exe());
    app.updater->clean_up_later(10);  // the old exe of the last update, once its process has quit
    ui::frontend_set_update_link(true);
    if (app.settings.check_updates) app.updater->check(false);
}

// Every frame of the menus outside a game: the answer of a finished check or install. False: the new version was
// started and the app quits.
bool update_frame(App& app) {
    if (!app.updater || !app.menu || app.started) return true;
    auto d = app.updater->take();
    if (!d) return true;
    using R = update::Updater::Result;
    switch (d->result) {
        case R::UpToDate:
            if (d->loud) ui::frontend_message(*app.menu, ours(app, "update.newest", {{"version", ANTANTA_VERSION}}));
            break;
        case R::CheckFailed:
        case R::InstallFailed:
            SDL_Log("update: %s", d->message.c_str());
            if (d->loud) ui::frontend_message(*app.menu, reason(app, d->message));
            break;
        case R::Available: {
            SDL_Log("update: %s is available", d->latest.version.c_str());
            // The news in the display language where latest.json has it (news_it, news_de), else in English.
            const std::string& news = app.lang.info.code == "it" && !d->latest.news_it.empty()   ? d->latest.news_it
                                      : app.lang.info.code == "de" && !d->latest.news_de.empty() ? d->latest.news_de
                                                                                                 : d->latest.news_en;
            const std::string text =
                ours(app, "update.available",
                     {{"version", d->latest.version}, {"current", ANTANTA_VERSION}, {"news", news}});
            const std::string install = ours(app, "update.install_now"), later = ours(app, "update.later");
            const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, install.c_str()},
                                                        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, later.c_str()}};
            const SDL_MessageBoxData box{
                SDL_MESSAGEBOX_INFORMATION, app.window, "Entente Engine", text.c_str(), 2, buttons, nullptr};
            int pressed = 0;
            if (SDL_ShowMessageBox(&box, &pressed) && pressed == 1) {
                app.updater->install();
                ui::frontend_message(*app.menu, ours(app, "update.downloading", {{"version", d->latest.version}}));
            }
            break;
        }
        case R::Installed: {
            std::string err;
            if (update::start_again(app.updater->paths().exe, &err)) {
                SDL_Log("update: %s installed and started", d->latest.version.c_str());
                return false;
            }
            update::swap_back(app.updater->paths());
            ui::frontend_message(*app.menu, ours(app, "update.start_failed", {{"reason", reason(app, err)}}));
            break;
        }
    }
    return true;
}




void start_lang_sync(App& app) {
    std::string url = app.lang_url;
#ifdef ANTANTA_RELEASE
    if (url.empty()) url = hosts::kLangUrl;
#endif
    std::string err;
    const std::optional<std::string> folder = app_folder(app, &err);
    if (url.empty() || !folder) return;
    if (url.back() != '/') url += '/';
    lang_pack::Server server;
    server.base = url;
    server.user_dir = std::filesystem::path(std::u8string(folder->begin(), folder->end()));
    app.lang_sync = std::make_unique<lang_pack::Sync>(std::move(server));
    app.lang_sync->start(app.lang.info.code, false);
}

// Every frame of the menus: the finished checks; a pack of the shown language is read at once.
void lang_sync_frame(App& app) {
    if (!app.lang_sync) return;
    while (auto d = app.lang_sync->take()) {
        SDL_Log("language pack %s: %s", d->code.c_str(), d->message.c_str());
        if (d->result == lang_pack::Sync::Result::Installed && d->code == app.lang.info.code)
            choose_language(app, true);
        // The start's check needs the login and the shown language has no game texts yet (a first start that took
        // the system's language): ask once per run whether to log in now (owner, 2026-10-22).
        if (d->result == lang_pack::Sync::Result::LoginNeeded && d->code == app.lang.info.code &&
            app.lang.game.empty() && !app.lang_asked) {
            app.lang_asked = true;
            const std::string text = ours(app, "lang.pack_ask");
            const std::string download = ours(app, "lang.pack_download"), later = ours(app, "update.later");
            const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, download.c_str()},
                                                        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, later.c_str()}};
            const SDL_MessageBoxData box{
                SDL_MESSAGEBOX_INFORMATION, app.window, "Entente Engine", text.c_str(), 2, buttons, nullptr};
            int pressed = 0;
            if (SDL_ShowMessageBox(&box, &pressed) && pressed == 1) app.lang_sync->start(d->code, true);
        }
    }
}
#endif

#ifdef ANTANTA_RELEASE_CHECK
// The public build's Releases notice (platform/release_check.hpp, decisions.md 40): it never updates itself. The
// windowed menu asks GitHub for the newest release at every start (unless Game options' "Check for updates" is off);
// no corner text in the menu (it read like a notice); batch runs never ask. A newer one is shown in a box whose button opens its
// page in the browser; nothing is downloaded. A failure is one line in the session log (and a box only when asked).
void start_release_check(App& app) {
    app.releases = std::make_unique<release_check::Checker>(ANTANTA_VERSION);
    ui::frontend_set_update_link(true, false);  // the Game options row only, no corner text
    if (app.settings.check_updates) app.releases->check(false);
}

// Every frame of the menus outside a game: the answer of a finished check.
void release_check_frame(App& app) {
    if (!app.releases || !app.menu || app.started) return;
    auto d = app.releases->take();
    if (!d) return;
    using R = release_check::Checker::Result;
    SDL_Log("release check: %s", d->message.c_str());
    if (d->result == R::UpToDate && d->loud) {
        ui::frontend_message(*app.menu, ours(app, "update.newest", {{"version", ANTANTA_VERSION}}));
    } else if (d->result == R::Failed && d->loud) {
        ui::frontend_message(*app.menu, ours(app, "update.on_releases_failed"));
    } else if (d->result == R::Newer) {
        const std::string text =
            ours(app, "update.on_releases", {{"version", d->release.version}, {"current", ANTANTA_VERSION}});
        const std::string open = ours(app, "update.open_page"), later = ours(app, "update.later");
        const SDL_MessageBoxButtonData buttons[] = {{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, open.c_str()},
                                                    {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, later.c_str()}};
        const SDL_MessageBoxData box{
            SDL_MESSAGEBOX_INFORMATION, app.window, "Entente Engine", text.c_str(), 2, buttons, nullptr};
        int pressed = 0;
        if (SDL_ShowMessageBox(&box, &pressed) && pressed == 1 && !SDL_OpenURL(d->release.url.c_str()))
            SDL_Log("release check: cannot open %s: %s", d->release.url.c_str(), SDL_GetError());
    }
}
#endif

// Our version in the main menu's corner (docs/release.md); the desktop's developer builds say so.
#if defined(ANTANTA_RELEASE) || defined(__EMSCRIPTEN__)
constexpr bool kDeveloperBuild = false;
#else
constexpr bool kDeveloperBuild = true;
#endif

SDL_AppResult start_menu(App& app) {
#ifndef __EMSCRIPTEN__
    if (!app.net_menu_script.empty()) return run_net_menu_batch(app);  // slice MP2
    if (!app.menu_screen.empty()) return run_menu_batch(app);
#endif
    ui::frontend_set_build_label(ANTANTA_VERSION, kDeveloperBuild);  // the windowed menu only: the batch PNGs stay
    load_settings(app);
    apply_cli_settings(app);
#ifdef ANTANTA_UPDATER
    start_updater(app);
    start_lang_sync(app);
#endif
#ifdef ANTANTA_RELEASE_CHECK
    start_release_check(app);
#endif
    load_profiles(app);
    load_progress(app);  // slice M4
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("cannot start the video: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
#ifdef __EMSCRIPTEN__
    const SDL_WindowFlags hidden = 0;
#else
    const SDL_WindowFlags hidden = SDL_WINDOW_HIDDEN;  // shown by place_window at its place
#endif
    if (!SDL_CreateWindowAndRenderer("Entente Engine", kViewWidth, kViewHeight, kWindowFlags | hidden, &app.window,
                                     &app.renderer)) {
        SDL_Log("cannot create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(app.renderer, 1);
    SDL_SetRenderLogicalPresentation(app.renderer, kViewWidth, kViewHeight, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    place_window(app);
    session_log_video_info(app.window, app.renderer);
    std::string err;
#ifdef __EMSCRIPTEN__
    // The museum's 44 MB come with the page's later pack when the Museum button is pressed (docs/web.md).
    ui::frontend_set_file_gate([](const std::string& path) { return web_need_file(path.c_str()); });
#endif
    app.menu = ui::frontend_open(app.renderer, loader(app), menu_fonts(), app.settings, app.profiles, app.seed, &err,
                                 &app.lang);
    if (!app.menu) {
        SDL_Log("menu: %s", err.c_str());
        return SDL_APP_FAILURE;
    }
    open_audio(app);
    configure_videos(app);
    app.video.play_startup(app.renderer);  // Buka, then the intro, over the menu; the menu shows when they are done
    return SDL_APP_CONTINUE;
}

#ifndef __EMSCRIPTEN__
// A message box with "Quit" and one other button (`yes`); true when that one was pressed.
bool ask_box(const App& app, const std::string& text, const std::string& yes) {
    const std::string quit = ours(app, "box.quit");
    const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, yes.c_str()},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, quit.c_str()},
    };
    const SDL_MessageBoxData box = {
        SDL_MESSAGEBOX_INFORMATION, nullptr, "Entente Engine", text.c_str(), 2, buttons, nullptr};
    int pressed = 0;
    return SDL_ShowMessageBox(&box, &pressed) && pressed == 1;
}

// SDL's folder dialog, waited for (Windows runs it on a thread of its own). nullopt when it was cancelled.
std::optional<std::string> folder_dialog() {
    struct Pick {
        SDL_AtomicInt done;
        std::string folder;
    };
    Pick pick{};
    SDL_SetAtomicInt(&pick.done, 0);
    SDL_ShowOpenFolderDialog(
        [](void* data, const char* const* list, int) {
            Pick& p = *static_cast<Pick*>(data);
            if (list && list[0]) p.folder = list[0];
            if (!list) SDL_Log("folder dialog: %s", SDL_GetError());
            SDL_SetAtomicInt(&p.done, 1);
        },
        &pick, nullptr, nullptr, false);
    while (SDL_GetAtomicInt(&pick.done) == 0) {
        SDL_PumpEvents();
        SDL_Delay(15);
    }
    if (pick.folder.empty()) return std::nullopt;
    return pick.folder;
}

// The game folder when neither --game nor ANTANTA_GAME_DIR names one (platform/game_folder.hpp, docs/release.md): the
// exe's folder, the Steam installs, the remembered folder; then, only in a run with a window, the folder picker until
// a folder with the game is chosen, which is remembered for the next start. False when there is none.
bool locate_game(App& app, bool windowed) {
    std::string err;
    const auto user = app_folder(app, &err);
    const char* base = SDL_GetBasePath();
    std::string found = find_game_folder(base ? base : "", user ? *user : "");
    if (found.empty() && windowed) {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            SDL_Log("cannot start the video: %s", SDL_GetError());
            return false;
        }
        if (!ask_box(app, ours(app, "folder.intro"), ours(app, "folder.choose"))) return false;
        const auto chosen = choose_game_folder(
            folder_dialog,
            [&app](const std::string& folder, const std::string& missing) {
                return ask_box(app, ours(app, "folder.wrong", {{"folder", folder}, {"file", missing}}),
                               ours(app, "folder.choose_another"));
            },
            is_file_on_disk);
        if (!chosen) return false;
        found = *chosen;
        if (!user || !remember_game_folder(*user, found, &err)) {
            SDL_Log("the game folder is not remembered: %s", err.c_str());
        }
    }
    if (found.empty()) {
        SDL_Log(
            "the game folder was not found. usage: antanta [save.svg] [--game <game folder>] [--commands-out <file>] "
            "[--trace <file>] [--script <file>] [--frames <ticks>] [--png <file>] [--size <W>x<H>] [--speed 1|2|4] "
            "[--paused]\n"
            "       antanta --menu [--game <game folder>] [--seed <n>] [--screen main|options|game_options|network|"
            "profiles|single_player|skirmish[:<choices>] [--start --frames <ticks> --trace <file>] --png <file>]\n"
            "the game folder: --game, the ANTANTA_GAME_DIR environment variable, the exe's folder, Steam, the "
            "remembered folder");
        return false;
    }
    while (found.size() > 3 && (found.back() == '/' || found.back() == '\\')) found.pop_back();
    SDL_Log("game folder: %s", found.c_str());
    app.game_dir = found;
    return true;
}
#endif

}  // namespace

namespace {

// SDL_AppInit's work; `windowed` tells whether the run has a window (no batch, PNG or bench run).
// The release exe's box when the start fails (SDL_AppInit), in the language chosen by then (the app is gone when it
// shows).
std::string g_start_failed;

SDL_AppResult app_init(void** appstate, int argc, char** argv, bool* windowed) {
    *windowed = false;
    auto app = std::make_unique<App>();
    app->host.set_files(loader(*app), &app->params);
    if (const char* env = std::getenv("ANTANTA_GAME_DIR")) app->game_dir = env;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (a == "--game") {
            app->game_dir = next();
        } else if (a == "--script") {
            app->script_path = next();
        } else if (a == "--frames") {
            app->batch_ticks = std::max(0, std::atoi(next().c_str()));
        } else if (a == "--png") {
            app->png_path = next();
        } else if (a == "--trace") {
            app->trace_path = next();
        } else if (a == "--trace-hashes") {
            app->trace_hashes = true;
        } else if (a == "--commands-out") {
            app->commands_path = next();
        } else if (a == "--speed") {
            const int v = std::atoi(next().c_str());
            app->speed = v >= 4 ? 4 : v >= 2 ? 2 : 1;
        } else if (a == "--bench-scroll") {
            app->bench_seconds = std::atof(next().c_str());
        } else if (a == "--bench-frame") {
            app->bench_frame_seconds = std::atof(next().c_str());
        } else if (a == "--paused") {
            app->paused = true;
        } else if (a == "--menu") {
            app->menu_mode = true;
        } else if (a == "--video" || a == "+video") {  // the original's `+video 0`
            app->videos_on = std::atoi(next().c_str()) != 0;
#ifdef __EMSCRIPTEN__
        } else if (a == "--frame-log") {
            app->frame_log = true;
#else
        } else if (a == "--videos") {
            app->videos_dir = next();
#endif
#ifdef ANTANTA_UPDATER
        } else if (a == "--update-url") {
            app->update_url = next();
        } else if (a == "--lang-url") {
            app->lang_url = next();
#endif
        } else if (a == "--no-audio") {
            app->audio_off = true;
        } else if (a == "--screen") {
            app->menu_screen = next();
        } else if (a == "--start") {
            app->start_game = true;
        } else if (a == "--end-statistics") {
            app->end_clock = std::max(0, std::atoi(next().c_str()));
        } else if (a == "--ui-scale" || a == "--zoom") {
            const std::string v = next();
            const auto n = static_cast<std::uint8_t>(std::clamp(v == "auto" ? 0 : std::atoi(v.c_str()), 0, 4));
            (a == "--zoom" ? app->cli_zoom : app->cli_ui_scale) = n;
        } else if (a == "--ui-pictures") {
            const std::string v = next();
            app->cli_ui_pictures = static_cast<std::uint8_t>(v == "crisp" ? 1 : v == "smooth" ? 2 : 0);
        } else if (a == "--sim-thread") {  // docs/sim-thread.md: off, on, auto (the setting's values), pictures
            const std::string v = next();
            app->sim_thread = v == "on" ? 1 : v == "auto" ? 2 : 0;
            app->sim_pictures = v == "pictures";
        } else if (a == "--lang" || a == "--lang-setting") {  // docs/languages.md 4
            const std::string v = next();
            if (!ui::lang_code_valid(v)) {
                SDL_Log("%s: '%s' is not a language code", a.c_str(), v.c_str());
                return SDL_APP_FAILURE;
            }
            (a == "--lang" ? app->cli_lang : app->cli_lang_setting) = v;
        } else if (a == "--display-font") {  // docs/languages.md 9 item 2: another font for the painted headings
            ui::frontend_set_display_font(next());
        } else if (a == "--debug-hud") {
            app->debug_hud = true;
        } else if (a == "--plus") {
            app->plus = true;
        } else if (a == "--seed") {
            app->seed = static_cast<std::uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        } else if (a == "--user-dir") {  // slice M4: our files' folder (tests; default %APPDATA%\EntenteEngine)
            app->user_dir = next();
        } else if (a == "--save-dir") {  // slice H: the folder of our saves (tests; default: saves in the user folder)
            app->save_dir = next();
        } else if (a == "--host") {
            app->net_port = std::atoi(next().c_str());
        } else if (a == "--join") {
            app->net_join = next();
        } else if (a == "--net-name") {
            app->net_name = next();
        } else if (a == "--net-skirmish") {
            app->net_choices = next();
        } else if (a == "--net-set") {
            app->net_settings.push_back(next());
        } else if (a == "--net-password") {
            app->net_password = next();
        } else if (a == "--net-bind") {
            app->net_bind = next();
        } else if (a == "--net-log-dir") {
            app->net_log_dir = next();
        } else if (a == "--net-lan-port") {
            app->net_lan_port = static_cast<std::uint16_t>(std::atoi(next().c_str()));
        } else if (a == "--net-menu-script") {
            app->net_menu_script = next();
        } else if (a == "--net-timeout") {
            app->net_timeout_s = std::max(1, std::atoi(next().c_str()));
        } else if (a == "--net-resume") {  // slice MP9
            app->net_resume = next();
        } else if (a == "--net-drop-at") {
            app->net_drop_at = static_cast<std::uint32_t>(std::strtoul(next().c_str(), nullptr, 10));
        } else if (a == "--size") {
            const std::string v = next();
            const std::size_t x = v.find('x');
            if (x != std::string::npos) {
                app->out_w = std::max(320, std::atoi(v.substr(0, x).c_str()));
                app->out_h = std::max(240, std::atoi(v.substr(x + 1).c_str()));
            }
        } else {
            app->save_path = a;
        }
    }
#ifdef __EMSCRIPTEN__
    app->game_dir = "/game";
    app->lower_case_paths = true;
    app->interactive = true;
    choose_language(*app);         // --lang or the system's; the setting once the settings are read
    if (app->save_path.empty()) {  // the front-end, as --menu
        const SDL_AppResult r = start_menu(*app);
        *appstate = app.release();
        return r;
    }
    std::string err;
    load_language_setting(*app);  // the game below is entered before load_settings
    if (!load_game(*app, &err)) {
        SDL_Log("cannot start: %s", err.c_str());
        return SDL_APP_FAILURE;
    }
    load_settings(*app);
#else
#ifdef ANTANTA_RELEASE
    // The release exe (docs/release.md): started without a save (a double click), it opens the main menu.
    if (app->save_path.empty() && app->net_port <= 0 && app->net_join.empty()) app->menu_mode = true;
#else
    if (app->save_path.empty()) app->save_path = default_save();
#endif
    *windowed = app->batch_ticks < 0 && app->menu_screen.empty() && app->bench_frame_seconds <= 0 &&
                app->net_menu_script.empty();
    app->interactive = *windowed;
    choose_language(*app);  // --lang or the system's; the setting once the settings are read
    g_start_failed = ours(*app, "start.failed");
    if (app->game_dir.empty() && !locate_game(*app, *windowed)) {
        return *windowed ? SDL_APP_SUCCESS : SDL_APP_FAILURE;  // with a window: the player chose Quit
    }
    if (app->batch_ticks < 0 && app->menu_screen.empty() && app->bench_frame_seconds <= 0) {
        app->session_log =
            session_log_start(app->game_dir, app->user_dir);  // a run with a window keeps a log in the user's folder
    }
    if (app->menu_mode) {
        const SDL_AppResult r = start_menu(*app);
        *appstate = app.release();
        return r;
    }
    if (*windowed) load_language_setting(*app);  // the game below is entered before load_settings
    std::string err;
    if (app->net_port > 0 || !app->net_join.empty()) {  // a network game: no save, the game comes from the server
        if (!net_open(*app, &err)) {
            SDL_Log("cannot start the network game: %s", err.c_str());
            return SDL_APP_FAILURE;
        }
        if (app->batch_ticks >= 0) {
            apply_cli_settings(*app);
            const SDL_AppResult r = run_net_batch(*app);
            *appstate = app.release();
            return r == SDL_APP_SUCCESS ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
        }
    } else if (!load_game(*app, &err)) {
        SDL_Log("cannot start: %s", err.c_str());
        return SDL_APP_FAILURE;
    }
    if (!app->net_client && !app->script_path.empty() && !load_script(*app, &err)) {
        SDL_Log("%s", err.c_str());
        return SDL_APP_FAILURE;
    }
    if (app->batch_ticks >= 0) {
        apply_cli_settings(*app);
        const SDL_AppResult r = run_batch(*app);
        *appstate = app.release();
        return r == SDL_APP_SUCCESS ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    }
    load_settings(*app);  // the interface settings of Game options
    apply_cli_settings(*app);
#endif
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("cannot start the video: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    const bool bench = app->bench_frame_seconds > 0;  // --bench-frame: hidden, no vsync
#ifdef __EMSCRIPTEN__
    const SDL_WindowFlags hidden = 0;  // a page has no hidden window: the bench draws on the page
#else
    const SDL_WindowFlags hidden = bench ? SDL_WINDOW_HIDDEN : 0;
#endif
    if (!SDL_CreateWindowAndRenderer("Entente Engine", app->out_w, app->out_h, kWindowFlags | hidden, &app->window,
                                     &app->renderer)) {
        SDL_Log("cannot create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(app->renderer, bench ? 0 : 1);
    session_log_video_info(app->window, app->renderer);
    if (!bench) open_audio(*app);
    app->last_ns = SDL_GetTicksNS();
    *appstate = app.release();
    return SDL_APP_CONTINUE;
}

}  // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    bool windowed = false;
    const SDL_AppResult r = app_init(appstate, argc, argv, &windowed);
#ifdef ANTANTA_RELEASE
    if (r == SDL_APP_FAILURE && windowed) {  // the release exe has no console: the player is told where the reason is
        const std::string text =
            g_start_failed.empty() ? ui::lang_text(ui::lang_english(), "start.failed") : g_start_failed;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Entente Engine", text.c_str(), nullptr);
    }
#endif
    return r;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* e) {
    App& app = *static_cast<App*>(appstate);
    if (e->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (e->type == SDL_EVENT_WINDOW_FOCUS_GAINED || e->type == SDL_EVENT_WINDOW_FOCUS_LOST) {  // the music pauses
        audio::audio_focus(app.audio.get(), e->type == SDL_EVENT_WINDOW_FOCUS_GAINED);
    }
#ifdef __EMSCRIPTEN__
    // The page went in or out of full screen (also by the browser: Esc held, F11): the setting follows, as Alt+Enter's.
    if (e->type == SDL_EVENT_WINDOW_RESIZED) {
        const bool full = web_is_fullscreen() != 0;
        if (app.settings.fullscreen != full) {
            app.settings.fullscreen = full, store_settings(app);
            if (app.menu) ui::frontend_set_fullscreen(*app.menu, full);
        }
    }
#else
    if (app.remember_window && (e->type == SDL_EVENT_WINDOW_MOVED || e->type == SDL_EVENT_WINDOW_RESIZED ||
                                e->type == SDL_EVENT_WINDOW_MAXIMIZED || e->type == SDL_EVENT_WINDOW_RESTORED)) {
        note_window(app);
    }
#endif
    if (app.video.active()) {  // a click, Esc, Space or Enter skips the video (ours; the original: Esc only); Alt+Enter
        if (ui::is_video_skip_event(*e)) app.video.skip(app.renderer);  // still switches full screen
        const bool alt_enter =
            e->type == SDL_EVENT_KEY_DOWN && e->key.key == SDLK_RETURN && (e->key.mod & SDL_KMOD_ALT) != 0;
        if (!alt_enter) return SDL_APP_CONTINUE;
    }
    if (app.menu) return menu_event(app, e);
    if (!app.started) return SDL_APP_CONTINUE;
    SDL_ConvertEventToRenderCoordinates(app.renderer, e);  // output pixels (the interface) or the debug HUD's canvas
    if (!app.debug_hud && app.zoom != 1) {                 // world pixels of the view
        const float z = static_cast<float>(app.zoom);
        if (e->type == SDL_EVENT_MOUSE_MOTION) e->motion.x /= z, e->motion.y /= z;
        if (e->type == SDL_EVENT_MOUSE_BUTTON_DOWN || e->type == SDL_EVENT_MOUSE_BUTTON_UP) {
            e->button.x /= z, e->button.y /= z;
        }
    }
    const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
    switch (e->type) {
        case SDL_EVENT_MOUSE_MOTION:
            app.mouse_x = e->motion.x, app.mouse_y = e->motion.y;
            pause_drag_move(app, app.mouse_x, app.mouse_y);  // slice I8
            if (app.hud_armed >= 0 && minimap_on(app)) {     // slice I3: leaving the button disarms it
                int px = 0, py = 0;
                const ui::MinimapLayout l = minimap_place(app, app.mouse_x, app.mouse_y, &px, &py);
                if (ui::hud_button_hit(l, px, py) != app.hud_armed) app.hud_armed = -1;
            }
            if (app.panning) {  // the camera where the ground under the cursor stays under it (at rest: no ease)
                ui::camera_jump(app.cam, app.pan_cam_x - (app.mouse_x - app.pan_x0),
                                app.pan_cam_y - (app.mouse_y - app.pan_y0),
                                ui::camera_bounds(app.live.map, app.view_w, app.view_h));
                app.pos = ui::camera_view(app.cam);
            }
            if (app.minimap_drag) {  // slice I2: the view follows a held press over the minimap
                int px = 0, py = 0;
                if (ui::minimap_contains(minimap_place(app, app.mouse_x, app.mouse_y, &px, &py), px, py)) {
                    minimap_jump(app, app.mouse_x, app.mouse_y);
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e->button.button == SDL_BUTTON_LEFT) {
                app.left_down = true;  // slice I3
                app.dragging = !in_panel(app, e->button.y) && app.build_type < 0 &&
                               !over_panel(app, e->button.x, e->button.y) &&  // slice I4
                               !over_minimap(app, e->button.x, e->button.y);  // slice I2
                app.minimap_drag = false;
                app.drag_x0 = e->button.x + static_cast<float>(view_x(app.pos));
                app.drag_y0 = e->button.y + static_cast<float>(view_y(app.pos));
                app.mouse_x = e->button.x, app.mouse_y = e->button.y;
                if (!app.dragging) left_click(app, e->button.x, e->button.y, shift);
                if (!app.dragging && minimap_on(app)) {  // slice I2: a press on the minimap holds the view
                    int px = 0, py = 0;
                    app.minimap_drag =
                        ui::minimap_contains(minimap_place(app, e->button.x, e->button.y, &px, &py), px, py);
                }
            } else if (e->button.button == SDL_BUTTON_RIGHT) {
                right_press(app, e->button.x, e->button.y);
            } else if (e->button.button == SDL_BUTTON_MIDDLE && smooth_camera(app) && !in_panel(app, e->button.y) &&
                       !over_panel(app, e->button.x, e->button.y)) {
                app.panning = true;
                app.pan_x0 = e->button.x, app.pan_y0 = e->button.y;
                ui::camera_jump(app.cam, app.cam.x, app.cam.y, ui::camera_bounds(app.live.map, app.view_w, app.view_h));
                app.pan_cam_x = app.cam.x, app.pan_cam_y = app.cam.y;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e->button.button == SDL_BUTTON_LEFT) {
                app.minimap_drag = false;  // slice I2
                app.left_down = false;     // slice I3
                app.pause_drag.on = false;
                app.build_held = false;  // slice I5: the held build press ends
                hud_release(app, e->button.x, e->button.y);
            }
            if (e->button.button != SDL_BUTTON_MIDDLE &&  // slice I8: a release after a press on a window
                window_mouse(app, e->button.x, e->button.y, false, e->button.button == SDL_BUTTON_RIGHT,
                             (SDL_GetModState() & SDL_KMOD_SHIFT) != 0)) {
                break;
            }
            if (e->button.button != SDL_BUTTON_MIDDLE &&  // slice I4: a release after a press on the panel
                panel_mouse(app, e->button.x, e->button.y, false, e->button.button == SDL_BUTTON_RIGHT,
                            (SDL_GetModState() & SDL_KMOD_CTRL) != 0, (SDL_GetModState() & SDL_KMOD_SHIFT) != 0)) {
                break;
            }
            if (e->button.button == SDL_BUTTON_LEFT && app.dragging) {
                app.dragging = false;
                const float x0 = app.drag_x0 - static_cast<float>(view_x(app.pos)),
                            y0 = app.drag_y0 - static_cast<float>(view_y(app.pos));
                if (std::abs(e->button.x - x0) < 5 && std::abs(e->button.y - y0) < 5) {
                    left_click(app, e->button.x, e->button.y, shift);
                    if (e->button.clicks >= 2 && !app.sel.units.empty()) {
                        ui::select_types_screen(*app.view(), app.sel,
                                                {view_x(app.pos), view_y(app.pos), app.view_w, app.view_h}, shift);
                    }
                } else {
                    box_select(app, x0, y0, e->button.x, e->button.y, shift);
                }
            } else if (e->button.button == SDL_BUTTON_MIDDLE) {
                app.panning = false;  // stops where it is, no ease
            } else if (e->button.button == SDL_BUTTON_RIGHT) {
                right_release(app, e->button.x, e->button.y);  // slice I9
            }
            break;
        case SDL_EVENT_KEY_DOWN:
            if ((e->key.key == SDLK_RETURN || e->key.key == SDLK_KP_ENTER) && (e->key.mod & SDL_KMOD_ALT) != 0 &&
                !e->key.repeat) {  // ours, as in the menus: Alt+Enter switches between window and full screen
                app.settings.fullscreen = !app.settings.fullscreen;
                apply_fullscreen(app);
                store_settings(app);
                break;
            }
            // Slice I9: the original's keys (ui/select_keys.hpp); with Alt none (WM_SYSKEYDOWN never reaches them).
            if ((e->key.mod & SDL_KMOD_ALT) == 0)
                key(app, key_press(e->key.key, e->key.scancode, e->key.mod), e->key.repeat);
            break;
        case SDL_EVENT_MOUSE_WHEEL:  // ours (docs/decisions.md 41): the zoom about the pointer, trackpads in fractions
            if (smooth_camera(app)) {
                const float y = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
                if (y != 0) zoom_by(app, y, e->wheel.mouse_x, e->wheel.mouse_y, false, false);
            }
            break;
        default: break;
    }
    return SDL_APP_CONTINUE;
}

// Smooth scrolling (ui/camera.hpp): the mouse within 3 pixels of the view's edge or the arrow keys held move the
// camera, eased in and out, at the speed of the Game options sliders; the frame is drawn at the camera's whole pixel.
// The camera is view state only: no tick, no order and no view input depends on it in a window.
void scroll_camera(App& app, double dt) {
    int ex = 0, ey = 0, kx = 0, ky = 0;
    float wx = 0, wy = 0, lx = 0, ly = 0;
    if (app.window && (SDL_GetWindowFlags(app.window) & SDL_WINDOW_MOUSE_FOCUS)) {
        SDL_GetMouseState(&wx, &wy);
        SDL_RenderCoordinatesFromWindow(app.renderer, wx, wy, &lx, &ly);
        float ew = kW, eh = kH;  // the debug HUD's canvas, else the output in pixels
        if (!app.debug_hud) {
            int ow = 0, oh = 0;
            SDL_GetRenderOutputSize(app.renderer, &ow, &oh);
            ew = static_cast<float>(ow), eh = static_cast<float>(oh);
        }
        ex = lx <= 3 ? -1 : lx >= ew - 4 ? 1 : 0, ey = ly <= 3 ? -1 : ly >= eh - 4 ? 1 : 0;
    }
    if (app.window && (SDL_GetWindowFlags(app.window) & SDL_WINDOW_INPUT_FOCUS)) {
        const bool* k = SDL_GetKeyboardState(nullptr);
        kx = (k[SDL_SCANCODE_RIGHT] ? 1 : 0) - (k[SDL_SCANCODE_LEFT] ? 1 : 0);
        ky = (k[SDL_SCANCODE_DOWN] ? 1 : 0) - (k[SDL_SCANCODE_UP] ? 1 : 0);
    }
    // The edge and the keys each at their own slider; with both, the faster one along each axis.
    const double frame_ms = std::max<double>(1, app.view()->speed_ms);
    const ui::ScrollSpeed me = ui::scroll_speed(app.settings.mouse_scroll, frame_ms);
    const ui::ScrollSpeed mk = ui::scroll_speed(app.settings.keyboard_scroll, frame_ms);
    ui::ScrollSpeed v{ex ? me.x : mk.x, ey ? me.y : mk.y};
    if (ex && kx) v.x = std::max(me.x, mk.x);
    if (ey && ky) v.y = std::max(me.y, mk.y);
    const int dx = ex ? ex : kx, dy = ey ? ey : ky;
    if (app.panning) return;  // the middle-mouse drag moves the camera (app_event)
    ui::camera_scroll(app.cam, dx, dy, v, dt, ui::camera_bounds(app.live.map, app.view_w, app.view_h));
    app.pos = ui::camera_view(app.cam);
}

// --bench-frame: one frame's parts; at the end their mean, p99 and max (the first frames, which draw the tiles under
// the view from nothing, left out).
SDL_AppResult bench_frame(App& app, Uint64 start, Uint64 t_draw, Uint64 t_present) {
    const Uint64 end = SDL_GetTicksNS();
    auto ms = [](Uint64 a, Uint64 b) { return static_cast<double>(b - a) / 1e6; };
    const double draw = ms(t_draw, t_present);
    app.bench_frames.push_back({ms(start, t_draw), app.world_ms, app.upload_ms, draw - app.world_ms - app.upload_ms,
                                ms(t_present, end), ms(start, end)});
    app.bench_ticks.push_back(app.bench_tick_n);
    app.bench_tick_n = 0;
    app.bench_frame_t += ms(start, end) / 1000;
    if (app.bench_frame_t < app.bench_frame_seconds + 0.5) return SDL_APP_CONTINUE;
    std::vector<std::array<double, 6>> f(app.bench_frames.begin() + std::min<std::size_t>(30, app.bench_frames.size()),
                                         app.bench_frames.end());
    if (f.empty()) return SDL_APP_SUCCESS;
    int ow = 0, oh = 0;
    SDL_GetRenderOutputSize(app.renderer, &ow, &oh);
    std::fprintf(stderr, "bench frame: output %d x %d, view %d x %d (zoom %.3f), renderer %s, %zu frames\n", ow, oh,
                 app.view_w, app.view_h, app.zoom, SDL_GetRendererName(app.renderer), f.size());
    static constexpr const char* kPart[6] = {"ticks", "world", "upload", "interface", "present", "whole"};
    for (int k = 0; k < 6; ++k) {
        std::vector<double> v;
        for (const auto& row : f) v.push_back(row[static_cast<std::size_t>(k)]);
        std::sort(v.begin(), v.end());
        double sum = 0;
        for (double x : v) sum += x;
        std::fprintf(stderr, "  %-9s mean %6.2f ms, p99 %6.2f, max %6.2f\n", kPart[k],
                     sum / static_cast<double>(v.size()), v[v.size() * 99 / 100], v.back());
    }
    double tick_ms = 0;
    int ticks = 0;
    for (std::size_t i = std::min<std::size_t>(30, app.bench_frames.size()); i < app.bench_frames.size(); ++i) {
        tick_ms += app.bench_frames[i][0], ticks += app.bench_ticks[i];
    }
    std::fprintf(stderr, "  %d ticks in %zu frames, %.2f ms a tick\n", ticks, f.size(), tick_ms / std::max(1, ticks));
    if (app.host.copies() > 0) {  // docs/sim-thread.md: the pictures of --sim-thread on
        std::fprintf(stderr, "  %u pictures, %.2f ms a picture\n", app.host.copies(),
                     app.host.copy_ms() / app.host.copies());
    }
    return SDL_APP_SUCCESS;
}

#ifdef __EMSCRIPTEN__
// --frame-log: the time of each SDL_AppIterate (the whole frame the app draws, not the browser's own part); every 5
// seconds the mean and the worst into the console.
struct FrameLog {
    App& app;
    Uint64 start = SDL_GetTicksNS();
    ~FrameLog() {
        if (!app.frame_log) return;
        const double ms = static_cast<double>(SDL_GetTicksNS() - start) / 1e6;
        app.frame_log_sum += ms, app.frame_log_max = std::max(app.frame_log_max, ms), ++app.frame_log_n;
        const double now = static_cast<double>(SDL_GetTicksNS()) / 1e9;
        if (now - app.frame_log_t < 5) return;
        int w = 0, h = 0;
        if (app.renderer) SDL_GetRenderOutputSize(app.renderer, &w, &h);
        SDL_Log(
            "frame log: %d frames in %.1f s, app part mean %.2f ms, worst %.2f ms, output %d x %d, tick %u, "
            "memory %zu MB",
            app.frame_log_n, now - app.frame_log_t, app.frame_log_sum / std::max(1, app.frame_log_n), app.frame_log_max,
            w, h, app.view() ? app.view()->tick : 0, emscripten_get_heap_size() >> 20);
        app.frame_log_t = now, app.frame_log_sum = app.frame_log_max = 0, app.frame_log_n = 0;
    }
};
#endif

SDL_AppResult SDL_AppIterate(void* appstate) {
    App& app = *static_cast<App*>(appstate);
#ifdef __EMSCRIPTEN__
    const FrameLog frame_log{app};
    web_credits(app);
#endif
    if (app.window_store_ns != 0 && SDL_GetTicksNS() >= app.window_store_ns) {  // the window rested (note_window)
        app.window_store_ns = 0;
        store_settings(app);
    }
    if (app.video.active()) {
        app.video.iterate(app.renderer);
        return SDL_APP_CONTINUE;
    }
#ifdef __EMSCRIPTEN__
    if (app.page_in_game != app.started) {  // the page asks before closing only while a game runs, not in the menus
        app.page_in_game = app.started;
        web_in_game(app.started ? 1 : 0);
    }
#endif
    audio::audio_frame(app.audio.get(), app.started ? &*app.view() : nullptr, listener(app),
                       static_cast<double>(SDL_GetTicksNS()) / 1e9);
#ifndef __EMSCRIPTEN__
    if (app.menu && (app.net_client || ui::frontend_net_screen(*app.menu) != 0 || app.lan_finder)) {
        if (net_menu_frame(app) == SDL_APP_FAILURE) return SDL_APP_FAILURE;  // slice MP2: the network pages
        if (!app.menu) return SDL_APP_CONTINUE;  // the network game started: the next frame draws it
    }
#endif
    if (app.menu && !ui::frontend_game_runs(*app.menu)) {  // slice I8: the game goes on under the diplomacy window
        if (app.cursor_gfx) ui::cursor_show_system(*app.cursor_gfx);  // slice I9: the menus keep the system's cursor
#ifdef ANTANTA_UPDATER
        if (!update_frame(app)) return SDL_APP_SUCCESS;  // docs/download.md: quits for the new version
        lang_sync_frame(app);
#endif
#ifdef ANTANTA_RELEASE_CHECK
        release_check_frame(app);
#endif
#ifdef __EMSCRIPTEN__
        web_lang_frame(app);
#endif
        for (int n = ui::frontend_take_clicks(*app.menu); n > 0; --n) audio::audio_click(app.audio.get());
        if (ui::frontend_overlay(*app.menu) && app.started) draw_frame(app);  // the end message over the game view
        menu_presentation(app);  // the in-game menu on the view, the other screens on 1024 x 768
        ui::frontend_draw(*app.menu);
        SDL_RenderPresent(app.renderer);
#ifdef __EMSCRIPTEN__
        if (!app.menu_drawn) {  // the page hides its own screen (and the browser smoke test sees the menu came up)
            app.menu_drawn = true;
            EM_ASM({
                if (Module.antantaReady) Module.antantaReady();
            });
        }
#endif
        return SDL_APP_CONTINUE;
    }
    if (app.started && app.menu) {  // slice I8: over the diplomacy window the system's cursor, as over the menus
        if (app.cursor_gfx) ui::cursor_show_system(*app.cursor_gfx);
    } else if (app.started) {  // slice I9: the game's cursor for this frame (docs/interface.md D.1)
        if (!app.cursor_gfx) app.cursor_gfx = ui::cursor_graphics_open(app.renderer, loader(app));
        ui::cursor_show(*app.cursor_gfx, game_cursor(app), app.debug_hud ? 1 : app.start_zoom);
    }
    const Uint64 now = SDL_GetTicksNS();
    const bool bench = app.bench_frame_seconds > 0 && app.started;
    if (bench) {  // --bench-frame: full edge speed, turning at the limits
        const double dt = static_cast<double>(now - app.last_ns) / 1e9;
        const ui::CameraBounds b = ui::camera_bounds(app.live.map, app.view_w, app.view_h);
        const double x0 = app.cam.x, y0 = app.cam.y;
        ui::camera_scroll(app.cam, app.bench_dx, app.bench_dy,
                          ui::scroll_speed(app.settings.mouse_scroll, std::max<double>(1, app.view()->speed_ms)), dt,
                          b);
        if (app.cam.x == x0) app.bench_dx = -app.bench_dx;
        if (app.cam.y == y0) app.bench_dy = -app.bench_dy;
        app.pos = ui::camera_view(app.cam);
    } else if (app.started) {
        scroll_camera(app, static_cast<double>(now - app.last_ns) / 1e9);
    }
    if (app.started && smooth_camera(app)) zoom_frame(app, static_cast<double>(now - app.last_ns) / 1e9);
    if (bench) run_script_events(app);  // a script's steps at the save's tick (`+0`) also while --paused
#ifndef __EMSCRIPTEN__                  // the network game (--host, --join) is desktop only so far
    if (app.net_client) {               // a network game: the ticks whose frames arrived, nothing else runs them
        if (app.started && app.host.mode() == SimHost::Mode::Thread && !app.host.lockstep()) {
            TickRecord r;  // the ticks the sim thread ran since the last frame (slice S3)
            while (app.started && app.host.take(&r)) after_tick(app, r);
        }
        if (!net_pump(app, 50)) {
            if (!app.net_menu) {
                SDL_Log("%s", app.net_error.c_str());
                return SDL_APP_FAILURE;
            }
            net_game_lost(app, app.net_error);  // slice MP2: back to the main menu
            return SDL_APP_CONTINUE;
        }
        if (app.started) {
            app.play_clock.advance(static_cast<double>(now - app.last_ns) / 1e6, true);
            publish_clock(app);
            app.blend.alpha = 1.0f;
            run_screen_texts(app, static_cast<double>(now - app.last_ns) / 1e6);
            box_live(app);
        }
    } else
#endif
        if (app.started) {
        // The sim thread (docs/sim-thread.md 4): the ticks it ran since the last frame first, in their order.
        const bool free_run = app.host.mode() == SimHost::Mode::Thread && !app.host.lockstep();
        if (free_run) {
            TickRecord r;
            while (app.started && app.host.take(&r)) after_tick(app, r);
        }
        // The game clock (ui/camera.hpp): ticks follow real time only, never the scrolling (docs/bugs.md B10).
        const double period = std::max<double>(1, app.view()->speed_ms) / app.speed;
        app.play_clock.advance(static_cast<double>(now - app.last_ns) / 1e6, !app.paused && app.view()->paused == 0);
        publish_clock(app);
        ui::tick_clock_begin(app.clock, static_cast<double>(now - app.last_ns) / 1e6, app.paused);

        while (app.started && app.messages.empty() && ui::tick_clock_due(app.clock, period, app.paused)) {
            run_script_events(app);
            if (!app.messages.empty()) break;                  // a script's mission_message
            if (free_run && app.host.in_flight() >= 2) break;  // the ring is full: the tick waits (tick_clock_end)
            do_tick(app);
            ++app.bench_tick_n;
            ui::tick_clock_ran(app.clock, period);
        }
        ui::tick_clock_end(app.clock, period);  // too slow to keep up: drop the backlog
        // Moving things are drawn this far between the last two ticks (paused: where they are).
        // With the sim thread still on a tick, the newest picture as it is (docs/sim-thread.md 4, "Granting ticks").
        app.blend.alpha = app.paused || (free_run && app.host.in_flight() > 0)
                              ? 1.0f
                              : static_cast<float>(std::clamp(app.clock.accum_ms / period, 0.0, 1.0));
        run_screen_texts(app, static_cast<double>(now - app.last_ns) / 1e6);
        if (app.started && !check_end(app)) return SDL_APP_FAILURE;
        if (app.started && !show_messages(app)) return SDL_APP_FAILURE;
        box_live(app);  // the selection follows the rubber band every frame
    }
    app.frame_ms = static_cast<double>(now - app.last_ns) / 1e6;
    app.last_ns = now;
    const Uint64 t_draw = SDL_GetTicksNS();
    draw_frame(app);
    if (app.menu) {  // slice I8: the diplomacy window over the running game
        for (int n = ui::frontend_take_clicks(*app.menu); n > 0; --n) audio::audio_click(app.audio.get());
        menu_presentation(app);
        ui::frontend_draw(*app.menu);
    }
    const Uint64 t_present = SDL_GetTicksNS();
#ifndef __EMSCRIPTEN__
    if (bench && !app.png_path.empty() && app.bench_frames.size() == 60) {  // the picture the GPU renderer drew
        if (save_png(app)) SDL_Log("wrote %s", app.png_path.c_str());
    }
#endif
    SDL_RenderPresent(app.renderer);
    if (bench) return bench_frame(app, now, t_draw, t_present);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    std::unique_ptr<App> app(static_cast<App*>(appstate));
    if (!app) return;
    if (app->window_store_ns != 0) store_settings(*app);  // a window change not stored yet
    app->host.close_files();
    app->host.shutdown();
    if (app->net_trace) std::fclose(app->net_trace);
    app->video.shutdown();
    app->menu.reset();  // its textures and text before the renderer
    SDL_DestroyTexture(app->world_tex);
    SDL_DestroyRenderer(app->renderer);
    SDL_DestroySurface(app->surface);
    SDL_DestroyWindow(app->window);
}
