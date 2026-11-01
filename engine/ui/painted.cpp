#include "ui/painted.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "ui/lang.hpp"

namespace antanta::ui {

namespace {

// ---- the looks (fitted to the original pictures with the English check, docs/languages.md 3) ----

enum Look : std::uint8_t {
    kMainMenu,    // the parchment strips of the main menu (basic.les)
    kSpMenu,      // the parchment cartridges of the single player menu (singleplay.les butcampaigns and the others)
    kCompass,     // the compass "Back" of the single player menu (singleplay.les butback.bmp)
    kGlobe,       // the glass globes of the load and save screens (singleplay.les butload, butcancel, butsave)
    kPlate,       // the metal plates of the campaign select (singleplay.les btn_applay, btn_menu)
    kWoodBar,     // the wooden bars of the menus and windows (195 x 27 and the like)
    kGoldBar,     // the statistics' Close (Statistics.les)
    kOptionsBar,  // the Options dialog's bars (Options.les)
    kSmallBar,    // the in-game menu's small bars (GameMenu.les, 100 x 17)
    kCapsBar,     // the hints window's small capitals (Hints.les)
    kPlaque,      // the labels on the Options dialog's plaques (Options.les back.bmp)
    kTiles,       // the stencil headings of letter tiles (Single Player, Multiplayer, Load Mission and the others)
    kTilesSerif,  // the headings of serif letter tiles (the Options dialog's Settings, the in-game menu's Game Menu)
    kResolution,  // the Options dialog's resolution pictures (Options.les 1024x768.bmp and the others)
};

constexpr PaintInk ink(std::uint32_t text, std::uint32_t halo, int dx, int dy, int grow, int blur, int strength) {
    return PaintInk{text,
                    halo,
                    static_cast<std::int8_t>(dx),
                    static_cast<std::int8_t>(dy),
                    static_cast<std::uint8_t>(grow),
                    static_cast<std::uint8_t>(blur),
                    static_cast<std::uint8_t>(strength)};
}

constexpr PaintLook look(PaintFont font, int em, int min_em, int lines, int tracking, PaintInk normal, PaintInk pressed,
                         PaintInk lit) {
    return PaintLook{font, em, min_em, lines, tracking, {normal, pressed, lit}};
}

constexpr PaintFont kSerif = PaintFont::Menu;

const std::array<PaintLook, 14> kLooks = {
    // kMainMenu: cream with a dark shadow; red with a light glow; black with a light glow
    look(kSerif, 20, 14, 1, 12, ink(0xfaf6cc, 0x201a08, 1, 1, 2, 3, 96), ink(0xd81c10, 0xfff0a0, 0, 0, 2, 3, 110),
         ink(0x141408, 0xfff4a8, 0, 0, 2, 3, 110)),
    // kSpMenu
    look(kSerif, 21, 14, 1, 20, ink(0xfaf6cc, 0x201a08, 1, 1, 2, 3, 96), ink(0xe83020, 0xfff0a0, 0, 0, 2, 3, 110),
         ink(0x141408, 0xfff4a8, 0, 0, 2, 3, 110)),
    // kCompass: black with a light glow
    look(kSerif, 25, 16, 1, 10, ink(0x1e1d14, 0xece4a8, 0, 0, 2, 3, 120), ink(0x1e1d14, 0xf1efb2, 0, 0, 2, 3, 160),
         ink(0x1e1d14, 0xece4a8, 0, 0, 2, 3, 160)),
    // kGlobe: cream with a dark shadow
    look(kSerif, 16, 11, 1, 0, ink(0xf0e8c0, 0x000000, 1, 1, 0, 1, 64), ink(0xf0e8c0, 0x000000, 1, 1, 0, 1, 64),
         ink(0xf0e8c0, 0x000000, 1, 1, 0, 1, 64)),
    // kPlate: cream; gold; gold
    look(kSerif, 25, 14, 1, 5, ink(0xd8d4b0, 0x383830, 0, 0, 2, 3, 160), ink(0xd8b058, 0x412f16, 0, 0, 2, 3, 160),
         ink(0xe0a858, 0x513315, 0, 0, 2, 3, 160)),
    // kWoodBar: cream with a dark shadow; the same pressed; gold with a glow under the mouse
    look(kSerif, 21, 14, 1, 20, ink(0xf0ecc8, 0x100800, 1, 1, 1, 1, 96), ink(0xe6e0bc, 0x100800, 1, 1, 1, 1, 96),
         ink(0xffd860, 0xe0a030, 0, 0, 2, 3, 80)),
    // kGoldBar
    look(kSerif, 20, 12, 1, 0, ink(0xe8dcb0, 0x100800, 1, 1, 0, 1, 96), ink(0xd8c8a0, 0x100800, 1, 1, 0, 1, 96),
         ink(0xf0a830, 0x100800, 1, 1, 0, 1, 96)),
    // kOptionsBar
    look(kSerif, 19, 12, 1, 0, ink(0xecebc8, 0x282116, 1, 2, 1, 1, 128), ink(0xe4e3bf, 0x251d12, 1, 2, 1, 1, 128),
         ink(0xffffff, 0x393028, 1, 2, 1, 1, 96)),
    // kSmallBar: white; orange; yellow
    look(kSerif, 13, 9, 1, 0, ink(0xf4f2f0, 0x000000, 1, 1, 0, 0, 48), ink(0xffb070, 0x000000, 1, 1, 0, 0, 48),
         ink(0xf0d040, 0x000000, 1, 1, 0, 0, 48)),
    // kCapsBar
    look(kSerif, 11, 8, 1, 15, ink(0xf0e0b0, 0x000000, 1, 1, 0, 0, 48), ink(0xf0e0b0, 0x000000, 1, 1, 0, 0, 48),
         ink(0xffd870, 0xc07010, 0, 0, 1, 1, 64)),
    // kPlaque: cream with a dark shadow, two lines
    look(kSerif, 15, 11, 2, 0, ink(0xf0e8c8, 0x000000, 1, 1, 0, 1, 64), {}, {}),
    // kTiles, kTilesSerif: the letters as large as the original's (up to em), their colour the original's
    look(PaintFont::Display, 80, 8, 1, 0, {}, {}, {}),
    look(kSerif, 40, 8, 1, 0, {}, {}, {}),
    // kResolution: cream with a dark glow, the same in both cells (normal and under the mouse)
    look(kSerif, 19, 12, 1, 0, ink(0xfaf6cc, 0x000000, 1, 1, 2, 3, 110), ink(0xfaf6cc, 0x000000, 1, 1, 2, 3, 110), {}),
};

// ---- the table ----

// The archives as the front-end opens them: the main menu's basic.les and the in-game menu's GameMenu.les through
// Menu.txt, the single player screens' singleplay.les (frontend.cpp single_player_window).
const std::vector<PaintedWord> kWords = {
    // The main menu (Menu.txt window 0): each button on its strip of back.bmp at the place Menu.txt gives
    {"basic.les", "but_odin_igrok.bmp", "pic.single_player", 3, PaintFill::Under, kMainMenu, 2, 1, 133, 27, "back.bmp",
     667, 213},
    {"basic.les", "but_setevaya_igra.bmp", "pic.multiplayer", 3, PaintFill::Under, kMainMenu, 3, 1, 129, 27, "back.bmp",
     667, 290},
    {"basic.les", "but_zagruzka.bmp", "pic.load_game", 3, PaintFill::Under, kMainMenu, 3, 1, 129, 27, "back.bmp", 667,
     364},
    {"basic.les", "but_nastroyki.bmp", "pic.options", 3, PaintFill::Under, kMainMenu, 3, 1, 129, 27, "back.bmp", 667,
     439},
    {"basic.les", "but_muzeam.bmp", "pic.museum", 3, PaintFill::Under, kMainMenu, 3, 1, 129, 27, "back.bmp", 667, 514},
    {"basic.les", "but_exit.bmp", "pic.quit_game", 3, PaintFill::Under, kMainMenu, 3, 1, 129, 27, "back.bmp", 667, 595},
    // The single player menu (SP_Fon_P2, frontend.cpp open_single_player)
    {"singleplay.les", "butcampaigns.bmp", "pic.campaigns", 3, PaintFill::Under, kSpMenu, 1, 2, 192, 30,
     "sp_fon_p2.bmp", 620, 151},
    {"singleplay.les", "butmissions.bmp", "pic.missions", 3, PaintFill::Under, kSpMenu, 1, 2, 192, 30, "sp_fon_p2.bmp",
     620, 230},
    {"singleplay.les", "butrandommaps.bmp", "pic.random_maps", 3, PaintFill::Under, kSpMenu, 1, 2, 192, 30,
     "sp_fon_p2.bmp", 624, 307},
    {"singleplay.les", "butback.bmp", "pic.back", 3, PaintFill::Under, kCompass, 6, 28, 87, 40, "sp_fon_p2.bmp", 46,
     76},
    // The load and save screens' globes (SP_Fon_P6, Back.bmp)
    {"singleplay.les", "butload.bmp", "pic.load", 3, PaintFill::Cols, kGlobe, 4, 34, 92, 24},
    {"singleplay.les", "butcancel.bmp", "pic.back", 3, PaintFill::Cols, kGlobe, 4, 34, 92, 24},
    {"singleplay.les", "butsave.bmp", "pic.save", 3, PaintFill::Cols, kGlobe, 4, 34, 92, 24},
    // The campaign select's plates (SP_Fon_P4)
    {"singleplay.les", "btn_applay.bmp", "pic.plate_ok", 3, PaintFill::Cols, kPlate, 28, 52, 78, 22},
    {"singleplay.les", "btn_menu.bmp", "pic.back", 3, PaintFill::Cols, kPlate, 28, 52, 78, 22},
    // The wooden bars: the profile page, the skirmish setup, the network pages
    {"singleplay.les", "sp_addbtn3.bmp", "pic.add", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"singleplay.les", "sp_delbtn3.bmp", "pic.delete", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"singleplay.les", "sp_continuebtn3.bmp", "pic.continue", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"singleplay.les", "sp_returnbtn3.bmp", "pic.back", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"singleplay.les", "sp_nextbtn.bmp", "pic.next", 3, PaintFill::Rows, kWoodBar, 8, 5, 150, 21},
    {"singleplay.les", "sp_prevbtn.bmp", "pic.prev", 3, PaintFill::Rows, kWoodBar, 8, 5, 150, 21},
    {"singleplay.les", "mp_bstart.bmp", "pic.start", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"multiplay.les", "mp_addbtn3.bmp", "pic.add", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"multiplay.les", "mp_continuebtn3.bmp", "pic.continue", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"multiplay.les", "mp_createbtn3.bmp", "pic.create", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"multiplay.les", "mp_joinbtn3.bmp", "pic.join", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"multiplay.les", "mp_bstart.bmp", "pic.start", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    // The windows' bars
    {"confirm.les", "yes.bmp", "pic.yes", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"confirm.les", "no.bmp", "pic.no", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"diplomacy.les", "ok.bmp", "pic.ok", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"diplomacy.les", "cancel.bmp", "pic.cancel", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"gameoptions.les", "apply.bmp", "pic.ok", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"gameoptions.les", "cancel.bmp", "pic.cancel", 3, PaintFill::Rows, kWoodBar, 8, 5, 179, 21},
    {"statistics.les", "close.bmp", "pic.close", 3, PaintFill::Rows, kGoldBar, 5, 3, 189, 22},
    {"options.les", "apply.bmp", "pic.ok", 3, PaintFill::Rows, kOptionsBar, 10, 4, 170, 20},
    {"options.les", "cancel.bmp", "pic.cancel", 3, PaintFill::Rows, kOptionsBar, 10, 4, 170, 20},
    {"options.les", "gameoptionsbtn.bmp", "pic.game_options", 3, PaintFill::Rows, kOptionsBar, 20, 3, 447, 22},
    // The in-game menu (Menu.txt window 4)
    {"gamemenu.les", "butback.bmp", "pic.back_to_game", 3, PaintFill::Rows, kSmallBar, 4, 0, 94, 13},
    {"gamemenu.les", "butload.bmp", "pic.load_game", 3, PaintFill::Rows, kSmallBar, 4, 0, 94, 13},
    {"gamemenu.les", "butrestart.bmp", "pic.restart_mission", 3, PaintFill::Rows, kSmallBar, 4, 0, 94, 13},
    {"gamemenu.les", "butreturn.bmp", "pic.return_to_menu", 3, PaintFill::Rows, kSmallBar, 4, 0, 94, 13},
    {"gamemenu.les", "butsave.bmp", "pic.save_game", 3, PaintFill::Rows, kSmallBar, 4, 0, 94, 13},
    {"gamemenu.les", "butsettings.bmp", "pic.settings", 3, PaintFill::Rows, kSmallBar, 4, 0, 94, 13},
    // The hints window
    {"hints.les", "close.bmp", "pic.close_caps", 3, PaintFill::Rows, kCapsBar, 7, 3, 128, 15},
    {"hints.les", "next.bmp", "pic.next_caps", 3, PaintFill::Rows, kCapsBar, 7, 3, 128, 15},
    {"hints.les", "prev.bmp", "pic.prev_caps", 3, PaintFill::Rows, kCapsBar, 7, 3, 128, 15},
    // The Options dialog's plaques (Options.les back.bmp)
    {"options.les", "back.bmp", "pic.game_speed", 1, PaintFill::Cols, kPlaque, 120, 82, 168, 44},
    {"options.les", "back.bmp", "pic.music_volume", 1, PaintFill::Cols, kPlaque, 120, 165, 168, 44},
    {"options.les", "back.bmp", "pic.sfx_volume", 1, PaintFill::Cols, kPlaque, 120, 246, 168, 44},
    {"options.les", "back.bmp", "pic.screen_resolution", 1, PaintFill::Cols, kPlaque, 120, 344, 168, 44},
    // The headings of letter tiles (L4b): the box is the tiles' band, the letters laid out again tile by tile
    {"singleplay.les", "sp_fon_p1.bmp", "pic.single_player", 1, PaintFill::Tiles, kTiles, 320, 45, 410, 72},
    {"singleplay.les", "sp_fon_p2.bmp", "pic.single_player", 1, PaintFill::Tiles, kTiles, 380, 27, 484, 72},
    {"singleplay.les", "sp_fon_p5.bmp", "pic.single_player", 1, PaintFill::Tiles, kTiles, 43, 45, 402, 72},
    {"singleplay.les", "sp_fon_p4.bmp", "pic.select_campaign", 1, PaintFill::Tiles, kTiles, 223, 8, 575, 82},
    {"singleplay.les", "loadmission.bmp", "pic.load_mission", 1, PaintFill::Tiles, kTiles, 0, 3, 552, 79},
    {"singleplay.les", "save.bmp", "pic.save_game", 1, PaintFill::Tiles, kTiles, 294, 2, 480, 78},
    {"savegame.les", "save.bmp", "pic.save_game", 1, PaintFill::Tiles, kTiles, 0, 0, 481, 81},
    {"multiplay.les", "mp_fon_p1.bmp", "pic.multiplayer", 1, PaintFill::Tiles, kTiles, 43, 45, 392, 77},
    {"multiplay.les", "mp_fon_p2.bmp", "pic.multiplayer", 1, PaintFill::Tiles, kTiles, 43, 45, 392, 77},
    {"multiplay.les", "mp_fon_p3.bmp", "pic.multiplayer", 1, PaintFill::Tiles, kTiles, 43, 45, 392, 77},
    {"multiplay.les", "mp_fon_p4.bmp", "pic.multiplayer", 1, PaintFill::Tiles, kTiles, 43, 45, 392, 77},
    {"options.les", "back.bmp", "pic.settings", 1, PaintFill::Tiles, kTilesSerif, 236, 10, 206, 25},
    {"gameoptions.les", "back.bmp", "pic.game_options", 1, PaintFill::Tiles, kTiles, 280, 45, 468, 64},
    {"diplomacy.les", "back.bmp", "pic.diplomacy", 1, PaintFill::Tiles, kTiles, 200, 173, 298, 55},
    {"gamemenu.les", "back.bmp", "pic.game_menu", 1, PaintFill::Tiles, kTilesSerif, 6, 10, 160, 22},
};

// ---- pixels ----

struct Rgb {
    float r = 0, g = 0, b = 0;
};

Rgb rgb(std::uint32_t c) {
    return {static_cast<float>((c >> 16) & 0xff), static_cast<float>((c >> 8) & 0xff), static_cast<float>(c & 0xff)};
}

std::uint8_t* at(Image& img, int x, int y) {
    return img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
}
const std::uint8_t* at(const Image& img, int x, int y) {
    return img.rgba.data() + (static_cast<std::size_t>(y) * img.width + x) * 4;
}

bool keyed(const std::uint8_t* p) { return p[0] == 255 && p[1] == 0 && p[2] == 255; }

std::uint8_t clamp8(float v) { return static_cast<std::uint8_t>(std::clamp(std::lround(v), 0L, 255L)); }

// One value per pixel of a rectangle (a mask: 0 or 1; a coverage: 0 to 1).
struct Plane {
    int w = 0, h = 0;
    std::vector<float> v;
    Plane(int width, int height) : w(width), h(height), v(static_cast<std::size_t>(width) * height, 0.0f) {}
    float& operator()(int x, int y) { return v[static_cast<std::size_t>(y) * w + x]; }
    float operator()(int x, int y) const { return v[static_cast<std::size_t>(y) * w + x]; }
    bool in(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
};

// A box of a cell clipped to it.
struct Box {
    int x = 0, y = 0, w = 0, h = 0;
};

Box clip_box(const PaintedWord& word, int cell_w, int cell_h) {
    Box b;
    b.x = std::clamp(word.x, 0, cell_w);
    b.y = std::clamp(word.y, 0, cell_h);
    b.w = std::clamp(word.x + word.w, 0, cell_w) - b.x;
    b.h = std::clamp(word.y + word.h, 0, cell_h) - b.y;
    return b;
}

// The largest value in a (2r + 1) square around each pixel.
Plane grow(const Plane& g, int r) {
    if (r <= 0) return g;
    Plane row(g.w, g.h), out(g.w, g.h);
    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            float m = 0;
            for (int k = std::max(0, x - r); k <= std::min(g.w - 1, x + r); ++k) m = std::max(m, g(k, y));
            row(x, y) = m;
        }
    }
    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            float m = 0;
            for (int k = std::max(0, y - r); k <= std::min(g.h - 1, y + r); ++k) m = std::max(m, row(x, k));
            out(x, y) = m;
        }
    }
    return out;
}

