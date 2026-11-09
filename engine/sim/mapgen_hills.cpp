// The random map generator's hills and mountains (docs/map-generator-plan.md 5.5, slice G5). See mapgen_hills.hpp.
//

// height and those of its up-right, right and down-right neighbours; over 25 blocks tanks, over 60 every land unit;
// CanPlaceHouse takes the span over a footprint (20). Distances here are "ground" pixels: a world point (x, y) lies at
// (x, 2y) on the ground (a cell's diamond is 96 x 48 world pixels, a square on the ground), so hills come out round on
// the ground. Neighbouring cells by Cell_Step's directions 1 to 4 are one "lattice step" (68 ground pixels) apart; a
// height difference of at most 20 per lattice step keeps every cell's corner span within 40.
//
// Mountains: a ridge's crest band (a set of corners within its half-width of the crest line) stands at least 62 above
// every corner next to it outside the band, so every cell with corners on both sides is over the walking rule. Any two
// neighbouring cells share a corner, so no walk crosses the band; the passes are the corridors, where the band is cut
// open (corners within 2 lattice steps of a corridor are never in it, and the corridor's own cells stay on the plain).
#include "mapgen_hills.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
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

// The smooth bump (1 - u^2)^2 for u in 1/1024 (0 from u = 1024 on), in 1/1024. Its steepest slope is 1.54 / radius.
std::int64_t Bump(std::int64_t u) {
    if (u >= 1024) return 0;
    const std::int64_t f = 1024 - u * u / 1024;
    return f * f / 1024;
}

constexpr std::int64_t kLattice = 68;    // ground pixels per lattice step (directions 1 to 4)
constexpr std::int64_t kHillSlope = 34;  // the hills' steepest slope, height per 100 ground pixels (span <= 35)
constexpr std::int64_t kStepLimit = 20;  // the most a height may differ from a lattice neighbour's outside a ridge
constexpr std::int64_t kCliff = 62;      // a crest band corner stands this much above its neighbours outside
constexpr std::int64_t kKeepOut = kZoneR + kMargin;

struct Terrain {
    std::int32_t W = 0, H = 0;
    std::size_t Index(std::int32_t x, std::int32_t y) const { return static_cast<std::size_t>(y) * W + x; }
    bool Inside(std::int32_t x, std::int32_t y) const { return x >= 0 && y >= 0 && x < W && y < H; }
};

// Lattice steps (directions 1 to 4) from the nearest cell with seed[i] != 0 (a breadth-first search).
std::vector<std::int32_t> LatticeDist(const Terrain& t, const std::vector<std::uint8_t>& seed) {
    std::vector<std::int32_t> d(seed.size(), -1);
    std::vector<std::int32_t> todo;
    for (std::size_t i = 0; i < seed.size(); ++i) {
        if (seed[i] != 0) d[i] = 0, todo.push_back(static_cast<std::int32_t>(i));
    }
    for (std::size_t k = 0; k < todo.size(); ++k) {
        const std::int32_t u = todo[k];
        for (int dir = 1; dir <= 4; ++dir) {
            std::int32_t x = u % t.W, y = u / t.W;
            Step(&x, &y, dir);
            if (!t.Inside(x, y) || d[t.Index(x, y)] >= 0) continue;
            d[t.Index(x, y)] = d[static_cast<std::size_t>(u)] + 1;
            todo.push_back(static_cast<std::int32_t>(t.Index(x, y)));
        }
    }
    for (std::int32_t& v : d) v = v < 0 ? 1 << 20 : v;
    return d;
}

// The span of a cell's four corners (the walking rule); -1 in the border band, where no unit walks anyway.
int Span(const std::vector<std::int32_t>& h, std::int32_t W, std::int32_t H, std::int32_t x, std::int32_t y) {
    if (x < 4 || y < 10 || x >= W - 3 || y >= H - 9) return -1;
    const int odd = y & 1;
    const std::size_t i = static_cast<std::size_t>(y) * W + x;
    const std::int32_t c[4] = {h[i], h[static_cast<std::size_t>(y - 1) * W + x + odd], h[i + 1],
                               h[static_cast<std::size_t>(y + 1) * W + x + odd]};
    return *std::max_element(c, c + 4) - *std::min_element(c, c + 4);
}

// Path distances in world pixels over the cells with walk[i] != 0 from `src` (Cell_Step's 8 directions; -1: unreached).
// With `stop` >= 0 it ends once that cell's distance is final. Dijkstra with a ring of buckets (every step is 48 to
// 96 px, so a ring of 128 distances holds every pending one).
void Paths(const Terrain& t, const std::vector<std::uint8_t>& walk, std::int32_t src, std::vector<std::int32_t>* dist,
           std::int32_t stop = -1) {
    dist->assign(walk.size(), -1);
    std::array<std::vector<std::int32_t>, 128> ring;
    ring[0].push_back(src);
    std::size_t pending = 1;
    (*dist)[static_cast<std::size_t>(src)] = 0;
    for (std::int32_t d = 0; pending > 0; ++d) {
        std::vector<std::int32_t>& bucket = ring[static_cast<std::size_t>(d & 127)];
        while (!bucket.empty()) {
            const std::int32_t u = bucket.back();
            bucket.pop_back();
            --pending;
            if (d != (*dist)[static_cast<std::size_t>(u)]) continue;
            if (u == stop) return;
            for (int dir = 1; dir <= 8; ++dir) {
                std::int32_t x = u % t.W, y = u / t.W;
                Step(&x, &y, dir);
                if (!t.Inside(x, y)) continue;
                const std::size_t v = t.Index(x, y);
                if (walk[v] == 0) continue;
                const std::int32_t nd = d + kStepLen[dir];
                if ((*dist)[v] < 0 || nd < (*dist)[v]) {
                    (*dist)[v] = nd;
                    ring[static_cast<std::size_t>(nd & 127)].push_back(static_cast<std::int32_t>(v));
                    ++pending;
                }
            }
        }
    }
}

