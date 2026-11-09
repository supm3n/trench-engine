// The random map generator's checks and report (docs/map-generator-plan.md 5.10, 5.12). Two levels: MapGen_Check
// with the generator's fixed rules (it decides which attempt is kept, so it reads no game file), and MapGen_Verify
// with the engine's own code on a started game (the sweep, the report and the tests). Both run the same list of
// checks over a common view of the map; only the walking and the placing rules differ.
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mapgen.hpp"
#include "mapgen_internal.hpp"
#include "mapgen_water.hpp"
#include "movement.hpp"
#include "skirmish.hpp"
#include "state.hpp"

namespace antanta::sim {

namespace mapgen {

bool Box1(std::int32_t W, std::int32_t H, std::int32_t x, std::int32_t y, const Footprint& f,
          std::vector<std::int32_t>* cells) {
    cells->clear();
    std::int32_t rx = x, ry = y;
    Step(&rx, &ry, 6);
    bool inside = true;
    for (std::int32_t r = 0; r < f.b + 2; ++r) {
        std::int32_t cx = rx, cy = ry;
        for (std::int32_t k = 0; k < f.a + 2; ++k) {
            if (cx < 0 || cy < 0 || cx >= W || cy >= H) {
                inside = false;
            } else {
                cells->push_back(cy * W + cx);
            }
            Step(&cx, &cy, 1);
        }
        Step(&rx, &ry, 4);
    }
    return inside;
}

int SawmillTrees(const MapData& m, std::int32_t x, std::int32_t y) {
    const std::int32_t W = m.width, H = m.height;
    std::int32_t x0 = x - 5, y0 = y - 10, x1 = x + 5, y1 = y + 10;
    auto clamp = [&](std::int32_t& cx, std::int32_t& cy) {
        if (cx <= 3) cx = 4;
        if (cy <= 9) cy = 10;
        if (cx >= W - 3) cx = W - 4;
        if (cy >= H - 9) cy = H - 10;
    };
    clamp(x0, y0);
    clamp(x1, y1);
    int trees = 0;
    for (std::int32_t cx = x0; cx < x1; ++cx) {
        for (std::int32_t cy = y0; cy < y1; ++cy)
            trees += IsTree(m.cells[static_cast<std::size_t>(cy) * W + cx].object);
    }
    return trees;
}

namespace {

bool Edge(std::int32_t W, std::int32_t H, std::int32_t x, std::int32_t y) {
    return x <= 3 || y <= 9 || x >= W - 3 || y >= H - 9;
}



bool FieldBox(const Grid& g, std::int32_t x, std::int32_t y, const Footprint& f) {
    const MapData& m = *g.map;
    const std::int32_t h0 = m.cells[static_cast<std::size_t>(y) * g.W + x].height;
    std::int32_t rx = x, ry = y + 8;
    for (std::int32_t r = 0; r < f.b + 10; ++r) {
        std::int32_t cx = rx, cy = ry;
        for (std::int32_t k = 0; k < f.a + 8; ++k) {
            if (!Edge(g.W, g.H, cx, cy) && cx >= 4 && cy >= 10) {
                const std::size_t i = static_cast<std::size_t>(cy) * g.W + cx;
                if (IsDeposit(m.cells[i].object) || (g.taken != nullptr && (*g.taken)[i] != 0)) return false;
                if (std::abs(m.cells[i].height - h0) > 10) return false;
                for (int d = 1; d <= 8; ++d) {
                    std::int32_t nx = cx, ny = cy;
                    Step(&nx, &ny, d);
                    if (nx >= 0 && ny >= 0 && nx < g.W && ny < g.H &&
                        IsDeposit(m.cells[static_cast<std::size_t>(ny) * g.W + nx].object)) {
                        return false;
                    }
                }
            }
            Step(&cx, &cy, 1);
        }
        Step(&rx, &ry, 4);
    }
    return true;
}

}  // namespace

bool CanPlace(const Grid& g, std::int32_t x, std::int32_t y, const Footprint& f, bool summer) {
    const MapData& m = *g.map;
    if (Edge(g.W, g.H, x, y)) return false;
    const bool mine = f.type == 10 || f.type == 11 || f.type == 13;
    const std::uint16_t own = f.type == 11 ? kGold : f.type == 10 ? kIron : f.type == 13 ? kOil : 0;
    const std::size_t at = static_cast<std::size_t>(y) * g.W + x;
    if (mine && m.cells[at].object != own) return false;
    std::int32_t lo = m.cells[at].height, hi = lo;
    std::int32_t rx = x, ry = y;
    Step(&rx, &ry, 6);
    for (std::int32_t r = 0; r < f.b + 2; ++r) {
        std::int32_t cx = rx, cy = ry;
        for (std::int32_t k = 0; k < f.a + 2; ++k) {
            if (Edge(g.W, g.H, cx, cy)) return false;
            const std::size_t i = static_cast<std::size_t>(cy) * g.W + cx;
            if (m.cells[i].object != 0 && !(i == at && mine)) return false;
            if (g.taken != nullptr && (*g.taken)[i] != 0) return false;
            lo = std::min<std::int32_t>(lo, m.cells[i].height), hi = std::max<std::int32_t>(hi, m.cells[i].height);
            Step(&cx, &cy, 1);
        }
        Step(&rx, &ry, 4);
    }
    rx = x, ry = y + 6;
    for (std::int32_t r = 0; r < f.b + 6; ++r) {
        std::int32_t cx = rx, cy = ry;
        for (std::int32_t k = 0; k < f.a + 6; ++k) {
            if (Edge(g.W, g.H, cx, cy)) return false;
            const std::uint16_t o = m.cells[static_cast<std::size_t>(cy) * g.W + cx].object;
            if (IsDeposit(o) && o != own) return false;
            Step(&cx, &cy, 1);
        }
        Step(&rx, &ry, 4);
    }
    if (!mine && hi - lo > 20) return false;
    if (f.type == kSawmill.type) return SawmillTrees(m, x, y) >= 15;
    if (f.type == kBarn.type) return summer && FieldBox(g, x, y, f);
    return true;
}

}  // namespace mapgen

using namespace mapgen;

namespace {

// The common view of a map for the checks.
struct Env {
    std::int32_t W = 0, H = 0;
    std::vector<std::uint16_t> objects;
    std::vector<std::uint8_t> heights;
    std::vector<std::uint8_t> walk;                  // 1: a unit may stand and walk there
    std::vector<std::uint8_t> taken;                 // houses planned by the kit site check
    std::vector<std::uint8_t> water;
    std::vector<std::uint16_t> grid;                 // the obstacle grid
    std::vector<std::array<std::int32_t, 2>> ports;  // the port sites
    std::vector<std::int32_t> seas;                  // the ships' open water by sea (ShipSeas), m.width x m.height / 2
    bool summer = true;
    // Whether house `f` (its type) can stand at (x, y) for the player of position p, under every nation's footprint;
    // the taken cells are tested apart with the largest footprint.
    std::function<bool(std::size_t p, std::int32_t x, std::int32_t y, const Footprint& f)> place;
};

bool TakenFree(const Env& e, std::int32_t x, std::int32_t y, const Footprint& f, std::vector<std::int32_t>* box) {
    if (!Box1(e.W, e.H, x, y, f, box)) return false;
    for (std::int32_t i : *box) {
        if (e.taken[static_cast<std::size_t>(i)] != 0) return false;
    }
    return true;
}

// No water under box 1 (CanPlaceHouse refuses an obstacle cell but for a port).
bool WaterFree(const Env& e, std::int32_t x, std::int32_t y, const Footprint& f) {
    std::vector<std::int32_t> box;
    Box1(e.W, e.H, x, y, f, &box);
    for (std::int32_t i : box) {
        if (e.water[static_cast<std::size_t>(i)] != 0) return false;
    }
    return true;
}

// The moves of the path search: the world steps of 16 directions (0, 9.5, 26.6, 45, 56.3, 68.2 and 90 degrees and
// their mirrors), so a path's length is within about 2 % of the straight line in any direction (the 8 neighbours of
// the diamond grid alone are up to 15 % longer at 45 degrees). A long move also needs the cells it crosses.
struct Move {
    std::int32_t len;
    std::array<std::array<std::int32_t, 2>, 2> to;                  // the cell offset from an even and an odd row
    std::array<std::array<std::array<std::int32_t, 2>, 3>, 2> via;  // the cells crossed (at 1/4, 1/2, 3/4)
};

std::vector<Move> Moves() {
    static constexpr std::int32_t kSteps[7][2] = {{96, 0}, {0, 48}, {48, 24}, {48, 72}, {144, 24}, {96, 96}, {48, 120}};
    std::vector<Move> out;
    for (const auto& st : kSteps) {
        for (int sx = 1; sx >= -1; sx -= 2) {
            for (int sy = 1; sy >= -1; sy -= 2) {
                if ((st[0] == 0 && sx < 0) || (st[1] == 0 && sy < 0)) continue;
                const std::int64_t dx = sx * st[0], dy = sy * st[1];
                Move mv{static_cast<std::int32_t>(ISqrt(dx * dx + dy * dy)), {}, {}};
                for (int odd = 0; odd < 2; ++odd) {
                    const Pt c = CellCentre(10, 10 + odd);
                    std::int32_t x = 0, y = 0;
                    CellAt(Pt{c.x + dx, c.y + dy}, &x, &y);
                    mv.to[static_cast<std::size_t>(odd)] = {x - 10, y - 10 - odd};
                    for (int k = 0; k < 3; ++k) {
                        CellAt(Pt{c.x + dx * (k + 1) / 4, c.y + dy * (k + 1) / 4}, &x, &y);
                        mv.via[static_cast<std::size_t>(odd)][static_cast<std::size_t>(k)] = {x - 10, y - 10 - odd};
                    }
                }
                out.push_back(mv);
            }
        }
    }
    return out;
}

const std::vector<Move>& AllMoves() {
    static const std::vector<Move> kMoves = Moves();
    return kMoves;
}

// The cell move k of a graph's mask reaches from u: u + Offset(W, odd row, k).
std::int32_t Offset(std::int32_t W, std::int32_t odd, std::size_t k) {
    const auto& t = AllMoves()[k].to[static_cast<std::size_t>(odd)];
    return t[1] * W + t[0];
}

// The path distance to an object cell: through its walking neighbours (-1: unreached).
std::int32_t DistanceTo(const Env& e, const std::vector<std::int32_t>& dist, std::int32_t x, std::int32_t y) {
    std::int32_t best = -1;
    for (int dir = 1; dir <= 8; ++dir) {
        std::int32_t nx = x, ny = y;
        Step(&nx, &ny, dir);
        if (nx < 0 || ny < 0 || nx >= e.W || ny >= e.H) continue;
        const std::int32_t d = dist[static_cast<std::size_t>(ny * e.W + nx)];
        if (d >= 0 && (best < 0 || d + kStepLen[dir] < best)) best = d + kStepLen[dir];
    }
    return best;
}

std::string Ratio(const char* what, std::int64_t lo, std::int64_t hi) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s %.2f", what, hi > 0 ? static_cast<double>(lo) / static_cast<double>(hi) : 0.0);
    return buf;
}







std::string TankReach(const Env& e, const MapGenLayout& layout, const std::vector<std::int32_t>& start_cells,
                      std::int32_t* misses) {
    const std::int32_t W = e.W, H = e.H;
    const std::size_t n = static_cast<std::size_t>(W) * static_cast<std::size_t>(H);
    std::vector<std::uint8_t> open(n, 0);
    for (std::int32_t y = 10; y < H - 9; ++y) {
        for (std::int32_t x = 4; x < W - 3; ++x) {
            const auto i = static_cast<std::size_t>(y * W + x);
            if (e.objects[i] != 0 || e.water[i] != 0) continue;
            const int odd = y & 1;
            const int h[4] = {e.heights[i], e.heights[static_cast<std::size_t>((y - 1) * W + x + odd)],
                              e.heights[i + 1], e.heights[static_cast<std::size_t>((y + 1) * W + x + odd)]};
            open[i] = *std::max_element(h, h + 4) - *std::min_element(h, h + 4) <= 25 ? 1 : 0;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!IsDeposit(e.objects[i])) continue;
        std::int32_t x = static_cast<std::int32_t>(i) % W, y = static_cast<std::int32_t>(i) / W;
        Step(&x, &y, 1);
        for (const auto& d : {std::array<std::int32_t, 2>{0, 0}, {0, 2}, {-1, 2}, {-1, 0}}) {
            const std::int32_t cx = x + d[0], cy = y + d[1];
            if (cx >= 0 && cy >= 0 && cx < W && cy < H) open[static_cast<std::size_t>(cy * W + cx)] = 0;
        }
    }
    const std::size_t players = start_cells.size();
    const std::vector<std::int8_t>& island = layout.island;
    auto isl = [&](std::size_t p) { return island.empty() ? 0 : island[p]; };
    std::vector<std::int8_t> seen(n, -1);  // the island whose first start reached the cell
    for (std::size_t s0 = 0; s0 < players; ++s0) {
        if (s0 > 0 && isl(s0 - 1) == isl(s0)) continue;
        const auto src = static_cast<std::size_t>(start_cells[s0]);
        if (open[src] == 0) continue;
        std::vector<std::int32_t> todo{start_cells[s0]};
        seen[src] = static_cast<std::int8_t>(isl(s0));
        for (std::size_t t = 0; t < todo.size(); ++t) {
            for (int dir = 1; dir <= 8; ++dir) {
                std::int32_t x = todo[t] % W, y = todo[t] / W;
                Step(&x, &y, dir);
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                const auto v = static_cast<std::size_t>(y * W + x);
                if (open[v] == 0 || seen[v] >= 0) continue;
                seen[v] = static_cast<std::int8_t>(isl(s0));
                todo.push_back(static_cast<std::int32_t>(v));
            }
        }
    }
    std::string first;
    std::int32_t missed = 0;
    auto miss = [&](const std::string& what) {
        if (first.empty()) first = what;
        ++missed;
    };
    for (std::size_t p = 0; p < players; ++p) {
        if (seen[static_cast<std::size_t>(start_cells[p])] != isl(p))
            miss("tanks: start " + std::to_string(p + 1) + " unreached");
    }
    // Any reached cell within `rad` world px of cell (x, y).
    auto near = [&](std::int32_t x, std::int32_t y, std::int64_t rad) {
        const Pt c = CellCentre(x, y);
        const std::int32_t rx = static_cast<std::int32_t>(rad / 96) + 1, ry = static_cast<std::int32_t>(rad / 24) + 1;
        for (std::int32_t yy = std::max(0, y - ry); yy <= std::min(H - 1, y + ry); ++yy) {
            for (std::int32_t xx = std::max(0, x - rx); xx <= std::min(W - 1, x + rx); ++xx) {
                if (seen[static_cast<std::size_t>(yy * W + xx)] >= 0 && Dist2(CellCentre(xx, yy), c) <= rad * rad)
                    return true;
            }
        }
        return false;
    };
    for (std::size_t k = 0; k < layout.groups.size(); ++k) {
        const MapGenGroup& gr = layout.groups[k];
        if (gr.ring == 4) continue;  // a neutral island's: ships reach it
        bool got = false;
        for (const auto& cell : gr.cells) got = got || near(cell[0], cell[1], 300);
        if (!got) miss("tanks: free group " + std::to_string(k + 1) + " (ring " + std::to_string(gr.ring) + ")");
    }
    std::vector<std::int32_t> box;
    for (std::size_t k = 0; k < e.ports.size(); ++k) {
        Box1(W, H, e.ports[k][0], e.ports[k][1], kPort, &box);
        bool got = false;
        for (std::int32_t i : box) got = got || seen[static_cast<std::size_t>(i)] >= 0;
        if (!got) miss("tanks: port site " + std::to_string(k + 1));
    }
    if (misses != nullptr) *misses = missed;
    return first;
}

// The hard checks of plan 5.10 on `e`; "" when all pass.
std::string RunChecks(Env& e, const MapGenSettings& g, const MapGenLayout& layout, MapGenReport* report) {
    const std::int32_t W = e.W, H = e.H;
    const std::size_t n = static_cast<std::size_t>(W) * static_cast<std::size_t>(H), players = layout.zones.size();
    const std::int64_t wpx = std::int64_t{W} * 96, hpx = std::int64_t{H} * 24;
    std::vector<MapGenStartStats> st(players);
    MapGenReport local;
    MapGenReport& r = report != nullptr ? *report : local;
    r.starts.clear();
    r.trees = r.stones = r.land_cells = r.above65_cells = r.steep_cells = 0;
    r.deposits = {};

    // Sanity: heights 1 to 250; every deposit a mine site after LoadMap's edge rule; the map's numbers.
    for (std::size_t i = 0; i < n; ++i) {
        const std::int32_t x = static_cast<std::int32_t>(i) % W, y = static_cast<std::int32_t>(i) / W;
        if (e.heights[i] < 1 || e.heights[i] > 250) return "sanity: height " + std::to_string(e.heights[i]);
        const std::uint16_t o = e.objects[i];
        if (IsDeposit(o)) {
            if (x < 6 || x > W - 6 || y < 15 || y > H - 15) return "sanity: deposit in the edge band";
            ++r.deposits[static_cast<std::size_t>(KindIndex(o))];
        }
        r.trees += IsTree(o) ? 1 : 0, r.stones += IsStone(o) ? 1 : 0, r.land_cells += e.water[i] == 0 ? 1 : 0;
        r.above65_cells += e.heights[i] > 65 ? 1 : 0;
        if (x + 1 < W && y >= 1 && y + 1 < H) {  // the walking rule's height range over a cell's four corners
            const int odd = y & 1;
            const int h[4] = {e.heights[i], e.heights[static_cast<std::size_t>(y - 1) * W + x + odd], e.heights[i + 1],
                              e.heights[static_cast<std::size_t>(y + 1) * W + x + odd]};
            r.steep_cells += *std::max_element(h, h + 4) - *std::min_element(h, h + 4) > 60 ? 1 : 0;
        }
    }

    // The start cells.
    std::vector<std::int32_t> start_cells;
    for (const MapGenZone& z : layout.zones) {
        std::int32_t x = 0, y = 0;
        CellAt(Pt{z.x, z.y}, &x, &y);
        if (e.walk[static_cast<std::size_t>(y * W + x)] == 0) return "land reach: a start cell is blocked";
        start_cells.push_back(y * W + x);
    }

    // Land reach: every start reaches every other start and the middle; on islands every start of its island.
    const std::vector<std::int8_t>& island = layout.island;
    std::vector<std::int32_t> dist(n, -1);  // a flood from start 1, on islands from each island's first (0: reached)
    for (std::size_t s0 = 0; s0 < players; ++s0) {
        if (island.empty() ? s0 > 0 : s0 > 0 && island[s0 - 1] == island[s0]) continue;
        std::vector<std::int32_t> todo{start_cells[s0]};
        dist[static_cast<std::size_t>(start_cells[s0])] = 0;
        while (!todo.empty()) {
            const std::int32_t u = todo.back();
            todo.pop_back();
            for (int dir = 1; dir <= 8; ++dir) {
                std::int32_t x = u % W, y = u / W;
                Step(&x, &y, dir);
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                const auto v = static_cast<std::size_t>(y * W + x);
                if (e.walk[v] == 0 || dist[v] == 0) continue;
                dist[v] = 0;
                todo.push_back(static_cast<std::int32_t>(v));
            }
        }
    }
    for (std::int32_t c : start_cells) {
        if (dist[static_cast<std::size_t>(c)] < 0) return "land reach: a start cannot reach start 1";
    }
    if (!island.empty()) {  // the floods of two islands must not meet
        std::vector<std::int32_t> mark(n, -1);
        for (std::size_t s0 = 0; s0 < players; ++s0) {
            std::vector<std::int32_t> todo{start_cells[s0]};
            if (mark[static_cast<std::size_t>(start_cells[s0])] >= 0) {
                if (mark[static_cast<std::size_t>(start_cells[s0])] != island[s0])
                    return "land reach: two islands joined by land";
                continue;
            }
            mark[static_cast<std::size_t>(start_cells[s0])] = island[s0];
            while (!todo.empty()) {
                const std::int32_t u = todo.back();
                todo.pop_back();
                for (int dir = 1; dir <= 8; ++dir) {
                    std::int32_t x = u % W, y = u / W;
                    Step(&x, &y, dir);
                    if (x < 0 || y < 0 || x >= W || y >= H) continue;
                    const auto v = static_cast<std::size_t>(y * W + x);
                    if (e.walk[v] == 0 || mark[v] >= 0) continue;
                    mark[v] = island[s0];
                    todo.push_back(static_cast<std::int32_t>(v));
                }
            }
            if (s0 > 0 && island[s0 - 1] == island[s0] && mark[static_cast<std::size_t>(start_cells[s0])] != island[s0])
                return "land reach: a start cannot reach its island's first";
        }
    }
    std::int32_t mid_cell = -1;  // the walking cell nearest the middle (a land map)
    if (layout.pattern == 0) {
        std::int32_t mx = 0, my = 0;
        CellAt(Pt{wpx / 2, hpx / 2}, &mx, &my);
        for (std::int32_t rad = 0; rad < 20 && mid_cell < 0; ++rad) {
            for (std::int32_t y = my - 2 * rad; y <= my + 2 * rad && mid_cell < 0; ++y) {
                for (std::int32_t x = mx - rad; x <= mx + rad && mid_cell < 0; ++x) {
                    if (x < 0 || y < 0 || x >= W || y >= H || e.walk[static_cast<std::size_t>(y * W + x)] == 0)
                        continue;
                    if (dist[static_cast<std::size_t>(y * W + x)] < 0) return "land reach: the middle";
                    mid_cell = y * W + x;
                }
            }
        }
        if (mid_cell < 0) return "land reach: no walking cell near the middle";
    }

    // Enemy distance: the path distance to the nearest other start, from one search from every start (the cheapest
    // edge between two starts' regions).
    const PathGraph pg = MakePathGraph(e.walk, W, H);
    std::vector<std::int8_t> label;
    PathDistances(pg, start_cells, -1, &dist, &label);
    std::vector<std::int32_t> enemy(players, -1);
    const std::vector<Move>& moves = AllMoves();
    for (std::size_t u = 0; u < n; ++u) {
        if (dist[u] < 0) continue;
        const auto odd = static_cast<std::int32_t>((u / static_cast<std::size_t>(W)) & 1);
        for (std::uint32_t mask = pg.moves[u]; mask != 0; mask &= mask - 1) {
            const auto k = static_cast<std::size_t>(std::countr_zero(mask));
            const auto v = static_cast<std::size_t>(static_cast<std::int32_t>(u) + Offset(W, odd, k));
            if (dist[v] < 0 || label[v] == label[u]) continue;
            const std::int32_t d = dist[u] + moves[k].len + dist[v];
            auto& best = enemy[static_cast<std::size_t>(label[u])];
            if (best < 0 || d < best) best = d;
        }
    }

    // Per start (one search over the whole map): counts within the radius, free space, the path distance to each
    // kind's nearest deposit, to the middle and to every free deposit group (its nearest deposit), and the computers'
    // measure to each group (|dx| + |dy| in cells and rows to its nearest deposit by that measure).
    const std::size_t groups = layout.groups.size();
    std::vector<std::vector<std::int32_t>> group_walk(groups, std::vector<std::int32_t>(players, -1));
    std::vector<std::vector<std::int32_t>> group_finder(groups, std::vector<std::int32_t>(players, -1));
    for (std::size_t p = 0; p < players; ++p) {
        const MapGenZone& z = layout.zones[p];
        const Pt c{z.x, z.y};
        MapGenStartStats& s = st[p];
        s.height = e.heights[static_cast<std::size_t>(start_cells[p])];
        s.to_enemy = enemy[p];
        PathDistances(pg, {start_cells[p]}, -1, &dist);
        if (mid_cell >= 0) s.to_middle = dist[static_cast<std::size_t>(mid_cell)];
        const std::int32_t sx = start_cells[p] % W, sy = start_cells[p] / W;
        for (std::size_t k = 0; k < groups; ++k) {
            for (const auto& cell : layout.groups[k].cells) {
                const std::int32_t d = DistanceTo(e, dist, cell[0], cell[1]);
                const std::int32_t f = std::abs(cell[0] - sx) + std::abs(cell[1] - sy);
                std::int32_t& w = group_walk[k][p];
                std::int32_t& fi = group_finder[k][p];
                if (d >= 0 && (w < 0 || d < w)) w = d;
                if (fi < 0 || f < fi) fi = f;
            }
        }
        s.to_deposit = {-1, -1, -1};
        const std::int32_t rx = static_cast<std::int32_t>(3000 / 96) + 2, ry = static_cast<std::int32_t>(3000 / 24) + 2;
        std::int32_t cx = 0, cy = 0;
        CellAt(c, &cx, &cy);
        for (std::int32_t y = std::max(0, cy - ry); y < std::min(H, cy + ry); ++y) {
            for (std::int32_t x = std::max(0, cx - rx); x < std::min(W, cx + rx); ++x) {
                const auto i = static_cast<std::size_t>(y * W + x);
                const std::uint16_t o = e.objects[i];
                const std::int64_t d2 = Dist2(CellCentre(x, y), c);
                if (IsDeposit(o)) {
                    const std::int32_t d = DistanceTo(e, dist, x, y);
                    auto& best = s.to_deposit[static_cast<std::size_t>(KindIndex(o))];
                    if (d >= 0 && (best < 0 || d < best)) best = d;
                }
                if (d2 > kZoneR * kZoneR) continue;
                ++s.zone_cells;
                if (IsDeposit(o)) ++s.deposits[static_cast<std::size_t>(KindIndex(o))];
                s.trees += IsTree(o) ? 1 : 0, s.stones += IsStone(o) ? 1 : 0;
                // Buildable: no object, flat enough (the building rule's span 20 over the cell and its neighbours),
                // outside the edge band.
                if (o != 0 || Edge(W, H, x, y)) continue;
                std::int32_t lo = e.heights[i], hi = lo;
                for (int dir = 1; dir <= 8; ++dir) {
                    std::int32_t nx = x, ny = y;
                    Step(&nx, &ny, dir);
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    const std::int32_t h = e.heights[static_cast<std::size_t>(ny * W + nx)];
                    lo = std::min(lo, h), hi = std::max(hi, h);
                }
                s.free_cells += hi - lo <= 20 ? 1 : 0;
            }
        }
    }
    r.starts = st;

    // Start height, kit counts, edge.
    for (std::size_t p = 0; p < players; ++p) {
        const MapGenStartStats& s = st[p];
        const std::string who = " (start " + std::to_string(p + 1) + ")";
        if (s.height != kPlain) return "start height " + std::to_string(s.height) + who;
        if (s.deposits != std::array<std::int32_t, 3>{2, 2, 2} || s.trees != kKitTrees || s.stones != kKitStones) {
            return "kit counts: deposits " + std::to_string(s.deposits[0]) + "/" + std::to_string(s.deposits[1]) + "/" +
                   std::to_string(s.deposits[2]) + ", trees " + std::to_string(s.trees) + ", stones " +
                   std::to_string(s.stones) + who;
        }
        const MapGenZone& z = layout.zones[p];
        if (z.x - kZoneR < kBandX || z.x + kZoneR > wpx - kBandX || z.y - kZoneR < kBandY ||
            z.y + kZoneR > hpx - kBandY)
            return "edge: zone outside the keep-out band" + who;
    }

    // Free space: within 2 % of the mean.
    std::int64_t sum = 0;
    for (const MapGenStartStats& s : st) sum += s.free_cells;
    for (std::size_t p = 0; p < players; ++p) {
        const std::int64_t d = st[p].free_cells * static_cast<std::int64_t>(players) - sum;
        if ((d < 0 ? -d : d) * 50 > sum)
            return Ratio("free space", st[p].free_cells * static_cast<std::int64_t>(players), sum);
    }

    // Resource distance (0.9) and enemy distance (0.77): smallest over largest.
    for (std::size_t k = 0; k < 3; ++k) {
        std::int64_t lo = -1, hi = 0;
        for (const MapGenStartStats& s : st) {
            if (s.to_deposit[k] < 0) return "resource distance: a kind unreached";
            lo = lo < 0 || s.to_deposit[k] < lo ? s.to_deposit[k] : lo,
            hi = std::max<std::int64_t>(hi, s.to_deposit[k]);
        }
        if (lo * 10 < hi * 9) return Ratio("resource distance", lo, hi);
    }
    {
        // Islands: a start alone on its island takes the straight line to the nearest other start; the ratio is
        // taken over the starts of islands of the same size (an island of 3 is no island of 2).
        std::vector<int> isize(players, 0);
        for (std::size_t p = 0; p < players && !island.empty(); ++p) {
            for (std::size_t q = 0; q < players; ++q) isize[p] += island[q] == island[p] ? 1 : 0;
            if (st[p].to_enemy >= 0) continue;
            std::int64_t best = -1;
            for (std::size_t q = 0; q < players; ++q) {
                if (q == p) continue;
                const std::int64_t d2 =
                    Dist2(Pt{layout.zones[p].x, layout.zones[p].y}, Pt{layout.zones[q].x, layout.zones[q].y});
                best = best < 0 || d2 < best ? d2 : best;
            }
            st[p].to_enemy = static_cast<std::int32_t>(ISqrt(best));
        }
        r.starts = st;
        r.enemy_ratio = 1000;
        for (int cls = 0; cls <= 8; ++cls) {
            std::int64_t lo = -1, hi = 0;
            for (std::size_t p = 0; p < players; ++p) {
                if (isize[p] != cls) continue;
                const MapGenStartStats& s = st[p];
                if (s.to_enemy < 0) return "enemy distance: unreached";
                lo = lo < 0 || s.to_enemy < lo ? s.to_enemy : lo, hi = std::max<std::int64_t>(hi, s.to_enemy);
            }
            if (lo < 0) continue;
            r.enemy_ratio =
                std::min(r.enemy_ratio, static_cast<std::int32_t>(lo * 1000 / std::max<std::int64_t>(1, hi)));
            if (lo * 100 < hi * 77) return Ratio("enemy distance", lo, hi);
        }
        // Islands: each start's straight line to the nearest start of another island (the enemy across the water;
        // with teams the island mates are teammates), smallest over largest over all starts.
        r.across_ratio = -1;
        if (!island.empty()) {
            std::int64_t lo = -1, hi = 0;
            for (std::size_t p = 0; p < players; ++p) {
                std::int64_t best = -1;
                for (std::size_t q = 0; q < players; ++q) {
                    if (island[q] == island[p]) continue;
                    const std::int64_t d2 =
                        Dist2(Pt{layout.zones[p].x, layout.zones[p].y}, Pt{layout.zones[q].x, layout.zones[q].y});
                    best = best < 0 || d2 < best ? d2 : best;
                }
                const std::int64_t d = ISqrt(best);
                lo = lo < 0 || d < lo ? d : lo, hi = std::max(hi, d);
            }
            r.across_ratio = static_cast<std::int32_t>(lo * 1000 / std::max<std::int64_t>(1, hi));
            if (lo * 100 < hi * 80) return Ratio("enemy distance across the water", lo, hi);
        }
    }

    // The free deposit rings (plan 5.6, 5.10; they replace the loose check of question 4): every group's cells hold
    // deposits; own groups: the same kinds for every player, the path to each own group within 0.85 and the
    // computers' measure within 0.80 over the players; contested groups: as many for every player, the paths of their
    // two players within 0.85; the middle group: the paths of all players within 0.40 (a 2 : 1 map has no point as far
    // from every start; as the starts' own distances to the middle).
    r.rings = {};
    {
        std::vector<std::vector<std::size_t>> own(players);  // per player its own groups in order
        for (std::size_t k = 0; k < groups; ++k) {
            const MapGenGroup& gr = layout.groups[k];
            for (const auto& cell : gr.cells) {
                if (!IsDeposit(e.objects[static_cast<std::size_t>(cell[1] * W + cell[0])]))
                    return "rings: a group without its deposit";
            }
            MapGenRingStats& rs = r.rings[static_cast<std::size_t>(std::clamp<int>(gr.ring, 1, 4) - 1)];
            ++rs.groups, rs.deposits += static_cast<std::int32_t>(gr.cells.size());
            if (gr.ring == 4) continue;  // a neutral island's: ships reach it, no walk
            std::vector<std::size_t> who;
            if (gr.ring == 1) who = {static_cast<std::size_t>(gr.players[0])}, own[who[0]].push_back(k);
            if (gr.ring == 2) who = {static_cast<std::size_t>(gr.players[0]), static_cast<std::size_t>(gr.players[1])};
            if (gr.ring == 3) {
                for (std::size_t q = 0; q < players; ++q) who.push_back(q);
            }
            std::int32_t lo = -1, hi = 0;
            for (std::size_t q : who) {
                const std::int32_t d = group_walk[k][q];
                if (d < 0) return "rings: a group unreached";
                lo = lo < 0 || d < lo ? d : lo, hi = std::max(hi, d);
                rs.walk_lo = rs.walk_lo < 0 || d < rs.walk_lo ? d : rs.walk_lo, rs.walk_hi = std::max(rs.walk_hi, d);
            }
            if (gr.ring == 2) {
                const std::int32_t ratio = static_cast<std::int32_t>(std::int64_t{lo} * 1000 / std::max(1, hi));
                rs.ratio = rs.ratio == 0 ? ratio : std::min(rs.ratio, ratio);
                if (lo * 100 < hi * 85) return Ratio("rings: contested group", lo, hi);
            }
            if (gr.ring == 3) {
                rs.ratio = static_cast<std::int32_t>(std::int64_t{lo} * 1000 / std::max(1, hi));
                if (lo * 100 < hi * 40) return Ratio("rings: middle group", lo, hi);
            }
        }
        // One contested group per pair of neighbours by land, none else (decisions 54 and 56: a player has as many as
        // the land gives it neighbours); two on a 2-player map. A Sea map may give every pair a second one deeper
        // inland (decision 57): then every pair has exactly one inland group (on a 2-player map its second group).
        std::vector<std::array<std::int8_t, 2>> want(layout.neighbours), inland_want;
        bool inland = false;
        for (const MapGenGroup& gr : layout.groups) inland = inland || (gr.ring == 2 && gr.inland);
        if (inland && layout.pattern != 1) return "rings: an inland contested group off a Sea map";
        if (inland) inland_want = layout.neighbours;
        if (players == 2 || inland) want.insert(want.end(), layout.neighbours.begin(), layout.neighbours.end());
        for (const MapGenGroup& gr : layout.groups) {
            if (gr.ring != 2) continue;
            const std::array<std::int8_t, 2> pr{std::min(gr.players[0], gr.players[1]),
                                                std::max(gr.players[0], gr.players[1])};
            const auto it = std::find(want.begin(), want.end(), pr);
            if (it == want.end()) return "rings: a contested group between no neighbours";
            want.erase(it);
            if (!gr.inland) continue;
            const auto in = std::find(inland_want.begin(), inland_want.end(), pr);
            if (in == inland_want.end()) return "rings: two inland contested groups for one pair";
            inland_want.erase(in);
        }
        if (!want.empty()) return "rings: a pair of neighbours without its contested group";
        if (!inland_want.empty()) return "rings: a pair of neighbours without its inland group";
        for (std::size_t q = 0; q < players; ++q) {
            if (own[q].size() != own[0].size()) return "rings: group counts differ";
        }
        for (std::size_t gi = 0; gi < own[0].size(); ++gi) {
            std::int32_t lo = -1, hi = 0, flo = -1, fhi = 0;
            for (std::size_t q = 0; q < players; ++q) {
                const std::size_t k = own[q][gi];
                std::array<int, 3> kinds{}, kinds0{};
                for (const auto& cell : layout.groups[k].cells)
                    ++kinds[static_cast<std::size_t>(
                        KindIndex(e.objects[static_cast<std::size_t>(cell[1] * W + cell[0])]))];
                for (const auto& cell : layout.groups[own[0][gi]].cells)
                    ++kinds0[static_cast<std::size_t>(
                        KindIndex(e.objects[static_cast<std::size_t>(cell[1] * W + cell[0])]))];
                if (kinds != kinds0) return "rings: own groups of other kinds";
                const std::int32_t d = group_walk[k][q], f = group_finder[k][q];
                lo = lo < 0 || d < lo ? d : lo, hi = std::max(hi, d);
                flo = flo < 0 || f < flo ? f : flo, fhi = std::max(fhi, f);
                st[q].own_walk = std::max(st[q].own_walk, d), st[q].own_finder = std::max(st[q].own_finder, f);
            }
            MapGenRingStats& rs = r.rings[0];
            const std::int32_t ratio = static_cast<std::int32_t>(std::int64_t{lo} * 1000 / std::max(1, hi));
            rs.ratio = rs.ratio == 0 ? ratio : std::min(rs.ratio, ratio);
            if (lo * 100 < hi * 85) return Ratio("rings: own groups", lo, hi);
            if (flo * 100 < fhi * 80) return Ratio("rings: own groups for the computers", flo, fhi);
        }
        r.starts = st;
    }

    // The middle (land maps): the starts' paths to it, smallest over largest (the report only since decision 56: the
    // starts are fitted per code, and the shared group goes to the fairest place, checked above as ring 3).
    r.middle_ratio = 0;
    if (mid_cell >= 0) {
        std::int32_t lo = -1, hi = 0;
        for (const MapGenStartStats& s : st)
            lo = lo < 0 || s.to_middle < lo ? s.to_middle : lo, hi = std::max(hi, s.to_middle);
        r.middle_ratio = static_cast<std::int32_t>(std::int64_t{lo} * 1000 / std::max(1, hi));
    }

    // The territories (plan 5.1): cells, buildable cells (no object, flat as the free space rule, no water, off the
    // edge band) and trees outside the zones, per territory (report).
    if (!layout.territory.empty()) {
        for (std::size_t i = 0; i < n; ++i) {
            const std::int8_t t = layout.territory[i];
            if (t < 0 || static_cast<std::size_t>(t) >= players) continue;
            MapGenStartStats& s = st[static_cast<std::size_t>(t)];
            const std::int32_t x = static_cast<std::int32_t>(i) % W, y = static_cast<std::int32_t>(i) / W;
            ++s.terr_cells;
            bool in_zone = false;
            for (const MapGenZone& z : layout.zones)
                in_zone = in_zone || Dist2(CellCentre(x, y), Pt{z.x, z.y}) <= kZoneR * kZoneR;
            if (!in_zone && IsTree(e.objects[i])) ++s.terr_trees;
            if (e.objects[i] != 0 || e.water[i] != 0 || Edge(W, H, x, y)) continue;
            std::int32_t lo = e.heights[i], hi = lo;
            for (int dir = 1; dir <= 8; ++dir) {
                std::int32_t nx = x, ny = y;
                Step(&nx, &ny, dir);
                if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                const std::int32_t h = e.heights[static_cast<std::size_t>(ny * W + nx)];
                lo = std::min(lo, h), hi = std::max(hi, h);
            }
            s.terr_free += hi - lo <= 20 ? 1 : 0;
        }
        r.starts = st;
        // Decision 51's aim as a rule (2026-11-06): the buildable land of the territories within 0.85.
        std::int64_t lo = -1, hi = 0;
        for (const MapGenStartStats& s : st)
            lo = lo < 0 || s.terr_free < lo ? s.terr_free : lo, hi = std::max<std::int64_t>(hi, s.terr_free);
        if (lo * 100 < hi * 85) return Ratio("territory land", lo, hi);
    }

    // The empty area (plan 5.10, 2026-11-05): inside the keep-out band no disc wider than kEmptyDisc without a
    // feature (any object, water, ground above 65, a start zone). The shipped maps' largest: 344 to 816 px.
    {
        std::vector<std::int32_t> feat;
        for (std::size_t i = 0; i < n; ++i) {
            const std::int32_t x = static_cast<std::int32_t>(i) % W, y = static_cast<std::int32_t>(i) / W;
            bool f = e.objects[i] != 0 || e.water[i] != 0 || e.heights[i] > 65;
            for (const MapGenZone& z : layout.zones) f = f || Dist2(CellCentre(x, y), Pt{z.x, z.y}) <= kZoneR * kZoneR;
            if (f) feat.push_back(static_cast<std::int32_t>(i));
        }
        PathDistances(MakePathGraph(std::vector<std::uint8_t>(n, 1), W, H), feat, -1, &dist);
        r.empty_disc = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const Pt c = CellCentre(static_cast<std::int32_t>(i) % W, static_cast<std::int32_t>(i) / W);
            if (c.x < kBandX || c.x > wpx - kBandX || c.y < kBandY || c.y > hpx - kBandY) continue;
            r.empty_disc = std::max(r.empty_disc, dist[i]);
        }
        if (r.empty_disc > kEmptyDisc) return "empty area: a disc of " + std::to_string(r.empty_disc) + " px";
    }

