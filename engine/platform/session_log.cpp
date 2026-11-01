#include "platform/session_log.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <functional>

#include "platform/files.hpp"

#ifdef _WIN32
#include <windows.h>

#include <csignal>
#include <cstring>
#include <cwchar>
#endif

namespace antanta {

struct SessionLog {
    SDL_IOStream* io = nullptr;
    SDL_Mutex* mutex = nullptr;
    std::string name;
    bool sdl_hooked = false;
    SDL_LogOutputFunction prev_out = nullptr;
    void* prev_out_data = nullptr;
#ifdef _WIN32
    bool crash_hooked = false;
    LPTOP_LEVEL_EXCEPTION_FILTER prev_filter = nullptr;
    std::wstring crash_folder;   // the folder above the logs: a crash also writes crash_<time>.txt there
    std::string crash_log_name;  // "logs\session_....txt", named in that file
    void (*prev_abort)(int) = nullptr;
#endif
};

namespace {

[[maybe_unused]] constexpr std::size_t kKeepSessionLogs = 10;
constexpr std::size_t kNameLength = 8 + 19 + 4;  // "session_" + "YYYY-MM-DD_HH-MM-SS" + ".txt"

bool digits(const std::string& s, std::size_t from, std::size_t count) {
    if (from + count > s.size()) return false;
    for (std::size_t i = from; i < from + count; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
    }
    return true;
}

#ifdef _WIN32
// UTF-8 to the wide text of Windows' file functions (the user folder may have letters beyond ASCII).
std::wstring wide(const std::string& text) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), n);
    out.pop_back();  // the terminating zero
    return out;
}
#endif

// Text into the file; the lock is skipped by the crash handler, which must never wait.
void put(SessionLog& log, const std::string& line, bool lock) {
    if (lock) SDL_LockMutex(log.mutex);
    if (log.io) {
        SDL_WriteIO(log.io, line.data(), line.size());
#ifndef _WIN32
        SDL_FlushIO(log.io);  // Windows writes straight to the file; the other systems' streams are buffered
#endif
    }
    if (lock) SDL_UnlockMutex(log.mutex);
}

void write_line(SessionLog& log, std::string_view tag, std::string_view text) {
    put(log, session_log_line(session_log_now(), tag, text), true);
}

const char* category_name(int category) {
    static const char* const kNames[] = {"",      "error",  "assert", "system", "audio",
                                         "video", "render", "input",  "test",   "gpu"};
    return category > 0 && category < static_cast<int>(sizeof kNames / sizeof kNames[0]) ? kNames[category] : "";
}

void SDLCALL tee_output(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    SessionLog& log = *static_cast<SessionLog*>(userdata);
    std::string tag;
    if (category != SDL_LOG_CATEGORY_APPLICATION) {
        const std::string name = category_name(category);
        tag = "[" + (name.empty() ? "category " + std::to_string(category) : name) + "] ";
    }
    if (priority == SDL_LOG_PRIORITY_WARN) {
        tag += "warning: ";
    } else if (priority == SDL_LOG_PRIORITY_ERROR) {
        tag += "error: ";
    } else if (priority == SDL_LOG_PRIORITY_CRITICAL) {
        tag += "critical: ";
    }
    write_line(log, tag, message ? message : "");
    if (log.prev_out) log.prev_out(log.prev_out_data, category, priority, message);
}

SDL_EnumerationResult SDLCALL collect_name(void* userdata, const char*, const char* name) {
    static_cast<std::vector<std::string>*>(userdata)->push_back(name);
    return SDL_ENUM_CONTINUE;
}

// Deletes the session logs beyond the newest `keep`: files of our own name pattern only.
void prune(const std::string& folder, std::size_t keep) {
    std::vector<std::string> names;
    SDL_EnumerateDirectory(folder.c_str(), collect_name, &names);
    for (const std::string& name : session_logs_to_delete(names, keep)) {
        const std::string path = folder + name;
        SDL_PathInfo info{};
        if (SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE) SDL_RemovePath(path.c_str());
    }
}

}  // namespace

// ---- names and lines (pure) ----

LogTime session_log_now() {
    LogTime out;
    SDL_Time t = 0;
    SDL_DateTime dt{};
    if (SDL_GetCurrentTime(&t) && SDL_TimeToDateTime(t, &dt, true)) {
        out = LogTime{dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second, dt.nanosecond / 1000000};
    }
    return out;
}

