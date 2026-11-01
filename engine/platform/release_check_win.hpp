// The public build's Releases notice on Windows (platform/release_check.hpp, docs/public-release.md): one GET of
// GitHub's answer for the newest release (platform/http_win.hpp) on a thread of its own, so the menu never waits.
// Nothing is downloaded but that answer, and nothing is installed.
#pragma once

#include <memory>
#include <optional>
#include <string>

#include "platform/release_check.hpp"

namespace antanta::release_check {

class Checker {
  public:
    // Newer: a release newer than this build is out. UpToDate: none, or no release yet (GitHub answers 404). Failed: no
    // internet, a rate limit, another status or an answer that does not read (`message` says which, for the log).
    enum class Result { Newer, UpToDate, Failed };
    struct Done {
        Result result = Result::Failed;
        bool loud = false;    // the menu's "Check for updates" asked (the check at the start only shows a newer one)
        std::string message;  // for the session log
        Release release;      // Newer
    };
    explicit Checker(std::string current_version);
    ~Checker();
    void check(bool loud);       // returns at once; while one runs, it answers out loud when `loud`
    std::optional<Done> take();  // a finished check, once (the app asks every frame)

  private:
    struct Shared;
    std::shared_ptr<Shared> s_;
};

}  // namespace antanta::release_check
