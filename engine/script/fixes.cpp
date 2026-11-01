// The owner's fixes of the shipped mission scripts (script/fixes.hpp, docs/missions.md 5.7, docs/bugs.md).
#include "script/fixes.hpp"

#include <cctype>

namespace antanta::sim {

namespace {

using namespace std::string_view_literals;

// Each original text is taken from the script as it ships in Data\Campaigns (CRLF line ends, tabs), with enough
// around the changed line that it occurs only once.
const std::vector<ScriptFix> kFixes = {
    // B235 (fix 3): Champagne's "losses too big" gave the win to PlayerFr, the human (champagne.lua:185); the loss
    // message now ends in your loss, PlayerGerm the winner.
    {"champagne.lua", "B235",
     "\t\t\t\tMessageBox(\"IDS_CAMPAIGN_INFO_CHAMPAGNE_LOSTSTOBIG\")\r\n"
     "\t\t\t\tLuaManager_StopCurrentScript(PlayerFr)\r\n"sv,
     "\t\t\t\tMessageBox(\"IDS_CAMPAIGN_INFO_CHAMPAGNE_LOSTSTOBIG\")\r\n"
     "\t\t\t\tLuaManager_StopCurrentScript(PlayerGerm)\r\n"sv},
    // B237 (fix 5): Marne's 5-minute timer runs FirstTaskCompleteSuccessfuly() (marne.lua:103, 120), which set
    // FirstTaskComplete = 1 in any state; it now acts only while the first task runs (FirstTaskComplete == 0).
    {"marne.lua", "B237",
     "function FirstTaskCompleteSuccessfuly()\r\n"
     "\r\n"
     "\tMessageBox(\"IDS_CAMPAIGN_INFO_MARNE_FRANCE_NEED_SUPPORT\")\r\n"
     "\tFirstTaskComplete = 1\r\n"sv,
     "function FirstTaskCompleteSuccessfuly()\r\n"
     "\tif FirstTaskComplete ~= 0 then return end\r\n"
     "\tMessageBox(\"IDS_CAMPAIGN_INFO_MARNE_FRANCE_NEED_SUPPORT\")\r\n"
     "\tFirstTaskComplete = 1\r\n"sv},
    // B239 (fix 1 in part): Nivel's debug line "We have : <n> units." at every pass (nivel.lua:114) goes; the line
    // stays empty. Its loss rule (nivel.lua:142) is kept.
    {"nivel.lua", "B239", "\tAddMessage(\"We have :\", GetPlayerStatsUnits(PlayerFr), \" units.\")\r\n"sv, "\r\n"sv},
};

bool SameNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

}  // namespace

const std::vector<ScriptFix>& Script_Fixes() { return kFixes; }

std::string Script_ApplyFixes(std::string_view path, std::string text, std::vector<std::string>* notes) {
    const std::size_t slash = path.find_last_of("\\/");
    const std::string_view file = slash == std::string_view::npos ? path : path.substr(slash + 1);
    for (const ScriptFix& f : kFixes) {
        if (!SameNoCase(file, f.file)) continue;
        const std::size_t at = text.find(f.from);
        const bool once = at != std::string::npos && text.find(f.from, at + 1) == std::string::npos;
        if (!once) {
            notes->push_back("fix " + std::string(f.bug) + " not applied: " + std::string(f.file) +
                             (at == std::string::npos ? " does not hold the shipped text"
                                                      : " holds the shipped text more than once"));
            continue;
        }
        text.replace(at, f.from.size(), f.to);
    }
    return text;
}

}  // namespace antanta::sim
