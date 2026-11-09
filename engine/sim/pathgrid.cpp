



#include "formats/les.hpp"
#include "movement.hpp"

namespace antanta::sim {

namespace {

constexpr std::uint16_t kBlocked = 0xffff;

std::size_t Index(const Map& m, std::int32_t x, std::int32_t y) {
    return static_cast<std::size_t>(y) * m.width + static_cast<std::size_t>(x);
}


void CellWorld(const Map& m, std::uint16_t x, std::uint16_t y, std::int32_t* wx, std::int32_t* wy) {
    if (x < m.width && y < m.height) {
        *wx = (x * 2 - (~static_cast<std::int32_t>(y) & 1)) * 48;
        *wy = y * 24 - m.heights[Index(m, x, y)];
    } else {
        *wx = 0, *wy = 0;
    }
}


bool Obstacle(const Map& m, std::int32_t x, std::int32_t y) {
    const auto ux = static_cast<std::uint16_t>(x), uy = static_cast<std::uint16_t>(y);
    if (ux <= 2 || uy <= 2 || ux > m.width - 4 || uy > m.height - 4) return false;
    const Cell p = Cell_Step(Cell{ux, uy}, 2);
    const auto px = static_cast<std::uint16_t>(p.x), py = static_cast<std::uint16_t>(p.y);
    std::int32_t ax = 0, ay = 0, bx = 0, by = 0;
    CellWorld(m, px, py, &ax, &ay);
    if (ay < 0) return false;
    CellWorld(m, px, static_cast<std::uint16_t>(py - 2), &bx, &by);
    if (by < 0) return false;
    const std::uint32_t gx = static_cast<std::uint32_t>(ax) / 48;
    const std::uint32_t gy = static_cast<std::uint32_t>((ay + by) / 2) / 24;
    const std::size_t i = static_cast<std::size_t>(gy) * (2u * m.width) + gx;
    return i < m.obstacle.size() && m.obstacle[i] != 0;
}

std::uint8_t ObjectByte(const GameState& s, int table, std::size_t i) {
    const std::vector<std::uint8_t>& t = s.object_blocking[static_cast<std::size_t>(table)];
    return i < t.size() ? t[i] : 0xff;  // past the table: not blocking (the original reads past its end)
}


bool BlocksLand(const GameState& s, std::uint16_t o) {
    if (o <= 100) return false;
    if (o < 9000) return true;
    return o > 0x257f && o < 0x25de && ObjectByte(s, 2, o - 0x2580u) < 3;
}


bool BlocksLarge(const GameState& s, std::uint16_t o) {
    if (o <= 100) return false;
    if (o < 9000) return true;
    if (o < 0x251c) return ObjectByte(s, 0, o - 9000u) < 2;
    if (o < 0x2580) return ObjectByte(s, 1, o - 0x251cu) < 2;
    return false;
}



bool BridgeCellOpen(const GameState& s, std::uint16_t o, Cell c) {
    const std::size_t k = o - 0x65u;
    if (k >= s.map.neutral_houses.size()) return false;
    const NeutralHouse& h = s.map.neutral_houses[k];
    if (h.type > 1) return false;
    const Cell base{h.map_x, h.map_y}, back{h.map_x, h.map_y - 2};
    const std::uint8_t first = h.type == 0 ? 1 : 4, second = h.type == 0 ? 4 : 1;
    const Cell a = Cell_Step(base, first), b = Cell_Step(back, second);
    return (a.x == c.x && a.y == c.y) || (back.x == c.x && back.y == c.y) || (b.x == c.x && b.y == c.y);
}


void Cell_Compute(GameState& s, std::int32_t x, std::int32_t y, bool region) {
    Map& m = s.map;
    const std::size_t i = Index(m, x, y);
    const std::uint16_t o = m.objects[i];
    std::uint16_t& land = m.pass_land[i];
    std::uint16_t& large = m.pass_large[i];
    std::uint8_t& weight = m.weight[i];
    weight = 0;
    if (o == 99) {  // the strip in front of a type 0x13 house
        land = kBlocked;
        large = region && m.flags[i] == 2 ? kBlocked : 0;
        weight = 5;
    } else if (o >= 0x25e4 && o <= 10999) {  // special object: its own cell may stay open
        const std::size_t k = o - 0x25e4u;
        const SpecialObject rec = k < m.specials.size() ? m.specials[k] : SpecialObject{};
        land = rec.f_a < 0.0f && rec.x == x && rec.y == y ? 0 : kBlocked;
        large = kBlocked;
    } else if (o >= 0x65 && o <= 999) {  // neutral house
        land = large = BridgeCellOpen(s, o, Cell{x, y}) ? 0 : kBlocked;
    } else if (Obstacle(m, x, y)) {
        land = large = kBlocked;
        weight = 5;
    } else if (o == 0x2581 || o == 0x2582 || o == 0x2583) {  // deposits block their cell and three more for large
        land = large = kBlocked;
        weight = 5;
        const Cell n = Cell_Step(Cell{x, y}, 1);
        m.pass_large[Index(m, n.x, n.y)] = kBlocked;
        m.pass_large[Index(m, n.x, n.y + 2)] = kBlocked;
        m.pass_large[Index(m, n.x - 1, n.y + 2)] = kBlocked;
        m.pass_large[Index(m, n.x - 1, n.y)] = kBlocked;
    } else {
        // Height range over the cell's four corners: > 25 blocks large units, > 60 land units.
        const Cell n = Cell_Step(Cell{x, y}, 1);
        const int h[4] = {m.heights[i], m.heights[Index(m, n.x, n.y)], m.heights[i + 1],
                          m.heights[Index(m, n.x, n.y + 2)]};
        int lo = h[0], hi = h[0];
        for (int v : h) lo = v < lo ? v : lo, hi = v > hi ? v : hi;
        const int range = hi - lo;
        if (range > 25) large = kBlocked;
        if (range > 60) {
            land = kBlocked;
        } else if (land == 0 && BlocksLand(s, o)) {
            land = kBlocked;
        }
        if (BlocksLarge(s, o)) large = kBlocked;
        weight = static_cast<std::uint8_t>(range / 3 + (land != 0 ? 5 : 0));
    }
}

// The in-place 3x3 mean of the weight grid (cell and its 8 neighbours, /9) over y0 <= y < y1, x0 <= x < x1.
void Weight_Smooth(Map& m, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    for (std::int32_t y = y0; y < y1; ++y) {
        for (std::int32_t x = x0; x < x1; ++x) {
            unsigned sum = 0;
            for (std::uint8_t d = 0; d <= 8; ++d) {
                const Cell n = Cell_Step(Cell{x, y}, d);
                sum += m.weight[Index(m, n.x, n.y)];
            }
            m.weight[Index(m, x, y)] = static_cast<std::uint8_t>((sum & 0xffff) / 9);
        }
    }
}

bool Border(const Map& m, std::int32_t x, std::int32_t y) {
    return x < 4 || y < 10 || x >= m.width - 3 || y >= m.height - 9;
}


void ClampInterior(const Map& m, std::int32_t* x, std::int32_t* y) {
    if (*x < 4) *x = 4;
    if (*y < 10) *y = 10;
    if (*x >= m.width - 3) *x = m.width - 4;
    if (*y >= m.height - 9) *y = m.height - 10;
}

}  // namespace




bool Cell_LandStatic(const GameState& s, std::int32_t x, std::int32_t y, std::uint8_t* weight) {
    const Map& m = s.map;
    if (Border(m, x, y)) {
        *weight = 10;
        return false;
    }
    const std::size_t i = Index(m, x, y);
    const std::uint16_t o = m.objects[i];
    *weight = 0;
    if (o == 99) {
        *weight = 5;
        return false;
    }
    if (o >= 0x25e4 && o <= 10999) {
        const std::size_t k = o - 0x25e4u;
        const SpecialObject rec = k < m.specials.size() ? m.specials[k] : SpecialObject{};
        return rec.f_a < 0.0f && rec.x == x && rec.y == y;
    }
    if (o >= 0x65 && o <= 999) return BridgeCellOpen(s, o, Cell{x, y});
    if (Obstacle(m, x, y) || o == 0x2581 || o == 0x2582 || o == 0x2583) {
        *weight = 5;
        return false;
    }
    const Cell n = Cell_Step(Cell{x, y}, 1);
    const int h[4] = {m.heights[i], m.heights[Index(m, n.x, n.y)], m.heights[i + 1], m.heights[Index(m, n.x, n.y + 2)]};
    int lo = h[0], hi = h[0];
    for (int v : h) lo = v < lo ? v : lo, hi = v > hi ? v : hi;
    const int range = hi - lo;
    const bool open = range <= 60 && (m.flags[i] & 0xfd) == 0 && !BlocksLand(s, o);
    *weight = static_cast<std::uint8_t>(range / 3 + (open ? 0 : 5));
    return open;
}



bool Obstacle_UnderCell(const Map& m, std::int32_t x, std::int32_t y) {
    std::int32_t wx = 0, wy = 0;
    CellWorld(m, static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), &wx, &wy);
    const std::int32_t world_w = m.width * 0x60, world_h = m.height * 0x18;
    if (wx <= 0 || wy < 26 || wx >= world_w - 100 || wy >= world_h - 25) return false;
    const std::size_t g = static_cast<std::size_t>(wy / 24) * (2u * m.width) + static_cast<std::size_t>(wx / 48);
    return m.obstacle[g] != 0;
}


