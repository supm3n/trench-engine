#include "ui/lang.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace antanta::ui {

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::string unescape(std::string_view v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size() && (v[i + 1] == 'n' || v[i + 1] == '\\')) {
            out += v[i + 1] == 'n' ? '\n' : '\\';
            ++i;
        } else {
            out += v[i];
        }
    }
    return out;
}

// The paths of the user folder are UTF-8 (SDL); std::filesystem takes them as such only from a u8string.
std::filesystem::path fs_path(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

std::optional<std::string> read_text(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) return std::nullopt;
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The pseudo-language's letters: a..z, then A..Z, each one UTF-8 letter that Liberation Serif Bold, Liberation Sans,
// Times New Roman Bold and Arial all have (checked against their character maps, 2026-10-22).
constexpr const char* kPseudoLower[26] = {"å", "ƀ", "ç", "đ", "é", "ƒ", "ĝ", "ĥ", "î", "ĵ", "ķ", "ļ", "ɱ",
                                          "ñ", "ö", "þ", "ǫ", "ŕ", "š", "ţ", "û", "ṽ", "ŵ", "ẋ", "ý", "ž"};
constexpr const char* kPseudoUpper[26] = {"Å", "Ɓ", "Ç", "Đ", "É", "Ƒ", "Ĝ", "Ĥ", "Î", "Ĵ", "Ķ", "Ļ", "Ṁ",
                                          "Ñ", "Ö", "Þ", "Ǫ", "Ŕ", "Š", "Ţ", "Û", "Ṽ", "Ŵ", "Ẋ", "Ý", "Ž"};

bool ascii_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

const std::string* find(const LangTable& t, std::string_view key) {
    const auto it = t.find(std::string(key));
    return it != t.end() ? &it->second : nullptr;
}

void merge_file(LangTable& into, const std::filesystem::path& p, std::vector<std::string>* warnings) {
    const std::optional<std::string> text = read_text(p);
    if (!text) return;  // a table a language does not have yet
    std::string err;
    std::optional<LangTable> t = lang_parse_table(*text, &err);
    if (!t) {
        if (warnings) warnings->push_back(reinterpret_cast<const char*>(p.u8string().c_str()) + (": " + err));
        return;
    }
    for (auto& [k, v] : *t) into[k] = std::move(v);
}

std::string builtin_text(const unsigned char* s) { return s ? reinterpret_cast<const char*>(s) : std::string(); }

}  // namespace

std::optional<LangTable> lang_parse_table(std::string_view text, std::string* error) {
    if (text.substr(0, 3) == "\xef\xbb\xbf") text.remove_prefix(3);
    LangTable out;
    std::size_t start = 0;
    int line_no = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        const std::string_view line = trim(text.substr(start, end - start));
        start = end + 1;
        ++line_no;
        if (line.empty() || line[0] == '#') continue;
        const std::size_t eq = line.find('=');
        const std::string_view key = eq == std::string_view::npos ? std::string_view() : trim(line.substr(0, eq));
        if (key.empty()) {
            *error = "line " + std::to_string(line_no) + ": not \"key = value\"";
            return std::nullopt;
        }
        std::string_view value = trim(line.substr(eq + 1));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
        if (!out.emplace(std::string(key), unescape(value)).second) {
            *error = "line " + std::to_string(line_no) + ": '" + std::string(key) + "' given twice";
            return std::nullopt;
        }
    }
    return out;
}

std::optional<LangInfo> lang_parse_info(std::string_view code, std::string_view text, std::string* error) {
    std::optional<LangTable> t = lang_parse_table(text, error);
    if (!t) return std::nullopt;
    LangInfo info;
    info.code = std::string(code);
    const std::string* name = find(*t, "name");
    if (!name || name->empty()) {
        *error = "no name";
        return std::nullopt;
    }
    info.name = *name;
    if (const std::string* p = find(*t, "plural")) {
        if (*p == "english") {
            info.plural = PluralRule::English;
        } else if (*p == "french") {
            info.plural = PluralRule::French;
        } else if (*p == "slavic") {
            info.plural = PluralRule::Slavic;
        } else if (*p == "none") {
            info.plural = PluralRule::None;
        } else {
            *error = "plural '" + *p + "' is not english, french, slavic or none";
            return std::nullopt;
        }
    }
    return info;
}

