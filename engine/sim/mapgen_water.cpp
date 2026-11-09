// The random map generator's water (docs/map-generator-plan.md 5.4, slice G6). See mapgen_water.hpp.
//
// The water is a field on the obstacle grid's corner lattice (one point per 48 x 24 world pixels) in "raised" world
// coordinates: the original's water lookups and its drawing take a cell's point raised by the cell's height, so a field
// in those coordinates lines up with the terrain as it is drawn. The sea (since 2026-11-06): the water drawn before the
// starts (sim/mapgen_sea.cpp), kept off the land skeleton (the zones with their margins and the corridors), then only
// the biggest body of water (one sea, no lake a transport cannot reach). Islands: per island the distance from its
// skeleton, seen through a slow warp, with its threshold set for equal land per player. A start whose coast lies too
// far gets a bay cut toward it. Lakes (land maps): noisy round blobs on low ground away from the skeleton. The cells
// then take the field at their point 40 px above the centre (between the water's 30 and the land's 50), the land near
// the water is lowered (the shore below 55, no cliff into the water), the grid and its outline records are built from
// the field, and LoadMap's own rule sets height 30 under the water, so every later rule sees the water as the engine
// will.
#include "mapgen_water.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace antanta::sim {

namespace mapgen {

namespace {

std::int64_t FloorDiv(std::int64_t a, std::int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// The step's random numbers: SplitMix64 over the key (never two draws in one call's arguments).
struct Rng {
    std::uint64_t s = 0;
    std::uint32_t next() {
        s += 0x9e3779b97f4a7c15ull;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
    }
    std::uint32_t below(std::uint32_t n) {
        return static_cast<std::uint32_t>((static_cast<std::uint64_t>(next()) * n) >> 32);
    }
    std::int64_t range(std::int64_t lo, std::int64_t hi) {
        return lo + static_cast<std::int64_t>(below(static_cast<std::uint32_t>(hi - lo + 1)));
    }
};

std::uint32_t Hash(std::uint32_t key, std::int64_t x, std::int64_t y) {
    std::uint64_t z = static_cast<std::uint64_t>(key) * 0x9e3779b97f4a7c15ull ^
                      static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) * 0xc2b2ae3d27d4eb4full ^
                      static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) * 0x165667b19e3779f9ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
}

// Value noise in -1024 .. 1024 over a lattice of `spacing` world pixels, smoothstep between the lattice points.
std::int64_t Noise(std::uint32_t key, std::int64_t x, std::int64_t y, std::int64_t spacing) {
    const std::int64_t ix = FloorDiv(x, spacing), iy = FloorDiv(y, spacing);
    const std::int64_t fx = (x - ix * spacing) * 1024 / spacing, fy = (y - iy * spacing) * 1024 / spacing;
    const std::int64_t sx = fx * fx * (3072 - 2 * fx) / (1024 * 1024), sy = fy * fy * (3072 - 2 * fy) / (1024 * 1024);
    auto v = [&](std::int64_t a, std::int64_t b) { return static_cast<std::int64_t>(Hash(key, a, b) % 2049) - 1024; };
    const std::int64_t top = v(ix, iy) * (1024 - sx) + v(ix + 1, iy) * sx;
    const std::int64_t bot = v(ix, iy + 1) * (1024 - sx) + v(ix + 1, iy + 1) * sx;
    return (top * (1024 - sy) + bot * sy) / (1024 * 1024);
}

constexpr std::int32_t kUnder = 30;      // LoadMap's height under water
constexpr std::int64_t kRaise = 40;      // a cell takes the field this far above its centre (its water 30, land 50)
constexpr std::int64_t kShoreMin = 150;  // the water keeps this far from the land skeleton (world px)
constexpr std::int32_t kFar = 1 << 28;

std::size_t CellIndex(const MapData& m, std::int32_t x, std::int32_t y) {
    return static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(x);
}

// CellWorld (sim/pathgrid.cpp): the left corner of the cell's diamond, raised by its height.
void CellWorldOf(const MapData& m, std::int32_t x, std::int32_t y, std::int32_t* wx, std::int32_t* wy) {
    if (x >= 0 && y >= 0 && x < m.width && y < m.height) {
        *wx = (x * 2 - (~y & 1)) * 48;
        *wy = y * 24 - m.cells[CellIndex(m, x, y)].height;
    } else {
        *wx = 0, *wy = 0;
    }
}

// Obstacle_UnderCell (sim/pathgrid.cpp): the grid block under the cell's raised left corner.
bool UnderCell(const MapData& m, std::int32_t x, std::int32_t y) {
    std::int32_t wx = 0, wy = 0;
    CellWorldOf(m, x, y, &wx, &wy);
    const std::int32_t world_w = m.width * 96, world_h = m.height * 24;
    if (wx <= 0 || wy < 26 || wx >= world_w - 100 || wy >= world_h - 25) return false;
    const std::size_t g = static_cast<std::size_t>(wy / 24) * (2u * static_cast<std::size_t>(m.width)) +
                          static_cast<std::size_t>(wx / 48);
    return g < m.obstacle_grid.size() && m.obstacle_grid[g] != 0;
}



bool Blocked(const MapData& m, std::int32_t x, std::int32_t y) {
    const auto ux = static_cast<std::uint16_t>(x), uy = static_cast<std::uint16_t>(y);
    if (ux <= 2 || uy <= 2 || ux > m.width - 4 || uy > m.height - 4) return false;
    std::int32_t px = ux, py = uy;
    Step(&px, &py, 2);
    std::int32_t ax = 0, ay = 0, bx = 0, by = 0;
    CellWorldOf(m, static_cast<std::uint16_t>(px), static_cast<std::uint16_t>(py), &ax, &ay);
    if (ay < 0) return false;
    CellWorldOf(m, static_cast<std::uint16_t>(px), static_cast<std::uint16_t>(py - 2), &bx, &by);
    if (by < 0) return false;
    const std::uint32_t gx = static_cast<std::uint32_t>(ax) / 48;
    const std::uint32_t gy = static_cast<std::uint32_t>((ay + by) / 2) / 24;
    const std::size_t i = static_cast<std::size_t>(gy) * (2u * static_cast<std::size_t>(m.width)) + gx;
    return i < m.obstacle_grid.size() && m.obstacle_grid[i] != 0;
}

// The span of a cell's four corners (the walking rule); -1 in the border band.
int Span(const MapData& m, std::int32_t x, std::int32_t y) {
    const std::int32_t W = m.width, H = m.height;
    if (x < 4 || y < 10 || x >= W - 3 || y >= H - 9) return -1;
    const int odd = y & 1;
    const int c[4] = {m.cells[CellIndex(m, x, y)].height, m.cells[CellIndex(m, x + odd, y - 1)].height,
                      m.cells[CellIndex(m, x + 1, y)].height, m.cells[CellIndex(m, x + odd, y + 1)].height};
    return *std::max_element(c, c + 4) - *std::min_element(c, c + 4);
}

// ---- The corner lattice. ----

struct Lattice {
    std::int32_t LW = 0, LH = 0;  // 2W + 1, H + 1
    std::size_t Index(std::int32_t i, std::int32_t j) const {
        return static_cast<std::size_t>(j) * static_cast<std::size_t>(LW) + static_cast<std::size_t>(i);
    }
    // The field at world point (x, y), bilinear between the lattice points (clamped to the lattice).
    std::int64_t Sample(const std::vector<std::int32_t>& f, std::int64_t x, std::int64_t y) const {
        x = std::clamp<std::int64_t>(x, 0, std::int64_t{LW - 1} * 48);
        y = std::clamp<std::int64_t>(y, 0, std::int64_t{LH - 1} * 24);
        const auto i = static_cast<std::int32_t>(std::min<std::int64_t>(x / 48, LW - 2));
        const auto j = static_cast<std::int32_t>(std::min<std::int64_t>(y / 24, LH - 2));
        const std::int64_t fx = x - std::int64_t{i} * 48, fy = y - std::int64_t{j} * 24;
        const std::int64_t a = f[Index(i, j)], b = f[Index(i + 1, j)], c = f[Index(i, j + 1)],
                           d = f[Index(i + 1, j + 1)];
        return FloorDiv(a * (48 - fx) * (24 - fy) + b * fx * (24 - fy) + c * (48 - fx) * fy + d * fx * fy, 48 * 24);
    }
};

// Chamfer distances (world px) on the lattice from the points with seed != 0: 48 across, 24 down, 54 diagonal.
std::vector<std::int32_t> Chamfer(const Lattice& L, const std::vector<std::uint8_t>& seed) {
    std::vector<std::int32_t> d(seed.size(), kFar);
    for (std::size_t k = 0; k < seed.size(); ++k) {
        if (seed[k] != 0) d[k] = 0;
    }
    auto relax = [&](std::size_t k, std::int32_t i, std::int32_t j, std::int32_t w) {
        if (i < 0 || j < 0 || i >= L.LW || j >= L.LH) return;
        d[k] = std::min(d[k], d[L.Index(i, j)] + w);
    };
    for (std::int32_t j = 0; j < L.LH; ++j) {
        for (std::int32_t i = 0; i < L.LW; ++i) {
            const std::size_t k = L.Index(i, j);
            relax(k, i - 1, j, 48), relax(k, i, j - 1, 24), relax(k, i - 1, j - 1, 54), relax(k, i + 1, j - 1, 54);
        }
    }
    for (std::int32_t j = L.LH - 1; j >= 0; --j) {
        for (std::int32_t i = L.LW - 1; i >= 0; --i) {
            const std::size_t k = L.Index(i, j);
            relax(k, i + 1, j, 48), relax(k, i, j + 1, 24), relax(k, i + 1, j + 1, 54), relax(k, i - 1, j + 1, 54);
        }
    }
    return d;
}

// Keeps the biggest body of water (value > 0, 4-connected on the lattice); every other water point becomes -1. The
// count kept.
std::int64_t KeepMainWater(const Lattice& L, std::vector<std::int32_t>* f) {
    std::vector<std::int32_t> body(f->size(), -1);
    std::int32_t best = -1;
    std::int64_t best_n = 0;
    std::vector<std::int32_t> todo;
    for (std::size_t k0 = 0; k0 < f->size(); ++k0) {
        if ((*f)[k0] <= 0 || body[k0] >= 0) continue;
        const auto id = static_cast<std::int32_t>(k0);
        todo.assign(1, id);
        body[k0] = id;
        for (std::size_t t = 0; t < todo.size(); ++t) {
            const std::int32_t i0 = todo[t] % L.LW, j0 = todo[t] / L.LW;
            static constexpr std::int32_t kD[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (const auto& dd : kD) {
                const std::int32_t i = i0 + dd[0], j = j0 + dd[1];
                if (i < 0 || j < 0 || i >= L.LW || j >= L.LH) continue;
                const std::size_t k = L.Index(i, j);
                if (body[k] >= 0 || (*f)[k] <= 0) continue;
                body[k] = id;
                todo.push_back(static_cast<std::int32_t>(k));
            }
        }
        if (static_cast<std::int64_t>(todo.size()) > best_n) best_n = static_cast<std::int64_t>(todo.size()), best = id;
    }
    for (std::size_t k = 0; k < f->size(); ++k) {
        if ((*f)[k] > 0 && body[k] != best) (*f)[k] = -1;
    }
    return best_n;
}

// ---- Coast outline records (marching squares). ----

// The crossing of an edge from value a at 0 to value b at len (a and b of opposite signs, never 0): its position.
std::int32_t Cross(std::int64_t a, std::int64_t b, std::int32_t len) {
    return static_cast<std::int32_t>((2 * a * len + (a - b)) / (2 * (a - b)));
}

}  // namespace

bool CoastRecords(const std::vector<std::int32_t>& field, std::int32_t W, std::int32_t H,
                  std::vector<std::uint16_t>* grid, std::vector<MapRoute>* routes) {
    const Lattice L{2 * W + 1, H + 1};
    grid->assign(static_cast<std::size_t>(2 * W) * static_cast<std::size_t>(H), 0);
    routes->clear();
    // A value never 0 (2 v + 1 keeps the sign of water, v > 0, and of land, v <= 0).
    auto val = [&](std::int32_t i, std::int32_t j) { return 2 * std::int64_t{field[L.Index(i, j)]} - 1; };
    for (std::int32_t gy = 0; gy < H; ++gy) {
        for (std::int32_t gx = 0; gx < 2 * W; ++gx) {
            // The 3 x 3 sub-lattice of the block: corners, edge middles, centre (twice the values, bilinear).
            std::int64_t s[3][3];
            const std::int64_t a = val(gx, gy), b = val(gx + 1, gy), c = val(gx, gy + 1), d = val(gx + 1, gy + 1);
            s[0][0] = 4 * a, s[0][2] = 4 * b, s[2][0] = 4 * c, s[2][2] = 4 * d;
            s[0][1] = 2 * (a + b), s[2][1] = 2 * (c + d), s[1][0] = 2 * (a + c), s[1][2] = 2 * (b + d);
            s[1][1] = a + b + c + d;
            // Sums of odd values can be 0: nudge them toward land (the next value down is odd again).
            int water = 0;
            for (auto& row : s) {
                for (std::int64_t& v : row) {
                    if (v == 0) v = -1;
                    water += v > 0 ? 1 : 0;
                }
            }
            const std::size_t gi = static_cast<std::size_t>(gy) * static_cast<std::size_t>(2 * W) + gx;
            if (water == 0) continue;
            if (water == 9) {
                (*grid)[gi] = 1;
                continue;
            }
            // The water part as strips between parallel lines: vertical ones at x = 0, 6, ..., 48 and where the coast
            // meets the top and bottom edges, or horizontal ones at y = 0, 3, ..., 24 and where it meets the left and
            // right edges, whichever crosses the coast more often (a steep coast would get no point inside the
            // block across vertical lines, so no foam). On each line the bilinear field is linear, so its water is one
            // span (the crossing measured from the line's top or left end, so blocks that share an edge put the same
            // points on it). A strip is the convex quadrilateral between the spans of its two lines. Strips touch only

            // the coast's pieces and on no inner side; a run of whole strips becomes one rectangle.
            std::vector<std::array<std::int32_t, 2>> ends;  // where the coast meets the block's edges
            if ((a > 0) != (b > 0)) ends.push_back({Cross(a, b, 48), 0});
            if ((c > 0) != (d > 0)) ends.push_back({Cross(c, d, 48), 24});
            if ((a > 0) != (c > 0)) ends.push_back({0, Cross(a, c, 24)});
            if ((b > 0) != (d > 0)) ends.push_back({48, Cross(b, d, 24)});
            const bool across =
                ends.size() == 2 && 2 * std::abs(ends[0][1] - ends[1][1]) > std::abs(ends[0][0] - ends[1][0]);
            // In strip terms: lines at p (0 .. len, step apart) with the field f0 at q = 0 and f1 at q = side; the
            // crossings of the two edges along the lines (cf at q = 0, cs at q = side, -1: none).
            const std::int32_t len = across ? 24 : 48, side = across ? 48 : 24, step = across ? 3 : 6;
            const std::int64_t f00 = a, f0l = across ? c : b, f10 = across ? b : c, f1l = d;
            std::int32_t cf = -1, cs = -1;
            if ((f00 > 0) != (f0l > 0)) cf = Cross(f00, f0l, len);
            if ((f10 > 0) != (f1l > 0)) cs = Cross(f10, f1l, len);
            std::vector<std::int32_t> lines;
            for (std::int32_t p = 0; p <= len; p += step) lines.push_back(p);
            if (cf >= 0) lines.push_back(cf);
            if (cs >= 0) lines.push_back(cs);
            std::sort(lines.begin(), lines.end());
            lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
            struct Span {
                std::int32_t lo = 0, hi = -1;  // hi < lo: no water on the line
            };
            std::vector<Span> spans;
            for (std::int32_t p : lines) {
                std::int64_t t = f00 * (len - p) + f0l * p, u = f10 * (len - p) + f1l * p;  // the line's two ends
                if (t == 0) t = -1;
                if (u == 0) u = -1;
                if (p == cf && p > 0 && p < len) t = 0;  // the coast meets the first edge here
                if (p == cs && p > 0 && p < len) u = 0;
                Span sp;
                if (t >= 0 && u >= 0) {
                    sp = Span{0, side};
                } else if (t > 0 || (t == 0 && u < 0)) {
                    sp = t == 0 ? Span{0, 0} : Span{0, Cross(t, u, side)};
                } else if (u > 0 || (u == 0 && t < 0)) {
                    sp = u == 0 ? Span{side, side} : Span{Cross(t, u, side), side};
                }
                spans.push_back(sp);
            }
            MapRoute rec;
            rec.v = 1;
            auto whole = [&](std::size_t k) { return spans[k].lo == 0 && spans[k].hi == side; };
            for (std::size_t k = 0; k + 1 < lines.size(); ++k) {
                std::size_t e = k + 1;  // a run of whole strips: one rectangle
                if (whole(k) && whole(e)) {
                    while (e + 1 < lines.size() && whole(e + 1)) ++e;
                }
                const Span l = spans[k], r = spans[e];
                const std::int32_t p0 = lines[k], p1 = lines[e];
                k = e - 1;
                std::vector<std::uint16_t> poly;
                auto put = [&poly, across](std::int32_t p, std::int32_t q) {
                    const auto v = static_cast<std::uint16_t>(across ? q | p << 8 : p | q << 8);
                    if (poly.empty() || poly.back() != v) poly.push_back(v);
                };
                if (l.hi >= l.lo) put(p0, l.lo);
                if (r.hi >= r.lo) put(p1, r.lo), put(p1, r.hi);
                if (l.hi >= l.lo) put(p0, l.hi);
                while (poly.size() > 1 && poly.front() == poly.back()) poly.pop_back();
                if (poly.size() < 3) continue;
                std::int64_t area = 0;
                for (std::size_t q = 0; q < poly.size(); ++q) {
                    const std::uint16_t v0 = poly[q], v1 = poly[(q + 1) % poly.size()];
                    area += std::int64_t{v0 & 0xff} * (v1 >> 8) - std::int64_t{v1 & 0xff} * (v0 >> 8);
                }
                if (area == 0) continue;
                // Clockwise on the screen (the order above is, for vertical strips; horizontal ones turn it).
                if (area < 0) std::reverse(poly.begin(), poly.end());
                rec.lists.push_back(std::move(poly));
            }
            if (rec.lists.empty()) continue;
            if (routes->size() >= 0x7fff) return false;
            (*grid)[gi] = static_cast<std::uint16_t>(0x8000u | routes->size());
            routes->push_back(std::move(rec));
        }
    }
    return true;
}

std::vector<std::uint8_t> BlockedCells(const MapData& m) {
    std::vector<std::uint8_t> out(m.cells.size(), 0);
    if (m.obstacle_grid.empty()) return out;
    for (std::int32_t y = 0; y < m.height; ++y) {
        for (std::int32_t x = 0; x < m.width; ++x) out[CellIndex(m, x, y)] = Blocked(m, x, y) ? 1 : 0;
    }
    return out;
}

std::vector<std::uint8_t> WaterCells(const MapData& m) {
    std::vector<std::uint8_t> out = BlockedCells(m);
    if (m.obstacle_grid.empty()) return out;
    for (std::int32_t y = 0; y < m.height; ++y) {
        for (std::int32_t x = 0; x < m.width; ++x) {
            if (UnderCell(m, x, y)) out[CellIndex(m, x, y)] = 1;
        }
    }
    return out;
}

std::vector<std::uint8_t> ShipCells(const MapData& m) {
    const std::int32_t w = m.width, h = m.height / 2;
    std::vector<std::uint8_t> nav(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    for (std::int32_t j = 0; j < h; ++j) {
        for (std::int32_t i = 0; i < w; ++i) {
            std::int32_t x = 0, y = 0;  // the coarse cell's centre cell (CoarseCentreCell)
            CellAt(Pt{96 * std::int64_t{i} + 48, 48 * std::int64_t{j} + 24}, &x, &y);
            if (x < 0 || y < 0 || x >= m.width || y >= m.height || !UnderCell(m, x, y)) continue;
            const std::uint16_t o = m.cells[CellIndex(m, x, y)].object;
            nav[static_cast<std::size_t>(j) * w + i] = o < 0x65 || o >= 0x2af8 || (o >= 0x2328 && o < 0x25e4) ? 1 : 0;
        }
    }
    std::vector<std::uint8_t> open(nav.size(), 0);
    for (std::int32_t j = 1; j + 1 < h; ++j) {
        for (std::int32_t i = 1; i + 1 < w; ++i) {
            bool ok = true;
            for (std::int32_t a = -2; a <= 2 && ok; ++a) {
                for (std::int32_t k = -2; k <= 3 && ok; ++k) {
                    const std::int32_t x = i + a, y = j + k;
                    if (x >= 0 && x < w && y >= 0 && y < h) ok = nav[static_cast<std::size_t>(y) * w + x] != 0;
                }
            }
            open[static_cast<std::size_t>(j) * w + i] = ok ? 1 : 0;
        }
    }
    return open;
}

std::vector<std::int32_t> ShipSeas(const std::vector<std::uint8_t>& open, std::int32_t w, std::int32_t h) {
    std::vector<std::int32_t> label(open.size(), -1);
    std::int32_t next = 0;
    for (std::size_t k0 = 0; k0 < open.size(); ++k0) {
        if (open[k0] == 0 || label[k0] >= 0) continue;
        std::vector<std::int32_t> todo{static_cast<std::int32_t>(k0)};
        label[k0] = next;
        for (std::size_t t = 0; t < todo.size(); ++t) {
            const std::int32_t i0 = todo[t] % w, j0 = todo[t] / w;
            for (std::int32_t dj = -1; dj <= 1; ++dj) {
                for (std::int32_t di = -1; di <= 1; ++di) {
                    const std::int32_t i = i0 + di, j = j0 + dj;
                    if (i < 0 || j < 0 || i >= w || j >= h) continue;
                    const std::size_t k = static_cast<std::size_t>(j) * w + i;
                    if (open[k] == 0 || label[k] >= 0) continue;
                    label[k] = next;
                    todo.push_back(static_cast<std::int32_t>(k));
                }
            }
        }
        ++next;
    }
    return label;
}

std::int32_t PortSea(const std::vector<std::int32_t>& seas, std::int32_t w, std::int32_t h, std::int32_t x,
                     std::int32_t y) {
    const Pt c = CellCentre(x, y);
    const std::int64_t ci = c.x / 96, cj = c.y / 48;
    std::int64_t best = -1;
    std::int32_t out = -1;
    for (std::int64_t j = cj - 6; j <= cj + 6; ++j) {
        for (std::int64_t i = ci - 4; i <= ci + 4; ++i) {
            if (i < 0 || j < 0 || i >= w || j >= h) continue;
            const std::int32_t l = seas[static_cast<std::size_t>(j * w + i)];
            const std::int64_t d = (i - ci) * (i - ci) * 4 + (j - cj) * (j - cj);
            if (l >= 0 && (best < 0 || d < best)) best = d, out = l;
        }
    }
    return out;
}

bool PortOk(const MapData& m, const std::vector<std::uint8_t>& blocked, std::int32_t x, std::int32_t y) {
    const std::int32_t W = m.width, H = m.height;
    auto edge = [&](std::int32_t cx, std::int32_t cy) { return cx <= 3 || cy <= 9 || cx >= W - 3 || cy >= H - 9; };
    if (edge(x, y)) return false;
    const Footprint& f = kPort;
    std::int32_t lo = m.cells[CellIndex(m, x, y)].height, hi = lo, trees = 0, land = 0, water = 0;
    // Box 1: from the cell below, b + 2 rows of a + 2 cells.
    std::int32_t rx = x, ry = y;
    Step(&rx, &ry, 6);
    for (std::int32_t r = 0; r < f.b + 2; ++r) {
        std::int32_t cx = rx, cy = ry;
        for (std::int32_t k = 0; k < f.a + 2; ++k) {
            if (edge(cx, cy)) return false;
            const std::size_t i = CellIndex(m, cx, cy);
            const std::uint16_t o = m.cells[i].object;
            if (o != 0) {
                if (o < 9000 || IsDeposit(o)) return false;
                ++trees;
            }
            (blocked[i] != 0 ? water : land) += 1;
            lo = std::min<std::int32_t>(lo, m.cells[i].height), hi = std::max<std::int32_t>(hi, m.cells[i].height);
            Step(&cx, &cy, 1);
        }
        Step(&rx, &ry, 4);
    }
    if (land < 20 || water < 50 || hi - lo > 50 || trees > 5) return false;
    // Box 2: from (x, y + 6), b + 6 rows of a + 6 cells, no deposit and no edge.
    rx = x, ry = y + 6;
    for (std::int32_t r = 0; r < f.b + 6; ++r) {
        std::int32_t cx = rx, cy = ry;
        for (std::int32_t k = 0; k < f.a + 6; ++k) {
            if (edge(cx, cy) || IsDeposit(m.cells[CellIndex(m, cx, cy)].object)) return false;
            Step(&cx, &cy, 1);
        }
        Step(&rx, &ry, 4);
    }


    std::array<bool, 4> side{true, true, true, true};
    std::int32_t ix = x, iy = y;
    for (std::int32_t i = 0; i < f.a; ++i) {
        std::int32_t cx = ix, cy = iy;
        for (std::int32_t j = 0; j < f.b; ++j) {
            if (i == 0 || i == f.a - 1 || j == 0 || j == f.b - 1) {
                if (!(UnderCell(m, cx, cy) && UnderCell(m, cx + 1, cy))) {
                    if (i == 0) side[0] = false;
                    if (i == f.a - 1) side[1] = false;
                    if (j == 0) side[2] = false;
                    if (j == f.b - 1) side[3] = false;
                }
            }
            Step(&cx, &cy, 4);
        }
        Step(&ix, &iy, 1);
    }
    return side[0] || side[1] || side[2] || side[3];
}

namespace {

// ---- The step. ----

struct Ctx {
    MapData& m;
    std::int32_t W, H;
    Lattice L;
    std::vector<Pt> starts;
    Pt middle;
    Ctx(MapData& map, const MapGenLayout& layout)
        : m(map), W(map.width), H(map.height), L{2 * map.width + 1, map.height + 1} {
        for (const MapGenZone& z : layout.zones) starts.push_back(Pt{z.x, z.y});
        middle = Pt{std::int64_t{W} * 48, std::int64_t{H} * 12};
    }
    // The lattice point nearest to a cell's raised point.
    std::size_t Raised(std::int32_t x, std::int32_t y) const {
        const Pt c = CellCentre(x, y);
        const std::int64_t ry = c.y - m.cells[CellIndex(m, x, y)].height;
        const auto i = static_cast<std::int32_t>(std::clamp<std::int64_t>((c.x + 24) / 48, 0, L.LW - 1));
        const auto j = static_cast<std::int32_t>(std::clamp<std::int64_t>(FloorDiv(ry + 12, 24), 0, L.LH - 1));
        return L.Index(i, j);
    }
};

// A zone's keep (the zone and its margin) on a Sea map reaches up to `more` px farther in places (a slow noise; 450,
// on islands 800: a crowded island is little more than its keeps), so the coast round a start is no circle; its
// nearest point stays at the margin.
std::int64_t KeepReach(std::uint32_t key, Pt p, std::int64_t more = 450) {
    return kZoneR + kMargin + std::max<std::int64_t>(0, Noise(key ^ 0x7f4a7c15u, p.x, p.y, 1500)) * more / 1024;
}

// The land skeleton: the raised points of the cells that must stay land (a zone with its margin, a corridor; on a land
// map within 900 px of the middle). On a Sea map a zone's keep reaches KeepReach.
// `margin`: round a zone (kMargin; a crowded Normal Islands map 100, decision 53).
std::vector<std::uint8_t> Skeleton(const Ctx& c, const MapGenLayout& layout, bool sea, std::uint32_t key,
                                   std::int64_t margin = kMargin) {
    std::vector<std::uint8_t> sk(static_cast<std::size_t>(c.L.LW) * static_cast<std::size_t>(c.L.LH), 0);
    const std::int64_t keep = kZoneR + margin, far = keep + 450;
    for (std::int32_t y = 0; y < c.H; ++y) {
        for (std::int32_t x = 0; x < c.W; ++x) {
            const std::size_t i = CellIndex(c.m, x, y);
            const Pt p = CellCentre(x, y);
            bool k = !layout.corridor.empty() && layout.corridor[i] != 0;
            if (!sea) k = k || Dist2(p, c.middle) <= 900 * 900;
            for (const Pt& s : c.starts) {
                if (k) break;
                const std::int64_t d2 = Dist2(p, s);
                if (d2 <= keep * keep) {
                    k = true;
                } else if (sea && d2 <= far * far) {
                    const std::int64_t reach = KeepReach(key, p) - (kMargin - margin);
                    k = d2 <= reach * reach;
                }
            }
            if (!k) continue;
            // The cell's raised centre and its left and right corners: a row of cells covers every lattice point of
            // its row (the centres alone sit on every other one, and the coast would follow them in a saw).
            const std::size_t r = c.Raised(x, y);
            sk[r] = 1;
            if (r % static_cast<std::size_t>(c.L.LW) > 0) sk[r - 1] = 1;
            if (r % static_cast<std::size_t>(c.L.LW) + 1 < static_cast<std::size_t>(c.L.LW)) sk[r + 1] = 1;
        }
    }
    return sk;
}

// A Sea or Islands map's field: the water drawn before the starts (sim/mapgen_sea.cpp) with a fine wobble on its
// coasts, kept kShoreMin px from the skeleton (the keeps with `margin` round the zones, and the corridors, stay
// land); only the biggest body of water kept. false: less than `least` per mille of the lattice.
bool SeaField(const Ctx& c, const MapGenLayout& layout, const SeaDesign& design, std::int64_t least,
              std::int64_t margin, Rng& r, std::vector<std::int32_t>* field, std::vector<std::int32_t>* dist,
              std::string* why) {
    const Lattice& L = c.L;
    const std::size_t n = static_cast<std::size_t>(L.LW) * static_cast<std::size_t>(L.LH);
    const std::uint32_t k1 = r.next(), k2 = r.next();
    const std::int64_t a = r.range(40, 110);
    *dist = Chamfer(L, Skeleton(c, layout, true, k1, margin));
    field->resize(n);
    for (std::int32_t j = 0; j < L.LH; ++j) {
        for (std::int32_t i = 0; i < L.LW; ++i) {
            const std::int64_t x = std::int64_t{i} * 48, y = std::int64_t{j} * 24;
            const std::size_t k = L.Index(i, j);
            const std::int64_t v = SeaDesignAt(design, x, y) + Noise(k2, x, y, 420) * a / 1024;
            (*field)[k] = static_cast<std::int32_t>(
                std::clamp<std::int64_t>(std::min<std::int64_t>(v, (*dist)[k] - kShoreMin), -kFar, kFar));
        }
    }
    const std::int64_t got = KeepMainWater(L, field) * 1000 / static_cast<std::int64_t>(n);
    if (got >= least) return true;
    *why = "water: no room for the sea (" + std::to_string(got / 10) + " %)";
    return false;
}

// A bay cut toward start s when its nearest water (on the lattice, raised) lies beyond `coast_far`: a channel 520 px
// wide from 1,950 px out toward the nearest water point, kept off the skeleton's shore band.
void Bay(const Ctx& c, const std::vector<std::int32_t>& dist, std::size_t s, std::int64_t coast_far,
         std::vector<std::int32_t>* field, std::int64_t half = 260) {
    const Lattice& L = c.L;
    const Pt st{c.starts[s].x, c.starts[s].y - 50};
    std::int64_t best = -1;
    Pt w{};
    for (std::int32_t j = 0; j < L.LH; ++j) {
        for (std::int32_t i = 0; i < L.LW; ++i) {
            if ((*field)[L.Index(i, j)] <= 0) continue;
            const Pt p{std::int64_t{i} * 48, std::int64_t{j} * 24};
            const std::int64_t d2 = Dist2(p, st);
            if (best < 0 || d2 < best) best = d2, w = p;
        }
    }
    if (best < 0) return;
    const std::int64_t len = ISqrt(best);
    if (len <= coast_far - 150) return;
    const Pt h{st.x + (w.x - st.x) * 1950 / len, st.y + (w.y - st.y) * 1950 / len};
    const std::int64_t vx = w.x - h.x, vy = w.y - h.y, vv = std::max<std::int64_t>(1, vx * vx + vy * vy);
    for (std::int32_t j = 0; j < L.LH; ++j) {
        for (std::int32_t i = 0; i < L.LW; ++i) {
            const std::size_t k = L.Index(i, j);
            if (dist[k] <= kShoreMin) continue;
            const std::int64_t px = std::int64_t{i} * 48 - h.x, py = std::int64_t{j} * 24 - h.y;
            const std::int64_t t = std::clamp<std::int64_t>((px * vx + py * vy) * 1024 / vv, 0, 1024);
            const std::int64_t dx = px - vx * t / 1024, dy = py - vy * t / 1024;
            const std::int64_t v = half - ISqrt(dx * dx + dy * dy);
            if (v > (*field)[k]) (*field)[k] = static_cast<std::int32_t>(v);
        }
    }
}

// Lakes on a land map: 0 to 4 (Large 0 to 6, at most `most`) noisy round blobs of 300 to 2,400 cells, on low ground
// (every cell within their reach at 62 or lower), 250 px or more beyond the skeleton's shore band, apart from each
// other. On a Sea map (`sea_dist`: the distance from the sea, null on a land map) 0 to 2 lakes, each 700 px or more
// inland. The count.
int LakeField(const Ctx& c, const MapGenLayout& layout, bool large, Rng& r, std::vector<std::int32_t>* field,
              const std::vector<std::int32_t>* sea_dist = nullptr, int most = 99) {
    const Lattice& L = c.L;
    const std::size_t n = static_cast<std::size_t>(L.LW) * static_cast<std::size_t>(L.LH);
    field->assign(n, -1);
    const std::vector<std::int32_t> dist = Chamfer(L, Skeleton(c, layout, false, 0));
    static constexpr int kCount[8] = {0, 1, 1, 2, 2, 3, 3, 4};
    int lakes = sea_dist != nullptr ? static_cast<int>(r.below(3)) : kCount[r.below(8)];
    if (large && sea_dist == nullptr) lakes += static_cast<int>(r.below(3));
    lakes = std::min(lakes, most);
    int made = 0;
    std::vector<std::pair<Pt, std::int64_t>> placed;  // centre (raised world), reach (ground px)
    for (int l = 0; l < lakes; ++l) {
        const std::int64_t cells = r.range(300, 2400);
        const std::int64_t rad = ISqrt(cells * 4608 * 1000 / 3142);  // ground px (a cell is 4,608 ground px^2)
        const std::uint32_t warp = r.next();
        for (int t = 0; t < 75; ++t) {
            const Pt cen{r.range(kBandX, std::int64_t{c.W} * 96 - kBandX),
                         r.range(kBandY, std::int64_t{c.H} * 24 - kBandY)};
            const std::int64_t reach = rad * 13 / 10;
            bool ok = true;
            for (const auto& [o, orad] : placed) {
                const std::int64_t dx = cen.x - o.x, dy = 2 * (cen.y - o.y), gap = reach + orad + 600;
                ok = ok && dx * dx + dy * dy > gap * gap;
            }
            // The lattice points within reach: off the skeleton's band.
            const std::int64_t ri = reach / 48 + 1, rj = reach / 48 + 1;
            const std::int64_t ci = cen.x / 48, cj = cen.y / 24;
            for (std::int64_t j = cj - rj; j <= cj + rj && ok; ++j) {
                for (std::int64_t i = ci - ri; i <= ci + ri && ok; ++i) {
                    if (i < 0 || j < 0 || i >= L.LW || j >= L.LH) {
                        ok = false;
                        continue;
                    }
                    const std::int64_t dx = i * 48 - cen.x, dy = 2 * (j * 24 - cen.y);
                    if (dx * dx + dy * dy > reach * reach) continue;
                    const std::size_t k = L.Index(static_cast<std::int32_t>(i), static_cast<std::int32_t>(j));
                    ok = dist[k] > kShoreMin + 250 && (sea_dist == nullptr || (*sea_dist)[k] > 700);
                }
            }
            // Low ground: the cells whose raised point lies within reach (and two cells around).
            const std::int64_t cx = cen.x / 96, cy = (cen.y + 50) / 24, rx = reach / 96 + 3, ry = reach / 48 + 5;
            for (std::int64_t y = cy - ry; y <= cy + ry && ok; ++y) {
                for (std::int64_t x = cx - rx; x <= cx + rx && ok; ++x) {
                    if (x < 0 || y < 0 || x >= c.W || y >= c.H) continue;
                    const Pt p = CellCentre(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
                    const std::int32_t h =
                        c.m.cells[CellIndex(c.m, static_cast<std::int32_t>(x), static_cast<std::int32_t>(y))].height;
                    const std::int64_t dx = p.x - cen.x, dy = 2 * (p.y - h - cen.y), lim = reach + 200;
                    if (dx * dx + dy * dy <= lim * lim) ok = h <= 62;
                }
            }
            if (!ok) continue;
            for (std::int64_t j = cj - rj; j <= cj + rj; ++j) {
                for (std::int64_t i = ci - ri; i <= ci + ri; ++i) {
                    const std::size_t k = L.Index(static_cast<std::int32_t>(i), static_cast<std::int32_t>(j));
                    const std::int64_t x = i * 48, y = j * 24, dx = x - cen.x, dy = 2 * (y - cen.y);
                    const std::int64_t d = ISqrt(dx * dx + dy * dy);
                    const std::int64_t rr = rad * (1024 + Noise(warp, x, y, 700) * 28 / 100) / 1024;
                    const std::int64_t v = std::min(rr - d, std::int64_t{dist[k]} - kShoreMin);
                    if (v > (*field)[k]) (*field)[k] = static_cast<std::int32_t>(v);
                }
            }
            placed.emplace_back(cen, reach);
            ++made;
            break;
        }
    }
    return made;
}

// The fairness of a neutral island's place (per mille): the straight distance to the nearest start of each island
// (islands) or to every start (a Sea map), smallest over largest.
std::int64_t IsleFair(const Ctx& c, const MapGenLayout& layout, Pt p) {
    std::vector<std::int64_t> near;
    for (std::size_t s = 0; s < c.starts.size(); ++s) {
        const std::int64_t d = ISqrt(Dist2(p, c.starts[s]));
        const std::size_t who = layout.island.empty() ? s : static_cast<std::size_t>(layout.island[s]);
        if (who >= near.size()) near.resize(who + 1, -1);
        if (near[who] < 0 || d < near[who]) near[who] = d;
    }
    std::int64_t lo = -1, hi = 1;
    for (std::int64_t d : near) lo = lo < 0 || d < lo ? d : lo, hi = std::max(hi, d);
    return lo * 1000 / hi;
}

// Small neutral islands (decision 53) holding a free deposit group that only ships reach: on a Sea map 0 or 1,
// between islands 0 to 2, each where the water is deep (900 px of sea round it), at a place about as far from every
// start (a Sea map, 0.75) or from every island's nearest start (0.85), the second 3,500 px from the first. Their
// land is lowered into the field; their places go to `isles`.
void Isles(const Ctx& c, const MapGenLayout& layout, Rng& r, std::vector<std::int32_t>* field,
           std::vector<std::array<std::int32_t, 3>>* isles) {
    const Lattice& L = c.L;
    const std::uint32_t want = layout.pattern == 1 ? r.below(2) : layout.pattern == 4 ? r.below(3) : 0;
    const std::int64_t rad = r.range(480, 620);
    const std::uint32_t warp = r.next();
    if (want == 0) return;
    std::vector<std::uint8_t> land(field->size(), 0);
    for (std::size_t k = 0; k < field->size(); ++k) land[k] = (*field)[k] <= 0 ? 1 : 0;
    const std::vector<std::int32_t> from_land = Chamfer(L, land);
    const std::int64_t need = layout.pattern == 1 ? 750 : 850, wpx = std::int64_t{c.W} * 96;
    const std::int64_t hpx = std::int64_t{c.H} * 24;
    for (std::uint32_t q = 0; q < want; ++q) {
        std::vector<std::pair<std::int64_t, std::size_t>> cand;  // (fairness, lattice point)
        std::int64_t best = 0;
        for (std::int32_t j = 0; j < L.LH; j += 4) {
            for (std::int32_t i = 0; i < L.LW; i += 2) {
                const std::size_t k = L.Index(i, j);
                const Pt p{std::int64_t{i} * 48, std::int64_t{j} * 24};
                if (from_land[k] < rad + 900 || p.x < kBandX || p.x > wpx - kBandX || p.y < kBandY + 300 ||
                    p.y > hpx - kBandY - 300)
                    continue;
                bool apart = true;
                for (const auto& o : *isles) apart = apart && Dist2(p, Pt{o[0], o[1]}) >= 3500 * 3500;
                if (!apart) continue;
                const std::int64_t fair = IsleFair(c, layout, p);
                if (fair < need) continue;
                cand.emplace_back(fair, k);
                best = std::max(best, fair);
            }
        }
        std::vector<std::size_t> pick;
        for (const auto& [fair, k] : cand) {
            if (fair >= best - 30) pick.push_back(k);
        }
        if (pick.empty()) return;
        const std::size_t k0 = pick[r.below(static_cast<std::uint32_t>(pick.size()))];
        const Pt cen{static_cast<std::int64_t>(k0 % static_cast<std::size_t>(L.LW)) * 48,
                     static_cast<std::int64_t>(k0 / static_cast<std::size_t>(L.LW)) * 24};
        const std::int64_t ri = (rad + 300) / 48 + 1, rj = (rad + 300) / 24 + 1;
        const std::int64_t ci = cen.x / 48, cj = cen.y / 24;
        for (std::int64_t j = cj - rj; j <= cj + rj; ++j) {
            for (std::int64_t i = ci - ri; i <= ci + ri; ++i) {
                if (i < 0 || j < 0 || i >= L.LW || j >= L.LH) continue;
                const std::size_t k = L.Index(static_cast<std::int32_t>(i), static_cast<std::int32_t>(j));
                const std::int64_t x = i * 48, y = j * 24;
                const std::int64_t d = ISqrt(Dist2(Pt{x, y}, cen));
                const std::int64_t rr = rad * (1024 + Noise(warp, x, y, 600) * 22 / 100) / 1024;
                const std::int64_t v = d - rr;
                if (v < (*field)[k]) (*field)[k] = static_cast<std::int32_t>(v);
            }
        }
        // The island's middle as raised by the plain (the cells sample the field 40 px above their centre).
        isles->push_back({static_cast<std::int32_t>(cen.x), static_cast<std::int32_t>(cen.y + kRaise + kPlain - 50),
                          static_cast<std::int32_t>(rad)});
    }
}

// An archipelago (a Sea map's look, 2026-11-06): with an even chance 3 to 6 islets of 200 to 380 px (Large 3 to 8) in
// deep water (700 px of sea round each), 1,500 px apart and from the neutral islands; no deposits, no port, no player
// land: only their look. The count made.
int Islets(const Ctx& c, Rng& r, const std::vector<std::array<std::int32_t, 3>>& isles,
           std::vector<std::int32_t>* field) {
    const Lattice& L = c.L;
    const std::uint32_t draw = r.next();
    const std::uint32_t warp = r.next();
    if ((draw & 1) == 0) return 0;
    const int want = 3 + static_cast<int>((draw >> 1) % (c.W > 200 ? 6u : 4u));
    std::vector<std::uint8_t> land(field->size(), 0);
    for (std::size_t k = 0; k < field->size(); ++k) land[k] = (*field)[k] <= 0 ? 1 : 0;
    const std::vector<std::int32_t> from_land = Chamfer(L, land);
    const std::int64_t wpx = std::int64_t{c.W} * 96, hpx = std::int64_t{c.H} * 24;
    std::vector<Pt> placed;
    for (const auto& o : isles) placed.push_back(Pt{o[0], o[1]});
    int made = 0;
    for (int q = 0; q < want; ++q) {
        const std::int64_t rad = r.range(200, 380);
        std::vector<std::size_t> cand;
        for (std::int32_t j = 0; j < L.LH; j += 4) {
            for (std::int32_t i = 0; i < L.LW; i += 2) {
                const std::size_t k = L.Index(i, j);
                const Pt p{std::int64_t{i} * 48, std::int64_t{j} * 24};
                if (from_land[k] < rad + 700 || p.x < kBandX || p.x > wpx - kBandX || p.y < kBandY + 300 ||
                    p.y > hpx - kBandY - 300)
                    continue;
                bool apart = true;
                for (const Pt& o : placed) apart = apart && Dist2(p, o) >= 1500 * 1500;
                if (apart) cand.push_back(k);
            }
        }
        if (cand.empty()) break;
        const std::size_t k0 = cand[r.below(static_cast<std::uint32_t>(cand.size()))];
        const Pt cen{static_cast<std::int64_t>(k0 % static_cast<std::size_t>(L.LW)) * 48,
                     static_cast<std::int64_t>(k0 / static_cast<std::size_t>(L.LW)) * 24};
        const std::int64_t ri = (rad + 200) / 48 + 1, rj = (rad + 200) / 24 + 1;
        for (std::int64_t j = cen.y / 24 - rj; j <= cen.y / 24 + rj; ++j) {
            for (std::int64_t i = cen.x / 48 - ri; i <= cen.x / 48 + ri; ++i) {
                if (i < 0 || j < 0 || i >= L.LW || j >= L.LH) continue;
                const std::size_t k = L.Index(static_cast<std::int32_t>(i), static_cast<std::int32_t>(j));
                const std::int64_t x = i * 48, y = j * 24;
                const std::int64_t rr = rad * (1024 + Noise(warp, x, y, 400) * 30 / 100) / 1024;
                const std::int64_t v = ISqrt(Dist2(Pt{x, y}, cen)) - rr;
                if (v < (*field)[k]) (*field)[k] = static_cast<std::int32_t>(v);
            }
        }
        placed.push_back(cen);
        ++made;
    }
    return made;
}

// The cells' heights from the field: water (the field 40 px above the centre) at 30, the land within a few lattice
// steps of it lowered to 50, 54, then 16 more per step (never raised), then the grid and LoadMap's rule.
bool Shape(Ctx& c, const std::vector<std::int32_t>& field) {
    MapData& m = c.m;
    const std::size_t n = m.cells.size();
    std::vector<std::uint8_t> design(n, 0);
    for (std::int32_t y = 0; y < c.H; ++y) {
        for (std::int32_t x = 0; x < c.W; ++x) {
            const Pt p = CellCentre(x, y);
            design[CellIndex(m, x, y)] = c.L.Sample(field, p.x, p.y - kRaise) > 0 ? 1 : 0;
        }
    }
    // Lattice steps (directions 1 to 4) from the water.
    std::vector<std::int32_t> d(n, kFar);
    std::vector<std::int32_t> todo;
    for (std::size_t i = 0; i < n; ++i) {
        if (design[i] != 0) d[i] = 0, todo.push_back(static_cast<std::int32_t>(i));
    }
    for (std::size_t t = 0; t < todo.size(); ++t) {
        const std::int32_t u = todo[t];
        if (d[static_cast<std::size_t>(u)] >= 12) continue;
        for (int dir = 1; dir <= 4; ++dir) {
            std::int32_t x = u % c.W, y = u / c.W;
            Step(&x, &y, dir);
            if (x < 0 || y < 0 || x >= c.W || y >= c.H || d[CellIndex(m, x, y)] != kFar) continue;
            d[CellIndex(m, x, y)] = d[static_cast<std::size_t>(u)] + 1;
            todo.push_back(static_cast<std::int32_t>(CellIndex(m, x, y)));
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        std::uint8_t& h = m.cells[i].height;
        if (d[i] == 0) {
            h = kUnder;
        } else if (d[i] < kFar) {
            const std::int32_t cap = d[i] == 1 ? 50 : 54 + 16 * (d[i] - 2);
            h = static_cast<std::uint8_t>(std::min<std::int32_t>(h, cap));
        }
    }
    if (!CoastRecords(field, c.W, c.H, &m.obstacle_grid, &m.routes)) return false;

    std::vector<std::uint8_t> under(n, 0);
    for (std::int32_t y = 0; y < c.H; ++y) {
        for (std::int32_t x = 0; x < c.W; ++x) under[CellIndex(m, x, y)] = UnderCell(m, x, y) ? 1 : 0;
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (under[i] != 0) m.cells[i].height = kUnder;
    }
    return true;
}

// Every start and (on a land map) the middle on foot (span 60) and by tanks (span 25), off the water,
// around the objects; on islands every start of an island from its first.
std::string Reach(const Ctx& c, const std::vector<std::uint8_t>& blocked, bool middle_too,
                  const std::vector<std::int8_t>& island) {
    const MapData& m = c.m;
    std::vector<std::int32_t> start_cells;
    for (const Pt& s : c.starts) {
        std::int32_t x = 0, y = 0;
        CellAt(s, &x, &y);
        start_cells.push_back(y * c.W + x);
    }
    std::int32_t mx = 0, my = 0;
    CellAt(c.middle, &mx, &my);
    for (const int limit : {60, 25}) {
        std::vector<std::uint8_t> seen(m.cells.size(), 0);
        auto ok = [&](std::int32_t x, std::int32_t y) {
            const std::size_t i = CellIndex(m, x, y);
            if (blocked[i] != 0 || BlocksWalk(m.cells[i].object)) return false;
            const int s = Span(m, x, y);
            return s >= 0 && s <= limit;
        };
        const char* who = limit == 60 ? "on foot" : "by tanks";
        for (std::size_t s0 = 0; s0 < start_cells.size(); ++s0) {
            // The first start of each island (every start's on a map without islands: start 1).
            if (island.empty() ? s0 > 0 : s0 > 0 && island[s0 - 1] == island[s0]) continue;
            std::fill(seen.begin(), seen.end(), std::uint8_t{0});
            std::vector<std::int32_t> todo{start_cells[s0]};
            seen[static_cast<std::size_t>(start_cells[s0])] = 1;
            for (std::size_t t = 0; t < todo.size(); ++t) {
                for (int dir = 1; dir <= 8; ++dir) {
                    std::int32_t x = todo[t] % c.W, y = todo[t] / c.W;
                    Step(&x, &y, dir);
                    if (x < 0 || y < 0 || x >= c.W || y >= c.H || seen[CellIndex(m, x, y)] != 0 || !ok(x, y)) continue;
                    seen[CellIndex(m, x, y)] = 1;
                    todo.push_back(y * c.W + x);
                }
            }
            for (std::size_t s = 0; s < start_cells.size(); ++s) {
                if (!island.empty() && island[s] != island[s0]) continue;
                if (seen[static_cast<std::size_t>(start_cells[s])] == 0)
                    return std::string("water: start ") + std::to_string(s + 1) + " cut off " + who;
            }
        }
        bool middle = !middle_too;
        for (std::int32_t y = my - 20; y <= my + 20 && !middle; ++y) {
            for (std::int32_t x = mx - 5; x <= mx + 5 && !middle; ++x)
                middle = x >= 0 && y >= 0 && x < c.W && y < c.H && seen[CellIndex(m, x, y)] != 0;
        }
        if (!middle) return std::string("water: the middle cut off ") + who;
    }
    return "";
}

// The straight distance from start s to its nearest water cell (-1: none within coast_far + 200 px).
std::int32_t CoastDistance(const MapData& m, Pt s, const std::vector<std::uint8_t>& water, std::int64_t coast_far) {
    std::int32_t sx = 0, sy = 0;
    CellAt(s, &sx, &sy);
    std::int64_t best = -1;
    const auto ry = static_cast<std::int32_t>(coast_far / 24 + 10), rx = static_cast<std::int32_t>(coast_far / 96 + 3);
    for (std::int32_t y = std::max(0, sy - ry); y < std::min(m.height, sy + ry + 1); ++y) {
        for (std::int32_t x = std::max(0, sx - rx); x < std::min(m.width, sx + rx + 1); ++x) {
            if (water[CellIndex(m, x, y)] == 0) continue;
            const std::int64_t d2 = Dist2(CellCentre(x, y), s);
            if (d2 <= (coast_far + 200) * (coast_far + 200) && (best < 0 || d2 < best)) best = d2;
        }
    }
    return best < 0 ? -1 : static_cast<std::int32_t>(ISqrt(best));
}

// The land path graph: walking cells off the water and free of blocking objects, span 60.
PathGraph LandGraph(const MapData& m, const std::vector<std::uint8_t>& blocked) {
    std::vector<std::uint8_t> walk(m.cells.size(), 0);
    for (std::int32_t y = 0; y < m.height; ++y) {
        for (std::int32_t x = 0; x < m.width; ++x) {
            const std::size_t i = CellIndex(m, x, y);
            const int sp = Span(m, x, y);
            walk[i] = blocked[i] == 0 && !BlocksWalk(m.cells[i].object) && sp >= 0 && sp <= 60 ? 1 : 0;
        }
    }
    return MakePathGraph(walk, m.width, m.height);
}

// The land path distances from a start up to `bound`.
std::vector<std::int32_t> LandPaths(const MapData& m, const PathGraph& pg, Pt s, std::int32_t bound) {
    std::int32_t x = 0, y = 0;
    CellAt(s, &x, &y);
    std::vector<std::int32_t> dist;
    PathDistances(pg, {y * m.width + x}, bound, &dist);
    return dist;
}

// The path to a port site: to the nearest reached cell of its box 1.
std::int32_t PortPath(const MapData& m, const std::vector<std::int32_t>& dist, std::int32_t x, std::int32_t y) {
    std::vector<std::int32_t> box;
    Box1(m.width, m.height, x, y, kPort, &box);
    std::int32_t best = -1;
    for (std::int32_t i : box) {
        const std::int32_t d = dist[static_cast<std::size_t>(i)];
        if (d >= 0 && (best < 0 || d < best)) best = d;
    }
    return best;
}

}  // namespace

std::string Water(MapData& m, const MapGenSettings& g, MapGenLayout* layout_out, std::uint64_t key,
                  const SeaDesign* design, int lakes_most) {
    const MapGenLayout& layout = *layout_out;
    Ctx c(m, layout);
    Rng r{key};
    std::vector<std::int32_t> field;
    layout_out->isles.clear();
    if (g.sea) {
        std::vector<std::int32_t> dist;
        std::string why;
        if (design == nullptr) return "water: no sea design";
        // Islands keep LandMap's third of water; a crowded Normal Islands map (6 or more players) keeps 100 px of
        // margin round a zone instead of 300 (decision 53, 2026-11-07: a slightly smaller start area for everyone).
        const bool islands = layout.pattern == 4;
        const std::int64_t margin = islands && c.W <= 200 && c.starts.size() >= 6 ? 100 : kMargin;
        if (!SeaField(c, layout, *design, islands ? kIslesLeast : kSeaLeast, margin, r, &field, &dist, &why))
            return why;
        // A bay wide enough for ships toward a start whose coast lies too far (rare: the starts were fitted to the
        // sea; the reach follows the band, decision 57).
        for (std::size_t s = 0; s < c.starts.size(); ++s) Bay(c, dist, s, CoastFar(layout), &field, 560);
        KeepMainWater(c.L, &field);
        Isles(c, layout, r, &field, &layout_out->isles);
        // Lakes inland (0 to 2), 700 px or more from the sea.
        std::vector<std::uint8_t> wet(field.size(), 0);
        for (std::size_t k = 0; k < field.size(); ++k) wet[k] = field[k] > 0 ? 1 : 0;
        const std::vector<std::int32_t> sea_dist = Chamfer(c.L, wet);
        std::vector<std::int32_t> lake;
        const int lakes = LakeField(c, layout, g.size == 1, r, &lake, &sea_dist);
        for (std::size_t k = 0; k < field.size(); ++k) field[k] = std::max(field[k], lake[k]);
        const int islets = layout.pattern == 4 ? 0 : Islets(c, r, layout.isles, &field);
        char buf[96];
        std::snprintf(buf, sizeof buf, ", %zu neutral islands, %d lakes", layout.isles.size(), lakes);
        layout_out->shape += buf;
        if (islets > 0) {
            std::snprintf(buf, sizeof buf, ", an archipelago of %d islets", islets);
            layout_out->shape += buf;
        }
    } else if (design != nullptr) {
        // The ponds of a land map (decision 56; drawn before the starts, sim/mapgen_land.cpp) with a fine wobble on
        // their shores, kept kShoreMin px from the skeleton (the zones with their margins, the corridors, the
        // middle); every pond stays (no main water).
        const Lattice& L = c.L;
        const std::size_t n = static_cast<std::size_t>(L.LW) * static_cast<std::size_t>(L.LH);
        const std::vector<std::int32_t> dist = Chamfer(L, Skeleton(c, layout, false, 0));
        const std::uint32_t k2 = r.next();
        const std::int64_t a = r.range(40, 110);
        field.resize(n);
        for (std::int32_t j = 0; j < L.LH; ++j) {
            for (std::int32_t i = 0; i < L.LW; ++i) {
                const std::int64_t x = std::int64_t{i} * 48, y = std::int64_t{j} * 24;
                const std::size_t k = L.Index(i, j);
                const std::int64_t v = SeaDesignAt(*design, x, y) + Noise(k2, x, y, 420) * a / 1024;
                field[k] = static_cast<std::int32_t>(
                    std::clamp<std::int64_t>(std::min<std::int64_t>(v, dist[k] - kShoreMin), -kFar, kFar));
            }
        }
    } else {
        LakeField(c, layout, g.size == 1, r, &field, nullptr, lakes_most);
    }
    if (!Shape(c, field)) return "water: too many coast records";
    const std::vector<std::uint8_t> blocked = BlockedCells(m), water = WaterCells(m);
    for (std::size_t i = 0; i < m.cells.size(); ++i) {
        if (water[i] == 0) continue;
        if (m.cells[i].object != 0) return "water: over an object of a zone";
        m.cells[i].object = kWaterMark;
    }
    std::string why = Reach(c, blocked, layout.pattern == 0, layout.island);
    if (!why.empty()) return why;
    if (g.sea) {
        for (std::size_t s = 0; s < c.starts.size(); ++s) {
            const std::int32_t d = CoastDistance(m, c.starts[s], water, CoastFar(layout));
            if (d < 0 || d > CoastFar(layout)) return "water: no coast near start " + std::to_string(s + 1);
        }
        std::int64_t full = 0;
        for (std::uint16_t v : m.obstacle_grid) full += v == 1 ? 1 : 0;
        const std::int64_t least = layout.pattern == 4 ? kIslesLeast : kSeaLeast;
        if (full * 1000 < static_cast<std::int64_t>(m.obstacle_grid.size()) * least) return "water: too little sea";
    }
    return "";
}

std::string Ports(MapData& m, const MapGenSettings& g, const MapGenLayout& layout, std::vector<std::uint8_t>* water) {
    for (MapCell& cell : m.cells) {
        if (cell.object == kWaterMark) cell.object = 0;
    }
    if (water != nullptr) *water = WaterCells(m);
    m.port_sites.clear();
    if (!g.sea) return "";
    const std::vector<std::uint8_t> blocked = BlockedCells(m);
    const std::size_t n = layout.zones.size();
    std::vector<Pt> starts;
    for (const MapGenZone& z : layout.zones) starts.push_back(Pt{z.x, z.y});
    // A candidate: the port rule with its footprint's trees, stones and decoration felled; box 1 off every zone.
    auto candidate = [&](std::int32_t x, std::int32_t y) {
        std::vector<std::int32_t> box;
        if (!Box1(m.width, m.height, x, y, kPort, &box)) return false;
        for (std::int32_t i : box) {
            const Pt p = CellCentre(i % m.width, i / m.width);
            for (const Pt& s : starts) {
                if (Dist2(p, s) <= (kZoneR + 100) * (kZoneR + 100)) return false;
            }
            if (IsDeposit(m.cells[static_cast<std::size_t>(i)].object)) return false;
        }
        std::vector<std::uint16_t> kept;
        for (std::int32_t i : box) kept.push_back(m.cells[static_cast<std::size_t>(i)].object);
        for (std::int32_t i : box) {
            std::uint16_t& o = m.cells[static_cast<std::size_t>(i)].object;
            if (o >= 9000 && !IsDeposit(o)) o = 0;
        }
        const bool ok = PortOk(m, blocked, x, y);
        for (std::size_t k = 0; k < box.size(); ++k) m.cells[static_cast<std::size_t>(box[k])].object = kept[k];
        return ok;
    };

    const std::int32_t sw = m.width, sh = m.height / 2;
    const std::vector<std::int32_t> seas = ShipSeas(ShipCells(m), sw, sh);
    std::vector<std::int32_t> sizes;
    for (std::int32_t l : seas) {
        if (l < 0) continue;
        if (static_cast<std::size_t>(l) >= sizes.size()) sizes.resize(static_cast<std::size_t>(l) + 1, 0);
        ++sizes[static_cast<std::size_t>(l)];
    }
    if (sizes.empty()) return "ports: no open water for ships";
    const auto main_sea = static_cast<std::int32_t>(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
    std::vector<std::array<std::int32_t, 2>> chosen;
    std::vector<std::int32_t> nearest(n, -1);
    const PathGraph pg = LandGraph(m, blocked);
    // Per start its land paths and its candidates (path, cell) in order of path; the nearest site each start could
    // take sets a least path (kSeaAccess plus 3 % of the farthest of them), and no start takes a site nearer than
    // that, so the starts' nearest sites keep the ratio of kSeaAccess below (decision 54 e: 0.67; g: 0.50 on Normal
    // Sea with 7 or 8 players).
    // Normal Sea with 7 or 8 players: 0.50 (decision 54 g; its coast band is 1,600 to 3,200 px). The reach follows
    // the band (decision 57).
    const std::int64_t port_far = PortFar(layout);
    const std::int64_t access = layout.pattern == 1 && m.width <= 200 && n >= 7 ? 50 : kSeaAccess;
    std::vector<std::vector<std::int32_t>> dists(n);
    std::vector<std::vector<std::pair<std::int32_t, std::int32_t>>> cand_of(n);
    std::int32_t least = 0;
    for (std::size_t s = 0; s < n; ++s) {
        dists[s] = LandPaths(m, pg, starts[s], static_cast<std::int32_t>(port_far) + 400);
        const std::vector<std::int32_t>& dist = dists[s];
        std::vector<std::pair<std::int32_t, std::int32_t>>& cands = cand_of[s];
        std::int32_t sx = 0, sy = 0;
        CellAt(starts[s], &sx, &sy);
        const auto ry = static_cast<std::int32_t>(port_far / 24 + 20),
                   rx = static_cast<std::int32_t>(port_far / 96 + 5);
        for (std::int32_t y = std::max(0, sy - ry); y < std::min(m.height, sy + ry + 1); ++y) {
            for (std::int32_t x = std::max(0, sx - rx); x < std::min(m.width, sx + rx + 1); ++x) {
                const std::size_t i = CellIndex(m, x, y);
                if (Dist2(CellCentre(x, y), starts[s]) > (port_far + 300) * (port_far + 300)) continue;
                bool near = false;  // only cells within 4 rows and 2 cells of the water
                for (std::int32_t yy = y - 8; yy <= y + 8 && !near; yy += 2) {
                    for (std::int32_t xx = x - 2; xx <= x + 2 && !near; ++xx)
                        near = xx >= 0 && yy >= 0 && xx < m.width && yy < m.height && blocked[CellIndex(m, xx, yy)];
                }
                if (!near) continue;
                const std::int32_t d = PortPath(m, dist, x, y);
                if (d < 0 || d > port_far) continue;
                cands.emplace_back(d, static_cast<std::int32_t>(i));
            }
        }
        std::sort(cands.begin(), cands.end());
        for (const auto& [d, i] : cands) {
            if (PortSea(seas, sw, sh, i % m.width, i / m.width) != main_sea || !candidate(i % m.width, i / m.width))
                continue;
            least = std::max(least, static_cast<std::int32_t>(d * (access + 3) / 100));
            break;
        }
    }
    for (std::size_t s = 0; s < n; ++s) {
        const std::vector<std::int32_t>& dist = dists[s];
        const std::vector<std::pair<std::int32_t, std::int32_t>>& cands = cand_of[s];
        // Sites an earlier start took count for this one too when its land reaches them.
        int got = 0;
        std::vector<std::uint8_t> mine(chosen.size(), 0);  // the earlier sites that count for this start
        for (std::size_t k = 0; k < chosen.size(); ++k) {
            const std::int32_t d = PortPath(m, dist, chosen[k][0], chosen[k][1]);
            if (d < 0 || d > port_far) continue;
            if (nearest[s] < 0 || d < nearest[s]) nearest[s] = d;
            mine[k] = 1;
            ++got;
        }
        for (const auto& [d, i] : cands) {
            if (got == 3) break;
            if (d < least) continue;
            const std::int32_t x = i % m.width, y = i / m.width;
            bool apart = true;
            // 600 px from this start's own sites, 300 from the others' (alternatives, not duplicates).
            for (std::size_t k = 0; k < chosen.size(); ++k) {
                const std::int64_t gap = k < mine.size() && mine[k] == 0 ? 300 : 600;
                apart = apart && Dist2(CellCentre(x, y), CellCentre(chosen[k][0], chosen[k][1])) >= gap * gap;
            }
            if (!apart || PortSea(seas, sw, sh, x, y) != main_sea || !candidate(x, y)) continue;
            chosen.push_back({x, y});
            if (nearest[s] < 0 || d < nearest[s]) nearest[s] = d;
            ++got;
        }
        if (got < 2) return "ports: " + std::to_string(got) + " port sites near start " + std::to_string(s + 1);
    }
    const std::int32_t lo = *std::min_element(nearest.begin(), nearest.end());
    const std::int32_t hi = *std::max_element(nearest.begin(), nearest.end());
    if (lo * 100 < hi * access) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "ports: sea access %.2f", static_cast<double>(lo) / hi);
        return buf;
    }
    for (const auto& o : chosen) {
        std::vector<std::int32_t> box;
        Box1(m.width, m.height, o[0], o[1], kPort, &box);
        for (std::int32_t i : box) {
            std::uint16_t& obj = m.cells[static_cast<std::size_t>(i)].object;
            if (obj >= 9000 && !IsDeposit(obj)) obj = 0;
        }
        m.port_sites.push_back(static_cast<std::uint32_t>(o[0]) | static_cast<std::uint32_t>(o[1]) << 16);
    }
    return "";
}

}  // namespace mapgen

MapGenWaterStats MapGen_WaterStats(const MapData& m, const MapGenLayout& layout) {
    using namespace mapgen;
    MapGenWaterStats s;
    std::int64_t full = 0;
    for (std::uint16_t v : m.obstacle_grid) full += v != 0 && (v & 0x8000) == 0 ? 1 : 0;
    s.blocked_share = m.obstacle_grid.empty()
                          ? 0
                          : static_cast<std::int32_t>(full * 1000 / static_cast<std::int64_t>(m.obstacle_grid.size()));
    s.coast_records = static_cast<std::int32_t>(m.routes.size());
    s.ports = static_cast<std::int32_t>(m.port_sites.size());
    const std::vector<std::uint8_t> water = WaterCells(m), blocked = BlockedCells(m);
    {  // water bodies that keep off the map's edge (10 cells, 20 rows), a Sea map's inland sea (its biggest) not
       // counted
        std::vector<std::uint8_t> seen(water.size(), 0);
        std::size_t biggest = 0;
        bool biggest_inland = false;
        for (std::size_t i0 = 0; i0 < water.size(); ++i0) {
            if (water[i0] == 0 || seen[i0] != 0) continue;
            bool edge = false;
            std::vector<std::int32_t> todo{static_cast<std::int32_t>(i0)};
            seen[i0] = 1;
            for (std::size_t t = 0; t < todo.size(); ++t) {
                const std::int32_t ux = todo[t] % m.width, uy = todo[t] / m.width;
                edge = edge || ux < 10 || uy < 20 || ux >= m.width - 10 || uy >= m.height - 20;
                for (int dir = 1; dir <= 8; ++dir) {
                    std::int32_t x = ux, y = uy;
                    Step(&x, &y, dir);
                    if (x < 0 || y < 0 || x >= m.width || y >= m.height) continue;
                    const std::size_t v = static_cast<std::size_t>(y) * m.width + x;
                    if (water[v] == 0 || seen[v] != 0) continue;
                    seen[v] = 1;
                    todo.push_back(static_cast<std::int32_t>(v));
                }
            }
            s.lakes += edge ? 0 : 1;
            if (todo.size() > biggest) biggest = todo.size(), biggest_inland = !edge;
        }
        if (layout.pattern == 1 && biggest_inland) --s.lakes;
    }
    const PathGraph pg = LandGraph(m, blocked);
    s.port_far = static_cast<std::int32_t>(PortFar(layout));
    for (const MapGenZone& z : layout.zones) {
        const Pt st{z.x, z.y};
        s.coast.push_back(CoastDistance(m, st, water, CoastFar(layout)));
        const std::vector<std::int32_t> dist = LandPaths(m, pg, st, s.port_far + 200);
        std::int32_t best = -1, near = 0;
        for (std::uint32_t v : m.port_sites) {
            const auto x = static_cast<std::int32_t>(v & 0xffff), y = static_cast<std::int32_t>(v >> 16);
            const std::int32_t d = PortPath(m, dist, x, y);
            if (d < 0) continue;
            best = best < 0 || d < best ? d : best;
            near += d <= s.port_far && PortOk(m, blocked, x, y) ? 1 : 0;
        }
        s.port.push_back(best);
        s.ports_near.push_back(near);
    }
    return s;
}

std::string MapGen_WaterStatsText(const MapGenWaterStats& s) {
    char buf[256];
    std::snprintf(buf, sizeof buf, "water: %.1f %% of the grid, %d lakes, %d coast records, %d port sites\n",
                  s.blocked_share / 10.0, s.lakes, s.coast_records, s.ports);
    std::string out = buf;
    for (std::size_t p = 0; p < s.coast.size(); ++p) {
        std::snprintf(buf, sizeof buf, "  start %zu: coast %d px, nearest port %d px by land, %d ports within %lld\n",
                      p + 1, s.coast[p], s.port[p], s.ports_near[p], static_cast<long long>(s.port_far));
        out += buf;
    }
    return out;
}

}  // namespace antanta::sim