    // Water (plan 5.10): a Sea map's share of whole water blocks 18 to 50 % (decision 54: the generated map tells the
    // computers it is a water map, so LandMap's third no longer binds; islands keep 34.5 to 60 %), every start with 2
    // or more port sites that pass the port rule within PortFar by land (by the Sea band, decision 57), every port's
    // water one sea; a land map at most 13 % water and no port site.
    {
        std::int64_t full = 0;
        const std::int64_t blocks = static_cast<std::int64_t>(e.grid.size());
        for (std::uint16_t v : e.grid) full += v != 0 && (v & 0x8000) == 0 ? 1 : 0;
        r.water_share = blocks > 0 ? static_cast<std::int32_t>(full * 1000 / blocks) : 0;
        if (!g.sea && full * 1000 > blocks * 130) return "water: land map " + std::to_string(r.water_share / 10) + " %";
        if (!g.sea && !e.ports.empty()) return "water: port sites on a land map";
        const std::int64_t least = g.islands ? kIslesLeast : kSeaLeast, most = g.islands ? 600 : 500;
        if (g.sea && (full * 1000 < blocks * least || full * 1000 > blocks * most))
            return "water: sea " + std::to_string(r.water_share / 10) + " %";
        // Every port's ships reach one sea (the ships' open water next to it).
        std::int32_t body = -1;
        for (const auto& site : e.ports) {
            const std::int32_t b = PortSea(e.seas, W, H / 2, site[0], site[1]);
            if (b < 0) return "water: no open water at a port site";
            if (body < 0) body = b;
            if (b != body) return "water: the port sites' water is not one sea";
        }
        std::vector<std::int32_t> box;
        for (std::size_t p = 0; p < players && g.sea; ++p) {
            PathDistances(pg, {start_cells[p]}, static_cast<std::int32_t>(PortFar(layout)), &dist);
            std::int32_t near = 0;
            for (const auto& site : e.ports) {
                Box1(W, H, site[0], site[1], kPort, &box);
                bool reached = false;
                for (std::int32_t i : box) reached = reached || dist[static_cast<std::size_t>(i)] >= 0;
                if (reached && e.place(p, site[0], site[1], kPort)) ++near;
            }
            st[p].ports = near;
            r.starts = st;
            if (near < 2) return "water: " + std::to_string(near) + " port sites near start " + std::to_string(p + 1);
        }
    }