// The mean over a (2r + 1) square around each pixel, the outside counted as 0.
Plane blur(const Plane& g, int r) {
    if (r <= 0) return g;
    const float n = static_cast<float>((2 * r + 1) * (2 * r + 1));
    Plane row(g.w, g.h), out(g.w, g.h);
    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            float s = 0;
            for (int k = std::max(0, x - r); k <= std::min(g.w - 1, x + r); ++k) s += g(k, y);
            row(x, y) = s;
        }
    }
    for (int y = 0; y < g.h; ++y) {
        for (int x = 0; x < g.w; ++x) {
            float s = 0;
            for (int k = std::max(0, y - r); k <= std::min(g.h - 1, y + r); ++k) s += row(x, k);
            out(x, y) = s / n;
        }
    }
    return out;
}

// ---- the blank ----

// The pixels of the old words in the box (box coordinates), the same for every cell: where the cells differ (the text
// changes colour between the states), and where a pixel stands out from its row (the text on a strip), cleaned of
// single specks and grown by two pixels for the soft edge of the glow and shadow.
Plane words_mask(const Image& img, const PaintedWord& word, const Box& b, int cell_w) {
    Plane m(b.w, b.h);
    for (int y = 0; y < b.h; ++y) {
        for (int c = 0; c < word.cells; ++c) {
            // the row's median colour in this cell
            std::array<std::vector<int>, 3> ch;
            for (int x = 0; x < b.w; ++x) {
                const std::uint8_t* p = at(img, c * cell_w + b.x + x, b.y + y);
                if (keyed(p)) continue;
                for (int k = 0; k < 3; ++k) ch[k].push_back(p[k]);
            }
            if (ch[0].empty()) continue;
            std::array<int, 3> med{};
            for (int k = 0; k < 3; ++k) {
                auto mid = ch[k].begin() + static_cast<std::ptrdiff_t>(ch[k].size() / 2);
                std::nth_element(ch[k].begin(), mid, ch[k].end());
                med[k] = *mid;
            }
            for (int x = 0; x < b.w; ++x) {
                const std::uint8_t* p = at(img, c * cell_w + b.x + x, b.y + y);
                if (keyed(p)) continue;
                const int dev = std::abs(p[0] - med[0]) + std::abs(p[1] - med[1]) + std::abs(p[2] - med[2]);
                if (dev > 96) m(x, y) = 1;
            }
        }
        for (int x = 0; x < b.w && word.cells > 1; ++x) {
            int d = 0;
            for (int c = 0; c < word.cells; ++c) {
                for (int e = c + 1; e < word.cells; ++e) {
                    const std::uint8_t* p = at(img, c * cell_w + b.x + x, b.y + y);
                    const std::uint8_t* q = at(img, e * cell_w + b.x + x, b.y + y);
                    if (keyed(p) || keyed(q)) continue;
                    for (int k = 0; k < 3; ++k) d = std::max(d, std::abs(p[k] - q[k]));
                }
            }
            if (d > 24) m(x, y) = 1;
        }
    }
    Plane clean(b.w, b.h);
    for (int y = 0; y < b.h; ++y) {
        for (int x = 0; x < b.w; ++x) {
            if (!m(x, y)) continue;
            int n = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) n += m.in(x + dx, y + dy) && m(x + dx, y + dy) > 0;
            }
            clean(x, y) = n >= 3 ? 1.0f : 0.0f;
        }
    }
    return grow(clean, 2);
}

