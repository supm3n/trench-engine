#include "platform/game_folder.hpp"

#include <SDL3/SDL.h>

#include "formats/bytes.hpp"
#include "platform/files.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace antanta {

namespace {

constexpr const char* kSteamGame = "steamapps/common/The Entente Gold";
constexpr const char* kRememberedFile = "game_folder.txt";

std::string join(const std::string& folder, const std::string& name) {
    if (folder.empty()) return name;
    const char last = folder.back();
    return last == '/' || last == '\\' ? folder + name : folder + "/" + name;
}

// For comparing folders: lower case, one kind of separator, no separator at the end.
std::string folder_key(const std::string& folder) {
    std::string out;
    for (char c : folder) {
        out += c == '\\' ? '/' : c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
    }
    while (out.size() > 1 && out.back() == '/') out.pop_back();
    return out;
}

void add_once(std::vector<std::string>& list, const std::string& folder) {
    if (folder.empty()) return;
    const std::string key = folder_key(folder);
    for (const std::string& f : list) {
        if (folder_key(f) == key) return;
    }
    list.push_back(folder);
}

bool is_number(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

bool iequals(const std::string& a, const char* b) {
    std::size_t i = 0;
    for (; i < a.size() && b[i]; ++i) {
        const char x = a[i] >= 'A' && a[i] <= 'Z' ? static_cast<char>(a[i] + 32) : a[i];
        if (x != b[i]) return false;
    }
    return i == a.size() && !b[i];
}

std::string lower(std::string s) {
    for (char& c : s) c = c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c;
    return s;
}

#ifdef _WIN32
std::string utf8(const wchar_t* text) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
    out.pop_back();  // the terminating zero
    return out;
}

std::string registry_text(HKEY root, const wchar_t* key, const wchar_t* value) {
    wchar_t buffer[1024];
    DWORD size = sizeof buffer;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) return {};
    return utf8(buffer);
}

// The entries of the Uninstall list under `root` in one registry view (KEY_WOW64_64KEY or KEY_WOW64_32KEY; 0: the
// default view, for HKCU, which has only one).
void add_installed(std::vector<InstalledProgram>& out, HKEY root, REGSAM view) {
    HKEY list = nullptr;
    if (RegOpenKeyExW(root, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view, &list) !=
        ERROR_SUCCESS) {
        return;
    }
    wchar_t name[256];
    for (DWORD i = 0;; ++i) {
        DWORD size = 256;
        const LONG r = RegEnumKeyExW(list, i, name, &size, nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;
        out.push_back(InstalledProgram{
            utf8(name), registry_text(list, name, L"DisplayName"), registry_text(list, name, L"InstallLocation"),
            registry_text(list, name, L"DisplayIcon"), registry_text(list, name, L"UninstallString")});
    }
    RegCloseKey(list);
}
#endif

std::string read_text_file(const std::string& path) {
    std::string err;
    const auto bytes = load_file(path, &err);
    return bytes ? std::string(bytes->begin(), bytes->end()) : std::string();
}

}  // namespace

std::string game_folder_missing(const std::string& folder, const std::function<bool(const std::string&)>& is_file) {
    for (const char* name : kGameKeyFiles) {
        if (!is_file(join(folder, name))) return name;
    }
    return {};
}

// The vdf is Valve's text format: quoted keys and values, braces for blocks, "\\" and "\"" escaped. Today's file
// names each library in a block with a "path" key; older ones (before 2021) gave the path as the value of a number
// key ("1" "D:\\SteamLibrary") right inside "libraryfolders".
std::vector<std::string> steam_library_folders(std::string_view vdf) {
    std::vector<std::string> out;
    std::vector<std::string> tokens;  // strings, "{" and "}" (a brace is never a quoted token: it keeps a \1 in front)
    for (std::size_t i = 0; i < vdf.size(); ++i) {
        const char c = vdf[i];
        if (c == '{' || c == '}') {
            tokens.push_back(std::string("\1") + c);
        } else if (c == '"') {
            std::string s;
            for (++i; i < vdf.size() && vdf[i] != '"'; ++i) {
                if (vdf[i] == '\\' && i + 1 < vdf.size()) ++i;
                s += vdf[i];
            }
            tokens.push_back(s);
        } else if (c == '/' && i + 1 < vdf.size() && vdf[i + 1] == '/') {  // a comment to the line's end
            while (i < vdf.size() && vdf[i] != '\n') ++i;
        }
    }
    auto brace = [](const std::string& t) { return t.size() == 2 && t[0] == '\1'; };
    int depth = 0;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if (t == "\1{") {
            ++depth;
        } else if (t == "\1}") {
            --depth;
        } else if (i + 1 < tokens.size() && !brace(tokens[i + 1])) {
            const std::string& value = tokens[i + 1];
            if (iequals(t, "path") || (depth == 1 && is_number(t))) add_once(out, value);
            ++i;  // the value is not a key
        }
    }
    return out;
}

