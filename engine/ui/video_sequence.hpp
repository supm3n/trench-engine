
//
// At startup the logo (Buka) and then the English intro play, then the main menu opens. The Medal button of the main
// menu plays the credits and goes back to the menu. Esc skips the video that is playing and goes on to the next one
// (the original: only Esc; ours also a mouse click, Space and Enter, ui/video_host.hpp). A video that cannot be opened
// is skipped the same way.
// With videos off (the original's `+video 0`) nothing starts.
#pragma once

#include <string>
#include <vector>

namespace antanta::ui {

enum class VideoRun { None, Startup, Credits };

class VideoSequence {
  public:
    void set_enabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }

    // Starts the startup list (logo, intro). False, and nothing plays, if videos are off, the list is empty or a
    // sequence is already running.
    bool start_startup(std::vector<std::string> files);
    // Starts the credits. Same rules.
    bool start_credits(const std::string& file);

    bool active() const { return run_ != VideoRun::None; }
    VideoRun run() const { return run_; }
    // The file to play now; empty when nothing plays.
    const std::string& current() const;

    // The video now playing is over: it ended, it was skipped or it could not be opened. Goes on to the next one.
    // Returns true while a video is still to play, false when the sequence is over and the menu is back.
    bool finish_current();

  private:
    bool begin(VideoRun run, std::vector<std::string> files);

    bool enabled_ = true;
    VideoRun run_ = VideoRun::None;
    std::vector<std::string> files_;
    std::size_t index_ = 0;
};

}  // namespace antanta::ui
