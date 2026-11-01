// The front-end (docs/frontend.md): the original's menu screens on a 1024 x 768 canvas, drawn with SDL_Renderer and
// SDL3_ttf. Slice FE1: the screen toolkit (windows with a background picture; buttons, images, labels, check boxes,
// sliders and the picture drop-down list of the original's widget classes, frontend.md 2.2), the main menu from
// Data/Menu.txt window 0, the Options and Game options dialogs, and a message window for what is not built yet.
// Slice FE2 adds the single player path: the profile screen, the single player menu and the skirmish ("Random maps")
// setup screen, with the edit field, the scrolling text list and the text combo box of the original's widget set.
//
// The menu screens touch no simulation state. The pictures come from the game's .les archives through the app's
// file loader; magenta (255, 0, 255) is transparent where the original blits with a colour key.
#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "formats/bytes.hpp"
#include "sim/endgame.hpp"
#include "sim/skirmish.hpp"
#include "ui/game_windows.hpp"
#include "ui/lang.hpp"
#include "ui/minimap.hpp"
#include "ui/mission_list.hpp"
#include "ui/museum_screen.hpp"
#include "ui/profiles.hpp"
#include "ui/settings.hpp"

namespace antanta::ui {

// Reads a file of the game folder by its path relative to it ("Data/Menu.txt").
using GameFiles = std::function<std::optional<Bytes>(const std::string& path, std::string* error)>;

struct Frontend;
struct FrontendFree {
    void operator()(Frontend* f) const;
};
using FrontendPtr = std::unique_ptr<Frontend, FrontendFree>;

// Opens the main menu. `font_files` are tried in order for Times New Roman Bold, the original's menu font (the
// owner's own C:\Windows\Fonts\timesbd.ttf first, then the free Liberation Serif Bold). The renderer must already
// draw on the 1024 x 768 canvas (logical presentation). `profiles` are the player names of the profile screen; `seed`,
// when set, is the seed of every skirmish started from the setup screen (tests), otherwise each game gets a random one.
// `lang` is the app's display language (docs/languages.md; it must outlive the front-end): the string table is shown
// through it, its `offered` languages fill the Language row of Game options, and in `xx` every text the menus draw goes
// through the marker check. None: English, no check (tests).
FrontendPtr frontend_open(SDL_Renderer* renderer, GameFiles files, const std::vector<std::string>& font_files,
                          const Settings& settings, const Profiles& profiles, std::optional<std::uint32_t> seed,
                          std::string* error, Language* lang = nullptr);
// The app changed the language it passed: the string table is shown through it again. The windows already open keep
// their texts; the next one opened shows the new language. Their pictures with painted words (docs/languages.md 3)
// are painted again at once.
void frontend_language_changed(Frontend& f);

// What the app has to do after an event. ProfilesChanged: store frontend_profiles (Continue and Back of the profile
// screen). StartSkirmish: Start on the setup screen asked for a game with the choices of frontend_skirmish_setup; the
// app builds and enters it, or shows why not with frontend_message and stays on the setup screen. EndMessageClosed:


// menu; the game is over and is dropped. PlayCredits: the Medal button; the app plays the credits video (or nothing
// with videos off) and the main menu is still there afterwards. The in-game menu (frontend_game_menu): GameMenuClosed,



enum class FrontendRequest {
    None,
    Quit,
    SettingsChanged,
    ProfilesChanged,
    StartSkirmish,
    EndMessageClosed,
    StatisticsClosed,
    PlayCredits,
    GameMenuClosed,
    HintsClosed,
    DiplomacyOk,
    Restart,
    BackToMenu,
    // Slice H, saving and loading (docs/frontend.md 4.10, 4.11): OpenLoad, Load on the main menu or Load Game in the
    // in-game menu, and OpenSave, Save Game: the app lists the saves and calls frontend_load_screen or
    // frontend_save_screen. LoadGame: Load on the load screen (frontend_chosen_save). SaveGame: Save or Enter on the
    // save screen (frontend_save_name); the screen is closed and the game goes on. QuickLoad: Yes on F6's question.
    // Back on these screens in a game asks for GameMenuClosed.
    OpenLoad,
    OpenSave,
    LoadGame,
    SaveGame,
    QuickLoad,