bool lang_code_valid(std::string_view code) {
    if (code.empty() || code.size() > 15) return false;
    return std::all_of(code.begin(), code.end(),
                       [](char c) { return ascii_letter(c) || (c >= '0' && c <= '9') || c == '-' || c == '_'; });
}

const char* lang_plural_form(PluralRule rule, long long n) {
    const long long a = n < 0 ? -n : n;
    switch (rule) {
        case PluralRule::English: return a == 1 ? "one" : "other";
        case PluralRule::French: return a <= 1 ? "one" : "other";
        case PluralRule::Slavic: {  // CLDR, whole numbers: 1, 21, 101 one; 2-4, 22-24 few; the rest many
            const long long d = a % 10, h = a % 100;
            if (d == 1 && h != 11) return "one";
            if (d >= 2 && d <= 4 && (h < 12 || h > 14)) return "few";
            return "many";
        }
        case PluralRule::None: return "other";
    }
    return "other";
}

std::string lang_fill(std::string_view text, const std::vector<std::pair<std::string, std::string>>& args) {
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t open = text.find('{', i);
        const std::size_t close = open == std::string_view::npos ? open : text.find('}', open);
        if (close == std::string_view::npos) break;
        const std::string_view name = text.substr(open + 1, close - open - 1);
        const auto it = std::find_if(args.begin(), args.end(), [&](const auto& a) { return a.first == name; });
        out += text.substr(i, open - i);
        if (it != args.end()) {
            out += it->second;
        } else {
            out += text.substr(open, close - open + 1);
        }
        i = close + 1;
    }
    out += text.substr(std::min(i, text.size()));
    return out;
}

std::string lang_pseudo(std::string_view english) {
    std::size_t lead = 0, tail = english.size();
    while (lead < tail && english[lead] == ' ') ++lead;
    while (tail > lead && english[tail - 1] == ' ') --tail;
    if (lead == tail) return std::string(english);
    std::string out(english.substr(0, lead));
    out += '[';
    std::size_t chars = 0;  // characters (UTF-8 lead bytes) of the text, for the padding
    bool in_placeholder = false;
    for (std::size_t i = lead; i < tail; ++i) {
        const char c = english[i];
        if ((static_cast<unsigned char>(c) & 0xc0) != 0x80) ++chars;
        if (c == '{') in_placeholder = true;
        if (!in_placeholder && ascii_letter(c)) {
            out += c >= 'a' ? kPseudoLower[c - 'a'] : kPseudoUpper[c - 'A'];
        } else {
            out += c;
        }
        if (c == '}') in_placeholder = false;
    }
    out.append((chars * 35 + 99) / 100, '~');
    out += ']';
    out += english.substr(tail);
    return out;
}

std::string lang_pseudo_long(std::string_view english) {
    std::size_t lead = 0, tail = english.size();
    while (lead < tail && english[lead] == ' ') ++lead;
    while (tail > lead && english[tail - 1] == ' ') --tail;
    if (lead == tail) return std::string(english);
    // The words between spaces and line breaks; a placeholder ("{name}") is left as it is, the letters around it
    // ("{n}x", "{name}'s") are accented.
    std::vector<std::pair<std::string_view, char>> words;  // a word and the separator after it (0: the end)
    for (std::size_t i = lead; i < tail;) {
        std::size_t end = i;
        while (end < tail && english[end] != ' ' && english[end] != '\n') ++end;
        words.emplace_back(english.substr(i, end - i), end < tail ? english[end] : '\0');
        i = end + 1;
    }
    auto letters = [](std::string_view w) {  // outside placeholders
        std::ptrdiff_t n = 0;
        bool in_placeholder = false;
        for (const char c : w) {
            if (c == '{') in_placeholder = true;
            n += !in_placeholder && ascii_letter(c);
            if (c == '}') in_placeholder = false;
        }
        return n;
    };
    std::string out(english.substr(0, lead));
    out += '[';
    bool glued = false;  // the word before was glued to this one
    for (std::size_t k = 0; k < words.size(); ++k) {
        const auto [w, after] = words[k];
        const auto n = letters(w);
        std::vector<const char*> small;  // the word's small letters, accented: its 40 % more come from them
        bool in_placeholder = false;
        for (const char c : w) {
            if (c == '{') in_placeholder = true;
            if (!in_placeholder && ascii_letter(c)) {
                out += c >= 'a' ? kPseudoLower[c - 'a'] : kPseudoUpper[c - 'A'];
                if (c >= 'a') small.push_back(kPseudoLower[c - 'a']);
            } else {
                out += c;
            }
            if (c == '}') in_placeholder = false;
        }
        const auto more = static_cast<std::size_t>((n * 40 + 99) / 100);
        for (std::size_t i = 0; i < more; ++i) out += small.empty() ? kPseudoLower['e' - 'a'] : small[i % small.size()];
        if (after == '\0') break;
        // Every other pair of letter words becomes one compound: no space between them.
        const bool glue = !glued && after == ' ' && n > 0 && k + 1 < words.size() && letters(words[k + 1].first) > 0;
        if (!glue) out += after;
        glued = glue;
    }
    out += ']';
    out += english.substr(tail);
    return out;
}

