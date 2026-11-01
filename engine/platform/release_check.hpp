// The public build's Releases notice (docs/public-release.md, decisions.md 40): the public build never updates itself;
// at the start (and on the menu's "Check for updates") it asks GitHub which release of the public repo is the newest
// and, when that one is newer than this build, only says so and can open the release page in the browser. Pure
// functions here (the year versions, shared with the updater, and reading GitHub's answer); the request on its own
// thread is platform/release_check_win.hpp.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace antanta::release_check {

// The public repo and GitHub's answer for its newest release (the "latest" one: not a draft, not a pre-release).
constexpr std::string_view kRepo = "supm3n/trench-engine";
constexpr const char* kLatestUrl = "https://api.github.com/repos/supm3n/trench-engine/releases/latest";
constexpr std::size_t kMaxAnswerBytes = 1024 * 1024;  // the answer lists the files too; a few kB in practice

// "<year>.<n>" or "<year>.<n>.<m>": two or three whole numbers. 2026.1 < 2026.2 < 2026.10 < 2027.1.
std::optional<std::vector<std::uint32_t>> parse_version(std::string_view v);
// True only when both parse and `candidate` is the later one.
bool version_newer(std::string_view candidate, std::string_view current);

// What the notice takes from GitHub's answer.
struct Release {
    std::string version;  // "tag_name" without its "v": "v2026.2" gives 2026.2
    std::string url;      // "html_url", the release's page
};
// Reads "tag_name" and "html_url" of the JSON object GitHub answers (other members, nested or not, are skipped). Fails
// with a sentence in `error` on JSON that does not parse, a missing member, a tag that is not "v" and a version, or a
// page that is not https://github.com/<kRepo>/releases/...
std::optional<Release> read_release(std::string_view json, std::string* error);

}  // namespace antanta::release_check
