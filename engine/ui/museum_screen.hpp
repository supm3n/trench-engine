

#pragma once

#include <vector>

#include "formats/museum.hpp"

namespace antanta::ui {


struct MuseumState {
    int nation = 0, type = 0;
    std::vector<int> list;
    int pick = -1;
    int entry = -1;
    int picture = 0;
};


void museum_filter(const MuseumFile& file, MuseumState& s);


bool museum_select(const MuseumFile& file, MuseumState& s);

void museum_set_nation(const MuseumFile& file, MuseumState& s, int nation);
void museum_set_type(const MuseumFile& file, MuseumState& s, int type);
void museum_pick_row(const MuseumFile& file, MuseumState& s, int row);


void museum_next_picture(const MuseumFile& file, MuseumState& s);
void museum_prev_picture(const MuseumFile& file, MuseumState& s);

bool museum_has_arrows(const MuseumFile& file, const MuseumState& s);



constexpr int kMuseumRowH = 45;
constexpr int kMuseumListH = 315;

// How many rows the open list shows.
int combo_rows(int list_h, int row_h);

int combo_scroll_down(int top, int count, int list_h, int row_h);
// A press on the up arrow.
int combo_scroll_up(int top);
// A press on the track between the arrows: `dy` is the press' distance below the list's top; the first shown row is
// dy / list_h of the way through the items (a byte in the original, so a press above the list wraps), then held as
// the down arrow does.
int combo_track_top(int dy, int count, int list_h, int row_h);



int combo_thumb_y(int top, int count, int list_h, int row_h, int top_limit, int bottom_limit);
bool combo_thumb_shown(int count, int list_h, int row_h);

}  // namespace antanta::ui