// The cells reached from `src` over the cells with mask[i] != 0 (Cell_Step's 8 directions).
std::vector<std::uint8_t> Reach(const Terrain& t, const std::vector<std::uint8_t>& mask, std::int32_t src) {
    std::vector<std::uint8_t> seen(mask.size(), 0);
    std::vector<std::int32_t> todo{src};
    seen[static_cast<std::size_t>(src)] = 1;
    for (std::size_t k = 0; k < todo.size(); ++k) {
        for (int dir = 1; dir <= 8; ++dir) {
            std::int32_t x = todo[k] % t.W, y = todo[k] / t.W;
            Step(&x, &y, dir);
            if (!t.Inside(x, y) || mask[t.Index(x, y)] == 0 || seen[t.Index(x, y)] != 0) continue;
            seen[t.Index(x, y)] = 1;
            todo.push_back(static_cast<std::int32_t>(t.Index(x, y)));
        }
    }
    return seen;
}

// The reached cell nearest to the map's middle within 3 cells (-1: none).
std::int32_t NearMiddle(const Terrain& t, const std::vector<std::int32_t>& dist) {
    std::int32_t mx = 0, my = 0;
    CellAt(Pt{std::int64_t{t.W} * 48, std::int64_t{t.H} * 12}, &mx, &my);
    std::int32_t best = -1;
    for (std::int32_t y = my - 6; y <= my + 6; ++y) {
        for (std::int32_t x = mx - 3; x <= mx + 3; ++x) {
            if (!t.Inside(x, y) || dist[t.Index(x, y)] < 0) continue;
            if (best < 0 || dist[t.Index(x, y)] < dist[static_cast<std::size_t>(best)])
                best = static_cast<std::int32_t>(t.Index(x, y));
        }
    }
    return best;
}

// The squared ground distance from p to the segment a-b (all in ground pixels).
std::int64_t SegDist2(Pt p, Pt a, Pt b) {
    const std::int64_t abx = b.x - a.x, aby = b.y - a.y, apx = p.x - a.x, apy = p.y - a.y;
    const std::int64_t den = abx * abx + aby * aby, num = apx * abx + apy * aby;
    if (den == 0 || num <= 0) return apx * apx + apy * apy;
    if (num >= den) return Dist2(p, b);
    return Dist2(p, Pt{a.x + abx * num / den, a.y + aby * num / den});
}

Pt Ground(Pt w) { return Pt{w.x, 2 * w.y}; }

}  // namespace