// One cell's blank: every masked pixel from its source (the background under it, or the same row a mask's width
// away), plus a colour correction that goes from the difference at the run's left end to the one at its right end
// (each the mean over a few unmasked pixels), smoothed over five rows.
// One cell's blank for PaintFill::Cols: every masked pixel a blend of the mean of the two unmasked pixels above it and
// of the two below it, by its distance to each.
void fill_cols(Image& img, const Box& b, const Plane& m, int cell_x) {
    auto mean = [&](int x, int y0, int y1, Rgb* out) {
        int n = 0;
        Rgb s;
        for (int y = std::max(0, y0); y <= std::min(img.height - 1, y1); ++y) {
            const std::uint8_t* p = at(img, cell_x + x, y);
            if (keyed(p)) continue;
            s.r += p[0], s.g += p[1], s.b += p[2], ++n;
        }
        if (n) *out = {s.r / n, s.g / n, s.b / n};
        return n > 0;
    };
    for (int x = 0; x < b.w; ++x) {
        for (int y = 0; y < b.h;) {
            if (m(x, y) == 0) {
                ++y;
                continue;
            }
            const int a = y;
            while (y < b.h && m(x, y) > 0) ++y;
            const int e = y;  // the run is [a, e) in the box
            Rgb top, bottom;
            const bool has_top = mean(b.x + x, b.y + a - 2, b.y + a - 1, &top);
            const bool has_bottom = mean(b.x + x, b.y + e, b.y + e + 1, &bottom);
            if (!has_top && !has_bottom) continue;
            if (!has_top) top = bottom;
            if (!has_bottom) bottom = top;
            for (int k = a; k < e; ++k) {
                std::uint8_t* p = at(img, cell_x + b.x + x, b.y + k);
                if (keyed(p)) continue;
                const float t = static_cast<float>(k - a + 1) / static_cast<float>(e - a + 1);
                p[0] = clamp8(top.r + (bottom.r - top.r) * t), p[1] = clamp8(top.g + (bottom.g - top.g) * t),
                p[2] = clamp8(top.b + (bottom.b - top.b) * t);
            }
        }
    }
}

