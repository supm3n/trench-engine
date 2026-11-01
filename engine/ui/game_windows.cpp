#include "ui/game_windows.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace antanta::ui {

namespace {

std::string text_of(const StringTable& strings, const std::string& key) {
    const auto it = strings.find(key);
    return it != strings.end() ? it->second : key;
}

bool same_unit(const sim::UnitRef& a, const sim::UnitRef& b) { return a.id == b.id && a.serial == b.serial; }

// The selection's id and serial pairs, so a member's test is one lookup and not a walk of the whole selection.
std::unordered_set<std::uint64_t> selected_keys(const Selection& sel) {
    std::unordered_set<std::uint64_t> out;
    out.reserve(sel.units.size());
    for (const sim::UnitRef& r : sel.units) out.insert(std::uint64_t{r.id} << 32 | r.serial);
    return out;
}

// The group's members that still exist, in member order.
std::vector<sim::UnitRef> living_members(const sim::GameState& s, const sim::Group& g) {
    std::vector<sim::UnitRef> out;
    for (const sim::UnitRef& m : g.members) {
        if (m.player == 0xff) continue;
        if (sim::Unit_Find(s, m) != nullptr) out.push_back(m);
    }
    return out;
}

// The group's living members are all in the selection (`keys`, selected_keys).
bool members_selected(const sim::GameState& s, const std::unordered_set<std::uint64_t>& keys, const sim::Group& g) {
    const std::vector<sim::UnitRef> m = living_members(s, g);
    return !m.empty() && std::all_of(m.begin(), m.end(), [&](const sim::UnitRef& u) {
        return keys.count(std::uint64_t{u.id} << 32 | u.serial) != 0;
    });
}


double clamp01(double f) { return f > 1.0 ? 1.0 : f < 0.0 ? 0.0 : f; }


int ftol(double v) { return static_cast<int>(v); }

}  // namespace

// ---- the pause window ----

Panel pause_window(int w, int h, int k) {
    k = std::max(1, k);
    Panel p;
    p.w = 250, p.h = 150;
    const int iw = w / k, ih = h / k;  // the screen in the original's pixels
    p.x = iw / 2 - p.w / 2;
    p.bottom = ih - (ih / 2 - p.h / 2);
    PanelWidget back;
    back.kind = PanelWidget::Kind::Picture;
    back.rect = {0, 0, 250, 150};
    back.pic = pic_named(kInterfaceLes, "window_pause.bmp");
    back.frames = 1;
    p.widgets.push_back(std::move(back));
    PanelWidget close;
    close.rect = {222, 10, 18, 13};
    close.pic = pic_named(kInterfaceLes, "Window_close.bmp");
    close.action = kGwPauseClose;
    p.widgets.push_back(std::move(close));
    return p;
}

// ---- moving a window ----

bool window_drag_press(WindowDrag& d, int win_x, int win_y, int win_w, int strip_h, int x, int y) {
    const int lx = x - win_x, ly = y - win_y;
    d.on = strip_h > 0 && lx > 0 && lx < win_w && ly > 0 && ly < strip_h;
    if (d.on) d.dx = lx, d.dy = ly;
    return d.on;
}

std::array<int, 2> window_drag_to(const WindowDrag& d, int win_w, int win_h, int screen_w, int screen_h, int x, int y) {
    int nx = x - d.dx, ny = y - d.dy;
    if (nx + win_w >= screen_w) nx = screen_w - win_w - 1;
    if (ny + win_h >= screen_h) ny = screen_h - win_h - 1;
    if (ny < 1) ny = 1;
    if (nx < 1) nx = 1;
    return {nx, ny};
}

// ---- the group bar ----

bool group_selected(const sim::GameState& s, const Selection& sel, const sim::Group& g) {
    return members_selected(s, selected_keys(sel), g);
}