    // Tanks after the forests (plan 5.10, 2026-11-06): every start, free group and port site of its island.
    {
        const std::string tanks = TankReach(e, layout, start_cells, &r.tank_miss);
        if (!tanks.empty()) return tanks;
    }

    // Kit sites: a city centre in the core, a barn with its field box (summer), a sawmill at each wood, a mine on each
    // kit deposit, 8 small houses in the zone. The houses planned take their cells.
    e.taken.assign(n, 0);
    std::vector<std::int32_t> box;
    for (std::size_t p = 0; p < players; ++p) {
        const MapGenZone& z = layout.zones[p];
        const Pt c{z.x, z.y};
        const std::string who = " (start " + std::to_string(p + 1) + ")";
        for (std::size_t k = 0; k < 6; ++k) {
            if (!e.place(p, z.deposits[k][0], z.deposits[k][1], kMineOf[k % 3])) return "kit sites: a mine" + who;
        }
        for (const auto& s : z.sawmills) {
            if (!e.place(p, s[0], s[1], kSawmill)) return "kit sites: a sawmill" + who;
        }
        // The zone's cells, nearest to the start first.
        std::vector<std::pair<std::int64_t, std::int32_t>> cells;
        std::int32_t cx = 0, cy = 0;
        CellAt(c, &cx, &cy);
        for (std::int32_t y = std::max(0, cy - 56); y < std::min(H, cy + 56); ++y) {
            for (std::int32_t x = std::max(0, cx - 15); x < std::min(W, cx + 15); ++x) {
                const std::int64_t d2 = Dist2(CellCentre(x, y), c);
                if (d2 <= kZoneR * kZoneR) cells.emplace_back(d2, y * W + x);
            }
        }
        std::sort(cells.begin(), cells.end());
        auto take = [&](const Footprint& f, std::int64_t within) {
            for (const auto& [d2, i] : cells) {
                if (d2 > within * within) break;
                const std::int32_t x = i % W, y = i / W;
                if (!TakenFree(e, x, y, f, &box) || !e.place(p, x, y, f)) continue;
                for (std::int32_t b : box) e.taken[static_cast<std::size_t>(b)] = 1;
                return true;
            }
            return false;
        };
        if (!take(kCityCentre, kCoreR)) return "kit sites: a city centre" + who;
        if (e.summer && !take(kBarn, kZoneR)) return "kit sites: a barn" + who;
        for (int k = 0; k < kDwellings; ++k) {
            if (!take(kDwelling, kZoneR)) return "kit sites: " + std::to_string(k) + " small houses" + who;
        }
    }
    (void)g;
    return "";
}

}  // namespace