void fill_cell(Image& img, const PaintedWord& word, const Box& b, const Plane& m, int cell_x, int cell_w,
               const Image* under) {
    const int cell_h = img.height;
    int mx0 = b.w, mx1 = -1;
    for (int y = 0; y < b.h; ++y) {
        for (int x = 0; x < b.w; ++x) {
            if (m(x, y) > 0) mx0 = std::min(mx0, x), mx1 = std::max(mx1, x);
        }
    }
    if (mx1 < 0) return;
    const int shift = mx1 - mx0 + 4;
    auto masked = [&](int x, int y) {  // cell coordinates
        const int bx = x - b.x, by = y - b.y;
        return m.in(bx, by) && m(bx, by) > 0;
    };
    // the source pixel of (x, y) in cell coordinates; false when there is none
    auto source = [&](int x, int y, Rgb* out) {
        if (word.fill == PaintFill::Under) {
            const int ux = word.ux + x, uy = word.uy + y;
            if (!under || ux < 0 || uy < 0 || ux >= under->width || uy >= under->height) return false;
            const std::uint8_t* p = at(*under, ux, uy);
            *out = {static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2])};
            return true;
        }
        for (int k : {shift, -shift, 2 * shift, -2 * shift}) {
            const int sx = x + k;
            if (sx < 2 || sx >= cell_w - 2 || masked(sx, y)) continue;
            const std::uint8_t* p = at(img, cell_x + sx, y);
            if (keyed(p)) continue;
            *out = {static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2])};
            return true;
        }
        return false;
    };
    // the mean difference (original - source) and the mean original over the unmasked pixels of a window
    auto window = [&](int x0, int x1, int y0, int y1, Rgb* diff, Rgb* orig) {
        int n = 0, nd = 0;
        Rgb d, o;
        for (int y = std::max(0, y0); y <= std::min(cell_h - 1, y1); ++y) {
            for (int x = std::max(0, x0); x <= std::min(cell_w - 1, x1); ++x) {
                if (masked(x, y)) continue;
                const std::uint8_t* p = at(img, cell_x + x, y);
                if (keyed(p)) continue;
                o.r += p[0], o.g += p[1], o.b += p[2], ++n;
                Rgb s;
                if (source(x, y, &s)) d.r += p[0] - s.r, d.g += p[1] - s.g, d.b += p[2] - s.b, ++nd;
            }
        }
        if (n) o.r /= n, o.g /= n, o.b /= n;
        if (nd) d.r /= nd, d.g /= nd, d.b /= nd;
        *diff = d, *orig = o;
        return n > 0;
    };
    const std::size_t cells = static_cast<std::size_t>(b.w) * b.h;
    std::vector<Rgb> corr(cells), plain(cells), src(cells);
    std::vector<std::uint8_t> has_src(cells);
    auto idx = [&](int x, int y) { return static_cast<std::size_t>(y) * b.w + x; };
    for (int y = 0; y < b.h; ++y) {
        const int cy = b.y + y;
        for (int x = 0; x < b.w;) {
            if (m(x, y) == 0) {
                ++x;
                continue;
            }
            const int a = x;
            while (x < b.w && m(x, y) > 0) ++x;
            const int e = x;  // the run is [a, e) in the box
            Rgb dl, ol, dr, orr;
            const bool left = window(b.x + a - 4, b.x + a - 1, cy - 2, cy + 2, &dl, &ol);
            const bool right = window(b.x + e, b.x + e + 3, cy - 2, cy + 2, &dr, &orr);
            if (!left) dl = dr, ol = orr;
            if (!right) dr = dl, orr = ol;
            for (int k = a; k < e; ++k) {
                const float t = static_cast<float>(k - a + 1) / static_cast<float>(e - a + 1);
                corr[idx(k, y)] = {dl.r + (dr.r - dl.r) * t, dl.g + (dr.g - dl.g) * t, dl.b + (dr.b - dl.b) * t};
                plain[idx(k, y)] = {ol.r + (orr.r - ol.r) * t, ol.g + (orr.g - ol.g) * t, ol.b + (orr.b - ol.b) * t};
                Rgb s;
                has_src[idx(k, y)] = source(b.x + k, cy, &s);
                src[idx(k, y)] = s;
            }
        }
    }
    for (int y = 0; y < b.h; ++y) {
        for (int x = 0; x < b.w; ++x) {
            if (m(x, y) == 0) continue;
            Rgb c;
            int n = 0;
            for (int k = std::max(0, y - 2); k <= std::min(b.h - 1, y + 2); ++k) {
                if (m(x, k) == 0) continue;
                const Rgb& d = corr[idx(x, k)];
                c.r += d.r, c.g += d.g, c.b += d.b, ++n;
            }
            c.r /= n, c.g /= n, c.b /= n;
            std::uint8_t* p = at(img, cell_x + b.x + x, b.y + y);
            if (keyed(p)) continue;
            const Rgb& s = src[idx(x, y)];
            const Rgb v = has_src[idx(x, y)] ? Rgb{s.r + c.r, s.g + c.g, s.b + c.b} : plain[idx(x, y)];
            p[0] = clamp8(v.r), p[1] = clamp8(v.g), p[2] = clamp8(v.b);
        }
    }
}

// ---- the text ----

// The text's coverage over a rectangle (the box with room for the halo), cell coordinates (ox, oy) at its top left.
struct Canvas {
    int ox = 0, oy = 0;
    Plane a{0, 0};
};

// Copies a line's coverage into the canvas with its left end at x and its baseline at y (cell coordinates), drawn
// `width` pixels wide (narrower than the line: squeezed, each column a mix of the two nearest).
void put_line(Canvas& cv, const Coverage& c, int x, int baseline, int width) {
    for (int ty = 0; ty < c.h; ++ty) {
        const int cy = baseline - c.ascent + ty - cv.oy;
        if (cy < 0 || cy >= cv.a.h) continue;
        for (int tx = 0; tx < width; ++tx) {
            const int cx = x + tx - cv.ox;
            if (cx < 0 || cx >= cv.a.w) continue;
            float v;
            if (width == c.w) {
                v = c.a[static_cast<std::size_t>(ty) * c.w + tx];
            } else {
                const float sx =
                    (static_cast<float>(tx) + 0.5f) * static_cast<float>(c.w) / static_cast<float>(width) - 0.5f;
                const int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, c.w - 1);
                const int x1 = std::min(x0 + 1, c.w - 1);
                const float f = std::clamp(sx - static_cast<float>(x0), 0.0f, 1.0f);
                v = c.a[static_cast<std::size_t>(ty) * c.w + x0] * (1 - f) +
                    c.a[static_cast<std::size_t>(ty) * c.w + x1] * f;
            }
            cv.a(cx, cy) = std::max(cv.a(cx, cy), v / 255.0f);
        }
    }
}