std::string session_log_name(const LogTime& t, int number) {
    char b[64];
    std::snprintf(b, sizeof b, "session_%04d-%02d-%02d_%02d-%02d-%02d", t.year, t.month, t.day, t.hour, t.minute,
                  t.second);
    std::string out = b;
    if (number > 1) {
        std::snprintf(b, sizeof b, "_%02d", number > 99 ? 99 : number);  // (windows.h defines min and max)
        out += b;
    }
    return out + ".txt";
}

bool is_session_log_name(const std::string& name) {
    const bool numbered = name.size() == kNameLength + 3;
    if (!numbered && name.size() != kNameLength) return false;
    if (name.compare(0, 8, "session_") != 0 || !name.ends_with(".txt")) return false;
    // YYYY-MM-DD_HH-MM-SS
    if (!digits(name, 8, 4) || name[12] != '-' || !digits(name, 13, 2) || name[15] != '-' || !digits(name, 16, 2) ||
        name[18] != '_' || !digits(name, 19, 2) || name[21] != '-' || !digits(name, 22, 2) || name[24] != '-' ||
        !digits(name, 25, 2)) {
        return false;
    }
    return !numbered || (name[27] == '_' && digits(name, 28, 2));
}

std::vector<std::string> session_logs_to_delete(const std::vector<std::string>& names, std::size_t keep) {
    std::vector<std::string> ours;
    for (const std::string& n : names) {
        if (is_session_log_name(n)) ours.push_back(n);
    }
    std::sort(ours.begin(), ours.end(), std::greater<std::string>());  // newest first: the names sort by time
    if (ours.size() <= keep) return {};
    return std::vector<std::string>(ours.begin() + static_cast<std::ptrdiff_t>(keep), ours.end());
}

std::string session_log_line(const LogTime& t, std::string_view tag, std::string_view text) {
    char b[32];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d.%03d  ", t.hour, t.minute, t.second, t.millisecond);
    std::string out = b;
    out += tag;
    out += text;
    if (out.empty() || out.back() != '\n') out += '\n';
    return out;
}

// ---- the file ----

SessionLogPtr session_log_open_in(const std::string& folder, const LogTime& now, std::size_t keep, std::string* error) {
    if (!SDL_CreateDirectory(folder.c_str())) {
        *error = "cannot make " + folder + ": " + SDL_GetError();
        return nullptr;
    }
    std::string name;
    for (int number = 1; number <= 99; ++number) {  // two sessions in the same second get a counter
        name = session_log_name(now, number);
        if (!SDL_GetPathInfo((folder + name).c_str(), nullptr)) break;
    }
    SessionLogPtr log(new SessionLog);  // NOLINT: owned by the unique_ptr at once
    log->name = name;
    log->io = SDL_IOFromFile((folder + name).c_str(), "wb");
    if (!log->io) {
        *error = "cannot write " + folder + name + ": " + SDL_GetError();
        return nullptr;
    }
    log->mutex = SDL_CreateMutex();
    prune(folder, keep);
#ifdef _WIN32
    // A crash's short report goes into the folder above the logs (our user folder, docs/release.md).
    std::string logs = folder;
    while (!logs.empty() && (logs.back() == '/' || logs.back() == '\\')) logs.pop_back();
    const std::size_t cut = logs.find_last_of("/\\");
    if (cut != std::string::npos) {
        log->crash_folder = wide(logs.substr(0, cut + 1));
        log->crash_log_name = logs.substr(cut + 1) + "\\" + name;
    }
#endif
    return log;
}

const std::string& session_log_file_name(const SessionLog& log) { return log.name; }

void session_log_write(SessionLog& log, std::string_view text) { write_line(log, "", text); }

void session_log_hook_sdl(SessionLog& log) {
    if (log.sdl_hooked) return;
    SDL_GetLogOutputFunction(&log.prev_out, &log.prev_out_data);
    SDL_SetLogOutputFunction(tee_output, &log);
    log.sdl_hooked = true;
}

// ---- the crash handler (Windows) ----

