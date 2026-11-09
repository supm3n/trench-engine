// The random map generator's free deposits, forests, stones and decoration (docs/map-generator-plan.md 5.6, 5.7;
// slice G4). Integer only; the random numbers are this file's own (SplitMix64 from the key mapgen.cpp gives), never
// the CRT rand() or the game's RNG table.
#include "mapgen_forest.hpp"
#include "mapgen_sea.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <vector>

#include "mapgen.hpp"
#include "mapgen_internal.hpp"
#include "mapgen_water.hpp"

namespace antanta::sim {

using namespace mapgen;

namespace {

// ---- The ids, measured from the shipped skirmish maps by tools/mapgen_trees.py (numbers only). ----

struct IdWeight {
    std::uint16_t id, w;
};

// summer: 12 maps, 214 tree ids, 1715 groves of 10 or more, decoration per map [0, 0, 0, 0, 10, 18, 21, 29, 40, 135,
// 146, 462] winter: 4 maps, 100 tree ids, 442 groves of 10 or more, decoration per map [0, 0, 0, 17]
const std::vector<IdWeight> kSummerTrees = {{9113, 68}, {9046, 62}, {9036, 61}, {9007, 54}, {9158, 52}, {9174, 49},
                                            {9171, 45}, {9028, 44}, {9157, 44}, {9163, 40}, {9051, 39}, {9159, 36},
                                            {9164, 36}, {9062, 33}, {9143, 33}, {9043, 31}, {9004, 28}, {9038, 26},
                                            {9063, 26}, {9135, 26}, {9173, 25}, {9172, 24}, {9013, 23}, {9142, 23},
                                            {9175, 22}, {9153, 18}, {9165, 17}, {9117, 16}};
const std::vector<IdWeight> kSummerStand0 = {{9036, 162}, {9007, 118}, {9004, 77}, {9038, 50}, {9013, 48}, {9046, 43},
                                             {9014, 41},  {9008, 39},  {9005, 37}, {9143, 35}, {9015, 34}, {9075, 34},
                                             {9028, 31},  {9034, 30},  {9113, 29}, {9135, 26}, {9062, 25}, {9025, 23},
                                             {9174, 23},  {9172, 21},  {9158, 20}, {9010, 19}, {9029, 19}, {9171, 18}};
const std::vector<IdWeight> kSummerStand1 = {{9046, 169}, {9051, 101}, {9028, 81}, {9062, 77}, {9036, 75}, {9038, 63},
                                             {9043, 62},  {9007, 60},  {9063, 60}, {9037, 41}, {9173, 39}, {9164, 21},
                                             {9143, 20},  {9163, 19},  {9067, 18}, {9119, 16}, {9155, 16}, {9171, 16},
                                             {9174, 15},  {9186, 15},  {9157, 14}};
const std::vector<IdWeight> kSummerStand2 = {
    {9171, 131}, {9164, 85}, {9163, 74}, {9158, 71}, {9174, 64}, {9143, 58}, {9172, 52}, {9173, 38}, {9007, 36},
    {9170, 36},  {9175, 36}, {9166, 32}, {9046, 31}, {9169, 26}, {9028, 23}, {9157, 23}, {9006, 21}, {9051, 21},
    {9062, 20},  {9036, 19}, {9145, 18}, {9153, 18}, {9160, 18}, {9162, 17}, {9084, 16}, {9142, 15}};
const std::vector<IdWeight> kSummerStand3 = {{9157, 124}, {9159, 104}, {9158, 89}, {9142, 55}, {9165, 55}, {9161, 53},
                                             {9113, 50},  {9167, 46},  {9151, 44}, {9168, 38}, {9143, 31}, {9153, 30},
                                             {9163, 29},  {9141, 28},  {9051, 21}, {9133, 20}, {9174, 20}, {9084, 18},
                                             {9117, 18},  {9145, 17},  {9043, 16}, {9148, 15}, {9007, 14}, {9036, 14},
                                             {9147, 14},  {9028, 13},  {9119, 13}, {9149, 13}};
const std::vector<IdWeight> kSummerStand4 = {
    {9174, 218}, {9175, 71}, {9113, 70}, {9158, 69}, {9164, 67}, {9163, 61}, {9013, 36}, {9173, 33}, {9153, 32},
    {9157, 32},  {9004, 31}, {9143, 27}, {9172, 27}, {9045, 25}, {9171, 25}, {9117, 21}, {9162, 21}, {9028, 20},
    {9007, 18},  {9159, 18}, {9142, 17}, {9170, 17}, {9151, 16}, {9155, 15}, {9059, 14}};
const std::vector<IdWeight> kSummerStand5 = {{9113, 524}, {9115, 56}, {9114, 49}, {9158, 41}, {9174, 32}, {9007, 29},
                                             {9036, 24},  {9086, 20}, {9157, 20}, {9159, 19}, {9013, 18}, {9085, 18},
                                             {9141, 18},  {9004, 17}, {9006, 17}, {9142, 17}, {9028, 16}, {9089, 16},
                                             {9135, 16},  {9143, 16}, {9090, 15}};
const std::vector<IdWeight> kSummerStones = {{9523, 179}, {9505, 128}, {9506, 107}, {9507, 78}, {9522, 77}, {9514, 70},
                                             {9517, 62},  {9521, 51},  {9509, 38},  {9518, 35}, {9515, 32}, {9516, 30},
                                             {9519, 29},  {9510, 23},  {9520, 22},  {9502, 14}, {9501, 12}, {9513, 12}};
const std::vector<IdWeight> kSummerDeco = {{9622, 279}, {9605, 242}, {9606, 161}, {9608, 122}, {9609, 102},
                                           {9612, 35},  {9614, 33},  {9611, 14},  {9607, 11}};
const std::vector<IdWeight> kWinterTrees = {
    {9215, 129}, {9206, 104}, {9214, 83}, {9200, 67}, {9201, 64}, {9192, 55}, {9185, 46}, {9193, 42}, {9055, 40},
    {9211, 39},  {9210, 37},  {9166, 32}, {9053, 30}, {9189, 29}, {9213, 27}, {9209, 25}, {9208, 23}, {9212, 20},
    {9203, 19},  {9216, 17},  {9207, 16}, {9202, 15}, {9135, 14}, {9186, 13}, {9217, 13}};
const std::vector<IdWeight> kWinterStand0 = {{9215, 134}, {9214, 98}, {9201, 67}, {9166, 55}, {9192, 49}, {9193, 48},
                                             {9206, 44},  {9211, 40}, {9212, 39}, {9217, 38}, {9207, 35}, {9213, 34},
                                             {9185, 31},  {9189, 29}, {9200, 28}, {9216, 27}, {9208, 25}, {9053, 23},
                                             {9202, 23},  {9055, 21}, {9190, 20}, {9162, 17}, {9203, 15}, {9188, 14},
                                             {9135, 12},  {9186, 12}, {9210, 12}, {9153, 11}};
const std::vector<IdWeight> kWinterStand1 = {{9206, 79}, {9200, 74}, {9201, 71}, {9055, 70}, {9185, 66}, {9189, 65},
                                             {9192, 65}, {9193, 63}, {9166, 60}, {9053, 56}, {9215, 48}, {9202, 34},
                                             {9214, 33}, {9211, 32}, {9186, 24}, {9190, 22}, {9188, 21}, {9203, 20},
                                             {9187, 19}, {9208, 18}, {9212, 18}, {9213, 15}, {9216, 14}, {9162, 13}};
const std::vector<IdWeight> kWinterStand2 = {{9215, 256}, {9210, 150}, {9206, 92}, {9200, 71}, {9214, 69},
                                             {9201, 61},  {9209, 54},  {9211, 30}, {9055, 25}, {9166, 23},
                                             {9212, 22},  {9192, 21},  {9202, 21}, {9053, 18}, {9203, 17},
                                             {9213, 16},  {9208, 14},  {9205, 13}, {9207, 13}, {9185, 12}};
const std::vector<IdWeight> kWinterStand3 = {{9206, 178}, {9214, 137}, {9200, 119}, {9215, 82}, {9201, 64},
                                             {9055, 60},  {9192, 55},  {9211, 42},  {9209, 41}, {9053, 31},
                                             {9210, 30},  {9185, 29},  {9193, 26},  {9202, 23}, {9203, 21},
                                             {9186, 18},  {9204, 16},  {9166, 13},  {9213, 13}};
const std::vector<IdWeight> kWinterStones = {{9523, 194}, {9507, 141}, {9522, 106}, {9505, 100}, {9517, 87},
                                             {9509, 69},  {9506, 51},  {9514, 51},  {9521, 49},  {9501, 40},
                                             {9510, 38},  {9518, 30},  {9519, 19},  {9520, 16},  {9516, 10}};
const std::vector<IdWeight> kWinterDeco = {{9622, 588}, {9614, 176}, {9615, 176}, {9611, 59}};

struct Climate {
    const std::vector<IdWeight>* trees;
    std::vector<const std::vector<IdWeight>*> stands;
    const std::vector<IdWeight>* stones;
    const std::vector<IdWeight>* deco;
};
const Climate kSummer{&kSummerTrees,
                      {&kSummerStand0, &kSummerStand1, &kSummerStand2, &kSummerStand3, &kSummerStand4, &kSummerStand5},
                      &kSummerStones,
                      &kSummerDeco};
const Climate kWinter{
    &kWinterTrees, {&kWinterStand0, &kWinterStand1, &kWinterStand2, &kWinterStand3}, &kWinterStones, &kWinterDeco};

// ---- Random numbers: SplitMix64. ----

std::uint64_t Mix(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

struct Rng {
    std::uint64_t s = 0;
    std::uint32_t next() { return static_cast<std::uint32_t>(Mix(s += 0x9e3779b97f4a7c15ull) >> 32); }
    // 0 .. n - 1 (n > 0), by the multiply and shift.
    std::uint32_t below(std::uint32_t n) {
        return static_cast<std::uint32_t>((static_cast<std::uint64_t>(next()) * n) >> 32);
    }
    std::int64_t range(std::int64_t lo, std::int64_t hi) {
        return lo + static_cast<std::int64_t>(below(static_cast<std::uint32_t>(hi - lo + 1)));
    }
};

std::uint32_t Hash(std::uint64_t key, std::int64_t x, std::int64_t y) {
    return static_cast<std::uint32_t>(
        Mix(key ^ static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) * 0xc2b2ae3d27d4eb4full ^
            static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) * 0x165667b19e3779f9ull) >>
        32);
}

std::int64_t FloorDiv(std::int64_t a, std::int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

// Value noise at world point (x, y) on a lattice of `scale` pixels: 0 .. 65535, smooth (the cubic fade).
std::int64_t Noise(std::uint64_t key, std::int64_t x, std::int64_t y, std::int64_t scale) {
    const std::int64_t gx = FloorDiv(x, scale), gy = FloorDiv(y, scale);
    const std::int64_t fx = (x - gx * scale) * 65536 / scale, fy = (y - gy * scale) * 65536 / scale;
    const std::int64_t sx = (fx * fx >> 16) * (196608 - 2 * fx) >> 16, sy = (fy * fy >> 16) * (196608 - 2 * fy) >> 16;
    const std::int64_t h00 = Hash(key, gx, gy) & 0xffff, h10 = Hash(key, gx + 1, gy) & 0xffff;
    const std::int64_t h01 = Hash(key, gx, gy + 1) & 0xffff, h11 = Hash(key, gx + 1, gy + 1) & 0xffff;
    const std::int64_t a = h00 + ((h10 - h00) * sx >> 16), b = h01 + ((h11 - h01) * sx >> 16);
    return a + ((b - a) * sy >> 16);
}

std::uint16_t Pick(const std::vector<IdWeight>& t, std::uint32_t h) {
    std::uint32_t sum = 0;
    for (const IdWeight& e : t) sum += e.w;
    std::uint32_t v = static_cast<std::uint32_t>((static_cast<std::uint64_t>(h) * sum) >> 32);
    for (const IdWeight& e : t) {
        if (v < e.w) return e.id;
        v -= e.w;
    }
    return t.back().id;
}

// The 8 neighbours of measure2.py's groves (all_nbrs).
void Neighbours(std::int32_t x, std::int32_t y, std::array<std::array<std::int32_t, 2>, 8>* out) {
    const std::int32_t o = y & 1;
    *out = {{{x + o, y - 1},
             {x - 1 + o, y - 1},
             {x + o, y + 1},
             {x - 1 + o, y + 1},
             {x + 1, y},
             {x - 1, y},
             {x, y - 2},
             {x, y + 2}}};
}

struct Ctx {
    MapData& m;
    std::int32_t W, H;
    std::vector<Pt> starts;
    std::vector<std::int32_t> start_cells;
    std::size_t Index(std::int32_t x, std::int32_t y) const { return static_cast<std::size_t>(y) * W + x; }
    bool Inside(std::int32_t x, std::int32_t y) const { return x >= 0 && y >= 0 && x < W && y < H; }
    std::uint16_t& Object(std::int32_t x, std::int32_t y) { return m.cells[Index(x, y)].object; }

    Ctx(MapData& map, const MapGenLayout& layout) : m(map), W(map.width), H(map.height) {
        for (const MapGenZone& z : layout.zones) {
            starts.push_back(Pt{z.x, z.y});
            std::int32_t x = 0, y = 0;
            CellAt(starts.back(), &x, &y);
            start_cells.push_back(y * W + x);
        }
    }
    // Inside the kit's keep-out band (plan 5.0).
    bool InBand(std::int32_t x, std::int32_t y) const {
        const Pt c = CellCentre(x, y);
        return c.x >= kBandX && c.x <= std::int64_t{W} * 96 - kBandX && c.y >= kBandY &&
               c.y <= std::int64_t{H} * 24 - kBandY;
    }
};

// ---- Plan 5.6: free deposits in rings. ----

// The groups' recipes (kind indices 0 gold, 1 iron, 2 oil). `left`: the kind drawn per map that a Normal map's own
// group leaves out (a Large map's second own group too); it is the contested groups' fourth deposit on Large maps.
struct Recipe {
    std::vector<std::vector<int>> own;  // per own group of every player
    std::vector<int> contested, middle;
};

Recipe MakeRecipe(const MapGenSettings& g, const MapGenLayout& layout, Rng& r) {
    Recipe out;
    const int left = static_cast<int>(r.below(3));
    std::vector<int> pair;
    for (int k = 0; k < 3; ++k) {
        if (k != left) pair.push_back(k);
    }
    const int extra1 = static_cast<int>(r.below(3));
    const int extra2 = static_cast<int>(r.below(3));
    if (g.size == 0) {
        out.own = {pair};
        out.contested = {0, 1, 2};
        out.middle = {0, 1, 2, extra1};
    } else {
        out.own = {{0, 1, 2}, pair};
        out.contested = {0, 1, 2, left};
        out.middle = {0, 1, 2, extra1, extra2};
    }
    if (layout.pattern == 4) out.middle.clear();  // islands: no place all players reach by land
    // Islands: contested groups only between island mates where every island pairs off (MapGen_FreeDeposits).
    return out;
}

struct Rings {
    Ctx& c;
    Rng& r;
    const MapGenLayout& layout;
    std::vector<std::vector<std::int32_t>> dist;       // per start: the path distance of every cell (-1 beyond)
    std::vector<std::array<std::int32_t, 3>> near_ai;  // per start and kind: the near kit deposit's |dx| + |dy|
    std::vector<std::array<std::int32_t, 2>> all;      // every deposit (cells)

    std::int32_t Finder(std::size_t s, std::int32_t x, std::int32_t y) const {
        return std::abs(x - c.start_cells[s] % c.W) + std::abs(y - c.start_cells[s] / c.W);
    }
    // Inside the keep-out band, outside every zone by `gap` px (600; islands 300, their land is scarce).
    std::vector<std::uint8_t> open;  // per cell: Open (fixed for the step)
    bool Open(std::int32_t x, std::int32_t y) const { return c.Inside(x, y) && open[c.Index(x, y)] != 0; }
    void FindOpen(std::int64_t gap) {
        open.assign(static_cast<std::size_t>(c.W) * static_cast<std::size_t>(c.H), 0);
        for (std::int32_t y = 0; y < c.H; ++y) {
            for (std::int32_t x = 0; x < c.W; ++x) {
                bool ok = c.InBand(x, y);
                const Pt p = CellCentre(x, y);
                for (const Pt& s : c.starts) ok = ok && Dist2(p, s) >= (kZoneR + gap) * (kZoneR + gap);
                open[c.Index(x, y)] = ok ? 1 : 0;
            }
        }
    }


    bool Spot(std::int32_t x, std::int32_t y, int k) const {
        if (!Open(x, y) || c.m.cells[c.Index(x, y)].object != 0) return false;
        const Pt p = CellCentre(x, y);
        for (const auto& d : all) {
            if (Dist2(p, CellCentre(d[0], d[1])) < 400 * 400) return false;
        }
        for (std::size_t s = 0; s < c.starts.size(); ++s) {
            if (Finder(s, x, y) < near_ai[s][static_cast<std::size_t>(k)] + 6) return false;
        }
        return true;
    }

    // A group of `kinds` around one of `centres` (drawn): each deposit within `spread` px of the centre on a cell that
    // passes Spot and `ok`, 400 px apart, every one a mine site (CanPlace). The cells in `out`.
    // The path distance from start s to a deposit at (x, y), as the checks take it (DistanceTo: through its walking
    // neighbours); -1 when unreached.
    std::int32_t Near(std::size_t s, std::int32_t x, std::int32_t y) const {
        std::int32_t best = -1;
        for (int dir = 1; dir <= 8; ++dir) {
            std::int32_t nx = x, ny = y;
            Step(&nx, &ny, dir);
            if (!c.Inside(nx, ny)) continue;
            const std::int32_t v = dist[s][c.Index(nx, ny)];
            if (v >= 0 && (best < 0 || v + kStepLen[dir] < best)) best = v + kStepLen[dir];
        }
        return best;
    }
    // A group's nearest deposit from start s (-1: unreached).
    std::int32_t NearGroup(std::size_t s, const std::vector<std::array<std::int32_t, 2>>& cells) const {
        std::int32_t best = -1;
        for (const auto& o : cells) {
            const std::int32_t v = Near(s, o[0], o[1]);
            if (v >= 0 && (best < 0 || v < best)) best = v;
        }
        return best;
    }

    // `accept` (may be null): a last test of the whole group (its cells), after it was found.
    bool Group(const std::vector<std::int32_t>& centres, const std::vector<int>& kinds, std::int64_t spread,
               const std::function<bool(std::size_t)>& ok, int tries, std::vector<std::array<std::int32_t, 2>>* out,
               const std::function<bool(const std::vector<std::array<std::int32_t, 2>>&)>* accept = nullptr) {
        if (centres.empty()) return false;
        const std::int32_t rx = static_cast<std::int32_t>(spread / 96) + 1,
                           ry = static_cast<std::int32_t>(spread / 24) + 1;
        std::vector<std::int32_t> around;
        for (int t = 0; t < tries; ++t) {
            const std::int32_t ci = centres[r.below(static_cast<std::uint32_t>(centres.size()))];
            const std::int32_t cx = ci % c.W, cy = ci / c.W;
            const Pt centre = CellCentre(cx, cy);
            around.clear();
            for (std::int32_t y = std::max(0, cy - ry); y <= std::min(c.H - 1, cy + ry); ++y) {
                for (std::int32_t x = std::max(0, cx - rx); x <= std::min(c.W - 1, cx + rx); ++x) {
                    if (Dist2(CellCentre(x, y), centre) <= spread * spread && Open(x, y) && ok(c.Index(x, y)))
                        around.push_back(static_cast<std::int32_t>(c.Index(x, y)));
                }
            }
            if (around.empty()) continue;
            out->clear();
            bool good = true;
            for (const int k : kinds) {
                bool found = false;
                for (int u = 0; u < 40 && !found; ++u) {
                    const std::int32_t j = around[r.below(static_cast<std::uint32_t>(around.size()))];
                    const std::int32_t x = j % c.W, y = j / c.W;
                    if (!Spot(x, y, k)) continue;
                    found = true;
                    for (const auto& o : *out)
                        found = found && Dist2(CellCentre(x, y), CellCentre(o[0], o[1])) >= 400 * 400;
                    if (found) out->push_back({x, y});
                }
                good = good && found;
                if (!good) break;
            }
            if (!good) continue;
            for (std::size_t q = 0; q < kinds.size(); ++q)
                c.Object((*out)[q][0], (*out)[q][1]) = kKinds[static_cast<std::size_t>(kinds[q])];
            const Grid grid{c.W, c.H, &c.m, nullptr};
            for (std::size_t q = 0; q < kinds.size() && good; ++q)
                good = CanPlace(grid, (*out)[q][0], (*out)[q][1], kMineOf[kinds[q]], false);
            if (good && accept != nullptr) good = (*accept)(*out);
            if (!good) {
                for (const auto& o : *out) c.Object(o[0], o[1]) = 0;
                continue;
            }
            for (const auto& o : *out) all.push_back(o);
            return true;
        }
        return false;
    }
    void Remove(const std::vector<std::array<std::int32_t, 2>>& cells) {
        for (const auto& o : cells) {
            c.Object(o[0], o[1]) = 0;
            all.erase(std::find(all.begin(), all.end(), o));
        }
    }
};

// The walking cells as MapGen_Check takes them: no blocking object (water is marked by one), off the border band, a
// cell's four corners within 60.
std::vector<std::uint8_t> Walk(const MapData& m) {
    const std::int32_t W = m.width, H = m.height;
    std::vector<std::uint8_t> walk(static_cast<std::size_t>(W) * H, 0);
    for (std::int32_t y = 1; y + 1 < H; ++y) {
        for (std::int32_t x = 0; x + 1 < W; ++x) {
            const auto i = static_cast<std::size_t>(y * W + x);
            if (BlocksWalk(m.cells[i].object) || WalkBorder(W, H, x, y)) continue;
            const int odd = y & 1;
            const int h[4] = {m.cells[i].height, m.cells[static_cast<std::size_t>((y - 1) * W + x + odd)].height,
                              m.cells[i + 1].height, m.cells[static_cast<std::size_t>((y + 1) * W + x + odd)].height};
            walk[i] = *std::max_element(h, h + 4) - *std::min_element(h, h + 4) <= 60 ? 1 : 0;
        }
    }
    return walk;
}

}  // namespace

std::uint16_t mapgen::TreeIdOf(bool winter, std::uint32_t hash) {
    return Pick(*(winter ? kWinter : kSummer).trees, hash);
}
std::uint16_t mapgen::StoneIdOf(bool winter, std::uint32_t hash) {
    return Pick(*(winter ? kWinter : kSummer).stones, hash);
}

bool MapGen_FreeDeposits(MapData& m, const MapGenSettings& g, MapGenLayout* layout, std::uint64_t key,
                         std::string* why) {
    Ctx c(m, *layout);
    Rng r{key};
    Rings d{c, r, *layout, {}, {}, {}, {}};
    const bool islands = layout->pattern == 4;
    // A Sea map of 7 or 8 players on a Normal map: little land behind each start; when an own group finds no fair
    // place for everyone, nobody gets that group (the counts stay equal).
    const bool crowded = layout->pattern == 1 && g.size == 0 && g.players >= 7;
    const bool sea = layout->pattern == 1;
    d.FindOpen(islands ? 300 : 600);
    const std::size_t n = c.starts.size();
    const Recipe recipe = MakeRecipe(g, *layout, r);
    const std::vector<std::uint8_t> walk = Walk(m);
    const Pt middle{std::int64_t{c.W} * 48, std::int64_t{c.H} * 12};
    std::int64_t far = 0;  // the path search's bound: well past the middle from every start
    for (const Pt& s : c.starts) far = std::max(far, ISqrt(Dist2(s, middle)));
    const PathGraph pg = MakePathGraph(walk, c.W, c.H);
    for (std::size_t s = 0; s < n; ++s) {
        d.dist.emplace_back();
        PathDistances(pg, {c.start_cells[s]}, g.sea ? -1 : static_cast<std::int32_t>(far * 16 / 10 + 2000),
                      &d.dist.back());
        const std::int32_t sx = c.start_cells[s] % c.W, sy = c.start_cells[s] / c.W;
        std::array<std::int32_t, 3> near{};
        for (std::size_t k = 0; k < 3; ++k) {
            const auto& f = layout->zones[s].deposits[k];
            near[k] = std::abs(f[0] - sx) + std::abs(f[1] - sy);
        }
        d.near_ai.push_back(near);
    }
    for (std::size_t i = 0; i < m.cells.size(); ++i) {
        if (IsDeposit(m.cells[i].object))
            d.all.push_back({static_cast<std::int32_t>(i % c.W), static_cast<std::int32_t>(i / c.W)});
    }
    layout->groups.clear();
    std::vector<std::array<std::int32_t, 2>> cells;
    auto keep = [&](std::uint8_t ring, std::int8_t a, std::int8_t b) {
        layout->groups.push_back(MapGenGroup{ring, {a, b}, cells});
    };

    // Ring 2, contested: per pair of neighbours by land (layout->neighbours; decisions 54 and 56: no ring, on islands
    // only between island mates, none across the water; two groups for the two players of a 2-player map) a group
    // whose path distances to the two are within 0.92 at its centre (0.85 at each deposit) and that no other start is
    // within 1.1 times as near; every pair's group in one common window of distance (the farther of the two, w to
    // 1.3 w), so the contested groups are about as far for every player.
    std::vector<std::array<std::size_t, 2>> pairs;
    for (const auto& pr : layout->neighbours) {
        if (recipe.contested.empty()) break;
        pairs.push_back({static_cast<std::size_t>(pr[0]), static_cast<std::size_t>(pr[1])});
        if (n == 2 && !sea) pairs.push_back({static_cast<std::size_t>(pr[0]), static_cast<std::size_t>(pr[1])});
    }
    auto contest = [&](std::size_t i, std::size_t a, std::size_t b, std::int64_t lo, std::int64_t hi, int ratio) {
        const std::int32_t da = d.dist[a][i], db = d.dist[b][i];
        if (da < 0 || db < 0) return false;
        const std::int32_t far_d = std::max(da, db);
        if (std::min(da, db) * 100 < far_d * ratio || far_d < lo || far_d > hi) return false;
        for (std::size_t q = 0; q < n; ++q) {
            if (q != a && q != b && d.dist[q][i] >= 0 && d.dist[q][i] * 10 < far_d * 11) return false;
        }
        return true;
    };
    // Each pair's range of distances (the farther of the two) where a group may stand, and its places (in cell
    // order), found once; a window only filters them. `extra` (may be empty): one more rule per pair and cell. false
    // with the pair that has no place in `failed`.
    auto find_places = [&](const std::vector<std::array<std::size_t, 2>>& prs,
                           const std::function<bool(std::size_t, std::size_t)>& extra,
                           std::vector<std::array<std::int64_t, 2>>* rg, std::vector<std::vector<std::int32_t>>* pl,
                           std::size_t* failed) {
        rg->clear();
        pl->assign(prs.size(), {});
        for (std::size_t q = 0; q < prs.size(); ++q) {
            const std::size_t a = prs[q][0], b = prs[q][1];
            std::int64_t least = -1, most = -1;
            for (std::int32_t y = 0; y < c.H; ++y) {
                for (std::int32_t x = 0; x < c.W; ++x) {
                    const std::size_t i = c.Index(x, y);
                    if (!d.Open(x, y) || !contest(i, a, b, 0, 1 << 30, 92) || (extra && !extra(q, i))) continue;
                    (*pl)[q].push_back(static_cast<std::int32_t>(i));
                    const std::int32_t v = std::max(d.dist[a][i], d.dist[b][i]);
                    least = least < 0 || v < least ? v : least, most = std::max<std::int64_t>(most, v);
                }
            }
            if (least < 0) {
                *failed = q;
                return false;
            }
            rg->push_back({least, most});
        }
        return true;
    };
    std::vector<std::array<std::int64_t, 2>> range;
    std::vector<std::vector<std::int32_t>> places;
    {
        std::size_t q = 0;
        if (!find_places(pairs, {}, &range, &places, &q)) {
            *why = "free deposits: no contested place for starts " + std::to_string(pairs[q][0] + 1) + " and " +
                   std::to_string(pairs[q][1] + 1);
            return false;
        }
    }
    // Places one group per pair (`prs`, each with its range `rg` and its places `pl`) in one common window: [w, 1.3 w]
    // goes up 5 % a step from the lowest w that reaches the farthest pair's nearest place; a pair whose range misses
    // it (a Sea map's neighbours can stand at quite different distances) takes the window nearest to it within its
    // range. `extra` (may be empty): one more rule per pair and cell. true with every group kept (`inland` set as
    // given), else none kept and `failed` the pair that failed last.
    auto place_pairs = [&](const std::vector<std::array<std::size_t, 2>>& prs,
                           const std::vector<std::array<std::int64_t, 2>>& rg,
                           const std::vector<std::vector<std::int32_t>>& pl,
                           const std::function<bool(std::size_t, std::size_t)>& extra, bool inland,
                           std::size_t* failed) {
        std::int64_t least_all = 0, most_all = -1;
        for (const auto& v : rg) least_all = std::max(least_all, v[0]), most_all = std::max(most_all, v[1]);
        bool placed = false;
        for (std::int64_t w = least_all * 100 / 130; w <= most_all && !placed; w = w * 105 / 100) {
            const std::size_t first = layout->groups.size();
            placed = true;
            for (std::size_t q = 0; q < prs.size() && placed; ++q) {
                const std::size_t a = prs[q][0], b = prs[q][1];
                const std::int64_t hi = std::clamp(w * 130 / 100, rg[q][0], rg[q][1]), lo = hi * 100 / 130;
                std::vector<std::int32_t> centres;
                for (const std::int32_t i : pl[q]) {
                    const std::int32_t far_d =
                        std::max(d.dist[a][static_cast<std::size_t>(i)], d.dist[b][static_cast<std::size_t>(i)]);
                    if (far_d < lo || far_d > hi) continue;
                    // A pair's second group (a 2-player land map, islands of two) on the other side, 4,000 px from
                    // the first.
                    if (q > 0 && prs[q] == prs[q - 1] &&
                        Dist2(CellCentre(i % c.W, i / c.W),
                              CellCentre(layout->groups.back().cells[0][0], layout->groups.back().cells[0][1])) <
                            4000 * 4000)
                        continue;
                    centres.push_back(i);
                }
                // The group as the check takes it (each player's nearest deposit) within 0.87: a margin over the
                // check's 0.85 for the cells the later deposits and decoration take.
                const std::function<bool(const std::vector<std::array<std::int32_t, 2>>&)> fair =
                    [&](const std::vector<std::array<std::int32_t, 2>>& gc) {
                        const std::int32_t da = d.NearGroup(a, gc), db = d.NearGroup(b, gc);
                        return da >= 0 && db >= 0 && std::min(da, db) * 100 >= std::max(da, db) * 87;
                    };
                placed = d.Group(
                    centres, recipe.contested, 700,
                    [&](std::size_t i) { return contest(i, a, b, lo, hi * 11 / 10, 85) && (!extra || extra(q, i)); },
                    60, &cells, &fair);
                if (placed) {
                    keep(2, static_cast<std::int8_t>(a), static_cast<std::int8_t>(b));
                    layout->groups.back().inland = inland;
                }
                if (!placed) *failed = q;
            }
            if (!placed) {
                for (std::size_t k = first; k < layout->groups.size(); ++k) d.Remove(layout->groups[k].cells);
                layout->groups.resize(first);
            }
        }
        return placed;
    };
    // A pair of neighbours whose group fits in no window is dropped (the land between them is too tight) while every
    // start keeps a pair; the layout's list follows (the check reads it).
    bool placed = pairs.empty();
    std::size_t failed = 0;  // the pair that failed last
    for (int round = 0; round < 8 && !placed; ++round) {
        if (round > 0) {
            if (n == 2) break;  // (2 players: both groups of the one pair)
            std::vector<int> degree(n, 0);
            for (const auto& pr : pairs) ++degree[pr[0]], ++degree[pr[1]];
            if (degree[pairs[failed][0]] < 2 || degree[pairs[failed][1]] < 2) break;
            const std::array<std::int8_t, 2> gone{static_cast<std::int8_t>(pairs[failed][0]),
                                                  static_cast<std::int8_t>(pairs[failed][1])};
            layout->neighbours.erase(std::find(layout->neighbours.begin(), layout->neighbours.end(), gone));
            pairs.erase(pairs.begin() + static_cast<std::ptrdiff_t>(failed));
            range.erase(range.begin() + static_cast<std::ptrdiff_t>(failed));
            places.erase(places.begin() + static_cast<std::ptrdiff_t>(failed));
        }
        placed = place_pairs(pairs, range, places, {}, false, &failed);
    }
    if (!placed) {
        *why = "free deposits: contested groups";
        return false;
    }

    // Ring 2 inland (decision 57, Sea maps): per pair of neighbours a second contested group deeper inland: its
    // straight distance to the main sea at least the band's nearest start distance plus 1,500 px and 1,000 more than
    // the pair's first group (its deposit farthest from the sea), 3,000 px from that group's deposits, fair as the
    // first. Every pair or none; with 3 or more players after the own groups, so it never takes their room. A
    // 2-player map's pair always gets a second group, right after the first as before: the inland one, or where it
    // does not fit both groups again as on a land map (the second on the other side, 4,000 px away).
    auto inland_pass = [&]() {
        if (pairs.empty()) return true;
        SeaGrid sg;  // the straight distance to the main sea (8-connected) on a grid of kSeaStep px
        sg.w = static_cast<std::int32_t>(std::int64_t{c.W} * 96 / kSeaStep) + 2;
        sg.h = static_cast<std::int32_t>(std::int64_t{c.H} * 24 / kSeaStep) + 2;
        sg.step = kSeaStep;
        std::vector<std::uint8_t> wet(sg.N(), 0);
        for (std::int32_t j = 0; j < sg.h; ++j) {
            for (std::int32_t i = 0; i < sg.w; ++i) {
                std::int32_t x = 0, y = 0;
                CellAt(Pt{std::int64_t{i} * kSeaStep, std::int64_t{j} * kSeaStep}, &x, &y);
                x = std::clamp(x, 0, c.W - 1), y = std::clamp(y, 0, c.H - 1);
                wet[sg.I(i, j)] = m.cells[c.Index(x, y)].object == kWaterMark ? 1 : 0;
            }
        }
        const std::vector<std::int32_t> to_sea = Chamfer(sg, Biggest(sg, wet, true));
        auto sea_d = [&](std::size_t i) {
            const Pt p = CellCentre(static_cast<std::int32_t>(i % static_cast<std::size_t>(c.W)),
                                    static_cast<std::int32_t>(i / static_cast<std::size_t>(c.W)));
            const auto ni =
                static_cast<std::int32_t>(std::clamp<std::int64_t>((p.x + kSeaStep / 2) / kSeaStep, 0, sg.w - 1));
            const auto nj =
                static_cast<std::int32_t>(std::clamp<std::int64_t>((p.y + kSeaStep / 2) / kSeaStep, 0, sg.h - 1));
            return std::int64_t{to_sea[sg.I(ni, nj)]};
        };
        // The pairs' first groups are the first ring 2 groups, in the pairs' order.
        std::vector<std::int64_t> least_sea(pairs.size());
        const std::int64_t band_near = SeaBandOf(g, layout->sea_band)[0];
        for (std::size_t q = 0; q < pairs.size(); ++q) {
            std::int64_t most = 0;
            for (const auto& o : layout->groups[q].cells) most = std::max(most, sea_d(c.Index(o[0], o[1])));
            least_sea[q] = std::max(band_near + 1500, most + 1000);
        }
        auto apart = [&](std::size_t q, std::size_t i, std::int64_t gap) {
            const Pt p = CellCentre(static_cast<std::int32_t>(i % static_cast<std::size_t>(c.W)),
                                    static_cast<std::int32_t>(i / static_cast<std::size_t>(c.W)));
            for (const auto& o : layout->groups[q].cells) {
                if (Dist2(p, CellCentre(o[0], o[1])) < gap * gap) return false;
            }
            return true;
        };
        const std::function<bool(std::size_t, std::size_t)> inland = [&](std::size_t q, std::size_t i) {
            return sea_d(i) >= least_sea[q] && apart(q, i, 3000);
        };
        std::size_t q = 0;
        bool done =
            find_places(pairs, inland, &range, &places, &q) && place_pairs(pairs, range, places, inland, true, &q);
        if (!done && n == 2) {  // both groups again as on a land map, in one window
            d.Remove(layout->groups[0].cells);
            layout->groups.clear();
            pairs.push_back(pairs[0]);
            done = find_places(pairs, {}, &range, &places, &q) && place_pairs(pairs, range, places, {}, false, &q);
            if (!done) {
                *why = "free deposits: contested groups";
                return false;
            }
        }
        return true;
    };

    if (sea && n == 2 && !inland_pass()) return false;

    // Ring 3, the shared group (land and Sea maps): a rich group at the fairest place of the map, reached from every
    // start, where the starts' paths are within 0.50 (the check's 0.40 with the deposits' spread). Before decision 56
    // it lay within 900 px of the map's middle; the starts are fitted per code now, so the fairest place is wherever
    // the layout puts it, and a map without one (0.50 nowhere) has no such group. A Sea map (decision 57) has it too,
    // and none when the group finds no room there.
    if (!recipe.middle.empty()) {
        auto reached = [&](std::size_t i) {
            for (std::size_t s = 0; s < n; ++s) {
                if (d.dist[s][i] < 0) return false;
            }
            return true;
        };
        // The players' paths there, smallest over largest (per mille); the centre among the cells within 30 of the
        // best, each deposit within 60.
        auto ratio = [&](std::size_t i) {
            std::int64_t lo = -1, hi = 1;
            for (std::size_t s = 0; s < n; ++s)
                lo = lo < 0 || d.dist[s][i] < lo ? d.dist[s][i] : lo, hi = std::max<std::int64_t>(hi, d.dist[s][i]);
            return lo * 1000 / hi;
        };
        std::vector<std::int32_t> near;
        std::int64_t best = 0;
        for (std::int32_t y = 0; y < c.H; ++y) {
            for (std::int32_t x = 0; x < c.W; ++x) {
                if (!d.Open(x, y) || !reached(c.Index(x, y))) continue;
                near.push_back(static_cast<std::int32_t>(c.Index(x, y)));
                best = std::max(best, ratio(c.Index(x, y)));
            }
        }
        std::vector<std::int32_t> centres;
        for (const std::int32_t i : near) {
            if (ratio(static_cast<std::size_t>(i)) >= best - 30) centres.push_back(i);
        }
        if (best >= 500) {
            if (!d.Group(
                    centres, recipe.middle, 650,
                    [&](std::size_t i) { return reached(i) && ratio(i) >= std::max<std::int64_t>(450, best - 60); },
                    100, &cells)) {
                if (!sea) {
                    *why = "free deposits: the middle group";
                    return false;
                }
            } else {
                keep(3, -1, -1);
            }
        }
    }

    // Ring 1, own: per player and own group of the recipe a group in its own territory, on its outer side (no nearer
    // the middle than its start less 600 px on a land map), clearly its own (every other start 1.2 times as
    // far by path). Every player's group in one window of path distance (lo to 1.15 lo, from about 0.8 of the
    // territory's radius down) and one window of the computers' measure (a0 to 1.24 a0, |dx| + |dy| in cells and rows:
    // the check's 0.80 holds by construction), so the expansions are as far for every player, for a person and for a
    // computer alike. A second own group keeps 2,200 px from the first.
    std::vector<std::int64_t> start_mid(n);
    std::int64_t terr_cells = 0;
    for (std::size_t s = 0; s < n; ++s) start_mid[s] = ISqrt(Dist2(c.starts[s], middle));
    for (std::int8_t t : layout->territory) terr_cells += t == 0 ? 1 : 0;
    const std::int64_t radius = ISqrt(terr_cells * 2304 * 1000 / 3142);
    const std::int64_t lo0 = std::clamp<std::int64_t>(radius * 8 / 10, 2200, 5000);
    std::vector<std::int64_t> windows;
    for (std::int64_t v = lo0; v >= (islands ? 1700 : 2000); v = v * 94 / 100) windows.push_back(v);
    for (std::int64_t v = lo0 * 106 / 100, k = 0; k < 6; v = v * 106 / 100, ++k) windows.push_back(v);
    std::vector<std::size_t> own_first(n, 0);
    for (std::size_t gi = 0; gi < recipe.own.size(); ++gi) {
        const std::size_t first = layout->groups.size();
        auto own = [&](std::size_t i, std::size_t p, std::int64_t wlo, std::int64_t whi) {
            if (layout->territory[i] != static_cast<std::int8_t>(p)) return false;
            const std::int32_t dp = d.dist[p][i];
            if (dp < wlo || dp > whi) return false;
            const auto x = static_cast<std::int32_t>(i % static_cast<std::size_t>(c.W));
            const auto y = static_cast<std::int32_t>(i / static_cast<std::size_t>(c.W));
            const Pt pt = CellCentre(x, y);
            if (layout->pattern == 0 && ISqrt(Dist2(pt, middle)) < start_mid[p] - 600) return false;
            for (std::size_t q = 0; q < n; ++q) {
                if (q != p && d.dist[q][i] >= 0 && d.dist[q][i] * 10 < dp * 12) return false;
            }
            if (gi > 0) {
                const MapGenGroup& g0 = layout->groups[own_first[p]];
                for (const auto& o : g0.cells) {
                    if (Dist2(pt, CellCentre(o[0], o[1])) < 2200 * 2200) return false;
                }
            }
            return true;
        };
        bool done = false;
        std::vector<std::vector<std::int32_t>> mine(n);      // per player its places at any distance, in cell order
        std::vector<std::int8_t> owner(m.cells.size(), -1);  // and whose place a cell is (open or not)
        for (std::int32_t y = 0; y < c.H; ++y) {
            for (std::int32_t x = 0; x < c.W; ++x) {
                const std::size_t i = c.Index(x, y);
                const std::int8_t t = layout->territory[i];
                if (t < 0 || !own(i, static_cast<std::size_t>(t), 0, 1 << 30)) continue;
                owner[i] = t;
                if (d.Open(x, y)) mine[static_cast<std::size_t>(t)].push_back(static_cast<std::int32_t>(i));
            }
        }
        for (const std::int64_t wlo : windows) {
            const std::int64_t whi = wlo * 115 / 100;
            std::vector<std::vector<std::int32_t>> cand(n);
            for (std::size_t p = 0; p < n; ++p) {
                for (const std::int32_t i : mine[p]) {
                    const std::int32_t dp = d.dist[p][static_cast<std::size_t>(i)];
                    if (dp >= wlo && dp <= whi) cand[p].push_back(i);
                }
            }
            bool all = true;
            for (const auto& v : cand) all = all && !v.empty();
            if (!all) continue;
            // The computers' window: a0 drawn from the first player's candidates whose window [a0, 1.15 a0] meets
            // every player's range of the measure (a sea map's territories face the sea in different directions, and
            // the measure counts a row as a quarter of a cell).
            std::int32_t f_lo = 0, f_hi = 1 << 30;
            for (std::size_t p = 0; p < n; ++p) {
                std::int32_t lo = 1 << 30, hi = 0;
                for (const std::int32_t i : cand[p]) {
                    const std::int32_t f = d.Finder(p, i % c.W, i / c.W);
                    lo = std::min(lo, f), hi = std::max(hi, f);
                }
                f_lo = std::max(f_lo, lo), f_hi = std::min(f_hi, hi);
            }
            std::vector<std::int32_t> firsts;
            for (const std::int32_t i : cand[0]) {
                const std::int32_t f = d.Finder(0, i % c.W, i / c.W);
                if (f * 124 / 100 >= f_lo && f <= f_hi) firsts.push_back(i);
            }
            for (int t = 0; t < 24 && !done && !firsts.empty(); ++t) {
                const std::int32_t i0 = firsts[r.below(static_cast<std::uint32_t>(firsts.size()))];
                const std::int32_t a0 = d.Finder(0, i0 % c.W, i0 / c.W);
                const std::int32_t a1 = a0 * 124 / 100;
                auto in_ai = [&](std::size_t p, std::size_t i) {
                    const std::int32_t f = d.Finder(p, static_cast<std::int32_t>(i % static_cast<std::size_t>(c.W)),
                                                    static_cast<std::int32_t>(i / static_cast<std::size_t>(c.W)));
                    return f >= a0 && f <= a1;
                };
                bool ok = true;
                for (std::size_t p = 0; p < n && ok; ++p) {
                    std::vector<std::int32_t> centres;
                    for (const std::int32_t i : cand[p]) {
                        if (in_ai(p, static_cast<std::size_t>(i))) centres.push_back(i);
                    }
                    ok = d.Group(
                        centres, recipe.own[gi], 600,
                        [&](std::size_t i) {
                            const std::int32_t dp = d.dist[p][i];
                            return owner[i] == static_cast<std::int8_t>(p) && dp >= wlo && dp <= whi * 11 / 10 &&
                                   in_ai(p, i);
                        },
                        40, &cells);
                    if (ok) {
                        if (gi == 0) own_first[p] = layout->groups.size();
                        keep(1, static_cast<std::int8_t>(p), -1);
                    }
                }
                // Every player's nearest deposit of the group as the check takes it within 0.87 (the check's 0.85
                // with a margin).
                std::int32_t glo = -1, ghi = 0;
                for (std::size_t k = first; ok && k < layout->groups.size(); ++k) {
                    const MapGenGroup& gr = layout->groups[k];
                    const std::int32_t v = d.NearGroup(static_cast<std::size_t>(gr.players[0]), gr.cells);
                    ok = v >= 0;
                    glo = glo < 0 || v < glo ? v : glo, ghi = std::max(ghi, v);
                }
                ok = ok && glo * 100 >= ghi * 87;
                if (ok) {
                    done = true;
                } else {
                    for (std::size_t k = first; k < layout->groups.size(); ++k) d.Remove(layout->groups[k].cells);
                    layout->groups.resize(first);
                }
            }
            if (done) break;
        }
        // The first own group for everyone, a Large map's second where it fits for all (decision 54; a sea's
        // territories are strips and pockets, a land map's follow the fitted starts), else for none (the counts stay
        // equal).
        if (!done && gi > 0) break;
        if (!done && crowded) break;
        if (!done) {
            *why = "free deposits: own group " + std::to_string(gi + 1);
            return false;
        }
    }

    if (sea && n > 2 && !inland_pass()) return false;

    // Ring 4: per small neutral island (decision 53) a group of gold, iron and oil, which only ships reach.
    for (const auto& isle : layout->isles) {
        const Pt cen{isle[0], isle[1]};
        std::vector<std::int32_t> centres;
        std::int32_t cx = 0, cy = 0;
        CellAt(cen, &cx, &cy);
        if (c.Inside(cx, cy) && m.cells[c.Index(cx, cy)].object != kWaterMark)
            centres.push_back(static_cast<std::int32_t>(c.Index(cx, cy)));
        if (d.Group(
                centres, {0, 1, 2}, isle[2] - 100, [&](std::size_t i) { return m.cells[i].object != kWaterMark; }, 40,
                &cells))
            keep(4, -1, -1);
    }
    return true;
}

void MapGen_TankLanes(MapData& m, const MapGenLayout& layout) {
    Ctx c(m, layout);
    const std::int32_t W = c.W, H = c.H;
    const std::size_t n = m.cells.size();
    const std::vector<std::uint8_t> water = BlockedCells(m);
    // Per cell 1 when a tank crosses it once its object is gone (MapGen_Check's TankReach less the objects).
    std::vector<std::uint8_t> ground(n, 0);
    for (std::int32_t y = 10; y < H - 9; ++y) {
        for (std::int32_t x = 4; x < W - 3; ++x) {
            const std::size_t i = c.Index(x, y);
            if (water[i] != 0 || IsDeposit(m.cells[i].object) || m.cells[i].object == kWaterMark) continue;
            const int odd = y & 1;
            const int h[4] = {m.cells[i].height, m.cells[c.Index(x + odd, y - 1)].height, m.cells[i + 1].height,
                              m.cells[c.Index(x + odd, y + 1)].height};
            ground[i] = *std::max_element(h, h + 4) - *std::min_element(h, h + 4) <= 25 ? 1 : 0;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!IsDeposit(m.cells[i].object)) continue;
        std::int32_t x = static_cast<std::int32_t>(i % static_cast<std::size_t>(W));
        std::int32_t y = static_cast<std::int32_t>(i / static_cast<std::size_t>(W));
        Step(&x, &y, 1);
        for (const auto& d : {std::array<std::int32_t, 2>{0, 0}, {0, 2}, {-1, 2}, {-1, 0}}) {
            if (c.Inside(x + d[0], y + d[1])) ground[c.Index(x + d[0], y + d[1])] = 0;
        }
    }
    // An object may go outside the zones (their kit stays as counted) and 100 px round them.
    auto cuttable = [&](std::size_t i) {
        const Pt p = CellCentre(static_cast<std::int32_t>(i % static_cast<std::size_t>(W)),
                                static_cast<std::int32_t>(i / static_cast<std::size_t>(W)));
        for (const Pt& s : c.starts) {
            if (Dist2(p, s) < (kZoneR + 100) * (kZoneR + 100)) return false;
        }
        return true;
    };
    const std::size_t players = c.starts.size();
    auto isl = [&](std::size_t p) { return layout.island.empty() ? std::int8_t{0} : layout.island[p]; };
    std::vector<std::int8_t> seen(n, -1);  // the island whose tanks reach the cell
    auto flood = [&](std::vector<std::int32_t> todo, std::int8_t id) {
        for (std::size_t t = 0; t < todo.size(); ++t) {
            for (int dir = 1; dir <= 8; ++dir) {
                std::int32_t x = todo[t] % W, y = todo[t] / W;
                Step(&x, &y, dir);
                if (!c.Inside(x, y)) continue;
                const std::size_t v = c.Index(x, y);
                if (ground[v] == 0 || m.cells[v].object != 0 || seen[v] >= 0) continue;
                seen[v] = id;
                todo.push_back(static_cast<std::int32_t>(v));
            }
        }
    };
    for (std::size_t s0 = 0; s0 < players; ++s0) {
        if (s0 > 0 && isl(s0 - 1) == isl(s0)) continue;
        const auto src = static_cast<std::size_t>(c.start_cells[s0]);
        if (ground[src] == 0 || m.cells[src].object != 0) continue;
        seen[src] = isl(s0);
        flood({c.start_cells[s0]}, isl(s0));
    }
    // The targets as TankReach takes them, each with its island (-1: any): per start its cell, per free group (not a
    // neutral island's) the cells within 300 px of its deposits, per port site its box 1.
    std::vector<std::pair<std::int8_t, std::vector<std::int32_t>>> targets;
    for (std::size_t p = 0; p < players; ++p) targets.push_back({isl(p), {c.start_cells[p]}});
    for (const MapGenGroup& gr : layout.groups) {
        if (gr.ring == 4) continue;
        std::vector<std::int32_t> cells;
        for (const auto& o : gr.cells) {
            const Pt q = CellCentre(o[0], o[1]);
            for (std::int32_t y = std::max(0, o[1] - 13); y <= std::min(H - 1, o[1] + 13); ++y) {
                for (std::int32_t x = std::max(0, o[0] - 4); x <= std::min(W - 1, o[0] + 4); ++x) {
                    if (Dist2(CellCentre(x, y), q) <= 300 * 300)
                        cells.push_back(static_cast<std::int32_t>(c.Index(x, y)));
                }
            }
        }
        const std::int8_t id = gr.players[0] >= 0 ? isl(static_cast<std::size_t>(gr.players[0])) : std::int8_t{0};
        targets.push_back({id, cells});
    }
    std::vector<std::int32_t> box;
    for (std::uint32_t v : m.port_sites) {
        Box1(W, H, static_cast<std::int32_t>(v & 0xffff), static_cast<std::int32_t>(v >> 16), kPort, &box);
        targets.push_back({std::int8_t{-1}, box});
    }
    // An unreached target: the cheapest way to it from its island's reached cells (an open cell costs 1, a cell whose
    // object may go 8), its objects removed, the new cells flooded.
    std::vector<std::int32_t> cost(n), from(n);
    std::vector<std::uint8_t> goal(n, 0);
    auto ours = [&](std::size_t i, std::int8_t id) { return seen[i] >= 0 && (id < 0 || seen[i] == id); };
    for (const auto& [id, cells] : targets) {
        bool got = false;
        for (std::int32_t i : cells) got = got || ours(static_cast<std::size_t>(i), id);
        if (got) continue;
        for (std::int32_t i : cells) goal[static_cast<std::size_t>(i)] = 1;
        std::fill(cost.begin(), cost.end(), -1);
        std::array<std::vector<std::int32_t>, 9> ring;  // the bucket queue (steps of 1 or 8)
        std::size_t pending = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (!ours(i, id)) continue;
            cost[i] = 0, from[i] = -1, ++pending;
            ring[0].push_back(static_cast<std::int32_t>(i));
        }
        std::int32_t hit = -1;
        for (std::int32_t dcur = 0; pending > 0 && hit < 0; ++dcur) {
            std::vector<std::int32_t>& bucket = ring[static_cast<std::size_t>(dcur % 9)];
            for (std::size_t t = 0; t < bucket.size() && hit < 0; ++t) {
                const std::int32_t u = bucket[t];
                --pending;
                if (cost[static_cast<std::size_t>(u)] != dcur) continue;
                if (goal[static_cast<std::size_t>(u)] != 0) {
                    hit = u;
                    break;
                }
                for (int dir = 1; dir <= 8; ++dir) {
                    std::int32_t x = u % W, y = u / W;
                    Step(&x, &y, dir);
                    if (!c.Inside(x, y)) continue;
                    const std::size_t v = c.Index(x, y);
                    if (ground[v] == 0) continue;
                    const bool free = m.cells[v].object == 0;
                    if (!free && !cuttable(v)) continue;
                    const std::int32_t nd = dcur + (free ? 1 : 8);
                    if (cost[v] >= 0 && cost[v] <= nd) continue;
                    cost[v] = nd, from[v] = u, ++pending;
                    ring[static_cast<std::size_t>(nd % 9)].push_back(static_cast<std::int32_t>(v));
                }
            }
            bucket.clear();
        }
        for (std::int32_t i : cells) goal[static_cast<std::size_t>(i)] = 0;
        if (hit < 0) continue;  // no way at all: the check says so
        std::int32_t v0 = hit;
        while (seen[static_cast<std::size_t>(v0)] < 0) v0 = from[static_cast<std::size_t>(v0)];
        const std::int8_t to = seen[static_cast<std::size_t>(v0)];
        std::vector<std::int32_t> path;
        for (std::int32_t v = hit; v >= 0 && seen[static_cast<std::size_t>(v)] < 0;
             v = from[static_cast<std::size_t>(v)]) {
            m.cells[static_cast<std::size_t>(v)].object = 0;
            seen[static_cast<std::size_t>(v)] = to;
            path.push_back(v);
        }
        flood(path, to);
    }
}