// One line at `em` with the look's tracking: drawn letter by letter, each moved right by the tracking so far.
Coverage line(const PaintLook& look, int em, std::string_view text, const PaintRaster& raster) {
    if (look.tracking == 0) return raster(look.font, em, text);
    std::vector<Coverage> letters;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t n = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        const std::string_view ch = text.substr(i, n);
        Coverage c = raster(look.font, em, ch);
        if (ch == " ") c.w = raster(look.font, em, "n n").w - raster(look.font, em, "nn").w, c.a.clear();
        letters.push_back(std::move(c));
        i += n;
    }
    if (letters.empty()) return raster(look.font, em, text);
    Coverage out;
    out.ascent = letters[0].ascent, out.cap = letters[0].cap;
    for (const Coverage& c : letters) out.h = std::max(out.h, c.h);
    std::vector<int> at;
    int x10 = 0;
    for (const Coverage& c : letters) at.push_back((x10 + 5) / 10), x10 += c.w * 10 + look.tracking;
    out.w = at.back() + letters.back().w;
    out.a.assign(static_cast<std::size_t>(out.w) * out.h, 0);
    for (std::size_t k = 0; k < letters.size(); ++k) {
        const Coverage& c = letters[k];
        for (int y = 0; y < c.h && !c.a.empty(); ++y) {
            for (int x = 0; x < c.w; ++x) {
                std::uint8_t& d = out.a[static_cast<std::size_t>(y) * out.w + at[k] + x];
                d = std::max(d, c.a[static_cast<std::size_t>(y) * c.w + x]);
            }
        }
    }
    return out;
}

// The text laid out in the box: its lines and the width each is drawn at, the capitals' height and the distance of
// the baselines.
struct Layout {
    std::vector<Coverage> lines;
    std::vector<int> widths;
    int cap = 0, advance = 0;
};

// `parts` as lines at the largest em from the look's down to its minimum at which each fits the box's width and all
// fit its height; false when none does.
bool fit_lines(const PaintLook& look, const Box& b, const std::vector<std::string_view>& parts,
               const PaintRaster& raster, Layout* l) {
    for (int em = look.em; em >= look.min_em; --em) {
        std::vector<Coverage> lines;
        int w = 0;
        for (std::string_view part : parts) {
            lines.push_back(line(look, em, part, raster));
            w = std::max(w, lines.back().w);
        }
        const int cap = lines[0].cap, advance = parts.size() > 1 ? cap * 3 / 2 + 2 : 0;
        if (w > b.w || cap + advance * static_cast<int>(parts.size() - 1) > b.h) continue;
        l->cap = cap, l->advance = advance;
        for (Coverage& c : lines) l->widths.push_back(c.w), l->lines.push_back(std::move(c));
        return true;
    }
    return false;
}

Layout layout(const PaintLook& look, const Box& b, const std::string& text, const PaintRaster& raster) {
    Layout l;
    const std::string_view all(text);
    // the text's own line breaks ("Game\nSpeed"), else one line, else two at the space that makes the longer shortest
    std::vector<std::string_view> parts;
    for (std::size_t at = 0;;) {
        const std::size_t nl = all.find('\n', at);
        parts.push_back(all.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at));
        if (nl == std::string_view::npos) break;
        at = nl + 1;
    }
    if (fit_lines(look, b, parts, raster, &l)) return l;
    if (parts.size() == 1 && look.lines >= 2) {
        std::vector<std::string_view> best;
        int best_w = 0;
        for (std::size_t sp = all.find(' '); sp != std::string_view::npos; sp = all.find(' ', sp + 1)) {
            const int w = std::max(line(look, look.em, all.substr(0, sp), raster).w,
                                   line(look, look.em, all.substr(sp + 1), raster).w);
            if (best.empty() || w < best_w) best = {all.substr(0, sp), all.substr(sp + 1)}, best_w = w;
        }
        if (!best.empty() && fit_lines(look, b, best, raster, &l)) return l;
    }
    // narrower: every line at the smallest em, squeezed to the box's width
    for (std::string_view part : parts) {
        Coverage c = line(look, look.min_em, part, raster);
        l.cap = c.cap, l.advance = c.cap * 3 / 2 + 2;
        l.widths.push_back(std::min(c.w, b.w));
        l.lines.push_back(std::move(c));
    }
    if (parts.size() == 1) l.advance = 0;
    return l;
}

// Draws one state's ink over the cell at cell_x.
void ink_cell(Image& img, const Canvas& cv, const PaintInk& ink, int cell_x) {
    const Plane halo = blur(grow(cv.a, ink.grow), ink.blur);
    const Rgb text = rgb(ink.text), glow = rgb(ink.halo);
    const float strength = static_cast<float>(ink.strength) / 64.0f;
    for (int y = 0; y < cv.a.h; ++y) {
        for (int x = 0; x < cv.a.w; ++x) {
            const int px = cv.ox + x, py = cv.oy + y;
            if (py < 0 || py >= img.height) continue;
            std::uint8_t* p = at(img, cell_x + px, py);
            if (keyed(p)) continue;
            const int hx = x - ink.dx, hy = y - ink.dy;
            const float h = halo.in(hx, hy) ? std::min(1.0f, halo(hx, hy) * strength) : 0.0f;
            const float a = cv.a(x, y);
            Rgb v{p[0] + (glow.r - p[0]) * h, p[1] + (glow.g - p[1]) * h, p[2] + (glow.b - p[2]) * h};
            v = {v.r + (text.r - v.r) * a, v.g + (text.g - v.g) * a, v.b + (text.b - v.b) * a};
            p[0] = clamp8(v.r), p[1] = clamp8(v.g), p[2] = clamp8(v.b);
        }
    }
}

// ---- the tiles (L4b) ----

int luma(const std::uint8_t* p) { return (p[0] * 3 + p[1] * 6 + p[2]) / 10; }

int percentile(std::vector<int> v, int pct) {
    if (v.empty()) return 0;
    auto at_pct = v.begin() + static_cast<std::ptrdiff_t>((v.size() - 1) * pct / 100);
    std::nth_element(v.begin(), at_pct, v.end());
    return *at_pct;
}

// A row of colours (a strip of the heading being made).
struct Strip {
    int w = 0, h = 0;
    std::vector<Rgb> c;
    Strip(int width, int height) : w(width), h(height), c(static_cast<std::size_t>(width) * height) {}
    Rgb& operator()(int x, int y) { return c[static_cast<std::size_t>(y) * w + x]; }
    const Rgb& operator()(int x, int y) const { return c[static_cast<std::size_t>(y) * w + x]; }
};

// One letter tile of the original heading: its columns, its pixels with the letter taken off, the letter's extent.
struct Tile {
    int x0 = 0, x1 = 0;
    int top = 0, bottom = 0, ink_w = 0;  // the letter's first and last row and its width (band rows)
    int ink = 0, mask = 0;  // a letter's core is darker than `ink`, its edge darker than `mask` (from the tile's paper)
};

// The heading as found in the box: the band of rows the tiles fill, the tiles, a separator between two tiles and the
// widest stretch of background between them (or beside them), and the letters' colour.
struct Heading {
    int y0 = 0, y1 = 0;
    std::vector<Tile> tiles;
    int sep_x = -1, sep_w = 0, gap_x = -1, gap_w = 0;
    Rgb ink;
};

