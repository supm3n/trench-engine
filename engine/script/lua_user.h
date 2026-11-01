/* Lua 5.0.3's user header (LUA_USER_H, included by lua.h; docs/missions.md 5.6). The vendored sources stay unmodified:
   these macros replace their number conversions, which go through sprintf and strtod and so depend on the C locale,
   with ours (script/lua_user.cpp), the same on clang, MSVC and wasm. lua_number2int is the float-to-int cast of table
   keys, made safe for values out of the int range. C, as the Lua sources include it. */
#ifndef ANTANTA_SCRIPT_LUA_USER_H
#define ANTANTA_SCRIPT_LUA_USER_H

#ifdef __cplusplus
extern "C" {
#endif

/* "%.14g" (LUA_NUMBER_FMT) into s, which holds at least 32 characters (lvm.c's buffer). */
void antanta_lua_number2str(char* s, double n);
/* strtod in the "C" locale: leading white space, a sign, decimal digits, a point, an exponent; *end after the last
   character read, or s when nothing was read. */
double antanta_lua_str2number(const char* s, char** end);
/* (int) n for n in the int range, else INT_MIN (no undefined cast). */
int antanta_lua_number2int(double n);

#ifdef __cplusplus
}
#endif

#define lua_number2str(s, n) antanta_lua_number2str((s), (n))
#define lua_str2number(s, p) antanta_lua_str2number((s), (p))
#define lua_number2int(i, n) ((i) = antanta_lua_number2int(n))

#endif
