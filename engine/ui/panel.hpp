// The panel toolkit of the original's in-game interface, the part without SDL (docs/interface.md B.1 to B.7, slice I4):
// a panel window with no background, its widgets (40 x 40 buttons from three-frame picture strips, pictures, live bars,
// text labels), the button states (normal, pressed, hover, disabled with the shadow overlay, the red active frame),

// (ui/unit_panel.hpp) is the first user; the build menu, the house panel, research, the market and the windows (slices
// I5 to I8) build their own Panel the same way. Drawing is in ui/panel_draw.hpp.
//
// Positions: widgets are placed in the original's pixels relative to the panel's top left corner. On the screen the
// panel's corner and every offset and size are multiplied by the interface scale k (ui/iface_layout.hpp), so k = 1
// gives the original's positions.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ui/iface_layout.hpp"

namespace antanta::ui {

// The archives the panels read (Data/Interface/...). Every entry of interface.les and InterfaceGroup.les has id 55555,
// so those are found by name; the icon archives are found by entry id (B.3).
inline constexpr const char* kInterfaceLes = "Data/Interface/interface.les";
inline constexpr const char* kInterfaceGroupLes = "Data/Interface/InterfaceGroup.les";
inline constexpr const char* kUnitsIconsLes = "Data/Interface/Icons/UnitsIcons.les";
inline constexpr const char* kHouseIconsLes = "Data/Interface/Icons/HouseIcons.les";


struct PanelPic {
    std::string archive;
    std::string name;
    std::uint16_t id = 0;
    bool operator==(const PanelPic&) const = default;
};
PanelPic pic_named(const char* archive, const std::string& name);
PanelPic pic_id(const char* archive, std::uint16_t id);
// A house type's icon (HouseIcons.les, entry id = type). The Building Academy (type 3, Entente Engine Plus F7) has
// no entry there: its trowel icon is in interface.les (germany_stroy_acad.bmp; France's and Russia's are the same).
PanelPic house_icon(std::uint16_t type);




enum class PanelFont : std::uint8_t { Serif16, Serif18, Sans14, Sans10 };

// Ours (docs/languages.md 2.5, the place panel.info): the room of a label of the info column (x 5) up to the buttons at
// x 195; a longer one (a translation) is drawn smaller or cut (ui/panel_draw.cpp). A label's rect.w is its room, 0
// for none.
inline constexpr int kPanelInfoW = 188;

inline constexpr std::uint32_t kPanelYellow = 0xffff80;
inline constexpr std::uint32_t kPanelRed = 0xc82828;

struct PanelWidget {



    enum class Kind : std::uint8_t { Button, Picture, Label };
    Kind kind = Kind::Button;
    IRect rect;  // relative to the panel, the original's pixels
    PanelPic pic;


    int frames = 3;
    int frame = 0;
    std::string text;  // a Label's text or a Button's caption (drawn at the top left, B.3)
    PanelFont font = PanelFont::Serif16;
    std::uint32_t rgb = kPanelYellow;  // 0xRRGGBB
    bool shadow = true;
    std::string tooltip;
    char key = 0;
    bool disabled = false;
    bool active = false;
    // What a left click (or the key) and a right click do: the panel's own action numbers (0: nothing).
    int action = 0, arg = 0;
    int right_action = 0, right_arg = 0;
};

struct Panel {
    // The panel's top left corner and size in the original's pixels, measured as the original does: x from the left
    // edge, y from the bottom (the unit panel is at (10, H - 208), so bottom = 208).
    int x = 0, bottom = 0, w = 0, h = 0;
    std::vector<PanelWidget> widgets;        // drawn in this order
    int tooltip_x = 30, tooltip_above = 20;  // B.7: tooltips at (30, panel top - 20)
};

// The panel's rectangle on a window of w x h pixels at scale k.
IRect panel_rect(const Panel& p, int w, int h, int k);
// A widget's rectangle on that window.
IRect widget_rect(const Panel& p, const PanelWidget& wd, int w, int h, int k);

// The button under window pixel (x, y), or -1 (only buttons take events; disabled ones show their tooltip).
int panel_hit(const Panel& p, int w, int h, int k, int x, int y);
// Whether the point is on the panel at all (a press there is not a press on the world).
bool panel_contains(const Panel& p, int w, int h, int k, int x, int y);




struct PanelInput {
    int pressed = -1;
    bool pressed_right = false;
    int hover = -1;
};
// What a click asks the panel's owner to do.
struct PanelClick {
    int action = 0, arg = 0;
};
// A press over the panel: remembers the button under it (or none). True when the press was on the panel.
bool panel_press(const Panel& p, PanelInput& in, int w, int h, int k, int x, int y, bool right);


std::optional<PanelClick> panel_release(const Panel& p, PanelInput& in, int w, int h, int k, int x, int y, bool right);
// A character key (event 3): the first enabled button whose hot key equals it.
std::optional<PanelClick> panel_key(const Panel& p, char key);

int panel_state(const Panel& p, const PanelInput& in, int index);

// B.7: the tooltip of the hovered button as screen text lines in interface pixels (window pixels / k, the units of
// the screen text list): at (30, panel top - 20); a text over 128 letters is cut at its last space before letter 128
// into two lines, the first 13 pixels higher. The original counts bytes; ours counts letters (UTF-8 characters,
// docs/languages.md 2.2, slice L2b), so a Russian tooltip is not cut after 64 letters. English is cut as before. The
// screen text list then wraps each line at the screen's width in pixels. h: the window height in pixels.
struct TooltipLine {
    std::string text;
    int x = 0, y = 0;
};
std::vector<TooltipLine> panel_tooltip(const Panel& p, const PanelInput& in, int h, int k);

}  // namespace antanta::ui
