// The owner's fixes of the shipped mission scripts (docs/missions.md 5.7, "Owner's decisions" of 2026-10-18), applied
// to the script text as it is loaded, under our rules only (the original rules run the scripts as shipped). The game's
// files are never changed. Every fix is in the table of script/fixes.cpp: the script's file name, its B number, the
// original text and its replacement. A fix is applied only when its original text occurs exactly once in the file,
// byte for byte; otherwise the script stays as it is and the log gets a line saying so.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace antanta::sim {

struct ScriptFix {
    std::string_view file;  // the script's file name, compared without case ("champagne.lua")
    std::string_view bug;   // its entry in docs/bugs.md ("B235")
    std::string_view from;  // the shipped text, exact (the shipped scripts have CRLF line ends)
    std::string_view to;    // its replacement, with as many line ends, so error lines keep their numbers
};

// Every fix (the table, for the tests and the docs).
const std::vector<ScriptFix>& Script_Fixes();

// `text` with the fixes for the script `path` (any folder, either slash). A fix whose original text is not found
// exactly once adds a line to *notes and is left out.
std::string Script_ApplyFixes(std::string_view path, std::string text, std::vector<std::string>* notes);

}  // namespace antanta::sim