void MapGen_Forests(MapData& m, const MapGenSettings& g, const MapGenLayout& layout, std::uint64_t key,
                    const mapgen::SeaDesign* belts) {
    Ctx c(m, layout);
    Rng r{key};
    const Climate& cl = g.winter ? kWinter : kSummer;
    const std::size_t n = m.cells.size();
    const std::uint64_t k_field = r.next(), k_field2 = r.next(), k_draw = r.next(), k_stand = r.next(), k_id = r.next();

    // Where nothing grows: a zone and the inside of its margin, whose edge wavers between 48 and 300 px out (a slow
    // noise per start), a deposit's room. The rest of the margin and the corridors grow at half the chance
    // (`thin`), so neither shows as a clean ring or stripe (2026-11-06).
    std::vector<std::uint8_t> open(n, 1), thin(n, 0);
    const std::uint64_t k_keep = r.next();
    std::vector<std::int32_t> deposits, isle_deposits;  // the latter on neutral islands: only ships reach them
    for (const MapGenGroup& gr : layout.groups) {
        for (const auto& o : gr.cells) {
            if (gr.ring == 4) isle_deposits.push_back(static_cast<std::int32_t>(c.Index(o[0], o[1])));
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (IsDeposit(m.cells[i].object)) deposits.push_back(static_cast<std::int32_t>(i));
    }
    for (std::int32_t y = 0; y < c.H; ++y) {
        for (std::int32_t x = 0; x < c.W; ++x) {
            const std::size_t i = c.Index(x, y);
            const Pt p = CellCentre(x, y);
            bool ok = m.cells[i].object == 0;
            bool half = !layout.corridor.empty() && layout.corridor[i] != 0;
            for (std::size_t s = 0; s < c.starts.size() && ok; ++s) {
                const std::int64_t d2 = Dist2(p, c.starts[s]);
                if (d2 > (kZoneR + kMargin) * (kZoneR + kMargin)) continue;
                const std::int64_t edge = kZoneR + 48 + Noise(k_keep + s, p.x, p.y, 700) * 252 / 65536;
                ok = d2 > edge * edge;
                half = true;
            }
            open[i] = ok ? 1 : 0;
            thin[i] = half ? 1 : 0;
        }
    }
    auto clear_disc = [&](Pt p, std::int64_t rad) {
        std::int32_t px = 0, py = 0;
        CellAt(p, &px, &py);
        const auto rx = static_cast<std::int32_t>(rad / 96) + 1, ry = static_cast<std::int32_t>(rad / 24) + 1;
        for (std::int32_t y = std::max(0, py - ry); y <= std::min(c.H - 1, py + ry); ++y) {
            for (std::int32_t x = std::max(0, px - rx); x <= std::min(c.W - 1, px + rx); ++x) {
                if (Dist2(CellCentre(x, y), p) <= rad * rad) open[c.Index(x, y)] = 0;
            }
        }
    };
    // No lanes are cut to the water or to the deposits: men walk through trees and stones, and MapGen_TankLanes
    // opens the way for tanks where the wood closes it (2026-11-06: the cut lanes showed as straight stripes).
    for (std::int32_t i : deposits) clear_disc(CellCentre(i % c.W, i / c.W), kRoom + 48);
    std::int64_t land = 0;  // every cell but the water (kWaterMark, sim/mapgen_water.hpp)
    for (const MapCell& cell : m.cells) land += cell.object != kWaterMark ? 1 : 0;

    // The forest field: two octaves of value noise in world pixels.
    const bool dense = g.dense_forest;
    const std::int64_t s1 = dense ? 3200 : 2000, s2 = dense ? 900 : 640;
    std::vector<std::int32_t> field(n, -1);
    for (std::int32_t y = 0; y < c.H; ++y) {
        for (std::int32_t x = 0; x < c.W; ++x) {
            const std::size_t i = c.Index(x, y);
            if (open[i] == 0) continue;
            const Pt p = CellCentre(x, y);
            field[i] =
                static_cast<std::int32_t>((2 * Noise(k_field, p.x, p.y, s1) + Noise(k_field2, p.x, p.y, s2)) / 3);
        }
    }
    // Targets (per mille of the land): all trees, the lone ones. Counted in the play area as the measure does (the
    // shipped maps' edge band is left out; the groves still grow into it).
    auto play = [&](std::size_t i) {
        const auto x = static_cast<std::int32_t>(i % static_cast<std::size_t>(c.W));
        const auto y = static_cast<std::int32_t>(i / static_cast<std::size_t>(c.W));
        return x >= 10 && x < c.W - 10 && y >= 20 && y < c.H - 20;
    };
    std::int64_t play_land = 0, kit_play = 0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!play(i) || m.cells[i].object == kWaterMark) continue;
        ++play_land;
        kit_play += IsTree(m.cells[i].object) ? 1 : 0;
    }
    const std::int64_t share = belts != nullptr ? (dense ? r.range(200, 260) : r.range(140, 180))
                                                : (dense ? r.range(170, 230) : r.range(95, 120));
    const std::int64_t lone_share = dense ? r.range(160, 200) : r.range(200, 260);
    const std::int64_t total = play_land * share / 1000, lone = total * lone_share / 1000;
    const std::int64_t grove_target = std::max<std::int64_t>(0, total - kit_play - lone);
    // A cell's chance (of 65,536) rises from the threshold T over `ramp` to pmax: dense in the middle of a stand,
    // thin at its edge. T is found by bisection so the expected count meets the target. Dense forest on crowded maps
    // (the clearings take a third of the land): pmax rises until the target fits.
    const std::int64_t ramp = dense ? 5000 : 6000;
    std::int64_t pmax = dense ? 30000 : 22000;
    auto chance = [&](std::int32_t f, std::int64_t t) -> std::int64_t {
        if (f <= t) return 0;
        return std::min<std::int64_t>(pmax, (f - t) * pmax / ramp);
    };
    // Each territory (and the shared middle) gets its share of the target by its buildable land, with its own threshold
    // (and its own cap where its noise is low), so every territory grows the same number of trees (plan 5.7,
    // 2026-11-05).
    const std::size_t labels = c.starts.size() + 1;
    auto label_of = [&](std::size_t i) {
        const std::int8_t t = layout.territory.empty() ? std::int8_t{-1} : layout.territory[i];
        return t < 0 ? labels - 1 : static_cast<std::size_t>(t);
    };
    // The Woodlands' forest belts and great woods (decision 56) first: thick stands (about half of the cells inside,
    // thinning over 250 px past the edge), the corridors cut through them (tanks' ways; men walk through trees
    // anyway), counted per territory so every territory still grows its share of the trees below.
    std::vector<std::int32_t> trees;
    std::vector<std::int64_t> have(labels, 0);
    if (belts != nullptr) {
        const auto stands_n = static_cast<std::int64_t>(cl.stands.size());
        const std::uint64_t k_belt = k_draw ^ 0x3c6ef372u;
        for (std::int32_t y = 0; y < c.H; ++y) {
            for (std::int32_t x = 0; x < c.W; ++x) {
                const std::size_t i = c.Index(x, y);
                if (field[i] < 0 || (!layout.corridor.empty() && layout.corridor[i] != 0)) continue;
                const Pt p = CellCentre(x, y);
                const std::int64_t v = SeaDesignAt(*belts, p.x, p.y);
                if (v <= -250) continue;
                const std::int64_t chance_b = 33000 * std::clamp<std::int64_t>(v + 250, 0, 250) / 250;
                if (static_cast<std::int64_t>(Hash(k_belt, x, y) & 0xffff) >= chance_b) continue;
                const std::int64_t st = std::min(stands_n - 1, Noise(k_stand, p.x, p.y, 2400) * stands_n / 65536);
                m.cells[i].object = Pick(*cl.stands[static_cast<std::size_t>(st)], Hash(k_id, x, y));
                trees.push_back(static_cast<std::int32_t>(i));
                field[i] = -1;
                if (play(i)) ++have[label_of(i)];
            }
        }
    }
    std::vector<std::vector<std::int32_t>> members(labels);
    std::vector<std::int64_t> area(labels, 0);  // buildable land, as the territories are cut (decision 51)
    std::int64_t area_all = 0;
    const std::vector<std::uint8_t> buildable = BuildableLand(m);
    for (std::size_t i = 0; i < n; ++i) {
        area[label_of(i)] += buildable[i], area_all += buildable[i];
        if (field[i] >= 0) members[label_of(i)].push_back(static_cast<std::int32_t>(i));
    }
    area_all = std::max<std::int64_t>(1, area_all);
    // A territory whose noise is low everywhere raises its own cap until its share fits.
    std::vector<std::int64_t> cap(labels, 0);
    auto expected = [&](std::size_t l, std::int64_t t) {
        std::int64_t expect = 0;
        for (std::int32_t i : members[l]) expect += chance(field[static_cast<std::size_t>(i)], t) * cap[l] / pmax;
        return expect / 65536;
    };
    auto expected_all = [&](std::int64_t t) {
        std::int64_t e = 0;
        for (std::size_t l = 0; l < labels; ++l) e += expected(l, t);
        return e;
    };
    std::fill(cap.begin(), cap.end(), pmax);
    while (dense && pmax < 50000 && expected_all(0) * 10 < grove_target * 15) {
        pmax += 4000;
        std::fill(cap.begin(), cap.end(), pmax);
    }
    // The whole map's target (counted in the play area) shared out by each territory's buildable land, less the
    // belt trees it already has.
    std::vector<std::int64_t> thr(labels, 65536);
    for (std::size_t l = 0; l < labels; ++l) {
        const std::int64_t target = std::max<std::int64_t>(0, grove_target * area[l] / area_all - have[l]);
        while (cap[l] < 60000 && expected(l, 0) * 10 < target * 12) cap[l] += 4000;
        std::int64_t lo = 0, hi = 65536;
        while (hi - lo > 1) {
            const std::int64_t mid = (lo + hi) / 2;
            (expected(l, mid) > target ? lo : hi) = mid;
        }
        thr[l] = hi;
    }
    const auto stands = static_cast<std::int64_t>(cl.stands.size());
    for (std::int32_t y = 0; y < c.H; ++y) {
        for (std::int32_t x = 0; x < c.W; ++x) {
            const std::size_t i = c.Index(x, y);
            const std::size_t l = label_of(i);
            if (field[i] < 0 || static_cast<std::int64_t>(Hash(k_draw, x, y) & 0xffff) >=
                                    chance(field[i], thr[l]) * cap[l] / pmax / (thin[i] != 0 ? 2 : 1))
                continue;
            const Pt p = CellCentre(x, y);
            const std::int64_t st = std::min(stands - 1, Noise(k_stand, p.x, p.y, 2400) * stands / 65536);
            m.cells[i].object = Pick(*cl.stands[static_cast<std::size_t>(st)], Hash(k_id, x, y));
            trees.push_back(static_cast<std::int32_t>(i));
        }
    }

    // Lone trees and clumps of 2 or 3 on the open land, two cells and more from any tree, each territory its share.
    auto clear_around = [&](std::int32_t x, std::int32_t y) {
        for (std::int32_t yy = y - 4; yy <= y + 4; ++yy) {
            for (std::int32_t xx = x - 2; xx <= x + 2; ++xx) {
                if (c.Inside(xx, yy) && IsTree(m.cells[c.Index(xx, yy)].object)) return false;
            }
        }
        return true;
    };
    std::vector<std::int64_t> quota(labels, 0);
    for (std::size_t l = 0; l < labels; ++l) quota[l] = lone * area[l] / area_all;
    // Each territory in turn from its own open cells, so a territory with little open land still gets its share.
    for (std::size_t l0 = 0; l0 < labels; ++l0) {
        const std::vector<std::int32_t>& pool = members[l0];
        for (std::int64_t t = 0; quota[l0] > 0 && !pool.empty() && t < quota[l0] * 40 + 40; ++t) {
            const std::int32_t i0 = pool[r.below(static_cast<std::uint32_t>(pool.size()))];
            auto x = i0 % c.W, y = i0 / c.W;
            if (m.cells[static_cast<std::size_t>(i0)].object != 0 || !clear_around(x, y)) continue;
            const std::uint32_t roll = r.below(100);
            const int size = roll < 60 ? 1 : roll < 85 ? 2 : 3;
            for (int k = 0; k < size; ++k) {
                const std::size_t i = c.Index(x, y);
                if (open[i] != 0 && m.cells[i].object == 0) {
                    m.cells[i].object = Pick(*cl.trees, Hash(k_id ^ 0x5bd1e995u, x, y));
                    trees.push_back(static_cast<std::int32_t>(i));
                    --quota[label_of(i)];
                }
                std::int32_t nx = x, ny = y;
                Step(&nx, &ny, static_cast<int>(r.range(1, 8)));
                if (c.Inside(nx, ny)) x = nx, y = ny;
            }
        }
    }

    // A territory whose open land is too thick with groves for its lone trees grows them in its groves instead (the
    // empty cells of its highest forest field first), so every territory keeps its share of the trees.
    for (std::size_t l = 0; l < labels; ++l) {
        if (quota[l] <= 0) continue;
        std::vector<std::pair<std::int32_t, std::int32_t>> empty;  // (-field, cell)
        for (const std::int32_t i : members[l]) {
            if (m.cells[static_cast<std::size_t>(i)].object == 0)
                empty.emplace_back(-field[static_cast<std::size_t>(i)], i);
        }
        const auto take =
            static_cast<std::size_t>(std::min<std::int64_t>(quota[l], static_cast<std::int64_t>(empty.size())));
        std::partial_sort(empty.begin(), empty.begin() + static_cast<std::ptrdiff_t>(take), empty.end());
        for (std::size_t k = 0; k < take; ++k) {
            const std::int32_t i = empty[k].second;
            const Pt p = CellCentre(i % c.W, i / c.W);
            const std::int64_t st = std::min(stands - 1, Noise(k_stand, p.x, p.y, 2400) * stands / 65536);
            m.cells[static_cast<std::size_t>(i)].object =
                Pick(*cl.stands[static_cast<std::size_t>(st)], Hash(k_id, i % c.W, i / c.W));
            trees.push_back(i);
        }
        quota[l] -= static_cast<std::int64_t>(take);
    }

    // Stones: single, most of them next to a tree.
    std::int64_t kit_stones = 0;
    for (const MapCell& cell : m.cells) kit_stones += IsStone(cell.object) ? 1 : 0;
    const std::int64_t stones = std::max<std::int64_t>(0, land * r.range(40, 65) / 10000 - kit_stones);
    for (std::int64_t k = 0, t = 0; k < stones && t < stones * 40 && !trees.empty(); ++t) {
        std::int32_t x = 0, y = 0;
        if (r.below(100) < 85) {
            const std::int32_t i = trees[r.below(static_cast<std::uint32_t>(trees.size()))];
            x = i % c.W, y = i / c.W;
            Step(&x, &y, static_cast<int>(r.range(1, 8)));
        } else {
            x = static_cast<std::int32_t>(r.below(static_cast<std::uint32_t>(c.W)));
            y = static_cast<std::int32_t>(r.below(static_cast<std::uint32_t>(c.H)));
        }
        if (!c.Inside(x, y) || open[c.Index(x, y)] == 0 || m.cells[c.Index(x, y)].object != 0) continue;
        m.cells[c.Index(x, y)].object = Pick(*cl.stones, Hash(k_id ^ 0x27d4eb2fu, x, y));
        ++k;
    }

    // Small decoration on the open land: 10 to 40 on a Normal map (the shipped maps: 0 to 462, most 10 to 40), by
    // the area on a Large one.
    const std::int64_t deco = r.range(10, 40) * land / 80000;
    for (std::int64_t k = 0, t = 0; k < deco && t < deco * 40; ++t) {
        const auto x = static_cast<std::int32_t>(r.below(static_cast<std::uint32_t>(c.W)));
        const auto y = static_cast<std::int32_t>(r.below(static_cast<std::uint32_t>(c.H)));
        if (open[c.Index(x, y)] == 0 || m.cells[c.Index(x, y)].object != 0) continue;
        m.cells[c.Index(x, y)].object = Pick(*cl.deco, Hash(k_id ^ 0x85ebca6bu, x, y));
        ++k;
    }

    // Every free deposit stays reachable: a deposit no start reaches gets a lane cut toward the nearest start.
    std::vector<std::uint8_t> walk = Walk(m);
    std::vector<std::uint8_t> seen(n, 0);
    auto flood = [&] {
        std::fill(seen.begin(), seen.end(), std::uint8_t{0});
        std::vector<std::int32_t> todo = c.start_cells;
        for (std::int32_t s : todo) seen[static_cast<std::size_t>(s)] = 1;
        while (!todo.empty()) {
            const std::int32_t u = todo.back();
            todo.pop_back();
            for (int dir = 1; dir <= 8; ++dir) {
                std::int32_t x = u % c.W, y = u / c.W;
                Step(&x, &y, dir);
                if (!c.Inside(x, y)) continue;
                const std::size_t v = c.Index(x, y);
                if (walk[v] == 0 || seen[v] != 0) continue;
                seen[v] = 1;
                todo.push_back(static_cast<std::int32_t>(v));
            }
        }
    };
    flood();
    for (std::int32_t i : deposits) {
        if (std::find(isle_deposits.begin(), isle_deposits.end(), i) != isle_deposits.end()) continue;
        const std::int32_t dx = i % c.W, dy = i / c.W;
        bool reached = false;
        for (int dir = 1; dir <= 8 && !reached; ++dir) {
            std::int32_t x = dx, y = dy;
            Step(&x, &y, dir);
            reached = c.Inside(x, y) && seen[c.Index(x, y)] != 0;
        }
        if (reached) continue;
        const Pt p = CellCentre(dx, dy);
        const Pt* to = &c.starts[0];
        for (const Pt& s : c.starts) to = Dist2(s, p) < Dist2(*to, p) ? &s : to;
        const std::int64_t len = ISqrt(Dist2(p, *to));
        for (std::int64_t t = 0; t <= len; t += 24) {
            const Pt q{p.x + (to->x - p.x) * t / len, p.y + (to->y - p.y) * t / len};
            std::int32_t qx = 0, qy = 0;
            CellAt(q, &qx, &qy);
            if (c.Inside(qx, qy) && seen[c.Index(qx, qy)] != 0) break;
            for (std::int32_t y = qy - 3; y <= qy + 3; ++y) {
                for (std::int32_t x = qx - 1; x <= qx + 1; ++x) {
                    if (!c.Inside(x, y) || Dist2(CellCentre(x, y), q) > 72 * 72) continue;
                    std::uint16_t& o = m.cells[c.Index(x, y)].object;
                    if (IsTree(o) || IsStone(o) || (o >= 9605 && o < 9700)) o = 0;
                }
            }
        }
        walk = Walk(m);
        flood();
    }
}