bool lang_marked(std::string_view drawn) {
    return std::none_of(drawn.begin(), drawn.end(), [](char c) { return ascii_letter(c); });
}

const Language& lang_english() {
    static const Language kEnglish = [] {
        Language l;
        for (const BuiltinLang& b : lang_builtin()) {
            if (std::strcmp(b.code, "en") != 0) continue;
            std::string err;
            if (auto t = lang_parse_table(builtin_text(b.ui_txt), &err)) l.ours_en = std::move(*t);
        }
        return l;
    }();
    return kEnglish;
}

std::vector<LangInfo> lang_list(const std::string& user_lang_dir, std::vector<std::string>* warnings) {
    std::map<std::string, LangInfo> found;
    for (const BuiltinLang& b : lang_builtin()) {
        std::string err;
        if (auto info = lang_parse_info(b.code, builtin_text(b.language_txt), &err)) found[b.code] = std::move(*info);
    }
    std::error_code ec;
    if (!user_lang_dir.empty()) {
        for (const auto& entry : std::filesystem::directory_iterator(fs_path(user_lang_dir), ec)) {
            const std::u8string name8 = entry.path().filename().u8string();
            const std::string code(name8.begin(), name8.end());
            if (!entry.is_directory(ec) || !lang_code_valid(code) || code == "xx" || code == "xl") continue;
            const std::optional<std::string> text = read_text(entry.path() / "language.txt");
            if (!text) continue;  // not a language folder
            std::string err;
            if (auto info = lang_parse_info(code, *text, &err)) {
                found[code] = std::move(*info);
            } else if (warnings) {
                warnings->push_back("lang/" + code + "/language.txt: " + err);
            }
        }
    }
    std::vector<LangInfo> out;
    for (auto& [code, info] : found) out.push_back(std::move(info));
    return out;
}

std::optional<Language> lang_load(std::string_view code, const std::string& user_lang_dir,
                                  std::vector<std::string>* warnings, std::string* error) {
    Language l;
    l.ours_en = lang_english().ours_en;
    if (code == "xx") {
        l.info = LangInfo{"xx", lang_pseudo("Pseudo"), PluralRule::English};
        l.pseudo = true;
        return l;
    }
    if (code == "xl") {  // docs/languages.md 2.5: the overflow check's long texts
        l.info = LangInfo{"xl", lang_pseudo_long("Pseudo long"), PluralRule::English};
        l.pseudo = l.pseudo_long = true;
        return l;
    }
    bool known = false;
    for (const BuiltinLang& b : lang_builtin()) {
        if (code != b.code) continue;
        std::string err;
        auto info = lang_parse_info(code, builtin_text(b.language_txt), &err);
        auto ours = lang_parse_table(builtin_text(b.ui_txt), &err);
        if (info) l.info = std::move(*info), known = true;
        if (ours) l.ours = std::move(*ours);
        if (auto over = lang_parse_table(builtin_text(b.overrides_txt), &err)) {  // hand fixes over the table
            for (auto& [k, v] : *over) l.ours[k] = std::move(v);
        }
    }
    if (!user_lang_dir.empty() && lang_code_valid(code)) {
        const std::filesystem::path dir = fs_path(user_lang_dir) / fs_path(std::string(code));
        if (const std::optional<std::string> text = read_text(dir / "language.txt")) {
            std::string err;
            if (auto info = lang_parse_info(code, *text, &err)) {
                l.info = std::move(*info), known = true;
            } else if (warnings) {
                warnings->push_back("lang/" + std::string(code) + "/language.txt: " + err);
            }
        }
        if (known) {
            merge_file(l.ours, dir / "ui.txt", warnings);
            // The game texts, each file over the one before: our fixes of the game's English (the fallback of every
            // language), the language's tables, its hand fixes (docs/languages.md 6.1).
            if (code != "en") merge_file(l.game, fs_path(user_lang_dir) / "en" / "overrides.txt", warnings);
            for (const char* f : {"game_st.txt", "game_tt.txt", "game_misc.txt", "overrides.txt"})
                merge_file(l.game, dir / f, warnings);
        }
    }
    if (!known) {
        *error = "no language '" + std::string(code) + "'";
        return std::nullopt;
    }
    return l;
}

