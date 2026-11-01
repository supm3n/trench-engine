// Where the game's files are (docs/release.md "The game folder found by itself"). The app takes --game, then
// ANTANTA_GAME_DIR, then the first of these that holds the game: the exe's own folder, the Steam installs (Steam's
// folder from the registry and the library folders its steamapps/libraryfolders.vdf lists, each with
// steamapps/common/The Entente Gold), the folder remembered in our user folder, the folders Windows' list of installed
// programs gives for the game (any drive). A run with a window then asks with a folder picker until a folder with the
// game is chosen, and remembers it. Nothing is ever written into the game folder.
//
// The pure parts (the check, the vdf reading, the order, the picker's loop) take their file access as functions, so
// the tests run them without a disk, a registry or a dialog.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace antanta {

// The files the engine cannot start without, relative to the game folder.
inline constexpr const char* kGameKeyFiles[] = {"Data/GameInf.txt", "Data/Menu.txt", "Data/StringTable_eng.st",
                                                "Data/BasicParams0.dat", "Data/Objects/objects.les"};

// The first key file `folder` lacks, "" when it holds them all (so it is the game). `is_file` answers for a path.
std::string game_folder_missing(const std::string& folder, const std::function<bool(const std::string&)>& is_file);

// The library folders Steam's steamapps/libraryfolders.vdf lists: every "path" value, with the vdf's "\\" made "\".
std::vector<std::string> steam_library_folders(std::string_view vdf);

// The game's place in each Steam install: for each Steam folder, <it>/steamapps/common/The Entente Gold, then the
// same in every library folder its libraryfolders.vdf lists (`read_text` gives a file's text, empty when missing).
// No folder twice (compared without case and separator kind).
std::vector<std::string> steam_game_folders(const std::vector<std::string>& steam_folders,
                                            const std::function<std::string(const std::string&)>& read_text);

// The game's Steam app id (steamapps/appmanifest_285480.acf of the owner's Steam copy).
inline constexpr const char* kSteamAppId = "285480";

// An entry of Windows' list of installed programs (Software\Microsoft\Windows\CurrentVersion\Uninstall\<key>).
struct InstalledProgram {
    std::string key, display_name, install_location, display_icon, uninstall_string;
};

// True for an entry that may be the game: its DisplayName has "entente" or "antanta" in it (any case), or its key is
// Steam's "Steam App 285480". A wrong match costs nothing: every folder is still checked for the key files.
bool looks_like_the_game(const InstalledProgram& p);

// The folder of the program a command line names: `"C:\Games\Entente\Entente.exe",0` and
// `C:\Games\Entente\unins000.exe /S` give C:\Games\Entente. "" when it names no folder (MsiExec.exe /X{...}).
std::string folder_of_command(const std::string& command);

// The game folders the installed programs give: for each entry that looks like the game, its InstallLocation, else
// the folder of its DisplayIcon, else that of its UninstallString (no folder twice).
std::vector<std::string> installed_game_folders(const std::vector<InstalledProgram>& programs);

// The folders to try, in order: the exe's folder, the Steam ones, the remembered one, the installed programs' ones
// (empty entries left out, no folder twice).
std::vector<std::string> game_folder_candidates(const std::string& exe_folder, const std::vector<std::string>& steam,
                                                const std::string& remembered,
                                                const std::vector<std::string>& installed);

// The picker's loop: `pick` asks for a folder (nullopt: the player gave up); a folder without the game goes to
// `wrong` with the missing file, which answers whether to ask again. The chosen game folder, or nullopt.
std::optional<std::string> choose_game_folder(
    const std::function<std::optional<std::string>()>& pick,
    const std::function<bool(const std::string& folder, const std::string& missing)>& wrong,
    const std::function<bool(const std::string&)>& is_file);

// ---- the real disk, registry and user folder ----

// True when `path` is a file.
bool is_file_on_disk(const std::string& path);
// Steam's folders from the registry (Windows: HKCU Software\Valve\Steam SteamPath, then HKLM
// SOFTWARE\WOW6432Node\Valve\Steam InstallPath and SOFTWARE\Valve\Steam InstallPath). Elsewhere: none.
std::vector<std::string> steam_folders_from_registry();
// The installed programs from the registry (Windows: HKLM's 64-bit and 32-bit views and HKCU of
// Software\Microsoft\Windows\CurrentVersion\Uninstall). Elsewhere: none.
std::vector<InstalledProgram> installed_programs_from_registry();
// The game folder remembered in `user_folder` (game_folder.txt; ending in a separator), "" when there is none.
std::string remembered_game_folder(const std::string& user_folder);
bool remember_game_folder(const std::string& user_folder, const std::string& game_folder, std::string* error);
// The first candidate (game_folder_candidates with the exe's folder, the registry's Steam folders, the remembered
// folder and the installed programs' folders) that holds the game, "" when none does.
std::string find_game_folder(const std::string& exe_folder, const std::string& user_folder);

}  // namespace antanta
