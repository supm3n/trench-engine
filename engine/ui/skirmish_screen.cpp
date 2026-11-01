#include "ui/skirmish_screen.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

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

void skirmish_set_players(SkirmishScreen& s, int players) {
    players = std::clamp(players, 2, 8);
    if (players == s.players) return;
    s.players = players;
    for (int k = players; k < 8; ++k) s.rows[k].level = 0;
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
