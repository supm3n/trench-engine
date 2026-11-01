#include "campaign.hpp"

#include <algorithm>
#include <charconv>

namespace antanta {

namespace {

constexpr std::size_t kCmpHeaderSize = 0x8c4, kCmpNameSize = 0x50;

std::string upper_ascii(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return out;
}

// The words (separated by white space) or the lines (CRLF, non-empty ones) of a text file, and the text between them:
// gaps[i] comes before word i, the last one after the last word.
struct Split {
    std::vector<std::string> words, gaps;
};

Split split_text(ByteSpan file, bool by_line) {
    const auto text = std::string_view(reinterpret_cast<const char*>(file.data()), file.size());
    auto is_gap = [by_line](char c) {
        return by_line ? c == '\r' || c == '\n' : c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    Split s;
    std::size_t at = 0;
    while (true) {
        const std::size_t start = at;
        while (at < text.size() && is_gap(text[at])) ++at;
        s.gaps.emplace_back(text.substr(start, at - start));
        if (at == text.size()) break;
        const std::size_t word = at;
        while (at < text.size() && !is_gap(text[at])) ++at;
        s.words.emplace_back(text.substr(word, at - word));
    }
    return s;
}

Bytes join_text(const std::vector<std::string>& words, std::vector<std::string> gaps,
                const std::vector<std::string>& usual) {
    if (gaps.size() != words.size() + 1) gaps = usual;
    std::string text;
    for (std::size_t i = 0; i < words.size(); ++i) text += gaps[i] + words[i];
    text += gaps.back();
    return Bytes(text.begin(), text.end());
}

bool to_int(std::string_view w, std::int32_t& out) {
    const auto r = std::from_chars(w.data(), w.data() + w.size(), out);
    return r.ec == std::errc() && r.ptr == w.data() + w.size();
}

}  // namespace

std::optional<CmpFile> parse_cmp(ByteSpan file, std::string* error) {
    if (file.size() < kCmpHeaderSize) {
        *error = "campaign file: " + std::to_string(file.size()) + " bytes, less than the 0x8c4-byte header";
        return std::nullopt;
    }
    Bytes head(kMisInfoSize, 0);  // the header has the layout of a mission record up to the briefing
    std::copy_n(file.begin(), kCmpHeaderSize, head.begin());
    CmpFile c;
    c.header = parse_mis_info(head);
    const std::size_t count = c.header.field_f2;
    const std::size_t total = kCmpHeaderSize + count * (kCmpNameSize + kMisInfoSize);
    if (file.size() != total) {
        *error = "campaign file: " + std::to_string(file.size()) + " bytes, the header's " + std::to_string(count) +
                 " missions need " + std::to_string(total);
        return std::nullopt;
    }
    const std::size_t records = kCmpHeaderSize + count * kCmpNameSize;
    for (std::size_t i = 0; i < count; ++i) {
        std::string name;  // the whole field up to its last nonzero byte, as MisInfo's strings
        for (std::size_t k = 0; k < kCmpNameSize; ++k)
            name += static_cast<char>(file[kCmpHeaderSize + i * kCmpNameSize + k]);
        while (!name.empty() && name.back() == 0) name.pop_back();
        c.file_names.push_back(std::move(name));
        c.missions.push_back(parse_mis_info(file.subspan(records + i * kMisInfoSize, kMisInfoSize)));
    }
    const Bytes again = write_cmp(c);
    for (std::size_t i = 0; i < file.size(); ++i) c.stray_bytes += again[i] != file[i];
    return c;
}

Bytes write_cmp(const CmpFile& c) {
    const std::size_t count = c.missions.size();
    Bytes out(kCmpHeaderSize + count * (kCmpNameSize + kMisInfoSize), 0);
    Bytes head(kMisInfoSize, 0);
    MisInfo info = c.header;
    info.field_f2 = static_cast<std::uint16_t>(count);
    write_mis_info(info, head.data());
    std::copy_n(head.begin(), kCmpHeaderSize, out.begin());
    for (std::size_t i = 0; i < count; ++i) {
        if (i < c.file_names.size()) {
            const std::string& n = c.file_names[i];
            for (std::size_t k = 0; k < kCmpNameSize && k < n.size(); ++k)
                out[kCmpHeaderSize + i * kCmpNameSize + k] = static_cast<std::uint8_t>(n[k]);
        }
        write_mis_info(c.missions[i], out.data() + kCmpHeaderSize + count * kCmpNameSize + i * kMisInfoSize);
    }
    return out;
}

std::optional<CmtFile> parse_cmt(ByteSpan file, std::string* error) {
    Split s = split_text(file, true);
    if (s.words.size() < 3) {
        *error = "campaign list: fewer than the three header lines";
        return std::nullopt;
    }
    CmtFile c;
    c.title_key = s.words[0], c.info_key = s.words[1], c.picture = s.words[2];
    c.missions.assign(s.words.begin() + 3, s.words.end());
    c.gaps = std::move(s.gaps);
    return c;
}

Bytes write_cmt(const CmtFile& c) {
    std::vector<std::string> words{c.title_key, c.info_key, c.picture};
    words.insert(words.end(), c.missions.begin(), c.missions.end());
    std::vector<std::string> usual(words.size() + 1, "\r\n");
    usual[0].clear();
    return join_text(words, c.gaps, usual);
}

namespace {

// The words of a .mit and the usual gap before each: one tab after MyPlayer and PlayersQuantity, two after the other
// labels.
struct MitWords {
    std::vector<std::string> words, usual;
    void label(const char* name) {
        usual.push_back(words.empty() ? "" : "\r\n");
        words.push_back(name);
    }
    void value(std::string text, bool two_tabs = true) {
        usual.push_back(two_tabs ? "\t\t" : "\t");
        words.push_back(std::move(text));
    }
};

}  // namespace

std::optional<MitFile> parse_mit(ByteSpan file, std::string* error) {
    const Split s = split_text(file, false);
    std::size_t at = 0;
    auto label = [&](const char* name) {
        if (at < s.words.size() && iequals_ascii(s.words[at], name)) return ++at, true;
        *error = std::string("mission setup: \"") + name + "\" expected at word " + std::to_string(at);
        return false;
    };
    auto text = [&](const char* name, std::string& into) {
        if (!label(name)) return false;
        if (at >= s.words.size()) return *error = std::string("mission setup: no value after ") + name, false;
        into = s.words[at++];
        return true;
    };
    auto number = [&](const char* name, std::int32_t& into) {
        if (!label(name)) return false;
        if (at >= s.words.size() || !to_int(s.words[at], into))
            return *error = std::string("mission setup: no number after ") + name, false;
        ++at;
        return true;
    };
    MitFile m;
    std::int32_t count = 0;
    if (!text("Title", m.title_key) || !text("Info", m.info_key) || !text("Map", m.map) ||
        !number("MyPlayer", m.my_player) || !number("PlayersQuantity", count))
        return std::nullopt;
    if (count < 0 || count > static_cast<std::int32_t>(kMisMaxPlayers)) {
        *error = "mission setup: " + std::to_string(count) + " players";
        return std::nullopt;
    }
    for (std::int32_t p = 0; p < count; ++p) {
        MitPlayer pl;
        if (!number("AIType", pl.ai_type) || !number("Team", pl.team) || !number("Gold", pl.gold) ||
            !number("Iron", pl.iron) || !number("Wood", pl.wood) || !number("Food", pl.food) || !number("Oil", pl.oil))
            return std::nullopt;
        m.players.push_back(pl);
    }
    if (at != s.words.size()) {
        *error = "mission setup: words left after the last player";
        return std::nullopt;
    }
    m.gaps = s.gaps;
    return m;
}

Bytes write_mit(const MitFile& m) {
    MitWords w;
    w.label("Title"), w.value(m.title_key);
    w.label("Info"), w.value(m.info_key);
    w.label("Map"), w.value(m.map);
    w.label("MyPlayer"), w.value(std::to_string(m.my_player), false);
    w.label("PlayersQuantity"), w.value(std::to_string(m.players.size()), false);
    for (const MitPlayer& p : m.players) {
        w.label("AIType"), w.value(std::to_string(p.ai_type));
        w.label("Team"), w.value(std::to_string(p.team));
        w.label("Gold"), w.value(std::to_string(p.gold));
        w.label("Iron"), w.value(std::to_string(p.iron));
        w.label("Wood"), w.value(std::to_string(p.wood));
        w.label("Food"), w.value(std::to_string(p.food));
        w.label("Oil"), w.value(std::to_string(p.oil));
    }
    // The usual layout: each label on its own line, so the gap before a label is a line break, before a value tabs.
    w.usual.push_back("\r\n");
    return join_text(w.words, m.gaps, w.usual);
}

std::string mission_text_key(std::string_view folder, std::string_view stem) {
    return "IDS_CAMPAIGN_INFO_" + upper_ascii(folder) + "_" + upper_ascii(stem);
}

std::string mission_block_key(std::string_view folder, std::string_view stem, unsigned player, unsigned block,
                              std::string_view kind) {
    return mission_text_key(folder, stem) + "_N" + std::to_string(player) + "_M" + std::to_string(block) + "_" +
           std::string(kind);
}

const TextRecord* find_text(const TextTable& table, std::string_view key) {
    const auto it = table.find(upper_ascii(key));
    return it == table.end() ? nullptr : &it->second;
}

}  // namespace antanta
