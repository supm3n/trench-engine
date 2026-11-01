// The original's in-game interface, the part without SDL (docs/interface.md part A, slice I1): where the frame, the
// resource bar, the thermometer and the screen texts go for a window of any size and an interface scale, the number

// setting and the camera bounds of a game view larger than 1024 x 768. Drawing is in ui/iface_draw.hpp.
//
// Positions: the original computes every HUD position from the screen size W x H (A.2). Here W x H is the window
// in pixels and k the interface scale: offsets and sizes of the original are multiplied by k and measured from the
// edge the original measures them from (the thermometer W - 32 becomes W - 32k), so k = 1 gives the original's
// numbers for its five modes. The resource numbers keep base * W / 1024, so they stay on their stretched labels.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "render/iso.hpp"

namespace antanta::ui {

// ---- the interface scale and the world zoom (PLAN.md 2026-10-15) ----

// Auto: the whole number nearest to the smaller of W / 1440 and H / 900, at least 1. That gives 1 at 1920 x 1080,
// 2 at 2560 x 1440 and 3840 x 2160, 3 at 5120 x 2880 (and 2 for a 2560 x 1440 window less its title bar).
int auto_ui_scale(int w, int h);
// The interface scale for the setting (0 Auto, 1..4) and the window.
int ui_scale_for(std::uint8_t setting, int w, int h);
// The world zoom for the setting (0 Auto: the interface scale, 1..4) and the window.
int world_zoom_for(std::uint8_t setting, std::uint8_t ui_scale_setting, int w, int h);

// How a picture is scaled to k times its size: Between ("in between": nearest neighbour to the whole multiple with
// softened edges between the source pixels, the B sample of out\ui_scale_demo), Crisp (nearest neighbour), Smooth
// (bilinear). k = 1 leaves every picture as it is.
enum class PictureFilter : std::uint8_t { Between = 0, Crisp = 1, Smooth = 2 };
// RGBA8 w x h to RGBA8 (w k) x (h k). Transparent pixels (alpha 0, the magenta key) do not bleed their colour.
std::vector<std::uint8_t> scale_picture(const std::vector<std::uint8_t>& rgba, int w, int h, int k, PictureFilter f);

// ---- the layout of part A ----

struct IRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool operator==(const IRect&) const = default;
};

// In window pixels.
struct HudLayout {
    int width = 0, height = 0, scale = 1;


    IRect top, bottom, left, right;
    // A.4: Thermometer.bmp (19 x 214) at (W - 32, 23); the fill column x W - 24 .. W - 21.
    IRect thermometer;
    int fill_x = 0, fill_w = 0;


    std::array<int, 6> number_x{};
    int number_y = 0;
    int shadow_dx = 0, shadow_dy = 0;
    // A.3 "Build more houses." at (30, 22); A.7 "Entente v 2.0" at (885, 20).
    int build_x = 0, build_y = 0;
    int version_x = 0, version_y = 0;
};
HudLayout hud_layout(int w, int h, int k);



struct ThermometerFill {
    int top = 0;     // screen y of the original at k = 1 (the column runs top .. 190)
    int bottom = 0;  // 191
    std::uint8_t r = 0, g = 0, b = 0;
};
ThermometerFill thermometer_fill(float power_made, float power_used, float factor_6c4, float factor_6e4);

// A.3: _ltow of the stock truncated toward zero: plain decimal, no separators, a minus sign when negative. (The
// original's long wraps above 2,147,483,647; ours prints the true number, the owner's fix rule.)
std::string resource_text(double value);

// A.7: "Entente v 2.0" shows while the window list holds only one window, which in a game started from the main
// menu (or loaded) means: no selection panel (units selected, or exactly one house) and no other window.
bool version_visible(bool units_selected, bool one_house_selected);



struct ScreenText {
    std::string text;
    int x = 0, y = 0;       // interface pixels (window pixels at scale 1)
    std::uint32_t rgb = 0;  // 0xRRGGBB
    int frames = 0;         // frames left
};
struct ScreenTexts {
    std::vector<ScreenText> entries;  // at most kMaxScreenTexts
};
constexpr std::size_t kMaxScreenTexts = 30;
// The width of a text in the message font (Times New Roman Bold 16) at scale 1, in pixels.
using TextWidth = std::function<int(const std::string&)>;


void screen_text_add(ScreenTexts& list, int x, int y, const std::string& text, int frames, std::uint32_t rgb, bool up,
                     int w, int h, bool minimap_shown, const TextWidth& width);

void screen_text_event(ScreenTexts& list, const std::string& text, std::uint32_t rgb, int w, int h, bool minimap_shown,
                       const TextWidth& width);

void screen_texts_frame(ScreenTexts& list);

// ---- the game view ----


struct ViewCells {
    int cols = 0, rows = 0;
};
ViewCells view_cells(int w, int h);


ViewPos clamp_camera(const MapData& map, int w, int h, ViewPos p);

}  // namespace antanta::ui