std::string Hills(MapData& m, const MapGenSettings& g, const std::vector<Pt>& starts,
                  const std::vector<std::uint8_t>& corridor, std::uint64_t key, int ridges) {
    const Terrain t{m.width, m.height};
    const std::int32_t W = t.W, H = t.H;
    const std::size_t n = m.cells.size();
    const std::int64_t wpx = std::int64_t{W} * 96, hpx = std::int64_t{H} * 24;
    const Pt middle{wpx / 2, hpx / 2};
    const int P = static_cast<int>(starts.size());
    Rng r{key};
    const std::uint32_t k_swell = r.next();
    const std::uint32_t k_warp = r.next();
    const std::uint32_t k_ridge = r.next();

    // Each cell's distance to the nearest start (world pixels) and that start (its territory).
    std::vector<std::int64_t> dz(n);
    std::vector<std::int8_t> label(n);
    std::vector<std::uint8_t> zone(n), corr(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Pt c = CellCentre(static_cast<std::int32_t>(i % W), static_cast<std::int32_t>(i / W));
        std::int64_t best = -1;
        for (int p = 0; p < P; ++p) {
            const std::int64_t d2 = Dist2(c, starts[static_cast<std::size_t>(p)]);
            if (best < 0 || d2 < best) best = d2, label[i] = static_cast<std::int8_t>(p);
        }
        dz[i] = ISqrt(best);
        zone[i] = dz[i] <= kKeepOut ? 1 : 0;
        corr[i] = corridor[i] != 0 ? 1 : 0;
    }
    const std::vector<std::int32_t> kz = LatticeDist(t, zone), kc = LatticeDist(t, corr);

    // The plain: 50 with a swell of plus or minus 4 over a few thousand pixels, none within a zone's margin.
    std::vector<std::int32_t> base(n), h(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Pt c = CellCentre(static_cast<std::int32_t>(i % W), static_cast<std::int32_t>(i / W));
        const std::int64_t ramp = std::clamp<std::int64_t>((dz[i] - kKeepOut) * 1024 / 600, 0, 1024);
        const std::int64_t sw = Noise(k_swell, c.x, c.y, 2400);
        base[i] = static_cast<std::int32_t>(kPlain + FloorDiv(sw * 4 * ramp + 512 * 1024, 1024 * 1024));
        h[i] = base[i];
    }

    // ---- Hills: smooth blobs of 50 to 1,000 cells above 65, peaks 110 to 230, slopes kept walkable; spread over the
    // territories in turn (AoE2's balanced elevation, research 1.2) and away from the zones and the corridors. ----
    std::int64_t play = 0;
    for (std::int32_t y = 20; y < H - 20; ++y) play += W - 20;
    const bool large = g.size == 1;
    const int count = static_cast<int>(r.range(large ? 50 : 30, large ? 80 : 45));
    const std::int64_t share = ridges > 0 ? r.range(4, 7) : r.range(5, 10);
    const std::int64_t target65 = play * share / 100;
    std::int64_t above65 = 0;
    std::vector<std::pair<Pt, std::int64_t>> placed;
    std::vector<std::int64_t> region_hi(static_cast<std::size_t>(P), 0), region_area(static_cast<std::size_t>(P), 1);
    for (std::size_t i = 0; i < n; ++i)
        region_area[static_cast<std::size_t>(label[i])] += zone[i] == 0 ? 1 : 0;  // centre (world) and radius (ground)
    auto lobe = [&](Pt c, std::int64_t radius, std::int64_t peak) {
        std::int32_t cx = 0, cy = 0;
        CellAt(c, &cx, &cy);
        const auto rx = static_cast<std::int32_t>(radius * 13 / 10 / 96) + 1;
        const auto ry = static_cast<std::int32_t>(radius * 13 / 10 / 48) + 1;
        for (std::int32_t y = std::max(0, cy - ry); y < std::min(H, cy + ry + 1); ++y) {
            for (std::int32_t x = std::max(0, cx - rx); x < std::min(W, cx + rx + 1); ++x) {
                const Pt q = CellCentre(x, y);
                const std::int64_t dx = q.x - c.x, dy = 2 * (q.y - c.y);
                const std::int64_t d =
                    ISqrt(dx * dx + dy * dy) * (1024 + Noise(k_warp, q.x, q.y, 700) * 22 / 100) / 1024;
                const std::int64_t v = kPlain + (peak - kPlain) * Bump(d * 1024 / radius) / 1024;
                const std::size_t i = t.Index(x, y);
                if (v <= h[i]) continue;
                if (h[i] <= 65 && v > 65 && y >= 20 && y < H - 20 && x >= 10 && x < W - 10) {
                    ++above65;
                    ++region_hi[static_cast<std::size_t>(label[i])];
                }
                h[i] = static_cast<std::int32_t>(v);
            }
        }
    };
    // The radius (ground) whose bump of `peak` covers `cells` above 65, and the peak lowered to the hills' slope.
    auto shape = [](std::int64_t cells, std::int64_t* peak) {
        const std::int64_t s1 = ISqrt(15 * 1048576 / (*peak - kPlain));  // the bump's share at 65, square-rooted
        const std::int64_t u65 = ISqrt((1024 - s1) * 1024);
        const std::int64_t radius = ISqrt(cells * 4608 * 1000 / 3142) * 1024 / std::max<std::int64_t>(u65, 1);
        *peak = std::min(*peak, kPlain + radius * kHillSlope / 154);
        return radius;
    };
    // Up to `count` hills until the share is reached; on a crowded map (little room between the zones) up to 40 more
    // while the hill land is under 3.5 %.
    for (int k = 0; above65 < target65 && (k < count || (k < count + 40 && above65 * 1000 < play * 35)); ++k) {
        // Two hills of three go to the territory with the least hill land for its size, the third anywhere.
        int region = P;
        if (k % 3 != 2) {
            for (int p = 0; p < P; ++p) {
                const auto pu = static_cast<std::size_t>(p);
                if (region == P || region_hi[pu] * region_area[static_cast<std::size_t>(region)] <
                                       region_hi[static_cast<std::size_t>(region)] * region_area[pu])
                    region = p;
            }
        }
        const std::int64_t u = r.below(1024);
        const std::int64_t cells = 30 + 320 * u * u * u / (1024 * 1024 * 1024);
        std::int64_t peak = r.range(110, 230);
        const std::int64_t radius = shape(cells, &peak);
        if (peak < 75) continue;
        bool found = false;
        Pt c;
        for (int tr = 0; tr < 75 && !found; ++tr) {
            const std::int64_t x = r.range(10 * 96, wpx - 10 * 96);
            const std::int64_t y = r.range(20 * 24, hpx - 20 * 24);
            c = Pt{x, y};
            std::int32_t cx = 0, cy = 0;
            CellAt(c, &cx, &cy);
            if (!t.Inside(cx, cy)) continue;
            const std::size_t i = t.Index(cx, cy);
            if (region < P && label[i] != region) continue;
            if (dz[i] < kKeepOut + radius / 3 || kc[i] * kLattice < radius / 3 + 2 * kLattice) continue;
            found = true;
            for (const auto& [o, orad] : placed)
                found = found && ISqrt(Dist2(Ground(o), Ground(c))) * 3 >= radius + orad;
        }
        if (!found) continue;
        placed.emplace_back(c, radius);
        lobe(c, radius, peak);
        // One to three smaller lobes beside the main one, so a hill is not a round dome.
        const int extra = static_cast<int>(r.range(1, 3));
        for (int e = 0; e < extra; ++e) {
            std::int64_t ox = 0, oy = 0, len = 0;
            while (len == 0) {
                ox = r.range(-1024, 1024);
                oy = r.range(-1024, 1024);
                len = ISqrt(ox * ox + oy * oy);
            }
            const std::int64_t off = radius * r.range(35, 75) / 100;
            std::int64_t lp = kPlain + (peak - kPlain) * r.range(50, 90) / 100;
            const std::int64_t lr = radius * r.range(45, 75) / 100;
            lp = std::min(lp, kPlain + lr * kHillSlope / 154);
            if (lp > 60) lobe(Pt{c.x + ox * off / len, c.y + oy * off / len / 2}, lr, lp);
        }
    }

    // ---- Mountains, and the hill ridges of a land map's Ridges kind: ridges on the borders between neighbouring
    // territories (KaM Remake's Voronoi obstacles, research 5), 35 to 60 % of a border's length, a crest band of
    // cliffs, the passes where the corridors cross. ----
    std::vector<std::uint8_t> crest(n, 0);
    std::vector<std::pair<int, int>> ridged;  // the borders that carry a ridge
    const bool mountains = ridges == 2;
    if (ridges > 0) {
        // Each border's extent along its bisector (from the cells where two territories meet, inside the play area).
        struct Border {
            int a = 0, b = 0;
            std::int64_t lo = 0, hi = 0;
            bool seen = false;
        };
        std::vector<Border> borders;
        for (int a = 0; a < P; ++a) {
            for (int b = a + 1; b < P; ++b) borders.push_back(Border{a, b, 0, 0, false});
        }
        auto border_of = [&](int a, int b) -> Border& {
            if (a > b) std::swap(a, b);
            return borders[static_cast<std::size_t>(a * P - a * (a + 1) / 2 + (b - a - 1))];
        };
        auto frame = [&](const Pt& s, const Pt& o, Pt* mid, Pt* along, std::int64_t* len) {
            *mid = Pt{(s.x + o.x) / 2, (s.y + o.y) / 2};
            *along = Pt{-(o.y - s.y), o.x - s.x};
            *len = ISqrt(Dist2(s, o));
        };
        for (std::int32_t y = 15; y < H - 15; ++y) {
            for (std::int32_t x = 6; x < W - 6; ++x) {
                const int a = label[t.Index(x, y)];
                for (int dir : {2, 7}) {
                    std::int32_t nx = x, ny = y;
                    Step(&nx, &ny, dir);
                    const int b = label[t.Index(nx, ny)];
                    if (a == b) continue;
                    Border& bd = border_of(a, b);
                    Pt mid, along;
                    std::int64_t len = 0;
                    frame(starts[static_cast<std::size_t>(bd.a)], starts[static_cast<std::size_t>(bd.b)], &mid, &along,
                          &len);
                    const Pt c = CellCentre(x, y);
                    const std::int64_t s = ((c.x - mid.x) * along.x + (c.y - mid.y) * along.y) / len;
                    if (!bd.seen || s < bd.lo) bd.lo = s;
                    if (!bd.seen || s > bd.hi) bd.hi = s;
                    bd.seen = true;
                }
            }
        }
        std::vector<int> order;
        for (std::size_t k = 0; k < borders.size(); ++k) {
            if (borders[k].seen && borders[k].hi - borders[k].lo >= 2000) order.push_back(static_cast<int>(k));
        }
        for (int k = static_cast<int>(order.size()) - 1; k > 0; --k)
            std::swap(order[static_cast<std::size_t>(k)], order[r.below(static_cast<std::uint32_t>(k + 1))]);
        const int want = static_cast<int>(mountains ? (large ? r.range(3, 6) : r.range(2, 4))
                                                    : (large ? r.range(3, 5) : r.range(2, 3)));
        int pieces = 0;
        for (std::size_t ob = 0; ob < order.size() && pieces < want; ++ob) {
            const Border& bd = borders[static_cast<std::size_t>(order[ob])];
            const Pt sa = starts[static_cast<std::size_t>(bd.a)], sb = starts[static_cast<std::size_t>(bd.b)];
            Pt mid, along;
            std::int64_t len = 0;
            frame(sa, sb, &mid, &along, &len);
            const std::int64_t L = bd.hi - bd.lo, run = L * r.range(35, 60) / 100;
            // The crest's height, its top above it, its half width; the foot's height (hill ridges: lower, narrower,
            // the foot nearer the crest).
            const std::int64_t hs = mountains ? r.range(140, 175) : r.range(112, 135);
            const std::int64_t top_add = mountains ? r.range(30, 70) : r.range(15, 35);
            const std::int64_t ws = mountains ? r.range(140, 210) : r.range(110, 160);
            const std::int64_t amp = r.range(150, 350), hf = hs - (mountains ? 66 : 50);
            const std::int64_t wf = (hf - kPlain) * 154 / kHillSlope + 60;
            const std::uint32_t k_line = k_ridge + static_cast<std::uint32_t>(order[ob]) * 0x9e3779b9u;
            // The crest samples every 96 pixels along the bisector, meandering across it; a sample counts when it
            // keeps its whole profile out of every zone's margin, 1,000 pixels from the middle and inside the map.
            std::vector<Pt> pts;
            std::vector<std::int64_t> top;
            std::vector<std::uint8_t> ok;
            for (std::int64_t s = bd.lo; s <= bd.hi; s += 96) {
                const std::int64_t off = Noise(k_line, s, 0, 1600) * amp / 1024;
                const Pt p{mid.x + along.x * s / len + (sb.x - sa.x) * off / len,
                           mid.y + along.y * s / len + (sb.y - sa.y) * off / len};
                std::int64_t near = -1;
                for (const Pt& st : starts) {
                    const std::int64_t d2 = Dist2(p, st);
                    near = near < 0 || d2 < near ? d2 : near;
                }
                pts.push_back(Ground(p));
                top.push_back(
                    std::min<std::int64_t>(250, hs + top_add + Noise(k_line ^ 0x5bd1e995u, s, 0, 900) * 15 / 1024));
                ok.push_back(p.x >= 4 * 96 && p.x <= wpx - 4 * 96 && p.y >= 10 * 24 && p.y <= hpx - 10 * 24 &&
                                     ISqrt(near) >= kKeepOut + ws + 200 && Dist2(p, middle) >= 1000 * 1000
                                 ? 1
                                 : 0);
            }
            // The stretches of valid samples 1,200 pixels long or more; the ridge takes its share of the border's
            // length inside one of them (all of it when the stretch is shorter).
            std::vector<std::pair<std::size_t, std::size_t>> stretches;
            for (std::size_t a = 0; a < ok.size();) {
                std::size_t b = a;
                while (b < ok.size() && ok[b] == ok[a]) ++b;
                if (ok[a] != 0 && (b - a) * 96 >= 1200) stretches.emplace_back(a, b);
                a = b;
            }
            if (stretches.empty()) continue;
            // A front: when a corridor crosses the border near the line between the two starts (the pass), the
            // stretch nearest that line and the ridge centred on it; else the stretch farthest from it, the ridge at
            // its far end, so the way between the two starts stays open.
            // The pass: the corridor's sample on the border nearest that line (within 1,500 px of it).
            bool pass = false;
            std::int64_t pass_s = 0;
            for (std::int64_t k = 0; k <= 1500 / 96 && !pass; ++k) {
                for (const std::int64_t sv : {k * 96, -k * 96}) {
                    std::int32_t cx = 0, cy = 0;
                    CellAt(Pt{mid.x + along.x * sv / len, mid.y + along.y * sv / len}, &cx, &cy);
                    if (!pass && t.Inside(cx, cy) && corr[t.Index(cx, cy)] != 0) pass = true, pass_s = sv;
                }
            }
            const auto zero = static_cast<std::size_t>(
                std::clamp<std::int64_t>((0 - bd.lo) / 96, 0, static_cast<std::int64_t>(ok.size())));
            // The ridge keeps 4 samples (384 px) off the pass on each side, so the pass stays open even where the
            // corridor runs along the border rather than across it.
            const auto pz = static_cast<std::size_t>(
                std::clamp<std::int64_t>((pass_s - bd.lo) / 96, 0, static_cast<std::int64_t>(ok.size())));
            const std::size_t pz_lo = pz >= 4 ? pz - 4 : 0, pz_hi = std::min(ok.size(), pz + 5);
            auto gap = [&](std::size_t q) {
                const auto [a, b] = stretches[q];
                return zero < a ? a - zero : zero >= b ? zero - b + 1 : 0;
            };
            // The parts of the crest: [first, end) sample ranges.
            std::vector<std::pair<std::size_t, std::size_t>> parts;
            if (pass) {  // half the share on each side of the pass, from the pass outward
                const std::size_t half = static_cast<std::size_t>(run / 192) + 1;
                std::size_t lo_a = 0, lo_e = 0, hi_a = 0, hi_e = 0;
                for (const auto& [a, b] : stretches) {
                    if (a < pz_lo && std::min(b, pz_lo) > lo_e) lo_a = a, lo_e = std::min(b, pz_lo);
                    if (b > pz_hi && (hi_e == 0 || std::max(a, pz_hi) < hi_a)) hi_a = std::max(a, pz_hi), hi_e = b;
                }
                if (lo_e > lo_a) parts.emplace_back(lo_e - std::min(half, lo_e - lo_a), lo_e);
                if (hi_e > hi_a) parts.emplace_back(hi_a, hi_a + std::min(half, hi_e - hi_a));
            } else {  // the stretch farthest from the line between the starts, at its far end
                std::size_t pick = 0;
                for (std::size_t k = 1; k < stretches.size(); ++k) {
                    if (gap(k) > gap(pick)) pick = k;
                }
                auto [a, b] = stretches[pick];
                // A stretch across that line: its longer side only, 4 samples off the line, so the line stays open.
                if (zero >= a && zero < b) {
                    if (zero - a >= b - zero) {
                        b = zero >= a + 4 ? zero - 4 : a;
                    } else {
                        a = std::min(b, zero + 5);
                    }
                }
                const std::size_t take = std::min<std::size_t>(b - a, static_cast<std::size_t>(run / 96) + 1);
                parts.emplace_back(zero < a ? b - take : a, zero < a ? b : a + take);
            }
            std::vector<std::vector<Pt>> runs;
            std::vector<std::vector<std::int64_t>> tops;
            for (const auto& [first, end] : parts) {
                runs.emplace_back(pts.begin() + static_cast<std::ptrdiff_t>(first),
                                  pts.begin() + static_cast<std::ptrdiff_t>(end));
                tops.emplace_back(top.begin() + static_cast<std::ptrdiff_t>(first),
                                  top.begin() + static_cast<std::ptrdiff_t>(end));
            }
            bool any = false;
            for (std::size_t q = 0; q < runs.size(); ++q) {
                const std::vector<Pt>& line = runs[q];
                if (line.size() < 8) continue;
                any = true;
                ++pieces;
                // Foothills along both flanks, so the ridge reads as a range and not a wall (walkable; the cliff rule
                // below keeps them under the crest band).
                for (std::size_t k = 0; k < line.size(); k += 4) {
                    std::int64_t ox = 0, oy = 0, olen = 0;
                    while (olen == 0) {
                        ox = r.range(-1024, 1024);
                        oy = r.range(-1024, 1024);
                        olen = ISqrt(ox * ox + oy * oy);
                    }
                    const std::int64_t off = ws + r.range(0, wf);
                    const std::int64_t frad = r.range(400, 900);
                    const std::int64_t fpeak = std::min(hs - (mountains ? 70 : 40), kPlain + frad * kHillSlope / 154);
                    const Pt c{line[k].x + ox * off / olen, (line[k].y + oy * off / olen) / 2};
                    if (fpeak > 65) lobe(c, frad, fpeak);
                }
                std::int64_t x0 = line[0].x, x1 = x0, y0 = line[0].y, y1 = y0;
                for (const Pt& p : line)
                    x0 = std::min(x0, p.x), x1 = std::max(x1, p.x), y0 = std::min(y0, p.y), y1 = std::max(y1, p.y);
                const std::int64_t reach = ws + wf;
                const auto cx0 = static_cast<std::int32_t>(std::max<std::int64_t>(0, (x0 - reach) / 96 - 1));
                const auto cx1 = static_cast<std::int32_t>(std::min<std::int64_t>(W - 1, (x1 + reach) / 96 + 1));
                const auto cy0 = static_cast<std::int32_t>(std::max<std::int64_t>(0, (y0 - reach) / 48 - 1));
                const auto cy1 = static_cast<std::int32_t>(std::min<std::int64_t>(H - 1, (y1 + reach) / 48 + 1));
                for (std::int32_t y = cy0; y <= cy1; ++y) {
                    for (std::int32_t x = cx0; x <= cx1; ++x) {
                        const Pt gp = Ground(CellCentre(x, y));
                        std::int64_t d2 = -1, peak = tops[q][0];
                        for (std::size_t k = 0; k + 1 < line.size(); ++k) {
                            const std::int64_t dk = SegDist2(gp, line[k], line[k + 1]);
                            if (d2 < 0 || dk < d2) d2 = dk, peak = tops[q][k];
                        }
                        if (d2 > reach * reach) continue;
                        const std::int64_t d = ISqrt(d2);
                        const std::size_t i = t.Index(x, y);
                        if (d <= ws && kc[i] >= 3 && kPlain + kStepLimit * kz[i] >= peak) {
                            const std::int64_t v = hs + (peak - hs) * (1024 - d * d * 1024 / (ws * ws)) / 1024;
                            h[i] = static_cast<std::int32_t>(std::max<std::int64_t>(h[i], v));
                            crest[i] = 1;
                        } else {
                            const std::int64_t v =
                                kPlain + (hf - kPlain) * Bump(std::max<std::int64_t>(0, d - ws) * 1024 / wf) / 1024;
                            if (crest[i] == 0) h[i] = static_cast<std::int32_t>(std::max<std::int64_t>(h[i], v));
                        }
                    }
                }
            }
            if (any) ridged.emplace_back(bd.a, bd.b);
        }
    }

    // ---- The limits: the zones and their margins at 50 and the hills rising from them at most 20 per lattice step;
    // the corridors on the plain (8 and 16 above it one and two steps off); the cliffs at the crest bands; then every
    // other step within 20 (spans within 40), heights 1 to 250. ----
    for (std::size_t i = 0; i < n; ++i) {
        if (crest[i] != 0) continue;
        std::int64_t cap = kPlain + kStepLimit * kz[i];
        if (kc[i] <= 2) cap = std::min<std::int64_t>(cap, base[i] + 8 * kc[i]);
        h[i] = static_cast<std::int32_t>(std::min<std::int64_t>(h[i], cap));
    }
    if (ridges > 0) {
        for (std::size_t i = 0; i < n; ++i) {
            if (crest[i] != 0) continue;
            for (int dir = 1; dir <= 8; ++dir) {
                std::int32_t x = static_cast<std::int32_t>(i % W), y = static_cast<std::int32_t>(i / W);
                Step(&x, &y, dir);
                if (t.Inside(x, y) && crest[t.Index(x, y)] != 0)
                    h[i] = std::min<std::int32_t>(h[i], h[t.Index(x, y)] - static_cast<std::int32_t>(kCliff));
            }
        }
    }
    for (int round = 0; round < 16; ++round) {
        bool changed = false;
        auto relax = [&](std::int32_t x, std::int32_t y, int dir) {
            const std::size_t i = t.Index(x, y);
            Step(&x, &y, dir);
            if (!t.Inside(x, y)) return;
            const std::size_t j = t.Index(x, y);
            if (crest[i] != 0 || crest[j] != 0 || h[i] <= h[j] + kStepLimit) return;
            h[i] = h[j] + static_cast<std::int32_t>(kStepLimit);
            changed = true;
        };
        for (std::int32_t y = 0; y < H; ++y) {
            for (std::int32_t x = 0; x < W; ++x) relax(x, y, 1), relax(x, y, 4);
        }
        for (std::int32_t y = H - 1; y >= 0; --y) {
            for (std::int32_t x = W - 1; x >= 0; --x) relax(x, y, 2), relax(x, y, 3);
        }
        if (!changed) break;
    }
    for (std::size_t i = 0; i < n; ++i) {
        h[i] = std::clamp<std::int32_t>(h[i], 1, 250);
        m.cells[i].height = static_cast<std::uint8_t>(h[i]);
    }

    // ---- The step's own checks. Tanks (span 25) reach every start and the middle from start 1. ----
    std::vector<std::uint8_t> walk(n, 0), tank(n, 0);
    for (std::int32_t y = 0; y < H; ++y) {
        for (std::int32_t x = 0; x < W; ++x) {
            const int s = Span(h, W, H, x, y);
            walk[t.Index(x, y)] = s >= 0 && s <= 60 ? 1 : 0;
            tank[t.Index(x, y)] = s >= 0 && s <= 25 ? 1 : 0;
        }
    }
    std::vector<std::int32_t> cell(static_cast<std::size_t>(P));
    for (int p = 0; p < P; ++p) {
        std::int32_t x = 0, y = 0;
        CellAt(starts[static_cast<std::size_t>(p)], &x, &y);
        cell[static_cast<std::size_t>(p)] = static_cast<std::int32_t>(t.Index(x, y));
    }
    const std::vector<std::uint8_t> tanks = Reach(t, tank, cell[0]);
    std::vector<std::int32_t> dist(n, -1);
    for (std::size_t i = 0; i < n; ++i) dist[i] = tanks[i] != 0 ? 0 : -1;
    for (int p = 0; p < P; ++p) {
        if (tanks[static_cast<std::size_t>(cell[static_cast<std::size_t>(p)])] == 0)
            return "hills: tanks cannot reach start " + std::to_string(p + 1);
    }
    if (NearMiddle(t, dist) < 0) return "hills: tanks cannot reach the middle";
    if (ridges == 0) return "";

    // Ridges: every start's walk to the middle at most 1.4 times the line (plus 300), and every ridged border's
    // pass is taken: the walk between its two starts at most 1.35 times the line (the plan's ridge check,
    // research 1.6).
    char buf[96];
    for (std::size_t i = 0; i < n; ++i) dist[i] = walk[i] != 0 ? 0 : -1;
    const std::int32_t mid = NearMiddle(t, dist);
    if (mid < 0) return "mountains: no walking cell at the middle";
    Paths(t, walk, mid, &dist);
    for (int p = 0; p < P; ++p) {
        const std::int32_t d = dist[static_cast<std::size_t>(cell[static_cast<std::size_t>(p)])];
        const std::int64_t line = ISqrt(Dist2(starts[static_cast<std::size_t>(p)], middle));
        if (d < 0) return "mountains: start " + std::to_string(p + 1) + " cannot reach the middle";
        if (d * 100 > line * 140 + 300 * 100) {
            std::snprintf(buf, sizeof buf, "mountains: the walk to the middle %.2f (start %d)",
                          static_cast<double>(d) / static_cast<double>(line), p + 1);
            return buf;
        }
    }
    for (const auto& [a, b] : ridged) {
        const std::int32_t to = cell[static_cast<std::size_t>(b)];
        Paths(t, walk, cell[static_cast<std::size_t>(a)], &dist, to);
        const std::int32_t d = dist[static_cast<std::size_t>(to)];
        const std::int64_t line =
            ISqrt(Dist2(starts[static_cast<std::size_t>(a)], starts[static_cast<std::size_t>(b)]));
        if (d < 0 || d * 100 > line * 135) {
            std::snprintf(buf, sizeof buf, "mountains: the pass between %d and %d %.2f", a + 1, b + 1,
                          d < 0 ? 0.0 : static_cast<double>(d) / static_cast<double>(line));
            return buf;
        }
    }
    return "";
}

}  // namespace mapgen