PathGraph mapgen::MakePathGraph(const std::vector<std::uint8_t>& walk, std::int32_t W, std::int32_t H) {
    const std::vector<Move>& moves = AllMoves();
    PathGraph pg{W, H, std::vector<std::uint32_t>(walk.size(), 0)};
    // Per row parity and move: the four cells (three crossed, the end) as index offsets. Every move stays within 2
    // cells and 5 rows, so inside that border no bound is tested.
    std::array<std::array<std::array<std::int32_t, 4>, 24>, 2> off{};
    for (std::size_t odd = 0; odd < 2; ++odd) {
        for (std::size_t k = 0; k < moves.size(); ++k) {
            for (std::size_t c = 0; c < 4; ++c) {
                const auto& v = c < 3 ? moves[k].via[odd][c] : moves[k].to[odd];
                off[odd][k][c] = v[1] * W + v[0];
            }
        }
    }
    for (std::int32_t y = 0; y < H; ++y) {
        const std::size_t odd = static_cast<std::size_t>(y & 1);
        for (std::int32_t x = 0; x < W; ++x) {
            const std::int32_t i = y * W + x;
            std::uint32_t mask = 0;
            if (x >= 3 && y >= 6 && x < W - 3 && y < H - 6) {
                for (std::size_t k = 0; k < moves.size(); ++k) {
                    const auto& o = off[odd][k];
                    const bool ok =
                        (walk[static_cast<std::size_t>(i + o[0])] & walk[static_cast<std::size_t>(i + o[1])] &
                         walk[static_cast<std::size_t>(i + o[2])] & walk[static_cast<std::size_t>(i + o[3])]) != 0;
                    mask |= ok ? 1u << k : 0u;
                }
            } else {
                for (std::size_t k = 0; k < moves.size(); ++k) {
                    bool ok = true;
                    for (std::size_t c = 0; c < 4 && ok; ++c) {
                        const auto& v = c < 3 ? moves[k].via[odd][c] : moves[k].to[odd];
                        const std::int32_t vx = x + v[0], vy = y + v[1];
                        ok = vx >= 0 && vy >= 0 && vx < W && vy < H && walk[static_cast<std::size_t>(vy * W + vx)] != 0;
                    }
                    mask |= ok ? 1u << k : 0u;
                }
            }
            pg.moves[static_cast<std::size_t>(i)] = mask;
        }
    }
    return pg;
}

