#include "params.hpp"

#include <bit>
#include <cstring>

namespace antanta {
namespace {

constexpr std::size_t kHouseRecord = 0x2EC;
constexpr std::size_t kUnitRecord = 0x9C0;

// C string at [pos, pos + n), "\\" collapsed to "\" as tools/render_proto.py cstr does.
std::string record_path(ByteSpan r, std::size_t pos, std::size_t n) {
    std::string s;
    for (std::size_t i = pos; i < pos + n && r[i] != 0; ++i) s.push_back(static_cast<char>(r[i]));
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        out.push_back(s[i]);
        if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '\\') ++i;
    }
    return out;
}

}  // namespace

std::optional<BasicParams> parse_basic_params(ByteSpan file, std::string* error) {
    Cursor c{file};
    const ByteSpan magic = c.take(4);
    if (!c.ok || std::memcmp(magic.data(), "~xg1", 4) != 0) {
        *error = "BasicParams magic ~xg1 missing";
        return std::nullopt;
    }
    const int n_houses = c.u16(), n_units = c.u16();
    BasicParams p;
    for (int i = 0; i < n_houses; ++i) {
        const ByteSpan r = c.take(kHouseRecord);
        if (!c.ok) {
            *error = "house record " + std::to_string(i) + " truncated";
            return std::nullopt;
        }
        HouseParams h;
        h.data_path = record_path(r, 0xc8, 0x12c - 0xc8);
        h.width = read_u16(r, 0x12c);
        h.height = read_u16(r, 0x12e);
        h.footprint_a = r[0x130];
        h.footprint_b = r[0x131];
        h.anchor_x = read_u16(r, 0x132);
        h.anchor_y = read_u16(r, 0x134);
        h.exit_x = static_cast<std::int16_t>(read_u16(r, 0x136));
        h.exit_y = static_cast<std::int16_t>(read_u16(r, 0x138));
        for (std::size_t k = 0; k < 5; ++k) h.upkeep[k] = std::bit_cast<float>(read_u32(r, 0x170 + 4 * k));
        h.power_use = std::bit_cast<float>(read_u32(r, 0x184));
        for (std::size_t k = 0; k < 10; ++k) h.produce[k] = read_u16(r, 0x148 + 2 * k);
        h.field_188 = r[0x188];
        h.field_189 = r[0x189];
        h.work = read_u16(r, 0x140);
        h.max_hp = read_u16(r, 0x144);
        h.capacity = read_u16(r, 0x146);
        h.point_count_b = r[0x21a];
        h.offset_x = static_cast<std::int16_t>(read_u16(r, 0x13a));
        h.offset_y = static_cast<std::int16_t>(read_u16(r, 0x13c));
        h.field_142 = read_u16(r, 0x142);
        h.field_1c8 = r[0x1c8];
        h.anim_count = r[0x18a];
        for (std::size_t k = 0; k < 10; ++k) {
            h.anim_kind[k] = read_u16(r, 0x18c + 2 * k);
            h.anim_x[k] = read_u16(r, 0x1a0 + 2 * k);
            h.anim_y[k] = read_u16(r, 0x1b4 + 2 * k);
        }
        h.fire_y0 = read_u16(r, 0x1de);
        for (std::size_t k = 0; k < 10; ++k) {
            h.fire_x[k] = read_u16(r, 0x1ca + 2 * k);
            h.fire_y[k] = read_u16(r, 0x1de + 2 * k);
            h.flash_x[k] = read_u16(r, 0x21c + 2 * k);
            h.flash_y[k] = read_u16(r, 0x230 + 2 * k);
            for (std::size_t l = 0; l < 4; ++l) h.fire_level[k][l] = r[0x1f2 + 4 * k + l];
        }
        h.field_2c0 = r[0x2c0];
        h.field_2c1 = r[0x2c1];
        h.hit_count = r[0x2c2];
        for (std::size_t k = 0; k < 10; ++k) {
            h.hit_x[k] = read_u16(r, 0x2c4 + 2 * k);
            h.hit_y[k] = read_u16(r, 0x2d8 + 2 * k);
        }
        for (std::size_t k = 0; k < 5; ++k) h.cost[k] = read_u32(r, 0x15c + 4 * k);
        for (std::size_t k = 0; k < 10; ++k) h.prereq[k] = read_u16(r, 0x2a8 + 2 * k);
        h.field_2bc = read_u32(r, 0x2bc);
        p.houses.push_back(std::move(h));
    }
    for (int i = 0; i < n_units; ++i) {
        const ByteSpan r = c.take(kUnitRecord);
        if (!c.ok) {
            *error = "unit record " + std::to_string(i) + " truncated";
            return std::nullopt;
        }
        UnitParams u;
        u.data_path = record_path(r, 0xc8, 0x12c - 0xc8);
        u.move_class = r[0x12c];
        u.width = read_u16(r, 0x12e);
        u.height = read_u16(r, 0x130);
        u.anchor_x = static_cast<std::int16_t>(read_u16(r, 0x264));
        u.anchor_y = static_cast<std::int16_t>(read_u16(r, 0x266));
        auto f32 = [&r](std::size_t pos) { return std::bit_cast<float>(read_u32(r, pos)); };
        u.speed = f32(0x134);
        for (std::size_t k = 0; k < 6; ++k) u.upkeep[k] = f32(0x18c + 4 * k), u.cost[k] = read_u32(r, 0x174 + 4 * k);
        u.build_time = read_u16(r, 0x1a4);
        u.field_170 = read_u16(r, 0x170), u.field_1bc = read_u16(r, 0x1bc);
        u.turn_degrees = f32(0x1c0);
        for (std::size_t k = 0; k < 30; ++k) u.anim_speed[k] = f32(0x1c4 + 4 * k), u.anim_size[k] = r[0x23c + k];
        u.size = static_cast<std::int16_t>(read_u16(r, 0x788));
        u.field_786 = read_u16(r, 0x786);
        for (std::size_t k = 0; k < 10; ++k) {
            const std::size_t at = 0x268 + 0x83 * k;
            u.sounds[k].name = record_path(r, at, 0x80);
            u.sounds[k].loop = r[at + 0x80], u.sounds[k].positioned = r[at + 0x81], u.sounds[k].count = r[at + 0x82];
        }
        u.accuracy = read_u16(r, 0x168), u.range = read_u16(r, 0x16a), u.reload = read_u16(r, 0x16c);
        u.max_hp = read_u16(r, 0x16e);
        for (std::size_t k = 0; k < 10; ++k)
            u.attack[k] = read_u16(r, 0x140 + 2 * k), u.defense[k] = read_u16(r, 0x154 + 2 * k);
        const bool ship = u.move_class >= 13 && u.move_class <= 15;
        for (int wi = 0; wi < r[0x138]; ++wi) {
            std::size_t n = 0;
            WeaponParams w;
            if (!ship) {
                const ByteSpan part0 = c.take(43);
                if (!c.ok) break;
                w.field_48 = part0[41];
                n = part0[42];
                const ByteSpan pts = c.take(12 * n);
                if (!c.ok) break;
                for (std::size_t k = 0; k < 3 && n > 0; ++k) w.point_a[k] = std::bit_cast<float>(read_u32(pts, 4 * k));
            }
            const ByteSpan pts_b = c.take(12 * n);
            if (!c.ok) break;
            for (std::size_t k = 0; k < 3 && n > 0; ++k) w.point_b[k] = std::bit_cast<float>(read_u32(pts_b, 4 * k));
            const ByteSpan part1 = c.take(15);
            if (!c.ok) break;
            w.attack = read_u16(part1, 0), w.reload = read_u16(part1, 2), w.field_3e = read_u16(part1, 4);
            w.fire_range = std::bit_cast<float>(read_u32(part1, 6));
            w.min_range = std::bit_cast<float>(read_u32(part1, 10));
            w.kind = part1[14];
            u.weapons.push_back(w);
        }
        if (!c.ok) {
            *error = "weapon blocks of unit record " + std::to_string(i) + " truncated";
            return std::nullopt;
        }
        p.units.push_back(std::move(u));
    }
    return p;
}