std::string lang_pick(const std::vector<std::string>& preferred, const std::vector<LangInfo>& available) {
    for (const std::string& p : preferred) {
        std::string want = p.substr(0, p.find_first_of("-_"));
        std::transform(want.begin(), want.end(), want.begin(),
                       [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; });
        for (const LangInfo& a : available) {
            if (a.code == want) return a.code;
        }
    }
    return "en";
}

namespace {

std::string pseudo_of(const Language& l, std::string_view english) {
    return l.pseudo_long ? lang_pseudo_long(english) : lang_pseudo(english);
}

}  // namespace

std::string lang_text(const Language& l, std::string_view key) {
    const std::string* en = find(l.ours_en, key);
    if (l.pseudo) return pseudo_of(l, en ? *en : key);
    if (const std::string* t = find(l.ours, key)) return *t;
    return en ? *en : std::string(key);
}

std::string lang_text(const Language& l, std::string_view key,
                      const std::vector<std::pair<std::string, std::string>>& args) {
    return lang_fill(lang_text(l, key), args);
}

std::string lang_plural(const Language& l, std::string_view key, long long n,
                        const std::vector<std::pair<std::string, std::string>>& args) {
    const std::string k(key);
    const std::string form = k + "." + lang_plural_form(l.info.plural, n);
    const std::string en_form = k + "." + lang_plural_form(PluralRule::English, n);
    std::string text;
    if (l.pseudo) {
        text = lang_text(l, en_form);
    } else if (const std::string* t = find(l.ours, form)) {
        text = *t;
    } else if (const std::string* o = find(l.ours, k + ".other")) {
        text = *o;
    } else if (const std::string* en = find(l.ours_en, en_form)) {
        text = *en;
    } else {
        text = en_form;
    }
    std::vector<std::pair<std::string, std::string>> all = args;
    all.emplace_back("n", std::to_string(n));
    return lang_fill(text, all);
}

namespace {

// One key's English against a text: true when the text is that English with something in each placeholder, and
// then the pieces in `args`. A middle text after a placeholder is the first one found, the last one must end the text,
// a placeholder catches at least one character, and two placeholders side by side never match.
bool reverse_match(std::string_view pattern, std::string_view text,
                   std::vector<std::pair<std::string, std::string>>* args) {
    args->clear();
    std::vector<std::string_view> literals;
    std::vector<std::string_view> names;
    std::size_t i = 0;
    for (;;) {
        const std::size_t open = pattern.find('{', i);
        const std::size_t close = open == std::string_view::npos ? open : pattern.find('}', open);
        if (close == std::string_view::npos) break;
        literals.push_back(pattern.substr(i, open - i));
        names.push_back(pattern.substr(open + 1, close - open - 1));
        i = close + 1;
    }
    literals.push_back(pattern.substr(std::min(i, pattern.size())));
    if (names.empty()) return text == literals[0];
    if (text.substr(0, literals[0].size()) != literals[0]) return false;
    std::size_t at = literals[0].size();
    for (std::size_t k = 0; k < names.size(); ++k) {
        const std::string_view next = literals[k + 1];
        std::size_t end;
        if (k + 1 == names.size()) {
            if (text.size() < at + next.size() || text.substr(text.size() - next.size()) != next) return false;
            end = text.size() - next.size();
        } else {
            if (next.empty()) return false;
            end = text.find(next, at);
            if (end == std::string_view::npos) return false;
        }
        if (end <= at) return false;
        args->emplace_back(std::string(names[k]), std::string(text.substr(at, end - at)));
        at = end + next.size();
    }
    return true;
}

}  // namespace