// Dijkstra with a ring of buckets (every move is 48 to 146 px long, so a ring of 256 distances holds every pending
// one): the same distances as a heap; a label goes to the first source that reaches a cell at its least distance.
void mapgen::PathDistances(const PathGraph& pg, const std::vector<std::int32_t>& sources, std::int32_t bound,
                           std::vector<std::int32_t>* dist, std::vector<std::int8_t>* label) {
    const std::vector<Move>& moves = AllMoves();
    std::array<std::array<std::int32_t, 24>, 2> step{};
    std::array<std::int32_t, 24> len{};
    for (std::size_t k = 0; k < moves.size(); ++k) {
        step[0][k] = Offset(pg.W, 0, k), step[1][k] = Offset(pg.W, 1, k);
        len[k] = moves[k].len;
    }
    dist->assign(pg.moves.size(), -1);
    if (label != nullptr) label->assign(pg.moves.size(), -1);
    std::array<std::vector<std::int32_t>, 256> ring;
    std::size_t pending = 0;
    for (std::size_t s = 0; s < sources.size(); ++s) {
        const auto i = static_cast<std::size_t>(sources[s]);
        if ((*dist)[i] == 0) continue;
        (*dist)[i] = 0;
        if (label != nullptr) (*label)[i] = static_cast<std::int8_t>(s);
        ring[0].push_back(sources[s]);
        ++pending;
    }
    // Raw pointers, unsigned compares (-1 as the largest) and the row parity by a constant divisor (the searches are
    // most of the checks' time); the same order and results.
    std::int32_t* const dp = dist->data();
    std::int8_t* const lp = label != nullptr ? label->data() : nullptr;
    const std::uint32_t* const mp = pg.moves.data();
    const std::uint32_t most = bound >= 0 ? static_cast<std::uint32_t>(bound) : 0xffffffffu;
    const std::int32_t W = pg.W;
    auto row = [W](std::int32_t u) { return W == 200 ? u / 200 : W == 300 ? u / 300 : u / W; };
    for (std::int32_t d = 0; pending > 0; ++d) {
        std::vector<std::int32_t>& bucket = ring[static_cast<std::size_t>(d & 255)];
        while (!bucket.empty()) {
            const std::int32_t u = bucket.back();
            bucket.pop_back();
            --pending;
            if (dp[u] != d) continue;
            const auto& st = step[static_cast<std::size_t>(row(u) & 1)];
            for (std::uint32_t mask = mp[u]; mask != 0; mask &= mask - 1) {
                const auto k = static_cast<std::size_t>(std::countr_zero(mask));
                const std::int32_t nd = d + len[k];
                if (static_cast<std::uint32_t>(nd) > most) continue;
                const std::int32_t v = u + st[k];
                if (static_cast<std::uint32_t>(dp[v]) <= static_cast<std::uint32_t>(nd)) continue;
                dp[v] = nd;
                if (lp != nullptr) lp[v] = lp[u];
                ring[static_cast<std::size_t>(nd & 255)].push_back(v);
                ++pending;
            }
        }
    }
}

