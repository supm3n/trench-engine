#include "ui/text_fit.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace antanta::ui {

namespace {

constexpr TextAlign L = TextAlign::Left, C = TextAlign::Centre;

// The places (docs/languages.md 2.5). Widths: from the place's x to what stands right of it (a check box, a field, a
// column line, the arrow of a combo box), measured on the original's pictures.
const std::vector<TextPlace> kPlaces = {
    // Game options (frontend.cpp open_game_options): the original's rows (label at x 60, the check box at 300), the
    // advisors (x 600, the check box at 925), the two headings centred in 420, the conduct template's label (x 600, its
    // box at 745) and the box's values (212 wide, text at 10, the arrow 32), our rows (label at x 60, the field at
    // 300), the Language row (label at x 600, its field at 765), and the values centred in our 180 wide field.
    {"options.label", 236, 15, 12, 2, L,
     "IDS_GOP_MOUSE_SCROLLING,IDS_GOP_KBD_SCROLLING,IDS_GOP_LANDSCAPE_FILTER,IDS_GOP_POOR_ANIMATION,"
     "IDS_GOP_REFLECTIONS"},
    {"options.advisor", 320, 15, 12, 2, L,
     "IDS_GOP_ECONOM_1,IDS_GOP_ECONOM_2,IDS_GOP_ECONOM_3,IDS_GOP_ECONOM_4,IDS_GOP_ECONOM_5,IDS_GOP_ECONOM_6,"
     "IDS_GOP_WAR_1,IDS_GOP_WAR_2,IDS_GOP_WAR_3,IDS_GOP_WAR_4"},
    {"options.heading", 420, 15, 12, 1, C, "IDS_GOP_ECONOM_ADV,IDS_GOP_WAR_ADV,options.network_heading"},
    {"options.conduct", 140, 15, 12, 2, L, "IDS_GOP_WAR_5"},
    {"options.conduct_value", 168, 13, 11, 1, L, "IDS_GOP_WAR_TPL_*"},
    {"options.row", 236, 15, 12, 2, L,
     "options.interface_scale,options.interface_pictures,options.world_zoom,options.autosave_every,options.seat_kept,"
     "options.computer_takes_over,options.check_updates,options.simulation_thread"},
    {"options.language", 160, 15, 12, 2, L, "options.language"},
    {"options.value", 170, 15, 12, 1, C,
     "options.auto,options.scale,options.pictures_*,options.whole_game,options.minutes.*,options.nobody,MP_LEVEL1,"
     "MP_LEVEL2,MP_LEVEL3,options.on,options.off"},
    // F1 (open_mission): the title at x 140, the close X at 422.
    {"mission.title", 270, 15, 12, 1, L, "IDS_RANDOM_MAP"},
    // The mission and campaign mission lists (213 and 212 wide, text at 8 in Times 16, the telephone's buttons 15
    // wide) and the campaign select (419 wide, text at 10, the buttons 21 wide). Their titles come from the text table.
    {"missions.row", 187, 13, 11, 1, L, ""},
    {"campaigns.row", 386, 15, 12, 1, L, ""},
    // The skirmish setup (open_skirmish): the labels centred in 145, the combo boxes' values (box width less the text's
    // offset and the 21 wide arrow).
    {"skirmish.label", 145, 15, 12, 1, C, "SP_LABEL_KOL_PL,SP_LABEL_RESOURCES,skirmish.speed"},
    {"skirmish.level", 102, 15, 12, 1, L, "MP_LEVEL0,MP_LEVEL1,MP_LEVEL2,MP_LEVEL3,skirmish.human"},
    {"skirmish.nation", 87, 15, 12, 1, L,
     "MPS_RUSSIA_0,MPS_ENGLAND_1,MPS_FRANCE_2,MPS_GERMANY_3,MPS_AUSTRIA_4,MPS_NULL"},
    {"skirmish.team", 27, 15, 12, 1, L, "MP_KOMMAND*"},
    {"skirmish.players", 57, 15, 12, 1, L, "SP_NPL_*"},
    {"skirmish.resources", 67, 15, 12, 1, L, "SP_RES_*"},
    {"skirmish.speed_value", 67, 15, 12, 1, L, "skirmish.speed_*"},
    // ours: the rule set's label centred in 90 (its box at 745), the box's values (215 wide, the text at 18)
    {"skirmish.rules_label", 90, 15, 12, 2, C, "skirmish.rules"},
    {"skirmish.rules_value", 176, 15, 12, 1, L, "skirmish.rules_classic,skirmish.rules_plus"},
    // The statistics (open_statistics): the row labels from x 55 to the first column at 255, the column heads
    // centred in 144.
    {"stats.row", 200, 15, 12, 1, L,
     "IDS_STAT_EXTRACTED_RESOURCES,IDS_STAT_SPENDED_RESOURCES,IDS_STAT_BUIED_RESOURCES,IDS_STAT_SOLD_RESOURCES,"
     "IDS_STAT_UNITS,IDS_STAT_HOUSES,IDS_STAT_NATION,IDS_STAT_DURATION,IDS_STAT_POINTS"},
    {"stats.column", 144, 15, 12, 1, C,
     "IDS_GOLD,IDS_FOOD,IDS_WOOD,IDS_IRON,IDS_OIL,IDS_STAT_TOTAL,IDS_STAT_LOSSES,IDS_STAT_KILLED,MPS_RUSSIA_0,"
     "MPS_ENGLAND_1,MPS_FRANCE_2,MPS_GERMANY_3,MPS_AUSTRIA_4"},
    // Diplomacy (open_diplomacy): the headings and the rows centred in their widths.
    {"diplomacy.player_head", 360, 15, 12, 1, C, "IDS_DIPLOMACY_PLAYERNAME"},
    {"diplomacy.relation_head", 256, 15, 12, 1, C, "IDS_DIPLOMACY_PLAERS_RELATION"},
    {"diplomacy.side_head", 128, 15, 12, 1, C, "IDS_DIPLOMACY_OUR,IDS_DIPLOMACY_TO_US"},
    {"diplomacy.row", 360, 15, 12, 1, C, ""},
    // The museum's three boxes (228 wide, text at 20, the arrow 45 wide); the entry names come from the text table.
    {"museum.box", 161, 15, 12, 1, L, "IDS_MUSEUM_NATION_*,IDS_MUSEUM_TYPE_*"},
    // The load screen's five lines under the picture, centred in 331.
    {"load.info", 331, 15, 12, 1, C, "load.original_info,load.unreadable,load.time_played"},
    // The network pages (ours): the help and details box of the games page (x 472 to its scroll column, four rows 22
    // apart), its Password label (the field at 590), page 1's connection (on the parchment, the field's width), and the
    // lobby: the player, nation and team columns, the map, the title centred in 390 and the box of lines (four rows 20
    // apart, as the games page's box).
    {"net.help", 470, 15, 12, 4, L,
     "net.none_found,net.saved_game,net.players,net.saved_rejoin,net.address,net.details_on_join,net.map,"
     "net.players_of,net.password_needed"},
    {"net.password", 114, 15, 12, 1, L, "net.password"},
    {"net.connection", 290, 15, 12, 1, L, "net.connection"},
    {"lobby.player", 156, 15, 12, 1, L, "net.slot_*,MP_LEVEL*,skirmish.human"},
    {"lobby.nation", 194, 15, 12, 1, L, "MPS_RUSSIA_0,MPS_ENGLAND_1,MPS_FRANCE_2,MPS_GERMANY_3,MPS_AUSTRIA_4,MPS_NULL"},
    {"lobby.team", 114, 15, 12, 1, L, "MP_KOMMAND*"},
    {"lobby.map", 380, 15, 12, 1, L, "net.map,net.rules"},
    // page 3, the scenarios (open_net_scenarios): the list's rows from x 12 to the scroll buttons at 350; their titles
    // come from the text table
    {"net.scenario_row", 338, 15, 12, 1, L, ""},
    {"lobby.title", 390, 15, 12, 1, C, "net.game_title"},
    {"lobby.lines", 470, 15, 12, 4, L,
     "net.waiting.*,net.all_in_host,net.all_in,net.seat_help,net.seat_move,net.join_at,net.has_password,"
     "net.joining,net.choose_game"},
    // The game's panels (house_panel.cpp, unit_panel.cpp): the info column from x 5 to the buttons at 195, in Times
    // 16 (the interface scale multiplies all of it).
    {"panel.info", 188, 13, 11, 1, L,
     "IDS_UNIT_DEFEND,IDS_HOUSE_IN,IDS_HOUSE_NOW_BUILD,IDS_GRENADE,IDS_UNIT_SELECTED,IDS_UNIT_EXPERIENCE,plus.upkeep,"
     "plus.reload_slow,plus.reload_slow_why"},
};

// The longest start of `s` (at a letter's start) whose width with `tail` after it is at most `width`.
std::size_t prefix_fitting(std::string_view s, std::string_view tail, int width,
                           const std::function<int(std::string_view)>& measure) {
    std::size_t best = 0;
    for (std::size_t n = 1; n <= s.size(); ++n) {
        if (n < s.size() && (static_cast<unsigned char>(s[n]) & 0xc0) == 0x80) continue;  // inside a letter
        if (measure(std::string(s.substr(0, n)) + std::string(tail)) > width) break;
        best = n;
    }
    return best;
}

}  // namespace