using namespace mapgen;

MapGenHillStats MapGen_HillStats(const MapData& m, const MapGenLayout& layout) {
    MapGenHillStats s;
    const Terrain t{m.width, m.height};
    const std::int32_t W = t.W, H = t.H;
    const std::size_t n = m.cells.size(), P = layout.zones.size();
    std::vector<std::int32_t> h(n);
    for (std::size_t i = 0; i < n; ++i) h[i] = m.cells[i].height, s.peak = std::max(s.peak, h[i]);
    std::vector<std::uint8_t> walk(n, 0), tank(n, 0), hi(n, 0), steep(n, 0);
    for (std::int32_t y = 0; y < H; ++y) {
        for (std::int32_t x = 0; x < W; ++x) {
            const std::size_t i = t.Index(x, y);
            const int sp = Span(h, W, H, x, y);
            const bool free = m.cells[i].object == 0;
            walk[i] = sp >= 0 && sp <= 60 && free ? 1 : 0;
            tank[i] = sp >= 0 && sp <= 25 && free ? 1 : 0;
            if (x < 10 || x >= W - 10 || y < 20 || y >= H - 20) continue;
            ++s.play_cells;
            hi[i] = h[i] > 65 ? 1 : 0, steep[i] = sp > 60 ? 1 : 0;
            s.above65 += hi[i], s.steep += steep[i], s.large_blocked += sp > 25 ? 1 : 0;
        }
    }
    auto groups = [&](const std::vector<std::uint8_t>& mask) {
        std::vector<std::uint8_t> seen(n, 0);
        int big = 0;
        for (std::size_t i0 = 0; i0 < n; ++i0) {
            if (mask[i0] == 0 || seen[i0] != 0) continue;
            std::vector<std::int32_t> todo{static_cast<std::int32_t>(i0)};
            seen[i0] = 1;
            for (std::size_t k = 0; k < todo.size(); ++k) {
                for (int dir = 1; dir <= 8; ++dir) {
                    std::int32_t x = todo[k] % W, y = todo[k] / W;
                    Step(&x, &y, dir);
                    if (!t.Inside(x, y) || mask[t.Index(x, y)] == 0 || seen[t.Index(x, y)] != 0) continue;
                    seen[t.Index(x, y)] = 1;
                    todo.push_back(static_cast<std::int32_t>(t.Index(x, y)));
                }
            }
            big += todo.size() >= 30 ? 1 : 0;
        }
        return big;
    };
    s.hills = groups(hi), s.walls = groups(steep);
    s.region_cells.assign(P, 0), s.region_above65.assign(P, 0);
    for (std::size_t i = 0; i < n; ++i) {
        const Pt c = CellCentre(static_cast<std::int32_t>(i % W), static_cast<std::int32_t>(i / W));
        std::int64_t best = -1;
        std::size_t lab = 0;
        for (std::size_t p = 0; p < P; ++p) {
            const std::int64_t d2 = Dist2(c, Pt{layout.zones[p].x, layout.zones[p].y});
            if (best < 0 || d2 < best) best = d2, lab = p;
        }
        if (P == 0 || best <= kKeepOut * kKeepOut) continue;
        ++s.region_cells[lab];
        s.region_above65[lab] += h[i] > 65 ? 1 : 0;
    }
    const Pt middle{std::int64_t{W} * 48, std::int64_t{H} * 12};
    std::vector<std::int32_t> dist;
    for (std::size_t p = 0; p < P; ++p) {
        const Pt sp{layout.zones[p].x, layout.zones[p].y};
        std::int32_t x = 0, y = 0;
        CellAt(sp, &x, &y);
        if (p == 0) {
            Paths(t, tank, static_cast<std::int32_t>(t.Index(x, y)), &dist);
            s.large_reach = NearMiddle(t, dist) >= 0;
            for (const MapGenZone& z : layout.zones) {
                std::int32_t zx = 0, zy = 0;
                CellAt(Pt{z.x, z.y}, &zx, &zy);
                s.large_reach = s.large_reach && dist[t.Index(zx, zy)] >= 0;
            }
        }
        Paths(t, walk, static_cast<std::int32_t>(t.Index(x, y)), &dist);
        const std::int32_t mid = NearMiddle(t, dist);
        s.to_middle.push_back(mid < 0 ? -1 : dist[static_cast<std::size_t>(mid)]);
        s.to_middle_straight.push_back(static_cast<std::int32_t>(ISqrt(Dist2(sp, middle))));
        std::int32_t best = -1, line = 0;
        for (std::size_t q = 0; q < P; ++q) {
            if (q == p) continue;
            std::int32_t qx = 0, qy = 0;
            CellAt(Pt{layout.zones[q].x, layout.zones[q].y}, &qx, &qy);
            const std::int32_t d = dist[t.Index(qx, qy)];
            if (d >= 0 && (best < 0 || d < best))
                best = d, line = static_cast<std::int32_t>(ISqrt(Dist2(sp, Pt{layout.zones[q].x, layout.zones[q].y})));
        }
        s.to_next.push_back(best), s.to_next_straight.push_back(line);
    }
    return s;
}

