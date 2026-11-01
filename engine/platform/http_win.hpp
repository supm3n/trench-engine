// HTTPS requests on Windows with WinHTTP (Windows' own DLL, no network library of ours). Shared by the updater
// (platform/update_win.hpp, docs/download.md "The updater"), the language packs and the public build's Releases notice
// (platform/release_check_win.hpp, docs/public-release.md). Nothing here downloads to a file or checks a signature.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace antanta::http {

// Only https://, or http:// to this PC (127.0.0.1 or localhost: the tests' server).
bool url_allowed(const std::string& url);
// A GET of `url`: the body goes to `sink` in pieces (false from it stops); more than `max_bytes`, a status other than
// 200, a redirect or a time-out (5 s to connect, 15 s without data) fails with a sentence in `error`.
bool http_get(const std::string& url, std::uint64_t max_bytes,
              const std::function<bool(const std::uint8_t*, std::size_t)>& sink, std::string* error);
// The same with more to say (the language packs and the Access login, docs/languages.md L3b): `method` GET or HEAD,
// `headers` extra request lines ("Name: value\r\n"), any status is an answer (the body goes to `sink` only for a
// 200); `status` and the response headers named in `want` (their values in the same order, "" when missing) come
// back. `wait_ms`: the longest wait for data (a long poll waits longer). `agent`: the User-Agent header. Fails on the
// network, a body over `max_bytes` or a stop from `sink`; the error never holds the headers (they may carry a token).
struct HttpAnswer {
    unsigned status = 0;
    std::vector<std::string> headers;
};
bool http_request(const std::string& method, const std::string& url, const std::string& headers,
                  const std::vector<std::string>& want, std::uint64_t max_bytes,
                  const std::function<bool(const std::uint8_t*, std::size_t)>& sink, HttpAnswer* answer,
                  std::string* error, unsigned wait_ms = 15000, const std::string& agent = "EntenteEngine-updater");

}  // namespace antanta::http