const std::vector<TextPlace>& text_places() { return kPlaces; }

const TextPlace& text_place(std::string_view name) {
    for (const TextPlace& p : kPlaces) {
        if (name == p.name) return p;
    }
    std::fprintf(stderr, "text_fit: no text place '%.*s'\n", static_cast<int>(name.size()), name.data());
    std::abort();
}

bool text_wrap(std::string_view text, int width, const std::function<int(std::string_view)>& measure,
               std::vector<std::string>* lines) {
    lines->clear();
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = text.find('\n', start);
        std::string_view rest = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        while (!rest.empty()) {
            if (measure(rest) <= width) {
                lines->emplace_back(rest);
                break;
            }
            std::size_t cut = std::string_view::npos;  // the last space before which the line fits
            for (std::size_t at = rest.find(' '); at != std::string_view::npos && at > 0; at = rest.find(' ', at + 1)) {
                if (measure(rest.substr(0, at)) > width) break;
                cut = at;
            }
            if (cut == std::string_view::npos) return false;  // a word wider than the line
            lines->emplace_back(rest.substr(0, cut));
            rest.remove_prefix(cut);
            while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        }
        if (nl == std::string_view::npos) break;
        if (text.substr(start, nl - start).empty()) lines->emplace_back();  // an empty paragraph
        start = nl + 1;
    }
    return true;
}