std::string lang_reverse(const Language& l, std::string_view english, std::string_view prefix) {
    const LangTable& en = l.ours_en;
    const std::string* best_key = nullptr;
    std::size_t best_fixed = 0;
    std::vector<std::pair<std::string, std::string>> best_args, args;
    for (auto it = en.lower_bound(std::string(prefix)); it != en.end() && it->first.starts_with(prefix); ++it) {
        const std::string& key = it->first;
        if (key.ends_with(".one") || key.ends_with(".few") || key.ends_with(".many")) continue;  // ".other" stands
        if (!reverse_match(it->second, english, &args)) continue;
        std::size_t fixed = it->second.size();  // the most text of its own wins: a whole sentence over a pattern
        for (const auto& a : args) fixed -= a.first.size() + 2;
        if (fixed == 0 || (best_key && fixed <= best_fixed)) continue;  // a placeholder alone matches nothing
        best_key = &key, best_fixed = fixed, best_args = args;
    }
    if (!best_key) return std::string(english);
    long long n = 0;
    bool count = false;
    std::vector<std::pair<std::string, std::string>> shown;
    for (const auto& [name, piece] : best_args) {
        if (name == "n") {
            char* end = nullptr;
            n = std::strtoll(piece.c_str(), &end, 10);
            count = end != piece.c_str() && *end == '\0';
            if (count) continue;
        }
        shown.emplace_back(name, lang_reverse(l, piece, prefix));
    }
    if (best_key->ends_with(".other") && count)
        return lang_plural(l, std::string_view(*best_key).substr(0, best_key->size() - 6), n, shown);
    if (count) shown.emplace_back("n", std::to_string(n));
    return lang_text(l, *best_key, shown);
}

std::string lang_game(const Language& l, std::string_view key, std::string_view english) {
    if (l.pseudo) return pseudo_of(l, english);
    if (const std::string* t = find(l.game, key)) return *t;
    return std::string(english);
}

void lang_localize(const Language& l, std::map<std::string, std::string>& strings) {
    if (!l.pseudo && l.game.empty()) return;
    for (auto& [key, value] : strings) value = lang_game(l, key, value);
}

void lang_localize(const Language& l, TextTable& table) {
    if (!l.pseudo && l.game.empty()) return;
    for (auto& [key, r] : table) {
        if (!r.title.empty()) r.title = lang_game(l, key + ".title", r.title);
        r.text = lang_game(l, key, r.text);
    }
}

std::string lang_short(const Language& l, std::string_view shown) {
    for (const LangTable* t : {&l.ours, &l.game}) {
        for (const auto& [key, value] : *t) {
            if (!key.ends_with(".short")) continue;
            const std::string* full = find(*t, std::string_view(key).substr(0, key.size() - 6));
            if (full && *full == shown) return value;
        }
    }
    return {};
}

std::optional<std::string> lang_check(Language& l, std::string_view drawn, const std::source_location& where) {
    if (!l.pseudo || lang_marked(drawn)) return std::nullopt;
    if (!l.data.empty()) {  // names and other data (lang_data) are left out, the longest first
        std::vector<const std::string*> data;
        for (const std::string& d : l.data) data.push_back(&d);
        std::sort(data.begin(), data.end(),
                  [](const std::string* a, const std::string* b) { return a->size() > b->size(); });
        std::string rest(drawn);
        for (const std::string* d : data) {
            for (std::size_t at; !d->empty() && (at = rest.find(*d)) != std::string::npos;) rest.erase(at, d->size());
        }
        if (lang_marked(rest)) return std::nullopt;
    }
    std::string file = where.file_name();
    for (char& c : file) c = c == '\\' ? '/' : c;
    if (const std::size_t at = file.rfind("/engine/"); at != std::string::npos) file = file.substr(at + 8);
    // The function's name without its namespaces, return type and parameters.
    std::string fn = where.function_name();
    for (const char* anon : {"(anonymous namespace)::", "`anonymous-namespace'::"}) {  // clang, MSVC
        for (std::size_t at; (at = fn.find(anon)) != std::string::npos;) fn.erase(at, std::strlen(anon));
    }
    fn = fn.substr(0, fn.find('('));
    if (const std::size_t at = fn.find_last_of(": "); at != std::string::npos) fn = fn.substr(at + 1);
    const std::string site = file + ":" + std::to_string(where.line()) + " (" + fn + ")";
    if (!l.misses.insert(site + " " + std::string(drawn)).second) return std::nullopt;
    return "lang: not looked up at " + site + ": \"" + std::string(drawn) + "\"";
}

const std::string& lang_data(const Language* l, const std::string& text) {
    if (l && l->pseudo && !text.empty()) l->data.insert(text);
    return text;
}

}  // namespace antanta::ui
