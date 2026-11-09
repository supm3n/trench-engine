#include "ui/skirmish_screen.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <vector>

namespace antanta::ui {

namespace {

bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}


int first_free_colour(const SkirmishScreen& s, int row) {
    for (int c = 0; c < 8; ++c) {
        bool used = false;
        for (int k = 0; k < 8; ++k) used = used || (k != row && s.rows[k].level != 0 && s.rows[k].colour == c);
        if (!used) return c;
    }
    return -1;
}

}  // namespace

std::vector<MapEntry> parse_random_ind(std::string_view text) {
    std::vector<MapEntry> out;
    std::vector<std::string_view> words;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        const std::size_t start = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (i > start) words.push_back(text.substr(start, i - start));
    }
    for (std::size_t k = 0; k + 1 < words.size(); k += 2) {
        MapEntry e;
        e.players = std::atoi(std::string(words[k]).c_str());
        e.file = std::string(words[k + 1]);
        out.push_back(std::move(e));
    }
    return out;
}

void append_extra_maps(std::vector<MapEntry>& maps, const std::function<bool(const std::string& file)>& exists) {
    for (const char* file : {"Aust_3r_5.dat", "Russ_3r_2.dat", "Russ_3r_4.dat"}) {
        bool listed = false;
        for (const MapEntry& m : maps) listed = listed || same_name(m.file, file);
        if (listed || !exists(file) || !exists(preview_file(file))) continue;
        maps.push_back({8, file});
    }
}

std::string random_ind_with(std::string_view ind, const std::vector<std::string>& extra) {
    const std::vector<MapEntry> listed = parse_random_ind(ind);
    std::string out(ind);
    if (!out.empty() && out.back() != '\n') out += "\r\n";
    for (const std::string& file : extra) {
        bool known = false;
        for (const MapEntry& m : listed) known = known || same_name(m.file, file);
        if (!known) out += "8\r\n" + file + "\r\n";
    }
    return out;
}

void limit_to_map_positions(std::vector<MapEntry>& maps,
                            const std::function<std::optional<Bytes>(const std::string& file)>& read_map,
                            std::vector<std::string>* left_out) {
    std::vector<MapEntry> kept;
    for (MapEntry& m : maps) {
        if (const std::optional<Bytes> file = read_map(m.file)) {
            std::string err;
            const std::optional<MapData> map = parse_map(*file, &err);
            if (!map) {
                if (left_out) left_out->push_back(m.file);
                continue;
            }
            if (!map->player_nations.empty())
                m.players = std::min(m.players, static_cast<int>(map->player_nations.size()));
        }
        kept.push_back(std::move(m));
    }
    maps = std::move(kept);
}

const MapEntry* map_at(const std::vector<MapEntry>& maps, int players, int index) {
    if (index < 0) return nullptr;
    int n = 0;
    for (const MapEntry& m : maps) {
        if (m.players < players) continue;
        if (n++ == index) return &m;
    }
    return nullptr;
}

int map_index(const std::vector<MapEntry>& maps, int players, std::string_view file) {
    int n = 0;
    for (const MapEntry& m : maps) {
        if (m.players < players) continue;
        if (same_name(m.file, file)) return n;
        ++n;
    }
    return -1;
}

std::string preview_file(const std::string& map_file) {
    const std::size_t dot = map_file.size() > 4 ? map_file.size() - 4 : 0;
    return map_file.substr(0, dot) + "_m.min";
}

SkirmishScreen skirmish_defaults(const std::string& name) {
    SkirmishScreen s;
    s.name = name;
    s.rows[0] = sim::SkirmishSlot{4, 0, 0, 0};
    s.rows[1] = sim::SkirmishSlot{1, 1, 1, 0};
    for (int k = 2; k < 8; ++k) s.rows[k] = sim::SkirmishSlot{0, 0, 0, 0};
    return s;
}

void skirmish_gen_teams(SkirmishScreen& s) {
    s.gen.island_plan = 0;
    s.gen_teams_dropped = false;
    if (!s.gen.islands) return;
    std::array<int, 9> members{};  // per team 1..8; index 0: the players without a team (and the closed rows)
    for (int k = 0; k < s.players; ++k) {
        const sim::SkirmishSlot& r = s.rows[static_cast<std::size_t>(k)];
        ++members[r.level != 0 && r.team >= 1 && r.team <= 8 ? r.team : 0];
    }
    std::vector<int> sizes(static_cast<std::size_t>(members[0]), 1);
    bool teams = false;
    for (int t = 1; t <= 8; ++t) {
        if (members[static_cast<std::size_t>(t)] > 0) sizes.push_back(members[static_cast<std::size_t>(t)]);
        teams = teams || members[static_cast<std::size_t>(t)] > 0;
    }
    if (!teams || sizes.size() < 2 || sizes.size() > 4) return;  // the generator draws the islands then
    std::sort(sizes.begin(), sizes.end(), std::greater<int>());
    std::uint32_t plan = 0;
    for (std::size_t i = 0; i < sizes.size(); ++i) plan |= static_cast<std::uint32_t>(sizes[i]) << (4 * i);
    // Teams too uneven for the islands' room (sim::MapGen_IslandPlanFits): the islands of no teams, and the screen
    // says so.
    if (!sim::MapGen_IslandPlanFits(s.gen.size, s.players, plan)) {
        s.gen_teams_dropped = true;
        return;
    }
    s.gen.island_plan = plan;
}

void skirmish_set_players(SkirmishScreen& s, int players) {
    players = std::clamp(players, 2, 8);
    s.gen.players = static_cast<std::uint8_t>(players);
    if (players == s.players) return;
    s.players = players;
    for (int k = players; k < 8; ++k) s.rows[k].level = 0;
    skirmish_gen_teams(s);
}