TextFit text_fit(const TextPlace& p, std::string_view text, std::string_view short_form, const TextMeasure& measure) {
    const std::size_t paragraphs = static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n')) + 1;
    TextFit out;
    auto at_em = [&](std::string_view t, int em, std::size_t max_lines) {
        std::vector<std::string> lines;
        const auto m = [&](std::string_view s) { return measure(em, s); };
        if (!text_wrap(t, p.width, m, &lines) || lines.empty() || lines.size() > max_lines) return false;
        out.em = em, out.lines = std::move(lines);
        return true;
    };
    for (const std::string_view t : {text, short_form}) {
        if (t.empty()) continue;
        out.shortened = t.data() == short_form.data() && !short_form.empty();
        // As written (a line per paragraph) at the place's em, then smaller; then more lines where the place allows.
        for (int em = p.em; em >= p.min_em; --em) {
            if (at_em(t, em, paragraphs)) return out;
        }
        if (static_cast<std::size_t>(p.lines) > paragraphs) {
            for (int em = p.em; em >= p.min_em; --em) {
                if (at_em(t, em, static_cast<std::size_t>(p.lines))) return out;
            }
        }
    }
    // The ellipsis at the smallest em: the lines the place has, the last one cut with "…" (U+2026, in every font).
    out = TextFit{};
    out.em = p.min_em, out.ellipsis = true;
    const auto m = [&](std::string_view s) { return measure(p.min_em, s); };
    std::string rest(text);
    for (char& c : rest) c = c == '\n' ? ' ' : c;
    for (int k = 1; k < p.lines && !rest.empty() && m(rest) > p.width; ++k) {
        std::size_t n = 0;  // the line: up to the last space before which it fits, else cut in its first word
        for (std::size_t at = rest.find(' '); at != std::string::npos && at > 0 && m(rest.substr(0, at)) <= p.width;
             at = rest.find(' ', at + 1)) {
            n = at;
        }
        if (n == 0) n = prefix_fitting(rest, "", p.width, m);
        if (n == 0) break;
        out.lines.push_back(rest.substr(0, n));
        rest.erase(0, n);
        while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
    }
    if (!rest.empty() && m(rest) <= p.width) {
        out.lines.push_back(rest);  // all of it, a long word broken in it: nothing to cut
        out.ellipsis = false;
        return out;
    }
    const std::string_view kEllipsis = "\xe2\x80\xa6";
    std::string last = rest.substr(0, prefix_fitting(rest, kEllipsis, p.width, m));
    while (!last.empty() && last.back() == ' ') last.pop_back();
    out.lines.push_back(last + std::string(kEllipsis));
    return out;
}

}  // namespace antanta::ui
