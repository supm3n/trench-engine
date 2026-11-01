


#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "formats/bytes.hpp"
#include "formats/mis.hpp"
#include "formats/museum.hpp"

namespace antanta::ui {

// One row: its text, what the screen shows while it is selected, and what Load starts.
struct MissionEntry {
    std::string title;
    std::string text;
                        // replaced with it)
    std::string map;
    std::string path;
    bool done = false;
};

// The row of the .mis `file_name` (as the folder listing gives it, "Karp.mis") of the stand-alone missions: the title
// and text from the text table (the glob's folder "Missions"), the map from the file's header. Fails when the header
// cannot be read.
std::optional<MissionEntry> standalone_mission_entry(std::string_view file_name, ByteSpan mis, const TextTable& table,
                                                     std::string* error);


std::optional<MissionEntry> multiplay_scenario_entry(std::string_view file_name, ByteSpan mis, const TextTable& table,
                                                     std::string* error);





struct MissionTexts {
    std::string title, text;
};
MissionTexts mission_texts(std::string_view mission_path, const TextTable& table);

// ---- the campaigns (slice M7, docs/missions.md 4.1 and 4.2) ----



struct CampaignEntry {
    std::string title;    // the row: the title of IDS_CAMPAIGN_INFO_<STEM> (.cmp) or of the .cmt's title key
    std::string text;     // the text box: that record's text, or the .cmt's info key's
    std::string picture;
    std::string stem;
    std::string path;     // the file, relative to the game folder: "Data/Campaigns/russian.cmp"
    bool lua = false;     // a .cmt: its missions are Lua (.mit) missions
};
// The row of `file_name` ("russian.cmp", "antanta.cmt", as the folder listing gives it). A missing record gives the
// stem as the title and no text. Fails when the file cannot be read as its kind.
std::optional<CampaignEntry> campaign_entry(std::string_view file_name, ByteSpan file, const TextTable& table,
                                            std::string* error);




std::optional<MissionEntry> campaign_mission_entry(std::string_view campaign, std::string_view file_name, ByteSpan mis,
                                                   const TextTable& table, std::string* error);



std::optional<MissionEntry> mit_mission_entry(std::string_view path, ByteSpan mit, const TextTable& table,
                                              std::string* error);



struct CampaignRows {
    std::vector<std::size_t> missions;
    int selected = -1;
};






CampaignRows cmp_rows(const std::vector<MisInfo>& missions, const std::vector<bool>& done,
                      const std::vector<bool>& present);
// A .cmt: k = the index after the last done mission; rows 0 to k - 1 (all shown as done) and row k when there is one.
// The original selects row k - 1 (the last won mission; none when nothing is won); ours selects row k, the next mission
// to play, when it exists (docs/bugs.md B220).
CampaignRows cmt_rows(std::size_t count, const std::vector<bool>& done);

}  // namespace antanta::ui
