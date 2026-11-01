// Lua's number conversions without the C locale (script/lua_user.h, docs/missions.md 5.6). std::to_chars and
// std::from_chars are exact (shortest round trip aside, "%.14g" is printf's rule) and the same on every target.
#include "script/lua_user.h"

#include <charconv>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>

extern "C" void antanta_lua_number2str(char* s, double n) {
    // lvm.c's buffer is 32 characters: "%.14g" needs at most 1 + 1 + 13 + 5 (e-308) + 1.
    const auto r = std::to_chars(s, s + 31, n, std::chars_format::general, 14);
    *(r.ec == std::errc() ? r.ptr : s) = '\0';
}

extern "C" double antanta_lua_str2number(const char* s, char** end) {
    const char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\v' || *p == '\f' || *p == '\r') ++p;
    bool neg = false;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    if (*p == '+' || *p == '-') {  // from_chars would take a second '-'; strtod reads nothing
        *end = const_cast<char*>(s);
        return 0.0;
    }
    double v = 0.0;
    const auto r = std::from_chars(p, p + std::strlen(p), v, std::chars_format::general);
    if (r.ptr == p) {
        *end = const_cast<char*>(s);
        return 0.0;
    }
    if (r.ec == std::errc::result_out_of_range) {  // strtod: HUGE_VAL, or 0 when it underflows
        const char* e = p;
        while (e < r.ptr && *e != 'e' && *e != 'E') ++e;
        v = e + 1 < r.ptr && e[1] == '-' ? 0.0 : HUGE_VAL;
    }
    *end = const_cast<char*>(r.ptr);
    return neg ? -v : v;
}

extern "C" int antanta_lua_number2int(double n) {
    return n > -2147483649.0 && n < 2147483648.0 ? static_cast<int>(n) : INT_MIN;
}