#ifdef _WIN32
namespace {

// The operating system calls the filter and the abort handler without user data, so the log they write to is found
// through this one pointer (set by session_log_hook_crash, cleared when the log closes).
SessionLog* g_crash_log = nullptr;

struct ExceptionName {
    DWORD code;
    const char* name;
};
constexpr ExceptionName kExceptionNames[] = {
    {0xC0000005, "access violation"},
    {0xC00000FD, "stack overflow"},
    {0xC0000094, "integer divide by zero"},
    {0xC000001D, "illegal instruction"},
    {0xC0000096, "privileged instruction"},
    {0xC0000409, "stack buffer overrun or fast fail"},
    {0xC0000374, "heap corruption"},
    {0xC000008C, "array bounds exceeded"},
    {0xC0000025, "non-continuable exception"},
    {0x80000003, "breakpoint"},
    {0xE06D7363, "C++ exception"},
};


void describe(char* out, std::size_t size, DWORD64 address) {
    HMODULE module = nullptr;
    char path[MAX_PATH] = "";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) &&
        GetModuleFileNameA(module, path, MAX_PATH) > 0) {
        const char* base = path;
        for (const char* p = path; *p; ++p) {
            if (*p == '\\' || *p == '/') base = p + 1;
        }
        std::snprintf(out, size, "%s+0x%llx", base,
                      static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
    } else {
        std::snprintf(out, size, "unknown+0x%llx", static_cast<unsigned long long>(address));
    }
}

// The short crash report a player can send (docs/release.md): %APPDATA%\EntenteEngine\crash_<time>.txt, open while a
// crash is written (crash_open to crash_close), with the same CRASH lines as the session log.
HANDLE g_crash_file = INVALID_HANDLE_VALUE;
wchar_t g_crash_path[MAX_PATH + 64] = L"";
// The crash box's text around the path (session_log_set_crash_box); English until the app sets the display language's.
wchar_t g_crash_before[512] = L"Entente Engine crashed. Sorry!\n\nA short report was saved as\n";
wchar_t g_crash_after[512] = L"\n\nPlease send that file to whoever gave you the game.";