std::optional<std::vector<std::array<std::uint8_t, 3>>> parse_player_colours(ByteSpan file, std::string* error) {
    std::vector<std::array<std::uint8_t, 3>> out;
    std::size_t pos = 0;
    while (pos < file.size()) {
        std::size_t end = pos;
        while (end < file.size() && file[end] != '\n') ++end;
        std::vector<int> nums;
        int v = -1;
        for (std::size_t i = pos; i <= end; ++i) {
            const int ch = i < end ? file[i] : ' ';
            if (ch >= '0' && ch <= '9') {
                v = (v < 0 ? 0 : v) * 10 + (ch - '0');
                if (v > 255) {
                    *error = "Colors.txt: value above 255";
                    return std::nullopt;
                }
            } else if (v >= 0) {
                nums.push_back(v);
                v = -1;
            }
        }
        if (!nums.empty()) {
            if (nums.size() < 3) {
                *error = "Colors.txt: line with fewer than three numbers";
                return std::nullopt;
            }
            out.push_back({static_cast<std::uint8_t>(nums[0]), static_cast<std::uint8_t>(nums[1]),
                           static_cast<std::uint8_t>(nums[2])});
        }
        pos = end + 1;
    }
    return out;
}

std::string unit_data_path_stand_in(int nation, int type, const std::string& path) {
    if (nation != 0 || type != 19 || path.size() != 19) return {};
    for (std::size_t i = 0; i < path.size(); ++i) {  // "Antanta\\Ship_Goeben", ASCII, any case
        const char c = path[i] >= 'A' && path[i] <= 'Z' ? static_cast<char>(path[i] - 'A' + 'a') : path[i];
        if (c != "antanta\\ship_goeben"[i]) return {};
    }
    return "Russia\\Ship_Rurik";
}

}  // namespace antanta