void PathGrid_Build(GameState& s) {
    Map& m = s.map;
    const std::size_t n = static_cast<std::size_t>(m.width) * m.height;
    ++m.pass_gen;
    m.pass_land.assign(n, 0);
    m.pass_large.assign(n, 0);
    m.weight.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) m.pass_land[i] = m.pass_large[i] = m.flags[i] != 0 ? kBlocked : 0;
    for (std::int32_t y = 0; y < m.height; ++y) {
        for (std::int32_t x = 0; x < m.width; ++x) {
            if (Border(m, x, y)) {
                m.pass_land[Index(m, x, y)] = m.pass_large[Index(m, x, y)] = kBlocked;
                m.weight[Index(m, x, y)] = 10;
            } else {
                Cell_Compute(s, x, y, false);
            }
        }
    }
    Weight_Smooth(m, 2, 4, static_cast<std::int16_t>(m.width - 4), static_cast<std::int16_t>(m.height - 4));
}


void PathGrid_UpdateRegion(GameState& s, std::int32_t x, std::int32_t y, std::int32_t r) {
    Map& m = s.map;
    std::int32_t x0 = x - r - 2, y0 = y - r - 2, x1 = x + r + 2, y1 = y + r + 2;
    ClampInterior(m, &x0, &y0);
    ClampInterior(m, &x1, &y1);
    ++m.pass_gen;
    for (std::int32_t cy = y0; cy < y1; ++cy) {
        for (std::int32_t cx = x0; cx < x1; ++cx) {
            const std::size_t i = Index(m, cx, cy);
            m.pass_land[i] = m.pass_large[i] = m.flags[i] != 0 ? kBlocked : 0;
        }
    }
    for (std::int32_t cy = y0; cy < y1; ++cy) {
        for (std::int32_t cx = x0; cx < x1; ++cx) Cell_Compute(s, cx, cy, true);
    }
    Weight_Smooth(m, x0, y0, x1, y1);
}



