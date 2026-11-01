#include "ui/frontend.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <tuple>
#include <utility>

#include "formats/image.hpp"
#include "formats/les.hpp"
#include "formats/map.hpp"
#include "formats/menu.hpp"
#include "formats/museum.hpp"
#include "ui/fonts.hpp"
#include "ui/game_menu.hpp"
#include "ui/museum_screen.hpp"
#include "ui/painted.hpp"
#include "ui/skirmish_screen.hpp"
#include "ui/text_fit.hpp"

namespace antanta::ui {

// The built-in display font's bytes (display_font.cpp, made by CMake).
const unsigned char* display_font_data();
std::size_t display_font_size();

namespace {

constexpr int kCanvasW = 1024, kCanvasH = 768;

struct TextureFree {
    void operator()(SDL_Texture* t) const { SDL_DestroyTexture(t); }
};
using Texture = std::unique_ptr<SDL_Texture, TextureFree>;
struct TextFree {
    void operator()(TTF_Text* t) const { TTF_DestroyText(t); }
};
using Text = std::unique_ptr<TTF_Text, TextFree>;

struct Picture {
    Texture tex;
    int w = 0, h = 0;
    // ours (docs/languages.md 3): a picture with painted words keeps where it came from, to be painted again when the
    // language changes (frontend_language_changed)
    std::string archive, name;
    bool keyed = false;
};

enum class Action {
    None,
    Exit,
    Medal,
    Settings,
    SinglePlayer,
    Museum,
    Network,
    Load,
    OptionsOk,
    OptionsCancel,
    GameOptions,
    GameOptionsOk,
    GameOptionsCancel,
    MusicToggle,
    SoundToggle,
    SoundVolume,
    MessageClose,
    // The in-game windows of slice I8 (docs/interface.md C.4, C.6, C.7)
    MissionClose,
    HintsClose,
    HintsPrev,
    HintsNext,
    HintsStartup,
    DiplomacyCheck,
    DiplomacyOk,
    DiplomacyCancel,
    // Single player (frontend.md 4.5 to 4.7)
    ProfileAdd,
    ProfileDelete,
    ProfileSelect,
    ProfileContinue,
    ProfileBack,
    SpCampaigns,
    SpMissions,
    SpRandomMaps,
    SpBack,
    SkPlayers,
    SkResources,
    SkRules,          // ours: the rule set box (Entente Engine or Entente Engine Plus, docs/your-version.md)
    SkRow,
    SkPrev,
    SkNext,
    SkStart,
    SkBack,
    // The museum (frontend.md 4.16)
    MuseumBack,
    MuseumNext,
    MuseumPrev,
    MuseumNation,
    MuseumType,
    MuseumPick,
    // The end of a game (docs/endgame.md 3.3, 4.1)
    EndClose,
    StatPlayer,
    StatClose,
    // The in-game menu (frontend.md 4.12, 4.13)
    GameMenu,    // a button of Menu.txt window 4; the widget's `row` is its GameMenuItem
    ConfirmYes,  // the Confirm window's Yes (Enter): the asking button's action
    ConfirmNo,   // and No (Esc): back to the menu
    // Saving and loading (slice H, frontend.md 4.10, 4.11)
    FileSelect,
    FileLoad,
    FileSave,
    FileBack,
    // The stand-alone missions (slice M4, docs/missions.md 4.2)
    MissionSelect,
    MissionLoad,
    MissionBack,
    GameMessageClose,
    // The campaigns (slice M7, docs/missions.md 4.1)
    CampaignSelect,
    CampaignApply,
    CampaignMenu,
    // The network game from the menu (slice MP2, ours on the original's Multiplay.les pages)
    NetContinue,    // page 1: Continue (Enter), the games page
    NetReturn,      // page 1 and 2: Return (Esc), the page before
    NetField,       // marks the address, port and password fields (net_field_clip)
    NetGameSelect,  // page 2: the list's select callback
    NetJoin,        // page 2: Join (Enter)
    NetCreate,
    NetScenarioSelect,
    NetScenarioCreate,
    NetHostCreate,      // the host's setup: Create in place of Start
    NetSpeed,           // the host's setup: the game speed box
    NetStart,           // the lobby: Start (Enter), the host's
    NetLeave,           // the lobby: Return (Esc)
    NetSeat,            // slice MP9: a seat's row in a resumed game's lobby, the host's click
};



constexpr Action kHandlers[] = {Action::Exit,   Action::Medal,   Action::Settings, Action::SinglePlayer,
                                Action::Museum, Action::Network, Action::Load};

enum class Kind {
    Button,
    Image,
    Label,
    CheckBox,
    Slider,
    DropList,
    Edit,
    TextList,
    Combo,
    Choice,
    TextBox,
    MuseumCombo,
    SlideShow
};

// What a widget of the skirmish screen stands for (the rows are found again by it when the table changes).
enum class Role {
    None,
    Name,
    Level,
    Nation,
    Colour,
    Team,
    Players,
    Resources,
    Rules,  // ours: the skirmish setup's rule set box
    Preview,
    Start,
    Prev,
    Next,
    MusNation,
    MusType,
    MusList,
    MusPicture,
    MusText,
    MissionText,
    CampaignText,
    NetAddress,    // slice MP2: page 1's fields, page 2's password and lines (`row`)
    NetPort,
    NetPassword,
    NetInfo
};

// One widget of a window. Coordinates are in the window. Which fields a kind uses is said at each.
struct Widget {
    Kind kind = Kind::Image;
    Action action = Action::None;  // button click, check box toggle, slider change
    int x = 0, y = 0, w = 0, h = 0;
    Picture pic;  // button (three states side by side), image, check box (two states), slider track
    bool enabled = true;

    int state = 0;
    bool pressed = false;
    std::uint8_t hotkey = 0;

    bool value = false;

    float pos = 0;
    Picture knob;
    bool drag = false;


    std::vector<Picture> items;
    Picture list_button;
    int field_w = 0;  // the picture field left of the button
    int sel = 0;
    bool open = false;
    bool below = true;

    Text text;
    std::string label;
    bool centred = false;
    bool shadow = false;
    // every kind: hidden widgets are neither drawn nor hit; the skirmish screen's row widgets know their row
    bool visible = true;
    bool dimmed = false;  // ours: a button whose action is not in the new engine yet (drawn darker, never lit)
    Role role = Role::None;
    int row = -1;

    bool focused = false;
    std::size_t max_chars = 0;
    std::uint64_t focus_ms = 0;

    int ox = 0, oy = 0;



    int tb_right = 0, tb_bottom = 0;
    std::array<int, 4> tb_up{}, tb_down{};


    std::vector<std::string> strs;
    std::vector<Text> item_text;


    int list_row = 25, list_ox = 12, list_oy = 14, list_up_y = 18, list_bottom = 0;
    bool font16 = false;


    std::vector<SDL_Color> row_ink;

    SDL_Color ink{0xff, 0xad, 0x0f, 0xff};
    int top = 0;
    int row_h = 0, list_h = 0;
    Picture up, down, bar;


    int thumb_y = 0;
    bool thumb_on = true;





    std::uint64_t start_ms = 0;
    int step_ms = 30;
    int per_pic = 2;
    bool loop = false;
    // ours (slice MP2): an image shows only this part of its picture
    std::optional<SDL_FRect> part;
    // ours (docs/languages.md 2.5, slice L2b): a text fitted into its place. A label made smaller or put on two lines
    // is drawn `text_dy` lower (its baseline kept, or its two lines centred on the line), its second line `text2`
    // `text2_dy` below the first; `tip` is the full text of a label cut with an ellipsis or shortened, shown under the
    // mouse. The values of a box or the rows of a list: `item_dy` and `item_tips` per item (empty: none fitted).
    // `fit`: the place of a list's rows (list_set_rows fits them), none for lists of names.
    Text text2;
    int text_dy = 0, text2_dy = 0;
    std::string tip;
    std::vector<int> item_dy;
    std::vector<std::string> item_tips;
    const char* fit = nullptr;
};

enum class Screen {
    Main,
    Options,
    GameOptions,
    Message,
    Mission,
    Hints,
    Diplomacy,
    Profiles,
    SinglePlayer,
    Skirmish,
    EndMessage,
    Statistics,
    GameMenu,
    Confirm,
    LoadGame,
    SaveGame,
    Museum,
    MissionList,
    GameMessage,
    CampaignSelect,
    NetPlayer,       // slice MP2: page 1 of the network game (MP_Fon_P1)
    NetGames,        // page 2 (MP_Fon_P2)
    NetScenarios,
    NetLobby,        // the lobby (MP_Fon_P4)
};

struct Archive {
    Bytes data;
    LesTable table;
    std::string path;
};

struct Window {
    Screen screen = Screen::Main;
    int x = 0, y = 0, w = 0, h = 0;
    Picture back;
    std::vector<Widget> widgets;
    // ours (docs/languages.md 2.4 rule 1): the Confirm window grows by `grow` pixels for a long question; the picture
    // is drawn in two parts, apart at `grow_at`, with its rows above that repeated in between
    int grow_at = 0, grow = 0;
    int drag_h = 0;
    WindowDrag drag;
};

}  // namespace

struct Frontend {
    SDL_Renderer* renderer = nullptr;
    GameFiles files;
    TTF_Font* font18 = nullptr;
    TTF_Font* font16 = nullptr;
    std::string font_path;       // the font file of both, for the smaller sizes of a fitted text (font_em)
    std::map<int, TTF_Font*> fit_fonts;
    // ours (docs/languages.md 3, slice L4): the display font of the painted headings at each em, and the picture of the
    // archive a painted button lies on (PaintFill::Under), kept for the next button of the same window
    std::map<int, TTF_Font*> display_fonts;
    std::string under_name;
    Image under;
    Text version;             // "Entente v 2.0" on the main menu
    Text build_label;         // ours: our version in the main menu's corner (frontend_set_build_label)
    Text update_link;         // ours: "Check for updates" above it (frontend_set_update_link)
    SDL_FRect update_rect{};  // where update_link was drawn last
    Text cursor;              // the edit field's bar
    TTF_TextEngine* engine = nullptr;
    std::vector<MenuWindow> menu;
    // The string table as shown (through the display language, docs/languages.md) and as read (English, kept for a
    // language change). The front-end hands no text to the game, so the table it shows may be translated.
    std::map<std::string, std::string> strings, strings_en;
    Language* lang = nullptr;             // the app's display language; none: English without the marker check
    std::vector<std::string> lang_codes;  // the codes of the Language row of Game options, in its order
    Settings settings;                    // as last stored
    Settings edit;                        // what the open Options dialog shows (Ok stores it, Cancel drops it)
    std::deque<Window> stack;             // a deque: a window pushed by an action leaves the others where they are
    int clicks = 0;                       // the click sounds the widgets asked for since frontend_take_clicks (slice J)
    float mouse_x = 0, mouse_y = 0;
    bool held = false;
    FrontendRequest request = FrontendRequest::None;
    // single player
    Profiles profiles;
    std::optional<std::uint32_t> seed;  // --seed; else every game gets a random one
    std::vector<MapEntry> maps;         // Maps/random.ind without the corrupt map
    bool maps_read = false;
    SkirmishScreen sk;  // the skirmish screen's table
    bool plus = false;  // ours: the rule set chosen last on the skirmish screen (Settings::rules_plus)
    // the museum (4.16): the file with the names and texts of the text table, what is shown, and Pictures.les (41 MB,
    // kept while the museum is open)
    MuseumFile museum;
    MuseumState mus;
    bool museum_wait = false;  // the Museum button was pressed and the museum's files are still coming (the web)
    std::optional<Archive> mus_pictures;
    sim::SkirmishSetup setup;           // what Start handed over last
    bool show_cursor = true;            // the edit field's blinking cursor (off for batch screenshots)
    SDL_Window* text_window = nullptr;  // the window whose text input the edit field needs

    std::vector<EndPlayer> end_players;
    std::size_t stat_sel = 0;
    // the canvas the windows are centred on (frontend_set_canvas); the in-game menu: whether Restart works, and the
    // button the open Confirm window asks for
    int canvas_w = kCanvasW, canvas_h = kCanvasH;
    bool can_restart = false;
    // Game options in a game (docs/frontend.md 4.4): your player's advisor switches and conduct template as the game
    // has them (none: no game, the menu's Game options), and the choices its Ok took, for the app to send
    std::optional<GameAdvisors> game_advisors, advisors_chosen;
    bool animate = true;  // the load screen's door opens step by step (batch screenshots: drawn open at once)
    GameMenuItem confirm_item = GameMenuItem::None;

    std::vector<std::string> hints;
    int hint_index = 0;
    bool hints_startup = false;
    std::vector<DiplomacyRow> diplomacy;
    // slice H: the load screen's saves, whether it was opened in a game (Back goes back to it), the one Load chose
    std::vector<SaveEntry> saves;
    bool file_in_game = false;
    SaveEntry chosen;
    std::string save_name;
    // slice M4: the mission list's rows and the one Load chose
    std::vector<MissionEntry> missions;
    MissionEntry chosen_mission;


