#include "ui/progress.hpp"

#include <algorithm>

namespace antanta::ui {

namespace {

bool same_text(std::string_view a, std::string_view b) {  // ASCII case ignored (B170)
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; };
        if (lower(a[i]) != lower(b[i])) return false;
    }
    return true;
}

// Tabs, line breaks and other control characters would break a line of our file; names never hold them.
bool clean(std::string_view s) {
    return !s.empty() && std::none_of(s.begin(), s.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

std::vector<std::string_view> split_tabs(std::string_view line) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t tab = line.find('\t', start);
        out.push_back(line.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos) return out;
        start = tab + 1;
    }
}

}  // namespace

std::vector<std::pair<std::string, std::string>> parse_progress_dat(ByteSpan file) {
    std::vector<std::string> lines;
    std::string line;
    for (const std::uint8_t b : file) {
        if (b == '\n') {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) lines.push_back(line);
            line.clear();
        } else {
            line += static_cast<char>(b);
        }
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) lines.push_back(line);
    std::vector<std::pair<std::string, std::string>> out;
    for (std::size_t i = 0; i + 1 < lines.size(); i += 2) out.emplace_back(lines[i], lines[i + 1]);
    return out;
}

std::string progress_print(const Progress& p) {
    std::string out =
        "# Entente Engine campaign progress: won<TAB>profile<TAB>campaign<TAB>mission per won mission;\n"
        "# imported<TAB>profile: that profile's Save/<profile>.dat of the game folder was read (once).\n";
    for (const std::string& name : p.imported) out += "imported\t" + name + "\n";
    for (const ProgressEntry& e : p.won) out += "won\t" + e.profile + "\t" + e.campaign + "\t" + e.mission + "\n";
    return out;
}

std::optional<Progress> progress_parse(std::string_view text, std::string* error) {
    Progress p;
    std::size_t start = 0;
    int line_no = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        ++line_no;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.empty() || line[0] == '#') continue;
        const std::vector<std::string_view> w = split_tabs(line);
        const bool ok = std::all_of(w.begin() + 1, w.end(), clean);
        if (w[0] == "won" && w.size() == 4 && ok) {
            progress_add(p, w[1], w[2], w[3]);
        } else if (w[0] == "imported" && w.size() == 2 && ok) {
            if (progress_needs_import(p, w[1])) p.imported.emplace_back(w[1]);
        } else {
            *error = "progress line " + std::to_string(line_no) + ": not a won or imported line";
            return std::nullopt;
        }
    }
    return p;
}

bool progress_add(Progress& p, std::string_view profile, std::string_view campaign, std::string_view mission) {
    if (!clean(profile) || !clean(campaign) || !clean(mission)) return false;
    if (progress_done(p, profile, campaign, mission)) return false;
    p.won.push_back(ProgressEntry{std::string(profile), std::string(campaign), std::string(mission)});
    return true;
}

bool progress_done(const Progress& p, std::string_view profile, std::string_view campaign, std::string_view mission) {
    return std::any_of(p.won.begin(), p.won.end(), [&](const ProgressEntry& e) {
        return e.profile == profile && same_text(e.campaign, campaign) && same_text(e.mission, mission);
    });
}

bool progress_needs_import(const Progress& p, std::string_view profile) {
    return std::find(p.imported.begin(), p.imported.end(), profile) == p.imported.end();
}

int progress_import(Progress& p, std::string_view profile,
                    const std::vector<std::pair<std::string, std::string>>& pairs) {
    if (!progress_needs_import(p, profile) || !clean(profile)) return 0;
    p.imported.emplace_back(profile);
    int added = 0;
    for (const auto& [campaign, mission] : pairs) added += progress_add(p, profile, campaign, mission) ? 1 : 0;
    return added;
}

std::optional<std::pair<std::string, std::string>> progress_key(std::string_view mission_path) {
    const std::size_t last = mission_path.find_last_of("\\/");
    if (last == std::string_view::npos || last == 0 || last + 1 >= mission_path.size()) return std::nullopt;
    const std::size_t before = mission_path.find_last_of("\\/", last - 1);
    const std::size_t from = before == std::string_view::npos ? 0 : before + 1;
    if (from >= last) return std::nullopt;
    return std::pair<std::string, std::string>{std::string(mission_path.substr(from, last - from)),
                                               std::string(mission_path.substr(last + 1))};
}

}  // namespace antanta::ui
