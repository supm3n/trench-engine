#include "platform/release_check.hpp"

namespace antanta::release_check {

std::optional<std::vector<std::uint32_t>> parse_version(std::string_view v) {
    std::vector<std::uint32_t> parts;
    std::size_t i = 0;
    while (true) {
        const std::size_t start = i;
        std::uint64_t n = 0;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9' && i - start < 9)
            n = n * 10 + static_cast<unsigned>(v[i++] - '0');
        if (i == start) return std::nullopt;
        parts.push_back(static_cast<std::uint32_t>(n));
        if (i == v.size()) break;
        if (v[i] != '.') return std::nullopt;
        ++i;
    }
    if (parts.size() < 2 || parts.size() > 3) return std::nullopt;
    return parts;
}

bool version_newer(std::string_view candidate, std::string_view current) {
    auto a = parse_version(candidate), b = parse_version(current);
    if (!a || !b) return false;
    a->resize(3, 0), b->resize(3, 0);
    return *a > *b;
}

namespace {

// Just enough JSON (RFC 8259) to find two string members of the outer object: every value is checked and skipped,
// strings are decoded (\u escapes to UTF-8). Nesting deeper than kMaxDepth fails.
class Json {
  public:
    explicit Json(std::string_view t) : t_(t) {}

    // The outer object's string members named `a` and `b` (the last one wins if a name repeats).
    bool outer(std::string* a_value, std::string_view a, std::string* b_value, std::string_view b) {
        space();
        if (!eat('{')) return false;
        space();
        if (eat('}')) return end();
        while (true) {
            std::string name;
            space();
            if (!string(&name)) return false;
            space();
            if (!eat(':')) return false;
            space();
            std::string* into = name == a ? a_value : name == b ? b_value : nullptr;
            if (into && peek() == '"') {
                if (!string(into)) return false;
            } else if (!value(1)) {
                return false;
            }
            space();
            if (eat('}')) return end();
            if (!eat(',')) return false;
        }
    }

  private:
    static constexpr int kMaxDepth = 64;
    std::string_view t_;
    std::size_t i_ = 0;

    char peek() const { return i_ < t_.size() ? t_[i_] : '\0'; }
    bool eat(char c) {
        if (peek() != c || i_ >= t_.size()) return false;
        ++i_;
        return true;
    }
    void space() {
        while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\t' || t_[i_] == '\n' || t_[i_] == '\r')) ++i_;
    }
    bool end() {
        space();
        return i_ == t_.size();
    }
    bool word(std::string_view w) {
        if (t_.substr(i_, w.size()) != w) return false;
        i_ += w.size();
        return true;
    }
    static void utf8(std::uint32_t c, std::string* out) {
        if (c < 0x80) {
            out->push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out->push_back(static_cast<char>(0xc0 | (c >> 6)));
            out->push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else if (c < 0x10000) {
            out->push_back(static_cast<char>(0xe0 | (c >> 12)));
            out->push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
            out->push_back(static_cast<char>(0x80 | (c & 0x3f)));
        } else {
            out->push_back(static_cast<char>(0xf0 | (c >> 18)));
            out->push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3f)));
            out->push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3f)));
            out->push_back(static_cast<char>(0x80 | (c & 0x3f)));
        }
    }
    bool hex4(std::uint32_t* c) {
        if (t_.size() - i_ < 4) return false;
        *c = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = t_[i_++];
            const int d = h >= '0' && h <= '9'   ? h - '0'
                          : h >= 'a' && h <= 'f' ? h - 'a' + 10
                          : h >= 'A' && h <= 'F' ? h - 'A' + 10
                                                 : -1;
            if (d < 0) return false;
            *c = *c * 16 + static_cast<std::uint32_t>(d);
        }
        return true;
    }
    bool string(std::string* out) {
        if (!eat('"')) return false;
        out->clear();
        while (i_ < t_.size()) {
            const char c = t_[i_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') {
                out->push_back(c);
                continue;
            }
            if (i_ >= t_.size()) return false;
            const char e = t_[i_++];
            switch (e) {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'u': {
                    std::uint32_t u = 0;
                    if (!hex4(&u)) return false;
                    if (u >= 0xd800 && u < 0xdc00) {  // a surrogate pair
                        std::uint32_t low = 0;
                        if (!word("\\u") || !hex4(&low) || low < 0xdc00 || low >= 0xe000) return false;
                        u = 0x10000 + ((u - 0xd800) << 10) + (low - 0xdc00);
                    } else if (u >= 0xdc00 && u < 0xe000) {
                        return false;
                    }
                    utf8(u, out);
                    break;
                }
                default: return false;
            }
        }
        return false;
    }
    bool number() {
        eat('-');
        if (eat('0')) {
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') ++i_;
        } else {
            return false;
        }
        if (eat('.')) {
            if (!(peek() >= '0' && peek() <= '9')) return false;
            while (peek() >= '0' && peek() <= '9') ++i_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++i_;
            if (peek() == '+' || peek() == '-') ++i_;
            if (!(peek() >= '0' && peek() <= '9')) return false;
            while (peek() >= '0' && peek() <= '9') ++i_;
        }
        return true;
    }
    bool value(int depth) {
        if (depth > kMaxDepth) return false;
        space();
        std::string skip;
        switch (peek()) {
            case '"': return string(&skip);
            case 't': return word("true");
            case 'f': return word("false");
            case 'n': return word("null");
            case '[': {
                ++i_;
                space();
                if (eat(']')) return true;
                while (true) {
                    if (!value(depth + 1)) return false;
                    space();
                    if (eat(']')) return true;
                    if (!eat(',')) return false;
                }
            }
            case '{': {
                ++i_;
                space();
                if (eat('}')) return true;
                while (true) {
                    space();
                    if (!string(&skip)) return false;
                    space();
                    if (!eat(':') || !value(depth + 1)) return false;
                    space();
                    if (eat('}')) return true;
                    if (!eat(',')) return false;
                }
            }
            default: return number();
        }
    }
};

}  // namespace

std::optional<Release> read_release(std::string_view json, std::string* error) {
    std::string tag, url;
    if (!Json(json).outer(&tag, "tag_name", &url, "html_url")) {
        *error = "GitHub's answer is not a JSON object";
        return std::nullopt;
    }
    if (tag.empty() || url.empty()) {
        *error = "GitHub's answer has no tag_name or html_url";
        return std::nullopt;
    }
    Release r;
    r.version = tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag;
    if (!parse_version(r.version)) {
        *error = "the release's tag " + tag + " is not a version";
        return std::nullopt;
    }
    const std::string page = "https://github.com/" + std::string(kRepo) + "/releases/";
    bool plain = true;  // no blanks, control characters, quotes or angle brackets in what the browser gets
    for (const char c : url) plain = plain && static_cast<unsigned char>(c) > ' ' && c != '"' && c != '<' && c != '>';
    if (url.rfind(page, 0) != 0 || !plain) {
        *error = "the release's page " + url + " is not on " + page;
        return std::nullopt;
    }
    r.url = url;
    return r;
}

}  // namespace antanta::release_check