// The tiles in the box (its rows are the band): a separator is a column darker than both columns two away in most
// rows (a letter's stroke is not, above and below the letter); the pieces between the separators are tiles, cut where
// their lightest pixels turn dark: at the box's ends the background beside the heading, inside it the background
// between two of the original's `words`.
bool find_heading(const Image& img, const Box& b, int words, Heading* hd) {
    if (b.w < 8 || b.h < 6) return false;
    hd->y0 = b.y, hd->y1 = b.y + b.h;
    std::vector<int> edges = {0};
    for (int x = 2; x < b.w - 2; ++x) {
        int darker = 0;
        for (int y = hd->y0; y < hd->y1; ++y) {
            const int l = luma(at(img, b.x + x, y));
            darker += l + 12 < std::min(luma(at(img, b.x + x - 2, y)), luma(at(img, b.x + x + 2, y)));
        }
        if (darker * 10 > b.h * 7 && x - edges.back() > 2) edges.push_back(x);
    }
    edges.push_back(b.w);
    std::vector<int> light(static_cast<std::size_t>(b.w));
    for (int x = 0; x < b.w; ++x) {
        std::vector<int> col;
        for (int y = hd->y0; y < hd->y1; ++y) col.push_back(luma(at(img, b.x + x, y)));
        light[static_cast<std::size_t>(x)] = percentile(std::move(col), 95);
    }
    const int dark = percentile(light, 90) * 6 / 10;  // the tiles are most of the box, or the lightest part of it
    // the pieces between the separators, cut where they turn dark; a dark piece inside is the background between two
    // words only for the widest (words - 1) of them (rust on a tile can be as dark)
    struct Run {
        int x0, x1;
        bool dark;
    };
    std::vector<Run> runs;
    for (std::size_t i = 0; i + 1 < edges.size(); ++i) {
        const int a = i == 0 ? 0 : edges[i] + 1, e = edges[i + 1];
        for (int x = a; x < e;) {
            const bool is_dark = light[static_cast<std::size_t>(x)] < dark;
            const int k = x;
            while (x < e && (light[static_cast<std::size_t>(x)] < dark) == is_dark) ++x;
            if (x - k >= 4 || (k == a && x == e)) runs.push_back({k, x, is_dark});
        }
    }
    std::vector<std::size_t> inside;
    for (std::size_t i = 1; i + 1 < runs.size(); ++i) {
        if (runs[i].dark) inside.push_back(i);
    }
    std::sort(inside.begin(), inside.end(),
              [&](std::size_t p, std::size_t q) { return runs[p].x1 - runs[p].x0 > runs[q].x1 - runs[q].x0; });
    for (std::size_t k = static_cast<std::size_t>(std::max(0, words - 1)); k < inside.size(); ++k)
        runs[inside[k]].dark = false;
    int prev_end = -1;  // the end of the last tile, for the separator after it
    for (const Run& r : runs) {
        if (r.dark) {
            const bool edge = &r == &runs.front() || &r == &runs.back();
            if (!edge && r.x1 - r.x0 > hd->gap_w) hd->gap_x = b.x + r.x0, hd->gap_w = r.x1 - r.x0;
            prev_end = -1;
        } else if (r.x1 - r.x0 >= 6) {
            if (prev_end >= 0 && hd->sep_x < 0 && r.x0 - prev_end <= 3) {
                hd->sep_x = b.x + prev_end, hd->sep_w = r.x0 - prev_end;
            }
            hd->tiles.push_back({b.x + r.x0, b.x + r.x1});
            prev_end = r.x1;
        }
    }
    if (hd->tiles.empty()) return false;
    if (hd->gap_x < 0) {  // one word: the background beside the tiles in the box, if any
        const int left = hd->tiles.front().x0 - b.x, right = b.x + b.w - hd->tiles.back().x1;
        if (left >= right && left >= 3) hd->gap_x = b.x, hd->gap_w = left;
        if (right > left && right >= 3) hd->gap_x = hd->tiles.back().x1, hd->gap_w = right;
    }
    const int y0 = hd->y0, y1 = hd->y1;
    // the letters: the rows and columns with a few pixels far darker than the tile's paper (rust is lighter), and their
    // colour
    Rgb sum;
    int n = 0;
    for (Tile& t : hd->tiles) {
        std::vector<int> paper;
        for (int y = y0; y < y1; ++y) {
            for (int x = t.x0; x < t.x1; ++x) paper.push_back(luma(at(img, x, y)));
        }
        const int paper_lum = percentile(std::move(paper), 90);
        t.ink = paper_lum * 4 / 10, t.mask = paper_lum * 55 / 100;
        const int need = std::max(2, (t.x1 - t.x0) / 8);
        int x_lo = t.x1, x_hi = t.x0 - 1;
        t.top = y1 - y0, t.bottom = -1;
        for (int y = y0; y < y1; ++y) {
            int row = 0;
            for (int x = t.x0; x < t.x1; ++x) {
                const std::uint8_t* p = at(img, x, y);
                if (keyed(p) || luma(p) >= t.ink) continue;
                ++row;
                sum.r += p[0], sum.g += p[1], sum.b += p[2], ++n;
            }
            if (row >= need && row * 10 < (t.x1 - t.x0) * 7) {  // not a dark line across the tile (the frame's edge)
                t.top = std::min(t.top, y - y0), t.bottom = std::max(t.bottom, y - y0);
            }
        }
        for (int x = t.x0; x < t.x1; ++x) {
            int col = 0;
            for (int y = y0; y < y1; ++y) col += luma(at(img, x, y)) < t.ink;
            if (col >= 3) x_lo = std::min(x_lo, x), x_hi = std::max(x_hi, x);
        }
        t.ink_w = std::max(0, x_hi - x_lo + 1);
    }
    hd->ink = n ? Rgb{sum.r / n, sum.g / n, sum.b / n} : Rgb{24, 20, 16};
    return true;
}

// The words of the entry's English (the original heading's).
int english_words(const PaintedWord& w) {
    const std::string english = lang_text(lang_english(), w.key);
    return 1 + static_cast<int>(std::count(english.begin(), english.end(), ' '));
}

// The tile with its letter taken off: each letter pixel (one darker than the tile's `mask`, and the ones beside it)
// from the nearest other tile at the same place where that one has none, else the mean of the tile's other pixels in
// that row (of all of them where the whole row is dark, as the shaded top of a serif tile).
Strip blank_tile(const Image& img, const Heading& hd, std::size_t i) {
    const Tile& t = hd.tiles[i];
    const int w = t.x1 - t.x0, h = hd.y1 - hd.y0;
    auto letter = [&](const Tile& s, int u, int v) {  // u, v in the tile
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int x = s.x0 + u + dx, y = hd.y0 + v + dy;
                if (x < s.x0 || x >= s.x1 || y < hd.y0 || y >= hd.y1) continue;
                if (luma(at(img, x, y)) < s.mask) return true;
            }
        }
        return false;
    };
    Strip out(w, h);
    for (int v = 0; v < h; ++v) {
        Rgb row;
        int n = 0;
        for (int u = 0; u < w; ++u) {
            if (letter(t, u, v)) continue;
            const std::uint8_t* p = at(img, t.x0 + u, hd.y0 + v);
            row.r += p[0], row.g += p[1], row.b += p[2], ++n;
        }
        if (n) {
            row = {row.r / n, row.g / n, row.b / n};
        } else {  // the letter across the whole row: the row's own mean
            for (int u = 0; u < w; ++u) {
                const std::uint8_t* p = at(img, t.x0 + u, hd.y0 + v);
                row.r += p[0] / static_cast<float>(w), row.g += p[1] / static_cast<float>(w),
                    row.b += p[2] / static_cast<float>(w);
            }
        }
        for (int u = 0; u < w; ++u) {
            const std::uint8_t* p = letter(t, u, v) ? nullptr : at(img, t.x0 + u, hd.y0 + v);
            for (std::size_t d = 1; !p && d < hd.tiles.size(); ++d) {
                for (const std::size_t j : {i + d, i - d}) {  // i - d past 0 wraps: out of range
                    if (j >= hd.tiles.size()) continue;
                    const Tile& s = hd.tiles[j];
                    const int su = u * (s.x1 - s.x0) / w;
                    if (letter(s, su, v)) continue;
                    p = at(img, s.x0 + su, hd.y0 + v);
                    break;
                }
            }
            out(u, v) = p ? Rgb{static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2])} : row;
        }
    }
    return out;
}

