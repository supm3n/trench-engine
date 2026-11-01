#include "ui/mission_list.hpp"

#include "formats/campaign.hpp"
#include "formats/mis.hpp"

namespace antanta::ui {

namespace {

std::string_view stem_of(std::string_view file_name) {
    const std::size_t dot = file_name.rfind('.');
    return dot == std::string_view::npos ? file_name : file_name.substr(0, dot);
}

bool ends_with_ci(std::string_view s, std::string_view tail) {
    if (s.size() < tail.size()) return false;
    for (std::size_t i = 0; i < tail.size(); ++i) {
        char c = s[s.size() - tail.size() + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        if (c != tail[i]) return false;
    }
    return true;
}

// A row of a folder's .mis files: the path Load builds (`folder` as the original spells it, a backslash, the stem and
// ".mis"), the map from the header, title and text from the record of `text_folder` (the folder as the glob saw it).
std::optional<MissionEntry> folder_mission_entry(std::string_view folder, std::string_view text_folder,
                                                 std::string_view file_name, ByteSpan mis, const TextTable& table,
                                                 std::string* error) {
    if (mis.size() < 0x924) {
        *error = std::string(file_name) + ": shorter than a mission header";
        return std::nullopt;
    }
    const MisInfo info = parse_mis_info(mis);
    MissionEntry e;
    const std::string_view stem = stem_of(file_name);
    e.path = "Data\\Campaigns\\" + std::string(folder) + "\\" + std::string(stem) + ".mis";
    e.map = std::string(mis_name(info.map));
    const MissionTexts t =
        mission_texts("Data\\Campaigns\\" + std::string(text_folder) + "\\" + std::string(file_name), table);
    e.title = t.title, e.text = t.text;
    return e;
}

}  // namespace

std::optional<MissionEntry> standalone_mission_entry(std::string_view file_name, ByteSpan mis, const TextTable& table,
                                                     std::string* error) {

    return folder_mission_entry("missions", "Missions", file_name, mis, table, error);
}

std::optional<MissionEntry> multiplay_scenario_entry(std::string_view file_name, ByteSpan mis, const TextTable& table,
                                                     std::string* error) {

    return folder_mission_entry("multiplay", "multiplay", file_name, mis, table, error);
}

MissionTexts mission_texts(std::string_view mission_path, const TextTable& table) {
    const std::size_t last = mission_path.find_last_of("\\/");
    const std::string_view file = last == std::string_view::npos ? mission_path : mission_path.substr(last + 1);
    std::string_view folder;
    if (last != std::string_view::npos && last > 0) {
        const std::size_t before = mission_path.find_last_of("\\/", last - 1);
        folder = mission_path.substr(before == std::string_view::npos ? 0 : before + 1,
                                     before == std::string_view::npos ? last : last - before - 1);
    }
    const std::string_view stem = stem_of(file);
    MissionTexts t;
    if (const TextRecord* r = find_text(table, mission_text_key(folder, stem))) {
        t.title = r->title, t.text = r->text;
    }
    if (t.title.empty()) t.title = std::string(stem);
    return t;
}

// ---- the campaigns (slice M7) ----

std::optional<CampaignEntry> campaign_entry(std::string_view file_name, ByteSpan file, const TextTable& table,
                                            std::string* error) {
    CampaignEntry c;
    c.stem = std::string(stem_of(file_name));
    c.path = "Data/Campaigns/" + std::string(file_name);
    c.lua = ends_with_ci(file_name, ".cmt");
    if (c.lua) {
                  // its text), line 3 the picture
        auto cmt = parse_cmt(file, error);
        if (!cmt) {
            *error = std::string(file_name) + ": " + *error;
            return std::nullopt;
        }
        const TextRecord* title = find_text(table, cmt->title_key);
        const TextRecord* info = find_text(table, cmt->info_key);
        c.title = title && !title->title.empty() ? title->title : c.stem;
        c.text = info ? info->text : std::string();
        c.picture = cmt->picture;
        return c;
    }
    auto cmp = parse_cmp(file, error);
    if (!cmp) {
        *error = std::string(file_name) + ": " + *error;
        return std::nullopt;
    }
    const TextRecord* r = find_text(table, "IDS_CAMPAIGN_INFO_" + c.stem);
    c.title = r && !r->title.empty() ? r->title : c.stem;
    c.text = r ? r->text : std::string();
    c.picture = std::string(mis_name(cmp->header.picture));
    return c;
}

std::optional<MissionEntry> campaign_mission_entry(std::string_view campaign, std::string_view file_name, ByteSpan mis,
                                                   const TextTable& table, std::string* error) {
    if (mis.size() < kMisInfoSize) {
        *error = std::string(file_name) + ": shorter than a mission header";
        return std::nullopt;
    }
    MissionEntry e;
    e.path = "Data\\Campaigns\\" + std::string(campaign) + "\\" + std::string(file_name);
    e.map = std::string(mis_name(parse_mis_info(mis).map));
    const MissionTexts t = mission_texts(e.path, table);
    e.title = t.title, e.text = t.text;
    return e;
}

std::optional<MissionEntry> mit_mission_entry(std::string_view path, ByteSpan mit, const TextTable& table,
                                              std::string* error) {
    auto m = parse_mit(mit, error);
    if (!m) {
        *error = std::string(path) + ": " + *error;
        return std::nullopt;
    }
    MissionEntry e;
    e.path = std::string(path);
    e.map = m->map;
    const TextRecord* title = find_text(table, m->title_key);
    const TextRecord* info = find_text(table, m->info_key);
    e.title = title && !title->title.empty() ? title->title : m->title_key;
    e.text = info ? info->text : std::string();
    return e;
}

CampaignRows cmp_rows(const std::vector<MisInfo>& missions, const std::vector<bool>& done,
                      const std::vector<bool>& present) {
    const std::size_t n = missions.size();
    auto is_done = [&](std::size_t i) { return i < done.size() && done[i]; };



    std::vector<int> depth(n, 0xff);
    for (std::size_t i = 0; i < n; ++i) {
        const MisInfo& m = missions[i];
        if (m.prereq_count == 0) {
            depth[i] = 0;
            continue;
        }
        int d = 0;
        for (std::size_t j = 0; j < m.prereq_count && j < m.prereq.size(); ++j) {
            std::size_t steps = 0;
            for (std::uint8_t b = m.prereq[j]; b != 0 && b < n && steps <= n; b = missions[b].prereq[j], ++steps) ++d;
            ++d;
        }
        depth[i] = d & 0xff;
    }
    CampaignRows out;
    int last_open = 0;
    for (std::size_t level = 0; level < n; ++level) {
        for (std::size_t i = 0; i < n; ++i) {
            if (depth[i] != static_cast<int>(level)) continue;
            if (!is_done(i)) {
                bool open = true;
                for (std::size_t j = 0; j < missions[i].prereq_count && j < missions[i].prereq.size(); ++j) {
                    if (!is_done(missions[i].prereq[j])) open = false;
                }
                if (!open) continue;
            }
            if (i >= present.size() || !present[i]) continue;
            out.missions.push_back(i);
            if (!is_done(i)) last_open = static_cast<int>(level);
        }
    }
    // The original selects the depth of the last listed mission that is not done (0 when all are), taken as a row
    // index: with a gap in the list (a mission won while one before it is not) that is another row or past the end,
    // where nothing is selected. Ours selects the first listed mission at that depth that is not done, the same row
    // whenever the list has no gap (docs/bugs.md B221); row 0 when every row is won, as the original.
    out.selected = out.missions.empty() ? -1 : 0;
    for (std::size_t r = 0; r < out.missions.size(); ++r) {
        if (depth[out.missions[r]] == last_open && !is_done(out.missions[r])) {
            out.selected = static_cast<int>(r);
            break;
        }
    }
    return out;
}

CampaignRows cmt_rows(std::size_t count, const std::vector<bool>& done) {

    std::size_t k = 0;
    for (std::size_t i = 0; i < 20 && i < done.size(); ++i) {
        if (done[i]) k = i + 1;
    }
    CampaignRows out;
    for (std::size_t i = 0; i < k && i < count; ++i) out.missions.push_back(i);
    if (k < count) out.missions.push_back(k);
    // B220: the original selects k - 1; ours the row after the won ones, the mission to play next.
    const int rows = static_cast<int>(out.missions.size());
    out.selected = static_cast<int>(k) < rows ? static_cast<int>(k) : rows - 1;
    return out;
}

}  // namespace antanta::ui
