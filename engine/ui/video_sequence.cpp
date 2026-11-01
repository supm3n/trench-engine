#include "ui/video_sequence.hpp"

#include <utility>

namespace antanta::ui {

namespace {
std::string in(const std::string& dir, const std::string& file) {
    if (dir.empty() || dir.back() == '/' || dir.back() == '\\') return dir + file;
    return dir + '/' + file;
}
}  // namespace

std::vector<std::string> video_sources(const std::string& name, const std::string& media_dir,
                                       const std::string& converted_dir) {
    static const char* const kOriginal[][2] = {
        {"buka", "Buka.wmv"}, {"intro-en", "Intro-en.wmv"}, {"credits", "Credits.wmv"}};
    std::vector<std::string> out;
    if (!media_dir.empty()) {
        for (const auto& o : kOriginal) {
            if (name == o[0]) out.push_back(in(media_dir, o[1]));
        }
    }
    if (!converted_dir.empty()) out.push_back(in(converted_dir, name + ".mpg"));
    return out;
}

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