    OpenMissions,
    StartMission,
    MessageClosed,





    OpenCampaigns,
    CampaignSelected,
    OpenCampaignMissions,






    NetScenarios,
    NetHost,
    NetJoin,
    NetStart,
    NetLeave,
    // Slice MP9: the host clicked a seat's row in the lobby of a resumed game (frontend_net_seat says which).
    NetSeat,
    // Ours (docs/download.md "The updater"): the "Check for updates" text of frontend_set_update_link was clicked.
    CheckUpdates
};

// An SDL event already converted to canvas coordinates (SDL_ConvertEventToRenderCoordinates).
FrontendRequest frontend_event(Frontend& f, const SDL_Event& e);
void frontend_draw(Frontend& f);
const Settings& frontend_settings(const Frontend& f);
// Ours: full screen was switched outside the front-end (Alt+Enter, the browser): its settings and an open Options
// dialog's resolution picker (its Full screen item) follow, so a later Ok keeps it.
void frontend_set_fullscreen(Frontend& f, bool on);
// The clicks the buttons, check boxes and lists asked for since the last call (the app plays them, slice J).
int frontend_take_clicks(Frontend& f);
const Profiles& frontend_profiles(const Frontend& f);
// The museum screen's state (filters, list, entry and picture shown); the museum's file (names and texts of the
// entries).
const MuseumState& frontend_museum(const Frontend& f);
const MuseumFile& frontend_museum_file(const Frontend& f);
// What the museum screen shows: the width of its picture (0: none), the lines of its text and whether Prev and Next are
// there.
struct MuseumShown {
    int picture_w = 0, text_lines = 0;
    bool arrows = false;
};
MuseumShown frontend_museum_shown(const Frontend& f);
// The choices Start handed over (valid after StartSkirmish).
const sim::SkirmishSetup& frontend_skirmish_setup(const Frontend& f);
// Opens the message window (500 x 200, close button and Enter) with `text` on top of the current screen.
void frontend_message(Frontend& f, const std::string& text);
// The browser version fetches the museum's files when the Museum button is first pressed (docs/web.md): `need(path)` is
// 1 when the file is there, 0 while it is coming (asked again every frame, the museum opens when it is there) and -1
// when it could not be had (a message says so). Without a gate (the desktop) the museum opens at once.
void frontend_set_file_gate(std::function<int(const std::string& path)> need);
// Ours (docs/release.md): the app's version, drawn small in the main menu's bottom right corner by every front-end
// opened after the call as "Entente Engine <version>" (with "(developer build)" for a developer build) in the display
// language. Empty (the default): nothing is drawn.
void frontend_set_build_label(const std::string& version, bool developer);
// Ours (docs/languages.md 9 item 2): the display font of the painted headings for every front-end opened after the call
// is this font file instead of the built-in one (--display-font, to compare fonts). Empty: the built-in one.
void frontend_set_display_font(const std::string& path);
// Ours (docs/download.md "The updater", and the public build's Releases notice, platform/release_check.hpp): on, every
// front-end opened after the call draws "Check for updates" small above the build label in the main menu's corner, a
// click on it asks for CheckUpdates, and Game options has the row "Check for updates" (Settings::check_updates). Off
// (the default: the browser, batch runs and every build with neither): neither.
void frontend_set_update_link(bool on);





void frontend_end_message(Frontend& f, std::uint8_t kind, const std::string& my_name);



bool frontend_game_message(Frontend& f, const std::string& text, bool end, std::string* error);
// True while the screens are windows over the game view (the end message, the in-game menu and what it opened):
// the app draws the game first and frontend_draw does not clear the canvas.
bool frontend_overlay(const Frontend& f);





bool frontend_game_menu(Frontend& f, bool can_restart, std::string* error);
// Game options in a game (docs/frontend.md 4.4): the app gives your player's advisor switches and conduct template
// before it opens the in-game menu; Game options then shows them (and the template's combo box, without the short
// animation box). Its Ok asks for SettingsChanged as always, and frontend_take_game_advisors hands the choices over
// once (for game_options_orders). The main menu forgets them.
void frontend_set_game_advisors(Frontend& f, const GameAdvisors& a);
std::optional<GameAdvisors> frontend_take_game_advisors(Frontend& f);
// True while the in-game menu is open (the bottom window), with or without the windows it opened over it.
bool frontend_game_menu_open(const Frontend& f);
// The canvas the windows are centred on (default 1024 x 768): the app gives the in-game menu the game view's size, so
// it sits in the middle of the view as in the original. Windows already open move to the new centre.
void frontend_set_canvas(Frontend& f, int w, int h);







bool frontend_mission(Frontend& f, const std::string& title, const std::vector<std::string>& paragraphs,
                      std::string* error);
bool frontend_hints(Frontend& f, std::vector<std::string> hints, int index, bool startup, std::string* error);
int frontend_hint_index(const Frontend& f);
bool frontend_hints_startup(const Frontend& f);
bool frontend_diplomacy(Frontend& f, std::vector<DiplomacyRow> rows, std::string* error);
const std::vector<DiplomacyRow>& frontend_diplomacy_rows(const Frontend& f);
// True while the bottom window is one of the in-game windows (the in-game menu, mission, hints, diplomacy, a game
// message): the app draws them over the game view at the interface's scale.
bool frontend_game_window_open(const Frontend& f);

bool frontend_game_runs(const Frontend& f);




struct SaveEntry {
    std::string name, path;
    std::array<std::string, 5> info;
    std::function<std::optional<Minimap>()> preview;
};


bool frontend_load_screen(Frontend& f, std::vector<SaveEntry> saves, bool in_game, std::string* error);
// The save the load screen's Load asked for (valid after LoadGame).
const SaveEntry& frontend_chosen_save(const Frontend& f);



bool frontend_save_screen(Frontend& f, std::vector<std::string> names, std::string* error);
std::string frontend_save_name(const Frontend& f);


bool frontend_confirm_quick_load(Frontend& f, std::string* error);





bool frontend_mission_list(Frontend& f, std::vector<MissionEntry> missions, int selected, std::string* error);
// The mission Load asked for (valid after StartMission).
const MissionEntry& frontend_chosen_mission(const Frontend& f);




bool frontend_campaign_select(Frontend& f, std::vector<CampaignEntry> campaigns, int selected, std::string* error);
// The selected campaign and its row (-1 and an empty entry when none).
const CampaignEntry& frontend_campaign(const Frontend& f);
int frontend_campaign_row(const Frontend& f);



bool frontend_campaign_missions(Frontend& f, std::vector<MissionEntry> missions, int selected, std::string* error);
// The name a save file may have (ours: letters, digits and _, at most 30 characters; the original also lets through
// characters that Windows refuses in a file name, docs/bugs.md B35).
std::string save_name_clip(const std::string& text);



struct EndPlayer {
    std::uint8_t slot = 0;
    std::string name;
    std::uint32_t colour = 0;
    sim::PlayerStatistics stats;
};


void frontend_statistics(Frontend& f, std::vector<EndPlayer> players);

// The network game from the menu (slice MP2, docs/multiplayer.md "Built in step 2"; desktop only, the browser version
// keeps the message of the Network game button). The original's pages of Multiplay.les with our widgets on them:
// page 1 (MP_Fon_P1): your name with the profile list, the host's address and the port; Continue opens page 2
// (MP_Fon_P2): the games found on this network and the typed address, the selected one's map and lines, the password,
// Join, and Create, which opens the skirmish screen as the host's setup (a Human level for the other players, the game
// speed, Create in place of Start). The lobby (MP_Fon_P4) shows the slots and who is in; Start for the host, Return.
struct NetChoices {
    std::string name;      // your name (page 1)
    std::string address;   // NetJoin: "host:port" of the selected row
    std::string password;  // page 2's field: the one to join with, or the new game's
    std::uint16_t port = 47625;
    std::uint32_t tick_ms = 30;  // the host's game speed (30: the game's x1)
    std::string resume;          // slice MP9: Join on a saved game's row: that save, hosted again
};
const NetChoices& frontend_net_choices(const Frontend& f);
// A game the app found on this network (net/lan.hpp).
struct NetGameRow {
    std::string name, map, address;
    int humans = 0, max_humans = 0;
    bool locked = false;
    // Slice MP9: a saved network game of ours (its file; Join hosts it again), with its players and when it was saved.
    std::string save, players, when;
};
// Page 2's list: the typed address first (when there is one), then these rows. The selection follows its address.
void frontend_net_games(Frontend& f, const std::vector<NetGameRow>& rows);
// One slot of the lobby.
struct NetLobbySlot {
    std::string player;  // the human's name, "Open", or the computer's level
    int nation = 0, colour = 0, team = 0;
    bool human = false, joined = false;
    int slot = -1;          // slice MP9: the setup's slot
    bool computer = false;  // slice MP9: a resumed game's seat the host gave to the computer
};
struct NetLobby {
    std::string title, map;
    bool plus = false;  // the setup's rule set: Entente Engine Plus (shown under the map)
    std::vector<NetLobbySlot> slots;
    std::vector<std::string> lines;  // what is going on, in the lower right box
    bool host = false, can_start = false;
    bool seats = false;  // slice MP9: a resumed game's lobby: the host clicks the seats (FrontendRequest::NetSeat)
};
// Opens the lobby over the games page, or shows it again with these contents.
bool frontend_net_lobby(Frontend& f, const NetLobby& lobby, std::string* error);



bool frontend_net_scenarios(Frontend& f, std::vector<MissionEntry> scenarios, int selected, std::string* error);
// Which network page is on top: 0 none, 1 page 1, 2 the games page, 3 the host's setup, 4 the lobby, 5 the scenarios.
int frontend_net_screen(const Frontend& f);
// Back to the games page (the lobby and the host's setup closed), with a message window when `why` is not empty.
void frontend_net_back(Frontend& f, const std::string& why);
// Slice MP9: the slot of the lobby row the host clicked last (FrontendRequest::NetSeat).
int frontend_net_seat(const Frontend& f);

// For screenshots and tests: opens a screen as the player would reach it from the main menu. Names: "main",
// "options", "game_options", "network" (page 1 of the Network game button), "network_games" (page 2), "network_host"
// (the host's setup; "network_host_plus" with Entente Engine Plus chosen), "profiles", "single_player",
// "skirmish", "game_menu" (the in-game menu alone), "confirm_restart" (it with the question of Restart Mission). The
// skirmish screen takes choices after a colon, separated by semicolons: "skirmish:players=4;resources=2;
// map=Aust_3r_4.dat;slots=4/0/0/0,3/1/1/0;rules=plus" (slots are level/nation/colour/team per row; a closed row ends
// the list; rules: classic or plus).
bool frontend_show(Frontend& f, const std::string& screen, std::string* error);
// For tests (slice MP2, the network menu's scripts): sets the choices of the open skirmish screen (also the host's
// setup) as the skirmish screen's spec of frontend_show does. False when the top screen is not the skirmish screen.
bool frontend_skirmish_choices(Frontend& f, const std::string& spec, std::string* error);
// Off: the load screen's door is drawn open at once (batch runs, whose screenshots must not depend on the clock).
void frontend_set_animate(Frontend& f, bool on);

}  // namespace antanta::ui
