// The random map generator (docs/map-generator-plan.md), slice G2: the map codes, the pipeline with its ordered steps
// and attempt streams, the start zones with their kit built by one recipe on a flat plain, the connection corridors,

// sim/mapgen_land.cpp; a Sea or Islands map's water, sim/mapgen_sea.cpp). The checks and the report are in
// mapgen_check.cpp. Integer only but for the shade (the original's float formula, the same bits on every
// target: + - * / and sqrt only, -ffp-contract=off) and the direction tables (the sim's own cos and sin,
// sim/dmath.hpp, rounded to integers once). Never the CRT rand() or the game's RNG table.
#include "mapgen.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "dmath.hpp"
#include "mapgen_forest.hpp"
#include "mapgen_ground.hpp"
#include "mapgen_hills.hpp"
#include "mapgen_internal.hpp"
#include "mapgen_land.hpp"
#include "mapgen_water.hpp"

namespace antanta::sim {

namespace mapgen {

std::int64_t ISqrt(std::int64_t v) {
    if (v <= 0) return 0;
    std::int64_t r = 0, bit = std::int64_t{1} << 62;
    while (bit > v) bit >>= 2;
    while (bit != 0) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

namespace {
std::int64_t FloorDiv(std::int64_t a, std::int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
}  // namespace

void CellAt(Pt p, std::int32_t* cx, std::int32_t* cy) {
    const std::int64_t ix = FloorDiv(p.x, 96), iy = FloorDiv(p.y, 48);
    const std::int64_t fx = p.x - ix * 96, fy = p.y - iy * 48, f2 = 2 * fy;
    std::int64_t x = ix, y = 2 * iy;
    if (fx >= 48) {
        if (fy >= 24) {
            if (f2 + fx - 96 >= 48)
                x = ix + 1, y = 2 * iy + 2;
            else
                y = 2 * iy + 1;
        } else if (f2 - fx + 96 < 48) {
            x = ix + 1;
        } else {
            y = 2 * iy + 1;
        }
    } else if (fy >= 24) {
        y = f2 - fx >= 48 ? 2 * iy + 2 : 2 * iy + 1;
    } else if (f2 + fx >= 48) {
        y = 2 * iy + 1;
    }
    *cx = static_cast<std::int32_t>(x), *cy = static_cast<std::int32_t>(y);
}

void Step(std::int32_t* x, std::int32_t* y, int dir) {
    static constexpr std::int32_t kEven[9][2] = {{0, 0},  {0, -1}, {0, 1}, {-1, 1}, {-1, -1},
                                                 {0, -2}, {0, 2},  {1, 0}, {-1, 0}};
    static constexpr std::int32_t kOdd[9][2] = {{0, 0},  {1, -1}, {1, 1}, {0, 1}, {0, -1},
                                                {0, -2}, {0, 2},  {1, 0}, {-1, 0}};
    const std::int32_t (*t)[2] = (*y & 1) != 0 ? kOdd : kEven;
    *x += t[dir][0], *y += t[dir][1];
}

// Per cell 1 when buildable as the report counts it: not water (kWaterMark), off the edge band, the building rule's
// span (20) over the cell and its neighbours. Kit objects are left out of the measure (every zone holds the same).
std::vector<std::uint8_t> BuildableLand(const MapData& m) {
    const std::int32_t W = m.width, H = m.height;
    std::vector<std::uint8_t> out(m.cells.size(), 0);
    for (std::int32_t y = 0; y < H; ++y) {
        for (std::int32_t x = 0; x < W; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * W + x;
            if (m.cells[i].object == kWaterMark || x <= 3 || y <= 9 || x >= W - 3 || y >= H - 9) continue;
            std::int32_t lo = m.cells[i].height, hi = lo;
            for (int dir = 1; dir <= 8; ++dir) {
                std::int32_t nx = x, ny = y;
                Step(&nx, &ny, dir);
                const std::int32_t h = m.cells[static_cast<std::size_t>(ny) * W + nx].height;
                lo = std::min(lo, h), hi = std::max(hi, h);
            }
            out[i] = hi - lo <= 20 ? 1 : 0;
        }
    }
    return out;
}

}  // namespace mapgen

using namespace mapgen;

namespace {

// ---- The random numbers: SplitMix64 to seed, PCG32 to draw (plan 5, 5.11). ----

std::uint64_t SplitMix64(std::uint64_t& s) {
    s += 0x9e3779b97f4a7c15ull;
    std::uint64_t z = s;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

struct Pcg32 {
    std::uint64_t state = 0, inc = 1;
    std::uint32_t next() {
        const std::uint64_t old = state;
        state = old * 6364136223846793005ull + inc;
        const auto x = static_cast<std::uint32_t>(((old >> 18) ^ old) >> 27);
        const auto rot = static_cast<std::uint32_t>(old >> 59);
        return (x >> rot) | (x << ((32u - rot) & 31u));
    }
    // 0 .. n - 1 (n > 0), by the multiply and shift (no division, the same on every target).
    std::uint32_t below(std::uint32_t n) {
        return static_cast<std::uint32_t>((static_cast<std::uint64_t>(next()) * n) >> 32);
    }
    // lo .. hi inclusive.
    std::int64_t range(std::int64_t lo, std::int64_t hi) {
        return lo + static_cast<std::int64_t>(below(static_cast<std::uint32_t>(hi - lo + 1)));
    }
};

// Each step draws from its own stream, seeded from the map seed and the attempt (plan 5.11), so a change in one step
// does not move the others and every attempt is a new map.
enum StepId : std::uint32_t {
    kStepPositions = 1,
    kStepKit,
    kStepCorridors,
    kStepTiles,
    kStepHills,
    kStepDeposits,
    kStepForests,
    kStepWater
};

Pcg32 Stream(std::uint32_t seed, std::uint32_t attempt, std::uint32_t step) {
    std::uint64_t s = (static_cast<std::uint64_t>(seed) << 32) | (attempt << 8) | step;
    const std::uint64_t a = SplitMix64(s), b = SplitMix64(s);
    Pcg32 r;
    r.inc = (b << 1) | 1u;
    r.next();
    r.state += a;
    r.next();
    return r;
}

// A hash of a lattice point (the noise and the per-cell choices).
std::uint32_t Hash(std::uint32_t key, std::int64_t x, std::int64_t y) {
    std::uint64_t s = static_cast<std::uint64_t>(key) * 0x9e3779b97f4a7c15ull ^
                      static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) * 0xc2b2ae3d27d4eb4full ^
                      static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) * 0x165667b19e3779f9ull;
    return static_cast<std::uint32_t>(SplitMix64(s) >> 32);
}

// Unit vectors of n directions with 16 fraction bits: the sim's own cos and sin, rounded once.
std::vector<Pt> Directions(int n) {
    std::vector<Pt> out(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k) {
        const double a = 6.283185307179586 * k / n;
        out[static_cast<std::size_t>(k)] = Pt{static_cast<std::int64_t>(std::floor(dmath::cos(a) * 65536.0 + 0.5)),
                                              static_cast<std::int64_t>(std::floor(dmath::sin(a) * 65536.0 + 0.5))};
    }
    return out;
}

// The circular distance of two direction indices on a table of n.
int AngleGap(int a, int b, int n) {
    const int d = ((a - b) % n + n) % n;
    return d < n - d ? d : n - d;
}

// The tree and stone ids: the measured tables of sim/mapgen_forest.cpp (G4). The ground: sim/mapgen_ground.cpp (G3).

// ---- Step 0: the frame (plan 5.0). ----

struct Frame {
    std::int32_t W = 0, H = 0;
    std::int64_t wpx = 0, hpx = 0;
};

// ---- Step 1: player positions (plan 5.1): fitted to a drawn design, sim/mapgen_land.cpp and sim/mapgen_sea.cpp. ----

// The layout of a map (MapGenLayout::pattern, plan 5.1 and 5.4, decision 53): 0 a land map, 1 a Sea map (its water
// drawn first, sim/mapgen_sea.cpp), 4 islands.
std::uint8_t SeaPattern(const MapGenSettings& g) { return g.islands ? 4 : g.sea ? 1 : 0; }

// ---- Islands (pattern 4, decision 53; water-first since 2026-11-08: IslandsPlan, sim/mapgen_sea.cpp). ----

Frame FrameOf(std::uint8_t size) {
    Frame f;
    f.W = size == 1 ? 300 : 200;
    f.H = size == 1 ? 600 : 400;
    f.wpx = std::int64_t{f.W} * 96, f.hpx = std::int64_t{f.H} * 24;
    return f;
}

// Whether the islands of a plan have room (the water-first islands, 2026-11-08: the plans the sweep builds on every
// code): an island holds at most 4 players; two islands differ by at most 2 players and, from 3 players on one of
// them, the other holds 2 or more (3+1 and 4+1 leave the single too little width); three or four islands hold the
// same count each, on a Normal map 2 or more and never four islands. The answer depends on the size and the plan
// only (the setup screen asks it for every team setting).
bool PlanFits(std::uint8_t size, int players, const std::vector<int>& sizes) {
    if (sizes.empty() || players < 2) return false;
    const int big = *std::max_element(sizes.begin(), sizes.end()),
              small = *std::min_element(sizes.begin(), sizes.end());
    if (big > 4) return false;
    if (sizes.size() == 2) return big - small <= 2 && (big <= 2 || small >= 2);
    return big == small && (size == 1 || (small >= 2 && sizes.size() == 3));
}

// The players per island: the code's plan when it fits (PlanFits), else one of the even splits drawn (decision 56
// b): one island each (2 players; 3, or 4 on a Large map, as a variant: four single islands on a Normal map find no
// fair starts on most codes), two islands with the players split evenly (the bigger first), or islands of 2 (6 players
// as three, 8 as four on a Large map).
std::vector<int> IslandPlan(const MapGenSettings& g, Pcg32& r) {
    std::vector<int> sizes = MapGen_IslandSizes(g.island_plan, g.players);
    if (!sizes.empty() && PlanFits(g.size, g.players, sizes)) return sizes;
    const int n = g.players;
    std::vector<std::vector<int>> plans;
    if (n == 2 || n == 3 || (n == 4 && g.size == 1)) plans.emplace_back(static_cast<std::size_t>(n), 1);
    if (n > 2) plans.push_back({(n + 1) / 2, n / 2});
    if (n == 6 || (n == 8 && g.size == 1)) plans.emplace_back(static_cast<std::size_t>(n / 2), 2);
    return plans.size() == 1 ? plans[0] : plans[r.below(static_cast<std::uint32_t>(plans.size()))];
}

// ---- Step 2: the start zones and their kit (plan 5.2). ----

// The six deposits (near gold, iron, oil, then far gold, iron, oil) as cell offsets from a start on an even row, drawn
// once per map in one quadrant with an even row offset; each start takes its own mirror image of each (the signs).
// So every start's deposits lie at the same world distance and at the same |dx| + |dy| in cells and rows, the

// far one by 8 or more (the base moves a few cells when the city centre stands), so every computer takes the near one
// first, as a player would (G2: a far one was taken first in 28 % of the picks). The woods' sites are drawn in a range
// per start, their sawmill boxes hold the same trees by construction.
struct Kit {
    std::array<std::array<std::int32_t, 2>, 6> off{};
    std::vector<std::uint16_t> combos;  // the sign choices (2 bits per deposit: x, y) that keep the recipe's angles
};

// The distances (near 600 to 900, far 1,000 to 1,250 px) and angles of the six offsets, and the sign choices whose
// directions keep the recipe: near ones at least 70 degrees apart, far ones at least 40 degrees from every near one
// and 28 degrees from each other, all deposits 450 px apart. Drawn again while fewer than 16 choices are left.
bool DrawKit(Pcg32& r, const std::vector<Pt>& dirs, Kit* kit) {
    for (int t = 0; t < 75; ++t) {
        bool drawn = true;
        for (std::size_t k = 0; k < 6 && drawn; ++k) {
            drawn = false;
            for (int u = 0; u < 30 && !drawn; ++u) {  // a far one: drawn again until it is 8 farther for the finder
                const std::int64_t d = k < 3 ? r.range(600, 900) : r.range(1000, 1250);
                const Pt dir = dirs[r.below(65)];  // 0 to 90 degrees
                const Pt to{(dir.x * d + 32768) >> 16, (dir.y * d + 32768) >> 16};
                std::int64_t err = -1;
                for (std::int64_t cy = std::max<std::int64_t>(0, to.y / 48 * 2 - 2); cy <= to.y / 48 * 2 + 4; cy += 2) {
                    for (std::int64_t cx = std::max<std::int64_t>(0, to.x / 96 - 1); cx <= to.x / 96 + 2; ++cx) {
                        const std::int64_t e = std::abs(ISqrt(96 * 96 * cx * cx + 24 * 24 * cy * cy) - d);
                        if (err < 0 || e < err) {
                            err = e;
                            kit->off[k] = {static_cast<std::int32_t>(cx), static_cast<std::int32_t>(cy)};
                        }
                    }
                }
                drawn = k < 3 || kit->off[k - 3][0] + kit->off[k - 3][1] + 8 <= kit->off[k][0] + kit->off[k][1];
            }
        }
        if (!drawn) continue;
        kit->combos.clear();
        for (std::uint32_t code = 0; code < 4096; ++code) {
            std::array<Pt, 6> v{};
            for (std::size_t k = 0; k < 6; ++k) {
                const std::uint32_t s = code >> (2 * k) & 3;
                v[k] = Pt{((s & 1) != 0 ? -96 : 96) * std::int64_t{kit->off[k][0]},
                          ((s & 2) != 0 ? -24 : 24) * std::int64_t{kit->off[k][1]}};
            }
            bool ok = true;
            for (std::size_t i = 0; i < 6 && ok; ++i) {
                for (std::size_t j = i + 1; j < 6 && ok; ++j) {
                    // cos 70 = 0.342, cos 40 = 0.766, cos 28 = 0.883
                    const std::int64_t cos = j < 3 ? 342 : i < 3 ? 766 : 883;
                    const std::int64_t dot = v[i].x * v[j].x + v[i].y * v[j].y;
                    const std::int64_t len = ISqrt(Dist2(v[i], Pt{}) * Dist2(v[j], Pt{}));
                    ok = dot * 1000 <= cos * len && Dist2(v[i], v[j]) >= 450 * 450;
                }
            }
            if (ok) kit->combos.push_back(static_cast<std::uint16_t>(code));
        }
        if (kit->combos.size() >= 16) return true;
    }
    return false;
}

struct KitBuilder {
    const Frame& f;
    MapData& m;
    bool winter;
    const std::vector<Pt>& dirs;         // 256 directions
    std::vector<std::uint8_t> reserved;  // 1: no tree or stone (deposit rooms, lanes); 2: also a house's site
    std::uint32_t id_key = 0;

    std::size_t Index(std::int32_t x, std::int32_t y) const { return static_cast<std::size_t>(y) * f.W + x; }
    bool Inside(std::int32_t x, std::int32_t y) const { return x >= 0 && y >= 0 && x < f.W && y < f.H; }
    std::uint16_t& Object(std::int32_t x, std::int32_t y) { return m.cells[Index(x, y)].object; }

    // A cell for a tree or a stone of the zone around c: inside the ring of the zone (off the core, inside the
    // radius by one cell), free and not reserved.
    bool Free(Pt c, std::int32_t x, std::int32_t y) const {
        if (!Inside(x, y)) return false;
        const std::int64_t d2 = Dist2(CellCentre(x, y), c);
        if (d2 < (kCoreR + 48) * (kCoreR + 48) || d2 > (kZoneR - 48) * (kZoneR - 48)) return false;
        return m.cells[Index(x, y)].object == 0 && reserved[Index(x, y)] == 0;
    }
    // A house's site (a mine's footprint and its border): no object, and no other site over it.
    void ReserveSite(const std::vector<std::int32_t>& cells) {
        for (std::int32_t i : cells) {
            for (int d = 0; d <= 8; ++d) {
                std::int32_t nx = i % f.W, ny = i / f.W;
                Step(&nx, &ny, d);
                if (Inside(nx, ny)) reserved[Index(nx, ny)] = 2;
            }
        }
    }
    void ReserveDisc(Pt p, std::int64_t r) {
        std::int32_t cx = 0, cy = 0;
        CellAt(p, &cx, &cy);
        const std::int32_t rx = static_cast<std::int32_t>(r / 96) + 2, ry = static_cast<std::int32_t>(r / 24) + 2;
        for (std::int32_t y = cy - ry; y <= cy + ry; ++y) {
            for (std::int32_t x = cx - rx; x <= cx + rx; ++x) {
                if (Inside(x, y) && Dist2(CellCentre(x, y), p) <= r * r)
                    reserved[Index(x, y)] = std::max<std::uint8_t>(reserved[Index(x, y)], 1);
            }
        }
    }
    // A lane of half-width `w` from c to p kept free of trees (a near deposit's path stays straight, so its path
    // distance is the recipe's for every start).
    void ReserveLane(Pt c, Pt p, std::int64_t w) {
        const std::int64_t len = ISqrt(Dist2(c, p));
        for (std::int64_t t = 0; t <= len; t += 24)
            ReserveDisc(Pt{c.x + (p.x - c.x) * t / len, c.y + (p.y - c.y) * t / len}, w);
    }
    std::uint16_t TreeId(std::int32_t x, std::int32_t y) const { return TreeIdOf(winter, Hash(id_key, x, y)); }

    // The six deposits: the kit's offsets in one of its sign choices; each with its mine's place (CanPlace) and its
    // room.
    bool Deposits(Pt c, const Kit& kit, Pcg32& r, MapGenZone* z) {
        std::int32_t sx = 0, sy = 0;
        CellAt(c, &sx, &sy);
        for (int t = 0; t < 75; ++t) {
            const std::uint32_t code = kit.combos[r.below(static_cast<std::uint32_t>(kit.combos.size()))];
            std::array<Pt, 6> p{};
            bool ok = true;
            for (std::size_t k = 0; k < 6; ++k) {
                const std::uint32_t s = code >> (2 * k) & 3;
                const std::int32_t x = sx + ((s & 1) != 0 ? -1 : 1) * kit.off[k][0];
                const std::int32_t y = sy + ((s & 2) != 0 ? -1 : 1) * kit.off[k][1];
                ok = ok && Inside(x, y);
                p[k] = CellCentre(x, y);
                z->deposits[k] = {x, y};
            }
            if (!ok) continue;
            for (int k = 0; k < 6; ++k) {
                const auto& d = z->deposits[static_cast<std::size_t>(k)];
                Object(d[0], d[1]) = kKinds[k % 3];
            }
            const Grid grid{f.W, f.H, &m, nullptr};
            for (int k = 0; k < 6 && ok; ++k) {
                const auto& d = z->deposits[static_cast<std::size_t>(k)];
                ok = CanPlace(grid, d[0], d[1], kMineOf[k % 3], false);
            }
            if (!ok) {
                for (const auto& d : z->deposits) Object(d[0], d[1]) = 0;
                continue;
            }
            for (int k = 0; k < 6; ++k) {
                const auto& d = z->deposits[static_cast<std::size_t>(k)];
                std::vector<std::int32_t> box;
                Box1(f.W, f.H, d[0], d[1], kMineOf[k % 3], &box);
                ReserveSite(box);
                ReserveDisc(p[static_cast<std::size_t>(k)], kRoom + 48);
                if (k < 3) ReserveLane(c, p[static_cast<std::size_t>(k)], 72);
            }
            return true;
        }
        return false;
    }






    bool Wood(Pt c, std::int64_t site_lo, std::int64_t site_hi, std::int64_t rx, std::int64_t ry, int count, int in_box,
              int away, const std::array<std::int32_t, 2>* other, Pcg32& r, std::array<std::int32_t, 2>* site,
              int* dir_out) {
        const int n = static_cast<int>(dirs.size());
        std::vector<std::int32_t> foot;
        for (int t = 0; t < 75; ++t) {
            const int a = static_cast<int>(r.below(static_cast<std::uint32_t>(n)));
            if (away >= 0 && AngleGap(a, away, n) < 64) continue;
            std::int32_t sx = 0, sy = 0;
            CellAt(Toward(c, dirs[static_cast<std::size_t>(a)], r.range(site_lo, site_hi)), &sx, &sy);
            if (other != nullptr && std::abs(sx - (*other)[0]) < 10 && std::abs(sy - (*other)[1]) < 20) continue;
            if (!Box1(f.W, f.H, sx, sy, kSawmill, &foot)) continue;
            std::vector<std::uint8_t> near_foot;  // the footprint and its border
            bool clear = true;
            std::vector<std::int32_t> border;
            for (std::int32_t i : foot) {
                for (int d = 0; d <= 8 && clear; ++d) {
                    std::int32_t nx = i % f.W, ny = i / f.W;
                    Step(&nx, &ny, d);
                    clear = Inside(nx, ny) && m.cells[Index(nx, ny)].object == 0 && reserved[Index(nx, ny)] != 2 &&
                            Dist2(CellCentre(nx, ny), c) <= kZoneR * kZoneR;
                    if (clear) border.push_back(static_cast<std::int32_t>(Index(nx, ny)));
                }
                if (!clear) break;
            }
            if (!clear) continue;
            const std::uint32_t key = r.next();
            const int first = static_cast<int>(r.below(2));
            for (int side = 0; side < 2; ++side) {
                // The grove's centre below the footprint and to one side, where the box has room.
                const Pt s = CellCentre(sx, sy), g{s.x + (side == first ? 1 : -1) * 240, s.y + 96};
                std::vector<std::pair<std::int64_t, std::int32_t>> inner, outer;  // (score, cell index)
                std::int32_t gcx = 0, gcy = 0;
                CellAt(g, &gcx, &gcy);
                for (std::int32_t y = std::min(sy - 10, gcy - static_cast<std::int32_t>(ry / 24) - 1);
                     y <= std::max(sy + 9, gcy + static_cast<std::int32_t>(ry / 24) + 1); ++y) {
                    for (std::int32_t x = std::min(sx - 5, gcx - static_cast<std::int32_t>(rx / 96) - 1);
                         x <= std::max(sx + 4, gcx + static_cast<std::int32_t>(rx / 96) + 1); ++x) {
                        if (!Free(c, x, y)) continue;
                        const auto i = static_cast<std::int32_t>(Index(x, y));
                        if (std::find(border.begin(), border.end(), i) != border.end()) continue;
                        const Pt q = CellCentre(x, y);
                        const std::int64_t dx = q.x - g.x, dy = q.y - g.y;
                        const std::int64_t e = (dx * dx * 1024) / (rx * rx) + (dy * dy * 1024) / (ry * ry);
                        const bool box = x >= sx - 5 && x < sx + 5 && y >= sy - 10 && y < sy + 10;
                        if (!box && e > 1024) continue;
                        (box ? inner : outer).emplace_back(e + (Hash(key, x, y) & 511), i);
                    }
                }
                if (static_cast<int>(inner.size()) < in_box || static_cast<int>(inner.size() + outer.size()) < count) {
                    continue;
                }
                std::sort(inner.begin(), inner.end());
                for (std::size_t k = static_cast<std::size_t>(in_box); k < inner.size(); ++k) outer.push_back(inner[k]);
                inner.resize(static_cast<std::size_t>(in_box));
                std::sort(outer.begin(), outer.end());
                outer.resize(static_cast<std::size_t>(count - in_box));
                for (const auto* list : {&inner, &outer}) {
                    for (const auto& [score, i] : *list) {
                        m.cells[static_cast<std::size_t>(i)].object = TreeId(i % f.W, i / f.W);
                    }
                }
                for (std::int32_t i : border) reserved[static_cast<std::size_t>(i)] = 2;
                *site = {sx, sy};
                *dir_out = a;
                return true;
            }
        }
        return false;
    }

    // `count` trees in clumps of 1 to 8 over the ring of the zone.
    bool Scatter(Pt c, int count, Pcg32& r, std::vector<std::int32_t>* trees) {
        const int n = static_cast<int>(dirs.size());
        int placed = 0;
        for (int t = 0; placed < count; ++t) {
            if (t >= 75 * 20) return false;
            const int size = std::min(static_cast<int>(r.range(1, 8)), count - placed);
            // Two draws in two statements: the order of a call's arguments differs between compilers.
            const Pt u = dirs[r.below(static_cast<std::uint32_t>(n))];
            const Pt centre = Toward(c, u, r.range(kCoreR + 100, kZoneR - 80));
            std::int32_t x = 0, y = 0;
            CellAt(centre, &x, &y);
            if (!Free(c, x, y)) continue;
            // A clump: a short random walk from its first tree.
            int got = 0;
            for (int w = 0; w < 40 && got < size; ++w) {
                if (Free(c, x, y)) {
                    Object(x, y) = TreeId(x, y);
                    trees->push_back(static_cast<std::int32_t>(Index(x, y)));
                    ++got;
                }
                std::int32_t nx = x, ny = y;
                Step(&nx, &ny, static_cast<int>(r.range(1, 8)));
                if (Inside(nx, ny) && Dist2(CellCentre(nx, ny), centre) <= 200 * 200) x = nx, y = ny;
            }
            placed += got;
        }
        return true;
    }

    // `count` stones, each on a cell next to one of `trees`.
    bool Stones(Pt c, int count, Pcg32& r, const std::vector<std::int32_t>& trees) {
        for (int k = 0; k < count; ++k) {
            bool done = false;
            for (int t = 0; t < 75 && !done; ++t) {
                const std::int32_t i = trees[r.below(static_cast<std::uint32_t>(trees.size()))];
                std::int32_t x = i % f.W, y = i / f.W;
                Step(&x, &y, static_cast<int>(r.range(1, 8)));
                if (!Free(c, x, y)) continue;
                Object(x, y) = StoneIdOf(winter, Hash(id_key ^ 0x27d4eb2fu, x, y));
                done = true;
            }
            if (!done) return false;
        }
        return true;
    }
};

// ---- Step 3: connections (plan 5.3). ----

// The corridors on a coarse grid of 192 x 192 world pixels: from each start to its neighbours by land (`neighbours`)
// and, on a land map, to the middle, along the cheapest line, a foreign zone with its margin 20 times dearer. The
// corridors keep off the drawn water (`design`, a Sea or Islands map's sea or a land map's ponds, may be null: its
// water 400 times dearer (the land always joins the starts), its shore within 700 px 6 times); on a Sea or Islands map
// no corridor goes to the middle. On Large land maps only about two thirds of the neighbour corridors are kept. A cell
// within 384 pixels of a corridor's line and outside every zone is marked.
void Corridors(const Frame& f, const MapGenSettings& g, std::uint8_t pattern, const std::vector<Pt>& starts,
               const std::vector<std::array<std::int8_t, 2>>& neighbours, const SeaDesign* design, Pcg32& r,
               std::vector<std::uint8_t>* mark) {
    const std::int64_t S = 192;
    const auto cw = static_cast<std::int32_t>(f.wpx / S), ch = static_cast<std::int32_t>(f.hpx / S);
    const int n = static_cast<int>(starts.size());
    auto node = [&](Pt p) {
        const auto x = static_cast<std::int32_t>(std::clamp<std::int64_t>(p.x / S, 0, cw - 1));
        const auto y = static_cast<std::int32_t>(std::clamp<std::int64_t>(p.y / S, 0, ch - 1));
        return y * cw + x;
    };
    std::vector<std::pair<int, int>> pairs;  // (start, start or -1 for the middle)
    for (const auto& pr : neighbours) pairs.emplace_back(pr[0], pr[1]);
    if (g.size == 1 && !g.sea && pairs.size() > 2) {
        std::vector<std::pair<int, int>> kept;
        for (const auto& pr : pairs) {
            if (r.below(3) != 0) kept.push_back(pr);
        }
        pairs = kept;
    }
    if (pattern == 0) {
        for (int i = 0; i < n; ++i) pairs.emplace_back(i, -1);
    }
    // The cost of each coarse node.
    std::vector<std::int32_t> cost(static_cast<std::size_t>(cw) * ch, 10);
    for (std::int32_t y = 0; y < ch; ++y) {
        for (std::int32_t x = 0; x < cw; ++x) {
            const Pt p{x * S + S / 2, y * S + S / 2};
            for (const Pt& s : starts) {
                if (Dist2(p, s) < (kZoneR + kMargin) * (kZoneR + kMargin))
                    cost[static_cast<std::size_t>(y) * cw + x] = 200;
            }
            if (design != nullptr) {  // over the drawn land, off its shore
                const std::int64_t v = SeaDesignAt(*design, p.x, p.y);
                std::int32_t& c = cost[static_cast<std::size_t>(y) * cw + x];
                c = std::max(c, v > 0 ? 4000 : v > -700 ? 60 : 10);
            }
        }
    }
    const Pt middle{f.wpx / 2, f.hpx / 2};
    std::vector<std::int64_t> dist;
    std::vector<std::int32_t> from;
    for (const auto& [a, b] : pairs) {
        const Pt pa = starts[static_cast<std::size_t>(a)], pb = b < 0 ? middle : starts[static_cast<std::size_t>(b)];
        const std::int32_t src = node(pa), dst = node(pb);
        dist.assign(cost.size(), -1);
        from.assign(cost.size(), -1);
        std::vector<std::pair<std::int64_t, std::int32_t>> heap{{0, src}};
        dist[static_cast<std::size_t>(src)] = 0;
        while (!heap.empty()) {
            std::pop_heap(heap.begin(), heap.end(), std::greater<>());
            const auto [d, u] = heap.back();
            heap.pop_back();
            if (d != dist[static_cast<std::size_t>(u)]) continue;
            if (u == dst) break;
            const std::int32_t ux = u % cw, uy = u / cw;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if ((dx == 0 && dy == 0) || ux + dx < 0 || uy + dy < 0 || ux + dx >= cw || uy + dy >= ch) continue;
                    const std::int32_t v = (uy + dy) * cw + ux + dx;
                    // The end nodes take the plain cost (a start lies in its own zone).
                    const std::int64_t c = v == dst ? 10 : cost[static_cast<std::size_t>(v)];
                    const std::int64_t nd = d + c * (dx != 0 && dy != 0 ? 14 : 10);
                    if (dist[static_cast<std::size_t>(v)] < 0 || nd < dist[static_cast<std::size_t>(v)]) {
                        dist[static_cast<std::size_t>(v)] = nd, from[static_cast<std::size_t>(v)] = u;
                        heap.emplace_back(nd, v);
                        std::push_heap(heap.begin(), heap.end(), std::greater<>());
                    }
                }
            }
        }
        for (std::int32_t u = dst; u >= 0; u = from[static_cast<std::size_t>(u)]) {
            const Pt p{(u % cw) * S + S / 2, (u / cw) * S + S / 2};
            const std::int64_t rad = 384;
            std::int32_t cx = 0, cy = 0;
            CellAt(p, &cx, &cy);
            for (std::int32_t y = cy - 17; y <= cy + 17; ++y) {
                for (std::int32_t x = cx - 5; x <= cx + 5; ++x) {
                    if (x < 0 || y < 0 || x >= f.W || y >= f.H) continue;
                    const Pt q = CellCentre(x, y);
                    if (Dist2(q, p) > rad * rad) continue;
                    bool in_zone = false;
                    for (const Pt& s : starts) in_zone = in_zone || Dist2(q, s) <= kZoneR * kZoneR;
                    if (!in_zone) (*mark)[static_cast<std::size_t>(y) * f.W + x] = 1;
                }
            }
            if (u == src) break;
        }
    }
}

// One attempt (plan 5.11) up to the checks: every step from its stream seeded with (seed, attempt). nullopt with the
// failed step in `why`. `water`: the water cells for the painter (Finish).
std::optional<MapData> BuildAttempt(const MapGenSettings& g, std::uint32_t attempt, MapGenLayout* layout,
                                    std::vector<std::uint8_t>* water, std::string* why) {
    MapData m;
    const Frame f = FrameOf(g.size);
    m.width = f.W, m.height = f.H;
    const std::size_t n = static_cast<std::size_t>(f.W) * static_cast<std::size_t>(f.H);
    const std::vector<Pt> dirs = Directions(256);

    // ---- Step 1: the design and the positions fitted to it (the territories by land path after the water). ----
    Pcg32 ps = Stream(g.seed, attempt, kStepPositions);
    std::vector<Pt> starts;
    SeaDesign design;    // a Sea or Islands map: its water
    LandDesign country;  // a land map: its kind of country (decision 56)
    layout->pattern = SeaPattern(g);
    layout->sea_band = 0;
    layout->territory.assign(n, -1);
    if (layout->pattern == 4) {
        const std::vector<int> sizes = IslandPlan(g, ps);
        const std::uint64_t plan_high = ps.next();
        if (!IslandsPlan(g, plan_high << 32 | ps.next(), sizes, &design, &starts, &layout->island, &layout->neighbours,
                         why))
            return std::nullopt;
        layout->shape = design.words;
    } else if (layout->pattern == 1) {
        const std::uint64_t plan_high = ps.next();
        if (!SeaPlan(g, plan_high << 32 | ps.next(), &design, &starts, &layout->neighbours, &layout->sea_band, why))
            return std::nullopt;
        layout->shape = design.words;
    } else {
        const std::uint64_t plan_high = ps.next();
        if (!LandPlan(g, plan_high << 32 | ps.next(), &country, &starts, &layout->neighbours, why)) return std::nullopt;
        layout->shape = country.field.words;
    }
    // The drawn water (a Sea or Islands map's sea, a land map's ponds) and the forest belts, for the steps that read
    // them.
    const SeaDesign* water_design = layout->pattern != 0               ? &design
                                    : country.kind == LandKind::kPonds ? &country.field
                                                                       : nullptr;
    const SeaDesign* belts = country.kind == LandKind::kWoodlands ? &country.field : nullptr;

    for (Pt& s : starts) {  // every start on the centre of the nearest cell of an even row (the kit's mirror images)
        const std::int64_t x = (s.x + 48) / 96, y = (s.y + 24) / 48 * 2;
        s = CellCentre(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
    }

    // ---- The plain (step 5 below raises the swell, the hills and the mountains outside the zones). ----
    m.cells.assign(n, MapCell{});
    for (MapCell& c : m.cells) c.height = kPlain, c.unknown4 = 205;

    // ---- Step 2: the zones and their kit, one recipe; the distances once per map, the directions per start. ----
    Pcg32 ks = Stream(g.seed, attempt, kStepKit);
    Kit kit;
    if (!DrawKit(ks, dirs, &kit)) {
        *why = "kit: no recipe in 75 draws";
        return std::nullopt;
    }
    KitBuilder kb{f, m, g.winter, dirs, std::vector<std::uint8_t>(n, 0), ks.next()};
    layout->zones.assign(starts.size(), MapGenZone{});
    for (std::size_t p = 0; p < starts.size(); ++p) {
        const Pt c = starts[p];
        MapGenZone& z = layout->zones[p];
        z.x = static_cast<std::int32_t>(c.x), z.y = static_cast<std::int32_t>(c.y);
        const std::string who = "kit of start " + std::to_string(p + 1) + ": ";
        if (!kb.Deposits(c, kit, ks, &z)) {
            *why = who + "deposits";
            return std::nullopt;
        }
        int home_dir = -1, second_dir = -1;
        if (!kb.Wood(c, 650, 850, 260, 200, 45, 27, -1, nullptr, ks, &z.sawmills[0], &home_dir)) {
            *why = who + "home wood";
            return std::nullopt;
        }
        if (!kb.Wood(c, 850, 1000, 240, 190, 35, 20, home_dir, &z.sawmills[0], ks, &z.sawmills[1], &second_dir)) {
            *why = who + "second wood";
            return std::nullopt;
        }
        std::vector<std::int32_t> trees;
        if (!kb.Scatter(c, kKitTrees - 80, ks, &trees)) {
            *why = who + "scattered trees";
            return std::nullopt;
        }
        if (!kb.Stones(c, kKitStones, ks, trees)) {
            *why = who + "stones";
            return std::nullopt;
        }
    }

    // ---- Step 3: the connections. ----
    Pcg32 cs = Stream(g.seed, attempt, kStepCorridors);
    layout->corridor.assign(n, 0);
    Corridors(f, g, layout->pattern, starts, layout->neighbours, water_design, cs, &layout->corridor);

    // ---- Step 5: hills and mountains (sim/mapgen_hills.cpp, slice G5; before anything grows outside the zones). ----
    Pcg32 hs = Stream(g.seed, attempt, kStepHills);
    const std::uint64_t hills_high = hs.next();
    const std::uint64_t hills_key = hills_high << 32 | hs.next();
    *why = Hills(m, g, starts, layout->corridor, hills_key,
                 g.mountains                         ? 2
                 : country.kind == LandKind::kRidges ? 1
                                                     : 0);
    if (!why->empty()) return std::nullopt;

    // ---- Step 4: the sea or the lakes (sim/mapgen_water.cpp, slice G6; after the heights, marked for steps 6, 7).
    // ----
    Pcg32 ws = Stream(g.seed, attempt, kStepWater);
    const std::uint64_t water_high = ws.next();
    *why = Water(m, g, layout, water_high << 32 | ws.next(), water_design, country.kind == LandKind::kPlains ? 0 : 1);
    if (!why->empty()) return std::nullopt;

    // ---- The territories cut by land path and buildable land now that the water and the heights are known
    // (decision 51). ----
    LandTerritories(m, BuildableLand(m), starts, &layout->territory);

    // ---- Steps 6 and 7: the free deposits; the forests, stones and decoration (sim/mapgen_forest.cpp, slice G4). ----
    Pcg32 fd = Stream(g.seed, attempt, kStepDeposits);
    const std::uint64_t deposits_high = fd.next();
    if (!MapGen_FreeDeposits(m, g, layout, deposits_high << 32 | fd.next(), why)) return std::nullopt;
    Pcg32 fs = Stream(g.seed, attempt, kStepForests);
    const std::uint64_t forests_high = fs.next();
    MapGen_Forests(m, g, *layout, forests_high << 32 | fs.next(), belts);
    *why = Ports(m, g, *layout, water);  // the port sites; the water marks removed
    if (!why->empty()) return std::nullopt;
    MapGen_TankLanes(m, *layout);
    return m;
}

// The rest of the attempt kept: the looks, which no check reads (so a failed attempt never pays for them).
void Finish(MapData& m, const MapGenSettings& g, std::uint32_t attempt, const MapGenLayout& layout,
            const std::vector<std::uint8_t>& water) {
    // ---- Step 8: ground painting (sim/mapgen_ground.cpp, slice G3; the shore by the water). ----
    Pcg32 ts = Stream(g.seed, attempt, kStepTiles);
    const std::uint64_t ground_high = ts.next();
    const std::uint64_t ground_key = ground_high << 32 | ts.next();
    MapGen_PaintGround(m, water, g.winter, ground_key);

    // ---- Step 9: shade and records. ----
    Map_ShadeFromHeights(m);
    for (std::size_t p = 0; p < layout.zones.size(); ++p) {
        const MapGenZone& z = layout.zones[p];
        m.player_names.emplace_back();
        m.player_nations.push_back(static_cast<std::uint8_t>(p % 5));
        m.player_bytes29.push_back(0);
        m.unit_counts[p] = kWorkers;
        for (int k = 0; k < kWorkers; ++k) {  // 8 workers (type 0) in two rows of four
            MapUnit u;
            u.player = static_cast<std::int32_t>(p);
            u.world_x = static_cast<float>(z.x - 84 + 56 * (k % 4));
            u.world_y = static_cast<float>(z.y - 24 + 48 * (k / 4));
            u.type = 0;
            m.units.push_back(u);
        }
    }
    m.special_capacity = 300;  // as every shipped map
}

}  // namespace

std::vector<int> MapGen_IslandSizes(std::uint32_t plan, int players) {
    std::vector<int> out;
    int sum = 0;
    for (std::uint32_t v = plan; v != 0; v >>= 4) {
        const int k = static_cast<int>(v & 15);
        if (k == 0 || (!out.empty() && k > out.back())) return {};
        out.push_back(k), sum += k;
    }
    if (sum != players || out.size() < 2 || out.size() > 4) return {};
    return out;
}

bool MapGen_IslandPlanFits(std::uint8_t size, int players, std::uint32_t plan) {
    return PlanFits(size, players, MapGen_IslandSizes(plan, players));
}

std::string MapGen_Code(const MapGenSettings& g) {
    std::string sw;
    if (g.winter) sw += 'W';
    if (g.sea || g.islands) sw += 'S';
    if (g.islands) {
        sw += 'I';
        for (int k : MapGen_IslandSizes(g.island_plan, g.players)) sw += static_cast<char>('0' + k);
    }
    if (g.mountains) sw += 'M';
    if (g.dense_forest) sw += 'F';
    if (sw.empty()) sw = "0";
    char buf[64];
    std::snprintf(buf, sizeof buf, "v%u-%c-%u-%s-%08X", unsigned{g.version}, g.size == 1 ? 'L' : 'N',
                  unsigned{g.players}, sw.c_str(), static_cast<unsigned>(g.seed));
    return buf;
}

std::optional<MapGenSettings> MapGen_ParseCode(const std::string& code, std::string* error) {
    auto fail = [error](const std::string& why) -> std::optional<MapGenSettings> {
        if (error != nullptr) *error = why;
        return std::nullopt;
    };
    // Spaces, tabs and line ends round the code are no part of it (a pasted code, a command line).
    const std::size_t b = code.find_first_not_of(" \t\r\n"), e = code.find_last_not_of(" \t\r\n");
    const std::string trimmed = b == std::string::npos ? std::string() : code.substr(b, e - b + 1);
    std::vector<std::string> parts(1);
    for (char c : trimmed) {
        if (c == '-') {
            parts.emplace_back();
        } else {
            parts.back() += c;
        }
    }
    if (parts.size() != 5 || parts[0].size() < 2 || (parts[0][0] != 'v' && parts[0][0] != 'V'))
        return fail("not a map code (v1-N-4-0-3F9A2C71)");
    MapGenSettings g;
    if (parts[0] != "v1" && parts[0] != "V1") return fail("this map code was made by another generator version");
    if (parts[1] == "N" || parts[1] == "n") {
        g.size = 0;
    } else if (parts[1] == "L" || parts[1] == "l") {
        g.size = 1;
    } else {
        return fail("map size must be N (normal) or L (large)");
    }
    if (parts[2].size() != 1 || parts[2][0] < '2' || parts[2][0] > '8') return fail("players must be 2..8");
    g.players = static_cast<std::uint8_t>(parts[2][0] - '0');
    // The switches: "0", or W, S, I, M, F in that order, each at most once; I may be followed by the island sizes.
    if (parts[3] != "0") {
        static constexpr char kOrder[] = "WSIMF";
        static const char* kBad = "switches must be 0 or some of W, S, I, M, F in that order";
        std::size_t next = 0;
        if (parts[3].empty()) return fail(kBad);
        for (std::size_t at = 0; at < parts[3].size(); ++at) {
            const char c = parts[3][at];
            const char u = c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
            while (next < 5 && kOrder[next] != u) ++next;
            if (next == 5) return fail(kBad);
            if (u == 'W') g.winter = true;
            if (u == 'S') g.sea = true;
            if (u == 'M') g.mountains = true;
            if (u == 'F') g.dense_forest = true;
            if (u == 'I') {
                g.islands = g.sea = true;
                int shift = 0;
                while (at + 1 < parts[3].size() && parts[3][at + 1] >= '1' && parts[3][at + 1] <= '8' && shift < 32) {
                    g.island_plan |= static_cast<std::uint32_t>(parts[3][++at] - '0') << shift;
                    shift += 4;
                }
                if (g.island_plan != 0 && MapGen_IslandSizes(g.island_plan, g.players).empty())
                    return fail("the island sizes (2 to 4 islands) must add up to the players, largest first");
            }
            ++next;
        }
    }
    if (parts[4].empty() || parts[4].size() > 8) return fail("the seed must be 1 to 8 hex digits");
    std::uint32_t seed = 0;
    for (char c : parts[4]) {
        std::uint32_t d = 0;
        if (c >= '0' && c <= '9') {
            d = static_cast<std::uint32_t>(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            d = static_cast<std::uint32_t>(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            d = static_cast<std::uint32_t>(c - 'A' + 10);
        } else {
            return fail("the seed must be 1 to 8 hex digits");
        }
        seed = seed << 4 | d;
    }
    g.seed = seed;
    return g;
}



void Map_ShadeFromHeights(MapData& m) {



    const std::int64_t W = m.width, N = static_cast<std::int64_t>(m.width) * m.height;
    const float lx = 1.0f, ly = 0.8f, lz = 0.7f;
    for (std::int64_t y = 0; y < m.height; ++y) {
        for (std::int64_t x = 0; x < W; ++x) {
            const std::int64_t i = y * W + x, dx = (y & 1) != 0 ? 1 : 0;
            std::int64_t n1 = (y - 1) * W + x + dx, n2 = (y + 1) * W + x + dx;
            if (n1 < 0 || n1 > N - 1) n1 = i;
            if (n2 < 0 || n2 > N - 1) n2 = i;


            const int h = m.cells[static_cast<std::size_t>(i)].height;
            const auto d1 = static_cast<float>(m.cells[static_cast<std::size_t>(n2)].height - h);
            const int d2 = m.cells[static_cast<std::size_t>(n1)].height - h;
            const auto a = static_cast<float>(static_cast<double>(d1) * -16.0 - static_cast<double>(d2) * 16.0);
            const auto b = static_cast<float>(-(static_cast<double>(d1) * 32.0 - static_cast<double>(d2) * 32.0));
            const double dot =
                static_cast<double>(ly) * b + static_cast<double>(lx) * a + static_cast<double>(lz) * 1024.0;
            const double light =
                std::sqrt(static_cast<double>(lz) * lz + static_cast<double>(ly) * ly + static_cast<double>(lx) * lx);
            const double normal = std::sqrt(static_cast<double>(b) * b + static_cast<double>(a) * a + 1048576.0);
            const auto c = static_cast<float>(dot / (light * normal));
            // shade = ftol((h * 0.0008 + c * 0.3 + 0.5) * 255), its low byte.
            const double v =
                (h * static_cast<double>(0.0008f) + static_cast<double>(c) * static_cast<double>(0.3f) + 0.5) * 255.0;
            m.cells[static_cast<std::size_t>(i)].shade = static_cast<std::uint8_t>(static_cast<std::int32_t>(v) & 0xff);
        }
    }
}

std::optional<MapData> MapGen_Build(const MapGenSettings& g, std::string* error, MapGenReport* report) {
    auto fail = [error](const char* why) -> std::optional<MapData> {
        if (error != nullptr) *error = why;
        return std::nullopt;
    };
    if (g.version != 1 || g.size > 1 || g.players < 2 || g.players > 8)
        return fail("map generator settings out of range");
    // Until the first release a change of the output keeps the version 1 and updates the hashes on purpose (plan 8.3).
    MapGenReport local;
    MapGenReport& r = report != nullptr ? *report : local;
    r = MapGenReport{};
    for (std::uint32_t attempt = 0; attempt < 16; ++attempt) {
        MapGenLayout layout;
        std::string why;
        std::vector<std::uint8_t> water;
        auto m = BuildAttempt(g, attempt, &layout, &water, &why);
        if (m) why = MapGen_Check(*m, g, layout, &r);
        if (m && why.empty()) {
            r.attempt = static_cast<std::uint8_t>(attempt);
            Finish(*m, g, attempt, layout, water);
            r.layout = std::move(layout);
            return m;
        }
        r.failed.push_back("p" + std::to_string(layout.pattern) + " " + why);
    }
    return fail("This map code makes no valid map, press New map");
}

void MapGen_LookNumbers(const MapData& m, MapGenReport* r) {
    r->look = MapGen_GroundLook(m, WaterCells(m));
    r->wood = MapGen_WoodLook(m, &r->layout);
}

std::uint64_t MapGen_Hash(const MapData& m) {
    std::uint64_t h = 0xcbf29ce484222325ull;
    auto byte = [&h](std::uint8_t b) { h = (h ^ b) * 0x100000001b3ull; };
    auto u16 = [&byte](std::uint32_t v) {
        byte(static_cast<std::uint8_t>(v)), byte(static_cast<std::uint8_t>(v >> 8));
    };
    auto u32 = [&u16](std::uint32_t v) { u16(v & 0xffff), u16(v >> 16); };
    u32(static_cast<std::uint32_t>(m.width)), u32(static_cast<std::uint32_t>(m.height));
    for (const MapCell& c : m.cells) {
        byte(c.height), u16(c.tile), byte(c.shade), byte(c.unknown4);
        for (std::uint16_t s : c.subtile) u16(s);
        u16(c.object);
    }
    u32(static_cast<std::uint32_t>(m.player_nations.size()));
    for (std::size_t p = 0; p < m.player_nations.size(); ++p) {
        for (char ch : m.player_names[p]) byte(static_cast<std::uint8_t>(ch));
        byte(0), byte(m.player_nations[p]), byte(m.player_bytes29[p]);
    }
    for (std::uint16_t v : m.house_counts) u16(v);
    for (std::uint16_t v : m.unit_counts) u16(v);
    u32(static_cast<std::uint32_t>(m.houses.size()));
    for (const MapUnit& u : m.units) {
        u32(static_cast<std::uint32_t>(u.player));
        std::uint32_t x = 0, y = 0;
        static_assert(sizeof(float) == 4);
        std::memcpy(&x, &u.world_x, 4), std::memcpy(&y, &u.world_y, 4);
        u32(x), u32(y), u16(u.type), byte(u.orient_plus1);
    }
    for (std::uint16_t v : m.obstacle_grid) u16(v);
    u32(static_cast<std::uint32_t>(m.routes.size() + m.neutral_houses.size() + m.decorations.size() +
                                   m.specials.size()));
    u16(m.special_capacity);
    for (const MapRoute& rt : m.routes) {  // the coast outline records (G6)
        u16(rt.v), u16(static_cast<std::uint32_t>(rt.lists.size()));
        for (const auto& poly : rt.lists) {
            u16(static_cast<std::uint32_t>(poly.size()));
            for (std::uint16_t v : poly) u16(v);
        }
    }
    for (std::uint32_t v : m.port_sites) u32(v);
    return h;
}

}  // namespace antanta::sim