// The coverage drawn `w` pixels wide (each column a mix of the two nearest).
Coverage squeezed(const Coverage& c, int w) {
    Coverage out = c;
    out.w = w;
    out.a.assign(static_cast<std::size_t>(w) * c.h, 0);
    for (int y = 0; y < c.h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float sx = (static_cast<float>(x) + 0.5f) * static_cast<float>(c.w) / static_cast<float>(w) - 0.5f;
            const int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, c.w - 1), x1 = std::min(x0 + 1, c.w - 1);
            const float f = std::clamp(sx - static_cast<float>(x0), 0.0f, 1.0f);
            const float v =
                c.a[static_cast<std::size_t>(y) * c.w + x0] * (1 - f) + c.a[static_cast<std::size_t>(y) * c.w + x1] * f;
            out.a[static_cast<std::size_t>(y) * w + x] = static_cast<std::uint8_t>(std::lround(v));
        }
    }
    return out;
}

// Copies `src`'s columns [sx, sx + sw) into `dst` from column dx on, `dw` columns wide (stretched or squeezed).
void put_columns(Strip& dst, int dx, int dw, const Strip& src, int sx, int sw) {
    for (int x = 0; x < dw; ++x) {
        const int s = sx + std::min(sw - 1, x * sw / std::max(1, dw));
        for (int y = 0; y < dst.h; ++y) dst(dx + x, y) = src(s, y);
    }
}

// The heading laid out again for `text`: one tile per letter (the original tile nearest in width, stretched to the
// letter), the original separator between two tiles, the background between the words; squeezed to the box when
// longer, centred where the original was.
void draw_tiles(Image& img, const PaintedWord& w, const std::string& text, const PaintRaster& raster) {
    const PaintLook& look = painted_look(w);
    const Box b = clip_box(w, img.width, img.height);
    Heading hd;
    if (b.w <= 0 || b.h <= 0 || text.empty() || !find_heading(img, b, english_words(w), &hd)) return;
    const int h = hd.y1 - hd.y0;
    // the letters' size: the capitals' height from the high letter tops (a fifth of the tiles: one stray speck is no
    // letter) to the most common bottom (the baseline)
    std::vector<int> tops, bottoms, pads;
    for (const Tile& t : hd.tiles) {
        if (t.bottom < t.top) continue;
        tops.push_back(t.top), bottoms.push_back(t.bottom);
        pads.push_back(t.x1 - t.x0 - t.ink_w);
    }
    if (tops.empty()) return;
    const int cap_top = percentile(tops, 20), baseline = percentile(bottoms, 50) + 1;
    const int pad = std::max(2, percentile(pads, 50));
    int em = look.min_em;
    for (int e = look.min_em; e <= look.em; ++e) {
        if (raster(look.font, e, "H").cap > baseline - cap_top) break;
        em = e;
    }
    std::vector<Strip> blanks;
    for (std::size_t i = 0; i < hd.tiles.size(); ++i) blanks.push_back(blank_tile(img, hd, i));
    Strip orig(img.width, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < img.width; ++x) {
            const std::uint8_t* p = at(img, x, hd.y0 + y);
            orig(x, y) = {static_cast<float>(p[0]), static_cast<float>(p[1]), static_cast<float>(p[2])};
        }
    }
    // the pieces: a tile per letter, a separator between two tiles, the background for a space
    struct Piece {
        int kind;  // 0 tile, 1 separator, 2 background
        int w;
        std::size_t tile;
        Coverage glyph;
    };
    std::vector<Piece> pieces;
    const int space_w = hd.gap_w > 0 ? hd.gap_w : pad * 3;
    std::vector<Coverage> glyphs;
    double glyphs_w = 0, inks_w = 0;
    int inks = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t n = lead < 0x80 ? 1 : lead < 0xe0 ? 2 : lead < 0xf0 ? 3 : 4;
        const std::string_view ch = std::string_view(text).substr(i, n);
        i += n;
        glyphs.push_back(ch == " " || ch == "\n" ? Coverage{} : raster(look.font, em, ch));
        glyphs_w += glyphs.back().w;
    }
    for (const Tile& t : hd.tiles) inks_w += t.ink_w, inks += t.ink_w > 0;
    // the letters no wider on average than the original's (a display font is often wider than the original stencil)
    std::size_t letters = 0;
    for (const Coverage& g : glyphs) letters += g.w > 0;
    const double squeeze =
        letters && inks ? std::min(1.0, inks_w / inks / (glyphs_w / static_cast<double>(letters))) : 1.0;
    std::size_t last = hd.tiles.size();
    for (Coverage& g : glyphs) {
        if (g.w == 0) {
            pieces.push_back({2, space_w, 0, {}});
            continue;
        }
        if (squeeze < 0.98) g = squeezed(g, std::max(1, static_cast<int>(std::lround(g.w * squeeze))));
        const int tw = std::max(g.w + pad, 8);
        std::size_t best = 0;
        int best_d = 1 << 30;
        for (std::size_t j = 0; j < hd.tiles.size(); ++j) {
            const int d = std::abs(hd.tiles[j].x1 - hd.tiles[j].x0 - tw) + (j == last ? 1000 : 0);
            if (d < best_d) best = j, best_d = d;
        }
        last = best;
        if (!pieces.empty() && pieces.back().kind == 0 && hd.sep_w > 0) pieces.push_back({1, hd.sep_w, 0, {}});
        pieces.push_back({0, tw, best, std::move(g)});
    }
    int total = 0;
    for (const Piece& p : pieces) total += p.w;
    if (total <= 0) return;
    Strip strip(total, h);
    Plane ink(total, h);
    int x = 0;
    for (const Piece& p : pieces) {
        if (p.kind == 0) {
            const Strip& t = blanks[p.tile];
            put_columns(strip, x, p.w, t, 0, t.w);
            const int gx = x + (p.w - p.glyph.w) / 2, gy = baseline - p.glyph.ascent;
            for (int v = 0; v < p.glyph.h; ++v) {
                for (int u = 0; u < p.glyph.w; ++u) {
                    if (!ink.in(gx + u, gy + v)) continue;
                    ink(gx + u, gy + v) =
                        std::max(ink(gx + u, gy + v), p.glyph.a[static_cast<std::size_t>(v) * p.glyph.w + u] / 255.0f);
                }
            }
        } else if (p.kind == 1) {
            put_columns(strip, x, p.w, orig, hd.sep_x, hd.sep_w);
        } else if (hd.gap_w <= 0) {
            put_columns(strip, x, p.w, blanks[0], 0, blanks[0].w);
        } else {
            for (int k = 0; k < p.w; k += hd.gap_w) {
                put_columns(strip, x + k, std::min(hd.gap_w, p.w - k), orig, hd.gap_x, std::min(hd.gap_w, p.w - k));
            }
        }
        x += p.w;
    }
    for (int y = 0; y < h; ++y) {
        for (int k = 0; k < total; ++k) {
            Rgb& c = strip(k, y);
            const float a = ink(k, y);
            c = {c.r + (hd.ink.r - c.r) * a, c.g + (hd.ink.g - c.g) * a, c.b + (hd.ink.b - c.b) * a};
        }
    }
    // the place: the background over the original tiles, then the strip centred where they were, inside the box
    const int t0 = hd.tiles.front().x0, t1 = hd.tiles.back().x1;
    const int out_w = std::min(total, b.w);
    const int left = std::clamp((t0 + t1) / 2 - out_w / 2, b.x, b.x + b.w - out_w);
    for (int y = 0; y < h; ++y) {
        for (int k = std::min(t0, left); k < std::max(t1, left + out_w); ++k) {
            std::uint8_t* p = at(img, k, hd.y0 + y);
            if (keyed(p)) continue;
            Rgb c;
            if (k >= left && k < left + out_w) {
                c = strip(std::min(total - 1, (k - left) * total / out_w), y);
            } else if (hd.gap_w > 0) {
                c = orig(hd.gap_x + (k - t0) % hd.gap_w, y);
            } else {
                continue;
            }
            p[0] = clamp8(c.r), p[1] = clamp8(c.g), p[2] = clamp8(c.b);
        }
    }
}