std::string MapGen_HillStatsText(const MapGenHillStats& s) {
    const double play = s.play_cells > 0 ? s.play_cells : 1;
    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "terrain: above 65 %.1f %%, steep (span > 60) %.2f %%, span > 25 %.1f %%, hills %d, walls %d, peak "
                  "%d, tanks reach all %s\n",
                  100.0 * s.above65 / play, 100.0 * s.steep / play, 100.0 * s.large_blocked / play, s.hills, s.walls,
                  s.peak, s.large_reach ? "yes" : "NO");
    std::string out = buf;
    out += "player  territory hills%  to middle (walk / line)   to nearest start (walk / line)\n";
    for (std::size_t p = 0; p < s.to_middle.size(); ++p) {
        std::snprintf(buf, sizeof buf, "%-7zu %-17.1f %5d / %5d  %.2f        %5d / %5d  %.2f\n", p + 1,
                      s.region_cells[p] ? 100.0 * s.region_above65[p] / s.region_cells[p] : 0.0, s.to_middle[p],
                      s.to_middle_straight[p],
                      s.to_middle_straight[p] ? static_cast<double>(s.to_middle[p]) / s.to_middle_straight[p] : 0.0,
                      s.to_next[p], s.to_next_straight[p],
                      s.to_next_straight[p] ? static_cast<double>(s.to_next[p]) / s.to_next_straight[p] : 0.0);
        out += buf;
    }
    return out;
}

}  // namespace antanta::sim