Panel group_bar(const sim::GameState& s, const Selection& sel, const StringTable& strings, bool shown, int w, int h,
                int k) {
    k = std::max(1, k);
    Panel p;
    const int width = w / k - 100;
    p.x = 50;
    p.w = 50, p.h = 50;
    p.tooltip_x = 55;
    p.tooltip_above = 40 - 27;
    PanelWidget toggle;
    toggle.rect = {5, 5, 40, 40};
    toggle.pic = pic_named(kInterfaceGroupLes, "groupwindow.bmp");
    toggle.tooltip = text_of(strings, "IDS_GROUP_SHOW");
    toggle.action = kGwGroupToggle;
    p.widgets.push_back(std::move(toggle));
    std::vector<const sim::Group*> groups;
    if (shown && s.my_player < s.players.size()) {
        for (const sim::Group& g : s.players[s.my_player].groups) {
            if (g.ai_owned == 0) groups.push_back(&g);
        }
    }
    if (shown && groups.empty()) {
        p.widgets.clear();
    } else if (!groups.empty()) {
        const int n = std::min<int>(static_cast<int>(groups.size()), 99);
        int size = 40;
        if (n >= ((width - 90) / 45) * ((90 - 5) / 45)) {
            const int v = ftol(std::sqrt((static_cast<double>(width) - 90.0) * 85.0 / n)) - 4;
            if (static_cast<std::uint16_t>(v) <= 40) size = v;  // a word compare: a negative size gives 40
        }
        const int step = size + 4;
        const int per_row = std::max(1, (width - step - 45) / step);
        int made = 0;
        const std::unordered_set<std::uint64_t> keys = selected_keys(sel);
        for (const sim::Group* g : groups) {
            PanelWidget b;
            b.rect = {(made % per_row) * step + 45, (made / per_row) * step + 5, size, size};
            b.pic = pic_named(kInterfaceGroupLes, "group.bmp");
            b.tooltip = text_of(strings, "IDS_GROUP_NUMBER") + std::to_string(made + 1);
            b.active = members_selected(s, keys, *g);
            b.action = kGwGroup;
            b.arg = static_cast<int>(g->serial);
            p.widgets.push_back(std::move(b));
            if (g->live != 0) {
                PanelWidget dark;
                dark.kind = PanelWidget::Kind::Label;
                dark.rect = {(made % per_row) * step + 45 + 3, (made / per_row) * step + 5 + 3, size, 20};
                dark.text = std::to_string(g->live);
                dark.font = PanelFont::Serif18;
                dark.rgb = 0x000000;
                dark.shadow = false;
                PanelWidget light = dark;
                light.rect.x -= 2, light.rect.y -= 2;
                light.rgb = 0xfff799;
                p.widgets.push_back(std::move(dark));
                p.widgets.push_back(std::move(light));
            }
            if (++made > 98) break;
        }
        p.h = (made / per_row + 1) * step + 5;
        p.w = made < per_row ? (made % per_row + 1) * step + 45 : width;
    }
    p.bottom = h / k - 40;
    return p;
}

bool group_bar_click(const sim::GameState& s, Selection& sel, std::uint32_t serial, bool shift) {
    if (s.my_player >= s.players.size()) return false;
    const auto& groups = s.players[s.my_player].groups;
    const auto it = std::find_if(groups.begin(), groups.end(), [&](const sim::Group& g) { return g.serial == serial; });
    if (it == groups.end()) return false;
    const bool selected = group_selected(s, sel, *it);
    const std::vector<sim::UnitRef> members = living_members(s, *it);
    if (shift && selected) {
        std::erase_if(sel.units, [&](const sim::UnitRef& r) {
            return std::any_of(members.begin(), members.end(), [&](const sim::UnitRef& m) { return same_unit(m, r); });
        });
        return false;
    }
    if (!shift) clear_selection(sel);
    sel.house.reset();                 // AddUnitToSelection: no house panel
    select_units(sel, members);
    return !shift && selected;
}

// ---- the text box ----

int textbox_max_top(int count, int height, int line_h) { return std::max(0, count - height / std::max(1, line_h)); }

int textbox_down(int top, int count, int height, int line_h) {
    if (top < count - 1 && height / std::max(1, line_h) + top < count) ++top;
    return std::min(top, textbox_max_top(count, height, line_h));
}

int textbox_rows(int height, int line_h) {
    line_h = std::max(1, line_h);
    return (height + line_h - 1) / line_h;
}

int textbox_thumb_y(int top, int count, int height, int line_h, int track_top, int track_bottom, int thumb_h) {
    double f = 0.0;
    if (top != 0 && count != 0) {
        line_h = std::max(1, line_h);
        if (height / line_h + top >= count) {
            f = 1.0;
        } else {
            f = clamp01(static_cast<double>(top) /
                        (static_cast<double>(count) - static_cast<double>(height) / line_h - 1.0));
        }
    }
    return ftol(static_cast<double>(track_bottom - thumb_h - track_top) * f + track_top);
}

int textbox_track_top(int y, int count, int height, int line_h, int track_top, int track_bottom) {
    const int span = track_bottom - track_top;
    const double f = clamp01(span != 0 ? static_cast<double>(y - track_top) / span : 0.0);
    const int top = ftol((static_cast<double>(count) - static_cast<double>(height) / std::max(1, line_h)) * f);
    return std::min(std::max(top, 0), textbox_max_top(count, height, line_h));
}

std::vector<std::string> textbox_wrap(const std::string& text, int width,
                                      const std::function<int(const std::string&)>& char_width) {
    std::vector<std::string> ch;  // the text's characters (UTF-8 sequences)
    for (std::size_t i = 0; i < text.size();) {
        std::size_t n = 1;
        const auto c = static_cast<unsigned char>(text[i]);
        if (c >= 0xf0) {
            n = 4;
        } else if (c >= 0xe0) {
            n = 3;
        } else if (c >= 0xc0) {
            n = 2;
        }
        n = std::min(n, text.size() - i);
        ch.push_back(text.substr(i, n));
        i += n;
    }
    auto join = [&](std::size_t from, std::size_t to) {
        std::string out;
        for (std::size_t i = from; i < to; ++i) out += ch[i];
        return out;
    };
    std::vector<std::string> lines;
    std::size_t start = 0, i = 0;
    int sum = 0;
    while (i < ch.size()) {
        sum += char_width(ch[i]);
        std::size_t end = i;
        if (ch[i] == "\n" || sum >= width) {
            if (i == start) {  // a line feed (or one too wide character) at the line's start: an empty line
                lines.emplace_back();
                start = i + 1;
            } else {
                if (sum >= width) {  // back to the last space after the line's start, else break here
                    for (std::size_t j = i; j > start; --j) {
                        if (ch[j] == " ") {
                            end = j;
                            break;
                        }
                    }
                }
                lines.push_back(join(ch[start] == " " ? start + 1 : start, end));
                start = ch[end] == "\n" ? end + 1 : end;
            }
            sum = 0;
        }
        i = end + 1;
    }
    if (sum != 0) lines.push_back(join(start, ch.size()));  // the last line as it is
    return lines;
}

