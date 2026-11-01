#include "platform/files.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "formats/bytes.hpp"

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#endif

namespace antanta {

std::optional<Bytes> load_file(const std::string& path, std::string* error) {
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
    // Windows takes both separators; on the web the page's loader writes the names in lower case with '/', and the
    // gate's node runs on Windows.
    const std::string& name = path;
#else
    // Linux and the like: the game's own names have Windows separators (a ship's DataPath "Russia\Ship_Rurik"), which
    // would be part of a file name here, so they become slashes (docs/bugs.md B296).
    std::string name = path;
    std::replace(name.begin(), name.end(), '\\', '/');
#endif
    std::size_t size = 0;
    void* data = SDL_LoadFile(name.c_str(), &size);
    if (!data) {
        *error = "cannot read " + path + ": " + SDL_GetError();
        return std::nullopt;
    }
    Bytes out(size);
    if (size > 0) std::memcpy(out.data(), data, size);
    SDL_free(data);
    return out;
}

bool save_file(const std::string& path, ByteSpan data, std::string* error) {
    if (!SDL_SaveFile(path.c_str(), data.data(), data.size())) {
        *error = "cannot write " + path + ": " + SDL_GetError();
        return false;
    }
    return true;
}

#ifdef _WIN32
namespace {
std::wstring wide(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(n > 0 ? static_cast<std::size_t>(n) : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    out.resize(out.size() - 1);
    return out;
}
std::string win_error(const char* what, const std::string& path) {
    return std::string(what) + " " + path + " (Windows error " + std::to_string(GetLastError()) + ")";
}
}  // namespace
#endif

bool save_file_atomic(const std::string& path, ByteSpan data, std::string* error) {
    const std::string tmp = path + ".tmp";
#ifdef _WIN32
    const std::wstring wtmp = wide(tmp), wpath = wide(path);
    HANDLE f = CreateFileW(wtmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        *error = win_error("cannot write", tmp);
        return false;
    }
    bool ok = true;
    for (std::size_t done = 0; ok && done < data.size();) {
        const DWORD part = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, 1u << 24));
        DWORD wrote = 0;
        ok = WriteFile(f, data.data() + done, part, &wrote, nullptr) != 0 && wrote == part;
        done += wrote;
    }
    ok = ok && FlushFileBuffers(f) != 0;
    if (!ok) *error = win_error("cannot write", tmp);
    CloseHandle(f);
    if (ok && MoveFileExW(wtmp.c_str(), wpath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        *error = win_error("cannot replace", path);
        ok = false;
    }
    if (!ok) DeleteFileW(wtmp.c_str());
    return ok;
#else
    if (!save_file(tmp, data, error)) return false;
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        *error = "cannot replace " + path;
        std::remove(tmp.c_str());
        return false;
    }
    return true;
#endif
}

namespace {
SDL_EnumerationResult SDLCALL collect(void* userdata, const char* /*dirname*/, const char* fname) {
    static_cast<std::vector<std::string>*>(userdata)->push_back(fname);
    return SDL_ENUM_CONTINUE;
}
}  // namespace

std::vector<std::string> list_files(const std::string& folder, const std::string& suffix) {
    std::vector<std::string> names, out;
    const bool sep = !folder.empty() && (folder.back() == '/' || folder.back() == '\\');
    const std::string base = sep ? folder : folder + "/";
    SDL_EnumerateDirectory(folder.c_str(), collect, &names);
    for (const std::string& n : names) {
        if (n.size() <= suffix.size() || !iequals_ascii(n.substr(n.size() - suffix.size()), suffix)) continue;
        SDL_PathInfo info;
        if (SDL_GetPathInfo((base + n).c_str(), &info) && info.type == SDL_PATHTYPE_FILE) out.push_back(n);
    }
    // The order of the original's lists (a Windows folder listing): letters compared as capitals, so No_name sits
    // between enemy_mid and peaceful_early.
    std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
            return std::toupper(static_cast<unsigned char>(x)) < std::toupper(static_cast<unsigned char>(y));
        });
    });
    return out;
}

bool make_folder(const std::string& folder, std::string* error) {
    if (!SDL_CreateDirectory(folder.c_str())) {
        *error = "cannot create " + folder + ": " + SDL_GetError();
        return false;
    }
    return true;
}

UserFolderPlan user_folder_plan(const std::string& new_folder, const std::string& old_folder,
                                const std::function<bool(const std::string&)>& is_folder) {
    const bool has_new = is_folder(new_folder);
    const bool has_old = is_folder(old_folder);
    if (has_new) return has_old ? UserFolderPlan::BothUseNew : UserFolderPlan::UseNew;
    return has_old ? UserFolderPlan::MoveOld : UserFolderPlan::Fresh;
}

namespace {

std::string g_user_folder_note;

bool is_folder_on_disk(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

}  // namespace

std::string migrate_old_user_folder(const std::string& base, std::string* note) {
    const std::string new_folder = base + "EntenteEngine", old_folder = base + "Antanta";
    switch (user_folder_plan(new_folder, old_folder, is_folder_on_disk)) {
        case UserFolderPlan::MoveOld:
            if (SDL_RenamePath(old_folder.c_str(), new_folder.c_str())) {
                *note = "moved the old user folder " + old_folder + " to " + new_folder;
            } else {
                *note = "could not move the old user folder " + old_folder + " to " + new_folder + " (" +
                        SDL_GetError() + "); using the old one for now";
                return old_folder + base.back();
            }
            break;
        case UserFolderPlan::BothUseNew:
            *note = "both " + new_folder + " and the old " + old_folder + " exist: using the new one";
            break;
        default: break;
    }
    return "";
}

const std::string& user_folder_note() { return g_user_folder_note; }

#ifdef _WIN32
namespace {

// The Roaming AppData folder with a trailing backslash ("" when Windows does not say), the place SDL_GetPrefPath uses.
std::string roaming_app_data() {
    wchar_t wide[MAX_PATH] = L"";
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, wide))) return "";
    char utf8[MAX_PATH * 4] = "";
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, sizeof utf8, nullptr, nullptr);
    return n > 0 ? std::string(utf8) + "\\" : "";
}

// Once per run, before SDL_GetPrefPath makes the new folder: the old one moves (migrate_old_user_folder). A failed move
// gives the old folder's path, which is then used for this run.
std::string old_folder_in_use() {
    const std::string base = roaming_app_data();
    if (base.empty()) return "";
    const std::string old_in_use = migrate_old_user_folder(base, &g_user_folder_note);
    if (!g_user_folder_note.empty()) SDL_Log("%s", g_user_folder_note.c_str());
    return old_in_use;
}

}  // namespace
#endif

std::optional<std::string> user_folder(std::string* error) {
#ifdef _WIN32
    static const std::string old_in_use = old_folder_in_use();
    if (!old_in_use.empty()) return old_in_use;
#endif
    // The web page keeps its name "Antanta" (the browser's own storage path, which players never see and which holds
    // their saved games).
#ifdef __EMSCRIPTEN__
    char* path = SDL_GetPrefPath("", "Antanta");
#else
    char* path = SDL_GetPrefPath("", "EntenteEngine");  // an empty organisation leaves its folder out
#endif
    if (!path) {
        *error = std::string("no settings folder: ") + SDL_GetError();
        return std::nullopt;
    }
    std::string out = path;
    SDL_free(path);
    return out;
}

}  // namespace antanta