std::string MapGen_Check(const MapData& m, const MapGenSettings& g, const MapGenLayout& layout, MapGenReport* report) {
    Env e;
    e.W = m.width, e.H = m.height;
    const std::size_t n = m.cells.size();
    e.objects.resize(n), e.heights.resize(n), e.walk.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) e.objects[i] = m.cells[i].object, e.heights[i] = m.cells[i].height;
    e.water = BlockedCells(m), e.grid = m.obstacle_grid, e.seas = ShipSeas(ShipCells(m), m.width, m.height / 2);
    for (std::uint32_t v : m.port_sites)
        e.ports.push_back({static_cast<std::int32_t>(v & 0xffff), static_cast<std::int32_t>(v >> 16)});
    // Walking: no blocking object (men walk through trees and stones), off the border band, and the walking rule (a
    // cell's four corners within 60; Cell_LandStatic).
    for (std::int32_t y = 0; y < e.H; ++y) {
        for (std::int32_t x = 0; x < e.W; ++x) {
            const auto i = static_cast<std::size_t>(y * e.W + x);
            if (BlocksWalk(e.objects[i]) || e.water[i] != 0 || WalkBorder(e.W, e.H, x, y)) continue;
            const int odd = y & 1;
            const int h[4] = {e.heights[i], e.heights[static_cast<std::size_t>((y - 1) * e.W + x + odd)],
                              e.heights[i + 1], e.heights[static_cast<std::size_t>((y + 1) * e.W + x + odd)]};
            e.walk[i] = *std::max_element(h, h + 4) - *std::min_element(h, h + 4) <= 60 ? 1 : 0;
        }
    }
    e.summer = !g.winter;
    const Grid grid{e.W, e.H, &m, &e.taken};  // the planned houses (also tested apart, and in the field box)
    e.place = [&](std::size_t, std::int32_t x, std::int32_t y, const Footprint& f) {
        if (f.type == kPort.type) return PortOk(m, e.water, x, y);
        return CanPlace(grid, x, y, f, e.summer) && WaterFree(e, x, y, f);
    };

    return RunChecks(e, g, layout, report);
}