void crash_file_put(const char* text) {
    DWORD written = 0;
    if (g_crash_file != INVALID_HANDLE_VALUE) {
        WriteFile(g_crash_file, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
    }
}

void crash_write(SessionLog& log, const char* text) {
    put(log, session_log_line(session_log_now(), "", text), false);
    crash_file_put(text);
    crash_file_put("\r\n");
}

void crash_open(SessionLog& log) {
    if (log.crash_folder.empty()) return;
    const LogTime t = session_log_now();
    std::swprintf(g_crash_path, sizeof g_crash_path / sizeof g_crash_path[0],
                  L"%lscrash_%04d-%02d-%02d_%02d-%02d-%02d.txt", log.crash_folder.c_str(), t.year, t.month, t.day,
                  t.hour, t.minute, t.second);
    g_crash_file = CreateFileW(g_crash_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    char line[MAX_PATH + 200];
    std::snprintf(line, sizeof line,
                  "Entente Engine %s crashed on %04d-%02d-%02d at %02d:%02d:%02d (local time). Sorry!\r\n"
                  "Please send this file to whoever gave you the game. The whole log of this session is %s in the "
                  "same folder.\r\n\r\n",
                  ANTANTA_VERSION, t.year, t.month, t.day, t.hour, t.minute, t.second, log.crash_log_name.c_str());
    crash_file_put(line);
}

void crash_close() {
    if (g_crash_file == INVALID_HANDLE_VALUE) return;
    FlushFileBuffers(g_crash_file);
    CloseHandle(g_crash_file);
    g_crash_file = INVALID_HANDLE_VALUE;
}

// The release exe has no console (docs/release.md): the player is told where the report is, unless the process asked
// Windows for no fault message boxes (SetErrorMode SEM_NOGPFAULTERRORBOX, as the tests do). True when it was shown.
bool crash_tell_player() {
#ifdef ANTANTA_RELEASE
    if ((GetErrorMode() & SEM_NOGPFAULTERRORBOX) != 0 || g_crash_path[0] == 0) return false;
    wchar_t text[MAX_PATH + 64 + 2 * 512];
    std::swprintf(text, sizeof text / sizeof text[0], L"%ls%ls%ls", g_crash_before, g_crash_path, g_crash_after);
    MessageBoxW(nullptr, text, L"Entente Engine", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    return true;
#else
    return false;
#endif
}

LONG WINAPI crash_filter(EXCEPTION_POINTERS* info) {
    SessionLog* log = g_crash_log;
    bool told = false;
    if (log) {
        crash_open(*log);
        const EXCEPTION_RECORD& rec = *info->ExceptionRecord;
        const char* name = "unknown exception";
        for (const ExceptionName& e : kExceptionNames) {
            if (e.code == rec.ExceptionCode) name = e.name;
        }
        char where[MAX_PATH + 40];
        char line[MAX_PATH + 200];
        describe(where, sizeof where, reinterpret_cast<DWORD64>(rec.ExceptionAddress));
        std::snprintf(line, sizeof line, "CRASH: exception 0x%08lx (%s) at %s",
                      static_cast<unsigned long>(rec.ExceptionCode), name, where);
        crash_write(*log, line);
        if (rec.ExceptionCode == 0xC0000005 && rec.NumberParameters >= 2) {
            const ULONG_PTR kind = rec.ExceptionInformation[0];
            std::snprintf(line, sizeof line, "CRASH: the program %s address 0x%llx",
                          kind == 0   ? "read"
                          : kind == 1 ? "wrote"
                                      : "ran",
                          static_cast<unsigned long long>(rec.ExceptionInformation[1]));
            crash_write(*log, line);
        }
#if defined(_M_X64) || defined(__x86_64__)
        CONTEXT ctx = *info->ContextRecord;
        crash_write(*log, "CRASH: return addresses (module+offset), newest first:");
        for (int i = 0; i < 40 && ctx.Rip != 0; ++i) {
            describe(where, sizeof where, ctx.Rip);
            std::snprintf(line, sizeof line, "  #%d %s", i, where);
            crash_write(*log, line);
            DWORD64 image_base = 0;
            PRUNTIME_FUNCTION fn = RtlLookupFunctionEntry(ctx.Rip, &image_base, nullptr);
            if (!fn) break;
            PVOID handler_data = nullptr;
            DWORD64 frame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, image_base, ctx.Rip, fn, &ctx, &handler_data, &frame, nullptr);
        }
#else
        crash_write(*log, "CRASH: no stack trace on this kind of processor");
#endif
        crash_write(*log, "CRASH: end of the report");
        if (log->io) SDL_FlushIO(log->io);
        crash_close();
        told = crash_tell_player();
    }
    if (told) return EXCEPTION_EXECUTE_HANDLER;  // the process ends without Windows' own "stopped working" box
    return log && log->prev_filter ? log->prev_filter(info) : EXCEPTION_CONTINUE_SEARCH;
}

void abort_handler(int) {
    SessionLog* log = g_crash_log;
    if (!log) return;
    crash_open(*log);
    crash_write(*log, "CRASH: abort() was called (a failed assert or a fatal library error)");
    PVOID frames[30];
    const USHORT n = RtlCaptureStackBackTrace(1, 30, frames, nullptr);
    char where[MAX_PATH + 40];
    char line[MAX_PATH + 80];
    for (USHORT i = 0; i < n; ++i) {
        describe(where, sizeof where, reinterpret_cast<DWORD64>(frames[i]));
        std::snprintf(line, sizeof line, "  #%u %s", static_cast<unsigned>(i), where);
        crash_write(*log, line);
    }
    crash_write(*log, "CRASH: end of the report");
    if (log->io) SDL_FlushIO(log->io);
    crash_close();
    crash_tell_player();
}

}  // namespace

void session_log_hook_crash(SessionLog& log) {
    if (log.crash_hooked) return;
    ULONG guarantee = 64 * 1024;  // room left on the stack to write the report after a stack overflow
    SetThreadStackGuarantee(&guarantee);
    g_crash_log = &log;
    log.prev_filter = SetUnhandledExceptionFilter(crash_filter);
    log.prev_abort = std::signal(SIGABRT, abort_handler);
    log.crash_hooked = true;
}

namespace {
void unhook_crash(SessionLog& log) {
    if (!log.crash_hooked) return;
    SetUnhandledExceptionFilter(log.prev_filter);
    std::signal(SIGABRT, log.prev_abort);
    g_crash_log = nullptr;
}
}  // namespace

void session_log_set_crash_box(const std::string& before, const std::string& after) {
    for (const auto& [from, to] : {std::pair{&before, g_crash_before}, std::pair{&after, g_crash_after}}) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, from->c_str(), -1, to, 512);
        if (n <= 0) to[0] = 0;
        to[511] = 0;
    }
}
#else
void session_log_hook_crash(SessionLog&) {}
void session_log_set_crash_box(const std::string&, const std::string&) {}

namespace {
void unhook_crash(SessionLog&) {}
}  // namespace
#endif

void SessionLogFree::operator()(SessionLog* log) const {
    if (!log) return;
    write_line(*log, "", "session ended");
    unhook_crash(*log);
    if (log->sdl_hooked) {
        SDL_LogOutputFunction now = nullptr;
        void* now_data = nullptr;
        SDL_GetLogOutputFunction(&now, &now_data);
        if (now == tee_output && now_data == log) SDL_SetLogOutputFunction(log->prev_out, log->prev_out_data);
    }
    if (log->io) {
        SDL_FlushIO(log->io);
        SDL_CloseIO(log->io);
    }
    if (log->mutex) SDL_DestroyMutex(log->mutex);
    delete log;
}

