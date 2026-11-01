// The display language (docs/languages.md, slice L1): the string tables, the lookup with its fallback, plural forms,
// named placeholders, the pseudo-language `xx` and the marker check of docs/languages.md 0. No SDL here; the app
// lists the folders and logs the misses.
//
// Only the interface uses this. The simulation, saves, replays, traces and the network protocol keep reading the
// game's English tables (StringTable_eng.st, TextTable_eng.tt) exactly as before (docs/languages.md 5.3): the app and
// the front-end look a text up only where they show it.
//
// Where the texts come from: our own texts are built into the engine from engine/lang/<code>/ (language.txt, ui.txt,
// overrides.txt; CMake embeds them); a language's game texts and any further language sit in the user folder,
// <user folder>/lang/<code>/ (language.txt, ui.txt, game_st.txt, game_tt.txt, game_misc.txt and overrides.txt, the
// hand fixes of game texts), never in git. A folder with a language.txt is a language. `xx` is made from English at
// load time and has no folder. The lookup order (docs/languages.md 6.1): overrides, the language's tables, then
// English; the game's English is fixed by <user folder>/lang/en/overrides.txt, in every language.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "formats/museum.hpp"

namespace antanta::ui {

// The plural rules of docs/languages.md 5.1: English-like (English, German, Italian: one for 1), French-like (one
// for 0 and 1), Slavic (Russian, Ukrainian: one, few, many) and none (one form).
enum class PluralRule : std::uint8_t { English, French, Slavic, None };

struct LangInfo {
    std::string code;  // the folder name: "en", "de", "xx"
    std::string name;  // in its own words: "English", "Deutsch"
    PluralRule plural = PluralRule::English;
};

using LangTable = std::map<std::string, std::string>;

// The table files: "key = value" per line, '#' starts a comment line, empty lines are skipped, a UTF-8 byte order
// mark is skipped. The key is everything before the first '=', the value everything after it, both trimmed; a value
// in double quotes keeps its spaces at the edges (" ally "). In the value "\n" is a line break and "\\" a backslash.
// Fails on a line without '=', an empty key or a key given twice.
std::optional<LangTable> lang_parse_table(std::string_view text, std::string* error);
// language.txt, the same format: "name" (required), "plural" (english, french, slavic or none; default english).
// "font" is read by a later slice and ignored here.
std::optional<LangInfo> lang_parse_info(std::string_view code, std::string_view text, std::string* error);
// A folder name that can be a language: 1 to 15 of a-z, A-Z, 0-9, '-' and '_'.
bool lang_code_valid(std::string_view code);

// The CLDR plural category of n: "one", "few", "many" or "other".
const char* lang_plural_form(PluralRule rule, long long n);
// Named placeholders: every "{name}" of `args` replaced by its value; an unknown one stays as it is.
std::string lang_fill(std::string_view text, const std::vector<std::pair<std::string, std::string>>& args);

// The pseudo-language: the letters accented (every ASCII letter becomes another letter, all of them in the four
// fonts, docs/languages.md 2.1), the text in brackets and about 35 % longer ("Save" -> "[Šåṽé~~]"). Spaces at the edges
// stay outside the brackets, "{name}" placeholders and line breaks stay as they are, an empty text stays empty.
std::string lang_pseudo(std::string_view english);
// The long pseudo-language `xl` (docs/languages.md 2.4 and 2.5): accented and in brackets as `xx`, but every word made
// 40 % longer with letters of its own (no `~`), and every other pair of words glued into one, as German compounds
// ("Seat kept after a drop" -> "[Šéåţéåķéþţķé åƒţéŕåƒåå đŕöþđŕ]"). It finds the places a long text and a long word
// do not fit.
std::string lang_pseudo_long(std::string_view english);
// The marker check: a drawn text that came through the lookup in `xx` has no ASCII letter left (numbers, signs and
// glued pieces of several looked-up texts pass). False means the text, or a piece of it, was not looked up.
bool lang_marked(std::string_view drawn);

// A language ready to use. `ours` are our texts in this language, `ours_en` the English ones (the fallback), `game`
// its game texts (game_st.txt and game_tt.txt; a .tt record's title is "KEY.title").
struct Language {
    LangInfo info{"en", "English", PluralRule::English};
    LangTable ours, ours_en, game;
    bool pseudo = false;                 // `xx` or `xl`: made from English, the marker check on
    bool pseudo_long = false;            // `xl`
    std::set<std::string> misses;        // the marker check: "site text" already reported
    mutable std::set<std::string> data;  // the marker check: names and other data it leaves out (lang_data)
    std::vector<LangInfo> offered;       // the app's list for the Language row of Game options (lang_list)
};

// The languages built into the engine (engine/lang/<code>/, embedded by CMake): code, language.txt, ui.txt and
// overrides.txt (hand fixes of our texts, read over ui.txt; docs/languages.md 6.1).
struct BuiltinLang {
    const char* code;
    const unsigned char* language_txt;
    const unsigned char* ui_txt;
    const unsigned char* overrides_txt;
};
std::vector<BuiltinLang> lang_builtin();

// English, built in (our texts only; the game's English is the game's own tables).
const Language& lang_english();
// The languages to offer: the built-in ones and every folder of `user_lang_dir` (may be empty) with a readable
// language.txt, sorted by code; `xx` is not listed. Unreadable folders are skipped and named in `warnings`.
std::vector<LangInfo> lang_list(const std::string& user_lang_dir, std::vector<std::string>* warnings);
// Loads a language: `xx`, a built-in one, or a folder of `user_lang_dir` (its files over the built-in ones). A table
// that cannot be read is skipped and named in `warnings` (English shows instead); fails only for an unknown code.
std::optional<Language> lang_load(std::string_view code, const std::string& user_lang_dir,
                                  std::vector<std::string>* warnings, std::string* error);
// The first start: the first of the system's preferred languages ("de", "de-AT", "pt_BR") whose code is in
// `available`, else "en".
std::string lang_pick(const std::vector<std::string>& preferred, const std::vector<LangInfo>& available);

// Our text of `key`: this language, then English, then the key. In `xx` the English made pseudo.
std::string lang_text(const Language& l, std::string_view key);
// The same with named placeholders.
std::string lang_text(const Language& l, std::string_view key,
                      const std::vector<std::pair<std::string, std::string>>& args);
// A plural text: "key.<form>" of n's form in this language (then "key.other", then English's form), with "{n}" and
// `args` filled in.
std::string lang_plural(const Language& l, std::string_view key, long long n,
                        const std::vector<std::pair<std::string, std::string>>& args = {});
// The English sentences that the parts without a language produce as text (the updater, the network server and
// client, the Lua scripts) are mapped back to their keys for display, as docs/languages.md 5.3 does for the computer
// names: every key of our English that starts with `prefix` is tried, and the text of the one that matches `english`
// is shown in this language. A key's English may hold placeholders; the pieces they catch are mapped the same way in
// turn (a reason inside a reason). A plural key matches by its ".other" form and is shown by the {n} it caught. A text
// that matches no key comes back as it is: a technical detail, which stays English as the logs do.
std::string lang_reverse(const Language& l, std::string_view english, std::string_view prefix);
// A game text for display: this language's text of `key`, else `english` (the game's own English of that key). In
// `xx` the English made pseudo.
std::string lang_game(const Language& l, std::string_view key, std::string_view english);
// A table of the game read from its English file, for display: every value through lang_game (titles of a text table
// as "KEY.title"). English leaves it as it is.
void lang_localize(const Language& l, std::map<std::string, std::string>& strings);
void lang_localize(const Language& l, TextTable& table);

// The short form of a shown text for a place too small for it (docs/languages.md 2.4 rule 2): the value of "KEY.short"
// when this language's text of KEY (ours or the game's) is `shown`; empty when there is none. English has none.
std::string lang_short(const Language& l, std::string_view shown);

// The marker check of one drawn text (docs/languages.md 0): in `xx`, a text that is not marked and was not reported
// from this place before gives the line to log ("lang: not looked up at ui/frontend.cpp:1265 (make_plain_label):
// \"Interface scale\""); otherwise nothing. Only `xx` checks.
std::optional<std::string> lang_check(Language& l, std::string_view drawn, const std::source_location& where);
// Data shown on its own or inside a looked-up text (a player's or a profile's name, a save's or a map's file name, an
// address): names stay as they are in every language, so in `xx` the marker check leaves `text` out wherever it is
// drawn (docs/languages.md 0). Nothing outside `xx`. Returns `text`.
const std::string& lang_data(const Language* l, const std::string& text);

}  // namespace antanta::ui