// ---- the mission window ----

MissionLog mission_log_start(const StringTable& strings) {
    MissionLog log;
    log.entries.push_back(text_of(strings, "IDS_RANDOM_ZADANIE"));
    return log;
}

void mission_log_add(MissionLog& log, const std::string& text) {
    if (!text.empty()) log.entries.push_back(text);
}

std::vector<std::string> mission_log_paragraphs(const MissionLog& log) {
    std::vector<std::string> out;
    for (const std::string& e : log.entries) {
        out.push_back(e);
        out.push_back("  ");
    }
    return out;
}

// ---- the hints window ----

std::optional<std::vector<std::string>> parse_hints(ByteSpan file, std::string* error) {
    auto u32 = [&](std::size_t at) {
        return static_cast<std::uint32_t>(file[at]) | static_cast<std::uint32_t>(file[at + 1]) << 8 |
               static_cast<std::uint32_t>(file[at + 2]) << 16 | static_cast<std::uint32_t>(file[at + 3]) << 24;
    };
    if (file.size() < 4) {
        *error = "Hints.uni: too short";
        return std::nullopt;
    }
    const std::uint32_t count = u32(0);
    std::vector<std::string> out;
    std::size_t at = 4;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (at + 4 > file.size()) {
            *error = "Hints.uni: hint " + std::to_string(i) + " is cut off";
            return std::nullopt;
        }
        const std::size_t chars = u32(at);
        at += 4;
        if (chars > (file.size() - at) / 2) {
            *error = "Hints.uni: hint " + std::to_string(i) + " is cut off";
            return std::nullopt;
        }
        std::string t = utf16le_to_utf8(file.subspan(at, chars * 2));
        std::erase(t, '\r');
        out.push_back(std::move(t));
        at += chars * 2;
    }
    return out;
}

int hint_next(int index, int count, bool shift) {
    index += shift ? 5 : 1;
    return index >= count ? 0 : index;
}

int hint_prev(int index, int count, bool shift) {
    index -= shift ? 5 : 1;
    return index < 0 ? count - 1 : index;
}

std::string keys_page(const Language& l) { return lang_text(l, "hints.keys"); }

// ---- the diplomacy window ----

std::vector<DiplomacyRow> diplomacy_rows(const sim::GameState& s, const std::vector<std::string>& names,
                                         const StringTable& strings) {
    static const char* const kNation[5] = {"MPS_RUSSIA_0", "MPS_ENGLAND_1", "MPS_FRANCE_2", "MPS_GERMANY_3",
                                           "MPS_AUSTRIA_4"};
    std::vector<DiplomacyRow> rows;
    const std::uint8_t me = s.my_player;
    if (me >= s.players.size()) return rows;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        const sim::Player& pl = s.players[p];
        if (p == me || pl.is_lost != 0) continue;
        DiplomacyRow r;
        r.player = static_cast<std::uint8_t>(p);
        std::string name = p < names.size() ? names[p] : std::string();
        if (name.empty()) name = text_of(strings, "IDS_COMP" + std::to_string(p + 1));
        r.text = name + " (" + (pl.nation < 5 ? text_of(strings, kNation[pl.nation]) : std::string()) + ")";
        const std::uint32_t c = pl.stat_colour;  // COLORREF 0x00BBGGRR
        r.rgb = (c & 0xff) << 16 | (c & 0xff00) | (c >> 16 & 0xff);
        r.our_war = s.players[me].is_ally[p] == 0;
        r.their_war = pl.is_ally[me] == 0;
        rows.push_back(std::move(r));
    }
    return rows;
}

sim::Command diplomacy_order(const sim::GameState& s, const std::vector<DiplomacyRow>& rows) {
    sim::Command c;
    c.kind = sim::Command::Kind::Act_b66a0;
    const std::uint8_t me = s.my_player;
    c.player = me;
    sim::OrderArg player;
    player.raw = me;
    sim::OrderArg table;
    table.tag = sim::OrderArg::Tag::Bytes;  // the recorder's b8: the 8 bytes of the table
    for (std::size_t p = 0; p < 8; ++p) {
        std::int32_t v = me < s.players.size() && p < s.players.size() ? s.players[me].is_ally[p] : 0;  // B33
        for (const DiplomacyRow& r : rows) {
            if (r.player == p) v = r.our_war ? 0 : 1;
        }
        if (p == me) v = 1;
        table.values.push_back(v);
    }
    c.args = {player, table};
    return c;
}

}  // namespace antanta::ui
