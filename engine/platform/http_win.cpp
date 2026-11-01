#include "platform/http_win.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h first
#include <winhttp.h>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3 0x00002000
#endif

namespace antanta::http {

namespace {

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

bool local_url(const std::string& url) {
    return url.rfind("http://127.0.0.1:", 0) == 0 || url.rfind("http://127.0.0.1/", 0) == 0 ||
           url.rfind("http://localhost:", 0) == 0 || url.rfind("http://localhost/", 0) == 0;
}

std::string net_error(const char* step, DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_TIMEOUT: return "the download server did not answer in time";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return "the download server was not found (no internet connection?)";
        case ERROR_WINHTTP_CANNOT_CONNECT: return "no connection to the download server";
        case ERROR_WINHTTP_SECURE_FAILURE: return "the download server's secure connection could not be checked";
        default: return std::string("the download failed (") + step + ", WinHTTP error " + std::to_string(code) + ")";
    }
}

}  // namespace

bool url_allowed(const std::string& url) { return url.rfind("https://", 0) == 0 || local_url(url); }

bool http_get(const std::string& url, std::uint64_t max_bytes,
              const std::function<bool(const std::uint8_t*, std::size_t)>& sink, std::string* error) {
    HttpAnswer answer;
    if (!http_request("GET", url, "", {}, max_bytes, sink, &answer, error)) return false;
    if (answer.status != 200) {
        *error = "the download server answered " + std::to_string(answer.status) + " for " + url;
        return false;
    }
    return true;
}

bool http_request(const std::string& method, const std::string& url, const std::string& headers,
                  const std::vector<std::string>& want, std::uint64_t max_bytes,
                  const std::function<bool(const std::uint8_t*, std::size_t)>& sink, HttpAnswer* answer,
                  std::string* error, unsigned wait_ms, const std::string& agent) {
    if (!url_allowed(url)) {
        *error = "the address " + url + " is not allowed (only https://)";
        return false;
    }
    const std::wstring w = widen(url);
    wchar_t host[256] = {}, path[2048] = {};
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host, uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path, uc.dwUrlPathLength = 2048;
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) {
        *error = "the address " + url + " could not be read";
        return false;
    }
    std::wstring object = path;  // the path and the query
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength) object.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    Handle session{WinHttpOpen(widen(agent).c_str(),
                               local_url(url) ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.h) return *error = net_error("open", GetLastError()), false;
    WinHttpSetTimeouts(session.h, 5000, 5000, 15000, static_cast<int>(wait_ms));
    if (secure) {  // TLS 1.2 or 1.3 only (1.3 is unknown to older Windows 10: then 1.2 alone)
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
        }
    }
    Handle connection{WinHttpConnect(session.h, host, uc.nPort, 0)};
    if (!connection.h) return *error = net_error("connect", GetLastError()), false;
    Handle request{WinHttpOpenRequest(connection.h, widen(method).c_str(), object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.h) return *error = net_error("request", GetLastError()), false;
    DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;  // the bucket answers directly; a redirect is not followed
    WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof never);
    DWORD no_cookies = WINHTTP_DISABLE_COOKIES;  // nothing kept between requests
    WinHttpSetOption(request.h, WINHTTP_OPTION_DISABLE_FEATURE, &no_cookies, sizeof no_cookies);
    const std::wstring extra = widen(headers);
    if (!WinHttpSendRequest(request.h, extra.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extra.c_str(),
                            extra.empty() ? 0 : static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        return *error = net_error("send", GetLastError()), false;
    }
    DWORD status = 0, len = sizeof status;
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &len, WINHTTP_NO_HEADER_INDEX);
    answer->status = status;
    answer->headers.clear();
    for (const std::string& name : want) {
        const std::wstring wname = widen(name);
        DWORD size = 0;
        WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CUSTOM, wname.c_str(), WINHTTP_NO_OUTPUT_BUFFER, &size,
                            WINHTTP_NO_HEADER_INDEX);
        std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
        if (size && WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CUSTOM, wname.c_str(), value.data(), &size,
                                        WINHTTP_NO_HEADER_INDEX)) {
            value.resize(size / sizeof(wchar_t));
            answer->headers.push_back(narrow(value));
        } else {
            answer->headers.emplace_back();
        }
    }
    if (status != 200 || method == "HEAD") return true;
    std::vector<std::uint8_t> buf(1 << 16);
    std::uint64_t total = 0;
    while (true) {
        DWORD got = 0;
        if (!WinHttpReadData(request.h, buf.data(), static_cast<DWORD>(buf.size()), &got)) {
            return *error = net_error("read", GetLastError()), false;
        }
        if (got == 0) return true;
        total += got;
        if (total > max_bytes) {
            *error = "the file " + url + " is bigger than it should be";
            return false;
        }
        if (!sink(buf.data(), got)) {
            if (error->empty()) *error = "the download was stopped";
            return false;
        }
    }
}

}  // namespace antanta::http