std::string lower_name(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    std::string s(slash == std::string_view::npos ? path : path.substr(slash + 1));
    for (char& ch : s) ch = static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
    return s;
}

}  // namespace

const std::vector<PaintedWord>& painted_words() { return kWords; }

const PaintedWord& painted_full_screen() {
    static const PaintedWord kFullScreen{
        "options.les", "1280x1024.bmp", "options.full_screen", 2, PaintFill::Cols, kResolution, 22, 9, 141, 29};
    return kFullScreen;
}

void painted_full_screen_blank(Image& img) {
    const PaintedWord& w = painted_full_screen();
    painted_blank(img, w, nullptr);
    // The columns' blend leaves streaks on the red glass: each row of the box is smoothed along the row (a box of
    // 2 * kR + 1 pixels, reaching kR beyond the box so its edges meet the picture).
    constexpr int kR = 8;
    const int cell_w = img.width / std::max<int>(1, w.cells);
    const Box b = clip_box(w, cell_w, img.height);
    for (int c = 0; c < w.cells; ++c) {
        for (int y = b.y; y < b.y + b.h; ++y) {
            std::vector<Rgb> row;
            for (int x = b.x; x < b.x + b.w; ++x) {
                Rgb sum;
                int n = 0;
                for (int k = std::max(0, x - kR); k <= std::min(cell_w - 1, x + kR); ++k) {
                    const std::uint8_t* p = at(img, c * cell_w + k, y);
                    if (keyed(p)) continue;
                    sum.r += p[0], sum.g += p[1], sum.b += p[2];
                    ++n;
                }
                row.push_back(n ? Rgb{sum.r / n, sum.g / n, sum.b / n} : Rgb{});
            }
            for (int x = b.x; x < b.x + b.w; ++x) {
                std::uint8_t* p = at(img, c * cell_w + x, y);
                if (keyed(p)) continue;
                const Rgb& v = row[static_cast<std::size_t>(x - b.x)];
                p[0] = clamp8(v.r), p[1] = clamp8(v.g), p[2] = clamp8(v.b);
            }
        }
    }
}

const PaintLook& painted_look(const PaintedWord& w) { return kLooks[w.look]; }

std::vector<const PaintedWord*> painted_find(std::string_view archive, std::string_view picture) {
    const std::string a = lower_name(archive), p = lower_name(picture);
    std::vector<const PaintedWord*> out;
    for (const PaintedWord& w : kWords) {
        if (a == w.archive && p == w.picture) out.push_back(&w);
    }
    return out;
}

void painted_blank(Image& img, const PaintedWord& w, const Image* under) {
    const int cell_w = img.width / std::max<int>(1, w.cells);
    const Box b = clip_box(w, cell_w, img.height);
    if (b.w <= 0 || b.h <= 0 || w.fill == PaintFill::Tiles) return;
    const Plane m = words_mask(img, w, b, cell_w);
    for (int c = 0; c < w.cells; ++c) {
        if (w.fill == PaintFill::Cols) {
            fill_cols(img, b, m, c * cell_w);
        } else {
            fill_cell(img, w, b, m, c * cell_w, cell_w, under);
        }
    }
}

void painted_draw(Image& img, const PaintedWord& w, const std::string& text, const PaintRaster& raster) {
    if (w.fill == PaintFill::Tiles) {
        draw_tiles(img, w, text, raster);
        return;
    }
    const PaintLook& look = painted_look(w);
    const int cell_w = img.width / std::max<int>(1, w.cells);
    const Box b = clip_box(w, cell_w, img.height);
    if (b.w <= 0 || b.h <= 0 || text.empty()) return;
    const Layout l = layout(look, b, text, raster);
    int margin = 2;
    for (const PaintInk& ink : look.ink)
        margin = std::max(margin, ink.grow + ink.blur + std::abs(ink.dx) + std::abs(ink.dy) + 2);
    Canvas cv;
    cv.ox = std::max(0, b.x - margin), cv.oy = std::max(0, b.y - margin);
    cv.a = Plane(std::min(cell_w, b.x + b.w + margin) - cv.ox, std::min(img.height, b.y + b.h + margin) - cv.oy);
    // the capitals' band centred on the box
    const int block = l.cap + (static_cast<int>(l.lines.size()) - 1) * l.advance;
    const int top = b.y + (b.h - block) / 2;
    for (std::size_t i = 0; i < l.lines.size(); ++i) {
        const int baseline = top + l.cap + static_cast<int>(i) * l.advance;
        put_line(cv, l.lines[i], b.x + (b.w - l.widths[i]) / 2, baseline, l.widths[i]);
    }
    for (int c = 0; c < w.cells; ++c) ink_cell(img, cv, look.ink[static_cast<std::size_t>(std::min(c, 2))], c * cell_w);
}

void painted_apply(Image& img, const PaintedWord& w, const Image* under, const std::string& text,
                   const PaintRaster& raster) {
    painted_blank(img, w, under);
    painted_draw(img, w, text, raster);
}

double painted_difference(const Image& a, const Image& b, const PaintedWord* w) {
    if (a.width != b.width || a.height != b.height) return 255.0;
    const int cells = w ? std::max<int>(1, w->cells) : 1, cell_w = a.width / cells;
    Box box{0, 0, cell_w, a.height};
    if (w) {
        const Box in = clip_box(*w, cell_w, a.height);
        box.x = std::max(0, in.x - 4), box.y = std::max(0, in.y - 4);
        box.w = std::min(cell_w, in.x + in.w + 4) - box.x, box.h = std::min(a.height, in.y + in.h + 4) - box.y;
    }
    double sum = 0;
    std::size_t n = 0;
    for (int c = 0; c < cells; ++c) {
        for (int y = box.y; y < box.y + box.h; ++y) {
            for (int x = box.x; x < box.x + box.w; ++x) {
                const std::uint8_t* p = at(a, c * cell_w + x, y);
                const std::uint8_t* q = at(b, c * cell_w + x, y);
                if (keyed(p)) continue;
                for (int k = 0; k < 3; ++k) sum += std::abs(static_cast<int>(p[k]) - static_cast<int>(q[k]));
                n += 3;
            }
        }
    }
    return n ? sum / static_cast<double>(n) : 0.0;
}

}  // namespace antanta::ui
