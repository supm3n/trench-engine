// The log of one play session (docs/bugs.md B5: the original erases its ReportError.log at every start, so the
// evidence of a crash is gone after the restart). A game run with a window writes one text file per session into
// %APPDATA%\Antanta\logs\, named by its start time, and keeps the newest 10. Batch runs and antanta-headless write no
// file.
//
// What goes into it: a header (build, system, SDL, game folder), every SDL_Log line with its time (the app's own
// load, new game and error lines go through SDL_Log, and SDL_Log still reaches stderr as before), and on Windows the
// exception code, the address and the return addresses (module and offset) of a crash. Closing the log (destroying
// the pointer) writes the last line and puts the hooks back.
//
// The pure parts (names, which logs to delete, the line format) and the file part (open_in, write) work without SDL's
// init, so the tests run them in a temp folder.
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;

namespace antanta {

// A local wall clock time, split up.
struct LogTime {
    int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0, millisecond = 0;
};
LogTime session_log_now();

// "session_2026-10-16_14-03-22.txt". `number` counts the logs of the same second: 1 (the first) has no suffix, 2 gives
// "session_2026-10-16_14-03-22_02.txt" (two digits, so the names still sort by age).
std::string session_log_name(const LogTime& t, int number);
// True for the names session_log_name makes and for no other file name.
bool is_session_log_name(const std::string& name);
// From the names found in the logs folder: the ones to delete so that the newest `keep` session logs stay. Other files
// are never in the answer.
std::vector<std::string> session_logs_to_delete(const std::vector<std::string>& names, std::size_t keep);
// One line of the log: "14:03:22.123  text\n" (tag, when not empty, goes in front of the text).
std::string session_log_line(const LogTime& t, std::string_view tag, std::string_view text);

struct SessionLog;
struct SessionLogFree {
    void operator()(SessionLog* l) const;
};
using SessionLogPtr = std::unique_ptr<SessionLog, SessionLogFree>;

// Creates the log file in `folder` (ending in a separator; created if missing) under the name for `now`, then deletes
// the older session logs beyond `keep` (this one counts). No hooks. null and the reason when the file cannot be made.
SessionLogPtr session_log_open_in(const std::string& folder, const LogTime& now, std::size_t keep, std::string* error);
// The name of the file that was made (without its folder).
const std::string& session_log_file_name(const SessionLog& log);
// Adds a line with the time of the call. Safe to call from several threads.
void session_log_write(SessionLog& log, std::string_view text);
// Every SDL_Log line also goes to the file (and on to the output function that was set before).
void session_log_hook_sdl(SessionLog& log);
// Windows: an unhandled exception (or abort) writes its code, address and stack into the file, and the same lines
// with the version into a short report a player can send, crash_<time>.txt in the folder above the log's folder (our
// user folder, docs/release.md). The release exe then tells the player where the report is, unless the process asked
// for no fault message boxes (SetErrorMode SEM_NOGPFAULTERRORBOX). Elsewhere: nothing.
void session_log_hook_crash(SessionLog& log);
// The release exe's crash box in the display language (docs/languages.md, slice L2): the text before and after the
// report's path, kept ready because the crash handler reads no tables. English until the app sets it. Elsewhere:
// nothing.
void session_log_set_crash_box(const std::string& before, const std::string& after);

// A game run with a window: opens the log in %APPDATA%\EntenteEngine\logs\ (keeping 10; `user_dir` instead of the
// default user folder when it is not empty, --user-dir), hooks SDL_Log and the crash handler, and writes the header.
// A failure is logged through SDL_Log and gives null (the game goes on without a file). Always null on the web.
SessionLogPtr session_log_start(const std::string& game_folder, const std::string& user_dir = "");
// The video driver, the renderer and the window size, through SDL_Log (call after the window is made).
void session_log_video_info(SDL_Window* window, SDL_Renderer* renderer);

}  // namespace antanta