void Terrain_SetObject(GameState& s, std::int32_t x, std::int32_t y, std::uint16_t object) {
    Map& m = s.map;
    if (x >= 0 && y >= 0 && x < m.width && y < m.height) {
        m.objects[static_cast<std::size_t>(m.width) * static_cast<std::size_t>(y) + static_cast<std::size_t>(x)] =
            object;
    }
    PathGrid_UpdateRegion(s, x, y, 2);
}







void House_PlaceOnMap(GameState& s, std::uint8_t player, std::uint16_t slot, std::uint8_t nation, std::uint16_t type,
                      std::int32_t x, std::int32_t y, std::uint8_t strip_nation) {
    Map& m = s.map;
    const std::size_t n = static_cast<std::size_t>(m.width) * m.height;
    const auto id = static_cast<std::uint16_t>((player + 1) * 1000 + slot);
    HouseFootprint f = nation < 6 && type < 25 ? s.house_footprints[nation][type] : HouseFootprint{};
    auto at = [&](std::int32_t cx, std::int32_t cy) {
        return static_cast<std::uint32_t>(m.width) * static_cast<std::uint32_t>(cy) + static_cast<std::uint32_t>(cx);
    };
    if (static_cast<std::uint16_t>(x) < m.width && static_cast<std::uint16_t>(y) < m.height) m.objects[at(x, y)] = id;
    const bool on_map = static_cast<std::uint16_t>(x) < m.width && static_cast<std::uint16_t>(y) < m.height;
    const std::uint8_t h0 = on_map ? m.heights[at(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y))] : 0;
    Cell row{x, y};
    for (int a = 0; a < f.a; ++a) {
        Cell c = row;
        for (int b = 0; b < f.b; ++b) {
            const std::uint32_t i = at(c.x, c.y);
            if (i < n) {
                m.objects[i] = id;
                if (type != 0x16) {
                    m.heights[i] = h0;
                } else if (!Obstacle(m, c.x, c.y)) {
                    m.heights[i] = 0x5a;
                }
            }
            c = Cell_Step(c, 4);
        }
        row = Cell_Step(row, 1);
    }
    if (type != 0x13 && type != 0x14) return;
    if (strip_nation != 0xff && strip_nation < 6) f = s.house_footprints[strip_nation][type];
    const int rows = type == 0x13 ? 2 : 6;
    Cell start{x, y};
    for (int k = 0; k < rows; ++k) start = Cell_Step(start, 2);
    for (int k = 0; k < rows; ++k) {
        Cell c = start;
        for (int a = 0; a < f.a; ++a) {
            const std::uint32_t i = at(c.x, c.y);
            if (i < n) {
                m.objects[i] = type == 0x13 ? 99 : 100;
                m.heights[i] = on_map ? m.heights[at(x, y)] : 0;
            }
            c = Cell_Step(c, 1);
        }
        start = Cell_Step(start, 4);
    }
}



