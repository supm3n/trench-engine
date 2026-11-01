#include "ui/video_sequence.hpp"

#include <utility>

namespace antanta::ui {

bool VideoSequence::begin(VideoRun run, std::vector<std::string> files) {
    if (!enabled_ || active() || files.empty()) return false;
    run_ = run;
    files_ = std::move(files);
    index_ = 0;
    return true;
}

bool VideoSequence::start_startup(std::vector<std::string> files) { return begin(VideoRun::Startup, std::move(files)); }

bool VideoSequence::start_credits(const std::string& file) { return begin(VideoRun::Credits, {file}); }

const std::string& VideoSequence::current() const {
    static const std::string none;
    return active() ? files_[index_] : none;
}

bool VideoSequence::finish_current() {
    if (!active()) return false;
    if (++index_ < files_.size()) return true;
    run_ = VideoRun::None;
    files_.clear();
    index_ = 0;
    return false;
}

}  // namespace antanta::ui
