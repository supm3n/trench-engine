// The platform file interface: load a whole file by name into memory. Desktop: a path in the game folder;
// web: a file the user picked, written into the Emscripten file system by the page.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "formats/bytes.hpp"

namespace antanta {

std::optional<Bytes> load_file(const std::string& path, std::string* error);

// Writes a whole file (our own files only, never the game folder).
bool save_file(const std::string& path, ByteSpan data, std::string* error);

// Writes a whole file so that a crash or a full disk never leaves half of it at `path`: the bytes go to `path`.tmp,
// are flushed to the disk, and then the temporary file replaces `path` in one step (Windows: MoveFileExW with
// write-through). On an error `path` keeps what it had before. Our save files (sim/ownsave.hpp).
bool save_file_atomic(const std::string& path, ByteSpan data, std::string* error);

// The names (not paths) of the files in `folder` whose names end in `suffix` (case-insensitive), sorted as the
// original's lists (letters compared as capitals). A missing
// folder gives an empty list.
std::vector<std::string> list_files(const std::string& folder, const std::string& suffix);

// Creates `folder` (and its parents) if missing.
bool make_folder(const std::string& folder, std::string* error);

// Our folder for settings and profiles, ending in a separator: %APPDATA%\EntenteEngine\ on Windows (created if
// missing). Before the product was renamed (docs/naming.md) it was %APPDATA%\Antanta\: the first call moves such a
// folder to the new name (user_folder_plan), so saves, profiles, settings and progress carry over.
std::optional<std::string> user_folder(std::string* error);

// What to do with the user folder at the start (the pure decision of user_folder; `is_folder` answers for a path).
enum class UserFolderPlan {
    Fresh,       // neither exists: make the new one
    UseNew,      // only the new one exists
    MoveOld,     // only the old one exists: rename it to the new name
    BothUseNew,  // both exist: use the new one, leave the old one alone
};
UserFolderPlan user_folder_plan(const std::string& new_folder, const std::string& old_folder,
                                const std::function<bool(const std::string&)>& is_folder);

// The move itself, for the base folder `base` (ending in a separator; Windows: %APPDATA%): renames base/Antanta to
// base/EntenteEngine when user_folder_plan says MoveOld. `note` says what happened ("" when nothing). Returns the old
// folder's path (with the separator) when the rename failed, so that it is used for now; "" otherwise.
std::string migrate_old_user_folder(const std::string& base, std::string* note);

// What the first user_folder call did with the old folder, in words for the log ("" when there was nothing to say).
const std::string& user_folder_note();

}  // namespace antanta