// ---- a game run with a window ----

namespace {

#ifdef _WIN32
std::string windows_version() {
    using VersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const FARPROC proc = ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : nullptr;
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    if (!proc || reinterpret_cast<VersionFn>(reinterpret_cast<void*>(proc))(&v) != 0) return "Windows";
    return "Windows " + std::to_string(v.dwMajorVersion) + "." + std::to_string(v.dwMinorVersion) + "." +
           std::to_string(v.dwBuildNumber);
}
#endif

[[maybe_unused]] std::string compiler_name() {  // (unused on the web, where there is no log)
#if defined(__clang__)
    return "clang " + std::to_string(__clang_major__) + "." + std::to_string(__clang_minor__);
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__GNUC__)
    return "gcc " + std::to_string(__GNUC__) + "." + std::to_string(__GNUC_MINOR__);
#else
    return "unknown compiler";
#endif
}

}  // namespace

SessionLogPtr session_log_start(const std::string& game_folder, const std::string& user_dir) {
#ifdef __EMSCRIPTEN__
    (void)game_folder;
    (void)user_dir;
    return nullptr;  // the web page has no folder of the user's to keep logs in
#else
    std::string err;
    std::optional<std::string> user;
    if (user_dir.empty()) {
        user = user_folder(&err);
    } else {  // an explicit user folder (tests): the default one is not touched
        user = user_dir.back() == '/' || user_dir.back() == '\\' ? user_dir : user_dir + "/";
    }
    if (!user) {
        SDL_Log("no session log: %s", err.c_str());
        return nullptr;
    }
    const std::string folder = *user + "logs" + std::string(1, user->back());  // the folder's own separator
    SessionLogPtr log = session_log_open_in(folder, session_log_now(), kKeepSessionLogs, &err);
    if (!log) {
        SDL_Log("no session log: %s", err.c_str());
        return nullptr;
    }
    session_log_hook_sdl(*log);
    session_log_hook_crash(*log);
    const LogTime t = session_log_now();
    char date[32];
    std::snprintf(date, sizeof date, "%04d-%02d-%02d", t.year, t.month, t.day);
    SDL_Log("Entente Engine session log, started %s (local time of this PC)", date);
    // The version is engine/CMakeLists.txt's ANTANTA_VERSION; no build date or time, so a rebuild gives the same exe.
#ifdef ANTANTA_RELEASE
    SDL_Log("Entente Engine %s, the release exe", ANTANTA_VERSION);
#else
    SDL_Log("Entente Engine %s, a developer build", ANTANTA_VERSION);
#endif
#ifdef NDEBUG
    SDL_Log("build: %s, %s, release", ANTANTA_VERSION, compiler_name().c_str());
#else
    SDL_Log("build: %s, %s, debug", ANTANTA_VERSION, compiler_name().c_str());
#endif
#ifdef _WIN32
    const std::string os = windows_version();
#else
    const std::string os = SDL_GetPlatform();
#endif
    SDL_Log("system: %s, %d logical CPUs, %d MB memory", os.c_str(), SDL_GetNumLogicalCPUCores(), SDL_GetSystemRAM());
    const int v = SDL_GetVersion();
    SDL_Log("SDL: %d.%d.%d, %s", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v), SDL_VERSIONNUM_MICRO(v),
            SDL_GetRevision());
    SDL_Log("game folder: %s", game_folder.c_str());
    if (!user_folder_note().empty()) SDL_Log("user folder: %s", user_folder_note().c_str());
    SDL_Log("log file: %s%s", folder.c_str(), log->name.c_str());
    return log;
#endif
}

void session_log_video_info(SDL_Window* window, SDL_Renderer* renderer) {
    const char* driver = SDL_GetCurrentVideoDriver();
    const char* name = renderer ? SDL_GetRendererName(renderer) : nullptr;
    int w = 0, h = 0, pw = 0, ph = 0;
    if (window) {
        SDL_GetWindowSize(window, &w, &h);
        SDL_GetWindowSizeInPixels(window, &pw, &ph);
    }
    SDL_Log("video: driver %s, renderer %s, window %d x %d (%d x %d pixels)", driver ? driver : "none",
            name ? name : "none", w, h, pw, ph);
}

}  // namespace antanta
