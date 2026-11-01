#include "platform/release_check_win.hpp"

#include <mutex>
#include <thread>

#include "platform/http_win.hpp"

namespace antanta::release_check {

struct Checker::Shared {
    std::mutex m;
    bool busy = false;
    bool loud_asked = false;  // "Check for updates" was pressed while the check at the start was running
    std::optional<Done> done;
    std::string version;
};

Checker::Checker(std::string current_version) : s_(std::make_shared<Shared>()) {
    s_->version = std::move(current_version);
}

Checker::~Checker() = default;

void Checker::check(bool loud) {
    {
        std::lock_guard<std::mutex> lock(s_->m);
        if (s_->busy) {
            if (loud) s_->loud_asked = true;
            return;
        }
        s_->busy = true;
    }
    // The thread keeps its state alive by itself, so the app may quit while it waits for the network.
    std::thread([s = s_, loud] {
        Done d;
        d.loud = loud;
        std::string body, err;
        http::HttpAnswer answer;
        // GitHub's API wants a User-Agent (docs.github.com, "User agent required") and names its JSON media type.
        const bool got = http::http_request(
            "GET", kLatestUrl, "Accept: application/vnd.github+json\r\n", {}, kMaxAnswerBytes,
            [&](const std::uint8_t* p, std::size_t n) {
                body.append(reinterpret_cast<const char*>(p), n);
                return true;
            },
            &answer, &err, 15000, "EntenteEngine/" + s->version);
        if (!got) {
            d.message = "GitHub could not be reached: " + err;
        } else if (answer.status == 404) {
            d.result = Result::UpToDate;
            d.message = "no release on GitHub yet";
        } else if (answer.status == 403 || answer.status == 429) {
            d.message = "GitHub's rate limit (answer " + std::to_string(answer.status) + ")";
        } else if (answer.status != 200) {
            d.message = "GitHub answered " + std::to_string(answer.status);
        } else if (auto r = read_release(body, &err)) {
            d.release = *r;
            d.result = version_newer(r->version, s->version) ? Result::Newer : Result::UpToDate;
            d.message = "the newest release is " + r->version;
        } else {
            d.message = err;
        }
        std::lock_guard<std::mutex> lock(s->m);
        if (s->loud_asked) d.loud = true, s->loud_asked = false;
        s->done = std::move(d);
        s->busy = false;
    }).detach();
}

std::optional<Checker::Done> Checker::take() {
    std::lock_guard<std::mutex> lock(s_->m);
    std::optional<Done> d = std::move(s_->done);
    s_->done.reset();
    return d;
}

}  // namespace antanta::release_check