MapGenWoodLook MapGen_WoodLook(const MapData& m, const MapGenLayout* layout) {
    MapGenWoodLook out;
    if (layout != nullptr && !layout->zones.empty()) {  // trees within 3,000 px of each start
        std::int64_t lo = -1, hi = 0;
        for (const MapGenZone& z : layout->zones) {
            const Pt s{z.x, z.y};
            std::int64_t k = 0;
            for (std::int32_t y = 0; y < m.height; ++y) {
                for (std::int32_t x = 0; x < m.width; ++x) {
                    k += IsTree(m.cells[static_cast<std::size_t>(y) * m.width + x].object) &&
                                 Dist2(CellCentre(x, y), s) <= 3000 * 3000
                             ? 1
                             : 0;
                }
            }
            lo = lo < 0 || k < lo ? k : lo, hi = std::max(hi, k);
        }
        out.wood_ratio = hi > 0 ? static_cast<std::int32_t>(lo * 1000 / hi) : 0;
    }
    const std::int32_t W = m.width, H = m.height;
    auto at = [&](std::int32_t x, std::int32_t y) { return m.cells[static_cast<std::size_t>(y) * W + x].object; };
    auto play = [&](std::int32_t x, std::int32_t y) { return x >= 10 && x < W - 10 && y >= 20 && y < H - 20; };
    std::int64_t cells = 0, trees = 0, stones = 0, near = 0;  // cells: the land of the play area (not its water)
    std::vector<std::int32_t> dens;
    const std::vector<std::uint8_t> water = BlockedCells(m);
    for (std::int32_t y = 0; y < H; ++y) {
        for (std::int32_t x = 0; x < W; ++x) {
            const std::uint16_t o = at(x, y);
            out.decoration += o >= 9605 && o < 9700 ? 1 : 0;
            if (!play(x, y)) continue;
            cells += water[static_cast<std::size_t>(y) * W + x] == 0 ? 1 : 0;
            if (IsStone(o)) {
                ++stones;
                bool t = false;
                for (std::int32_t yy = std::max(0, y - 7); yy <= std::min(H - 1, y + 7) && !t; ++yy) {
                    for (std::int32_t xx = std::max(0, x - 2); xx <= std::min(W - 1, x + 2) && !t; ++xx)
                        t = IsTree(at(xx, yy)) && Dist2(CellCentre(xx, yy), CellCentre(x, y)) < 150 * 150;
                }
                near += t ? 1 : 0;
            }
            if (!IsTree(o)) continue;
            ++trees;
            std::int32_t k = 0, all = 0;
            for (std::int32_t yy = y - 4; yy <= y + 4; ++yy) {
                for (std::int32_t xx = x - 2; xx <= x + 2; ++xx) {
                    const std::int32_t cx = std::clamp(xx, 0, W - 1), cy = std::clamp(yy, 0, H - 1);
                    k += IsTree(at(cx, cy)) ? 1 : 0, ++all;
                }
            }
            dens.push_back(k * 1000 / all);
        }
    }
    // Groves (8-connected) over the whole map.
    std::vector<std::int32_t> label(static_cast<std::size_t>(W) * H, -1);
    std::int64_t lone = 0, all_trees = 0;
    std::array<std::array<std::int32_t, 2>, 8> nb{};
    for (std::int32_t i0 = 0; i0 < W * H; ++i0) {
        if (!IsTree(m.cells[static_cast<std::size_t>(i0)].object) || label[static_cast<std::size_t>(i0)] >= 0) continue;
        std::vector<std::int32_t> todo{i0}, members;
        label[static_cast<std::size_t>(i0)] = i0;
        while (!todo.empty()) {
            const std::int32_t u = todo.back();
            todo.pop_back();
            members.push_back(u);
            Neighbours(u % W, u / W, &nb);
            for (const auto& q : nb) {
                if (q[0] < 0 || q[1] < 0 || q[0] >= W || q[1] >= H) continue;
                const std::size_t v = static_cast<std::size_t>(q[1]) * W + q[0];
                if (!IsTree(m.cells[v].object) || label[v] >= 0) continue;
                label[v] = i0;
                todo.push_back(static_cast<std::int32_t>(v));
            }
        }
        const auto size = static_cast<std::int32_t>(members.size());
        all_trees += size;
        lone += size <= 3 ? size : 0;
        if (size > out.biggest) {
            out.biggest = size;
            std::vector<std::uint16_t> ids;
            for (std::int32_t u : members) ids.push_back(m.cells[static_cast<std::size_t>(u)].object);
            std::sort(ids.begin(), ids.end());
            out.big_grove_ids = static_cast<std::int32_t>(std::unique(ids.begin(), ids.end()) - ids.begin());
        }
    }
    out.tree_share = cells > 0 ? static_cast<std::int32_t>(trees * 1000 / cells) : 0;
    out.lone_share = all_trees > 0 ? static_cast<std::int32_t>(lone * 1000 / all_trees) : 0;
    if (!dens.empty()) {
        std::nth_element(dens.begin(), dens.begin() + static_cast<std::ptrdiff_t>(dens.size() / 2), dens.end());
        out.density = dens[dens.size() / 2];
    }
    out.stones_per_k = cells > 0 ? static_cast<std::int32_t>(stones * 100000 / cells) : 0;
    out.stones_near = stones > 0 ? static_cast<std::int32_t>(near * 1000 / stones) : 0;
    return out;
}

}  // namespace antanta::sim
