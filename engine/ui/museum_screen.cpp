#include "ui/museum_screen.hpp"

#include <algorithm>

namespace antanta::ui {

void museum_filter(const MuseumFile& file, MuseumState& s) {
    s.list.clear();
    for (std::size_t i = 0; i < file.entries.size(); ++i) {
        if (file.entries[i].nation == s.nation && file.entries[i].type == s.type) s.list.push_back(static_cast<int>(i));
    }
    s.pick = s.list.empty() ? -1 : 0;
    museum_select(file, s);
}

bool museum_select(const MuseumFile&, MuseumState& s) {
    if (s.pick >= 0 && s.pick < static_cast<int>(s.list.size())) {
        s.entry = s.list[static_cast<std::size_t>(s.pick)];
        s.picture = 0;
        return true;
    }
    s.entry = -1;
    return false;
}

void museum_set_nation(const MuseumFile& file, MuseumState& s, int nation) {
    s.nation = nation;
    museum_filter(file, s);
}

void museum_set_type(const MuseumFile& file, MuseumState& s, int type) {
    s.type = type;
    museum_filter(file, s);
}

void museum_pick_row(const MuseumFile& file, MuseumState& s, int row) {
    s.pick = row;
    museum_select(file, s);
}

void museum_next_picture(const MuseumFile& file, MuseumState& s) {
    if (s.entry < 0) return;
    const int n = static_cast<int>(file.entries[static_cast<std::size_t>(s.entry)].pictures.size());
    if (n == 0) return;
    if (++s.picture >= n) s.picture = 0;
}

void museum_prev_picture(const MuseumFile& file, MuseumState& s) {
    if (s.entry < 0) return;
    const int n = static_cast<int>(file.entries[static_cast<std::size_t>(s.entry)].pictures.size());
    if (n == 0) return;
    if (--s.picture < 0) s.picture = n - 1;
}

bool museum_has_arrows(const MuseumFile& file, const MuseumState& s) {
    return s.entry >= 0 && file.entries[static_cast<std::size_t>(s.entry)].pictures.size() > 1;
}

int combo_rows(int list_h, int row_h) { return list_h / std::max(1, row_h); }

namespace {

// The end of the arrow and track presses: the first shown row held so the last rows fill the list.
int hold_full(int top, int count, int list_h, int row_h) {
    const int rows = combo_rows(list_h, row_h);
    if (count < top + rows) top = std::max(count - rows, 0);
    return top;
}

}  // namespace

int combo_scroll_down(int top, int count, int list_h, int row_h) {
    ++top;
    if (top >= count) top = count - 1;
    return hold_full(top, count, list_h, row_h);
}

int combo_scroll_up(int top) { return std::max(top - 1, 0); }

int combo_track_top(int dy, int count, int list_h, int row_h) {


    const int top = static_cast<int>(static_cast<std::uint8_t>(dy * count / std::max(1, list_h)));
    return hold_full(top, count, list_h, row_h);
}

bool combo_thumb_shown(int count, int list_h, int row_h) { return count > combo_rows(list_h, row_h); }

int combo_thumb_y(int top, int count, int list_h, int row_h, int top_limit, int bottom_limit) {
    const int rows = combo_rows(list_h, row_h);
    bottom_limit = std::max(bottom_limit, top_limit);
    if (count <= rows) return top_limit;
    return top_limit + top * (bottom_limit - top_limit) / (count - rows);
}

}  // namespace antanta::ui
