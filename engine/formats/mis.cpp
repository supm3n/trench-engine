#include "mis.hpp"

#include <algorithm>
#include <bit>

namespace antanta {

namespace {

std::uint8_t u8_at(ByteSpan b, std::size_t o) { return b[o]; }
std::uint16_t u16_at(ByteSpan b, std::size_t o) { return read_u16(b, o); }
std::int32_t i32_at(ByteSpan b, std::size_t o) { return static_cast<std::int32_t>(read_u32(b, o)); }
float f32_at(ByteSpan b, std::size_t o) { return std::bit_cast<float>(read_u32(b, o)); }

// wchar[n] at an offset (it may be odd: a dialog's title), as far as the last nonzero character: the leftovers of an
// earlier, longer text after the first NUL stay, so that the field writes back as it was (see mis_text).
std::u16string wide_at(ByteSpan b, std::size_t o, std::size_t n) {
    std::u16string s;
    for (std::size_t i = 0; i < n; ++i) s += static_cast<char16_t>(u16_at(b, o + 2 * i));
    while (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

std::string narrow_at(ByteSpan b, std::size_t o, std::size_t n) {
    std::string s;
    for (std::size_t i = 0; i < n; ++i) s += static_cast<char>(b[o + i]);
    while (!s.empty() && s.back() == 0) s.pop_back();
    return s;
}

// The writers fill a buffer that is zero already.
void put_u8(std::uint8_t* out, std::size_t o, std::uint32_t v) { out[o] = static_cast<std::uint8_t>(v); }
void put_u16(std::uint8_t* out, std::size_t o, std::uint32_t v) { put_u8(out, o, v), put_u8(out, o + 1, v >> 8); }
void put_u32(std::uint8_t* out, std::size_t o, std::uint32_t v) { put_u16(out, o, v), put_u16(out, o + 2, v >> 16); }
void put_i32(std::uint8_t* out, std::size_t o, std::int32_t v) { put_u32(out, o, static_cast<std::uint32_t>(v)); }
void put_f32(std::uint8_t* out, std::size_t o, float v) { put_u32(out, o, std::bit_cast<std::uint32_t>(v)); }

void put_wide(std::uint8_t* out, std::size_t o, std::size_t n, const std::u16string& s) {
    for (std::size_t i = 0; i < n && i < s.size(); ++i) put_u16(out, o + 2 * i, s[i]);
}

void put_narrow(std::uint8_t* out, std::size_t o, std::size_t n, const std::string& s) {
    for (std::size_t i = 0; i < n && i < s.size(); ++i) put_u8(out, o + i, static_cast<std::uint8_t>(s[i]));
}

MisAction read_action(ByteSpan b, std::size_t o) {
    MisAction a;
    a.lead = static_cast<std::uint32_t>(i32_at(b, o));
    a.kind = static_cast<std::uint32_t>(i32_at(b, o + 4));
    for (std::size_t i = 0; i < 5; ++i) {
        a.ints[i] = i32_at(b, o + 8 + 4 * i);
        a.floats[i] = f32_at(b, o + 0x1c + 4 * i);
    }
    a.player = u16_at(b, o + 0x30);
    return a;
}

void write_action(const MisAction& a, std::uint8_t* out, std::size_t o) {
    put_u32(out, o, a.lead);
    put_u32(out, o + 4, a.kind);
    for (std::size_t i = 0; i < 5; ++i) {
        put_i32(out, o + 8 + 4 * i, a.ints[i]);
        put_f32(out, o + 0x1c + 4 * i, a.floats[i]);
    }
    put_u16(out, o + 0x30, a.player);
}

constexpr std::size_t kDialogsAt = 0xc60, kDialogSize = 0x2c0, kActionAt = 0xc28;

MisDialog read_dialog(ByteSpan b, std::size_t o) {
    MisDialog d;
    d.on = u8_at(b, o) != 0;
    d.title = wide_at(b, o + 1, 40);
    d.text = wide_at(b, o + 0x52, 256);
    d.button1_picture = u8_at(b, o + 0x252);
    d.button1_next = u8_at(b, o + 0x253);
    d.button2_picture = u8_at(b, o + 0x288);
    d.button2_next = u8_at(b, o + 0x289);
    d.action = read_action(b, o + 0x28c);
    return d;
}

void write_dialog(const MisDialog& d, std::uint8_t* out, std::size_t o) {
    put_u8(out, o, d.on);
    put_wide(out, o + 1, 40, d.title);
    put_wide(out, o + 0x52, 256, d.text);
    put_u8(out, o + 0x252, d.button1_picture);
    put_u8(out, o + 0x253, d.button1_next);
    put_u8(out, o + 0x288, d.button2_picture);
    put_u8(out, o + 0x289, d.button2_next);
    write_action(d.action, out, o + 0x28c);
}

MisBlock read_block(ByteSpan b) {
    MisBlock k;
    k.done = u8_at(b, 0);
    k.text_b = wide_at(b, 2, 512);
    k.text_c = wide_at(b, 0x402, 512);
    k.text_uc = wide_at(b, 0x802, 512);
    k.id = u8_at(b, 0xc02);
    for (std::size_t i = 0; i < 5; ++i) k.prereq[i] = u8_at(b, 0xc03 + i);
    k.open_prereq = u8_at(b, 0xc08);
    k.condition = u16_at(b, 0xc0a);
    k.x = f32_at(b, 0xc0c);
    k.y = f32_at(b, 0xc10);
    k.unit_type = u16_at(b, 0xc14);
    k.house_type = u16_at(b, 0xc16);
    k.timer = i32_at(b, 0xc18);
    k.advance = u8_at(b, 0xc1c);
    k.stage = u8_at(b, 0xc1d);
    k.n = i32_at(b, 0xc20);
    k.win = u8_at(b, 0xc24);
    k.lose = u8_at(b, 0xc25);
    k.action = read_action(b, kActionAt);
    for (std::size_t i = 0; i < kMisDialogs; ++i) k.dialogs[i] = read_dialog(b, kDialogsAt + i * kDialogSize);
    k.countdown = i32_at(b, 0x27e0);
    return k;
}

void write_block(const MisBlock& k, std::uint8_t* out) {
    put_u8(out, 0, k.done);
    put_wide(out, 2, 512, k.text_b);
    put_wide(out, 0x402, 512, k.text_c);
    put_wide(out, 0x802, 512, k.text_uc);
    put_u8(out, 0xc02, k.id);
    for (std::size_t i = 0; i < 5; ++i) put_u8(out, 0xc03 + i, k.prereq[i]);
    put_u8(out, 0xc08, k.open_prereq);
    put_u16(out, 0xc0a, k.condition);
    put_f32(out, 0xc0c, k.x);
    put_f32(out, 0xc10, k.y);
    put_u16(out, 0xc14, k.unit_type);
    put_u16(out, 0xc16, k.house_type);
    put_i32(out, 0xc18, k.timer);
    put_u8(out, 0xc1c, k.advance);
    put_u8(out, 0xc1d, k.stage);
    put_i32(out, 0xc20, k.n);
    put_u8(out, 0xc24, k.win);
    put_u8(out, 0xc25, k.lose);
    write_action(k.action, out, kActionAt);
    for (std::size_t i = 0; i < kMisDialogs; ++i) write_dialog(k.dialogs[i], out, kDialogsAt + i * kDialogSize);
    put_i32(out, 0x27e0, k.countdown);
}

// The player table: block counts u16[8] at 0x926, AIType u8[8] at 0x936, five float[8] tables from 0x940 (gold, iron,
// food, oil, wood), team i8[8] at 0x9e0.
constexpr std::size_t kCountsAt = 0x926, kAiAt = 0x936, kResAt = 0x940, kTeamAt = 0x9e0;

}  // namespace

MisBlock parse_mis_block(ByteSpan bytes) { return read_block(bytes); }
void write_mis_block(const MisBlock& block, std::uint8_t* out) { write_block(block, out); }

std::u16string_view mis_text(const std::u16string& field) {
    return std::u16string_view(field).substr(0, std::min(field.find(u'\0'), field.size()));
}

std::string_view mis_name(const std::string& field) {
    return std::string_view(field).substr(0, std::min(field.find('\0'), field.size()));
}

MisInfo parse_mis_info(ByteSpan b) {
    MisInfo m;
    m.version = u16_at(b, 0);
    m.title = wide_at(b, 2, 80);
    m.picture = narrow_at(b, 0xa2, 80);
    m.field_f2 = u16_at(b, 0xf2);
    m.text = wide_at(b, 0xf4, 1000);
    m.field_8c4 = u16_at(b, 0x8c4);
    m.field_8c6 = u16_at(b, 0x8c6);
    m.prereq_count = u8_at(b, 0x8c8);
    for (std::size_t i = 0; i < m.prereq.size(); ++i) m.prereq[i] = u8_at(b, 0x8c9 + i);
    m.map = narrow_at(b, 0x8d3, 80);
    return m;
}

void write_mis_info(const MisInfo& m, std::uint8_t* out) {
    put_u16(out, 0, m.version);
    put_wide(out, 2, 80, m.title);
    put_narrow(out, 0xa2, 80, m.picture);
    put_u16(out, 0xf2, m.field_f2);
    put_wide(out, 0xf4, 1000, m.text);
    put_u16(out, 0x8c4, m.field_8c4);
    put_u16(out, 0x8c6, m.field_8c6);
    put_u8(out, 0x8c8, m.prereq_count);
    for (std::size_t i = 0; i < m.prereq.size(); ++i) put_u8(out, 0x8c9 + i, m.prereq[i]);
    put_narrow(out, 0x8d3, 80, m.map);
}

std::optional<MisFile> parse_mis(ByteSpan file, std::string* error) {
    if (file.size() < kMisHeaderSize) {
        *error = "mission file: " + std::to_string(file.size()) + " bytes, less than the 0xd08-byte header";
        return std::nullopt;
    }
    MisFile m;
    m.info = parse_mis_info(file.first(kMisInfoSize));
    m.player_count = file[0x924];
    if (m.player_count > kMisMaxPlayers) {
        *error = "mission file: " + std::to_string(m.player_count) + " players, the header holds 8";
        return std::nullopt;
    }
    std::size_t blocks = 0;
    std::array<std::uint16_t, kMisMaxPlayers> counts{};
    for (std::size_t p = 0; p < kMisMaxPlayers; ++p) {
        counts[p] = u16_at(file, kCountsAt + 2 * p);
        MisPlayer& pl = m.players[p];
        pl.ai_type = u8_at(file, kAiAt + p);
        pl.gold = f32_at(file, kResAt + 4 * p);
        pl.iron = f32_at(file, kResAt + 0x20 + 4 * p);
        pl.food = f32_at(file, kResAt + 0x40 + 4 * p);
        pl.oil = f32_at(file, kResAt + 0x60 + 4 * p);
        pl.wood = f32_at(file, kResAt + 0x80 + 4 * p);
        pl.team = static_cast<std::int8_t>(file[kTeamAt + p]);
        if (p < m.player_count) blocks += counts[p];
    }
    const std::size_t total = kMisHeaderSize + blocks * kMisBlockSize;
    if (file.size() != total && file.size() != total + 1) {
        *error = "mission file: " + std::to_string(file.size()) + " bytes, the header says " + std::to_string(total) +
                 " (or one more)";
        return std::nullopt;
    }
    std::size_t at = kMisHeaderSize;
    for (std::size_t p = 0; p < m.player_count; ++p) {
        for (std::size_t k = 0; k < counts[p]; ++k, at += kMisBlockSize)
            m.players[p].blocks.push_back(read_block(file.subspan(at, kMisBlockSize)));
    }
    if (file.size() == total + 1) m.my_player = file[total];
    const Bytes again = write_mis(m);
    for (std::size_t i = 0; i < file.size(); ++i) m.stray_bytes += again[i] != file[i];
    return m;
}

Bytes write_mis(const MisFile& m) {
    std::size_t blocks = 0;
    for (std::size_t p = 0; p < m.player_count; ++p) blocks += m.players[p].blocks.size();
    Bytes out(kMisHeaderSize + blocks * kMisBlockSize + (m.my_player ? 1 : 0), 0);
    std::uint8_t* o = out.data();
    write_mis_info(m.info, o);
    put_u8(o, 0x924, m.player_count);
    std::size_t at = kMisHeaderSize;
    for (std::size_t p = 0; p < kMisMaxPlayers; ++p) {
        const MisPlayer& pl = m.players[p];
        put_u16(o, kCountsAt + 2 * p, static_cast<std::uint32_t>(pl.blocks.size()));
        put_u8(o, kAiAt + p, pl.ai_type);
        put_f32(o, kResAt + 4 * p, pl.gold);
        put_f32(o, kResAt + 0x20 + 4 * p, pl.iron);
        put_f32(o, kResAt + 0x40 + 4 * p, pl.food);
        put_f32(o, kResAt + 0x60 + 4 * p, pl.oil);
        put_f32(o, kResAt + 0x80 + 4 * p, pl.wood);
        put_u8(o, kTeamAt + p, static_cast<std::uint8_t>(pl.team));
        if (p >= m.player_count) continue;
        for (const MisBlock& k : pl.blocks) {
            write_block(k, o + at);
            at += kMisBlockSize;
        }
    }
    if (m.my_player) put_u8(o, at, *m.my_player);
    return out;
}

}  // namespace antanta
