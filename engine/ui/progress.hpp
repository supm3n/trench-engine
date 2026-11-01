// The campaign progress (docs/missions.md 4.3): which missions each profile has won. The original appends


// reads the game's Save\<profile>.dat once per profile, never writing it (as the profiles, ui/profiles.hpp). No SDL
// and no file access here; the app reads and writes the files.
//
// Changed on purpose (docs/bugs.md B170): the original appends a pair for every win, so the file grows without limit;
// its reader compares the campaign byte for byte (case-sensitive: Player.dat holds "antanta" for the folder
// "Antanta") and marks every mission whose file name starts with the stored line (strncmp over the line's length, so
// an empty line marks all). Ours keeps each won mission once and compares campaign and mission names whole, ignoring
// ASCII case.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formats/bytes.hpp"

namespace antanta::ui {

struct ProgressEntry {
    std::string profile, campaign, mission;  // "Player", "russian", "russia_1_1.mis"
};

struct Progress {
    std::vector<ProgressEntry> won;     // in the order they were won
    std::vector<std::string> imported;  // the profiles whose Save\<profile>.dat was read already
};

// Save\<profile>.dat: CRLF (or LF) lines in pairs, campaign stem then mission file; empty lines are skipped (the
// shipped Player.dat ends with one), a last line without its pair is dropped.
std::vector<std::pair<std::string, std::string>> parse_progress_dat(ByteSpan file);

// Our file: "#" comment lines, "won<TAB>profile<TAB>campaign<TAB>mission" per won mission, "imported<TAB>profile" per
// profile whose game file was read.
std::string progress_print(const Progress& p);
std::optional<Progress> progress_parse(std::string_view text, std::string* error);


bool progress_add(Progress& p, std::string_view profile, std::string_view campaign, std::string_view mission);

bool progress_done(const Progress& p, std::string_view profile, std::string_view campaign, std::string_view mission);
// Whether the profile's game file still has to be read (then progress_import, also with no file: an empty one).
bool progress_needs_import(const Progress& p, std::string_view profile);
// Takes the pairs of the profile's Save\<profile>.dat once; returns how many were new.
int progress_import(Progress& p, std::string_view profile,
                    const std::vector<std::pair<std::string, std::string>>& pairs);




std::optional<std::pair<std::string, std::string>> progress_key(std::string_view mission_path);

}  // namespace antanta::ui