    std::vector<CampaignEntry> campaigns;
    int campaign_sel = -1;
    bool campaign_list = false;
    // slice MP2: the network pages' choices, the typed host address, the games found on this network, page 2's rows
    // (the typed address first) and the selected one's address, whether the skirmish screen is the host's setup, the
    // lobby shown
    NetChoices net;
    std::string net_ip;
    std::vector<NetGameRow> net_found, net_rows;
    std::string net_selected;
    bool net_host = false;
    NetLobby lobby;
    int net_seat = -1;                    // slice MP9: the slot of the lobby row clicked last
    std::vector<MissionEntry> scenarios;  // page 3's rows: the multiplayer scenarios
    ~Frontend() {
        stack.clear();
        version.reset();
        build_label.reset();
        update_link.reset();
        cursor.reset();
        if (text_window) SDL_StopTextInput(text_window);
        if (engine) TTF_DestroyRendererTextEngine(engine);
        if (font18) TTF_CloseFont(font18);
        if (font16) TTF_CloseFont(font16);
        for (auto& [em, font] : display_fonts) TTF_CloseFont(font);  // before the fallback fonts below
        for (auto& [em, font] : fit_fonts) TTF_CloseFont(font);
        TTF_Quit();
    }
};

void FrontendFree::operator()(Frontend* f) const { std::unique_ptr<Frontend> owned(f); }  // owns and deletes

namespace {

// ---- loading ----

std::optional<Archive> open_archive(Frontend& f, const std::string& path, std::string* error) {
    Archive a;
    a.path = path;
    auto data = f.files(path, error);
    if (!data) return std::nullopt;
    a.data = std::move(*data);
    auto table = parse_les(a.data, error);
    if (!table) {
        *error = path + ": " + *error;
        return std::nullopt;
    }
    a.table = std::move(*table);
    return a;
}

void paint_words(Frontend& f, const Archive& a, const std::string& name, Image& img);  // below
void paint_full_screen(Frontend& f, Image& img);                                       // below

// Ours: the name of the Options resolution picker's Full screen item (ui/painted.hpp painted_full_screen), loaded
// through load_picture like a picture of Options.les so it is painted again when the language changes.
constexpr const char* kFullScreenItem = "full screen";
constexpr int kFullScreenSel = 5;  // its place in the picker, after the five sizes



std::optional<Picture> load_picture(Frontend& f, const Archive& a, const std::string& name, bool keyed,
                                    std::string* error) {
    const bool full_screen = name == kFullScreenItem;
    const LesEntry* e = find_entry(a.table, full_screen ? painted_full_screen().picture : name);
    if (!e) {
        *error = a.path + ": no picture " + name;
        return std::nullopt;
    }
    auto bytes = read_entry(a.data, *e, error);
    if (!bytes) return std::nullopt;
    auto img = decode_bmp(*bytes, error);
    if (!img) {
        *error = a.path + "/" + name + ": " + *error;
        return std::nullopt;
    }
    if (full_screen) {
        paint_full_screen(f, *img);
    } else {
        paint_words(f, a, name, *img);
    }
    if (keyed) {
        for (std::size_t i = 0; i + 3 < img->rgba.size(); i += 4) {
            if (img->rgba[i] == 255 && img->rgba[i + 1] == 0 && img->rgba[i + 2] == 255) img->rgba[i + 3] = 0;
        }
    }
    SDL_Surface* s =
        SDL_CreateSurfaceFrom(img->width, img->height, SDL_PIXELFORMAT_RGBA32, img->rgba.data(), img->width * 4);
    Picture p;
    p.tex.reset(s ? SDL_CreateTextureFromSurface(f.renderer, s) : nullptr);
    SDL_DestroySurface(s);
    if (!p.tex) {
        *error = name + ": " + SDL_GetError();
        return std::nullopt;
    }
    SDL_SetTextureScaleMode(p.tex.get(), SDL_SCALEMODE_NEAREST);  // DirectDraw's stretching blit
    SDL_SetTextureBlendMode(p.tex.get(), keyed ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    p.w = img->width, p.h = img->height;
    if (full_screen || !painted_find(a.path, name).empty()) p.archive = a.path, p.name = name, p.keyed = keyed;
    return p;
}

// The text height GetTextExtentPoint32W gives for Times New Roman Bold 18: its GDI cell height (measured, docs/
// frontend.md 7). The rounding above does not give it exactly (18 at em 15), so it is the measured number.
constexpr int kCell18 = 17;


Widget make_button(Picture pic, int x, int y, Action action, std::uint8_t hotkey) {
    Widget w;
    w.kind = Kind::Button;
    w.x = x, w.y = y, w.w = pic.w / 3, w.h = pic.h;
    w.pic = std::move(pic);
    w.action = action;
    w.hotkey = hotkey;
    return w;
}


Widget make_check(Picture pic, int x, int y, bool value, Action action) {
    Widget w;
    w.kind = Kind::CheckBox;
    w.x = x, w.y = y, w.w = pic.w / 2, w.h = pic.h;
    w.pic = std::move(pic);
    w.value = value;
    w.action = action;
    return w;
}

Widget make_slider(Picture track, Picture knob, int x, int y, int width, int height, float value, Action action) {
    Widget w;
    w.kind = Kind::Slider;
    w.x = x, w.y = y, w.w = width, w.h = height;
    w.pic = std::move(track);
    w.knob = std::move(knob);
    w.pos = value;
    w.action = action;
    return w;
}



Widget make_label(Frontend& f, const std::string& key, int x, int y) {
    Widget w;
    w.kind = Kind::Label;


    w.x = x, w.y = y, w.h = 14;
    auto it = f.strings.find(key);
    w.label = it != f.strings.end() ? it->second : key;
    w.shadow = true;
    return w;
}

void make_text(Frontend& f, Widget& w, SDL_Color colour,
               const std::source_location& where = std::source_location::current()) {
    check_drawn(f.lang, w.label, where);
    w.text.reset(TTF_CreateText(f.engine, f.font18, w.label.c_str(), 0));
    if (w.text) TTF_SetTextColor(w.text.get(), colour.r, colour.g, colour.b, colour.a);
}

// ---- the screens ----

bool open_main(Frontend& f, std::string* error) {
    if (f.menu.empty()) {
        *error = "Menu.txt has no window 0";
        return false;
    }
    const MenuWindow& mw = f.menu[0];
    f.game_advisors.reset();           // the main menu: no game
    auto a = open_archive(f, menu_archive_path(mw.archive), error);
    if (!a) return false;
    Window win;
    win.screen = Screen::Main;
    win.x = mw.x, win.y = mw.y, win.w = mw.w, win.h = mw.h;
    auto back = load_picture(f, *a, mw.background, false, error);
    if (!back) return false;
    win.back = std::move(*back);
    for (const MenuElement& e : mw.elements) {
        if (e.type != 1) continue;  // window 0 has buttons only; slide shows and images come with later screens
        auto pic = load_picture(f, *a, e.file, true, error);
        if (!pic) return false;
        const Action act = e.handler < std::size(kHandlers) ? kHandlers[e.handler] : Action::None;
        if (act == Action::None) SDL_Log("Menu.txt window 0: handler %u is not a main menu action", e.handler);
        Widget b = make_button(std::move(*pic), e.x, e.y, act, e.hotkey);
        b.w = e.w, b.h = e.h;  // MakeMenu takes the size from the file (the picture is stretched to it)
        win.widgets.push_back(std::move(b));
    }
    f.stack.clear();
    f.stack.push_back(std::move(win));
    return true;
}

Widget make_plain_label(Frontend& f, const std::string& text, int x, int y, SDL_Color colour,
                        const std::source_location& where = std::source_location::current());  // below
// Our text of `key` in the display language (engine/lang/en/ui.txt, docs/languages.md slice L2).
std::string ours(const Frontend& f, std::string_view key,
                 const std::vector<std::pair<std::string, std::string>>& args = {});


bool open_options(Frontend& f, std::string* error) {
    auto a = open_archive(f, "Data/Interface/Menu/Options.les", error);
    if (!a) return false;
    f.edit = f.settings;
    Window win;
    win.screen = Screen::Options;
    auto back = load_picture(f, *a, "Back.bmp", false, error);
    if (!back) return false;
    win.back = std::move(*back);
    win.w = win.back.w, win.h = win.back.h;
    win.x = f.canvas_w / 2 - win.w / 2, win.y = f.canvas_h / 2 - win.h / 2;  // centred on the screen
    win.drag_h = kDragStripOptions;
    std::optional<Picture> p[3], k[3];
    const char* const kTracks[] = {"Green.bmp", "Blue.bmp", "Red.bmp"};
    for (int i = 0; i < 3; ++i) {
        p[i] = load_picture(f, *a, kTracks[i], true, error);
        k[i] = load_picture(f, *a, "Slider.bmp", true, error);
        if (!p[i] || !k[i]) return false;
    }
    win.widgets.push_back(make_slider(std::move(*p[0]), std::move(*k[0]), 314, 160, 291, 65,
                                      volume_to_slider(f.edit.music_volume), Action::None));
    win.widgets.push_back(make_slider(std::move(*p[1]), std::move(*k[1]), 317, 230, 291, 65,
                                      volume_to_slider(f.edit.sound_volume), Action::SoundVolume));
    win.widgets.push_back(make_slider(std::move(*p[2]), std::move(*k[2]), 315, 67, 291, 65,
                                      speed_to_slider(f.edit.speed_ms), Action::None));
    Widget list;
    list.kind = Kind::DropList;
    list.x = 370, list.y = 332, list.field_w = 185;
    for (const char* name : {"1024x768.bmp", "1152x864.bmp", "1280x960.bmp", "1280x1024.bmp", "1600x1200.bmp"}) {
        auto item = load_picture(f, *a, name, true, error);
        if (!item) return false;
        list.items.push_back(std::move(*item));
    }
    auto lb = load_picture(f, *a, "ComboBoxButton.bmp", true, error);
    if (!lb) return false;
    list.list_button = std::move(*lb);
    list.w = list.field_w + list.list_button.w / 3;
    list.h = list.items[0].h;
    list.sel = std::min<int>(f.edit.resolution, static_cast<int>(list.items.size()) - 1);
#ifndef __EMSCRIPTEN__
    // Ours: a sixth item, Full screen (borderless, Settings::fullscreen, as Alt+Enter); a size is a window that big.
    auto full = load_picture(f, *a, kFullScreenItem, true, error);
    if (!full) return false;
    list.items.push_back(std::move(*full));
    if (f.edit.fullscreen) list.sel = kFullScreenSel;
#endif
#ifdef __EMSCRIPTEN__
    // Ours: the page always fits the browser window, so the choice has no effect. The list stays (OptionsOk reads it,
    // so the stored value is kept) but is hidden, and a label says what happens instead (docs/web.md).
    list.visible = false;
    const int list_h = list.h;
    win.widgets.push_back(std::move(list));
    win.widgets.push_back(make_plain_label(f, ours(f, "options.fits_window"), 372, 332 + (list_h - 14) / 2,
                                           SDL_Color{0xff, 0xf7, 0x99, 0xff}));
#else
    win.widgets.push_back(std::move(list));
#endif
    auto gb = load_picture(f, *a, "GreenBox.bmp", true, error);
    auto bb = load_picture(f, *a, "BlueBox.bmp", true, error);
    if (!gb || !bb) return false;
    win.widgets.push_back(make_check(std::move(*gb), 602, 159, f.edit.music_on, Action::MusicToggle));
    win.widgets.push_back(make_check(std::move(*bb), 602, 232, f.edit.sound_on, Action::SoundToggle));
    const struct {
        const char* pic;
        int x, y;
        Action act;
        std::uint8_t key;
    } kButtons[] = {{"Cancel.bmp", 468, 422, Action::OptionsCancel, 27},
                    {"Apply.bmp", 109, 422, Action::OptionsOk, 13},
                    {"GameOptionsBtn.bmp", 94, 470, Action::GameOptions, 0}};
    for (const auto& b : kButtons) {
        auto pic = load_picture(f, *a, b.pic, true, error);
        if (!pic) return false;
        win.widgets.push_back(make_button(std::move(*pic), b.x, b.y, b.act, b.key));
    }
    f.stack.push_back(std::move(win));
    return true;
}

// A text object for a widget; every text drawn in the menus is made here or in make_text, where the marker check of
// docs/languages.md 0 sees it (the caller is the place it names). make_text_raw: a text the player typed (no check).
Text make_text_object(Frontend& f, const std::string& s, SDL_Color c,
                      const std::source_location& where = std::source_location::current());
Text make_text_raw(Frontend& f, const std::string& s, SDL_Color c);
std::string lookup(const Frontend& f, const std::string& key);
const Language& language(const Frontend& f);
// A name or other data shown in a text (a player's or a profile's name, a map's or a save's file name, an address):
// it stays as it is in every language, and the marker check of `xx` leaves it out (ui/lang.hpp lang_data).
const std::string& data(Frontend& f, const std::string& text);

// ---- text that fits its place (ours, docs/languages.md 2.5, slice L2b) ----

// Times New Roman Bold at `em` pixels: the original's two sizes, else opened once from the same file.
TTF_Font* font_em(Frontend& f, int em) {
    if (static_cast<int>(TTF_GetFontSize(f.font18)) == em) return f.font18;
    if (static_cast<int>(TTF_GetFontSize(f.font16)) == em) return f.font16;
    auto [it, added] = f.fit_fonts.try_emplace(em, nullptr);
    if (added) it->second = no_kerning(TTF_OpenFont(f.font_path.c_str(), static_cast<float>(em)));
    return it->second ? it->second : f.font18;
}

// `text` fitted into `place` (ui/text_fit.hpp). A text that fits as it is is not measured further.
TextFit fit_text(Frontend& f, const TextPlace& p, const std::string& text) {
    int w = 0;
    if (text.find('\n') == std::string::npos && TTF_GetStringSize(font_em(f, p.em), text.c_str(), 0, &w, nullptr) &&
        w <= p.width) {
        return TextFit{p.em, {text}, false, false};
    }
    return text_fit(p, text, lang_short(language(f), text), [&f](int em, std::string_view s) {
        int sw = 0;
        TTF_GetStringSize(font_em(f, em), s.data(), s.size(), &sw, nullptr);
        return sw;
    });
}

// ---- painted words (ours, docs/languages.md 3, slices L4 and L4b) ----

std::string g_display_font;  // frontend_set_display_font: a font file in place of the built-in display font

// The display font at `em`: the built-in OFL font (or the file of frontend_set_display_font), the menu font at that em
// behind it for the letters it lacks (Cyrillic); the menu font alone when it cannot be opened.
TTF_Font* display_em(Frontend& f, int em) {
    auto [it, added] = f.display_fonts.try_emplace(em, nullptr);
    if (added) {
        SDL_IOStream* io = g_display_font.empty() ? SDL_IOFromConstMem(display_font_data(), display_font_size())
                                                  : SDL_IOFromFile(g_display_font.c_str(), "rb");
        it->second = io ? TTF_OpenFontIO(io, true, static_cast<float>(em)) : nullptr;
        if (!it->second) SDL_Log("display font: %s", SDL_GetError());
        if (it->second) TTF_AddFallbackFont(it->second, font_em(f, em));
    }
    return it->second ? it->second : font_em(f, em);
}

// A line drawn white into a coverage mask for ui/painted.
Coverage paint_raster(Frontend& f, PaintFont font, int em, std::string_view text) {
    return text_coverage(font == PaintFont::Display ? display_em(f, em) : font_em(f, em), text);
}

// A picture with painted English words (ui/painted.hpp) shows the display language's text instead. English, and a word
// whose translation is the English text, keep the original picture.
void paint_words(Frontend& f, const Archive& a, const std::string& name, Image& img) {
    if (!f.lang) return;
    for (const PaintedWord* w : painted_find(a.path, name)) {
        const std::string text = lang_text(*f.lang, w->key);
        if (text == lang_text(lang_english(), w->key)) continue;
        check_drawn(f.lang, text, std::source_location::current());
        const Image* under = nullptr;
        if (w->under) {
            const std::string key = a.path + "/" + w->under;
            if (f.under_name != key) {
                std::string err;
                const LesEntry* e = find_entry(a.table, w->under);
                auto bytes = e ? read_entry(a.data, *e, &err) : std::nullopt;
                auto pic = bytes ? decode_bmp(*bytes, &err) : std::nullopt;
                f.under_name = pic ? key : std::string();
                f.under = pic ? std::move(*pic) : Image{};
            }
            if (!f.under_name.empty()) under = &f.under;
        }
        painted_apply(img, *w, under, text,
                      [&f](PaintFont font, int em, std::string_view s) { return paint_raster(f, font, em, s); });
    }
}

// Ours: the resolution picker's Full screen item, in every language (English too).
void paint_full_screen(Frontend& f, Image& img) {
    const std::string text = ours(f, painted_full_screen().key);
    check_drawn(f.lang, text, std::source_location::current());
    painted_full_screen_blank(img);
    painted_draw(img, painted_full_screen(), text,
                 [&f](PaintFont font, int em, std::string_view s) { return paint_raster(f, font, em, s); });
}

// How much lower a line of a smaller font is drawn so that its baseline stays where the original font's is.
int baseline_dy(TTF_Font* original, TTF_Font* smaller) {
    return TTF_GetFontAscent(original) - TTF_GetFontAscent(smaller);
}

// A label (its `label` already made into `text` in Times 18) fitted into `place`: as it is when it fits (English);
// else smaller, on two lines, shortened or cut with an ellipsis and the full text as its tooltip.
void fit_label(Frontend& f, Widget& w, const char* place) {
    if (!w.text || w.label.empty()) return;
    const TextPlace& p = text_place(place);
    const TextFit r = fit_text(f, p, w.label);
    if (r.as_is(p)) return;
    Uint8 cr = 0, cg = 0, cb = 0, ca = 0;
    TTF_GetTextColor(w.text.get(), &cr, &cg, &cb, &ca);
    TTF_Font* font = font_em(f, r.em);
    auto make = [&](const std::string& s) {
        Text t(TTF_CreateText(f.engine, font, s.c_str(), 0));
        if (t) TTF_SetTextColor(t.get(), cr, cg, cb, ca);
        return t;
    };
    w.text = make(r.lines[0]);
    if (r.lines.size() > 1) {  // the two lines centred on the original line
        w.text2 = make(r.lines[1]);
        w.text2_dy = TTF_GetFontLineSkip(font);
        w.text_dy = kCell18 / 2 - w.text2_dy;
    } else {
        w.text_dy = baseline_dy(f.font18, font);
    }
    if (r.ellipsis || r.shortened) {
        w.tip = w.label;
        if (!w.centred) w.w = p.width, w.h = kCell18;  // the area the tooltip shows for
    }
}

// A label's text replaced (the marker check's place is the caller) and fitted into `place`.
void relabel(Frontend& f, Widget& w, const std::string& text, SDL_Color c, const char* place,
             const std::source_location& where = std::source_location::current()) {
    w.label = text;
    w.text = make_text_object(f, text, c, where);
    w.text2.reset();
    w.text_dy = w.text2_dy = 0;
    w.tip.clear();
    fit_label(f, w, place);
}

// The rows of a box of text (`rows`, labels from top to bottom): the paragraphs (an empty one is left out) wrapped
// into `place` (its width and as many rows as it has), smaller when they need more rows. English as before: a row per
// paragraph. The marker check sees the paragraphs whole (a wrapped piece of a name is not a name it knows).
void fit_rows(Frontend& f, const std::vector<Widget*>& rows, const char* place,
              const std::vector<std::string>& paragraphs, SDL_Color c,
              const std::source_location& where = std::source_location::current()) {
    std::string joined;
    for (const std::string& para : paragraphs) {
        if (para.empty()) continue;
        check_drawn(f.lang, para, where);
        joined += (joined.empty() ? "" : "\n") + para;
    }
    TextPlace p = text_place(place);
    p.lines = static_cast<int>(rows.size());
    const TextFit r = joined.empty() ? TextFit{p.em, {}, false, false} : fit_text(f, p, joined);
    TTF_Font* font = font_em(f, r.em);
    for (std::size_t k = 0; k < rows.size(); ++k) {
        Widget& w = *rows[k];
        w.label = k < r.lines.size() ? r.lines[k] : std::string();
        w.text.reset(TTF_CreateText(f.engine, font, w.label.c_str(), 0));
        if (w.text) TTF_SetTextColor(w.text.get(), c.r, c.g, c.b, c.a);
        w.text_dy = baseline_dy(f.font18, font);
        w.tip = (r.ellipsis || r.shortened) && k + 1 == r.lines.size() ? joined : std::string();
        if (!w.tip.empty()) w.w = p.width, w.h = kCell18;
    }
}

// An item of a box or a list (made by make_text_object in `base`) fitted into one line of `place`: smaller, shortened
// or cut; `dy` gets the shift that keeps its baseline, `tip` the full text when it was cut or shortened.
Text fit_item(Frontend& f, const char* place, const std::string& s, Text made, TTF_Font* base, int* dy,
              std::string* tip) {
    *dy = 0;
    if (!made || s.empty()) return made;
    TextPlace p = text_place(place);
    p.lines = 1;
    const TextFit r = fit_text(f, p, s);
    if (r.as_is(p)) return made;
    Uint8 cr = 0, cg = 0, cb = 0, ca = 0;
    TTF_GetTextColor(made.get(), &cr, &cg, &cb, &ca);
    TTF_Font* font = font_em(f, r.em);
    Text t(TTF_CreateText(f.engine, font, r.lines[0].c_str(), 0));
    if (t) TTF_SetTextColor(t.get(), cr, cg, cb, ca);
    *dy = baseline_dy(base, font);
    if (r.ellipsis || r.shortened) *tip = s;
    return t;
}

// Every item of a box or a list fitted into `place` (item_dy and item_tips follow item_text).
void fit_items(Frontend& f, Widget& w, const char* place, TTF_Font* base) {
    w.item_dy.assign(w.item_text.size(), 0);
    w.item_tips.assign(w.item_text.size(), std::string());
    for (std::size_t k = 0; k < w.item_text.size() && k < w.strs.size(); ++k) {
        w.item_text[k] = fit_item(f, place, w.strs[k], std::move(w.item_text[k]), base, &w.item_dy[k], &w.item_tips[k]);
    }
}

int item_dy(const Widget& w, int k) {
    return k >= 0 && k < static_cast<int>(w.item_dy.size()) ? w.item_dy[static_cast<std::size_t>(k)] : 0;
}

bool g_update_link = false;       // frontend_set_update_link
bool g_update_corner = false;     // frontend_set_update_link's corner_link
constexpr int kUpdateRowY = 606;  // its row in Game options
constexpr int kSimThreadY = 334;  // ours: the sim thread's row (docs/sim-thread.md), above the interface rows
constexpr int kLanguageY = 600;   // ours: the language's row (docs/languages.md 4), right column, last

// Ours (PLAN.md 2026-10-15, docs/interface.md I1): a row of the interface settings under the original's three check
// boxes of Game options: our label at (60, y + 6) and a field at (300, y) that shows the value and, pressed, the next.
bool add_choice(Frontend& f, Window& win, const LesTable* table, const Bytes& data, const std::string& label, int y,
                std::vector<std::string> values, int sel, std::string* error,
                const std::source_location& where = std::source_location::current());

// Ours: the minutes the network rows of Game options offer (0: the whole game). A value of the settings file that is
// not among them (written by hand or with another list) is offered too, in its place, so Ok keeps it.
const std::vector<int> kNetAutosave = {1, 2, 3, 5, 10, 15};
const std::vector<int> kNetSeatHold = {1, 2, 5, 10, 15, 30, 0};
constexpr int kNetAutosaveY = 504, kNetSeatHoldY = 538, kNetAiY = 572;  // their rows, found by these at Ok
std::vector<int> net_minutes_offered(const std::vector<int>& offer, int v) {
    std::vector<int> out = offer;
    if (std::find(out.begin(), out.end(), v) == out.end())
        out.insert(std::find_if(out.begin(), out.end(), [v](int o) { return o == 0 || o > v; }), v);
    return out;
}





// its list 192 high: ComboBox.bmp in three parts, ScrollerBack.bmp and ScrollerUp.bmp (all magenta) as its scroll
// column (Scroller.bmp, the thumb, is not in the archive), the six IDS_GOP_WAR_TPL_* in Times New Roman Bold 16

bool open_game_options(Frontend& f, std::string* error) {
    auto a = open_archive(f, "Data/Interface/Menu/GameOptions.les", error);
    if (!a) return false;
    const Settings& s = f.settings;
    Window win;
    win.screen = Screen::GameOptions;
    auto back = load_picture(f, *a, "Back.bmp", false, error);
    if (!back) return false;
    win.back = std::move(*back);
    win.w = 1024, win.h = 768;
    win.x = (f.canvas_w - 1024) / 2, win.y = (f.canvas_h - 768) / 2;
    auto pic = [&](const char* name) { return load_picture(f, *a, name, true, error); };
    auto add_check = [&](int x, int y, bool value) {
        auto p = pic("CheckBox.bmp");
        if (!p) return false;
        win.widgets.push_back(make_check(std::move(*p), x, y, value, Action::None));
        return true;
    };
    auto apply = pic("Apply.bmp"), cancel = pic("Cancel.bmp");
    if (!apply || !cancel) return false;
    win.widgets.push_back(make_button(std::move(*apply), 313, 691, Action::GameOptionsOk, 13));
    win.widgets.push_back(make_button(std::move(*cancel), 517, 691, Action::GameOptionsCancel, 27));
    const std::uint8_t scrolls[] = {s.mouse_scroll, s.keyboard_scroll};
    const char* const kScrollKeys[] = {"IDS_GOP_MOUSE_SCROLLING", "IDS_GOP_KBD_SCROLLING"};
    for (int i = 0; i < 2; ++i) {
        win.widgets.push_back(make_label(f, kScrollKeys[i], 60, 146 + 40 * i));
        auto track = pic("SliderBack.bmp"), knob = pic("Slider.bmp");
        if (!track || !knob) return false;
        win.widgets.push_back(make_slider(std::move(*track), std::move(*knob), 300, 140 + 40 * i, 256, 32,
                                          volume_to_slider(scrolls[i]), Action::None));
    }
    win.widgets.push_back(make_label(f, "IDS_GOP_LANDSCAPE_FILTER", 60, 240));
    if (!add_check(300, 234, s.landscape_filter)) return false;
    const GameAdvisors* game = f.game_advisors ? &*f.game_advisors : nullptr;
    if (!game) {
        win.widgets.push_back(make_label(f, "IDS_GOP_POOR_ANIMATION", 60, 274));
        if (!add_check(300, 268, s.short_anims)) return false;
    }
    win.widgets.push_back(make_label(f, "IDS_GOP_REFLECTIONS", 60, 308));
    if (!add_check(300, 301, s.reflections)) return false;
    Widget head = make_label(f, "IDS_GOP_ECONOM_ADV", 560, 138);
    head.w = 420, head.centred = true;
    win.widgets.push_back(std::move(head));
    for (int k = 0; k < 6; ++k) {
        win.widgets.push_back(make_label(f, "IDS_GOP_ECONOM_" + std::to_string(k + 1), 600, 172 + 34 * k));
        if (!add_check(925, 165 + 34 * k, game ? game->economic[k] != 0 : s.economic[k])) return false;
    }
    head = make_label(f, "IDS_GOP_WAR_ADV", 560, 400);
    head.w = 420, head.centred = true;
    win.widgets.push_back(std::move(head));
    if (game) win.widgets.push_back(make_label(f, "IDS_GOP_WAR_5", 600, 434));
    for (int k = 0; k < 4; ++k) {
        win.widgets.push_back(make_label(f, "IDS_GOP_WAR_" + std::to_string(k + 1), 600, 468 + 34 * k));
        if (!add_check(925, 461 + 34 * k, game ? game->military[k] != 0 : s.military[k])) return false;
    }
    if (game) {
        Widget c;
        c.kind = Kind::MuseumCombo;
        c.x = 745, c.y = 428, c.w = 212, c.h = 32;
        c.row_h = 32, c.list_h = 192, c.ox = 10, c.oy = 8;
        auto field = pic("ComboBox.bmp"), bar = pic("ScrollerBack.bmp"), up = pic("ScrollerUp.bmp"),
             down = pic("ScrollerUp.bmp");
        if (!field || !bar || !up || !down) return false;
        c.pic = std::move(*field), c.bar = std::move(*bar), c.up = std::move(*up), c.down = std::move(*down);
        c.thumb_on = false;
        for (int k = 1; k <= 6; ++k) {
            c.strs.push_back(lookup(f, "IDS_GOP_WAR_TPL_" + std::to_string(k)));
            c.item_text.push_back(make_text_object(f, c.strs.back(), SDL_Color{0xff, 0xf7, 0x99, 0xff}));
            if (c.item_text.back()) TTF_SetTextFont(c.item_text.back().get(), f.font16);
        }
        fit_items(f, c, "options.conduct_value", f.font16);
        c.sel = std::min<int>(game->conduct, 5);
        win.widgets.push_back(std::move(c));
    }
    for (Widget& w : win.widgets) {
        if (w.kind != Kind::Label) continue;
        make_text(f, w, SDL_Color{0xff, 0xf7, 0x99, 0xff});
        fit_label(f, w,
                  w.centred    ? "options.heading"
                  : w.x == 60  ? "options.label"
                  : w.y == 434 ? "options.conduct"
                               : "options.advisor");
    }
    // Ours: the interface settings (the original's screen has room under its three check boxes).
    std::vector<std::string> scales = {ours(f, "options.auto")};
    for (int k = 1; k <= 4; ++k) scales.push_back(ours(f, "options.scale", {{"n", std::to_string(k)}}));
    if (!add_choice(f, win, &a->table, a->data, ours(f, "options.interface_scale"), 368, scales, s.ui_scale, error) ||
        !add_choice(f, win, &a->table, a->data, ours(f, "options.interface_pictures"), 402,
                    {ours(f, "options.pictures_between"), ours(f, "options.pictures_crisp"),
                     ours(f, "options.pictures_smooth")},
                    s.ui_pictures, error) ||
        !add_choice(f, win, &a->table, a->data, ours(f, "options.world_zoom"), 436, scales, s.world_zoom, error)) {
        return false;
    }
#ifndef __EMSCRIPTEN__
    // Ours: the host's network game settings (docs/multiplayer.md "The network settings page"), from the menu only
    // (a running game's server already has its own) and not in the browser (no network game there).
    if (!game) {
        head = make_plain_label(f, ours(f, "options.network_heading"), 60, 478, SDL_Color{0xff, 0xf7, 0x99, 0xff});
        head.w = 420, head.centred = true;
        fit_label(f, head, "options.heading");
        win.widgets.push_back(std::move(head));
        auto texts = [&f](const std::vector<int>& minutes) {
            std::vector<std::string> out;
            for (int m : minutes)
                out.push_back(m == 0 ? ours(f, "options.whole_game") : lang_plural(language(f), "options.minutes", m));
            return out;
        };
        auto index = [](const std::vector<int>& minutes, int v) {
            return static_cast<int>(std::find(minutes.begin(), minutes.end(), v) - minutes.begin());
        };
        const std::vector<int> autosave = net_minutes_offered(kNetAutosave, s.net_autosave),
                               hold = net_minutes_offered(kNetSeatHold, s.net_seat_hold);
        if (!add_choice(f, win, &a->table, a->data, ours(f, "options.autosave_every"), kNetAutosaveY, texts(autosave),
                        index(autosave, s.net_autosave), error) ||
            !add_choice(f, win, &a->table, a->data, ours(f, "options.seat_kept"), kNetSeatHoldY, texts(hold),
                        index(hold, s.net_seat_hold), error) ||
            !add_choice(
                f, win, &a->table, a->data, ours(f, "options.computer_takes_over"), kNetAiY,
                {ours(f, "options.nobody"), lookup(f, "MP_LEVEL1"), lookup(f, "MP_LEVEL2"), lookup(f, "MP_LEVEL3")},
                s.net_ai_level, error)) {
            return false;
        }
    }
#endif
    // Ours (docs/download.md "The updater"): from the menu, where the updater is built in; the last row.
    if (!game && g_update_link &&
        !add_choice(f, win, &a->table, a->data, ours(f, "options.check_updates"), kUpdateRowY,
                    {ours(f, "options.on"), ours(f, "options.off")}, s.check_updates ? 0 : 1, error)) {
        return false;
    }
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
    // Ours (docs/sim-thread.md): the simulation on a thread of its own, from the next game or load on (settings.hpp
    // sim_thread: 0 off, 1 on, 2 auto). Not on the one-thread web page, which has no second thread.
    if (!add_choice(f, win, &a->table, a->data, ours(f, "options.simulation_thread"), kSimThreadY,
                    {ours(f, "options.off"), ours(f, "options.on"), ours(f, "options.auto")}, s.sim_thread, error)) {
        return false;
    }
#endif
    // Ours (docs/languages.md 4): the display language, the languages found by their own names; the same row on the
    // desktop and in the browser, from the menu and in a game: in the right column under the military advisor, the one
    // place free in every variant (label at x 600, the field at 765). Ok stores a changed choice (Settings::language).
    const Language& l = language(f);
    std::vector<LangInfo> offered = l.offered.empty() ? std::vector<LangInfo>{l.info} : l.offered;
    if (std::none_of(offered.begin(), offered.end(), [&](const LangInfo& o) { return o.code == l.info.code; }))
        offered.push_back(l.info);  // `xx`, or --lang with a folder that is not offered
    std::vector<std::string> names;
    f.lang_codes.clear();
    int lang_sel = 0;
    for (const LangInfo& o : offered) {
        if (o.code == l.info.code) lang_sel = static_cast<int>(names.size());
        names.push_back(data(f, o.name));  // each language by its own name, in every language
        f.lang_codes.push_back(o.code);
    }
    if (!add_choice(f, win, &a->table, a->data, ours(f, "options.language"), kLanguageY, names, lang_sel, error))
        return false;
    win.widgets[win.widgets.size() - 2].x = 600, win.widgets.back().x = 765;
    Widget& lang_label = win.widgets[win.widgets.size() - 2];  // its room there is narrower: fitted again
    lang_label.text = make_text_object(f, lang_label.label, SDL_Color{0xff, 0xf7, 0x99, 0xff});
    lang_label.text2.reset();
    lang_label.text_dy = lang_label.text2_dy = 0;
    lang_label.tip.clear();
    fit_label(f, lang_label, "options.language");
    f.stack.push_back(std::move(win));
    return true;
}







std::vector<std::string> wrap_text(TTF_Font* font, const std::string& text, int width, bool keep_space) {
    std::vector<std::string> lines;
    std::string rest = text;
    while (!rest.empty()) {
        std::size_t fit = rest.size();
        int w = 0;
        while (fit > 0 && TTF_GetStringSize(font, rest.c_str(), fit, &w, nullptr) && w > width) {
            const std::size_t space = rest.rfind(' ', fit - 1);
            if (space != std::string::npos && space != 0) {
                fit = space;
            } else {  // one word wider than the line: one letter less, never half of a UTF-8 letter (B314)
                --fit;
                while (fit > 0 && (static_cast<unsigned char>(rest[fit]) & 0xc0) == 0x80) --fit;
            }
        }
        if (fit == 0) {  // not even one letter fits: the first letter alone
            fit = 1;
            while (fit < rest.size() && (static_cast<unsigned char>(rest[fit]) & 0xc0) == 0x80) ++fit;
        }
        lines.push_back(rest.substr(0, fit));
        rest = rest.substr(!keep_space && fit < rest.size() && rest[fit] == ' ' ? fit + 1 : fit);
    }
    return lines;
}

bool open_message(Frontend& f, const std::string& message, std::string* error) {
    auto a = open_archive(f, "Data/Interface/WindowMessage.les", error);
    if (!a) return false;
    Window win;
    win.screen = Screen::Message;
    auto back = load_picture(f, *a, "Window_message.bmp", false, error);
    auto close = load_picture(f, *a, "Window_close.bmp", true, error);
    if (!back || !close) return false;
    win.back = std::move(*back);
    win.w = 500, win.h = 200;
    win.x = (f.canvas_w - win.w) / 2, win.y = (f.canvas_h - win.h) / 2;
    win.drag_h = kDragStripMessage;
    Widget b = make_button(std::move(*close), 466, 16, Action::MessageClose, 13);
    b.w = 18, b.h = 13;
    win.widgets.push_back(std::move(b));
    // Ours (docs/languages.md 2.4 rule 1): the lines go down to the window's bottom edge (9 of them; every English
    // message of the game needs 5 at most). A longer text is drawn smaller, down to em 12 (lines 12 apart), and what is
    // left after that ends in an ellipsis and goes to the log.
    constexpr int kBottom = 196;
    int em = static_cast<int>(TTF_GetFontSize(f.font18)), pitch = 15;
    std::vector<std::string> lines = wrap_text(f.font18, message, 450, false);
    while (57 + pitch * (static_cast<int>(lines.size()) - 1) + kCell18 > kBottom && em > 12) {
        --em, pitch = em;
        lines = wrap_text(font_em(f, em), message, 450, false);
    }
    TTF_Font* font = font_em(f, em);
    const int room = (kBottom - 57 - TTF_GetFontHeight(font)) / pitch + 1;
    if (static_cast<int>(lines.size()) > room) {
        SDL_Log("front-end: a message too long for its window, cut: %s", message.c_str());
        lines.resize(static_cast<std::size_t>(room));
        lines.back() += "\xe2\x80\xa6";
    }
    int line = 0;
    for (const std::string& text : lines) {
        Widget l;
        l.kind = Kind::Label;
        l.x = 22, l.y = 57 + pitch * line++, l.h = 18;
        l.label = text;
        if (em == static_cast<int>(TTF_GetFontSize(f.font18))) {
            make_text(f, l, SDL_Color{0, 0, 0, 0xff});
        } else {
            check_drawn(f.lang, text);
            l.text.reset(TTF_CreateText(f.engine, font, text.c_str(), 0));
            if (l.text) TTF_SetTextColor(l.text.get(), 0, 0, 0, 0xff);
            l.text_dy = baseline_dy(f.font18, font);
        }
        win.widgets.push_back(std::move(l));
    }
    f.stack.push_back(std::move(win));
    return true;
}

// ---- the in-game menu (frontend.md 4.12, 4.13) ----





bool open_game_menu(Frontend& f, std::string* error) {
    if (f.menu.size() < 5) {
        *error = "Menu.txt has no window 4";
        return false;
    }
    const MenuWindow& mw = f.menu[4];
    auto a = open_archive(f, menu_archive_path(mw.archive), error);
    if (!a) return false;
    Window win;
    win.screen = Screen::GameMenu;
    win.w = mw.w, win.h = mw.h;
    win.x = (f.canvas_w - win.w) / 2, win.y = (f.canvas_h - win.h) / 2;
    win.drag_h = kDragStripMessage;  // MakeMenu sets it for window 4 only
    auto back = load_picture(f, *a, mw.background, false, error);
    if (!back) return false;
    win.back = std::move(*back);
    for (const MenuElement& e : mw.elements) {
        if (e.type != 1) continue;
        auto pic = load_picture(f, *a, e.file, true, error);
        if (!pic) return false;
        const GameMenuItem item = game_menu_item(e.handler);
        if (item == GameMenuItem::None)
            SDL_Log("Menu.txt window 4: handler %u is not an in-game menu action", e.handler);
        Widget b = make_button(std::move(*pic), e.x, e.y, Action::GameMenu, e.hotkey);
        b.w = e.w, b.h = e.h;
        b.row = static_cast<int>(item);
        b.dimmed = !game_menu_unavailable(language(f), item, f.can_restart).empty();
        win.widgets.push_back(std::move(b));
    }
    f.stack.clear();
    f.stack.push_back(std::move(win));
    return true;
}





bool open_confirm(Frontend& f, GameMenuItem item, std::string* error) {
    auto a = open_archive(f, "Data/Interface/Confirm.les", error);
    if (!a) return false;
    Window win;
    win.screen = Screen::Confirm;
    auto back = load_picture(f, *a, "back.bmp", false, error);
    auto yes = load_picture(f, *a, "yes.bmp", true, error);
    auto no = load_picture(f, *a, "no.bmp", true, error);
    if (!back || !yes || !no) return false;
    win.back = std::move(*back);
    win.w = win.back.w, win.h = win.back.h;
    win.x = (f.canvas_w - win.w) / 2, win.y = (f.canvas_h - win.h) / 2;
    win.widgets.push_back(make_button(std::move(*yes), 29, 187, Action::ConfirmYes, 13));
    win.widgets.push_back(make_button(std::move(*no), 230, 187, Action::ConfirmNo, 27));
    std::string question = lookup(f, game_menu_confirm_key(item));
    // Ours: the English string table asks "Are you sure you want to ad QuickSave?" (a letter lost); it says "load".
    if (const std::size_t at = question.find(" ad QuickSave");
        item == GameMenuItem::QuickLoad && at != std::string::npos)
        question.replace(at, 13, " load QuickSave");
    // Ours (docs/languages.md 2.4 rule 1): the original drops the lines after the text box's sixth (106 high); here
    // the window grows by the lines that do not fit, its buttons move down with it.
    const std::vector<std::string> lines = wrap_text(f.font18, question, 330, true);
    const int extra = std::max(0, 17 * (static_cast<int>(lines.size()) - 1) + kCell18 - 106);
    if (extra > 0) {
        win.grow_at = 55 + 106, win.grow = extra;
        win.h += extra;
        win.y = (f.canvas_h - win.h) / 2;
        for (Widget& b : win.widgets) b.y += extra;  // Yes and No
    }
    int line = 0;
    for (const std::string& text : lines) {
        Widget l;
        l.kind = Kind::Label;
        l.x = 58, l.y = 55 + 17 * line++, l.h = 18;
        l.label = text;
        make_text(f, l, SDL_Color{0x1e, 0x1e, 0x1e, 0xff});
        win.widgets.push_back(std::move(l));
    }
    f.confirm_item = item;
    f.stack.push_back(std::move(win));
    return true;
}

// ---- the in-game windows of slice I8 (docs/interface.md C.4, C.6, C.7) ----



void place_window(const Frontend& f, Window& win) {
    if (win.screen == Screen::Mission) {
        win.x = (f.canvas_w - 500) / 2, win.y = f.canvas_h / 2 - 100;
    } else if (win.screen == Screen::Hints) {
        win.x = 50, win.y = 50;
    } else {
        win.x = (f.canvas_w - win.w) / 2, win.y = (f.canvas_h - win.h) / 2;
    }
}





Widget make_textbox(Frontend& f, int x, int y, int w, int h, std::array<int, 4> margins, std::array<int, 4> up,
                    std::array<int, 4> down, Picture thumb, const std::vector<std::string>& paragraphs,
                    SDL_Color colour) {
    Widget b;
    b.kind = Kind::TextBox;
    b.x = x, b.y = y, b.w = w, b.h = h;
    b.ox = margins[0], b.oy = margins[1], b.tb_right = margins[2], b.tb_bottom = margins[3];
    b.tb_up = up, b.tb_down = down;
    b.knob = std::move(thumb);
    std::map<std::string, int> widths;  // GetTextExtentPoint32W of one character
    auto char_width = [&](const std::string& c) {
        auto [it, added] = widths.try_emplace(c, 0);
        if (added && c != "\n") TTF_GetStringSize(f.font18, c.c_str(), c.size(), &it->second, nullptr);
        return it->second;
    };
    for (const std::string& para : paragraphs) {
        for (std::string& line : textbox_wrap(para, b.tb_right - b.ox, char_width)) b.strs.push_back(std::move(line));
    }
    for (const std::string& line : b.strs) b.item_text.push_back(make_text_object(f, line, colour));
    return b;
}





bool open_mission(Frontend& f, const std::string& title, const std::vector<std::string>& paragraphs,
                  std::string* error) {
    auto a = open_archive(f, "Data/Interface/InfoMission.les", error);
    if (!a) return false;
    auto g = open_archive(f, "Data/Interface/InterfaceGroup.les", error);
    if (!g) return false;
    auto back = load_picture(f, *a, "Info_mission.bmp", false, error);
    auto close = load_picture(f, *a, "zadclose.bmp", true, error);
    auto thumb = load_picture(f, *g, "sp_dvizoklist.bmp", true, error);
    if (!back || !close || !thumb) return false;
    Window win;
    win.screen = Screen::Mission;
    win.back = std::move(*back);
    win.w = 454, win.h = 329;
    place_window(f, win);
    win.drag_h = kDragStripMission;
    Widget x = make_button(std::move(*close), 422, 12, Action::MissionClose, 27);
    x.w = 25, x.h = 26;
    win.widgets.push_back(std::move(x));
    Widget t = make_plain_label(f, title, 140, 17, SDL_Color{0xff, 0xff, 0xff, 0xff});
    t.shadow = false;
    fit_label(f, t, "mission.title");
    win.widgets.push_back(std::move(t));
    win.widgets.push_back(make_textbox(f, 37, 75, 409, 216, {5, 0, 352, 207}, {386, 0, 408, 11}, {388, 207, 408, 217},
                                       std::move(*thumb), paragraphs, SDL_Color{0, 0, 0, 0xff}));
    f.stack.clear();
    f.stack.push_back(std::move(win));
    return true;
}






bool open_hints(Frontend& f, std::string* error) {
    auto a = open_archive(f, "Data/Interface/Hints.les", error);
    if (!a) return false;
    auto back = load_picture(f, *a, "Back.bmp", false, error);
    auto startup = load_picture(f, *a, "Startup2.bmp", true, error);
    auto close = load_picture(f, *a, "Close.bmp", true, error);
    auto close_x = load_picture(f, *a, "CloseUp.bmp", true, error);
    auto prev = load_picture(f, *a, "Prev.bmp", true, error);
    auto next = load_picture(f, *a, "Next.bmp", true, error);
    auto thumb = load_picture(f, *a, "sp_dvizoklist.bmp", true, error);
    if (!back || !startup || !close || !close_x || !prev || !next || !thumb) return false;
    Window win;
    win.screen = Screen::Hints;
    win.back = std::move(*back);
    win.w = 454, win.h = 340;
    place_window(f, win);
    win.widgets.push_back(make_check(std::move(*startup), 44, 268, f.hints_startup, Action::HintsStartup));
    win.widgets.push_back(make_button(std::move(*close), 6, 300, Action::HintsClose, 13));
    win.widgets.push_back(make_button(std::move(*close_x), 427, 5, Action::HintsClose, 27));
    win.widgets.push_back(make_button(std::move(*prev), 154, 300, Action::HintsPrev, 0));
    win.widgets.push_back(make_button(std::move(*next), 303, 300, Action::HintsNext, 0));
    const int count = static_cast<int>(f.hints.size()) + 1;
    f.hint_index = std::clamp(f.hint_index, 0, count - 1);
    const SDL_Color cream{0xf6, 0xe4, 0xc8, 0xff};
    Widget n = make_plain_label(f, std::to_string(f.hint_index + 1), 150, 5, cream);
    Widget c = make_plain_label(f, std::to_string(count), 350, 5, cream);
    n.shadow = c.shadow = false;
    win.widgets.push_back(std::move(n));
    win.widgets.push_back(std::move(c));
    const std::string text = f.hint_index < static_cast<int>(f.hints.size())
                                 ? f.hints[static_cast<std::size_t>(f.hint_index)]
                                 : keys_page(language(f));
    win.widgets.push_back(make_textbox(f, 0, 47, 449, 198, {25, 20, 404, 188}, {423, 0, 448, 14}, {423, 182, 448, 196},
                                       std::move(*thumb), {text}, SDL_Color{0x39, 0x26, 0x05, 0xff}));
    f.stack.clear();
    f.stack.push_back(std::move(win));
    return true;
}






bool open_diplomacy(Frontend& f, std::string* error) {
    auto a = open_archive(f, "Data/Interface/Menu/Diplomacy.les", error);
    if (!a) return false;
    auto back = load_picture(f, *a, "Back.bmp", false, error);
    auto cancel = load_picture(f, *a, "Cancel.bmp", true, error);
    auto ok = load_picture(f, *a, "Ok.bmp", true, error);
    if (!back || !cancel || !ok) return false;
    Window win;
    win.screen = Screen::Diplomacy;
    win.back = std::move(*back);
    win.w = win.back.w, win.h = win.back.h;
    place_window(f, win);
    win.widgets.push_back(make_button(std::move(*cancel), 146, 690, Action::DiplomacyCancel, 27));
    win.widgets.push_back(make_button(std::move(*ok), 351, 690, Action::DiplomacyOk, 13));
    const SDL_Color yellow{0xff, 0xf7, 0x99, 0xff};
    auto header = [&](const char* key, int x, int y, int width, const char* place) {
        Widget l = make_label(f, key, x, y);
        make_text(f, l, yellow);
        l.centred = true, l.w = width;
        fit_label(f, l, place);
        win.widgets.push_back(std::move(l));
    };
    header("IDS_DIPLOMACY_PLAYERNAME", 44, 248, 360, "diplomacy.player_head");
    header("IDS_DIPLOMACY_PLAERS_RELATION", 405, 240, 256, "diplomacy.relation_head");
    header("IDS_DIPLOMACY_OUR", 405, 253, 128, "diplomacy.side_head");
    header("IDS_DIPLOMACY_TO_US", 534, 253, 128, "diplomacy.side_head");
    for (const DiplomacyRow& r : f.diplomacy) {
        const int p = r.player;
        Widget l = make_plain_label(f, r.text, 44, 292 + 45 * p,
                                    SDL_Color{static_cast<Uint8>(r.rgb >> 16), static_cast<Uint8>(r.rgb >> 8),
                                              static_cast<Uint8>(r.rgb), 0xff});
        l.centred = true, l.w = 360;
        fit_label(f, l, "diplomacy.row");
        win.widgets.push_back(std::move(l));
        auto ours = load_picture(f, *a, "War_Peace.bmp", true, error);
        auto theirs = load_picture(f, *a, "War_Peace.bmp", true, error);
        if (!ours || !theirs) return false;
        Widget o = make_check(std::move(*ours), 445, 281 + 45 * p, r.our_war, Action::DiplomacyCheck);
        o.row = p;
        win.widgets.push_back(std::move(o));
        Widget t = make_check(std::move(*theirs), 574, 281 + 45 * p, r.their_war, Action::None);
        t.enabled = false;
        win.widgets.push_back(std::move(t));
    }
    f.stack.clear();
    f.stack.push_back(std::move(win));
    return true;
}

// ---- the museum (docs/frontend.md 4.16) ----

constexpr SDL_Color kMuseumInk{0xc8, 0xc8, 0xc8, 0xff};





Widget make_museum_combo(Frontend& f, const Archive& a, int y, Action action, Role role, std::string* error) {
    Widget c;
    c.kind = Kind::MuseumCombo;
    c.action = action, c.role = role;
    c.x = 659, c.y = y, c.w = 228, c.h = kMuseumRowH;
    c.row_h = kMuseumRowH, c.list_h = kMuseumListH, c.ox = 20, c.oy = 12;
    auto field = load_picture(f, a, "Combo_2.bmp", true, error);
    auto up = load_picture(f, a, "SliderDown.bmp", true, error);
    auto down = load_picture(f, a, "SliderUp.bmp", true, error);
    auto bar = load_picture(f, a, "palka.bmp", true, error);
    auto thumb = load_picture(f, a, "Scroller.bmp", true, error);
    if (!field || !up || !down || !bar || !thumb) {
        c.kind = Kind::Image;  // the caller stops on the error
        return c;
    }
    c.pic = std::move(*field), c.up = std::move(*up), c.down = std::move(*down), c.bar = std::move(*bar);
    c.knob = std::move(*thumb);
    c.thumb_y = y;
    return c;
}



void museum_combo_items(Frontend& f, Widget& c, const std::vector<std::string>& items) {
    c.strs = items;
    c.item_text.clear();
    for (const std::string& s : items) c.item_text.push_back(make_text_object(f, s, kMuseumInk));
    fit_items(f, c, "museum.box", f.font18);
    c.sel = items.empty() ? -1 : 0;
    c.top = 0;
}



void museum_combo_thumb(Widget& c) {
    const int count = static_cast<int>(c.strs.size());
    c.thumb_on = combo_thumb_shown(count, c.list_h, c.row_h);
    c.thumb_y = combo_thumb_y(c.top, count, c.list_h, c.row_h, c.y + c.row_h + c.up.h,
                              c.y + c.row_h + c.list_h - c.down.h - c.knob.h);
}

Widget* find_role(Window& win, Role role);  // below

std::vector<std::string> museum_names(const Frontend& f) {
    std::vector<std::string> names;
    for (int i : f.mus.list) names.push_back(f.museum.entries[static_cast<std::size_t>(i)].name);
    return names;
}





void museum_refresh(Frontend& f, Window& win, bool rebuild_list) {
    const MuseumEntry* entry = f.mus.entry >= 0 ? &f.museum.entries[static_cast<std::size_t>(f.mus.entry)] : nullptr;
    Widget* image = find_role(win, Role::MusPicture);
    if (image) {
        image->pic = Picture{};
        image->w = image->h = 0;
        if (entry && f.mus.picture < static_cast<int>(entry->pictures.size()) && f.mus_pictures) {
            std::string err;
            auto pic =
                load_picture(f, *f.mus_pictures, entry->pictures[static_cast<std::size_t>(f.mus.picture)], false, &err);
            if (pic) {
                image->w = pic->w, image->h = pic->h;
                image->pic = std::move(*pic);
            } else {
                SDL_Log("museum: %s", err.c_str());
            }
        }
    }
    const bool arrows = museum_has_arrows(f.museum, f.mus);
    for (Widget& w : win.widgets) {
        if (w.role == Role::Prev || w.role == Role::Next) w.visible = arrows;
    }
    if (Widget* box = find_role(win, Role::MusText)) {
        Picture thumb = std::move(box->knob);
        std::vector<std::string> paragraphs;
        if (entry) paragraphs.push_back(entry->text);
        Widget fresh = make_textbox(f, 45, 128, 500, 528, {32, 39, 459, 491}, {476, 37, 496, 59}, {476, 470, 496, 492},
                                    std::move(thumb), paragraphs, kMuseumInk);
        fresh.shadow = true;
        fresh.ink = kMuseumInk;
        fresh.role = Role::MusText;
        *box = std::move(fresh);
    }
    if (rebuild_list) {
        if (Widget* list = find_role(win, Role::MusList)) museum_combo_items(f, *list, museum_names(f));
    }
}



bool open_museum(Frontend& f, std::string* error) {
    auto mus = f.files("Data/museum/Museum.mus", error);
    if (!mus) return false;
    auto file = parse_museum_mus(*mus, error);
    if (!file) return false;
    auto tt = f.files("Data/TextTable_eng.tt", error);
    if (!tt) return false;
    auto table = parse_text_table(*tt, error);
    if (!table) {
        *error = "Data/TextTable_eng.tt: " + *error;
        return false;
    }
    lang_localize(language(f), *table);  // for display only (docs/languages.md 5.3)
    museum_apply_texts(*file, *table, f.strings);
    auto a = open_archive(f, "Data/museum/Museum.les", error);
    auto pictures = open_archive(f, "Data/museum/Pictures.les", error);
    if (!a || !pictures) return false;
    f.museum = std::move(*file);
    f.mus = MuseumState{};
    museum_filter(f.museum, f.mus);
    f.mus_pictures = std::move(pictures);
    Window win;
    win.screen = Screen::Museum;
    auto back = load_picture(f, *a, "Museum.bmp", false, error);
    auto back_button = load_picture(f, *a, "Back.bmp", true, error);
    auto next = load_picture(f, *a, "Next.bmp", true, error);
    auto prev = load_picture(f, *a, "Prev.bmp", true, error);
    auto thumb = load_picture(f, *a, "sp_dvizoklist.bmp", true, error);
    if (!back || !back_button || !next || !prev || !thumb) return false;
    win.back = std::move(*back);
    win.x = win.y = 0, win.w = 1024, win.h = 768;
    win.widgets.push_back(make_button(std::move(*back_button), 188, 678, Action::MuseumBack, 27));
    Widget n = make_button(std::move(*next), 778, 678, Action::MuseumNext, 0);
    n.role = Role::Next;
    Widget p = make_button(std::move(*prev), 604, 678, Action::MuseumPrev, 0);
    p.role = Role::Prev;
    win.widgets.push_back(std::move(n));
    win.widgets.push_back(std::move(p));
    const struct {
        int y;
        Action action;
        Role role;
    } kBoxes[] = {{184, Action::MuseumNation, Role::MusNation},
                  {227, Action::MuseumType, Role::MusType},
                  {272, Action::MuseumPick, Role::MusList}};
    for (const auto& b : kBoxes) {
        Widget c = make_museum_combo(f, *a, b.y, b.action, b.role, error);
        if (c.kind != Kind::MuseumCombo) return false;
        const std::vector<std::string>& items = b.role == Role::MusNation ? f.museum.nations
                                                : b.role == Role::MusType ? f.museum.types
                                                                          : museum_names(f);
        museum_combo_items(f, c, items);
        if (b.role != Role::MusList) c.sel = b.role == Role::MusNation ? f.mus.nation : f.mus.type;
        win.widgets.push_back(std::move(c));
    }
    Widget image;
    image.kind = Kind::Image;
    image.x = 625, image.y = 369;
    image.role = Role::MusPicture;
    win.widgets.push_back(std::move(image));
    Widget text;
    text.kind = Kind::TextBox;
    text.role = Role::MusText;
    text.knob = std::move(*thumb);
    win.widgets.push_back(std::move(text));
    museum_refresh(f, win, false);
    f.stack.push_back(std::move(win));
    return true;
}

// ---- single player: widgets of the original's set that FE1 did not need ----

constexpr SDL_Color kCream{0xf6, 0xe4, 0xc8, 0xff};
constexpr SDL_Color kOrange{0xff, 0xad, 0x0f, 0xff};

Text make_text_raw(Frontend& f, const std::string& s, SDL_Color c) {
    Text t(TTF_CreateText(f.engine, f.font18, s.c_str(), 0));
    if (t) TTF_SetTextColor(t.get(), c.r, c.g, c.b, c.a);
    return t;
}

Text make_text_object(Frontend& f, const std::string& s, SDL_Color c, const std::source_location& where) {
    check_drawn(f.lang, s, where);
    return make_text_raw(f, s, c);
}

std::string lookup(const Frontend& f, const std::string& key) {
    auto it = f.strings.find(key);
    return it != f.strings.end() ? it->second : key;
}

const Language& language(const Frontend& f) { return f.lang ? *f.lang : lang_english(); }

std::string ours(const Frontend& f, std::string_view key,
                 const std::vector<std::pair<std::string, std::string>>& args) {
    return lang_text(language(f), key, args);
}

const std::string& data(Frontend& f, const std::string& text) { return lang_data(f.lang, text); }


Widget make_plain_label(Frontend& f, const std::string& text, int x, int y, SDL_Color colour,
                        const std::source_location& where) {
    Widget w;
    w.kind = Kind::Label;
    w.x = x, w.y = y, w.h = 14;
    w.label = text;
    w.shadow = true;
    w.text = make_text_object(f, text, colour, where);
    return w;
}

bool add_choice(Frontend& f, Window& win, const LesTable* table, const Bytes& data, const std::string& label, int y,
                std::vector<std::string> values, int sel, std::string* error, const std::source_location& where) {
    Archive a;
    a.data = data, a.table = *table, a.path = "Data/Interface/Menu/GameOptions.les";
    auto field = load_picture(f, a, "SliderBack.bmp", true, error);
    if (!field) return false;
    win.widgets.push_back(make_plain_label(f, label, 60, y + 6, SDL_Color{0xff, 0xf7, 0x99, 0xff}, where));
    fit_label(f, win.widgets.back(), "options.row");
    Widget c;
    c.kind = Kind::Choice;
    c.x = 300, c.y = y, c.w = 180, c.h = 30;
    c.pic = std::move(*field);
    for (const std::string& v : values)
        c.item_text.push_back(make_text_object(f, v, SDL_Color{0xff, 0xf7, 0x99, 0xff}, where));
    c.strs = std::move(values);
    fit_items(f, c, "options.value", f.font18);
    c.sel = std::clamp(sel, 0, static_cast<int>(c.strs.size()) - 1);
    win.widgets.push_back(std::move(c));
    return true;
}


Widget make_edit(Frontend& f, int x, int y, int w, int h, int ox, int oy, std::size_t max_chars,
                 const std::string& text) {
    Widget e;
    e.kind = Kind::Edit;
    e.x = x, e.y = y, e.w = w, e.h = h;
    e.ox = ox, e.oy = oy;
    e.max_chars = max_chars;
    e.label = text;
    e.text = make_text_raw(f, text, e.ink);  // typed by the player
    return e;
}

void edit_set_text(Frontend& f, Widget& e, const std::string& text) {
    e.label = text;
    e.text = make_text_raw(f, text, e.ink);  // typed by the player
}

void edit_focus(Widget& e) {
    e.focused = true;
    e.focus_ms = SDL_GetTicks();
}




int list_visible_rows(const Widget& w) { return std::max(1, (w.h - w.list_oy - kCell18) / w.list_row + 1); }
int list_max_top(const Widget& w) { return std::max(0, static_cast<int>(w.strs.size()) - list_visible_rows(w)); }

void list_set_rows(Frontend& f, Widget& w, const std::vector<std::string>& rows, int selected) {
    w.strs = rows;
    w.item_text.clear();
    for (const std::string& r : rows) {
        w.item_text.push_back(make_text_object(f, r, kCream));
        if (w.font16 && w.item_text.back()) TTF_SetTextFont(w.item_text.back().get(), f.font16);
    }
    if (w.fit) fit_items(f, w, w.fit, w.font16 ? f.font16 : f.font18);
    w.sel = rows.empty() ? -1 : std::clamp(selected, 0, static_cast<int>(rows.size()) - 1);
    if (w.sel >= 0 && w.sel < w.top) w.top = w.sel;
    if (w.sel >= w.top + list_visible_rows(w)) w.top = w.sel - list_visible_rows(w) + 1;
    w.top = std::clamp(w.top, 0, list_max_top(w));
}


struct Rect {
    int x, y, w, h;
};
Rect list_up_rect(const Widget& w) { return {w.w - w.up.w / 3, w.list_up_y, w.up.w / 3, w.up.h}; }
Rect list_down_rect(const Widget& w) {
    const int bottom = w.list_bottom > 0 ? w.list_bottom : w.h;
    return {w.w - w.down.w / 3, bottom - w.down.h, w.down.w / 3, w.down.h};
}
bool in_rect(const Rect& r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }



void combo_close(Widget& w) {
    w.open = false;
    w.h = w.row_h;
}

// ---- the end of a game (docs/endgame.md 3 and 4) ----

constexpr SDL_Color kStatText{0xff, 0xf7, 0x99, 0xff};


Widget make_stat_label(Frontend& f, const std::string& text, int x, int y) {
    Widget w = make_plain_label(f, text, x, y, kStatText);
    w.w = 144;
    w.centred = true;
    fit_label(f, w, "stats.column");
    return w;
}

std::string stat_name(Frontend& f, const EndPlayer& p) {
    return p.name.empty() ? lookup(f, "IDS_COMP" + std::to_string(p.slot + 1)) : p.name;  // a name: data
}



void stat_fill(Frontend& f, Window& win) {
    if (f.stat_sel >= f.end_players.size()) return;
    const sim::PlayerStatistics& s = f.end_players[f.stat_sel].stats;
    static const char* const kNation[5] = {"MPS_RUSSIA_0", "MPS_ENGLAND_1", "MPS_FRANCE_2", "MPS_GERMANY_3",
                                           "MPS_AUSTRIA_4"};
    std::vector<std::string> v;
    for (std::size_t i = 0; i < 5; ++i) {
        for (const auto* row : {&s.extracted, &s.spent, &s.bought, &s.sold}) v.push_back(std::to_string((*row)[i]));
    }
    for (std::int32_t n :
         {s.units_total, s.units_lost, s.units_killed, s.houses_built, s.houses_lost, s.houses_destroyed}) {
        v.push_back(std::to_string(n));
    }
    v.push_back(lookup(f, kNation[s.nation % 5]));
    char t[32];
    std::snprintf(t, sizeof t, "%02u:%02u:%02u", s.duration / 3600, s.duration % 3600 / 60, s.duration % 60);
    v.push_back(t);
    v.push_back(std::to_string(s.points));
    std::size_t k = 0;
    for (Widget& w : win.widgets) {
        if (w.role != Role::Preview || k >= v.size()) continue;
        relabel(f, w, v[k++], kStatText, "stats.column");
    }
    for (Widget& w : win.widgets) {
        if (w.kind == Kind::CheckBox && w.action == Action::StatPlayer) w.value = w.row == static_cast<int>(f.stat_sel);
    }
}



bool open_statistics(Frontend& f, std::string* error) {
    auto a = open_archive(f, "Data/Interface/Statistics.les", error);
    if (!a) return false;
    Window win;
    win.screen = Screen::Statistics;
    auto back = load_picture(f, *a, "Back.bmp", false, error);
    auto close = load_picture(f, *a, "Close.bmp", true, error);
    if (!back || !close) return false;
    win.back = std::move(*back);
    win.x = 0, win.y = 0, win.w = kCanvasW, win.h = kCanvasH;
    Widget cb = make_button(std::move(*close), 404, 720, Action::StatClose, 0x1b);
    cb.w = 205, cb.h = 28;
    win.widgets.push_back(std::move(cb));
    static const char* const kRows[] = {"IDS_STAT_EXTRACTED_RESOURCES",
                                        "IDS_STAT_SPENDED_RESOURCES",
                                        "IDS_STAT_BUIED_RESOURCES",
                                        "IDS_STAT_SOLD_RESOURCES",
                                        "IDS_STAT_UNITS",
                                        "IDS_STAT_HOUSES",
                                        "IDS_STAT_NATION",
                                        "IDS_STAT_DURATION",
                                        "IDS_STAT_POINTS"};
    static const int kRowY[] = {172, 202, 232, 262, 346, 376, 460, 490, 520};
    for (std::size_t i = 0; i < std::size(kRows); ++i) {
        win.widgets.push_back(make_plain_label(f, lookup(f, kRows[i]), 55, kRowY[i], kStatText));
        fit_label(f, win.widgets.back(), "stats.row");
    }
    static const char* const kRes[] = {"IDS_GOLD", "IDS_FOOD", "IDS_WOOD", "IDS_IRON", "IDS_OIL"};
    for (int k = 0; k < 5; ++k) win.widgets.push_back(make_stat_label(f, lookup(f, kRes[k]), 255 + 144 * k, 142));
    static const char* const kCols[] = {"IDS_STAT_TOTAL", "IDS_STAT_LOSSES", "IDS_STAT_KILLED"};
    for (int k = 0; k < 3; ++k) win.widgets.push_back(make_stat_label(f, lookup(f, kCols[k]), 255 + 144 * k, 316));
    auto value = [&](int x, int y) {
        Widget w = make_stat_label(f, "", x, y);
        w.role = Role::Preview;
        win.widgets.push_back(std::move(w));
    };
    for (int k = 0; k < 5; ++k) {
        for (int y : {172, 202, 232, 262}) value(255 + 144 * k, y);
    }
    for (int y : {346, 376}) {
        for (int k = 0; k < 3; ++k) value(255 + 144 * k, y);
    }
    for (int y : {460, 490, 520}) value(255, y);
    for (std::size_t k = 0; k < f.end_players.size(); ++k) {
        auto pic = load_picture(f, *a, "Player.bmp", true, error);
        if (!pic) return false;
        const int kk = static_cast<int>(k);
        const int x = kk < 4 ? 48 + 236 * kk : -896 + 236 * kk, y = kk < 4 ? 626 : 661;
        Widget b = make_check(std::move(*pic), x, y, k == 0, Action::StatPlayer);
        b.row = kk;
        const int bw = b.w, bh = b.h;
        win.widgets.push_back(std::move(b));
        const std::uint32_t c = f.end_players[k].colour;  // COLORREF: red in the low byte
        Widget n = make_plain_label(
            f, data(f, stat_name(f, f.end_players[k])), x, y - 1,
            SDL_Color{static_cast<Uint8>(c), static_cast<Uint8>(c >> 8), static_cast<Uint8>(c >> 16), 0xff});
        n.w = bw, n.h = bh, n.centred = true;
        win.widgets.push_back(std::move(n));
    }
    f.stat_sel = 0;
    stat_fill(f, win);
    f.stack.clear();
    f.stack.push_back(std::move(win));
    return true;
}

// ---- the screens of the single player path ----


std::string player_name(const Frontend& f) {
    const Profiles& p = f.profiles;
    return p.selected >= 0 && p.selected < static_cast<int>(p.names.size()) ? p.names[p.selected]
                                                                            : "Player";
}


bool single_player_window(Frontend& f, Window& win, Archive& a, Screen screen, const char* background,
                          std::string* error) {
    auto arch = open_archive(f, "Data/Interface/Menu/singleplay.les", error);
    if (!arch) return false;
    a = std::move(*arch);
    win.screen = screen;
    win.x = 0, win.y = 0, win.w = kCanvasW, win.h = kCanvasH;
    auto back = load_picture(f, a, background, false, error);
    if (!back) return false;
    win.back = std::move(*back);
    return true;
}

std::optional<Widget> sp_button(Frontend& f, const Archive& a, const char* name, int x, int y, Action action,
                                std::uint8_t hotkey, std::string* error) {
    auto pic = load_picture(f, a, name, true, error);
    if (!pic) return std::nullopt;
    return make_button(std::move(*pic), x, y, action, hotkey);
}


bool open_profiles(Frontend& f, std::string* error) {
    Window win;
    Archive a;
    if (!single_player_window(f, win, a, Screen::Profiles, "SP_Fon_P1.bmp", error)) return false;
    // the edit field with the selected name, then the list with its scroll buttons and thumb
    win.widgets.push_back(make_edit(f, 480, 142, 180, 25, 3, 3, kProfileNameMax, player_name(f)));
    edit_focus(win.widgets.back());
    Widget list;
    list.kind = Kind::TextList;
    list.action = Action::ProfileSelect;
    list.x = 340, list.y = 197, list.w = 377, list.h = 352;
    auto up = load_picture(f, a, "SP_UPListBtn3.bmp", true, error);
    auto down = load_picture(f, a, "SP_DownListBtn3.bmp", true, error);
    auto thumb = load_picture(f, a, "SP_DvizokList.bmp", true, error);
    if (!up || !down || !thumb) return false;
    list.up = std::move(*up), list.down = std::move(*down), list.knob = std::move(*thumb);
    for (const std::string& n : f.profiles.names) data(f, n);  // names
    list_set_rows(f, list, f.profiles.names, f.profiles.selected);
    win.widgets.push_back(std::move(list));
    const struct {
        const char* pic;
        int x, y;
        Action act;
        std::uint8_t key;
    } kButtons[] = {{"SP_AddBtn3.bmp", 320, 593, Action::ProfileAdd, 0},
                    {"SP_DelBtn3.bmp", 524, 593, Action::ProfileDelete, 0},
                    {"SP_ContinueBtn3.bmp", 524, 686, Action::ProfileContinue, 13},
                    {"SP_ReturnBtn3.bmp", 320, 686, Action::ProfileBack, 27}};
    for (const auto& b : kButtons) {
        auto w = sp_button(f, a, b.pic, b.x, b.y, b.act, b.key, error);
        if (!w) return false;
        win.widgets.push_back(std::move(*w));
    }
    f.stack.push_back(std::move(win));
    return true;
}


bool open_single_player(Frontend& f, std::string* error) {
    Window win;
    Archive a;
    if (!single_player_window(f, win, a, Screen::SinglePlayer, "SP_Fon_P2.bmp", error)) return false;
    const struct {
        const char* pic;
        int x, y;
        Action act;
        std::uint8_t key;
    } kButtons[] = {{"butcampaigns.bmp", 620, 151, Action::SpCampaigns, 0},
                    {"butmissions.bmp", 620, 230, Action::SpMissions, 0},
                    {"butrandommaps.bmp", 624, 307, Action::SpRandomMaps, 0},
                    {"butback.bmp", 46, 76, Action::SpBack, 27}};
    for (const auto& b : kButtons) {
        auto w = sp_button(f, a, b.pic, b.x, b.y, b.act, b.key, error);
        if (!w) return false;
        win.widgets.push_back(std::move(*w));
    }
    f.stack.push_back(std::move(win));
    return true;
}

// ---- saving and loading (slice H, frontend.md 4.10, 4.11) ----




bool file_list(Frontend& f, const Archive& a, Widget& list, int y, int h, std::string* error) {
    list.kind = Kind::TextList;
    list.action = Action::FileSelect;
    list.x = 664, list.y = y, list.w = 212, list.h = h;
    list.list_row = 22, list.list_ox = 8, list.list_oy = 8, list.list_up_y = 6, list.list_bottom = h - 16;
    auto up = load_picture(f, a, "SP_UPTel.bmp", true, error);
    auto down = load_picture(f, a, "SP_DownTel.bmp", true, error);
    auto thumb = load_picture(f, a, "SP_DvizokTel.bmp", true, error);
    if (!up || !down || !thumb) return false;
    list.up = std::move(*up), list.down = std::move(*down), list.knob = std::move(*thumb);
    return true;
}

Widget picture_at(Picture pic, int x, int y) {
    Widget w;
    w.kind = Kind::Image;
    w.x = x, w.y = y, w.w = pic.w, w.h = pic.h;
    w.pic = std::move(pic);
    return w;
}





std::optional<Widget> slide_show(Frontend& f, const Archive& a, const std::string& name, int count, int x, int y,
                                 int per_pic, bool loop, std::string* error) {
    Widget s;
    s.kind = Kind::SlideShow;
    s.x = x, s.y = y;
    for (int k = 0; k < count; ++k) {
        auto pic = load_picture(f, a, name + std::to_string(k) + ".bmp", true, error);
        if (!pic) return std::nullopt;
        s.items.push_back(std::move(*pic));
    }
    s.w = s.items[0].w, s.h = s.items[0].h;
    s.start_ms = SDL_GetTicks();
    s.step_ms = std::clamp<int>(f.settings.speed_ms, 30, 90);
    s.per_pic = per_pic, s.loop = loop;
    return s;
}






bool open_load(Frontend& f, std::string* error) {
    Window win;
    Archive a;
    if (!single_player_window(f, win, a, Screen::LoadGame, "SP_Fon_P6.bmp", error)) return false;
    const struct {
        const char* pic;
        int x, y;
        Action act;
        std::uint8_t key;
    } kButtons[] = {{"butload.bmp", 647, 108, Action::FileLoad, 13}, {"butcancel.bmp", 747, 108, Action::FileBack, 27}};
    for (const auto& b : kButtons) {
        auto w = sp_button(f, a, b.pic, b.x, b.y, b.act, b.key, error);
        if (!w) return false;
        win.widgets.push_back(std::move(*w));
    }
    const struct {
        const char* pic;
        int x, y;
    } kPictures[] = {{"loadmission.bmp", 439, 27}, {"miss_name.bmp", 663, 336}, {"PB_Back1.bmp", 54, 688}};
    for (const auto& p : kPictures) {
        auto pic = load_picture(f, a, p.pic, true, error);
        if (!pic) return false;
        win.widgets.push_back(picture_at(std::move(*pic), p.x, p.y));
    }

    auto door = slide_show(f, a, "Door_", 8, 524, 348, 2, false, error);
    if (!door) return false;
    win.widgets.push_back(std::move(*door));
    Widget list;
    if (!file_list(f, a, list, 417, 304, error)) return false;
    std::vector<std::string> names;
    for (const SaveEntry& e : f.saves) names.push_back(e.name);
    list_set_rows(f, list, names, 0);
    list.sel = -1;
    list.visible = !f.animate;
    win.widgets.push_back(std::move(list));
    Widget preview;
    preview.kind = Kind::Image;
    preview.role = Role::Preview;
    preview.x = 153, preview.y = 125, preview.w = 294, preview.h = 296;
    win.widgets.push_back(std::move(preview));
    for (int k = 0; k < 5; ++k) {
        Widget l = make_plain_label(f, "", 135, 450 + 20 * k, kStatText);
        l.w = 331, l.centred = true, l.row = k;
        win.widgets.push_back(std::move(l));
    }
    f.stack.clear();
    f.stack.push_back(std::move(win));
    return true;
}




bool open_save(Frontend& f, const std::vector<std::string>& names, std::string* error) {
    Window win;
    Archive a;
    if (!single_player_window(f, win, a, Screen::SaveGame, "Back.bmp", error)) return false;
    const struct {
        const char* pic;
        int x, y;
    } kPictures[] = {{"Save.bmp", 224, 28}, {"Game_Name.bmp", 663, 336}};
    for (const auto& p : kPictures) {
        auto pic = load_picture(f, a, p.pic, true, error);
        if (!pic) return false;
        win.widgets.push_back(picture_at(std::move(*pic), p.x, p.y));
    }
    const struct {
        const char* pic;
        int x, y;
        Action act;
        std::uint8_t key;
    } kButtons[] = {{"butcancel.bmp", 747, 108, Action::FileBack, 27}, {"butsave.bmp", 647, 108, Action::FileSave, 13}};
    for (const auto& b : kButtons) {
        auto w = sp_button(f, a, b.pic, b.x, b.y, b.act, b.key, error);
        if (!w) return false;
        win.widgets.push_back(std::move(*w));
    }
    Widget list;
    if (!file_list(f, a, list, 416, 303, error)) return false;
    for (const std::string& n : names) data(f, n);  // the saves' file names
    list_set_rows(f, list, names, 0);
    list.sel = -1;
    win.widgets.push_back(std::move(list));
    auto field = load_picture(f, a, "Edit.bmp", true, error);
    if (!field) return false;
    const int fw = field->w, fh = field->h;
    win.widgets.push_back(picture_at(std::move(*field), 660, 700));
    Widget edit = make_edit(f, 660, 700, fw, fh, 6, 10, 30, ours(f, "save.default_name"));
    edit.ink = SDL_Color{0x00, 0xff, 0x00, 0xff};
    edit_set_text(f, edit, ours(f, "save.default_name"));
    edit.action = Action::FileSave;  // marks the save name field (save_name_clip); Enter is Save's hot key
    edit_focus(edit);
    win.widgets.push_back(std::move(edit));
    f.stack.clear();
    f.stack.push_back(std::move(win));
    return true;
}

Widget* find_role(Window& win, Role role) {
    for (Widget& w : win.widgets) {
        if (w.role == role) return &w;
    }
    return nullptr;
}

void load_preview(Frontend& f, Widget& image, const std::string& map_file);  // below

// ---- the mission lists (slice M4, docs/missions.md 4.2) ----

constexpr SDL_Color kMissionInk{0xc8, 0xc8, 0xc8, 0xff};
constexpr SDL_Color kWonInk{0xff, 0xad, 0x96, 0xff};






void mission_show(Frontend& f, Window& win, int row) {
    const MissionEntry* m =
        row >= 0 && row < static_cast<int>(f.missions.size()) ? &f.missions[static_cast<std::size_t>(row)] : nullptr;
    if (Widget* image = find_role(win, Role::Preview)) {
        image->pic = Picture{};
        if (m) load_preview(f, *image, m->map);
    }
    if (Widget* box = find_role(win, Role::MissionText)) {
        Picture thumb = std::move(box->knob);
        std::vector<std::string> paragraphs;
        if (m) paragraphs.push_back(m->text);
        const bool c = f.campaign_list;
        Widget fresh = make_textbox(f, 75, 448, 450, 220, {5, 6, 405, 220}, {420, c ? 33 : 34, 450, c ? 55 : 57},
                                    {420, 169, 450, c ? 187 : 186}, std::move(thumb), paragraphs, kMissionInk);
        fresh.shadow = true, fresh.ink = kMissionInk;
        fresh.role = Role::MissionText;
        *box = std::move(fresh);
    }
}


// (747, 108, Esc), the titles loadmission.bmp (439, 27) and miss_name.bmp (663, 336), the door (524, 348: the slide



// orange), the telephone's scroll buttons, and the progress bar's frame PB_Back1 (54, 688). The first row is selected.


bool open_mission_list(Frontend& f, int selected, std::string* error) {
    Window win;
    Archive a;
    if (!single_player_window(f, win, a, Screen::MissionList, "SP_Fon_P3.bmp", error)) return false;
    const struct {
        const char* pic;
        int x, y;
        Action act;
        std::uint8_t key;
    } kButtons[] = {{"butload.bmp", 647, 108, Action::MissionLoad, 13},
                    {"butcancel.bmp", 747, 108, Action::MissionBack, 27}};
    for (const auto& b : kButtons) {
        auto w = sp_button(f, a, b.pic, b.x, b.y, b.act, b.key, error);
        if (!w) return false;
        win.widgets.push_back(std::move(*w));
    }
    const struct {
        const char* pic;
        int x, y;
    } kPictures[] = {{"loadmission.bmp", 439, 27}, {"miss_name.bmp", 663, 336}};
    for (const auto& p : kPictures) {
        auto pic = load_picture(f, a, p.pic, true, error);
        if (!pic) return false;
        win.widgets.push_back(picture_at(std::move(*pic), p.x, p.y));
    }
    auto door = slide_show(f, a, "Door_", 8, 524, 348, 2, false, error);
    if (!door) return false;
    win.widgets.push_back(std::move(*door));
    Widget preview;
    preview.kind = Kind::Image;
    preview.role = Role::Preview;
    preview.x = 155, preview.y = 128, preview.w = 290, preview.h = 293;
    win.widgets.push_back(std::move(preview));
    auto thumb = load_picture(f, a, "SP_DvizokList.bmp", true, error);
    if (!thumb) return false;
    Widget text;
    text.kind = Kind::TextBox;
    text.role = Role::MissionText;
    text.knob = std::move(*thumb);
    win.widgets.push_back(std::move(text));
    Widget list;
    if (!file_list(f, a, list, 417, 304, error)) return false;
    list.action = Action::MissionSelect;
    list.w = f.campaign_list ? 212 : 213;
    list.font16 = true;
    list.fit = "missions.row";
    std::vector<std::string> titles;
    for (const MissionEntry& m : f.missions) {
        titles.push_back(m.title);
        list.row_ink.push_back(m.done && f.campaign_list ? kWonInk : kCream);
    }
    list_set_rows(f, list, titles, selected);
    list.visible = !f.animate;
    win.widgets.push_back(std::move(list));
    auto bar = load_picture(f, a, "PB_Back1.bmp", true, error);
    if (!bar) return false;
    win.widgets.push_back(picture_at(std::move(*bar), 54, 688));
    f.stack.push_back(std::move(win));
    Window& top = f.stack.back();
    for (const Widget& w : top.widgets) {
        if (w.kind == Kind::TextList) selected = w.sel;
    }
    mission_show(f, top, selected);
    return true;
}

// ---- the campaign select screen (slice M7, docs/missions.md 4.1) ----






void campaign_show(Frontend& f, Window& win, int row) {
    f.campaign_sel = row >= 0 && row < static_cast<int>(f.campaigns.size()) ? row : -1;
    const CampaignEntry* c = f.campaign_sel >= 0 ? &f.campaigns[static_cast<std::size_t>(row)] : nullptr;
    if (Widget* image = find_role(win, Role::Preview)) {
        image->pic = Picture{};
        std::string err;
        auto a = c && !c->picture.empty() ? open_archive(f, "Data/Interface/Menu/singleplay.les", &err) : std::nullopt;
        auto pic = a ? load_picture(f, *a, c->picture, true, &err) : std::nullopt;
        if (pic) {
            image->pic = std::move(*pic);
        } else if (c) {
            SDL_Log("front-end: no picture for the campaign %s: %s", c->stem.c_str(), err.c_str());
        }
    }
    if (Widget* box = find_role(win, Role::CampaignText)) {
        Picture thumb = std::move(box->knob);
        std::vector<std::string> paragraphs;
        if (c) paragraphs.push_back(c->text);
        Widget fresh = make_textbox(f, 485, 510, 524, 200, {5, 12, 479, 190}, {497, 34, 524, 55}, {497, 149, 524, 167},
                                    std::move(thumb), paragraphs, kMissionInk);
        fresh.shadow = true, fresh.ink = kMissionInk;
        fresh.role = Role::CampaignText;
        *box = std::move(fresh);
    }
}







bool open_campaign_select(Frontend& f, int selected, std::string* error) {
    Window win;
    Archive a;
    if (!single_player_window(f, win, a, Screen::CampaignSelect, "SP_Fon_P4.bmp", error)) return false;
    const struct {
        const char* pic;
        int x, y;
        Action act;
        std::uint8_t key;
    } kButtons[] = {{"btn_applay.bmp", 245, 596, Action::CampaignApply, 13},
                    {"btn_menu.bmp", 102, 596, Action::CampaignMenu, 27}};
    for (const auto& b : kButtons) {
        auto w = sp_button(f, a, b.pic, b.x, b.y, b.act, b.key, error);
        if (!w) return false;
        win.widgets.push_back(std::move(*w));
    }
    for (const auto& [name, x, y] : {std::tuple{"SkyLeft_", 1, 1}, std::tuple{"SkyRight_", 872, 0}}) {
        auto sky = slide_show(f, a, name, 33, x, y, 5, true, error);
        if (!sky) return false;
        win.widgets.push_back(std::move(*sky));
    }
    Widget list;
    list.kind = Kind::TextList;
    list.action = Action::CampaignSelect;
    list.x = 44, list.y = 237, list.w = 419, list.h = 329;
    list.list_row = 25, list.list_ox = 10, list.list_oy = 0, list.list_up_y = 8, list.list_bottom = 329 - 18;
    auto up = load_picture(f, a, "SP_UPListBtn3.bmp", true, error);
    auto down = load_picture(f, a, "SP_DownListBtn3.bmp", true, error);
    auto knob = load_picture(f, a, "SP_DvizokList.bmp", true, error);
    auto thumb = load_picture(f, a, "SP_DvizokList.bmp", true, error);
    if (!up || !down || !knob || !thumb) return false;
    list.up = std::move(*up), list.down = std::move(*down), list.knob = std::move(*knob);
    list.fit = "campaigns.row";
    std::vector<std::string> titles;
    for (const CampaignEntry& c : f.campaigns) titles.push_back(c.title);
    list_set_rows(f, list, titles, selected);
    const int sel = list.sel;
    win.widgets.push_back(std::move(list));
    Widget picture;
    picture.kind = Kind::Image;
    picture.role = Role::Preview;
    picture.x = 629, picture.y = 213, picture.w = 225, picture.h = 225;
    win.widgets.push_back(std::move(picture));
    Widget text;
    text.kind = Kind::TextBox;
    text.role = Role::CampaignText;
    text.knob = std::move(*thumb);
    win.widgets.push_back(std::move(text));
    f.stack.push_back(std::move(win));
    campaign_show(f, f.stack.back(), sel);
    return true;
}



void set_minimap_picture(Frontend& f, Widget& image, const Minimap& mm, const std::string& what) {
    Bytes rgb = minimap_to_rgb(mm);
    SDL_Surface* s =
        SDL_CreateSurfaceFrom(Minimap::kSide, Minimap::kSide, SDL_PIXELFORMAT_RGB24, rgb.data(), Minimap::kSide * 3);
    image.pic.tex.reset(s ? SDL_CreateTextureFromSurface(f.renderer, s) : nullptr);
    SDL_DestroySurface(s);
    if (!image.pic.tex) {
        SDL_Log("front-end: preview of %s: %s", what.c_str(), SDL_GetError());
        return;
    }
    SDL_SetTextureScaleMode(image.pic.tex.get(), SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(image.pic.tex.get(), SDL_BLENDMODE_NONE);
    image.pic.w = image.pic.h = Minimap::kSide;
}

void load_preview(Frontend& f, Widget& image, const std::string& map_file) {
    image.pic = Picture{};
    std::string err;
    auto bytes = f.files("Maps/" + preview_file(map_file), &err);
    std::optional<Minimap> mm;
    if (bytes) mm = parse_minimap(*bytes, &err);
    if (!mm) {
        SDL_Log("front-end: no preview for %s: %s", map_file.c_str(), err.c_str());
        return;
    }
    set_minimap_picture(f, image, *mm, map_file);
}



void show_map(Frontend& f, Window& win) {
    const MapEntry* m = map_at(f.maps, f.sk.players, f.sk.map);
    if (Widget* image = find_role(win, Role::Preview)) {
        image->pic = Picture{};
        if (m) load_preview(f, *image, m->file);
    }
    if (Widget* start = find_role(win, Role::Start)) start->visible = start->enabled = m != nullptr;
    if (Widget* prev = find_role(win, Role::Prev)) prev->enabled = f.sk.map > 0;
    if (Widget* next = find_role(win, Role::Next)) {
        next->enabled = map_at(f.maps, f.sk.players, f.sk.map + 1) != nullptr;
    }
}



void sync_skirmish(Frontend& f, Window& win) {
    for (Widget& w : win.widgets) {
        if (w.role == Role::Players) {
            w.sel = f.sk.players - 2;
            combo_close(w);
        } else if (w.role == Role::Resources) {
            w.sel = f.sk.resources;
            combo_close(w);
        } else if (w.role == Role::Rules) {
            w.sel = f.sk.plus ? 1 : 0;
            combo_close(w);
        }
        if (w.row < 0) continue;
        const sim::SkirmishSlot& r = f.sk.rows[w.row];
        const bool shown = w.row < f.sk.players;
        const bool opened = shown && r.level != 0;
        w.y = 275 + 40 * w.row;
        switch (w.role) {
            case Role::Name: w.visible = shown; break;
            case Role::Level: w.visible = shown, w.sel = r.level == 5 ? 4 : r.level; break;  // 5: Human (MP2)
            case Role::Nation: w.visible = opened, w.sel = r.nation; break;
            case Role::Colour: w.visible = opened, w.sel = r.colour; break;
            case Role::Team: w.visible = opened, w.sel = r.team; break;
            default: break;
        }
        if (w.kind == Kind::Combo) {
            combo_close(w);
        } else if (w.kind == Kind::DropList) {
            w.open = false;
            w.below = true;
            w.h = w.items[0].h;
        }
    }
}

bool make_combo(Frontend& f, const Archive& a, Widget& c, const std::vector<std::string>& items, int x, int y, int w,
                int list_h, int row_h, int ox, int oy, const char* place, std::string* error) {
    c.kind = Kind::Combo;
    c.action = Action::SkRow;
    c.x = x, c.y = y, c.w = w, c.h = row_h;
    c.row_h = row_h, c.list_h = list_h, c.ox = ox, c.oy = oy;
    c.strs = items;
    for (const std::string& s : items) c.item_text.push_back(make_text_object(f, s, SDL_Color{0, 0, 0, 0xff}));
    fit_items(f, c, place, f.font18);
    auto field = load_picture(f, a, "mp_player4.bmp", true, error);
    auto bar = load_picture(f, a, "mp_plaer4_vert.bmp", true, error);
    auto up = load_picture(f, a, "Upbtn1.bmp", true, error);
    auto down = load_picture(f, a, "Downbtn1.bmp", true, error);
    if (!field || !bar || !up || !down) return false;
    c.pic = std::move(*field), c.bar = std::move(*bar), c.up = std::move(*up), c.down = std::move(*down);
    return true;
}

std::vector<std::string> strings_numbered(const Frontend& f, const std::string& prefix, int first, int count) {
    std::vector<std::string> out;
    for (int i = 0; i < count; ++i) out.push_back(lookup(f, prefix + std::to_string(first + i)));
    return out;
}

constexpr std::uint32_t kNetSpeeds[] = {30, 45, 60, 90};  // slice MP2: the host's game speeds (ms per tick)


// MP_Fon_P4 over "Single Player", row 0 is your network name, the levels end with Human (level 5, a player who joins),
// row 1 is one at first, the game speed under the player count and resources, and Create (Multiplay.les) in place of
// Start. Ours (docs/your-version.md, docs/decisions.md 1): the rule set box, Entente Engine or Entente Engine Plus, in
// the column of the player count and the resources (one row more, so those rows are moved up: 205, 245 and 285, the
// host's four rows 30 apart from 200), the same on the desktop and the web and in the host's setup; "Original" is
// never offered.
bool open_skirmish(Frontend& f, std::string* error, bool net = false) {
    if (!f.maps_read) {  // Maps/random.ind, read once; a map that does not parse is left out and logged once
        auto ind = f.files("Maps/random.ind", error);
        if (!ind) return false;
        std::vector<std::string> left_out;
        f.maps = parse_random_ind(std::string_view(reinterpret_cast<const char*>(ind->data()), ind->size()));
        append_extra_maps(f.maps, [&f](const std::string& file) {
            std::string e;
            return f.files("Maps/" + file, &e).has_value();
        });
        limit_to_map_positions(
            f.maps,
            [&f](const std::string& file) {
                std::string e;
                return f.files("Maps/" + file, &e);
            },
            &left_out);
        for (const std::string& name : left_out)
            SDL_Log("front-end: %s is left out of the map list: its file does not parse (docs/bugs.md B4)",
                    name.c_str());
        f.maps_read = true;
    }
    if (f.maps.empty()) {
        *error = "Maps/random.ind lists no map";
        return false;
    }
    f.sk = skirmish_defaults(net ? f.net.name : player_name(f));
    f.sk.plus = f.plus;
    f.net_host = net;
    if (net) f.sk.rows[1].level = 5;
    Window win;
    Archive a;
    if (!single_player_window(f, win, a, Screen::Skirmish, "SP_Fon_P5.bmp", error)) return false;
    std::optional<Archive> mp;
    if (net) {
        mp = open_archive(f, "Data/Interface/Menu/Multiplay.les", error);
        if (!mp) return false;
        auto title = load_picture(f, *mp, "mp_fon_p4.bmp", false, error);
        if (!title) return false;
        Widget t = picture_at(std::move(*title), 44, 44);
        t.w = 402, t.h = 84, t.part = SDL_FRect{44, 44, 402, 84};
        win.widgets.push_back(std::move(t));
    }
    // Start, Return, Next, Prev in the original's order
    const struct {
        const char* pic;
        int x, y;
        Action act;
        std::uint8_t key;
        Role role;
    } kButtons[] = {{"mp_bstart.bmp", 345, 689, Action::SkStart, 0, Role::Start},
                    {"SP_ReturnBtn3.bmp", 140, 687, Action::SkBack, 27, Role::None},
                    {"SP_NextBtn.bmp", 816, 687, Action::SkNext, 0, Role::Next},
                    {"SP_PrevBtn.bmp", 645, 687, Action::SkPrev, 0, Role::Prev}};
    for (const auto& b : kButtons) {
        auto w = net && b.act == Action::SkStart
                     ? sp_button(f, *mp, "mp_createbtn3.bmp", b.x, b.y, Action::NetHostCreate, 0, error)
                     : sp_button(f, a, b.pic, b.x, b.y, b.act, b.key, error);
        if (!w) return false;
        w->role = b.role;
        win.widgets.push_back(std::move(*w));
    }
    // the original's rows are at 220 and 265; ours has the rule set under them (the host's setup the speed too)
    const int players_y = net ? 200 : 205, resources_y = net ? 230 : 245, speed_y = 260, rules_y = net ? 290 : 285;
    for (const auto& l :
         {std::pair<const char*, int>{"SP_LABEL_KOL_PL", players_y}, {"SP_LABEL_RESOURCES", resources_y}}) {
        Widget label = make_plain_label(f, lookup(f, l.first), 650, l.second + 3, kCream);
        label.w = 145, label.centred = true;
        fit_label(f, label, "skirmish.label");
        win.widgets.push_back(std::move(label));
    }
    Widget players, resources;
    if (!make_combo(f, a, players, strings_numbered(f, "SP_NPL_", 2, 7), 800, players_y, 100, 175, 25, 20, 4,
                    "skirmish.players", error) ||
        !make_combo(f, a, resources, strings_numbered(f, "SP_RES_", 0, 3), 800, resources_y, 100, 75, 25, 10, 4,
                    "skirmish.resources", error)) {
        return false;
    }
    players.action = Action::SkPlayers, players.role = Role::Players;
    resources.action = Action::SkResources, resources.role = Role::Resources;
    win.widgets.push_back(std::move(players));
    win.widgets.push_back(std::move(resources));
    if (net) {
        Widget label = make_plain_label(f, ours(f, "skirmish.speed"), 650, speed_y + 3, kCream);
        label.w = 145, label.centred = true;
        fit_label(f, label, "skirmish.label");
        win.widgets.push_back(std::move(label));
        Widget speed;
        if (!make_combo(f, a, speed,
                        {ours(f, "skirmish.speed_normal"), ours(f, "skirmish.speed_slower"),
                         ours(f, "skirmish.speed_slow"), ours(f, "skirmish.speed_very_slow")},
                        800, speed_y, 100, 100, 25, 10, 4, "skirmish.speed_value", error)) {
            return false;
        }
        speed.action = Action::NetSpeed;
        for (int k = 0; k < 4; ++k) {
            if (kNetSpeeds[k] == f.net.tick_ms) speed.sel = k;
        }
        win.widgets.push_back(std::move(speed));
    }
    {  // the rule set: a shorter label and a wider box for the two names; what Plus is shows under the mouse
        const std::string plus_tip = ours(f, "skirmish.rules_plus_tip");
        Widget label = make_plain_label(f, ours(f, "skirmish.rules"), 650, rules_y + 3, kCream);
        label.w = 90, label.centred = true;
        fit_label(f, label, "skirmish.rules_label");
        label.tip = plus_tip;
        win.widgets.push_back(std::move(label));
        Widget rules;
        if (!make_combo(f, a, rules, {ours(f, "skirmish.rules_classic"), ours(f, "skirmish.rules_plus")}, 745, rules_y,
                        215, 50, 25, 18, 4, "skirmish.rules_value", error)) {  // the stretched field's edge
            return false;
        }
        rules.action = Action::SkRules, rules.role = Role::Rules;
        rules.item_tips[1] = plus_tip;
        win.widgets.push_back(std::move(rules));
    }
    Widget preview;
    preview.kind = Kind::Image;
    preview.role = Role::Preview;
    preview.x = 668, preview.y = 359, preview.w = 290, preview.h = 293;
    win.widgets.push_back(std::move(preview));
    // one row per slot: your name, or the level; nation; colour; team
    std::vector<std::string> levels = strings_numbered(f, "MP_LEVEL", 0, 4);
    if (net) levels.push_back(ours(f, "skirmish.human"));
    const std::vector<std::string> teams = strings_numbered(f, "MP_KOMMAND", 0, 9);
    std::vector<std::string> nations;
    for (const char* key :
         {"MPS_RUSSIA_0", "MPS_ENGLAND_1", "MPS_FRANCE_2", "MPS_GERMANY_3", "MPS_AUSTRIA_4", "MPS_NULL"}) {
        nations.push_back(lookup(f, key));
    }
    for (int k = 0; k < 8; ++k) {
        const int y = 275 + 40 * k;
        auto tag = [k](Widget&& w, Role role) {
            w.role = role, w.row = k;
            return std::move(w);
        };
        if (k == 0) {
            win.widgets.push_back(tag(make_plain_label(f, data(f, f.sk.name), 70, y, kCream), Role::Name));
        } else {
            Widget level;
            if (!make_combo(f, a, level, levels, 60, y, 135, net ? 150 : 120, 30, 10, 6, "skirmish.level", error))
                return false;
            win.widgets.push_back(tag(std::move(level), Role::Level));
        }
        Widget nation, team;
        if (!make_combo(f, a, nation, nations, 245, y, 120, 150, 30, 10, 6, "skirmish.nation", error) ||
            !make_combo(f, a, team, teams, 548, y, 60, 120, 30, 10, 6, "skirmish.team", error)) {
            return false;
        }
        win.widgets.push_back(tag(std::move(nation), Role::Nation));
        Widget colour;
        colour.kind = Kind::DropList;
        colour.action = Action::SkRow;
        colour.x = 412, colour.y = y, colour.field_w = 90, colour.w = 90;
        for (int c = 1; c <= 8; ++c) {
            auto pic = load_picture(f, a, "mp_color" + std::to_string(c) + ".bmp", true, error);
            if (!pic) return false;
            colour.items.push_back(std::move(*pic));
        }
        colour.h = colour.items[0].h;
        win.widgets.push_back(tag(std::move(colour), Role::Colour));
        win.widgets.push_back(tag(std::move(team), Role::Team));
    }
    f.stack.push_back(std::move(win));
    Window& top = f.stack.back();
    sync_skirmish(f, top);
    show_map(f, top);
    return true;
}

// ---- actions ----

void log_failure(bool ok, const std::string& error) {
    if (!ok) SDL_Log("front-end: %s", error.c_str());
}

void message(Frontend& f, const std::string& text) {
    std::string err;
    log_failure(open_message(f, text, &err), err);
}

std::function<int(const std::string&)> g_file_gate;  // frontend_set_file_gate
std::string g_build_version;                         // frontend_set_build_label
bool g_build_developer = false;

// The original's "Entente v 2.0" and ours in the main menu's corner: the build label and "Check for updates".
void make_corner_texts(Frontend& f) {
    const std::string version = ours(f, "version.game");
    check_drawn(f.lang, version);
    f.version.reset(TTF_CreateText(f.engine, f.font16, version.c_str(), 0));
    if (f.version) TTF_SetTextColor(f.version.get(), 0xff, 0xf7, 0x99, 0xff);
    f.build_label.reset();
    if (!g_build_version.empty()) {
        const std::string label = ours(f, g_build_developer ? "menu.build_label_developer" : "menu.build_label",
                                       {{"version", data(f, g_build_version)}});
        check_drawn(f.lang, label);
        f.build_label.reset(TTF_CreateText(f.engine, f.font16, label.c_str(), 0));
        if (f.build_label) TTF_SetTextColor(f.build_label.get(), 0xff, 0xf7, 0x99, 0xff);
    }
    f.update_link.reset();
    if (g_update_link && g_update_corner) {
        const std::string link = ours(f, "menu.check_updates");
        check_drawn(f.lang, link);
        f.update_link.reset(TTF_CreateText(f.engine, f.font16, link.c_str(), 0));
    }
}

// The Museum button: opens the museum at once, or on the web when its pack is there (asked again each frame).
void museum_poll(Frontend& f) {
    const int state = g_file_gate ? g_file_gate("data/museum/museum.les") : 1;
    if (state == 0) return;
    f.museum_wait = false;
    std::string err;
    if (state < 0) {
        message(f, ours(f, "museum.pictures_failed"));
    } else if (!open_museum(f, &err)) {
        log_failure(false, err);
        message(f, ours(f, "museum.files_failed"));
    }
}

void close_top(Frontend& f) {
    if (f.stack.size() > 1) f.stack.pop_back();
}

Widget* find_kind(Window& win, Kind kind) {
    for (Widget& w : win.widgets) {
        if (w.kind == kind) return &w;
    }
    return nullptr;
}



void profiles_refresh(Frontend& f, Window& win, bool set_edit) {
    for (const std::string& n : f.profiles.names) data(f, n);  // names
    if (Widget* list = find_kind(win, Kind::TextList)) list_set_rows(f, *list, f.profiles.names, f.profiles.selected);
    if (Widget* edit = find_kind(win, Kind::Edit)) {
        if (set_edit) edit_set_text(f, *edit, player_name(f));
        edit_focus(*edit);
    }
}




void start_skirmish(Frontend& f, const sim::SkirmishSetup& setup);

std::string describe(const sim::SkirmishSetup& s) {
    std::string out = "map " + s.map + ", " + std::to_string(s.players) + " players, resources " +
                      std::to_string(s.resources) + ", seed " + std::to_string(s.seed) +
                      ", slots (level/nation/colour/team)";
    for (const sim::SkirmishSlot& sl : s.slots) {
        out += " " + std::to_string(sl.level) + "/" + std::to_string(sl.nation) + "/" + std::to_string(sl.colour) +
               "/" + std::to_string(sl.team);
    }
    return out;
}

void start_skirmish(Frontend& f, const sim::SkirmishSetup& setup) {
    f.setup = setup;
    SDL_Log("front-end: Start, %s", describe(setup).c_str());
    f.request = FrontendRequest::StartSkirmish;
}



void skirmish_start(Frontend& f) {
    const MapEntry* m = map_at(f.maps, f.sk.players, f.sk.map);
    if (!m) return;
    const std::uint32_t seed = f.seed ? *f.seed : static_cast<std::uint32_t>(SDL_rand_bits() ^ SDL_GetTicksNS());
    start_skirmish(f, skirmish_setup(f.sk, m->file, seed, f.settings));
}

const Widget* find(const Window& w, Kind kind, int nth) {
    for (const Widget& x : w.widgets) {
        if (x.kind == kind && nth-- == 0) return &x;
    }
    return nullptr;
}







constexpr SDL_Color kParchmentInk{0x30, 0x20, 0x10, 0xff};  // our text on the parchment fields of page 1

// The address, port and password fields take printable ASCII without spaces, at most `max` characters.
std::string net_field_clip(const std::string& text, std::size_t max) {
    std::string out;
    for (char c : text) {
        if (c > 0x20 && c < 0x7f && out.size() < max) out += c;
    }
    return out;
}

bool net_window(Frontend& f, Window& win, Archive& mp, Archive& sp, Screen screen, const char* background,
                std::string* error) {
    auto m = open_archive(f, "Data/Interface/Menu/Multiplay.les", error);
    if (!m) return false;
    mp = std::move(*m);
    if (!single_player_window(f, win, sp, screen, "Back.bmp", error)) return false;
    auto back = load_picture(f, mp, background, false, error);
    if (!back) return false;
    win.back = std::move(*back);
    return true;
}

Widget net_edit(Frontend& f, int x, int y, int w, std::size_t max, const std::string& text, Role role, SDL_Color ink) {
    Widget e = make_edit(f, x, y, w, 26, 3, 3, max, text);
    e.ink = ink;
    edit_set_text(f, e, text);
    e.action = Action::NetField, e.role = role;
    return e;
}

// The text list of pages 1 and 2 at (x, y), w x h, its scroll buttons on the page's scroll column.
bool net_list(Frontend& f, const Archive& mp, const Archive& sp, Widget& list, int x, int y, int w, int h,
              std::string* error) {
    list.kind = Kind::TextList;
    list.x = x, list.y = y, list.w = w, list.h = h;
    list.list_up_y = 30;
    auto up = load_picture(f, mp, "upbtn3.bmp", true, error);
    auto down = load_picture(f, mp, "downbtn3.bmp", true, error);
    auto thumb = load_picture(f, sp, "SP_DvizokList.bmp", true, error);
    if (!up || !down || !thumb) return false;
    list.up = std::move(*up), list.down = std::move(*down), list.knob = std::move(*thumb);
    return true;
}

// Page 1 (MP_Fon_P1): the profile list ("Players List", Add and Delete as on the profile screen), your name, the
// connection (TCP/IP, the only one), the host's address (empty: the games on this network) and the port; Return (Esc)
// to the main menu, Continue (Enter) to page 2.
bool open_net_player(Frontend& f, std::string* error) {
    Window win;
    Archive mp, sp;
    if (!net_window(f, win, mp, sp, Screen::NetPlayer, "mp_fon_p1.bmp", error)) return false;
    if (f.net.name.empty()) f.net.name = player_name(f);
    Widget name = make_edit(f, 632, 403, 300, 26, 3, 3, kProfileNameMax, f.net.name);  // the first edit field: the
    name.ink = kParchmentInk;                                                          // profile actions take it
    edit_set_text(f, name, f.net.name);
    edit_focus(name);
    win.widgets.push_back(std::move(name));
    Widget list;
    if (!net_list(f, mp, sp, list, 52, 190, 390, 372, error)) return false;
    list.action = Action::ProfileSelect;
    for (const std::string& n : f.profiles.names) data(f, n);  // names
    list_set_rows(f, list, f.profiles.names, f.profiles.selected);
    win.widgets.push_back(std::move(list));
    Widget connection = make_plain_label(f, ours(f, "net.connection"), 642, 447, kParchmentInk);
    connection.shadow = false;
    fit_label(f, connection, "net.connection");
    win.widgets.push_back(std::move(connection));
    win.widgets.push_back(net_edit(f, 626, 481, 300, 64, f.net_ip, Role::NetAddress, kParchmentInk));
    win.widgets.push_back(net_edit(f, 632, 519, 120, 5, std::to_string(f.net.port), Role::NetPort, kParchmentInk));
    const struct {
        const Archive* a;
        const char* pic;
        int x, y;
        Action act;
        std::uint8_t key;
    } kButtons[] = {{&mp, "mp_addbtn3.bmp", 45, 594, Action::ProfileAdd, 0},
                    {&sp, "sp_delbtn3.bmp", 248, 594, Action::ProfileDelete, 0},
                    {&sp, "sp_returnbtn3.bmp", 45, 687, Action::NetReturn, 27},
                    {&mp, "mp_continuebtn3.bmp", 248, 687, Action::NetContinue, 13}};
    for (const auto& b : kButtons) {
        auto w = sp_button(f, *b.a, b.pic, b.x, b.y, b.act, b.key, error);
        if (!w) return false;
        win.widgets.push_back(std::move(*w));
    }
    f.stack.push_back(std::move(win));
    return true;
}

std::string map_title(const std::string& file) {
    return file.size() > 4 && file.compare(file.size() - 4, 4, ".dat") == 0 ? file.substr(0, file.size() - 4) : file;
}

// Page 2's list, picture and lines follow f.net_found, the typed address and the selected address.
void net_games_refresh(Frontend& f, Window& win) {
    f.net_rows.clear();
    if (!f.net_ip.empty()) {
        NetGameRow typed;
        typed.address =
            f.net_ip.find(':') == std::string::npos ? f.net_ip + ":" + std::to_string(f.net.port) : f.net_ip;
        typed.name = ours(f, "net.typed", {{"address", data(f, typed.address)}});
        f.net_rows.push_back(typed);
    }
    f.net_rows.insert(f.net_rows.end(), f.net_found.begin(), f.net_found.end());
    std::vector<std::string> texts;
    int sel = -1;
    for (std::size_t k = 0; k < f.net_rows.size(); ++k) {
        const NetGameRow& r = f.net_rows[k];
        std::string t = data(f, r.name);  // a game's name, an address or our row texts of the app
        if (r.max_humans > 0) t += "  " + std::to_string(r.humans) + "/" + std::to_string(r.max_humans);
        if (r.locked) t = ours(f, "net.row_locked", {{"row", t}});
        texts.push_back(t);
        if (r.address == f.net_selected) sel = static_cast<int>(k);
    }
    if (sel < 0 && !f.net_rows.empty()) sel = 0;
    f.net_selected = sel >= 0 ? f.net_rows[static_cast<std::size_t>(sel)].address : std::string();
    std::vector<std::string> lines;
    std::string map;
    if (sel < 0) {
        lines = {ours(f, "net.none_found")};
    } else if (const NetGameRow& s = f.net_rows[static_cast<std::size_t>(sel)]; !s.save.empty()) {  // slice MP9
        map = s.map;
        lines = {ours(f, "net.saved_game", {{"map", data(f, map_title(s.map))}, {"when", s.when}}),
                 ours(f, "net.players", {{"players", data(f, s.players)}}), ours(f, "net.saved_rejoin")};
    } else if (f.net_rows[static_cast<std::size_t>(sel)].max_humans == 0) {
        lines = {ours(f, "net.address", {{"address", data(f, f.net_rows[static_cast<std::size_t>(sel)].address)}}),
                 ours(f, "net.details_on_join")};
    } else {
        const NetGameRow& r = f.net_rows[static_cast<std::size_t>(sel)];
        map = r.map;
        lines = {ours(f, "net.map", {{"map", data(f, map_title(r.map))}}),
                 ours(f, "net.players_of", {{"n", std::to_string(r.humans)}, {"max", std::to_string(r.max_humans)}}),
                 ours(f, "net.address", {{"address", data(f, r.address)}})};
        if (r.locked) lines.push_back(ours(f, "net.password_needed"));
    }
    std::vector<Widget*> rows;
    for (Widget& w : win.widgets) {
        if (w.kind == Kind::TextList) {
            list_set_rows(f, w, texts, sel);
            w.sel = sel;
        } else if (w.role == Role::Preview) {
            w.pic = Picture{};
            if (!map.empty()) load_preview(f, w, map);
        } else if (w.role == Role::NetInfo) {
            rows.push_back(&w);
        }
    }
    // The help and the details wrap in the box (docs/languages.md 2.4 rule 1); each text is a paragraph, a "\n" in a
    // text starts another (the help texts: English keeps its rows, a translation may let a sentence run on).
    fit_rows(f, rows, "net.help", lines, kCream);
}

// Page 2 (MP_Fon_P2): the list (the typed address, then the games found on this network: name, players, password),
// the selected game's map in the frame and its lines in the box below with the password field; Join (Enter), Return
// (Esc) to page 1, Create to the host's setup.
bool open_net_games(Frontend& f, std::string* error) {
    Window win;
    Archive mp, sp;
    if (!net_window(f, win, mp, sp, Screen::NetGames, "mp_fon_p2.bmp", error)) return false;
    Widget list;
    if (!net_list(f, mp, sp, list, 142, 188, 300, 422, error)) return false;
    list.action = Action::NetGameSelect;
    win.widgets.push_back(std::move(list));
    Widget preview;
    preview.kind = Kind::Image;
    preview.role = Role::Preview;
    preview.x = 568, preview.y = 232, preview.w = 298, preview.h = 300;
    win.widgets.push_back(std::move(preview));
    for (int k = 0; k < 4; ++k) {
        Widget l = make_plain_label(f, "", 472, 578 + 22 * k, kCream);
        l.role = Role::NetInfo, l.row = k;
        win.widgets.push_back(std::move(l));
    }
    win.widgets.push_back(make_plain_label(f, ours(f, "net.password"), 472, 684, kCream));
    fit_label(f, win.widgets.back(), "net.password");
    auto field = load_picture(f, sp, "Edit.bmp", true, error);
    if (!field) return false;
    const int fw = field->w, fh = field->h;
    win.widgets.push_back(picture_at(std::move(*field), 590, 672));
    Widget password = make_edit(f, 590, 672, fw, fh, 6, 10, 32, f.net.password);
    password.action = Action::NetField, password.role = Role::NetPassword;
    win.widgets.push_back(std::move(password));
    const struct {
        const Archive* a;
        const char* pic;
        int x, y;
        Action act;
        std::uint8_t key;
    } kButtons[] = {{&mp, "mp_joinbtn3.bmp", 148, 647, Action::NetJoin, 13},
                    {&sp, "sp_returnbtn3.bmp", 45, 687, Action::NetReturn, 27},
                    {&mp, "mp_createbtn3.bmp", 248, 687, Action::NetCreate, 0}};
    for (const auto& b : kButtons) {
        auto w = sp_button(f, *b.a, b.pic, b.x, b.y, b.act, b.key, error);
        if (!w) return false;
        win.widgets.push_back(std::move(*w));
    }
    f.stack.push_back(std::move(win));
    net_games_refresh(f, f.stack.back());
    return true;
}





void net_scenario_show(Frontend& f, Window& win, int row) {
    const MissionEntry* m =
        row >= 0 && row < static_cast<int>(f.scenarios.size()) ? &f.scenarios[static_cast<std::size_t>(row)] : nullptr;
    if (Widget* image = find_role(win, Role::Preview)) {
        image->pic = Picture{};
        if (m) load_preview(f, *image, m->map);
    }
    if (Widget* box = find_role(win, Role::MissionText)) {
        Picture thumb = std::move(box->knob);
        std::vector<std::string> paragraphs;
        if (m) paragraphs.push_back(m->text);
        Widget fresh = make_textbox(f, 455, 567, 525, 158, {5, 1, 480, 140}, {500, 16, 525, 36}, {500, 122, 525, 140},
                                    std::move(thumb), paragraphs, kMissionInk);
        fresh.shadow = true, fresh.ink = kMissionInk;
        fresh.role = Role::MissionText;
        *box = std::move(fresh);
    }
}





// the places of page 2's (ours, and Return is singleplay.les's: mp_returnbtn3 is corrupt). The first row is selected.
// In the original Create made the game on the scenario and the lobby took the slots; ours opens the host's setup with
// the scenario's map chosen (docs/frontend.md 4.15).
bool open_net_scenarios(Frontend& f, int selected, std::string* error) {
    Window win;
    Archive mp, sp;
    if (!net_window(f, win, mp, sp, Screen::NetScenarios, "mp_fon_p3.bmp", error)) return false;
    Widget list;
    if (!net_list(f, mp, sp, list, 70, 212, 364, 380, error)) return false;
    list.action = Action::NetScenarioSelect;
    list.list_row = 25, list.list_up_y = 4;
    list.fit = "net.scenario_row";
    std::vector<std::string> titles;
    for (const MissionEntry& m : f.scenarios) titles.push_back(m.title);
    list_set_rows(f, list, titles,
                  f.scenarios.empty() ? -1 : std::clamp(selected, 0, static_cast<int>(titles.size()) - 1));
    const int sel = list.sel;
    win.widgets.push_back(std::move(list));
    Widget preview;
    preview.kind = Kind::Image;
    preview.role = Role::Preview;
    preview.x = 566, preview.y = 231, preview.w = 301, preview.h = 303;
    win.widgets.push_back(std::move(preview));
    auto thumb = load_picture(f, sp, "SP_DvizokList.bmp", true, error);
    if (!thumb) return false;
    Widget text;
    text.kind = Kind::TextBox;
    text.role = Role::MissionText;
    text.knob = std::move(*thumb);
    win.widgets.push_back(std::move(text));
    const struct {
        const Archive* a;
        const char* pic;
        int x, y;
        Action act;
        std::uint8_t key;
    } kButtons[] = {{&sp, "sp_returnbtn3.bmp", 45, 687, Action::NetReturn, 27},
                    {&mp, "mp_createbtn3.bmp", 248, 687, Action::NetScenarioCreate, 13}};
    for (const auto& b : kButtons) {
        auto w = sp_button(f, *b.a, b.pic, b.x, b.y, b.act, b.key, error);
        if (!w) return false;
        win.widgets.push_back(std::move(*w));
    }
    f.stack.push_back(std::move(win));
    net_scenario_show(f, f.stack.back(), sel);
    return true;
}

// The lobby (MP_Fon_P4): the game's name, a row per slot (player, nation, colour, team, and in the Accept column
// whether the human is in, or the computer's mark), the map, the lines of what is going on in the lower right box
// (the picture's "Chat" box), Return (Esc) and the host's Start (Enter, dimmed until every human is in).
bool open_net_lobby(Frontend& f, std::string* error) {
    Window win;
    Archive mp, sp;
    if (!net_window(f, win, mp, sp, Screen::NetLobby, "mp_fon_p4.bmp", error)) return false;
    const NetLobby& l = f.lobby;
    Widget title = make_plain_label(f, l.title, 50, 145, kCream);
    title.w = 390, title.centred = true;
    fit_label(f, title, "lobby.title");
    win.widgets.push_back(std::move(title));
    const std::vector<std::string> teams = strings_numbered(f, "MP_KOMMAND", 0, 9);
    const char* nations[] = {"MPS_RUSSIA_0",  "MPS_ENGLAND_1", "MPS_FRANCE_2",
                             "MPS_GERMANY_3", "MPS_AUSTRIA_4", "MPS_NULL"};
    for (std::size_t k = 0; k < l.slots.size() && k < 8; ++k) {
        const NetLobbySlot& s = l.slots[k];
        const int y = 250 + 40 * static_cast<int>(k);
        win.widgets.push_back(make_plain_label(f, s.player, 72, y + 6, kCream));
        fit_label(f, win.widgets.back(), "lobby.player");
        win.widgets.push_back(make_plain_label(f, lookup(f, nations[std::clamp(s.nation, 0, 5)]), 252, y + 6, kCream));
        fit_label(f, win.widgets.back(), "lobby.nation");
        auto swatch =
            load_picture(f, sp, "mp_color" + std::to_string(std::clamp(s.colour, 0, 7) + 1) + ".bmp", true, error);
        if (!swatch) return false;
        Widget c = picture_at(std::move(*swatch), 470, y);
        c.w = c.pic.w / 2, c.part = SDL_FRect{0, 0, static_cast<float>(c.w), static_cast<float>(c.h)};
        win.widgets.push_back(std::move(c));
        win.widgets.push_back(
            make_plain_label(f, teams[static_cast<std::size_t>(std::clamp(s.team, 0, 8))], 600, y + 6, kCream));
        fit_label(f, win.widgets.back(), "lobby.team");
        auto mark = load_picture(f, mp,
                                 !s.human || s.computer ? "mp_comp.bmp"
                                 : s.joined             ? "mp_ready.bmp"
                                                        : "mp_noready.bmp",
                                 true, error);
        if (!mark) return false;
        win.widgets.push_back(picture_at(std::move(*mark), 897, y + 2));
        if (l.seats && s.human) {  // slice MP9: the whole row is the host's button (no picture of its own)
            Widget seat;
            seat.kind = Kind::Button, seat.action = Action::NetSeat, seat.row = s.slot;
            seat.x = 50, seat.y = y, seat.w = 880, seat.h = 36;
            win.widgets.push_back(std::move(seat));
        }
    }
    win.widgets.push_back(
        make_plain_label(f, ours(f, "net.map", {{"map", data(f, map_title(l.map))}}), 58, 644, kCream));
    fit_label(f, win.widgets.back(), "lobby.map");
    // ours: the rule set of the host's setup, for everyone (docs/your-version.md)
    win.widgets.push_back(make_plain_label(
        f, ours(f, "net.rules", {{"rules", ours(f, l.plus ? "skirmish.rules_plus" : "skirmish.rules_classic")}}), 58,
        662, kCream));
    fit_label(f, win.widgets.back(), "lobby.map");
    // What is going on: four rows 20 apart, the lines wrapped into them (docs/languages.md 2.4 rule 1).
    const std::size_t first = win.widgets.size();
    for (int k = 0; k < 4; ++k) {
        Widget row;
        row.kind = Kind::Label;
        row.x = 472, row.y = 640 + 20 * k, row.h = 14;
        row.shadow = true;
        win.widgets.push_back(std::move(row));
    }
    std::vector<Widget*> rows;
    for (std::size_t k = first; k < win.widgets.size(); ++k) rows.push_back(&win.widgets[k]);
    fit_rows(f, rows, "lobby.lines", l.lines, kCream);
    auto back = sp_button(f, sp, "sp_returnbtn3.bmp", 45, 687, Action::NetLeave, 27, error);
    if (!back) return false;
    win.widgets.push_back(std::move(*back));
    if (l.host) {
        auto start = sp_button(f, mp, "mp_bstart.bmp", 248, 687, Action::NetStart, 13, error);
        if (!start) return false;
        start->enabled = l.can_start, start->dimmed = !l.can_start;
        win.widgets.push_back(std::move(*start));
    }
    f.stack.push_back(std::move(win));
    return true;
}

Widget* find_net(Window& win, Role role) {
    for (Widget& w : win.widgets) {
        if (w.role == role && w.kind == Kind::Edit) return &w;
    }
    return nullptr;
}

// Continue on page 1: the name (also a profile, as Continue on the profile screen), the address and the port.
void net_continue(Frontend& f) {
    Window& win = f.stack.back();
    const Widget* name = find_kind(win, Kind::Edit);
    const Widget* address = find_net(win, Role::NetAddress);
    const Widget* port = find_net(win, Role::NetPort);
    if (!name || !address || !port) return;
    const std::string n = name->label;
    if (n.find_first_not_of(' ') == std::string::npos) {
        message(f, ours(f, "net.type_name"));
        return;
    }
    const long p = std::strtol(port->label.c_str(), nullptr, 10);
    if (p < 1 || p > 65535 || port->label.find_first_not_of("0123456789") != std::string::npos) {
        message(f, ours(f, "net.port_range"));
        return;
    }
    profile_add(f.profiles, n);
    f.net.name = n;
    f.net.port = static_cast<std::uint16_t>(p);
    f.net_ip = address->label;
    std::string err;
    log_failure(open_net_games(f, &err), err);
    f.request = FrontendRequest::ProfilesChanged;
}

std::string net_password(Window& win) {
    const Widget* w = find_net(win, Role::NetPassword);
    return w ? w->label : std::string();
}

void do_action(Frontend& f, Widget& w) {
    std::string err;
    switch (w.action) {
        case Action::Exit: f.request = FrontendRequest::Quit; break;
        case Action::Settings: log_failure(open_options(f, &err), err); break;
#ifdef __EMSCRIPTEN__
        case Action::Network:
            message(f, ours(f, "network.not_available"));
#else
        case Action::Network:
            log_failure(open_net_player(f, &err), err);
#endif
            break;
        case Action::Medal: f.request = FrontendRequest::PlayCredits; break;
        case Action::Museum:
            f.museum_wait = true;
            museum_poll(f);
            break;
        case Action::MuseumBack:
            f.mus_pictures.reset();
            close_top(f);
            break;
        case Action::MuseumNation:
        case Action::MuseumType:
            if (w.action == Action::MuseumNation) {
                museum_set_nation(f.museum, f.mus, w.sel);
            } else {
                museum_set_type(f.museum, f.mus, w.sel);
            }
            museum_refresh(f, f.stack.back(), true);
            break;
        case Action::MuseumPick:
            museum_pick_row(f.museum, f.mus, w.sel);
            museum_refresh(f, f.stack.back(), false);
            break;
        case Action::MuseumNext:
            museum_next_picture(f.museum, f.mus);
            museum_refresh(f, f.stack.back(), false);
            break;
        case Action::MuseumPrev:
            museum_prev_picture(f.museum, f.mus);
            museum_refresh(f, f.stack.back(), false);
            break;
        case Action::Load: f.request = FrontendRequest::OpenLoad; break;
        case Action::SinglePlayer: log_failure(open_profiles(f, &err), err); break;
        case Action::ProfileAdd: {
            Window& win = f.stack.back();
            if (Widget* edit = find_kind(win, Kind::Edit)) profile_add(f.profiles, edit->label);
            profiles_refresh(f, win, false);
            break;
        }
        case Action::ProfileDelete: {
            Window& win = f.stack.back();
            profile_delete(f.profiles);
            profiles_refresh(f, win, true);
            break;
        }
        case Action::ProfileSelect: {
            Window& win = f.stack.back();
            const Widget* edit = find_kind(win, Kind::Edit);
            profile_select(f.profiles, w.sel, edit ? edit->label : std::string());
            profiles_refresh(f, win, true);
            break;
        }
        case Action::ProfileContinue: {
            Window& win = f.stack.back();
            if (const Widget* edit = find_kind(win, Kind::Edit)) profile_add(f.profiles, edit->label);
            close_top(f);
            log_failure(open_single_player(f, &err), err);
            f.request = FrontendRequest::ProfilesChanged;
            break;
        }
        case Action::ProfileBack:
            close_top(f);
            f.request = FrontendRequest::ProfilesChanged;
            break;
        case Action::SpCampaigns: f.request = FrontendRequest::OpenCampaigns; break;
        case Action::SpMissions: f.request = FrontendRequest::OpenMissions; break;
        case Action::SpRandomMaps: log_failure(open_skirmish(f, &err), err); break;
        case Action::SpBack:
            close_top(f);
            log_failure(open_profiles(f, &err), err);
            break;
        case Action::SkPlayers: {
            Window& win = f.stack.back();
            skirmish_set_players(f.sk, w.sel + 2);
            sync_skirmish(f, win);
            show_map(f, win);
            break;
        }
        case Action::SkResources: skirmish_set_resources(f.sk, w.sel); break;
        case Action::SkRules:  // ours: kept in the settings file, so the next start offers it again
            f.sk.plus = f.plus = f.settings.rules_plus = w.sel == 1;
            f.request = FrontendRequest::SettingsChanged;
            break;
        case Action::SkRow: {
            Window& win = f.stack.back();
            switch (w.role) {
                case Role::Level: skirmish_set_level(f.sk, w.row, f.net_host && w.sel == 4 ? 5 : w.sel); break;
                case Role::Nation: skirmish_set_nation(f.sk, w.row, w.sel); break;
                case Role::Colour: skirmish_set_colour(f.sk, w.row, w.sel); break;
                case Role::Team: skirmish_set_team(f.sk, w.row, w.sel); break;
                default: break;
            }
            sync_skirmish(f, win);
            break;
        }
        case Action::SkPrev:
            if (f.sk.map > 0) --f.sk.map;
            show_map(f, f.stack.back());
            break;
        case Action::SkNext:
            if (map_at(f.maps, f.sk.players, f.sk.map + 1)) ++f.sk.map;
            show_map(f, f.stack.back());
            break;
        case Action::SkStart: skirmish_start(f); break;
        case Action::SkBack: close_top(f); break;
        case Action::MessageClose: close_top(f); break;
        case Action::GameMessageClose: f.request = FrontendRequest::MessageClosed; break;
        case Action::MissionSelect: mission_show(f, f.stack.back(), w.sel); break;
        case Action::MissionLoad: {
            const Widget* list = find(f.stack.back(), Kind::TextList, 0);
            if (!list || list->sel < 0 || list->sel >= static_cast<int>(f.missions.size())) break;
            f.chosen_mission = f.missions[static_cast<std::size_t>(list->sel)];
            SDL_Log("front-end: Load mission %s", f.chosen_mission.path.c_str());
            f.request = FrontendRequest::StartMission;
            break;
        }
        case Action::MissionBack:
            close_top(f);
            if (f.campaign_list) f.request = FrontendRequest::OpenCampaigns;
            break;
        case Action::CampaignSelect:
            campaign_show(f, f.stack.back(), w.sel);
            f.request = FrontendRequest::CampaignSelected;
            break;
        case Action::CampaignApply:
            if (f.campaign_sel >= 0) f.request = FrontendRequest::OpenCampaignMissions;
            break;
        case Action::CampaignMenu: close_top(f); break;
        case Action::EndClose: f.request = FrontendRequest::EndMessageClosed; break;
        case Action::StatPlayer:
            f.stat_sel = static_cast<std::size_t>(w.row);
            stat_fill(f, f.stack.back());
            break;
        case Action::StatClose:
            f.end_players.clear();
            log_failure(open_main(f, &err), err);
            f.request = FrontendRequest::StatisticsClosed;
            break;
        case Action::MusicToggle: f.edit.music_on = w.value; break;
        case Action::SoundToggle: f.edit.sound_on = w.value; break;
        case Action::SoundVolume: f.edit.sound_volume = volume_from_slider(w.pos); break;
        case Action::OptionsOk: {
            const Window& win = f.stack.back();
            const int sel = find(win, Kind::DropList, 0)->sel;
#ifndef __EMSCRIPTEN__
            f.edit.fullscreen = sel == kFullScreenSel;  // ours: the sixth item; the size stays for the window
#endif
            if (sel != kFullScreenSel) f.edit.resolution = static_cast<std::uint8_t>(sel);
            f.edit.speed_ms = speed_from_slider(find(win, Kind::Slider, 2)->pos);
            f.edit.music_volume = volume_from_slider(find(win, Kind::Slider, 0)->pos);
            f.settings = f.edit;
            close_top(f);
            f.request = FrontendRequest::SettingsChanged;
            break;
        }
        case Action::OptionsCancel: close_top(f); break;
        case Action::GameOptions: log_failure(open_game_options(f, &err), err); break;
        case Action::GameOptionsOk: {
            const Window& win = f.stack.back();
            Settings& s = f.settings;
            s.mouse_scroll = scroll_from_slider(find(win, Kind::Slider, 0)->pos);
            s.keyboard_scroll = scroll_from_slider(find(win, Kind::Slider, 1)->pos);
            const int g = f.game_advisors ? 1 : 0;
            s.landscape_filter = find(win, Kind::CheckBox, 0)->value;
            if (g == 0) s.short_anims = find(win, Kind::CheckBox, 1)->value;
            s.reflections = find(win, Kind::CheckBox, 2 - g)->value;
            for (int k = 0; k < 6; ++k) s.economic[k] = find(win, Kind::CheckBox, 3 - g + k)->value;
            for (int k = 0; k < 4; ++k) s.military[k] = find(win, Kind::CheckBox, 9 - g + k)->value;
            if (g == 1) {  // in a game the switches are your player's in the game: act_a53f0, act_a5380
                GameAdvisors a;
                for (int k = 0; k < 6; ++k) a.economic[k] = s.economic[k] ? 1 : 0;
                for (int k = 0; k < 4; ++k) a.military[k] = s.military[k] ? 1 : 0;
                a.conduct = static_cast<std::uint8_t>(std::max(0, find(win, Kind::MuseumCombo, 0)->sel));
                f.advisors_chosen = f.game_advisors = a;
            }
            s.ui_scale = static_cast<std::uint8_t>(find(win, Kind::Choice, 0)->sel);  // ours
            s.ui_pictures = static_cast<std::uint8_t>(find(win, Kind::Choice, 1)->sel);
            s.world_zoom = static_cast<std::uint8_t>(find(win, Kind::Choice, 2)->sel);
            for (const Widget& c : win.widgets) {  // ours: the network rows (from the menu only), by their rows
                if (c.kind != Kind::Choice) continue;
                if (c.y == kNetAutosaveY) {
                    s.net_autosave =
                        static_cast<std::uint8_t>(net_minutes_offered(kNetAutosave, s.net_autosave)[c.sel]);
                } else if (c.y == kNetSeatHoldY) {
                    s.net_seat_hold =
                        static_cast<std::uint8_t>(net_minutes_offered(kNetSeatHold, s.net_seat_hold)[c.sel]);
                } else if (c.y == kNetAiY) {
                    s.net_ai_level = static_cast<std::uint8_t>(c.sel);
                }
            }
            for (const Widget& c : win.widgets) {  // ours: the updater's row (docs/download.md), the sim thread's
                if (c.kind == Kind::Choice && c.y == kUpdateRowY) s.check_updates = c.sel == 0;
                if (c.kind == Kind::Choice && c.y == kSimThreadY) s.sim_thread = static_cast<std::uint8_t>(c.sel);
                if (c.kind == Kind::Choice && c.y == kLanguageY && c.sel >= 0 &&
                    c.sel < static_cast<int>(f.lang_codes.size()) &&
                    f.lang_codes[static_cast<std::size_t>(c.sel)] != language(f).info.code) {
                    s.language = f.lang_codes[static_cast<std::size_t>(c.sel)];  // unchanged: still "follow the system"
                }
            }
            // The Options dialog below keeps showing its own values; these are stored at once.
            f.edit.mouse_scroll = s.mouse_scroll, f.edit.keyboard_scroll = s.keyboard_scroll;
            f.edit.landscape_filter = s.landscape_filter, f.edit.short_anims = s.short_anims;
            f.edit.reflections = s.reflections, f.edit.economic = s.economic, f.edit.military = s.military;
            f.edit.ui_scale = s.ui_scale, f.edit.ui_pictures = s.ui_pictures, f.edit.world_zoom = s.world_zoom;
            f.edit.check_updates = s.check_updates;
            f.edit.net_autosave = s.net_autosave, f.edit.net_seat_hold = s.net_seat_hold;
            f.edit.net_ai_level = s.net_ai_level, f.edit.sim_thread = s.sim_thread, f.edit.language = s.language;
            close_top(f);
            f.request = FrontendRequest::SettingsChanged;
            break;
        }
        case Action::GameOptionsCancel: close_top(f); break;
        case Action::GameMenu: {  // a button of the in-game menu
            const auto item = static_cast<GameMenuItem>(w.row);
            if (const std::string why = game_menu_unavailable(language(f), item, f.can_restart); !why.empty()) {
                message(f, why);  // ours: Save, Load, and Restart without a skirmish setup
                break;
            }
            switch (item) {
                case GameMenuItem::Save: f.request = FrontendRequest::OpenSave; break;
                case GameMenuItem::Load: f.request = FrontendRequest::OpenLoad; break;
                case GameMenuItem::Continue: f.request = FrontendRequest::GameMenuClosed; break;
                case GameMenuItem::Settings: log_failure(open_options(f, &err), err); break;
                case GameMenuItem::Restart:
                case GameMenuItem::BackToMenu:
                    log_failure(open_confirm(f, item, &err), err);
                    break;
                default: break;
            }
            break;
        }
        case Action::ConfirmYes: {
            const GameMenuItem item = f.confirm_item;
            close_top(f);
            if (item == GameMenuItem::Restart) {
                f.request = FrontendRequest::Restart;  // the app builds the game again
            } else if (item == GameMenuItem::BackToMenu) {
                log_failure(open_main(f, &err), err);
                f.request = FrontendRequest::BackToMenu;
            } else if (item == GameMenuItem::QuickLoad) {
                f.request = FrontendRequest::QuickLoad;
            }
            break;
        }
        case Action::ConfirmNo:
            if (f.stack.size() > 1) {
                close_top(f);
            } else {
                f.request = FrontendRequest::GameMenuClosed;
            }
            break;
        case Action::FileSelect: {
            if (w.sel < 0 || w.sel >= static_cast<int>(w.strs.size())) break;
            Window& win = f.stack.back();
            if (win.screen == Screen::SaveGame) {
                if (Widget* edit = find_kind(win, Kind::Edit))
                    edit_set_text(f, *edit, save_name_clip(w.strs[w.sel])), edit_focus(*edit);
            } else if (w.sel < static_cast<int>(f.saves.size())) {
                const SaveEntry& e = f.saves[static_cast<std::size_t>(w.sel)];
                for (Widget& l : win.widgets) {
                    if (l.kind == Kind::Label && l.row >= 0 && l.row < 5)
                        relabel(f, l, e.info[l.row], kStatText, "load.info");
                }
                if (Widget* image = find_role(win, Role::Preview)) {
                    image->pic = Picture{};
                    if (e.preview) {
                        if (auto mm = e.preview()) set_minimap_picture(f, *image, *mm, e.name);
                    }
                }
            }
            break;
        }
        case Action::FileLoad: {
            const Widget* list = find(f.stack.back(), Kind::TextList, 0);
            if (!list || list->sel < 0 || list->sel >= static_cast<int>(f.saves.size())) break;
            f.chosen = f.saves[list->sel];
            SDL_Log("front-end: Load %s", f.chosen.path.c_str());
            f.request = FrontendRequest::LoadGame;
            break;
        }
        case Action::FileSave: {
            const Widget* edit = find(f.stack.back(), Kind::Edit, 0);
            f.save_name = edit ? save_name_clip(edit->label) : std::string();
            if (f.save_name.empty()) break;  // ours: no file without a name
            f.request = FrontendRequest::SaveGame;
            break;
        }
        case Action::FileBack:
            if (f.file_in_game || f.stack.back().screen == Screen::SaveGame) {
                f.request = FrontendRequest::GameMenuClosed;
            } else {
                log_failure(open_main(f, &err), err);
            }
            break;
        case Action::MissionClose:
        case Action::DiplomacyCancel:
            f.request = FrontendRequest::GameMenuClosed;
            break;
        case Action::HintsClose: f.request = FrontendRequest::HintsClosed; break;
        case Action::HintsStartup: f.hints_startup = w.value; break;
        case Action::HintsPrev:
        case Action::HintsNext: {
            const bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
            const int count = static_cast<int>(f.hints.size()) + 1;
            f.hint_index = w.action == Action::HintsNext ? hint_next(f.hint_index, count, shift)
                                                         : hint_prev(f.hint_index, count, shift);
            log_failure(open_hints(f, &err), err);  // `w` is gone now
            break;
        }
        case Action::DiplomacyCheck:
            for (DiplomacyRow& r : f.diplomacy) {
                if (r.player == w.row) r.our_war = w.value;
            }
            break;
        case Action::DiplomacyOk: f.request = FrontendRequest::DiplomacyOk; break;
        case Action::NetContinue: net_continue(f); break;
        case Action::NetReturn: close_top(f); break;
        case Action::NetField: break;
        case Action::NetGameSelect:
            if (w.sel >= 0 && w.sel < static_cast<int>(f.net_rows.size())) {
                f.net_selected = f.net_rows[static_cast<std::size_t>(w.sel)].address;
            }
            net_games_refresh(f, f.stack.back());
            break;
        case Action::NetJoin:
            if (f.net_selected.empty()) {
                message(f, ours(f, "net.choose_game"));
                break;
            }
            f.net.address = f.net_selected;
            f.net.password = net_password(f.stack.back());
            f.net.resume.clear();
            for (const NetGameRow& r : f.net_rows) {  // slice MP9: a saved game's row: host it again
                if (r.address == f.net_selected && !r.save.empty()) f.net.resume = r.save;
            }
            f.request = FrontendRequest::NetJoin;
            break;
        case Action::NetCreate:
                                 // frontend_net_scenarios)
            f.net.password = net_password(f.stack.back());
            f.request = FrontendRequest::NetScenarios;
            break;
        case Action::NetScenarioSelect: net_scenario_show(f, f.stack.back(), w.sel); break;
        case Action::NetScenarioCreate: {
            const Widget* list = find(f.stack.back(), Kind::TextList, 0);
            const int sel = list ? list->sel : -1;
            const std::string map = sel >= 0 && sel < static_cast<int>(f.scenarios.size())
                                        ? f.scenarios[static_cast<std::size_t>(sel)].map
                                        : std::string();
            if (!open_skirmish(f, &err, true)) {
                log_failure(false, err);
                break;
            }
            if (map.empty()) break;  // no scenario: the first map, as Create on page 2 opened it before
            f.sk.map = map_index(f.maps, f.sk.players, map);
            if (f.sk.map < 0) {
                SDL_Log("front-end: the scenario's map %s is not in the map list", map.c_str());
                f.sk.map = 0;
            }
            show_map(f, f.stack.back());
            break;
        }
        case Action::NetHostCreate: {
            const MapEntry* m = map_at(f.maps, f.sk.players, f.sk.map);
            if (!m) break;
            const std::uint32_t seed =
                f.seed ? *f.seed : static_cast<std::uint32_t>(SDL_rand_bits() ^ SDL_GetTicksNS());
            f.setup = skirmish_setup(f.sk, m->file, seed, f.settings);
            SDL_Log("front-end: Create, %s", describe(f.setup).c_str());
            f.request = FrontendRequest::NetHost;
            break;
        }
        case Action::NetSpeed: f.net.tick_ms = kNetSpeeds[std::clamp(w.sel, 0, 3)]; break;
        case Action::NetStart: f.request = FrontendRequest::NetStart; break;
        case Action::NetLeave: f.request = FrontendRequest::NetLeave; break;
        case Action::NetSeat: f.net_seat = w.row, f.request = FrontendRequest::NetSeat; break;
        case Action::None: break;
    }
}

// ---- input ----

bool inside(const Widget& w, int x, int y) { return x >= w.x && x < w.x + w.w && y >= w.y && y < w.y + w.h; }



void list_close(Widget& w) {
    if (!w.below) w.y += static_cast<int>(w.items.size()) * w.items[0].h, w.below = true;
    w.open = false;
    w.h = w.items[0].h;
}

void list_event(Frontend& f, Window& win, Widget& w, int ev, int mx, int my) {
    const int n = static_cast<int>(w.items.size()), h0 = w.items[0].h;

    auto close = [&] {
        list_close(w);
        if (w.action != Action::None) do_action(f, w);
    };
    switch (ev) {
        case 0:  // button down
            if (!w.below || w.open) {
                close();
            } else {
                w.open = true;
                w.h = (n + 1) * h0;
                if (win.h < w.h + my) w.y -= n * h0, w.below = false;
            }
            break;
        case 1:  // button up
            if (w.open || !w.below) close();
            break;
        case 4:  // mouse move
            if (!w.open) break;
            if (!f.held) {
                close();
                break;
            }
            if (mx > w.x + w.field_w) break;
            {
                const int rel = my - w.y;
                const int k = w.below ? (rel > h0 ? (rel - h0) / h0 : -1) : (rel < n * h0 ? rel / h0 : -1);
                if (k >= 0) w.sel = std::min(k, n - 1);
            }
            break;
        default: break;
    }
}





void combo_event(Frontend& f, Widget& w, int ev, int mx, int my) {
    const int n = static_cast<int>(w.strs.size());
    if (n == 0) return;
    const int bx = w.x + w.w - w.down.w;  // the left edge of the button column (local_8)
    const int ly = w.y + w.row_h;         // the top of the open list
    const int visible = std::max(1, w.list_h / w.row_h);
    switch (ev) {
        case 0:
            if (!w.open) {
                w.open = true;
                w.h = w.row_h + w.list_h;
            } else if (mx > bx && mx < bx + w.up.w && my > ly && my < ly + w.up.h) {
                w.top = std::max(0, w.top - 1);
            } else if (mx > bx && mx < bx + w.down.w && my > ly + w.list_h - w.down.h && my < ly + w.list_h) {
                w.top = std::min(w.top + 1, n - 1);
                if (n < w.top + visible) w.top = std::max(n - visible, 0);
            }
            break;
        case 4:
            if (w.open && mx < bx && my - ly >= 0) w.sel = std::min((my - ly) / w.row_h + w.top, n - 1);
            break;
        case 1:
            if (w.open && mx < bx) {
                combo_close(w);
                do_action(f, w);
            }
            break;
        default: break;
    }
}





void museum_combo_event(Frontend& f, Widget& w, int ev, int mx, int my) {
    const int n = static_cast<int>(w.strs.size());
    if (n == 0) return;
    const int bx = w.x + w.w - w.up.w;  // the left edge of the scroll column (local_8)
    const int ly = w.y + w.row_h;       // the top of the open list
    switch (ev) {
        case 0:
            if (!w.open) {
                w.open = true;
                w.h = w.row_h + w.list_h;
            } else if (mx > bx && mx < bx + w.up.w && my > ly && my < ly + w.up.h) {
                w.top = combo_scroll_up(w.top);
            } else if (mx > bx && mx < bx + w.down.w && my > ly + w.list_h - w.down.h && my < ly + w.list_h) {
                w.top = combo_scroll_down(w.top, n, w.list_h, w.row_h);
            } else {
                if (mx > bx && mx < bx + w.up.w) {  // the track (a press on the box's own corner above the list too)
                    w.top = combo_track_top(my - ly, n, w.list_h, w.row_h);
                    ++f.clicks;
                }
                museum_combo_thumb(w);
                break;
            }
            museum_combo_thumb(w);
            ++f.clicks;
            break;
        case 4:
            if (w.open && mx < bx && my - ly >= 0) {
                const int before = w.sel;
                w.sel = std::min((my - ly) / w.row_h + w.top, n - 1);
                museum_combo_thumb(w);
                if (w.sel != before) ++f.clicks;
            }
            break;
        case 1:
            if (w.open && mx < bx) {
                combo_close(w);
                museum_combo_thumb(w);
                ++f.clicks;
                do_action(f, w);
            }
            break;
        default: break;
    }
}



void text_list_press(Frontend& f, Widget& w, int mx, int my) {
    const int lx = mx - w.x, ly = my - w.y;
    const Rect up = list_up_rect(w), down = list_down_rect(w);
    if (in_rect(down, lx, ly)) {
        w.top = std::min(w.top + 1, list_max_top(w));
    } else if (in_rect(up, lx, ly)) {
        w.top = std::max(w.top - 1, 0);
    } else if (lx >= up.x && lx < up.x + up.w && ly >= up.y && ly < down.y + down.h) {
        const int span = down.y + down.h - up.y;
        w.top = std::clamp(((ly - up.y) * list_max_top(w) + span / 2) / span, 0, list_max_top(w));
    } else if (ly >= w.list_oy) {
        const int k = (ly - w.list_oy) / w.list_row + w.top;
        const bool row = k < static_cast<int>(w.strs.size());
        int tw = 0;
        if (row && w.item_text[k]) TTF_GetTextSize(w.item_text[k].get(), &tw, nullptr);
        if (row && lx - w.list_ox <= tw) {
            w.sel = k;
            do_action(f, w);
        }
    }
}




void textbox_event(Frontend& f, Widget& w, int ev, int mx, int my) {
    const int count = static_cast<int>(w.strs.size()), height = w.tb_bottom - w.oy;
    const int bx = mx - w.x, by = my - w.y;
    const std::array<int, 4> track{w.tb_up[0], w.tb_up[3] + 1, w.tb_up[2], w.tb_down[1] - 1};
    auto in = [&](const std::array<int, 4>& r) { return bx > r[0] && bx < r[2] && by > r[1] && by < r[3]; };
    if (ev == 0 && count != 0) {
        if (in(w.tb_up)) {
            w.top = std::min(std::max(0, w.top - 1), textbox_max_top(count, height, kCell18));
            ++f.clicks;
        } else if (in(w.tb_down)) {
            w.top = textbox_down(w.top, count, height, kCell18);
            ++f.clicks;
        } else if (in(track)) {
            w.top = textbox_track_top(by, count, height, kCell18, track[1], track[3]);
            ++f.clicks;
            const int ty = textbox_thumb_y(w.top, count, height, kCell18, track[1], track[3], w.knob.h);
            if (in({track[0], ty, track[2], ty + w.knob.h})) w.drag = true;
        }
    } else if (ev == 1 || ev == 7) {
        w.drag = false;
    } else if (ev == 4 && w.drag) {
        w.top = textbox_track_top(by, count, height, kCell18, track[1], track[3]);
    }
}

// One mouse event for the widget: 0 down, 1 up, 4 move over it, 7 the mouse left it (the event numbers of the
// original's widget handlers).
void widget_event(Frontend& f, Window& win, Widget& w, int ev, int mx, int my) {
    switch (w.kind) {
        case Kind::Button:
            if (ev == 0) {
                w.state = 1, w.pressed = true;
            } else if (ev == 1 && w.pressed) {
                w.state = 2, w.pressed = false;
                ++f.clicks;  // the original plays its click before the callback (PlaySoundEvent(1, 0, ...))
                do_action(f, w);
            } else if (ev == 4) {
                if (!w.pressed && !f.held) {
                    w.state = 2;
                } else {
                    w.state = 1, w.pressed = true;
                }
            } else if (ev == 7) {
                w.state = 0, w.pressed = false;
            }
            break;
        case Kind::CheckBox:
            if (ev == 0 && w.enabled) {
                w.value = !w.value;
                ++f.clicks;
                do_action(f, w);
            }
            break;
        case Kind::Slider: {
            const float before = w.pos;
            if (ev == 0) {
                w.drag = true;
            } else if (ev == 1 && w.drag) {
                w.drag = false;
                w.pos = slider_value_at(mx, w.x, w.w);
                if (w.pos != before) do_action(f, w);
            } else if (ev == 4 && w.drag) {
                if (!f.held) {
                    w.drag = false;
                } else {
                    w.pos = slider_value_at(mx, w.x, w.w);
                    if (w.pos != before) do_action(f, w);
                }
            } else if (ev == 7) {
                w.drag = false;
            }
            break;
        }
        case Kind::DropList:
            if (ev == 0) ++f.clicks;
            list_event(f, win, w, ev, mx, my);
            break;
        case Kind::Combo:
            if (ev == 0) ++f.clicks;
            combo_event(f, w, ev, mx, my);
            break;
        case Kind::MuseumCombo: museum_combo_event(f, w, ev, mx, my); break;
        case Kind::Edit:
            if (ev == 0) edit_focus(w);
            break;
        case Kind::TextList:
            if (ev == 0) ++f.clicks, text_list_press(f, w, mx, my);
            break;
        case Kind::Choice:  // ours: a press shows the next value
            if (ev == 0 && !w.strs.empty()) w.sel = (w.sel + 1) % static_cast<int>(w.strs.size());
            break;
        case Kind::TextBox: textbox_event(f, w, ev, mx, my); break;  // slice I8
        case Kind::Image:
        case Kind::SlideShow:
        case Kind::Label: break;
    }
}

bool is_open(const Widget& w) {
    return (w.kind == Kind::DropList || w.kind == Kind::Combo || w.kind == Kind::MuseumCombo) && w.open;
}
bool hittable(const Widget& w) {
    return w.visible && w.enabled && w.kind != Kind::Label && w.kind != Kind::Image && w.kind != Kind::SlideShow;
}

// An open list or combo box has the focus in the original (it is drawn last and takes the events first), so it wins
// over whatever lies below its list.
int widget_at(const Window& win, int x, int y) {
    for (int i = static_cast<int>(win.widgets.size()) - 1; i >= 0; --i) {
        const Widget& w = win.widgets[i];
        if (hittable(w) && is_open(w) && inside(w, x, y)) return i;
    }
    for (int i = static_cast<int>(win.widgets.size()) - 1; i >= 0; --i) {
        const Widget& w = win.widgets[i];
        if (hittable(w) && inside(w, x, y)) return i;
    }
    return -1;
}

}  // namespace

FrontendPtr frontend_open(SDL_Renderer* renderer, GameFiles files, const std::vector<std::string>& font_files,
                          const Settings& settings, const Profiles& profiles, std::optional<std::uint32_t> seed,
                          std::string* error, Language* lang) {
    FrontendPtr f(std::make_unique<Frontend>().release());
    f->lang = lang;
    f->renderer = renderer;
    f->files = std::move(files);
    f->settings = settings;
    f->plus = settings.rules_plus;
    f->profiles = profiles;
    f->seed = seed;
    if (!TTF_Init()) {
        *error = std::string("SDL_ttf: ") + SDL_GetError();
        return nullptr;
    }
    for (const std::string& path : font_files) {
        f->font18 = open_gdi_font(path, 18);
        f->font16 = open_gdi_font(path, 16);
        if (f->font18 && f->font16) {
            f->font_path = path;
            SDL_Log("menu font: %s, em %g and %g", path.c_str(), static_cast<double>(TTF_GetFontSize(f->font18)),
                    static_cast<double>(TTF_GetFontSize(f->font16)));
            break;
        }
    }
    if (!f->font18 || !f->font16) {
        *error = "no menu font could be opened (Times New Roman Bold or Liberation Serif Bold)";
        return nullptr;
    }
    f->engine = TTF_CreateRendererTextEngine(renderer);
    if (!f->engine) {
        *error = std::string("SDL_ttf text engine: ") + SDL_GetError();
        return nullptr;
    }
    make_corner_texts(*f);
    f->cursor = make_text_object(*f, "|", kOrange);
    if (SDL_Window* window = SDL_GetRenderWindow(renderer)) {  // the edit field takes typed characters
        SDL_StartTextInput(window);
        f->text_window = window;
    }
    auto menu = f->files("Data/Menu.txt", error);
    if (!menu) return nullptr;
    auto windows = parse_menu_txt(*menu, error);
    if (!windows) return nullptr;
    f->menu = std::move(*windows);
    auto st = f->files("Data/StringTable_eng.st", error);
    if (!st) return nullptr;
    auto strings = parse_string_table(*st, error);
    if (!strings) {
        *error = "Data/StringTable_eng.st: " + *error;
        return nullptr;
    }
    f->strings_en = std::move(*strings);
    f->strings = f->strings_en;
    lang_localize(language(*f), f->strings);
    if (!open_main(*f, error)) return nullptr;
    return f;
}

void frontend_language_changed(Frontend& f) {
    f.strings = f.strings_en;
    lang_localize(language(f), f.strings);
    make_corner_texts(f);
    // the painted words (docs/languages.md 3): the open windows' pictures that have some are loaded again
    auto repaint = [&f](Picture& p) {
        if (p.name.empty()) return;
        std::string err;
        auto a = open_archive(f, p.archive, &err);
        auto again = a ? load_picture(f, *a, p.name, p.keyed, &err) : std::nullopt;
        if (again) {
            p = std::move(*again);
        } else {
            SDL_Log("painted words: %s", err.c_str());
        }
    };
    for (Window& win : f.stack) {
        repaint(win.back);
        for (Widget& w : win.widgets) {
            repaint(w.pic);
            for (Picture& p : w.items) repaint(p);  // ours: the resolution picker's Full screen
        }
    }
}

FrontendRequest frontend_event(Frontend& f, const SDL_Event& e) {
    f.request = FrontendRequest::None;
    if (f.stack.empty()) return f.request;


    const std::size_t top = f.stack.size() - 1;
    auto local = [&](float x, float y) {
        return std::pair<int, int>{static_cast<int>(x) - f.stack[top].x, static_cast<int>(y) - f.stack[top].y};
    };
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION: {
            f.mouse_x = e.motion.x, f.mouse_y = e.motion.y;
            Window& win = f.stack[top];
            if (win.drag.on && f.held) {
                const auto [nx, ny] = window_drag_to(win.drag, win.w, win.h, f.canvas_w, f.canvas_h,
                                                     static_cast<int>(e.motion.x), static_cast<int>(e.motion.y));
                win.x = nx, win.y = ny;
                break;
            }
            win.drag.on = false;  // a move without the button ends it
            const auto [mx, my] = local(e.motion.x, e.motion.y);
            for (std::size_t i = 0; i < win.widgets.size() && top < f.stack.size(); ++i) {
                Widget& w = win.widgets[i];
                if (w.kind == Kind::Label || w.kind == Kind::Image) continue;
                if (hittable(w) && inside(w, mx, my)) {
                    widget_event(f, win, w, 4, mx, my);
                } else if (w.state != 0 || w.pressed || w.drag) {
                    widget_event(f, win, w, 7, mx, my);
                }
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (e.button.button != SDL_BUTTON_LEFT) break;
            const bool down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            const SDL_FPoint at{e.button.x, e.button.y};
            if (f.update_link && f.stack.size() == 1 && f.stack[0].screen == Screen::Main &&
                SDL_PointInRectFloat(&at, &f.update_rect)) {  // ours: "Check for updates", on the release
                if (!down) ++f.clicks, f.request = FrontendRequest::CheckUpdates;
                break;
            }
            f.held = down;
            f.mouse_x = e.button.x, f.mouse_y = e.button.y;
            const auto [mx, my] = local(e.button.x, e.button.y);
            Window& win = f.stack[top];
            const int i = widget_at(win, mx, my);
            if (down) {  // a press elsewhere takes the focus from an open list (its event 8 closes it, with its
                         // callback)
                for (int k = 0; k < static_cast<int>(win.widgets.size()); ++k) {
                    Widget& o = win.widgets[k];
                    if (k == i) continue;
                    if (o.kind == Kind::DropList && o.open) {
                        list_close(o);
                        if (o.action != Action::None) do_action(f, o);
                    } else if ((o.kind == Kind::Combo || o.kind == Kind::MuseumCombo) && o.open) {
                        combo_close(o);
                        do_action(f, o);
                    } else if (o.kind == Kind::Edit) {
                        o.focused = false;
                    }
                }
            }
            if (i >= 0) widget_event(f, win, win.widgets[i], down ? 0 : 1, mx, my);
            if (down && i < 0) {
                window_drag_press(win.drag, win.x, win.y, win.w, win.drag_h, static_cast<int>(e.button.x),
                                  static_cast<int>(e.button.y));
            } else if (!down && top < f.stack.size()) {
                f.stack[top].drag.on = false;
            }
            break;
        }
        case SDL_EVENT_KEY_DOWN: {
            if (e.key.repeat) break;
            const SDL_Keycode key = e.key.key == SDLK_KP_ENTER ? SDLK_RETURN : e.key.key;  // VK_RETURN for both
            Window& win = f.stack[top];
            for (Widget& w : win.widgets) {
                if (w.kind == Kind::Button && w.hotkey != 0 && w.visible && w.enabled &&
                    static_cast<SDL_Keycode>(hotkey_sdl_key(w.hotkey)) == key) {
                    do_action(f, w);
                    return f.request;  // the action may have closed this window
                }
            }
            if (key == SDLK_BACKSPACE) {
                for (Widget& w : f.stack[top].widgets) {
                    if (w.kind == Kind::Edit && w.focused && !w.label.empty()) {
                        std::size_t cut = w.label.size() - 1;
                        while (cut > 0 && (static_cast<unsigned char>(w.label[cut]) & 0xc0) == 0x80) --cut;
                        edit_set_text(f, w, w.label.substr(0, cut));
                    }
                }
            }
            break;
        }
        case SDL_EVENT_TEXT_INPUT: {
            for (Widget& w : f.stack[top].widgets) {
                if (w.kind != Kind::Edit || !w.focused) continue;
                std::string t = w.label + e.text.text;
                t.erase(std::remove_if(t.begin(), t.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; }),
                        t.end());
                edit_set_text(f, w,
                              w.action == Action::FileSave   ? save_name_clip(t)
                              : w.action == Action::NetField ? net_field_clip(t, w.max_chars)
                                                             : profile_name_clip(t));
            }
            break;
        }
        default: break;
    }
    return f.request;
}

namespace {

void blit(SDL_Renderer* r, const Picture& p, const SDL_FRect* src, float x, float y, float w, float h) {
    const SDL_FRect dst{x, y, w, h};
    SDL_RenderTexture(r, p.tex.get(), src, &dst);
}

// The picture state of a scroll button: pressed while the mouse is down over it, lit while it is over it.
int button_state(const Frontend& f, float x, float y, int w, int h) {
    const bool over = f.mouse_x >= x && f.mouse_x < x + static_cast<float>(w) && f.mouse_y >= y &&
                      f.mouse_y < y + static_cast<float>(h);
    return over ? (f.held ? 1 : 2) : 0;
}

void draw_shadowed(TTF_Text* t, float x, float y, SDL_Color c) {
    TTF_SetTextColor(t, 0, 0, 0, 255);
    TTF_DrawRendererText(t, x + 2, y + 1);
    TTF_SetTextColor(t, c.r, c.g, c.b, c.a);
    TTF_DrawRendererText(t, x, y);
}

// The slide show's step since it was made: one per frame of the original's loop, taken as one tick period (assumed,
// docs/frontend.md 4.10). The position is step / per_pic (step * 0.5 for a door); at step per_pic * n - 1 (the last
// picture's last frame) it ends, or a looping one starts again. Batch screenshots: a door is open, a loop at its start.
int door_step(const Frontend& f, const Widget& w) {
    const int end = w.per_pic * static_cast<int>(w.items.size()) - 1;
    if (!f.animate) return w.loop ? 0 : end;
    const std::uint64_t steps = (SDL_GetTicks() - w.start_ms) / static_cast<std::uint64_t>(w.step_ms);
    if (w.loop) return static_cast<int>(steps % static_cast<std::uint64_t>(end + 1));
    return static_cast<int>(std::min<std::uint64_t>(steps, static_cast<std::uint64_t>(end)));
}
bool door_open(const Frontend& f, const Widget& w) {
    return !w.loop && door_step(f, w) >= w.per_pic * static_cast<int>(w.items.size()) - 1;
}

void draw_widget(Frontend& f, const Window& win, const Widget& w) {
    SDL_Renderer* r = f.renderer;
    if (!w.visible) return;
    const float x = static_cast<float>(win.x + w.x), y = static_cast<float>(win.y + w.y);
    switch (w.kind) {
        case Kind::Button: {
            if (!w.pic.tex) break;  // slice MP9: the lobby's seat rows are buttons without a picture
            const float third = static_cast<float>(w.pic.w / 3);
            const SDL_FRect src{third * static_cast<float>(w.dimmed ? 0 : w.state), 0, third,
                                static_cast<float>(w.pic.h)};
            if (w.dimmed) SDL_SetTextureColorMod(w.pic.tex.get(), 128, 128, 128);  // ours: not in the engine yet
            blit(r, w.pic, &src, x, y, static_cast<float>(w.w), static_cast<float>(w.h));
            if (w.dimmed) SDL_SetTextureColorMod(w.pic.tex.get(), 255, 255, 255);
            break;
        }
        case Kind::Image:
            if (w.pic.tex)
                blit(r, w.pic, w.part ? &*w.part : nullptr, x, y, static_cast<float>(w.w), static_cast<float>(w.h));
            break;
        case Kind::SlideShow: {
            if (w.items.empty()) break;
            const auto k = std::min(static_cast<std::size_t>(door_step(f, w) / w.per_pic), w.items.size() - 1);
            const Picture& pic = w.items[k];
            if (pic.tex) blit(r, pic, nullptr, x, y, static_cast<float>(pic.w), static_cast<float>(pic.h));
            break;
        }
        case Kind::CheckBox: {
            const SDL_FRect src{static_cast<float>(w.value ? w.w : 0), 0, static_cast<float>(w.w),
                                static_cast<float>(w.h)};
            blit(r, w.pic, &src, x, y, static_cast<float>(w.w), static_cast<float>(w.h));
            break;
        }
        case Kind::Slider: {
            blit(r, w.pic, nullptr, x, y, static_cast<float>(w.w), static_cast<float>(w.h));
            const int kx = slider_knob_x(w.pos, w.x, w.w, w.knob.w);
            blit(r, w.knob, nullptr, static_cast<float>(win.x + kx), y, static_cast<float>(w.knob.w),
                 static_cast<float>(w.knob.h));
            break;
        }
        case Kind::DropList: {
            const int n = static_cast<int>(w.items.size()), h0 = w.items[0].h, half = w.items[0].w / 2;
            const float fw = static_cast<float>(w.field_w);
            const float field_y = w.below ? y : y + static_cast<float>(n * h0);
            const SDL_FRect left{0, 0, static_cast<float>(half), static_cast<float>(h0)};
            const SDL_FRect right{static_cast<float>(half), 0, static_cast<float>(half), static_cast<float>(h0)};
            blit(r, w.items[w.sel], &left, x, field_y, fw, static_cast<float>(h0));
            if (w.open) {
                const float list_y = w.below ? y + static_cast<float>(h0) : y;
                for (int k = 0; k < n; ++k) {
                    blit(r, w.items[k], k == w.sel ? &right : &left, x, list_y + static_cast<float>(k * h0), fw,
                         static_cast<float>(h0));
                }
                SDL_SetRenderDrawColor(r, 0, 0, 0, 255);  // the frame (GDI lines in the selected pen; black assumed)
                const SDL_FRect frame{x, list_y, fw, static_cast<float>(n * h0)};
                SDL_RenderRect(r, &frame);
            }
            // The button keeps its place right of the field where it was made.
            if (!w.list_button.tex) break;  // the colour pickers of the skirmish screen have none
            const int bw = w.list_button.w / 3;
            const float bx = x + fw, by = static_cast<float>(win.y) + static_cast<float>(w.below ? w.y : w.y + n * h0);
            const bool over = f.mouse_x > bx && f.mouse_x < bx + static_cast<float>(bw) && f.mouse_y > by &&
                              f.mouse_y < by + static_cast<float>(w.list_button.h);
            const int state = over ? (f.held ? 1 : 2) : 0;
            const SDL_FRect src{static_cast<float>(state * bw), 0, static_cast<float>(bw),
                                static_cast<float>(w.list_button.h)};
            blit(r, w.list_button, &src, bx, by, static_cast<float>(bw), static_cast<float>(w.list_button.h));
            break;
        }
        case Kind::Edit: {
            const float avail = static_cast<float>(w.w - 5 - w.ox);
            int tw = 0;
            if (w.text) TTF_GetTextSize(w.text.get(), &tw, nullptr);
            const float shift = static_cast<float>(tw) >= avail ? static_cast<float>(tw) - avail + 1 : 0;
            const SDL_Rect clip{static_cast<int>(x) + w.ox, static_cast<int>(y), static_cast<int>(avail), w.h};
            SDL_SetRenderClipRect(r, &clip);
            if (w.text)
                TTF_DrawRendererText(w.text.get(), x + static_cast<float>(w.ox) - shift, y + static_cast<float>(w.oy));
            // the bar after the text blinks while the field has the focus (the original counts frames; this runs
            // 250 ms off, 250 ms on)
            if (w.focused && f.show_cursor && f.cursor && ((SDL_GetTicks() - w.focus_ms) / 250) % 2 == 1) {
                TTF_DrawRendererText(f.cursor.get(), x + static_cast<float>(w.ox) - shift + static_cast<float>(tw),
                                     y + static_cast<float>(w.oy));
            }
            SDL_SetRenderClipRect(r, nullptr);
            break;
        }
        case Kind::TextList: {
            const SDL_Rect clip{static_cast<int>(x), static_cast<int>(y), w.w, w.h};
            SDL_SetRenderClipRect(r, &clip);
            for (int k = w.top; k < static_cast<int>(w.strs.size()); ++k) {
                const int ry = w.list_oy + w.list_row * (k - w.top);
                if (ry + kCell18 > w.h) break;
                if (w.item_text[k]) {
                    draw_shadowed(w.item_text[k].get(), x + static_cast<float>(w.list_ox),
                                  y + static_cast<float>(ry + item_dy(w, k)),
                                  k == w.sel                               ? kOrange
                                  : k < static_cast<int>(w.row_ink.size()) ? w.row_ink[static_cast<std::size_t>(k)]
                                                                           : kCream);
                }
            }
            SDL_SetRenderClipRect(r, nullptr);
            const Rect up = list_up_rect(w), down = list_down_rect(w);
            auto button = [&](const Picture& p, const Rect& rc) {
                const float bx = x + static_cast<float>(rc.x), by = y + static_cast<float>(rc.y);
                const SDL_FRect src{static_cast<float>(button_state(f, bx, by, rc.w, rc.h) * rc.w), 0,
                                    static_cast<float>(rc.w), static_cast<float>(rc.h)};
                blit(r, p, &src, bx, by, static_cast<float>(rc.w), static_cast<float>(rc.h));
            };
            button(w.up, up);
            button(w.down, down);
            // the thumb runs between the buttons with the first shown row
            const int y0 = up.y + up.h, y1 = down.y - w.knob.h;
            const int max_top = list_max_top(w);
            const int ty = max_top > 0 ? y0 + (y1 - y0) * w.top / max_top : y0;
            blit(r, w.knob, nullptr, x + static_cast<float>(w.w - w.knob.w), y + static_cast<float>(ty),
                 static_cast<float>(w.knob.w), static_cast<float>(w.knob.h));
            break;
        }
        case Kind::Combo: {
            const float rh = static_cast<float>(w.row_h), ww = static_cast<float>(w.w);
            const float half = static_cast<float>(w.pic.w / 2), ph = static_cast<float>(w.pic.h);
            const SDL_FRect left{0, 0, half, ph}, right{half, 0, half, ph};
            blit(r, w.pic, &left, x, y, ww, rh);
            blit(r, w.down, nullptr, x + ww - static_cast<float>(w.down.w), y, static_cast<float>(w.down.w), rh);
            const auto text_at = [&](int k, float ty) {
                if (k >= 0 && k < static_cast<int>(w.item_text.size()) && w.item_text[k]) {
                    TTF_DrawRendererText(w.item_text[k].get(), x + static_cast<float>(w.ox),
                                         ty + static_cast<float>(w.oy + item_dy(w, k)));
                }
            };
            text_at(w.sel, y);
            if (w.open) {
                const float ly = y + rh;
                const int rows = w.list_h / w.row_h;
                for (int k = 0; k < rows; ++k) {
                    const float ry = ly + static_cast<float>(k) * rh;
                    blit(r, w.pic, w.top + k == w.sel ? &right : &left, x, ry, ww, rh);
                    text_at(w.top + k, ry);
                }
                const float bw = static_cast<float>(w.up.w);  // the scroll column: bar, up arrow, down arrow
                blit(r, w.bar, nullptr, x + ww - bw, ly, bw, static_cast<float>(w.list_h));
                blit(r, w.up, nullptr, x + ww - bw, ly, bw, static_cast<float>(w.up.h));
                blit(r, w.down, nullptr, x + ww - static_cast<float>(w.down.w),
                     ly + static_cast<float>(w.list_h - w.down.h), static_cast<float>(w.down.w),
                     static_cast<float>(w.down.h));
                SDL_SetRenderDrawColor(r, 0, 0, 0, 255);  // the frame (GDI lines; its far corner is not clear)
                const SDL_FRect frame{x, ly, ww, static_cast<float>(w.list_h)};
                SDL_RenderRect(r, &frame);
            }
            break;
        }
        case Kind::MuseumCombo: {
                                   // list
            const float rh = static_cast<float>(w.row_h), ww = static_cast<float>(w.w);
            const float part = static_cast<float>(w.pic.w / 3), ph = static_cast<float>(w.pic.h);
            const SDL_FRect field{0, 0, part, ph}, row{part, 0, part, ph}, picked{2 * part, 0, part, ph};
            blit(r, w.pic, &field, x, y, ww, rh);
            blit(r, w.down, nullptr, x + ww - static_cast<float>(w.down.w), y, static_cast<float>(w.down.w), rh);
            const float ly = y + rh, lh = static_cast<float>(w.list_h);
            const int rows = combo_rows(w.list_h, w.row_h);
            if (w.open) {
                for (int k = 0; k < rows; ++k) {  // the rows, then the scroll column over their right end
                    blit(r, w.pic, w.top + k == w.sel ? &picked : &row, x, ly + static_cast<float>(k) * rh, ww, rh);
                }
                const float cx = x + ww - static_cast<float>(w.up.w);
                blit(r, w.bar, nullptr, cx, ly, static_cast<float>(w.bar.w), lh);
                blit(r, w.up, nullptr, cx, ly, static_cast<float>(w.up.w), static_cast<float>(w.up.h));
                blit(r, w.down, nullptr, cx, ly + lh - static_cast<float>(w.down.h), static_cast<float>(w.down.w),
                     static_cast<float>(w.down.h));
                if (w.thumb_on) {
                    blit(r, w.knob, nullptr, cx, static_cast<float>(win.y + w.thumb_y), static_cast<float>(w.knob.w),
                         static_cast<float>(w.knob.h));
                }
            }
            const auto text_at = [&](int k, float ty) {  // the text goes on after all the pictures (GDI on the DC)
                if (k >= 0 && k < static_cast<int>(w.item_text.size()) && w.item_text[static_cast<std::size_t>(k)]) {
                    TTF_DrawRendererText(w.item_text[static_cast<std::size_t>(k)].get(), x + static_cast<float>(w.ox),
                                         ty + static_cast<float>(w.oy + item_dy(w, k)));
                }
            };
            text_at(w.sel, y);
            if (w.open) {
                for (int k = 0; k < rows; ++k) text_at(w.top + k, ly + static_cast<float>(k) * rh);

                SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
                const SDL_FRect frame{x, ly, ww, lh};
                SDL_RenderRect(r, &frame);
            }
            break;
        }
        case Kind::Choice: {  // ours: the field stretched, the value centred with a shadow
            blit(r, w.pic, nullptr, x, y, static_cast<float>(w.w), static_cast<float>(w.h));
            if (w.sel < 0 || w.sel >= static_cast<int>(w.item_text.size()) || !w.item_text[w.sel]) break;
            int tw = 0, th = 0;
            TTF_GetTextSize(w.item_text[w.sel].get(), &tw, &th);
            draw_shadowed(w.item_text[w.sel].get(), x + static_cast<float>((w.w - tw) / 2),
                          y + static_cast<float>((w.h - th) / 2), SDL_Color{0xff, 0xf7, 0x99, 0xff});
            break;
        }
        case Kind::TextBox: {
            const int count = static_cast<int>(w.strs.size()), height = w.tb_bottom - w.oy;
            const int ty = textbox_thumb_y(w.top, count, height, kCell18, w.tb_up[3] + 1, w.tb_down[1] - 1, w.knob.h);
            if (w.knob.tex) {
                blit(r, w.knob, nullptr, x + static_cast<float>(w.tb_up[0]), y + static_cast<float>(ty),
                     static_cast<float>(w.knob.w), static_cast<float>(w.knob.h));
            }
            for (int k = w.top, dy = 0; k < count && dy < height; ++k, dy += kCell18) {
                if (!w.item_text[static_cast<std::size_t>(k)]) continue;
                if (w.shadow) {
                    draw_shadowed(w.item_text[static_cast<std::size_t>(k)].get(), x + static_cast<float>(w.ox),
                                  y + static_cast<float>(w.oy + dy), w.ink);
                } else {
                    TTF_DrawRendererText(w.item_text[static_cast<std::size_t>(k)].get(), x + static_cast<float>(w.ox),
                                         y + static_cast<float>(w.oy + dy));
                }
            }
            break;
        }
        case Kind::Label: {
            // A fitted text (ours, docs/languages.md 2.5): `text_dy` lower, its second line `text2_dy` below.
            const auto line = [&](TTF_Text* t, int dy) {
                if (!t) return;
                float tx = x, ty = y;
                if (w.centred) {
                    int tw = 0;
                    TTF_GetTextSize(t, &tw, nullptr);
                    tx = static_cast<float>(win.x + w.x + w.w / 2 - tw / 2 - 1);
                    ty = static_cast<float>(win.y + w.y + w.h / 2 - kCell18 / 2);
                }
                ty += static_cast<float>(dy);
                if (w.shadow) {
                    Uint8 cr = 0, cg = 0, cb = 0, ca = 0;
                    TTF_GetTextColor(t, &cr, &cg, &cb, &ca);
                    TTF_SetTextColor(t, 0, 0, 0, 255);
                    TTF_DrawRendererText(t, tx + 2, ty + 1);
                    TTF_SetTextColor(t, cr, cg, cb, ca);
                }
                TTF_DrawRendererText(t, tx, ty);
            };
            line(w.text.get(), w.text_dy);
            line(w.text2.get(), w.text_dy + w.text2_dy);
            break;
        }
    }
}

// A grown window (the Confirm window, docs/languages.md 2.4 rule 1): its picture down to `grow_at`, the rows of the
// text box above that repeated for `grow` pixels, then the rest of the picture.
void draw_grown(Frontend& f, const Window& win) {
    const float x = static_cast<float>(win.x), y = static_cast<float>(win.y), w = static_cast<float>(win.back.w);
    const int at = win.grow_at, band = 40;  // the band: the 40 rows above `at`, inside the text box
    const SDL_FRect top{0, 0, w, static_cast<float>(at)};
    blit(f.renderer, win.back, &top, x, y, w, static_cast<float>(at));
    for (int done = 0; done < win.grow; done += band) {
        const int h = std::min(band, win.grow - done);
        const SDL_FRect src{0, static_cast<float>(at - band), w, static_cast<float>(h)};
        blit(f.renderer, win.back, &src, x, y + static_cast<float>(at + done), w, static_cast<float>(h));
    }
    const SDL_FRect bottom{0, static_cast<float>(at), w, static_cast<float>(win.back.h - at)};
    blit(f.renderer, win.back, &bottom, x, y + static_cast<float>(at + win.grow), w,
         static_cast<float>(win.back.h - at));
}

// The full text of a fitted text that was cut or shortened, under the mouse (docs/languages.md 2.4 rule 2): the top
// window's label, list row or box value there.
const std::string* tip_under_mouse(const Frontend& f) {
    if (f.stack.empty()) return nullptr;
    const Window& win = f.stack.back();
    const float mx = f.mouse_x - static_cast<float>(win.x), my = f.mouse_y - static_cast<float>(win.y);
    auto in = [&](int x, int y, int w, int h) {
        return mx >= static_cast<float>(x) && mx < static_cast<float>(x + w) && my >= static_cast<float>(y) &&
               my < static_cast<float>(y + h);
    };
    auto item = [](const Widget& w, int k) -> const std::string* {
        if (k < 0 || k >= static_cast<int>(w.item_tips.size())) return nullptr;
        const std::string& t = w.item_tips[static_cast<std::size_t>(k)];
        return t.empty() ? nullptr : &t;
    };
    for (auto it = win.widgets.rbegin(); it != win.widgets.rend(); ++it) {
        const Widget& w = *it;
        if (!w.visible) continue;
        switch (w.kind) {
            case Kind::Label:
                if (!w.tip.empty() && in(w.x, w.y, w.w, std::max(w.h, kCell18))) return &w.tip;
                break;
            case Kind::TextList:
                if (in(w.x, w.y, w.w, w.h) && my >= static_cast<float>(w.y + w.list_oy)) {
                    const int k = w.top + static_cast<int>(my - static_cast<float>(w.y + w.list_oy)) / w.list_row;
                    if (const std::string* t = item(w, k)) return t;
                }
                break;
            case Kind::Choice:
                if (in(w.x, w.y, w.w, w.h)) return item(w, w.sel);
                break;
            case Kind::Combo:
            case Kind::MuseumCombo:
                if (in(w.x, w.y, w.w, w.row_h)) return item(w, w.sel);
                if (w.open && in(w.x, w.y + w.row_h, w.w, w.list_h)) {
                    return item(w, w.top + static_cast<int>(my - static_cast<float>(w.y + w.row_h)) / w.row_h);
                }
                break;
            default: break;
        }
    }
    return nullptr;
}

void draw_tip(Frontend& f) {
    const std::string* tip = tip_under_mouse(f);
    if (!tip) return;
    Text t(TTF_CreateText(f.engine, f.font16, tip->c_str(), 0));
    if (!t) return;
    TTF_SetTextWrapWidth(t.get(), 480);  // ours: a long tooltip (the rule set's) on a few lines
    int tw = 0, th = 0;
    TTF_GetTextSize(t.get(), &tw, &th);
    const float w = static_cast<float>(tw + 8), h = static_cast<float>(th + 4);
    const float x = std::clamp(f.mouse_x, 0.0f, std::max(0.0f, static_cast<float>(f.canvas_w) - w));
    float y = f.mouse_y + 22;
    if (y + h > static_cast<float>(f.canvas_h)) y = f.mouse_y - 4 - h;
    const SDL_FRect box{x, y, w, h};
    SDL_SetRenderDrawColor(f.renderer, 0x20, 0x18, 0x10, 255);
    SDL_RenderFillRect(f.renderer, &box);
    SDL_SetRenderDrawColor(f.renderer, kCream.r, kCream.g, kCream.b, 255);
    SDL_RenderRect(f.renderer, &box);
    TTF_SetTextColor(t.get(), kCream.r, kCream.g, kCream.b, 255);
    TTF_DrawRendererText(t.get(), x + 4, y + 2);
}

}  // namespace

void frontend_draw(Frontend& f) {
    if (f.museum_wait) museum_poll(f);

    for (Window& win : f.stack) {
        for (const Widget& w : win.widgets) {
            if (w.kind != Kind::SlideShow || !door_open(f, w)) continue;
            for (Widget& l : win.widgets) {
                if (l.kind == Kind::TextList) l.visible = true;
            }
            break;
        }
    }
    if (!frontend_overlay(f)) {
        SDL_SetRenderDrawColor(f.renderer, 0, 0, 0, 255);
        SDL_RenderClear(f.renderer);
    }
    for (const Window& win : f.stack) {
        if (win.grow > 0) {
            draw_grown(f, win);
        } else {
            blit(f.renderer, win.back, nullptr, static_cast<float>(win.x), static_cast<float>(win.y),
                 static_cast<float>(win.w), static_cast<float>(win.h));
        }
        for (const Widget& w : win.widgets) {
            if (!is_open(w)) draw_widget(f, win, w);
        }
        for (const Widget& w : win.widgets) {  // an open list has the focus: drawn last, over its neighbours
            if (is_open(w)) draw_widget(f, win, w);
        }
    }


    if (f.stack.size() == 1 && f.stack[0].screen == Screen::Main && f.version) {
        TTF_DrawRendererText(f.version.get(), 885, 20);
    }
    // Ours: the version of this build, small, in the main menu's bottom right corner (docs/release.md).
    if (f.stack.size() == 1 && f.stack[0].screen == Screen::Main && f.build_label) {
        int w = 0, h = 0;
        TTF_GetTextSize(f.build_label.get(), &w, &h);
        const Window& win = f.stack[0];
        TTF_DrawRendererText(f.build_label.get(), static_cast<float>(win.x + win.w - 10 - w),
                             static_cast<float>(win.y + win.h - 8 - h));
        if (f.update_link) {  // ours: "Check for updates" above it, white under the mouse
            int lw = 0, lh = 0;
            TTF_GetTextSize(f.update_link.get(), &lw, &lh);
            f.update_rect = {static_cast<float>(win.x + win.w - 10 - lw),
                             static_cast<float>(win.y + win.h - 12 - h - lh), static_cast<float>(lw),
                             static_cast<float>(lh)};
            const SDL_FPoint m{f.mouse_x, f.mouse_y};
            const bool over = SDL_PointInRectFloat(&m, &f.update_rect);
            TTF_SetTextColor(f.update_link.get(), 0xff, over ? 0xff : 0xf7, over ? 0xff : 0x99, 0xff);
            TTF_DrawRendererText(f.update_link.get(), f.update_rect.x, f.update_rect.y);
        }
    }
    draw_tip(f);  // ours: the full text of a cut one (docs/languages.md 2.4)
}

const Settings& frontend_settings(const Frontend& f) { return f.settings; }

void frontend_set_fullscreen(Frontend& f, bool on) {
    f.settings.fullscreen = f.edit.fullscreen = on;
    for (Window& win : f.stack) {  // the Options resolution picker: Full screen, or back to the size it showed
        if (win.screen != Screen::Options) continue;
        for (Widget& c : win.widgets) {
            if (c.kind != Kind::DropList || static_cast<int>(c.items.size()) <= kFullScreenSel) continue;
            if (on && c.sel != kFullScreenSel) {
                c.row = c.sel;
                c.sel = kFullScreenSel;
            } else if (!on && c.sel == kFullScreenSel) {
                c.sel = c.row >= 0 && c.row < kFullScreenSel ? c.row : f.edit.resolution;
            }
        }
    }
}

int frontend_take_clicks(Frontend& f) {
    const int n = f.clicks;
    f.clicks = 0;
    return n;
}
const Profiles& frontend_profiles(const Frontend& f) { return f.profiles; }
const sim::SkirmishSetup& frontend_skirmish_setup(const Frontend& f) { return f.setup; }
const MuseumState& frontend_museum(const Frontend& f) { return f.mus; }
const MuseumFile& frontend_museum_file(const Frontend& f) { return f.museum; }

MuseumShown frontend_museum_shown(const Frontend& f) {
    MuseumShown shown;
    for (const Window& win : f.stack) {
        if (win.screen != Screen::Museum) continue;
        for (const Widget& w : win.widgets) {
            if (w.role == Role::MusPicture && w.pic.tex) shown.picture_w = w.w;
            if (w.role == Role::MusText) shown.text_lines = static_cast<int>(w.strs.size());
            if (w.role == Role::Next && w.visible) shown.arrows = true;
        }
    }
    return shown;
}
void frontend_message(Frontend& f, const std::string& text) { message(f, text); }

void frontend_set_file_gate(std::function<int(const std::string& path)> need) { g_file_gate = std::move(need); }

void frontend_set_display_font(const std::string& path) { g_display_font = path; }

void frontend_set_build_label(const std::string& version, bool developer) {
    g_build_version = version, g_build_developer = developer;
}
void frontend_set_update_link(bool on, bool corner_link) {
    g_update_link = on;
    g_update_corner = on && corner_link;
}

bool frontend_game_message(Frontend& f, const std::string& text, bool end, std::string* error) {
    f.stack.clear();
    if (!open_message(f, text, error)) return false;
    Window& win = f.stack.back();
    win.screen = end ? Screen::EndMessage : Screen::GameMessage;
    for (Widget& w : win.widgets) {
        if (w.action == Action::MessageClose) w.action = end ? Action::EndClose : Action::GameMessageClose;
    }
    return true;
}

void frontend_end_message(Frontend& f, std::uint8_t kind, const std::string& my_name) {
    std::string err;
    const std::string text =
        kind == 2 ? lookup(f, "IDS_YOU_DEAD") : lookup(f, "IDS_PLAYER") + my_name + lookup(f, "IDS_WIN");
    log_failure(frontend_game_message(f, text, true, &err), err);
}

bool frontend_overlay(const Frontend& f) {
    return (f.stack.size() == 1 && f.stack[0].screen == Screen::EndMessage) || frontend_game_window_open(f);
}

void frontend_set_game_advisors(Frontend& f, const GameAdvisors& a) { f.game_advisors = a; }

std::optional<GameAdvisors> frontend_take_game_advisors(Frontend& f) {
    std::optional<GameAdvisors> a;
    a.swap(f.advisors_chosen);
    return a;
}

bool frontend_game_menu(Frontend& f, bool can_restart, std::string* error) {
    f.can_restart = can_restart;
    return open_game_menu(f, error);
}

bool frontend_game_menu_open(const Frontend& f) { return !f.stack.empty() && f.stack[0].screen == Screen::GameMenu; }

void frontend_set_canvas(Frontend& f, int w, int h) {
    if (w == f.canvas_w && h == f.canvas_h) return;
    f.canvas_w = w, f.canvas_h = h;
    for (Window& win : f.stack) place_window(f, win);  // centred, but for the mission and hints windows (slice I8)
}

// ---- the in-game windows of slice I8 ----

bool frontend_mission(Frontend& f, const std::string& title, const std::vector<std::string>& paragraphs,
                      std::string* error) {
    return open_mission(f, title, paragraphs, error);
}

bool frontend_hints(Frontend& f, std::vector<std::string> hints, int index, bool startup, std::string* error) {
    f.hints = std::move(hints);
    f.hint_index = index, f.hints_startup = startup;
    return open_hints(f, error);
}
int frontend_hint_index(const Frontend& f) { return f.hint_index; }
bool frontend_hints_startup(const Frontend& f) { return f.hints_startup; }

bool frontend_diplomacy(Frontend& f, std::vector<DiplomacyRow> rows, std::string* error) {
    f.diplomacy = std::move(rows);
    return open_diplomacy(f, error);
}
const std::vector<DiplomacyRow>& frontend_diplomacy_rows(const Frontend& f) { return f.diplomacy; }

bool frontend_game_window_open(const Frontend& f) {
    if (f.stack.empty()) return false;
    const Screen s = f.stack[0].screen;
    return s == Screen::GameMenu || s == Screen::Mission || s == Screen::Hints || s == Screen::Diplomacy ||
           s == Screen::Confirm ||    // slice H: F6's question alone over the game
           s == Screen::GameMessage;  // slice M4: a game message, centred on the view as the original's
}

bool frontend_game_runs(const Frontend& f) { return !f.stack.empty() && f.stack[0].screen == Screen::Diplomacy; }

// ---- saving and loading (slice H) ----

bool frontend_load_screen(Frontend& f, std::vector<SaveEntry> saves, bool in_game, std::string* error) {
    f.saves = std::move(saves);
    f.file_in_game = in_game;
    return open_load(f, error);
}

const SaveEntry& frontend_chosen_save(const Frontend& f) { return f.chosen; }

// ---- the mission lists (slice M4) ----

bool frontend_mission_list(Frontend& f, std::vector<MissionEntry> missions, int selected, std::string* error) {
    f.missions = std::move(missions);
    f.campaign_list = false;
    return open_mission_list(f, selected, error);
}

const MissionEntry& frontend_chosen_mission(const Frontend& f) { return f.chosen_mission; }

// ---- the campaigns (slice M7) ----

namespace {




bool campaign_base(Frontend& f, std::string* error) {
    while (!f.stack.empty() &&
           (f.stack.back().screen == Screen::CampaignSelect || f.stack.back().screen == Screen::MissionList)) {
        f.stack.pop_back();
    }
    if (!f.stack.empty() && f.stack.back().screen == Screen::SinglePlayer) return true;
    return open_single_player(f, error);
}

}  // namespace

bool frontend_campaign_select(Frontend& f, std::vector<CampaignEntry> campaigns, int selected, std::string* error) {
    if (!campaign_base(f, error)) return false;
    f.campaigns = std::move(campaigns);
    return open_campaign_select(f, selected, error);
}

const CampaignEntry& frontend_campaign(const Frontend& f) {
    static const CampaignEntry kNone;
    return f.campaign_sel >= 0 && f.campaign_sel < static_cast<int>(f.campaigns.size())
               ? f.campaigns[static_cast<std::size_t>(f.campaign_sel)]
               : kNone;
}

int frontend_campaign_row(const Frontend& f) { return f.campaign_sel; }

bool frontend_campaign_missions(Frontend& f, std::vector<MissionEntry> missions, int selected, std::string* error) {
    if (!campaign_base(f, error)) return false;
    f.missions = std::move(missions);
    f.campaign_list = true;
    return open_mission_list(f, selected, error);
}

bool frontend_save_screen(Frontend& f, std::vector<std::string> names, std::string* error) {
    f.file_in_game = true;
    return open_save(f, names, error);
}

std::string frontend_save_name(const Frontend& f) { return f.save_name; }

bool frontend_confirm_quick_load(Frontend& f, std::string* error) {
    f.stack.clear();
    return open_confirm(f, GameMenuItem::QuickLoad, error);
}

std::string save_name_clip(const std::string& text) {
    std::string out;
    for (const char c : text) {
        const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        if (ok && out.size() < 30) out += c;
    }
    return out;
}

const NetChoices& frontend_net_choices(const Frontend& f) { return f.net; }

bool apply_skirmish_spec(Frontend& f, const std::string& spec, std::string* error);  // below

bool frontend_skirmish_choices(Frontend& f, const std::string& spec, std::string* error) {
    if (f.stack.empty() || f.stack.back().screen != Screen::Skirmish) {
        *error = "the skirmish screen is not open";
        return false;
    }
    if (!apply_skirmish_spec(f, spec, error)) return false;
    sync_skirmish(f, f.stack.back());
    show_map(f, f.stack.back());
    return true;
}

void frontend_net_games(Frontend& f, const std::vector<NetGameRow>& rows) {
    f.net_found = rows;
    for (Window& win : f.stack) {
        if (win.screen == Screen::NetGames) net_games_refresh(f, win);
    }
}

bool frontend_net_lobby(Frontend& f, const NetLobby& lobby, std::string* error) {
    f.lobby = lobby;
    while (f.stack.size() > 1 && f.stack.back().screen != Screen::NetGames) f.stack.pop_back();
    return open_net_lobby(f, error);
}

int frontend_net_seat(const Frontend& f) { return f.net_seat; }

bool frontend_net_scenarios(Frontend& f, std::vector<MissionEntry> scenarios, int selected, std::string* error) {
    f.scenarios = std::move(scenarios);
    return open_net_scenarios(f, selected, error);
}

int frontend_net_screen(const Frontend& f) {
    if (f.stack.empty()) return 0;
    switch (f.stack.back().screen) {
        case Screen::NetPlayer: return 1;
        case Screen::NetGames: return 2;
        case Screen::NetScenarios: return 5;
        case Screen::Skirmish: return f.net_host ? 3 : 0;
        case Screen::NetLobby: return 4;
        default: return 0;
    }
}

void frontend_net_back(Frontend& f, const std::string& why) {
    bool games = false;
    for (const Window& win : f.stack) games = games || win.screen == Screen::NetGames;
    while (games && f.stack.back().screen != Screen::NetGames) f.stack.pop_back();
    if (!why.empty()) message(f, why);
}

void frontend_statistics(Frontend& f, std::vector<EndPlayer> players) {
    f.end_players = std::move(players);
    std::string err;
    log_failure(open_statistics(f, &err), err);
}

// "skirmish:players=4;resources=2;map=Aust_3r_4.dat;slots=4/0/0/0,3/1/1/0": the choices of the setup screen for a
// screenshot, set the way the callbacks leave them. Slots are level/nation/colour/team per row from row 0.
bool apply_skirmish_spec(Frontend& f, const std::string& spec, std::string* error) {
    std::size_t start = 0;
    while (start < spec.size()) {
        std::size_t end = spec.find(';', start);
        if (end == std::string::npos) end = spec.size();
        const std::string item = spec.substr(start, end - start);
        start = end + 1;
        const std::size_t eq = item.find('=');
        if (eq == std::string::npos) {
            *error = "skirmish choice '" + item + "' has no '='";
            return false;
        }
        const std::string key = item.substr(0, eq), value = item.substr(eq + 1);
        if (key == "players") {
            skirmish_set_players(f.sk, std::atoi(value.c_str()));
        } else if (key == "resources") {
            skirmish_set_resources(f.sk, std::atoi(value.c_str()));
        } else if (key == "rules") {  // "classic" or "plus"
            f.sk.plus = value == "plus";
        } else if (key == "map") {
            f.sk.map = map_index(f.maps, f.sk.players, value);
            if (f.sk.map < 0) {
                *error = "map " + value + " is not in the list";
                return false;
            }
        } else if (key == "slots") {
            std::size_t row = 0, at = 0;
            while (at < value.size() && row < 8) {
                std::size_t comma = value.find(',', at);
                if (comma == std::string::npos) comma = value.size();
                int v[4] = {0, 0, 0, 0};
                if (std::sscanf(value.substr(at, comma - at).c_str(), "%d/%d/%d/%d", &v[0], &v[1], &v[2], &v[3]) != 4) {
                    *error = "slot '" + value.substr(at, comma - at) + "' is not level/nation/colour/team";
                    return false;
                }
                f.sk.rows[row++] = sim::SkirmishSlot{static_cast<std::uint8_t>(v[0]), static_cast<std::uint8_t>(v[1]),
                                                     static_cast<std::uint8_t>(v[2]), static_cast<std::uint8_t>(v[3])};
                at = comma + 1;
            }
        } else {
            *error = "unknown skirmish choice '" + key + "'";
            return false;
        }
    }
    return true;
}

// "museum:nation=3;type=5;pick=2;picture=1;open=list;hover=4;scroll=1": the museum screen for a screenshot, with the
// choices set the way the callbacks leave them. nation, type: the filters (0 Russia ... 5 Universal; 0 Buildings ...
// 5 Soldiers); pick: the row of the entry list; picture: which of the entry's pictures; open: the box that is open
// (nation, type or list), hover: its picked row, scroll: its first shown row.
bool apply_museum_spec(Frontend& f, const std::string& spec, std::string* error) {
    Window& win = f.stack.back();
    std::string open;
    int hover = -1, scroll = 0;
    std::size_t start = 0;
    while (start < spec.size()) {
        std::size_t end = spec.find(';', start);
        if (end == std::string::npos) end = spec.size();
        const std::string item = spec.substr(start, end - start);
        start = end + 1;
        const std::size_t eq = item.find('=');
        if (eq == std::string::npos) {
            *error = "museum choice '" + item + "' has no '='";
            return false;
        }
        const std::string key = item.substr(0, eq), value = item.substr(eq + 1);
        const int number = std::atoi(value.c_str());
        if (key == "nation" || key == "type") {
            const bool nation = key == "nation";
            const std::size_t limit = nation ? f.museum.nations.size() : f.museum.types.size();
            if (number < 0 || static_cast<std::size_t>(number) >= limit) {
                *error = "museum " + key + " " + value + " is not in its list";
                return false;
            }
            nation ? museum_set_nation(f.museum, f.mus, number) : museum_set_type(f.museum, f.mus, number);
            if (Widget* box = find_role(win, nation ? Role::MusNation : Role::MusType)) box->sel = number;
            museum_refresh(f, win, true);
        } else if (key == "pick") {
            if (number < 0 || static_cast<std::size_t>(number) >= f.mus.list.size()) {
                *error = "museum pick " + value + " is not a row of the list";
                return false;
            }
            museum_pick_row(f.museum, f.mus, number);
            if (Widget* box = find_role(win, Role::MusList)) box->sel = number;
            museum_refresh(f, win, false);
        } else if (key == "picture") {
            if (f.mus.entry < 0 || static_cast<std::size_t>(number) >=
                                       f.museum.entries[static_cast<std::size_t>(f.mus.entry)].pictures.size()) {
                *error = "museum picture " + value + " does not exist";
                return false;
            }
            f.mus.picture = number;
            museum_refresh(f, win, false);
        } else if (key == "open") {
            open = value;
        } else if (key == "hover") {
            hover = number;
        } else if (key == "scroll") {
            scroll = number;
        } else {
            *error = "unknown museum choice '" + key + "'";
            return false;
        }
    }
    if (!open.empty()) {
        const Role role = open == "nation" ? Role::MusNation : open == "type" ? Role::MusType : Role::MusList;
        Widget* box = find_role(win, role);
        if (!box || (open != "nation" && open != "type" && open != "list")) {
            *error = "museum open=" + open + ": nation, type or list";
            return false;
        }
        box->open = true;
        box->h = box->row_h + box->list_h;
        box->top = std::clamp(scroll, 0,
                              std::max(0, static_cast<int>(box->strs.size()) - combo_rows(box->list_h, box->row_h)));
        if (hover >= 0) box->sel = std::min(hover, static_cast<int>(box->strs.size()) - 1);
        museum_combo_thumb(*box);
    }
    return true;
}

void frontend_set_animate(Frontend& f, bool on) { f.animate = on; }

bool frontend_show(Frontend& f, const std::string& screen, std::string* error) {
    f.show_cursor = false;  // screenshots are taken at the first frame: no bar in the middle of a blink
    f.animate = false;      // and the load screen's door is open
    if (!open_main(f, error)) return false;
    if (screen == "main") return true;
    if (screen == "network" || screen.rfind("network_", 0) == 0) {  // slice MP2: the network pages
        if (!open_net_player(f, error)) return false;
        if (screen == "network") return true;
        f.net.name = player_name(f);
        if (!open_net_games(f, error)) return false;
        if (screen == "network_games") return true;
        if (screen == "network_host") return open_skirmish(f, error, true);
        if (screen == "network_host_plus") {  // the host's setup with Entente Engine Plus chosen
            if (!open_skirmish(f, error, true)) return false;
            f.sk.plus = true;
            sync_skirmish(f, f.stack.back());
            return true;
        }
        if (screen == "network_lobby") {  // a made-up lobby: you in, one human to come, a computer
            NetLobby l;
            l.title = ours(f, "net.game_title", {{"name", data(f, f.net.name)}}), l.map = "Aust_3r_3.dat",
            l.host = true;
            l.plus = true;  // the rule set's line under the map
            l.slots = {{f.net.name, 0, 0, 0, true, true},
                       {ours(f, "net.slot_open"), 1, 1, 0, true, false},
                       {ours(f, "net.slot_easy"), 5, 2, 0, false, false}};
            l.lines = {lang_plural(language(f), "net.waiting", 1),
                       ours(f, "net.join_at", {{"address", "192.0.2.10"}, {"port", "47625"}})};
            return frontend_net_lobby(f, l, error);
        }
        if (screen == "network_lobby8") {  // slice MP8: eight humans, all in (the eight player test's setup)
            NetLobby l;
            l.title = ours(f, "net.game_title", {{"name", data(f, f.net.name)}}), l.map = "Aust_3r_3.dat",
            l.host = true, l.can_start = true;
            for (int k = 0; k < 8; ++k) {
                l.slots.push_back(
                    {k == 0 ? f.net.name : data(f, "Guest" + std::to_string(k + 1)), k % 5, k, k / 2 + 1, true, true});
            }
            l.lines = {ours(f, "net.all_in_host"),
                       ours(f, "net.join_at", {{"address", "192.0.2.10"}, {"port", "47625"}})};
            return frontend_net_lobby(f, l, error);
        }
        *error = "unknown screen '" + screen +
                 "' (network, network_games, network_host[_plus], network_lobby[8]; network_scenarios is the app's)";
        return false;
    }
    if (screen == "game_menu" || screen == "confirm_restart") {
        f.can_restart = true;
        if (!open_game_menu(f, error)) return false;
        return screen == "game_menu" || open_confirm(f, GameMenuItem::Restart, error);
    }
    if (screen == "museum" || screen.rfind("museum:", 0) == 0) {
        if (!open_museum(f, error)) return false;
        return screen.size() <= 7 || apply_museum_spec(f, screen.substr(7), error);
    }
    if (screen == "profiles") return open_profiles(f, error);
    if (screen == "single_player") return open_single_player(f, error);
    if (screen == "skirmish" || screen.rfind("skirmish:", 0) == 0) {
        if (!open_single_player(f, error) || !open_skirmish(f, error)) return false;
        if (screen.size() > 9) {
            if (!apply_skirmish_spec(f, screen.substr(9), error)) return false;
            sync_skirmish(f, f.stack.back());
            show_map(f, f.stack.back());
        }
        return true;
    }
    if (!open_options(f, error)) return false;
    if (screen == "options") return true;
    if (screen == "game_options") return open_game_options(f, error);
    if (screen == "game_options_ingame") {  // as in a game with every switch on and the template Defense + attack
        f.game_advisors = GameAdvisors{{1, 1, 1, 1, 1, 1}, {1, 1, 1, 1}, 2};
        return open_game_options(f, error);
    }
    *error = "unknown screen '" + screen +
             "' (main, options, game_options, game_options_ingame, network[_games|_host|_lobby], profiles, "
             "single_player, skirmish, "
             "museum, game_menu, confirm_restart)";
    return false;
}

}  // namespace antanta::ui