std::string MapGen_Verify(const MapData& m, const MapGenSettings& g, const MapGenLayout& layout, const GameData& data,
                          const AiTemplate* tpl) {
    SkirmishSetup setup;
    setup.map = MapGen_Code(g);
    setup.players = g.players;
    for (std::uint8_t i = 1; i < g.players; ++i) setup.slots[i] = SkirmishSlot{2, 0, i, 0};
    std::string err;
    auto s = std::make_unique<GameState>();
    {
        auto started = GameState_NewSkirmish(setup, m, data, tpl, &err);
        if (!started) return "start: " + err;
        *s = std::move(*started);
    }
    int deposits = 0;
    for (const MapCell& c : m.cells) deposits += IsDeposit(c.object) ? 1 : 0;
    if (s->mine_site_count != deposits) return "sanity: mine sites " + std::to_string(s->mine_site_count);
    Env e;
    e.W = s->map.width, e.H = s->map.height;
    const std::size_t n = static_cast<std::size_t>(e.W) * static_cast<std::size_t>(e.H);
    e.objects = s->map.objects, e.heights = s->map.heights, e.walk.assign(n, 0), e.water.assign(n, 0);
    e.grid = s->map.obstacle;
    std::vector<std::uint8_t> open(s->map.water.g.size(), 0);
    for (std::size_t k = 0; k < open.size(); ++k)
        open[k] = s->map.water.g[k] == 0 || s->map.water.g[k] == 11 ? 1 : 0;  // small ships pass 11
    e.seas = ShipSeas(open, s->map.water.w, s->map.water.h);
    for (std::uint32_t v : s->port_sites)
        e.ports.push_back({static_cast<std::int32_t>(v & 0xffff), static_cast<std::int32_t>(v >> 16)});
    for (std::int32_t y = 0; y < e.H; ++y) {
        for (std::int32_t x = 0; x < e.W; ++x) {
            std::uint8_t w = 0;
            e.walk[static_cast<std::size_t>(y * e.W + x)] = Cell_LandStatic(*s, x, y, &w) ? 1 : 0;
            e.water[static_cast<std::size_t>(y * e.W + x)] = Cell_Obstacle(s->map, x, y) ? 1 : 0;
        }
    }
    e.summer = s->climate != 3;
    e.place = [&](std::size_t p, std::int32_t x, std::int32_t y, const Footprint& f) {
        // The positions are the state's players; every nation's footprint must fit.
        Player& pl = s->players[p];
        const std::uint8_t own = pl.nation;
        bool ok = true;
        for (std::uint8_t nation = 0; nation < 5 && ok; ++nation) {
            pl.nation = nation;
            ok = House_CanPlace(*s, static_cast<std::uint8_t>(p), x, y, f.type);
        }
        pl.nation = own;
        return ok;
    };
    return RunChecks(e, g, layout, nullptr);
}