void skirmish_set_resources(SkirmishScreen& s, int resources) { s.resources = std::clamp(resources, 0, 2); }

void skirmish_set_level(SkirmishScreen& s, int row, int level) {
    if (row < 1 || row > 7) return;                    // row 0 is you: its level stays 4
    level = level == 5 ? 5 : std::clamp(level, 0, 3);  // 5: a human who joins the host's network game (slice MP2)
    sim::SkirmishSlot& r = s.rows[row];
    if (level != 0 && r.level == 0) {
        r.nation = 5, r.team = 0;
        const int c = first_free_colour(s, row);
        if (c >= 0) r.colour = static_cast<std::uint8_t>(c);
    }
    r.level = static_cast<std::uint8_t>(level);
    skirmish_gen_teams(s);
}

void skirmish_set_nation(SkirmishScreen& s, int row, int nation) {
    if (row < 0 || row > 7) return;
    s.rows[row].nation = static_cast<std::uint8_t>(std::clamp(nation, 0, 5));
}

bool skirmish_set_colour(SkirmishScreen& s, int row, int colour) {
    if (row < 0 || row > 7 || colour < 0 || colour > 7) return false;
    for (int k = 0; k < 8; ++k) {
        if (k != row && s.rows[k].level != 0 && s.rows[k].colour == colour) return false;
    }
    s.rows[row].colour = static_cast<std::uint8_t>(colour);
    return true;
}

void skirmish_set_team(SkirmishScreen& s, int row, int team) {
    if (row < 0 || row > 7) return;
    s.rows[row].team = static_cast<std::uint8_t>(std::clamp(team, 0, 8));
    skirmish_gen_teams(s);
}

bool skirmish_generated(const SkirmishScreen& s) { return s.gen_offered && s.map == 0; }

const MapEntry* skirmish_map_file(const std::vector<MapEntry>& maps, const SkirmishScreen& s, int index) {
    return map_at(maps, s.players, s.gen_offered ? index - 1 : index);
}

bool skirmish_has_entry(const std::vector<MapEntry>& maps, const SkirmishScreen& s, int index) {
    return (s.gen_offered && index == 0) || skirmish_map_file(maps, s, index) != nullptr;
}

std::string skirmish_map_name(const std::vector<MapEntry>& maps, const SkirmishScreen& s) {
    if (skirmish_generated(s)) {
        sim::MapGenSettings g = s.gen;
        g.players = static_cast<std::uint8_t>(s.players);
        return sim::MapGen_Code(g);
    }
    const MapEntry* m = skirmish_map_file(maps, s, s.map);
    return m ? m->file : std::string();
}

bool gen_switch(const sim::MapGenSettings& g, GenSwitch w) {
    switch (w) {
        case GenSwitch::Winter: return g.winter;
        case GenSwitch::Sea: return g.sea;
        case GenSwitch::Islands: return g.islands;
        case GenSwitch::Mountains: return g.mountains;
        case GenSwitch::DenseForest: return g.dense_forest;
    }
    return false;
}

void gen_set_switch(sim::MapGenSettings& g, GenSwitch w, bool on) {
    switch (w) {
        case GenSwitch::Winter: g.winter = on; break;
        case GenSwitch::Sea: g.sea = on, g.islands = g.islands && on; break;  // no islands without sea
        case GenSwitch::Islands: g.islands = on, g.sea = g.sea || on; break;  // islands are a Sea map
        case GenSwitch::Mountains: g.mountains = on; break;
        case GenSwitch::DenseForest: g.dense_forest = on; break;
    }
}

void skirmish_gen_size(SkirmishScreen& s, int size) { s.gen.size = static_cast<std::uint8_t>(std::clamp(size, 0, 1)); }

bool skirmish_gen_code(SkirmishScreen& s, const std::string& code, std::string* error) {
    const auto g = sim::MapGen_ParseCode(code, error);
    if (!g) return false;
    skirmish_set_players(s, g->players);
    s.gen = *g;
    // An island plan in the code sets the teams: the players in row order, one team per island.
    if (g->islands && g->island_plan != 0) {
        int row = 0;
        const std::vector<int> sizes = sim::MapGen_IslandSizes(g->island_plan, g->players);
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            for (int q = 0; q < sizes[i] && row < 8; ++q)
                s.rows[static_cast<std::size_t>(row++)].team = static_cast<std::uint8_t>(i + 1);
        }
    }
    return true;
}

std::uint32_t gen_next_seed(std::uint32_t seed) {
    std::uint32_t x = seed + 0x9e3779b9u;  // a 32-bit mix (lowbias32), so the next codes do not look alike
    x ^= x >> 16, x *= 0x7feb352du, x ^= x >> 15, x *= 0x846ca68bu, x ^= x >> 16;
    return x != 0 ? x : 1;
}

std::string map_display_name(const std::string& map) {
    if (map.size() > 4 && same_name(std::string_view(map).substr(map.size() - 4), ".dat"))
        return map.substr(0, map.size() - 4);
    return map;
}

sim::SkirmishSetup skirmish_setup(const SkirmishScreen& s, const std::string& map, std::uint32_t seed,
                                  const Settings& settings) {
    sim::SkirmishSetup out;
    out.map = map;
    out.players = static_cast<std::uint8_t>(s.players);
    out.slots = s.rows;
    out.resources = static_cast<std::uint8_t>(s.resources);
    out.seed = seed;
    out.economic = settings.economic;
    out.military = settings.military;
    out.plus = s.plus;
    return out;
}

}  // namespace antanta::ui