std::vector<std::string> steam_game_folders(const std::vector<std::string>& steam_folders,
                                            const std::function<std::string(const std::string&)>& read_text) {
    std::vector<std::string> out;
    for (const std::string& steam : steam_folders) {
        if (steam.empty()) continue;
        add_once(out, join(steam, kSteamGame));
        for (const std::string& library :
             steam_library_folders(read_text(join(steam, "steamapps/libraryfolders.vdf")))) {
            add_once(out, join(library, kSteamGame));
        }
    }
    return out;
}

bool looks_like_the_game(const InstalledProgram& p) {
    const std::string name = lower(p.display_name);
    return name.find("entente") != std::string::npos || name.find("antanta") != std::string::npos ||
           lower(p.key) == std::string("steam app ") + kSteamAppId;
}

std::string folder_of_command(const std::string& command) {
    const std::size_t start = command.find_first_not_of(' ');
    std::string path = start == std::string::npos ? std::string() : command.substr(start);
    if (!path.empty() && path.front() == '"') {
        path = path.substr(1, path.find('"', 1) - 1);  // npos - 1 still takes the rest
    } else {
        const std::size_t exe = lower(path).find(".exe");
        if (exe != std::string::npos) path.resize(exe + 4);  // drops ",0" and the arguments
    }
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::vector<std::string> installed_game_folders(const std::vector<InstalledProgram>& programs) {
    std::vector<std::string> out;
    for (const InstalledProgram& p : programs) {
        if (!looks_like_the_game(p)) continue;
        std::string folder = p.install_location;
        if (folder.empty()) folder = folder_of_command(p.display_icon);
        if (folder.empty()) folder = folder_of_command(p.uninstall_string);
        add_once(out, folder);
    }
    return out;
}

std::vector<std::string> game_folder_candidates(const std::string& exe_folder, const std::vector<std::string>& steam,
                                                const std::string& remembered,
                                                const std::vector<std::string>& installed) {
    std::vector<std::string> out;
    add_once(out, exe_folder);
    for (const std::string& f : steam) add_once(out, f);
    add_once(out, remembered);
    for (const std::string& f : installed) add_once(out, f);
    return out;
}

std::optional<std::string> choose_game_folder(
    const std::function<std::optional<std::string>()>& pick,
    const std::function<bool(const std::string& folder, const std::string& missing)>& wrong,
    const std::function<bool(const std::string&)>& is_file) {
    for (;;) {
        const std::optional<std::string> folder = pick();
        if (!folder) return std::nullopt;
        const std::string missing = game_folder_missing(*folder, is_file);
        if (missing.empty()) return folder;
        if (!wrong(*folder, missing)) return std::nullopt;
    }
}

bool is_file_on_disk(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

std::vector<std::string> steam_folders_from_registry() {
    std::vector<std::string> out;
#ifdef _WIN32
    add_once(out, registry_text(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath"));
    add_once(out, registry_text(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath"));
    add_once(out, registry_text(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath"));
#endif
    return out;
}

std::vector<InstalledProgram> installed_programs_from_registry() {
    std::vector<InstalledProgram> out;
#ifdef _WIN32
    add_installed(out, HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
    add_installed(out, HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);
    add_installed(out, HKEY_CURRENT_USER, 0);
#endif
    return out;
}

std::string remembered_game_folder(const std::string& user_folder) {
    std::string text = read_text_file(user_folder + kRememberedFile);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return text;
}

bool remember_game_folder(const std::string& user_folder, const std::string& game_folder, std::string* error) {
    const std::string text = game_folder + "\n";
    return save_file_atomic(user_folder + kRememberedFile,
                            ByteSpan(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()), error);
}

std::string find_game_folder(const std::string& exe_folder, const std::string& user_folder) {
    const std::vector<std::string> steam = steam_game_folders(steam_folders_from_registry(), read_text_file);
    const std::string remembered = user_folder.empty() ? std::string() : remembered_game_folder(user_folder);
    const std::vector<std::string> installed = installed_game_folders(installed_programs_from_registry());
    for (const std::string& folder : game_folder_candidates(exe_folder, steam, remembered, installed)) {
        if (game_folder_missing(folder, is_file_on_disk).empty()) return folder;
    }
    return {};
}

}  // namespace antanta
