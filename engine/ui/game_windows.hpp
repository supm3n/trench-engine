




#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "formats/bytes.hpp"
#include "sim/state.hpp"
#include "ui/hud.hpp"
#include "ui/lang.hpp"
#include "ui/panel.hpp"
#include "ui/unit_panel.hpp"

namespace antanta::ui {

// The actions of the two panels' buttons (PanelWidget::action).
enum GameWindowAction : int {
    kGwPauseClose = 200,
    kGwGroupToggle,
    kGwGroup,
};





Panel pause_window(int w, int h, int k);





inline constexpr int kDragStripPause = 33, kDragStripMission = 44, kDragStripMessage = 41, kDragStripOptions = 42;

// A press (event 0) that no child took, strictly inside the strip, starts the drag and keeps where the window was
// grabbed; while the left button is held a move (event 4) puts the window at the pointer less that offset, kept
// inside the screen (x + w >= W gives W - w - 1, the same down; then below 1 gives 1); a move without the button
// ends it. All in one pixel space (the screen's, the window's top left).
struct WindowDrag {
    bool on = false;
    int dx = 0, dy = 0;  // the press point less the window's corner
};
bool window_drag_press(WindowDrag& d, int win_x, int win_y, int win_w, int strip_h, int x, int y);
// The window's new top left for a move to (x, y) while the drag is on and the button held.
std::array<int, 2> window_drag_to(const WindowDrag& d, int win_w, int win_h, int screen_w, int screen_h, int x, int y);







bool group_selected(const sim::GameState& s, const Selection& sel, const sim::Group& g);


// w x h pixels at scale k: the toggle groupwindow.bmp at (5, 5) (tooltip IDS_GROUP_SHOW), and while `shown` one

// from (45, 5) 44 apart, smaller when more than fit (sqrt((W - 190) * 85 / n) - 4), wrapping into rows; tooltip
// IDS_GROUP_NUMBER and the number from 1, the red frame on a selected group, the living members' count in Times New


Panel group_bar(const sim::GameState& s, const Selection& sel, const StringTable& strings, bool shown, int w, int h,
                int k);




bool group_bar_click(const sim::GameState& s, Selection& sel, std::uint32_t serial, bool shift);





int textbox_max_top(int count, int height, int line_h);

int textbox_down(int top, int count, int height, int line_h);

int textbox_rows(int height, int line_h);


int textbox_thumb_y(int top, int count, int height, int line_h, int track_top, int track_bottom, int thumb_h);


int textbox_track_top(int y, int count, int height, int line_h, int track_top, int track_bottom);






std::vector<std::string> textbox_wrap(const std::string& text, int width,
                                      const std::function<int(const std::string&)>& char_width);






struct MissionLog {
    std::vector<std::string> entries;
};
MissionLog mission_log_start(const StringTable& strings);
void mission_log_add(MissionLog& log, const std::string& text);
// What the text box holds: the entries with their separators, in order.
std::vector<std::string> mission_log_paragraphs(const MissionLog& log);





std::optional<std::vector<std::string>> parse_hints(ByteSpan file, std::string* error);

int hint_next(int index, int count, bool shift);
int hint_prev(int index, int count, bool shift);
// Ours: the new engine's key list, the last page of the hints window (F1 opens the mission window as in the original),
// in the display language.
std::string keys_page(const Language& l);






struct DiplomacyRow {
    std::uint8_t player = 0;
    std::string text;
    std::uint32_t rgb = 0;  // 0xRRGGBB
    bool our_war = true, their_war = true;
};
std::vector<DiplomacyRow> diplomacy_rows(const sim::GameState& s, const std::vector<std::string>& names,
                                         const StringTable& strings);



sim::Command diplomacy_order(const sim::GameState& s, const std::vector<DiplomacyRow>& rows);

}  // namespace antanta::ui
