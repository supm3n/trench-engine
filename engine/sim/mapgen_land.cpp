// The random map generator's plain land maps (docs/map-generator-plan.md 5.1; decision 56). See mapgen_land.hpp.
//
// The code draws a kind of country first: Plains (open parkland, nothing drawn), Woodlands (one to three forest belts
// that bend across the map and up to three great woods, drawn as bodies and filled by the forests step, the corridors
// cut through them), Ridges (nothing drawn: the hills step raises hill ridges with passes along the borders between
// the territories, as the Mountains switch does, lower) and Ponds (a few small lakes drawn as bodies anywhere between
// the starts, well under the land map's water limit, so the computers still play it as a land map). Then the starts
// are fitted as on a Sea map: candidates off the features inside the frame, spread by a randomised farthest-point pick
// and a climb on their spacing, and a set is kept only when the judge finds it fair by land path (mapgen_sea.hpp
// Judge: the nearest enemy within 0.85, neighbours by land with contested places, own places for everyone, land shares
// that LandTerritories can even out). No ring, no mirrored kit: every code gets its own layout.
#include "mapgen_land.hpp"

#include <algorithm>
#include <cstdio>

namespace antanta::sim::mapgen {

namespace {

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

std::int64_t FloorDiv(std::int64_t a, std::int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

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

// The drawn features: bodies (a line of points with a radius at each, mapgen_sea.hpp) seen through a slow domain warp
// and one octave of noise, so no belt is a straight band and no pond a disc.
struct Draft {
    std::vector<Body> bodies;
    std::uint32_t wk = 0, k1 = 0;
    std::int64_t wa = 0, ws = 1, a1 = 0, s1 = 1;
};

std::int64_t DraftAt(const Draft& d, std::int64_t x, std::int64_t y) {
    const std::int64_t qx = x + Noise(d.wk, x, y, d.ws) * d.wa / 1024;
    const std::int64_t qy = y + Noise(d.wk ^ 0x5bd1e995u, x, y, d.ws) * d.wa / 1024;
    std::int64_t v = -kBodyDeep;
    for (const Body& b : d.bodies) v = std::max(v, BodyValue(b, qx, qy));
    return v + Noise(d.k1, x, y, d.s1) * d.a1 / 1024;
}

Pt AnyDir(Rng& r) {
    const std::int64_t x = r.range(-1024, 1024);
    const std::int64_t y = r.range(-1024, 1024);
    return Unit(x == 0 && y == 0 ? 1 : x, y);
}

// Woodlands: one or two forest belts (Large two or three), each a bending line through a point of the inner map along
// a drawn direction, 7,000 to 14,000 px long (Large 1.5 times), 560 to 1,000 px wide and varying along it; and up to
// two great woods (Large one to three), round with a lobe or two.
void DrawWoods(Rng& r, bool large, std::int64_t wpx, std::int64_t hpx, Draft* d, std::string* words) {
    auto S = [&](std::int64_t v) { return large ? v * 3 / 2 : v; };
    d->wk = r.next(), d->k1 = r.next();
    d->ws = S(r.range(3000, 5000));
    d->wa = d->ws * r.range(10, 18) / 100;
    d->s1 = r.range(500, 900);
    d->a1 = r.range(60, 160);
    const int belts = static_cast<int>(r.range(1, 2)) + (large ? 1 : 0);
    for (int q = 0; q < belts; ++q) {
        const std::int64_t ax = r.range(wpx * 25 / 100, wpx * 75 / 100);
        const std::int64_t ay = r.range(hpx * 25 / 100, hpx * 75 / 100);
        const Pt c{ax, ay}, u = AnyDir(r);
        const std::int64_t half = S(r.range(3500, 7000));
        const std::int64_t w = S(r.range(280, 500));
        Body b;
        for (int j = 0; j < 4; ++j) {
            const std::int64_t t = -half + 2 * half * j / 3;
            const std::int64_t off = j == 0 || j == 3 ? 0 : r.range(-half / 4, half / 4);
            b.line.push_back(Toward(Toward(c, u, t), Perp(u), off));
            b.rad.push_back(w * r.range(70, 130) / 100);
        }
        d->bodies.push_back(b);
    }
    const int woods = static_cast<int>(large ? r.range(1, 3) : r.range(0, 2));
    for (int q = 0; q < woods; ++q) {
        const std::int64_t cx = r.range(wpx * 15 / 100, wpx * 85 / 100);
        const std::int64_t cy = r.range(hpx * 15 / 100, hpx * 85 / 100);
        const Pt c{cx, cy};
        const std::int64_t rad = S(r.range(900, 1500));
        Body b;
        b.line = {c};
        b.rad = {rad};
        d->bodies.push_back(b);
        const int lobes = static_cast<int>(r.range(1, 2));
        for (int l = 0; l < lobes; ++l) {
            const Pt v = AnyDir(r);
            const std::int64_t off = rad * r.range(50, 80) / 100;
            const std::int64_t lrad = rad * r.range(50, 80) / 100;
            Body lobe;
            lobe.line = {Toward(c, v, off)};
            lobe.rad = {lrad};
            d->bodies.push_back(lobe);
        }
    }
    char buf[96];
    std::snprintf(buf, sizeof buf, "Woodlands, %d forest belts, %d great woods", belts, woods);
    *words = buf;
}

// Ponds: a few small lakes (Normal 1 to 4 by the players, Large one or two more), each 380 to 750 px in radius (Large
// to 950), a short line of two radii with a lobe or two, 1,500 px off the middle (the middle stays open land) and
// apart from each other, inside the keep-out band. Well under the land map's water limit (13 % of the blocks).
void DrawPonds(Rng& r, bool large, int n, std::int64_t wpx, std::int64_t hpx, Draft* d, std::string* words) {
    auto S = [&](std::int64_t v) { return large ? v * 3 / 2 : v; };
    d->wk = r.next(), d->k1 = r.next();
    d->ws = S(r.range(2500, 4000));
    d->wa = d->ws * r.range(6, 12) / 100;
    d->s1 = r.range(450, 800);
    d->a1 = r.range(60, 140);
    int count = static_cast<int>(n <= 4 ? r.range(2, 4) : n <= 6 ? r.range(2, 3) : r.range(1, 2));
    if (large) count += static_cast<int>(r.range(1, 2));
    const Pt middle{wpx / 2, hpx / 2};
    std::vector<std::pair<Pt, std::int64_t>> placed;
    for (int q = 0; q < count; ++q) {
        const std::int64_t rad = large ? r.range(420, 950) : r.range(380, 750);
        for (int t = 0; t < 20; ++t) {
            const std::int64_t cx = r.range(kBandX + rad + 400, wpx - kBandX - rad - 400);
            const std::int64_t cy = r.range(kBandY + rad + 300, hpx - kBandY - rad - 300);
            const Pt c{cx, cy};
            bool ok = Dist2(c, middle) >= (rad + 1500) * (rad + 1500);
            for (const auto& [o, orad] : placed) ok = ok && Dist2(c, o) >= (rad + orad + 1200) * (rad + orad + 1200);
            if (!ok) continue;
            const Pt v = AnyDir(r);
            const std::int64_t len = rad * r.range(30, 90) / 100;
            const std::int64_t rad2 = rad * r.range(55, 100) / 100;
            Body b;
            b.line = {c, Toward(c, v, len)};
            b.rad = {rad, rad2};
            d->bodies.push_back(b);
            const int lobes = static_cast<int>(r.range(0, 2));
            for (int l = 0; l < lobes; ++l) {
                const Pt lv = AnyDir(r);
                const std::int64_t off = rad * r.range(50, 75) / 100;
                const std::int64_t lrad = rad * r.range(45, 70) / 100;
                Body lobe;
                lobe.line = {Toward(c, lv, off)};
                lobe.rad = {lrad};
                d->bodies.push_back(lobe);
            }
            placed.emplace_back(c, rad);
            break;
        }
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "Ponds, %d ponds", static_cast<int>(placed.size()));
    *words = buf;
}

struct Fit {
    std::vector<Pt> starts;   // in cycle order
    std::vector<Pair> pairs;  // the neighbours by land, indices into `starts`
    std::int64_t score = -1;
};

}  // namespace

bool LandPlan(const MapGenSettings& g, std::uint64_t key, LandDesign* design, std::vector<Pt>* starts,
              std::vector<Pair>* neighbours, std::string* why) {
    Rng r{key};
    const bool large = g.size == 1;
    const int n = g.players;
    const std::int64_t wpx = large ? 300 * 96 : 200 * 96, hpx = large ? 600 * 24 : 400 * 24;
    const std::int64_t min_d = !large && n >= 7 ? 2800 : 2 * kZoneR + 400;
    const std::int64_t x_lo = kBandX + kZoneR + 48, x_hi = wpx - x_lo, y_lo = kBandY + kZoneR + 24, y_hi = hpx - y_lo;
    // The kind: Plains, Woodlands and Ponds always; Ridges only without the Mountains switch (whose ridges are the
    // same, higher).
    std::vector<LandKind> kinds{LandKind::kPlains, LandKind::kWoodlands, LandKind::kPonds};
    if (!g.mountains) kinds.push_back(LandKind::kRidges);
    const LandKind kind = kinds[r.below(static_cast<std::uint32_t>(kinds.size()))];
    design->kind = kind;
    const bool drawn = kind == LandKind::kWoodlands || kind == LandKind::kPonds;
    SeaGrid dg;
    dg.w = static_cast<std::int32_t>(wpx / kSeaStep) + 1, dg.h = static_cast<std::int32_t>(hpx / kSeaStep) + 1;
    dg.step = kSeaStep;
    SeaGrid gg;  // the land paths: every other node
    gg.w = (dg.w + 1) / 2, gg.h = (dg.h + 1) / 2, gg.step = kGeoStep;
    for (int draw = 0; draw < 24; ++draw) {
        Draft dr;
        std::string words;
        switch (kind) {
            case LandKind::kPlains: words = "Plains, open country"; break;
            case LandKind::kWoodlands: DrawWoods(r, large, wpx, hpx, &dr, &words); break;
            case LandKind::kRidges: words = "Ridges, hill ridges with passes"; break;
            case LandKind::kPonds: DrawPonds(r, large, n, wpx, hpx, &dr, &words); break;
        }
        std::vector<std::int32_t> val(dg.N(), -static_cast<std::int32_t>(kBodyDeep));
        std::vector<std::uint8_t> wet(dg.N(), 0), dry(dg.N(), 1);
        if (drawn) {
            for (std::int32_t j = 0; j < dg.h; ++j) {
                for (std::int32_t i = 0; i < dg.w; ++i) {
                    const std::size_t k = dg.I(i, j);
                    val[k] =
                        static_cast<std::int32_t>(DraftAt(dr, std::int64_t{i} * kSeaStep, std::int64_t{j} * kSeaStep));
                    wet[k] = kind == LandKind::kPonds && val[k] > 0 ? 1 : 0;
                    dry[k] = wet[k] != 0 ? 0 : 1;
                }
            }
        }
        // The land for the paths: the main dry land (4-connected; a pond never cuts the map, but a corner behind one
        // is no place to start). Woodlands are walked through (men walk through trees; the corridors open them for
        // tanks), so their belts only keep the starts off.
        const std::vector<std::uint8_t> main_land = kind == LandKind::kPonds ? Biggest(dg, dry, false) : dry;
        const std::vector<std::int32_t> to_wet =
            kind == LandKind::kPonds ? Chamfer(dg, wet) : std::vector<std::int32_t>();
        std::vector<std::uint8_t> geo_land(gg.N(), 0);
        for (std::int32_t j = 0; j < gg.h; ++j) {
            for (std::int32_t i = 0; i < gg.w; ++i)
                geo_land[gg.I(i, j)] = main_land[dg.I(std::min(2 * i, dg.w - 1), std::min(2 * j, dg.h - 1))];
        }
        // The candidates: nodes of the land path grid inside the frame, a pond's shore kZoneR + kMargin + 100 px off
        // (the pond stays whole), a belt's edge 1,000 px off (its trees stop at the zone's margin); at most about 320
        // of them (400 with 7 or 8 players; every k-th by a hash).
        const std::int64_t keep = kZoneR + kMargin + 100;
        const std::size_t most_cand = n >= 7 ? 400 : 320;
        std::vector<std::size_t> cand;
        for (std::int32_t j = 0; j < gg.h; ++j) {
            for (std::int32_t i = 0; i < gg.w; ++i) {
                const std::size_t k = dg.I(std::min(2 * i, dg.w - 1), std::min(2 * j, dg.h - 1));
                const std::int64_t x = std::int64_t{i} * kGeoStep, y = std::int64_t{j} * kGeoStep;
                if (geo_land[gg.I(i, j)] == 0 || x < x_lo || x > x_hi || y < y_lo || y > y_hi) continue;
                if (kind == LandKind::kPonds && to_wet[k] < keep) continue;
                if (kind == LandKind::kWoodlands && val[k] > -1000) continue;
                cand.push_back(gg.I(i, j));
            }
        }
        if (cand.size() > most_cand) {
            const std::uint32_t thin = r.next(), keep_k = static_cast<std::uint32_t>(cand.size() / most_cand + 1);
            std::vector<std::size_t> some;
            for (std::size_t c : cand) {
                if (Hash(thin, static_cast<std::int64_t>(c), 3) % keep_k == 0) some.push_back(c);
            }
            cand = std::move(some);
        }
        const std::size_t nc = cand.size();
        if (nc < static_cast<std::size_t>(n)) continue;
        auto pt = [&](std::size_t c) {
            return Pt{static_cast<std::int64_t>(cand[c] % static_cast<std::size_t>(gg.w)) * kGeoStep,
                      static_cast<std::int64_t>(cand[c] / static_cast<std::size_t>(gg.w)) * kGeoStep};
        };
        // The straight distance between every two candidates, and the land path (-1: none) where ponds bend it; on
        // the other kinds the land is open, and the path is the line.
        std::vector<std::int32_t> line(nc * nc);
        for (std::size_t a = 0; a < nc; ++a) {
            for (std::size_t b = 0; b < nc; ++b)
                line[a * nc + b] = static_cast<std::int32_t>(ISqrt(Dist2(pt(a), pt(b))));
        }
        std::vector<std::int32_t> geo(line);
        if (kind == LandKind::kPonds) {
            for (std::size_t a = 0; a < nc; ++a) {
                const std::vector<std::int32_t> d = Paths(gg, geo_land, cand[a], kGeoStep, kGeoStep);
                for (std::size_t b = 0; b < nc; ++b) geo[a * nc + b] = d[cand[b]];
            }
        }
        // How fair a set of starts is spaced, per mille: the least of the nearest enemy by land (smallest over
        // largest) and in a straight line (the same, plus 50); below 0 when two starts are closer than min_d (minus
        // the sum of what every too close pair lacks, so a climb can spread a crowded set) or one reaches another
        // not at all. `worst`: the start whose nearest neighbour is the nearest of all (the one to move).
        std::size_t worst = 0;
        const std::int64_t need = n == 2 ? std::max(min_d, wpx * 4 / 10) : min_d;
        auto spacing = [&](const std::vector<std::size_t>& set) {
            std::int64_t e_lo = kInf, e_hi = 0, l_lo = kInf, l_hi = 0, lack = 0;
            for (std::size_t k = 0; k < set.size(); ++k) {
                const std::size_t a = set[k];
                std::int64_t e = kInf, l = kInf;
                for (std::size_t b : set) {
                    if (b == a) continue;
                    const std::int64_t v = geo[a * nc + b];
                    if (v < 0) return std::int64_t{-100000};
                    e = std::min(e, v);
                    l = std::min<std::int64_t>(l, line[a * nc + b]);
                    if (line[a * nc + b] < need) lack += need - line[a * nc + b];
                }
                if (l < l_lo) worst = k;
                e_lo = std::min(e_lo, e), e_hi = std::max(e_hi, e), l_lo = std::min(l_lo, l), l_hi = std::max(l_hi, l);
            }
            if (lack > 0) return -lack;
            return std::min(e_lo * 1000 / std::max<std::int64_t>(1, e_hi),
                            l_lo * 1000 / std::max<std::int64_t>(1, l_hi) + 50);
        };
        Fit best;
        int judged = 0;
        std::vector<std::int64_t> near(nc);
        for (int t = 0; t < 12 && judged < 6 && best.score < 900; ++t) {
            // A randomised farthest-point pick (each next start among the candidates at least 0.72 as far from the
            // ones chosen as the farthest), then moved one start at a time while the spacing gets better.
            std::vector<std::size_t> set{r.below(static_cast<std::uint32_t>(nc))};
            for (std::size_t c = 0; c < nc; ++c) near[c] = Dist2(pt(c), pt(set[0]));
            while (set.size() < static_cast<std::size_t>(n)) {
                const std::int64_t far = *std::max_element(near.begin(), near.end());
                std::vector<std::size_t> pool;
                for (std::size_t c = 0; c < nc; ++c) {
                    if (near[c] * 100 >= far * 72) pool.push_back(c);
                }
                const std::size_t c = pool[r.below(static_cast<std::uint32_t>(pool.size()))];
                set.push_back(c);
                for (std::size_t q = 0; q < nc; ++q) near[q] = std::min(near[q], Dist2(pt(q), pt(c)));
            }
            std::int64_t sc = spacing(set);
            // The climb: the start with the nearest neighbour goes to the candidate where the spacing is best, or
            // (every second step) a drawn start slides to the best candidate within 1,500 px, until no move helps.
            for (int step = 0, idle = 0; step < 40 * n && idle < 3 * n && sc < 960; ++step) {
                const bool slide = step % 2 == 1;
                const std::size_t k = slide ? r.below(static_cast<std::uint32_t>(n)) : worst;
                const std::size_t was = set[k];
                std::size_t best_c = was;
                std::int64_t best_s = sc;
                for (std::size_t c = 0; c < nc; ++c) {
                    if ((slide && line[was * nc + c] > 1500) || std::find(set.begin(), set.end(), c) != set.end())
                        continue;
                    set[k] = c;
                    const std::int64_t s2 = spacing(set);
                    if (s2 > best_s) best_s = s2, best_c = c;
                }
                set[k] = best_c;
                sc = spacing(set);
                idle = best_c == was ? idle + 1 : 0;
            }
            if (sc < 850) continue;
            ++judged;
            std::vector<Pt> pts, ordered;
            std::vector<Pair> pairs;
            for (std::size_t c : set) pts.push_back(pt(c));
            const std::int64_t s = Judge(gg, geo_land, pts, {}, large, &ordered, &pairs);
            if (s < 0) continue;
            const std::int64_t score = std::min(s, sc);
            if (score > best.score) best.score = score, best.starts = ordered, best.pairs = pairs;
        }
        if (best.score < 0) continue;
        // The cycle from a drawn start, either way round; the pairs follow.
        const int turn = static_cast<int>(r.below(static_cast<std::uint32_t>(n)));
        const bool back = r.below(2) != 0;
        auto place = [&](int q) { return back ? ((turn - q) % n + n) % n : ((q - turn) % n + n) % n; };
        starts->assign(static_cast<std::size_t>(n), Pt{});
        for (int q = 0; q < n; ++q)
            (*starts)[static_cast<std::size_t>(place(q))] = best.starts[static_cast<std::size_t>(q)];
        neighbours->clear();
        for (const Pair& pr : best.pairs) {
            const int a = place(pr[0]), b = place(pr[1]);
            neighbours->push_back({static_cast<std::int8_t>(std::min(a, b)), static_cast<std::int8_t>(std::max(a, b))});
        }
        std::sort(neighbours->begin(), neighbours->end());
        design->field.gw = dg.w, design->field.gh = dg.h;
        design->field.value = drawn ? std::move(val) : std::vector<std::int32_t>();
        design->field.words = words;
        return true;
    }
    *why = "positions: no fair starts on 24 land designs";
    return false;
}

}  // namespace antanta::sim::mapgen
