#include "map.hpp"

#include <bit>
#include <cstring>

namespace antanta {
namespace {

constexpr std::size_t kCellSize = 25;
constexpr std::size_t kHouseSize = 0x84;
constexpr std::size_t kUnitSize = 0x70;
constexpr std::size_t kDecorationSize = 80;
constexpr std::size_t kNameSize = 44;
constexpr int kMaxPlayers = 8;
constexpr int kMaxExtraUnits = 64;  // resync window, as tools/map_dump.py

MapHouse read_house(ByteSpan r, int player) {
    MapHouse h;
    h.player = player;
    h.cell_x = static_cast<std::int32_t>(read_u32(r, 0));
    h.cell_y = static_cast<std::int32_t>(read_u32(r, 4));
    h.type = r[8];
    h.build_state = read_u16(r, 0x0a);
    h.hp = std::bit_cast<float>(read_u32(r, 0x0c));
    h.field_1c = r[0x1c];
    h.port_side = read_u32(r, 0x20);
    return h;
}

MapUnit read_unit(ByteSpan r, int player) {
    MapUnit u;
    u.player = player;
    u.world_x = std::bit_cast<float>(read_u32(r, 0));
    u.world_y = std::bit_cast<float>(read_u32(r, 4));
    u.type = read_u16(r, 8);
    u.orient_plus1 = r[0x0a];
    return u;
}

// Tail sections A to E (formats.md "Tail"). Succeeds only if the parse ends exactly at the end of the data.
bool parse_tail(ByteSpan data, std::size_t pos, MapData* m) {
    Cursor c{data, pos};
    const int n_deco = c.u16();
    for (int i = 0; i < n_deco && c.ok; ++i) {
        const ByteSpan rec = c.take(kDecorationSize);
        if (!c.ok) break;
        m->decorations.push_back(parse_decoration(rec));
    }
    const std::size_t grid_count = static_cast<std::size_t>(2 * m->width) * static_cast<std::size_t>(m->height);
    const ByteSpan grid = c.take(grid_count * 2);
    if (!c.ok) return false;
    m->obstacle_grid.resize(grid_count);
    for (std::size_t i = 0; i < grid_count; ++i) m->obstacle_grid[i] = read_u16(grid, i * 2);

    const int n_routes = c.u16();
    for (int i = 0; i < n_routes && c.ok; ++i) {
        MapRoute route;
        const int lists = c.u8();
        route.v = c.u16();
        for (int j = 0; j < lists && c.ok; ++j) {
            const int len = c.u8();
            std::vector<std::uint16_t> list;
            for (int k = 0; k < len && c.ok; ++k) list.push_back(c.u16());
            route.lists.push_back(std::move(list));
        }
        m->routes.push_back(std::move(route));
    }

    const int n_neutral = c.u16();
    for (int i = 0; i < n_neutral && c.ok; ++i) {
        const ByteSpan r = c.take(kHouseSize);
        if (c.ok) m->neutral_houses.push_back(read_house(r, kMaxPlayers));
    }
    m->ignored_unit_records = c.u16();
    c.skip(kUnitSize * m->ignored_unit_records);

    m->special_capacity = c.u16();
    const int n_special = c.u16();
    for (int i = 0; i < n_special && c.ok; ++i) {
        MapSpecial s;
        s.kind = c.u8();
        s.x = c.u16();
        s.y = c.u16();
        s.f_a = c.f32();
        s.f_b = c.f32();
        s.b = c.u8();
        m->specials.push_back(s);
    }
    // The loader guards this read with "cursor < size - 1"; older maps stop before it.
    if (c.ok && c.pos + 1 < data.size()) {
        const int n_ports = c.u16();
        for (int i = 0; i < n_ports && c.ok; ++i) m->port_sites.push_back(c.u32());
    }
    return c.ok && c.pos == data.size();
}

}  // namespace

MapDecoration parse_decoration(ByteSpan record) {
    Cursor r{record};
    MapDecoration d;
    d.type = r.u8();
    d.f01 = r.u16();
    d.f03 = r.f32();
    d.f07 = r.u8();
    d.f08 = r.f32();
    d.f0c = r.f32();
    d.f10 = r.u16();
    d.f12 = r.u16();
    d.f14 = r.u16();
    d.f16 = r.u8();
    d.f17 = r.u8();
    d.world_x = r.f32();
    d.world_y = r.f32();
    const ByteSpan rest = r.take(d.rest.size());
    if (r.ok) std::memcpy(d.rest.data(), rest.data(), d.rest.size());
    return d;
}

std::optional<Bytes> unpack_pack(ByteSpan file, std::string* error) {
    if (file.size() < 8) {
        *error = "packed file shorter than its 8-byte header";
        return std::nullopt;
    }
    const std::uint32_t csize = read_u32(file, 0);
    const std::uint32_t usize = read_u32(file, 4);
    if (std::uint64_t{csize} + 8 != file.size()) {
        *error = "compressed size " + std::to_string(csize) + " does not match the file size";
        return std::nullopt;
    }
    return inflate_exact(file.subspan(8), usize, error);
}

std::optional<Minimap> parse_minimap(ByteSpan file, std::string* error) {
    auto data = unpack_pack(file, error);
    if (!data) return std::nullopt;
    constexpr std::size_t kBytes = std::size_t{Minimap::kSide} * Minimap::kSide * 2;
    if (data->size() != kBytes) {
        *error = "minimap is " + std::to_string(data->size()) + " bytes, expected 80000";
        return std::nullopt;
    }
    Minimap m;
    m.rgb565.resize(kBytes / 2);
    for (std::size_t i = 0; i < m.rgb565.size(); ++i) m.rgb565[i] = read_u16(*data, i * 2);
    return m;
}

std::optional<Minimap> parse_svg_preview(ByteSpan file, std::string* error) {
    auto data = unpack_pack(file, error);
    if (!data) return std::nullopt;
    constexpr std::size_t kBytes = std::size_t{Minimap::kSide} * Minimap::kSide * 2;
    std::size_t pos = 0;
    while (pos + 6 <= data->size()) {
        const std::uint16_t tag = read_u16(*data, pos);
        const std::uint32_t size = read_u32(*data, pos + 2);
        pos += 6;
        if (size > data->size() - pos) break;
        if (tag == 5 && size == kBytes) {
            Minimap m;
            m.rgb565.resize(kBytes / 2);
            for (std::size_t i = 0; i < m.rgb565.size(); ++i) m.rgb565[i] = read_u16(*data, pos + i * 2);
            return m;
        }
        pos += size;
    }
    *error = "the save has no preview picture";
    return std::nullopt;
}

Bytes minimap_to_rgb(const Minimap& m) {
    Bytes out(m.rgb565.size() * 3);
    for (std::size_t i = 0; i < m.rgb565.size(); ++i) {
        const unsigned v = m.rgb565[i];
        out[i * 3 + 0] = static_cast<std::uint8_t>((v >> 11 & 31) * 255 / 31);
        out[i * 3 + 1] = static_cast<std::uint8_t>((v >> 5 & 63) * 255 / 63);
        out[i * 3 + 2] = static_cast<std::uint8_t>((v & 31) * 255 / 31);
    }
    return out;
}

std::optional<MapData> parse_map(ByteSpan file, std::string* error) {
    auto data = unpack_pack(file, error);
    if (!data) return std::nullopt;
    return parse_map_stream(*data, error);
}

std::optional<MapData> parse_map_stream(ByteSpan data, std::string* error) {
    Cursor c{data};
    const ByteSpan magic = c.take(4);
    if (!c.ok || std::memcmp(magic.data(), "~AMp", 4) != 0) {
        *error = "map magic ~AMp missing";
        return std::nullopt;
    }
    MapData m;
    m.width = c.u16();
    m.height = c.u16();
    const std::size_t n_cells = static_cast<std::size_t>(m.width) * static_cast<std::size_t>(m.height);
    const ByteSpan cells = c.take(n_cells * kCellSize);
    if (!c.ok) {
        *error = "cell array runs past the end of the map";
        return std::nullopt;
    }
    m.cells.resize(n_cells);
    for (std::size_t i = 0; i < n_cells; ++i) {
        const ByteSpan r = cells.subspan(i * kCellSize, kCellSize);
        MapCell& cell = m.cells[i];
        cell.height = r[0];
        cell.tile = read_u16(r, 1);
        cell.shade = r[3];
        cell.unknown4 = r[4];
        for (std::size_t k = 0; k < cell.subtile.size(); ++k) cell.subtile[k] = read_u16(r, 5 + k * 2);
        cell.object = read_u16(r, 23);
    }

    const int players = c.u16();
    if (players > kMaxPlayers) {
        *error = "map declares " + std::to_string(players) + " players, at most 8 are possible";
        return std::nullopt;
    }
    for (int p = 0; p < players; ++p) {
        const ByteSpan r = c.take(kNameSize);
        if (!c.ok) break;
        m.player_names.push_back(utf16le_to_utf8(r));
        m.player_nations.push_back(r[0x28]);
        m.player_bytes29.push_back(r[0x29]);
    }
    for (std::uint16_t& n : m.house_counts) n = c.u16();
    // As tools/map_dump.py: only the first `players` counts are read; the others are 0 in every map.
    for (int p = 0; p < players && c.ok; ++p) {
        for (int i = 0; i < m.house_counts[p] && c.ok; ++i) {
            const ByteSpan r = c.take(kHouseSize);
            if (c.ok) m.houses.push_back(read_house(r, p));
        }
    }
    for (std::uint16_t& n : m.unit_counts) n = c.u16();
    const std::size_t units_start = c.pos;
    for (int p = 0; p < players && c.ok; ++p) {
        for (int i = 0; i < m.unit_counts[p] && c.ok; ++i) {
            const ByteSpan r = c.take(kUnitSize);
            if (c.ok) m.units.push_back(read_unit(r, p));
        }
    }
    if (!c.ok) {
        *error = "house or unit records run past the end of the map";
        return std::nullopt;
    }

    // Brit_3M_3 stores more unit records than its header declares: try 0..63 extra records until the tail
    // ends exactly at the end of the data (tools/map_dump.py read_dat). The extras are units (a deliberate
    // difference from the original, which skips them and reads them as the start of its tail; docs/bugs.md, data
    // defects). Records are stored in player order and the header's slip is in the first count (Brit_3M_3: 5 where
    // 11 are stored), so the extras go to the first player: 11 / 10 there. No other map has extras.
    for (int extra = 0; extra < kMaxExtraUnits; ++extra) {
        const std::size_t tail = c.pos + kUnitSize * static_cast<std::size_t>(extra);
        if (tail > data.size()) break;
        MapData attempt = m;
        if (!parse_tail(data, tail, &attempt)) continue;
        attempt.extra_unit_records = extra;
        if (extra > 0 && players > 0) {
            attempt.unit_counts[0] = static_cast<std::uint16_t>(attempt.unit_counts[0] + extra);
            attempt.units.clear();
            Cursor u{data};
            u.pos = units_start;
            for (int p = 0; p < players; ++p) {
                for (int i = 0; i < attempt.unit_counts[p]; ++i)
                    attempt.units.push_back(read_unit(u.take(kUnitSize), p));
            }
        }
        return attempt;
    }
    *error = "map tail does not end at the end of the data";
    return std::nullopt;
}

std::optional<LandTextures> parse_land_textures(ByteSpan file, std::string* error) {
    Cursor c{file};
    const int n = c.u16();
    const ByteSpan flags = c.take(std::size_t{3} * n);
    constexpr std::size_t kTexBytes = std::size_t{LandTextures::kSide} * LandTextures::kSide * 2;
    const ByteSpan pixels = c.take(kTexBytes * n);
    if (!c.ok) {
        *error = "LandTextures.dat truncated";
        return std::nullopt;
    }
    LandTextures t;
    for (int i = 0; i < n; ++i) {
        t.flags.push_back({flags[i * 3], flags[i * 3 + 1], flags[i * 3 + 2]});
        t.textures.push_back(
            decode_argb4444(pixels.subspan(kTexBytes * i, kTexBytes), LandTextures::kSide, LandTextures::kSide));
    }
    return t;
}

}  // namespace antanta