void NeutralHouse_Place(GameState& s, std::uint8_t type, std::int32_t x, std::int32_t y) {
    Map& m = s.map;
    if (x < 0 || y < 0 || x >= m.width || y >= m.height) return;
    const auto slot = static_cast<std::uint16_t>(m.neutral_houses.size());
    m.neutral_houses.push_back(NeutralHouse{type, x, y});
    const auto id = static_cast<std::uint16_t>(slot + 0x65);
    const HouseFootprint f = type < 25 ? s.house_footprints[5][type] : HouseFootprint{};
    Cell row{x, y};
    for (int a = 0; a < f.a; ++a) {
        Cell c = row;
        for (int b = 0; b < f.b; ++b) {
            if (c.x >= 0 && c.y >= 0 && c.x < m.width && c.y < m.height) m.objects[Index(m, c.x, c.y)] = id;
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    m.objects[Index(m, x, y)] = id;
    PathGrid_UpdateRegion(s, x, y, 10);
    PathGrid_UpdateRegion(s, x, y, 10);
    const std::int32_t rect[4] = {x - 5, y - 10, x + 5, y + 10};
    CoarseWater_Rebuild(s, rect);
}


void SpecialObjects_Load(GameState& s, std::uint16_t capacity, std::span<const SpecialObject> records) {
    Map& m = s.map;
    m.special_capacity = capacity;
    m.specials.assign(records.begin(), records.end());
    for (std::size_t k = 0; k < records.size(); ++k) {
        const std::size_t i = static_cast<std::size_t>(m.width) * records[k].y + records[k].x;
        if (i < m.objects.size()) m.objects[i] = static_cast<std::uint16_t>(0x25e4 + k);
    }
}

bool GameData_LoadObjects(GameData& d, ByteSpan objects_les, std::string* error) {
    auto table = parse_les(objects_les, error);
    if (!table) return false;
    if (table->entries.size() < 3) {
        *error = "objects.les has fewer than 3 entries";
        return false;
    }
    for (std::size_t t = 0; t < 3; ++t) {
        auto data = read_entry(objects_les, table->entries[t], error);
        if (!data) return false;
        if (data->size() < 2) {
            *error = "objects.les entry " + std::to_string(t) + " is empty";
            return false;
        }
        const std::size_t count = read_u16(*data, 0);
        if (2 + count * 0x38 > data->size()) {
            *error = "objects.les entry " + std::to_string(t) + " is truncated";
            return false;
        }
        d.object_blocking[t].clear();
        for (std::size_t k = 0; k < count; ++k) d.object_blocking[t].push_back((*data)[2 + 0x38 * k]);
    }
    return true;
}






std::optional<GameState> GameState_FromMap(const MapData& map, const GameData& data, std::string* error) {
    if (map.width < 8 || map.height < 20 || map.width > 0xffff / 2 || map.height > 0xffff) {
        *error = "map size out of range";
        return std::nullopt;
    }
    GameState s;
    s.unit_types = data.unit_types;
    s.house_footprints = data.house_footprints;
    s.object_blocking = data.object_blocking;
    Map& m = s.map;
    s.map_width = m.width = static_cast<std::uint16_t>(map.width);
    s.map_height = m.height = static_cast<std::uint16_t>(map.height);
    const std::size_t n = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
    m.heights.resize(n), m.objects.resize(n), m.flags.assign(n, 0);
    m.terrain.resize(n);
    m.obstacle.assign(n * 2, 0);
    m.cell_units.assign(n, {});
    m.wave.assign(n, 0);
    Grid_Init(s);
    int deposits = 0;
    for (std::size_t i = 0; i < n; ++i) {
        m.heights[i] = map.cells[i].height;
        m.terrain[i] = TerrainCell{map.cells[i].tile, map.cells[i].shade, map.cells[i].unknown4, map.cells[i].subtile};
        std::uint16_t o = map.cells[i].object;
        if ((o & 0x8000) != 0) o &= 0x7fff, m.flags[i] = 1;
        if (o > 0x2580 && o < 0x2584 && deposits < 199) {
            const auto x = static_cast<std::int32_t>(i % map.width), y = static_cast<std::int32_t>(i / map.width);
            if (x < 6 || x > map.width - 6 || y < 15 || y > map.height - 15) {
                o = 0;
            } else {
                ++deposits;
            }
        }
        m.objects[i] = o;
    }
    std::array<std::uint16_t, 8> slots{};
    for (const MapHouse& h : map.houses) {
        const auto p = static_cast<std::size_t>(h.player);
        if (p >= map.player_nations.size() || map.player_nations[p] > 4) {
            *error = "player " + std::to_string(h.player) + " has no nation 0..4";
            return std::nullopt;
        }
        if (h.cell_x < 0 || h.cell_y < 0 || h.cell_x >= map.width || h.cell_y >= map.height) {
            *error = "a house lies off the map";
            return std::nullopt;
        }
        House_PlaceOnMap(s, static_cast<std::uint8_t>(p), slots[p]++, map.player_nations[p], h.type, h.cell_x,
                         h.cell_y);
    }
    if (map.obstacle_grid.size() != n * 2) {
        *error = "obstacle grid size does not match the map";
        return std::nullopt;
    }
    m.obstacle = map.obstacle_grid;

    PathGrid_Build(s);
    for (const MapHouse& h : map.neutral_houses) NeutralHouse_Place(s, h.type, h.cell_x, h.cell_y);
    std::vector<SpecialObject> specials;
    for (const MapSpecial& r : map.specials) specials.push_back(SpecialObject{r.x, r.y, r.kind, r.b, r.f_a, r.f_b});
    SpecialObjects_Load(s, map.special_capacity, specials);
    for (std::int32_t y = 0; y < map.height; ++y) {
        for (std::int32_t x = 0; x < map.width; ++x) {
            if (Obstacle_UnderCell(m, x, y)) m.heights[Index(m, x, y)] = 0x1e;
        }
    }
    PathGrid_Build(s);
    CoarseWater_Init(s);
    return s;
}

bool Cell_Obstacle(const Map& m, std::int32_t x, std::int32_t y) { return Obstacle(m, x, y); }

// Our rules (docs/ai.md 10.17): the land masses of the terrain alone, labelled from 1 by a breadth-first search in
// cell order over the 8 steps; a cell is land when it is inside the border band and has no obstacle (water) under it,
// or is an open bridge cell. Houses, trees, deposits and slopes are left out, so the labels never change during a
// game (the obstacle grid and the bridges are fixed). 0: water or the border. Built at the first call, a scratch
// cache (GameState::land_region) that a load clears.
std::uint16_t Land_Region(GameState& s, std::int32_t x, std::int32_t y) {
    const Map& m = s.map;
    const std::size_t n = static_cast<std::size_t>(m.width) * m.height;
    if (x < 0 || y < 0 || x >= m.width || y >= m.height) return 0;
    if (s.land_region.size() != n) {
        const auto land = [&](Cell c) {
            if (Border(m, c.x, c.y)) return false;
            const std::uint16_t o = m.objects[Index(m, c.x, c.y)];
            if (o >= 0x65 && o <= 999) return BridgeCellOpen(s, o, c);
            return !Obstacle(m, c.x, c.y);
        };
        std::vector<std::uint16_t> lab(n, 0);
        std::vector<Cell> queue;
        std::uint16_t next = 0;
        for (std::int32_t cy = 0; cy < m.height; ++cy) {
            for (std::int32_t cx = 0; cx < m.width; ++cx) {
                if (lab[Index(m, cx, cy)] != 0 || !land(Cell{cx, cy}) || next == 0xffff) continue;
                lab[Index(m, cx, cy)] = ++next;
                queue.assign(1, Cell{cx, cy});
                for (std::size_t h = 0; h < queue.size(); ++h) {
                    for (std::uint8_t d = 1; d <= 8; ++d) {
                        const Cell q = Cell_Step(queue[h], d);
                        if (q.x < 0 || q.y < 0 || q.x >= m.width || q.y >= m.height) continue;
                        std::uint16_t& l = lab[Index(m, q.x, q.y)];
                        if (l != 0 || !land(q)) continue;
                        l = next;
                        queue.push_back(q);
                    }
                }
            }
        }
        s.land_region.swap(lab);
    }
    return s.land_region[Index(m, x, y)];
}

}  // namespace antanta::sim