std::string MapGen_ReportText(const MapGenSettings& g, const MapGenReport& r, double build_ms, double check_ms) {
    std::string out = MapGen_Code(g);
    char buf[768];
    std::snprintf(buf, sizeof buf, "  attempt %u", unsigned{r.attempt} + 1);
    out += buf;
    if (!r.failed.empty()) {
        out += " (";
        for (std::size_t k = 0; k < r.failed.size(); ++k)
            out += (k ? "; " : "") + std::to_string(k + 1) + ": " + r.failed[k];
        out += ")";
    }
    std::snprintf(buf, sizeof buf, "  %s  build %.0f ms  checks %.0f ms\n",
                  r.layout.shape.empty() ? "territories" : r.layout.shape.c_str(), build_ms, check_ms);
    out += buf;
    out += "player  height  gold iron oil  trees stones  free%   to gold/iron/oil     enemy   ports  start (world)\n";
    for (std::size_t p = 0; p < r.starts.size(); ++p) {
        const MapGenStartStats& s = r.starts[p];
        const MapGenZone z = p < r.layout.zones.size() ? r.layout.zones[p] : MapGenZone{};
        std::snprintf(buf, sizeof buf, "%-7zu %-7d %-4d %-4d %-4d %-5d %-7d %-7.1f %4d / %4d / %4d   %6d  %-6d %d,%d\n",
                      p + 1, s.height, s.deposits[0], s.deposits[1], s.deposits[2], s.trees, s.stones,
                      s.zone_cells ? 100.0 * s.free_cells / s.zone_cells : 0.0, s.to_deposit[0], s.to_deposit[1],
                      s.to_deposit[2], s.to_enemy, s.ports, z.x, z.y);
        out += buf;
    }
    // The territories and the rings (plan 5.1, 5.6).
    out += "player  territory cells  buildable %  trees (outside the zone)  to middle  own groups walk / computers\n";
    for (std::size_t p = 0; p < r.starts.size(); ++p) {
        const MapGenStartStats& s = r.starts[p];
        std::snprintf(buf, sizeof buf, "%-7zu %-16d %-12.1f %-25d %-10d %d / %d\n", p + 1, s.terr_cells,
                      s.terr_cells ? 100.0 * s.terr_free / s.terr_cells : 0.0, s.terr_trees, s.to_middle, s.own_walk,
                      s.own_finder);
        out += buf;
    }
    std::int32_t tlo = -1, thi = 0, flo = -1, fhi = 0, clo = -1, chi = 0;
    for (const MapGenStartStats& s : r.starts) {
        tlo = tlo < 0 || s.terr_trees < tlo ? s.terr_trees : tlo, thi = std::max(thi, s.terr_trees);
        flo = flo < 0 || s.terr_free < flo ? s.terr_free : flo, fhi = std::max(fhi, s.terr_free);
        clo = clo < 0 || s.terr_cells < clo ? s.terr_cells : clo, chi = std::max(chi, s.terr_cells);
    }
    std::snprintf(buf, sizeof buf,
                  "territories: cells %.3f, buildable %.3f, trees %.3f (smallest over largest); middle distance %.2f, "
                  "nearest enemy %.2f; largest empty disc %d px\n",
                  chi ? static_cast<double>(clo) / chi : 0.0, fhi ? static_cast<double>(flo) / fhi : 0.0,
                  thi ? static_cast<double>(tlo) / thi : 0.0, r.middle_ratio / 1000.0, r.enemy_ratio / 1000.0,
                  r.empty_disc);
    out += buf;
    static const char* kRing[4] = {"own", "contested", "middle", "neutral islands"};
    out += "rings: kit 2/2/2 per player";
    for (std::size_t k = 0; k < 4; ++k) {
        const MapGenRingStats& rs = r.rings[k];
        std::snprintf(buf, sizeof buf, "; %s %d groups, %d deposits, walk %d..%d (%.2f)", kRing[k], rs.groups,
                      rs.deposits, rs.walk_lo, rs.walk_hi, rs.ratio / 1000.0);
        out += buf;
    }
    out += "\n";
    const double land = r.land_cells > 0 ? r.land_cells : 1;
    std::snprintf(buf, sizeof buf,
                  "map: water %.1f %%, land above 65 %.1f %%, steep %.1f %%, trees %.1f %%, stones %d, deposits "
                  "%d/%d/%d\nground: sub-tiles %.1f %%, same-id neighbours %.2f, blobs %d / %d, window ids %d top "
                  "%.2f, base tiles differ %.2f\nwood: trees %.1f %%, lone %.2f, biggest grove %d (%d ids), density "
                  "%.2f, stones %.1f per 1,000 cells (%.2f next to a tree), decoration %d, trees within 3,000 px "
                  "%.2f\n",
                  r.water_share / 10.0, 100.0 * r.above65_cells / land, 100.0 * r.steep_cells / land,
                  100.0 * r.trees / land, r.stones, r.deposits[0], r.deposits[1], r.deposits[2],
                  r.look.subtile_share / 10.0, r.look.same_slots / 1000.0, r.look.blob_median, r.look.blob_p90,
                  r.look.window_ids, r.look.window_top / 1000.0, r.look.base_differ / 1000.0, r.wood.tree_share / 10.0,
                  r.wood.lone_share / 1000.0, r.wood.biggest, r.wood.big_grove_ids, r.wood.density / 1000.0,
                  r.wood.stones_per_k / 100.0, r.wood.stones_near / 1000.0, r.wood.decoration,
                  r.wood.wood_ratio / 1000.0);
    out += buf;
    return out;
}

std::string MapGen_ReportTsvHead() {
    return "code\tattempt\tfailed\tpattern\tbuild_ms\tfree_min\tfree_max\tres_ratio_min\tenemy_ratio\tenemy_min\t"
           "trees\tdeposits\tsame_slots\twindow_ids\twindow_top\ttree_share\tlone\tbiggest\tdensity\tverify\t"
           "water\tmiddle_ratio\tterr_free\tterr_trees\town_ratio\tcontested_ratio\tmiddle_group\tempty_disc\t"
           "ports_min\tisle_groups\tshape\town_groups\tacross_ratio\ttank_miss\tsea_band\tcontested\tinland\n";
}

std::string MapGen_ReportTsv(const MapGenSettings& g, const MapGenReport& r, double build_ms,
                             const std::string& verify) {
    std::int32_t fmin = -1, fmax = 0, emin = -1, emax = 0;
    double res = 1.0;
    for (std::size_t k = 0; k < 3; ++k) {
        std::int32_t lo = -1, hi = 0;
        for (const MapGenStartStats& s : r.starts)
            lo = lo < 0 || s.to_deposit[k] < lo ? s.to_deposit[k] : lo, hi = std::max(hi, s.to_deposit[k]);
        if (hi > 0) res = std::min(res, static_cast<double>(lo) / hi);
    }
    for (const MapGenStartStats& s : r.starts) {
        fmin = fmin < 0 || s.free_cells < fmin ? s.free_cells : fmin, fmax = std::max(fmax, s.free_cells);
        emin = emin < 0 || s.to_enemy < emin ? s.to_enemy : emin, emax = std::max(emax, s.to_enemy);
    }
    std::string failed;
    for (std::size_t k = 0; k < r.failed.size(); ++k) failed += (k ? "; " : "") + r.failed[k];
    std::int32_t tlo = -1, thi = 0, flo = -1, fhi = 0;
    for (const MapGenStartStats& s : r.starts) {
        tlo = tlo < 0 || s.terr_trees < tlo ? s.terr_trees : tlo, thi = std::max(thi, s.terr_trees);
        flo = flo < 0 || s.terr_free < flo ? s.terr_free : flo, fhi = std::max(fhi, s.terr_free);
    }
    std::int32_t ports_min = -1;
    for (const MapGenStartStats& s : r.starts) ports_min = ports_min < 0 || s.ports < ports_min ? s.ports : ports_min;
    // A Sea map's band (decision 57): drawn by the code / kept; the contested groups and the inland ones among them.
    int contested = 0, inland = 0;
    for (const MapGenGroup& gr : r.layout.groups) contested += gr.ring == 2 ? 1 : 0, inland += gr.inland ? 1 : 0;
    char tail[640];
    std::snprintf(
        tail, sizeof tail,
        "\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%d\t%d\t%d\t%s\t%.2f\t%.3f\t%d\t%d/%d\t%d\t%d\n",
        r.water_share / 1000.0, r.middle_ratio / 1000.0, fhi ? static_cast<double>(flo) / fhi : 0.0,
        thi ? static_cast<double>(tlo) / thi : 0.0, r.rings[0].ratio / 1000.0, r.rings[1].ratio / 1000.0,
        r.rings[2].ratio / 1000.0, r.empty_disc, ports_min, r.rings[3].groups, r.layout.shape.c_str(),
        static_cast<double>(r.rings[0].groups) / static_cast<double>(std::max<std::size_t>(1, r.starts.size())),
        r.across_ratio / 1000.0, r.tank_miss, r.layout.pattern == 1 ? mapgen::SeaBandDrawn(g) : 0,
        static_cast<int>(r.layout.sea_band), contested, inland);
    char buf[768];
    std::snprintf(
        buf, sizeof buf,
        "%s\t%u\t%s\t%u\t%.1f\t%d\t%d\t%.3f\t%.3f\t%d\t%d\t%d/%d/%d\t%.3f\t%d\t%.3f\t%.3f\t%.3f\t%d\t%.3f\t%s",
        MapGen_Code(g).c_str(), unsigned{r.attempt} + 1, failed.empty() ? "-" : failed.c_str(),
        unsigned{r.layout.pattern}, build_ms, fmin, fmax, res, emax > 0 ? static_cast<double>(emin) / emax : 0.0, emin,
        r.trees, r.deposits[0], r.deposits[1], r.deposits[2], r.look.same_slots / 1000.0, r.look.window_ids,
        r.look.window_top / 1000.0, r.wood.tree_share / 1000.0, r.wood.lone_share / 1000.0, r.wood.biggest,
        r.wood.density / 1000.0, verify.empty() ? "ok" : verify.c_str());
    return std::string(buf) + tail;
}

}  // namespace antanta::sim
