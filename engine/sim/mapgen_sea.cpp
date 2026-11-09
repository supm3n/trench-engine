// The random map generator's Sea and Islands maps (docs/map-generator-plan.md 5.1, 5.4; decisions 53, 54 and
// 57). See mapgen_sea.hpp.
//
// A design is one of five kinds of sea drawn from the seed (the players and the size limit which kinds can be fair):
// Coastal (one deep sea along a side, land to the other edge), Mediterranean (an inland sea), Gulf (a sea along a
// long side with a long gulf across the land), Strait (Large: a long channel from a short side) and Continental (a
// corner sea, two opposite coasts or three sides, with fjords). A sea along the edge is drawn as land: the coast is
// where a depth ends that varies along the edge by a slow one-dimensional noise (50 to 200 %), so no line runs
// parallel to the edge. Inland seas, gulfs, bays, fjords and land capes are bodies (a line of points with a radius at
// each). Everything is seen through a slow domain warp plus two octaves of noise, and the level is set so the water
// takes the drawn share of the map (20 to 32 %: decision 54 lets a generated map tell the computers it is a water
// map). The starts are then fitted to the land: candidates in the code's band of distance from the main sea
// (decision 57: kSeaNear 1,600 to kSeaFar 2,600 px, or deeper inland, 2,400 to 3,600 or 3,200 to 4,500, about a
// third of the codes each; the same band for every player, a nearer band when a deeper one finds no fair set) and
// within 400 px more of the ships' wide water (ports for everyone), spread by a randomised farthest-point pick and a
// climb on their spacing (straight and by land), and a set is kept only when it is fair by land path: the nearest
// enemy within 0.85, the neighbours by land (the starts whose land territories touch, a Delaunay of the land) each
// with three or more contested places in one common window, four own places for everyone in one window; the land
// shares within 0.35 before LandTerritories evens them out. No ring: a start has as many neighbours as the land gives
// it.
//
// Islands (IslandsPlan, water-first since 2026-11-08, review step 3): the islands are land bodies in an ocean, laid
// out in one of several ways (two bands, two columns, two parallel islands across a diagonal channel, three columns,
// a triangle, four corners, a diamond, a row), each island a bending spine of points with radii, lobes, a peninsula
// and bays, its radii scaled so its land is its players' share; then the starts are fitted per island with the same
// climb and judge, plus the straight distance across the water within 0.85.
#include "mapgen_sea.hpp"
#include "mapgen_water.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <queue>
#include <utility>

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

// One-dimensional value noise in -1024 .. 1024 along a closed line of length `period` (the map's perimeter), about
// `spacing` px between its lattice points (the count rounded so the line closes).
std::int64_t Noise1(std::uint32_t key, std::int64_t u, std::int64_t period, std::int64_t spacing) {
    const std::int64_t n = std::max<std::int64_t>(2, period / spacing), sp = period / n;
    const std::int64_t i = FloorDiv(u, sp), f = (u - i * sp) * 1024 / sp;
    const std::int64_t s = f * f * (3072 - 2 * f) / (1024 * 1024);
    auto v = [&](std::int64_t k) { return static_cast<std::int64_t>(Hash(key, ((k % n) + n) % n, 7) % 2049) - 1024; };
    return (v(i) * (1024 - s) + v(i + 1) * s) / 1024;
}

constexpr std::int64_t kDeep = kBodyDeep;  // values beyond this inside the land are all alike

// A sea along a run of the map's edge, drawn as land (decision 54, after the review of 2026-11-08): from every point
// of the run the sea reaches inland as far as `depth` times a factor from a slow one-dimensional noise along the
// edge (amp_lo to amp_hi per mille), and tapers to nothing over `taper` px past the run's ends. The run is measured
// clockwise along the perimeter from the top-left corner and may turn corners.
struct Shore {
    std::int64_t u0 = 0, len = 0;
    std::int64_t depth = 0;
    std::int64_t scale[4] = {1000, 1000, 1000, 1000};  // per side, per mille: a short side's coast runs up and down
                                                       // the map, and a port's 7 cells need deeper water there
    std::uint32_t key = 0;
    std::int64_t spacing = 3000;
    std::int64_t amp_lo = 500, amp_hi = 2000;
    std::int64_t taper = 2500;
};

// Sides: 0 top (north), 1 right (east), 2 bottom (south), 3 left (west).
std::int64_t SideLen(int s, std::int64_t wpx, std::int64_t hpx) { return s % 2 == 0 ? wpx : hpx; }
std::int64_t SideU0(int s, std::int64_t wpx, std::int64_t hpx) {
    return s == 0 ? 0 : s == 1 ? wpx : s == 2 ? wpx + hpx : 2 * wpx + hpx;
}

// The depth of the shore at perimeter coordinate u of side s (its noise applied: the slow octave over amp_lo to
// amp_hi, a second at a third of the spacing for headlands and coves, up to a quarter more or less; the taper left
// out).
std::int64_t ShoreDepth(const Shore& sh, int s, std::int64_t u, std::int64_t P) {
    const std::int64_t f = sh.amp_lo + (Noise1(sh.key, u, P, sh.spacing) + 1024) * (sh.amp_hi - sh.amp_lo) / 2048 +
                           Noise1(sh.key ^ 0x9e3779b9u, u, P, sh.spacing / 3) * 400 / 1024;
    return sh.depth * sh.scale[s] / 1000 * std::max<std::int64_t>(f, 250) / 1000;
}
int SideOf(std::int64_t u, std::int64_t wpx, std::int64_t hpx) {
    const std::int64_t P = 2 * (wpx + hpx);
    u = ((u % P) + P) % P;
    return u < wpx ? 0 : u < wpx + hpx ? 1 : u < 2 * wpx + hpx ? 2 : 3;
}

// How far inside the shore's water (x, y) lies (negative outside).
std::int64_t ShoreValue(const Shore& sh, std::int64_t x, std::int64_t y, std::int64_t wpx, std::int64_t hpx) {
    const std::int64_t P = 2 * (wpx + hpx);
    std::int64_t best = -kDeep;
    for (int s = 0; s < 4; ++s) {
        std::int64_t t = 0, u = 0;  // the distance from that edge and the perimeter coordinate of the foot
        switch (s) {
            case 0: t = y, u = x; break;
            case 1: t = wpx - x, u = wpx + y; break;
            case 2: t = hpx - y, u = wpx + hpx + (wpx - x); break;
            default: t = x, u = 2 * wpx + hpx + (hpx - y); break;
        }
        if (t > sh.depth * sh.scale[s] / 1000 * sh.amp_hi / 1000 + 500) continue;  // never water this far in
        const std::int64_t du = ((u - sh.u0) % P + P) % P;
        const std::int64_t inside = du <= sh.len ? std::min(du, sh.len - du) : -std::min(du - sh.len, P - du);
        const std::int64_t ramp = std::clamp(inside, -sh.taper, sh.taper);
        best = std::max(best, ShoreDepth(sh, s, u, P) * ramp / sh.taper - t);
    }
    return best;
}

struct Draft {
    std::vector<Body> bodies;
    std::vector<Shore> shores;
    bool ocean = false;  // islands: water everywhere but in the land bodies (else land everywhere but in the water)
    bool edge = false;   // the sea lies along the edge: its depths and widths are scaled to the share, the level moves
                         // little (an inland sea takes the plain level search)
    std::uint32_t wk = 0, k1 = 0, k2 = 0;
    std::int64_t wa = 0, ws = 1, a1 = 0, s1 = 1, a2 = 0, s2 = 1;
    std::int64_t wpx = 0, hpx = 0;
    std::string words;
};

std::int64_t DraftAt(const Draft& d, std::int64_t x, std::int64_t y) {
    const std::int64_t qx = x + Noise(d.wk, x, y, d.ws) * d.wa / 1024;
    const std::int64_t qy = y + Noise(d.wk ^ 0x5bd1e995u, x, y, d.ws) * d.wa / 1024;
    std::int64_t v = d.ocean ? kDeep : -kDeep;
    for (const Shore& sh : d.shores) v = std::max(v, ShoreValue(sh, qx, qy, d.wpx, d.hpx));
    for (const Body& b : d.bodies) {
        if (!b.land && !b.cut) v = std::max(v, BodyValue(b, qx, qy));
    }
    for (const Body& b : d.bodies) {
        if (b.land) v = std::min(v, -BodyValue(b, qx, qy));
    }
    for (const Body& b : d.bodies) {
        if (b.cut) v = std::max(v, BodyValue(b, qx, qy));
    }
    return v + Noise(d.k1, x, y, d.s1) * d.a1 / 1024 + Noise(d.k2, x, y, d.s2) * d.a2 / 1024;
}

// A point of the map's edge, `u` px clockwise from the top-left corner, and the direction into the map there.
Pt Perim(std::int64_t u, std::int64_t wpx, std::int64_t hpx) {
    const std::int64_t P = 2 * (wpx + hpx);
    u = ((u % P) + P) % P;
    if (u < wpx) return Pt{u, 0};
    u -= wpx;
    if (u < hpx) return Pt{wpx, u};
    u -= hpx;
    if (u < wpx) return Pt{wpx - u, hpx};
    return Pt{0, hpx - (u - wpx)};
}
Pt Inward(std::int64_t u, std::int64_t wpx, std::int64_t hpx) {
    const std::int64_t P = 2 * (wpx + hpx);
    u = ((u % P) + P) % P;
    if (u < wpx) return Pt{0, 65536};
    if (u < wpx + hpx) return Pt{-65536, 0};
    if (u < 2 * wpx + hpx) return Pt{0, -65536};
    return Pt{65536, 0};
}

// A direction from p toward the map's middle, turned up to about 35 degrees either way (drawn).
Pt Inland(Rng& r, Pt p, std::int64_t wpx, std::int64_t hpx) {
    const Pt c = Unit(wpx / 2 - p.x, hpx / 2 - p.y);
    const std::int64_t k = r.range(-700, 700);
    return Unit(c.x * 1024 - c.y * k, c.y * 1024 + c.x * k);
}

// A tapering, bending arm of water from p along u.
Body Arm(Rng& r, Pt p, Pt u, std::int64_t len, std::int64_t w0, std::int64_t w1) {
    Body b;
    const std::int64_t bend = r.range(-len / 4, len / 4);
    b.line = {p, Toward(Toward(p, u, len / 2), Perp(u), bend), Toward(Toward(p, u, len), Perp(u), bend / 2)};
    b.rad = {w0, (w0 + w1) / 2, w1};
    return b;
}

enum class Kind { kCoastal, kMediterranean, kGulf, kStrait, kContinental };
const char* kSideName[4] = {"north", "east", "south", "west"};

// One design drawn (decision 54: a kind of sea chosen by the code among those that can be fair for the players and
// the size, each its own recipe; nothing on the map's axes but the edge itself, nothing mirrored). The share of the
// map the water takes goes to `share` (per mille of the nodes).
void Draw(Rng& r, int n, bool large, std::int64_t wpx, std::int64_t hpx, Draft* d, std::int64_t* share) {
    auto S = [&](std::int64_t v) { return large ? v * 3 / 2 : v; };
    d->wpx = wpx, d->hpx = hpx;
    d->wk = r.next(), d->k1 = r.next(), d->k2 = r.next();
    d->ws = S(r.range(5000, 8500));
    d->wa = d->ws * r.range(14, 24) / 100;
    d->s1 = S(r.range(1800, 3200));
    d->a1 = r.range(250, 600);
    d->s2 = r.range(650, 1100);
    d->a2 = r.range(70, 170);
    const std::int64_t P = 2 * (wpx + hpx);
    // The kinds that can be fair: a single coast or an inland sea leave room for up to 4 starts on Normal (6 on
    // Large); a strait needs the Large map's length; a gulf and the continental coasts take any count.
    std::vector<Kind> kinds{Kind::kGulf, Kind::kContinental};
    if (n <= (large ? 6 : 4)) kinds.push_back(Kind::kCoastal), kinds.push_back(Kind::kMediterranean);
    if (large && n >= 3) kinds.push_back(Kind::kStrait);
    const Kind kind = kinds[r.below(static_cast<std::uint32_t>(kinds.size()))];
    char buf[200];
    auto any_dir = [&]() {
        const std::int64_t x = r.range(-1024, 1024);
        const std::int64_t y = r.range(-1024, 1024);
        return Unit(x == 0 && y == 0 ? 1 : x, y);
    };
    // A shore along `sides` whole sides in a row from `first`, reaching ext0 and ext1 px round the corners at its
    // ends (negative: it ends before the corner and land holds the corner); its depth factor drawn in lo_lo to
    // lo_hi and hi_lo to hi_hi per mille (two draws, one after the other: never two draws in one call's arguments).
    auto shore = [&](int first, int sides, std::int64_t depth, std::int64_t lo_lo, std::int64_t lo_hi,
                     std::int64_t hi_lo, std::int64_t hi_hi) {
        Shore sh;
        const std::int64_t amp_lo = r.range(lo_lo, lo_hi);
        const std::int64_t amp_hi = r.range(hi_lo, hi_hi);
        const std::int64_t ext0 = r.range(-1500, 3500), ext1 = r.range(-1500, 3500);
        sh.u0 = SideU0(first, wpx, hpx) - ext0;
        sh.len = ext0 + ext1;
        for (int q = 0; q < sides; ++q) sh.len += SideLen((first + q) % 4, wpx, hpx);
        sh.len = std::min(sh.len, P);
        sh.depth = depth;
        for (int q = 0; q < sides; ++q) {
            if ((first + q) % 2 == 1) sh.scale[(first + q) % 4] = large ? 1400 : 1600;
        }
        sh.key = r.next();
        sh.spacing = S(r.range(2200, 6500));
        sh.amp_lo = amp_lo, sh.amp_hi = amp_hi;
        sh.taper = S(r.range(2000, 3200));
        d->shores.push_back(sh);
        d->edge = true;
        return sh;
    };
    // A point of a shore's run and the depth there.
    auto on_shore = [&](const Shore& sh, std::int64_t* u) {
        *u = sh.u0 + static_cast<std::int64_t>(r.below(static_cast<std::uint32_t>(std::max<std::int64_t>(1, sh.len))));
        return ShoreDepth(sh, SideOf(*u, wpx, hpx), *u, P);
    };
    // Bays (round), fjords (narrow bending arms from the coast inland) and capes (land strips reaching toward the
    // edge) on a shore.
    int bays = 0, fjords = 0, capes = 0;
    auto shore_features = [&](const Shore& sh, int bays_hi, int fjords_hi, int capes_hi) {
        const int b = static_cast<int>(r.range(bays_hi > 0 ? 1 : 0, bays_hi));
        for (int q = 0; q < b; ++q) {
            std::int64_t u = 0;
            const std::int64_t depth = on_shore(sh, &u);
            Body body;
            body.line = {Toward(Perim(u, wpx, hpx), Inward(u, wpx, hpx), depth + S(r.range(200, 1200)))};
            body.rad = {S(r.range(700, 1400))};
            d->bodies.push_back(body);
        }
        const int f = static_cast<int>(r.range(0, fjords_hi));
        for (int q = 0; q < f; ++q) {
            std::int64_t u = 0;
            const std::int64_t depth = on_shore(sh, &u);
            const Pt p = Perim(u, wpx, hpx), v = Inland(r, p, wpx, hpx);
            const std::int64_t len = S(r.range(2000, 3800));
            const std::int64_t w0 = S(r.range(450, 700));
            const std::int64_t w1 = S(r.range(150, 300));
            d->bodies.push_back(Arm(r, Toward(p, Inward(u, wpx, hpx), depth * 6 / 10), v, len, w0, w1));
        }
        const int c = static_cast<int>(r.range(0, capes_hi));
        for (int q = 0; q < c; ++q) {
            std::int64_t u = 0;
            const std::int64_t depth = on_shore(sh, &u);
            const Pt p = Perim(u, wpx, hpx), v = Inward(u, wpx, hpx);
            Body body;
            body.land = true;
            body.line = {Toward(p, v, depth + S(3000)), Toward(p, v, depth * r.range(15, 40) / 100)};
            const std::int64_t w = S(r.range(350, 700));
            body.rad = {w, w * 2 / 3};
            d->bodies.push_back(body);
        }
        bays += b, fjords += f, capes += c;
    };
    // A round sea in a corner of a shore's run (the inner corner of two sides), so the two coasts meet in a curve.
    auto corner_sea = [&](int corner, std::int64_t depth) {
        const Pt p = Perim(SideU0(corner, wpx, hpx), wpx, hpx);
        const Pt v = Inland(r, p, wpx, hpx);
        Body b;
        b.line = {Toward(p, v, S(r.range(0, 800)))};
        b.rad = {depth * r.range(120, 180) / 100};
        d->bodies.push_back(b);
    };
    switch (kind) {
        case Kind::kCoastal: {
            // One deep sea along a side (a long side from 3 players on: the starts stand along it), land to the
            // other edge.
            const int side = n == 2 ? static_cast<int>(r.below(4)) : static_cast<int>(r.below(2)) * 2;
            const Shore sh = shore(side, 1, large ? 2600 : 2200, 350, 550, 1600, 2100);
            shore_features(sh, 3, 2, 2);
            *share = r.range(200, 300);
            std::snprintf(buf, sizeof buf, "Coastal, a deep sea on the %s side, %d bays, %d fjords, %d capes",
                          kSideName[side], bays, fjords, capes);
            break;
        }
        case Kind::kMediterranean: {
            // An inland sea with bays, gulfs and capes, anywhere, in any direction.
            const std::int64_t off_x = r.range(-wpx / 6, wpx / 6), off_y = r.range(-hpx / 6, hpx / 6);
            const Pt c{wpx / 2 + off_x, hpx / 2 + off_y};
            const std::int64_t ux = r.range(-1024, 1024), uy = r.range(-600, 600);
            const Pt u = Unit(ux == 0 && uy == 0 ? 1 : ux, uy);
            const std::int64_t len = r.range(wpx * 20 / 100, wpx * 45 / 100);
            const int k = 2 + static_cast<int>(r.below(3));
            Body sea;
            std::int64_t rad_sum = 0;
            for (int j = 0; j < k; ++j) {
                const std::int64_t off = r.range(-hpx / 8, hpx / 8);
                sea.line.push_back(Toward(Toward(c, u, -len / 2 + len * j / (k - 1)), Perp(u), off));
                const std::int64_t rad = S(r.range(1200, 2600));
                sea.rad.push_back(rad);
                rad_sum += rad;
            }
            const std::int64_t rad_p = rad_sum / k;  // the sea's mean half width: bays, gulfs and capes start there
            d->bodies.push_back(sea);
            auto on_sea = [&]() {
                const std::size_t j = r.below(static_cast<std::uint32_t>(k - 1));
                const std::int64_t t = r.range(0, 1024);
                const Pt a = sea.line[j], b = sea.line[j + 1];
                return Pt{a.x + (b.x - a.x) * t / 1024, a.y + (b.y - a.y) * t / 1024};
            };
            bays = static_cast<int>(r.range(1, 3));
            for (int q = 0; q < bays; ++q) {
                const Pt p = on_sea(), v = any_dir();
                Body b;
                b.line = {Toward(p, v, rad_p + S(r.range(-500, 700)))};
                b.rad = {S(r.range(700, 1500))};
                d->bodies.push_back(b);
            }
            fjords = static_cast<int>(r.range(0, 2));  // gulfs here
            for (int q = 0; q < fjords; ++q) {
                const Pt p = on_sea(), v = any_dir();
                const std::int64_t len_g = S(r.range(2000, 3800));
                const std::int64_t w0 = S(r.range(700, 1000));
                const std::int64_t w1 = S(r.range(250, 400));
                d->bodies.push_back(Arm(r, Toward(p, v, rad_p * 2 / 3), v, len_g, w0, w1));
            }
            capes = static_cast<int>(r.range(0, 2));
            for (int q = 0; q < capes; ++q) {
                const Pt p = on_sea(), v = any_dir();
                Body b;
                b.land = true;
                b.line = {Toward(p, v, rad_p + S(3000)), Toward(p, v, rad_p * r.range(25, 60) / 100)};
                const std::int64_t w = S(r.range(350, 650));
                b.rad = {w, w * 2 / 3};
                d->bodies.push_back(b);
            }
            *share = r.range(200, 280);
            std::snprintf(buf, sizeof buf, "Mediterranean, an inland sea, %d bays, %d gulfs, %d capes", bays, fjords,
                          capes);
            break;
        }
        case Kind::kGulf: {
            // A sea along a long side and one long gulf from it across the land (55 to 80 % of the height, bending),
            // so the starts on its two sides are far apart by land and no neighbours (decision 54's cut pairs).
            const int side = static_cast<int>(r.below(2)) * 2;
            const Shore sh = shore(side, 1, large ? 1700 : 1400, 450, 650, 1500, 1900);
            const std::int64_t along = wpx * r.range(30, 70) / 100, u = SideU0(side, wpx, hpx) + along;
            const Pt p = Perim(u, wpx, hpx), in = Inward(u, wpx, hpx);
            const std::int64_t tilt = r.range(-150, 150);
            const Pt v = Unit(in.x * 1024 - in.y * tilt, in.y * 1024 + in.x * tilt);
            const std::int64_t len_c = hpx * r.range(55, 80) / 100;
            const std::int64_t w0 = large ? r.range(1500, 2100) : r.range(1100, 1600), w1 = w0 * r.range(50, 75) / 100;
            const std::int64_t bend = r.range(-len_c / 8, len_c / 8);
            // Four points: the width varies along it (a narrows, a widening), so its sides are no parallels.
            Body b;
            b.line = {p, Toward(Toward(p, v, len_c / 3), Perp(v), bend * 2 / 3),
                      Toward(Toward(p, v, len_c * 2 / 3), Perp(v), bend),
                      Toward(Toward(p, v, len_c), Perp(v), bend / 2)};
            const std::int64_t wa = (w0 * 2 + w1) / 3 * r.range(65, 135) / 100;
            const std::int64_t wb = (w0 + w1 * 2) / 3 * r.range(65, 135) / 100;
            b.rad = {w0, wa, wb, w1};
            d->bodies.push_back(b);
            // Side bays off the gulf, and the shore's own features.
            const int side_bays = static_cast<int>(r.range(0, 2));
            for (int q = 0; q < side_bays; ++q) {
                const std::int64_t t = r.range(300, 900);
                const Pt c = Toward(Toward(p, v, len_c * t / 1024), Perp(v), bend * (1024 - t) / 1024);
                const Pt w = any_dir();
                Body bay;
                bay.line = {Toward(c, w, (w0 + w1) / 2)};
                bay.rad = {S(r.range(500, 900))};
                d->bodies.push_back(bay);
            }
            shore_features(sh, 2, 1, 1);
            bays += side_bays;
            *share = r.range(210, 300);
            std::snprintf(buf, sizeof buf,
                          "Gulf, a sea on the %s side and a long gulf across the land, %d bays, "
                          "%d fjords, %d capes",
                          kSideName[side], bays, fjords, capes);
            break;
        }
        case Kind::kStrait: {
            // Large: a long channel of nearly even width from a short side deep into the land (65 to 85 % of the
            // length), bending, with a sea at its mouth; the land wraps round its end.
            const int side = static_cast<int>(r.below(2)) * 2 + 1;
            const Shore sh = shore(side, 1, 1600, 700, 800, 1400, 1700);
            const std::int64_t along = hpx * r.range(30, 70) / 100, u = SideU0(side, wpx, hpx) + along;
            const Pt p = Perim(u, wpx, hpx), in = Inward(u, wpx, hpx);
            const std::int64_t tilt = r.range(-120, 120);
            const Pt v = Unit(in.x * 1024 - in.y * tilt, in.y * 1024 + in.x * tilt);
            const std::int64_t len_c = wpx * r.range(65, 85) / 100;
            const std::int64_t w0 = r.range(1600, 2400);
            // Its width varies along it (narrows and widenings), and it may end in a round lagoon.
            Body b;
            const int k = 4;
            for (int j = 0; j < k; ++j) {
                const std::int64_t off = j == 0 ? 0 : r.range(-hpx / 10, hpx / 10);
                b.line.push_back(Toward(Toward(p, v, len_c * j / (k - 1)), Perp(v), off));
                b.rad.push_back(w0 * r.range(60, 130) / 100);
            }
            if (r.below(2) != 0) b.rad.back() = w0 * r.range(140, 200) / 100;
            d->bodies.push_back(b);
            const int side_bays = static_cast<int>(r.range(0, 2));
            for (int q = 0; q < side_bays; ++q) {
                const std::int64_t t = r.range(300, 1000);
                const Pt c = Toward(p, v, len_c * t / 1024), w = any_dir();
                Body bay;
                bay.line = {Toward(c, w, w0)};
                bay.rad = {r.range(700, 1300)};
                d->bodies.push_back(bay);
            }
            shore_features(sh, 2, 1, 1);
            bays += side_bays;
            *share = r.range(220, 300);
            std::snprintf(buf, sizeof buf, "Strait, a long channel from the %s side, %d bays, %d fjords, %d capes",
                          kSideName[side], bays, fjords, capes);
            break;
        }
        case Kind::kContinental: {
            // Coasts with fjords: a corner sea (two adjacent sides), two opposite long coasts (from 5 players on:
            // two rows of starts), or three sides.
            const std::uint32_t pick = r.below(100);
            int variant = 0;  // 0 a corner, 1 opposite, 2 three sides
            if (n <= 4) {
                variant = pick < 60 ? 0 : 2;
            } else if (n <= 6 || large) {
                variant = pick < 30 ? 0 : pick < 65 ? 1 : 2;
            } else {
                variant = pick < 40 ? 1 : 2;
            }
            if (variant == 0) {
                const int corner = static_cast<int>(r.below(4));  // the run starts on side `corner`
                const Shore sh = shore(corner, 2, large ? 1800 : 1500, 500, 650, 1500, 1900);
                corner_sea((corner + 1) % 4, sh.depth);
                shore_features(sh, 3, 3, 2);
                *share = r.range(220, 300);
                std::snprintf(buf, sizeof buf,
                              "Continental, a corner sea to the %s and %s, %d bays, %d fjords, "
                              "%d capes",
                              kSideName[corner], kSideName[(corner + 1) % 4], bays, fjords, capes);
            } else if (variant == 1) {
                const Shore a = shore(0, 1, large ? 1500 : 1200, 500, 650, 1500, 1900);
                const Shore b = shore(2, 1, large ? 1500 : 1200, 500, 650, 1500, 1900);
                shore_features(a, 2, 2, 1);
                shore_features(b, 2, 2, 1);
                *share = r.range(230, 310);
                std::snprintf(buf, sizeof buf,
                              "Continental, coasts to the north and south, %d bays, %d fjords, "
                              "%d capes",
                              bays, fjords, capes);
            } else {
                // From a short side when the starts stand in two rows (5 players on), so both long sides are in.
                const int first = n >= 5 ? (r.below(2) != 0 ? 1 : 3) : static_cast<int>(r.below(4));
                const Shore sh = shore(first, 3, large ? 1400 : 1150, 500, 700, 1500, 1900);
                const int corners = static_cast<int>(r.range(0, 2));
                for (int q = 0; q < corners; ++q) corner_sea((first + 1 + static_cast<int>(r.below(2))) % 4, sh.depth);
                shore_features(sh, 3, 3, 2);
                *share = r.range(260, 330);
                std::snprintf(buf, sizeof buf,
                              "Continental, coasts on three sides (%s to %s), %d bays, %d fjords, "
                              "%d capes",
                              kSideName[first], kSideName[(first + 2) % 4], bays, fjords, capes);
            }
            break;
        }
    }
    // A sea along the edge keeps a calmer warp (6 to 11 %): its depth varies by the shore's own noise, and a strong
    // warp would push the coast off the map in places and leave a start there without a coast.
    if (d->edge) d->wa = d->ws * r.range(6, 11) / 100;
    d->words = buf;
}

}  // namespace

// ---- Grids (the fitter's, shared with the land maps: mapgen_sea.hpp). ----

// Chamfer distances (world px) from the nodes with seed != 0, a 5 x 5 mask (orthogonal, diagonal, knight moves).
std::vector<std::int32_t> Chamfer(const SeaGrid& g, const std::vector<std::uint8_t>& seed) {
    std::vector<std::int32_t> d(g.N(), kInf);
    for (std::size_t k = 0; k < d.size(); ++k) {
        if (seed[k] != 0) d[k] = 0;
    }
    const auto s = static_cast<std::int32_t>(g.step);
    const std::int32_t a = s, b = s * 181 / 128, c = s * 286 / 128;
    static constexpr std::array<std::array<std::int32_t, 2>, 8> kBack = {
        {{-1, 0}, {-1, -1}, {0, -1}, {1, -1}, {-1, -2}, {1, -2}, {-2, -1}, {2, -1}}};
    const std::array<std::int32_t, 8> len = {a, b, a, b, c, c, c, c};
    for (int pass = 0; pass < 2; ++pass) {
        const int sg = pass == 0 ? 1 : -1;
        for (std::int32_t jj = 0; jj < g.h; ++jj) {
            const std::int32_t j = pass == 0 ? jj : g.h - 1 - jj;
            for (std::int32_t ii = 0; ii < g.w; ++ii) {
                const std::int32_t i = pass == 0 ? ii : g.w - 1 - ii;
                std::int32_t& v = d[g.I(i, j)];
                for (std::size_t q = 0; q < kBack.size(); ++q) {
                    const std::int32_t x = i + sg * kBack[q][0], y = j + sg * kBack[q][1];
                    if (x < 0 || y < 0 || x >= g.w || y >= g.h) continue;
                    v = std::min(v, d[g.I(x, y)] + len[q]);
                }
            }
        }
    }
    return d;
}

// The biggest connected set of nodes with on != 0 (8-connected or 4-connected): 1 there, else 0.
std::vector<std::uint8_t> Biggest(const SeaGrid& g, const std::vector<std::uint8_t>& on, bool eight) {
    std::vector<std::int32_t> comp(g.N(), -1);
    std::int32_t best = -1;
    std::size_t best_n = 0;
    std::vector<std::int32_t> todo;
    for (std::size_t k0 = 0; k0 < g.N(); ++k0) {
        if (on[k0] == 0 || comp[k0] >= 0) continue;
        const auto id = static_cast<std::int32_t>(k0);
        todo.assign(1, id);
        comp[k0] = id;
        for (std::size_t t = 0; t < todo.size(); ++t) {
            const std::int32_t i0 = todo[t] % g.w, j0 = todo[t] / g.w;
            for (std::int32_t dj = -1; dj <= 1; ++dj) {
                for (std::int32_t di = -1; di <= 1; ++di) {
                    if ((di == 0 && dj == 0) || (!eight && di != 0 && dj != 0)) continue;
                    const std::int32_t i = i0 + di, j = j0 + dj;
                    if (i < 0 || j < 0 || i >= g.w || j >= g.h) continue;
                    const std::size_t k = g.I(i, j);
                    if (on[k] == 0 || comp[k] >= 0) continue;
                    comp[k] = id;
                    todo.push_back(static_cast<std::int32_t>(k));
                }
            }
        }
        if (todo.size() > best_n) best_n = todo.size(), best = id;
    }
    std::vector<std::uint8_t> out(g.N(), 0);
    for (std::size_t k = 0; k < g.N(); ++k) out[k] = comp[k] == best && best >= 0 ? 1 : 0;
    return out;
}

// Path distances (world px) over the nodes with pass != 0 from `src`, 8 neighbours; -1 where not reached.
std::vector<std::int32_t> Paths(const SeaGrid& g, const std::vector<std::uint8_t>& pass, std::size_t src,
                                std::int64_t sx, std::int64_t sy) {
    std::vector<std::int32_t> d(g.N(), -1);
    if (pass[src] == 0) return d;
    using Item = std::pair<std::int32_t, std::int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> q;
    d[src] = 0;
    q.emplace(0, static_cast<std::int32_t>(src));
    const auto ax = static_cast<std::int32_t>(sx), ay = static_cast<std::int32_t>(sy);
    const auto dg = static_cast<std::int32_t>(ISqrt(sx * sx + sy * sy));
    while (!q.empty()) {
        const auto [du, u] = q.top();
        q.pop();
        if (du != d[static_cast<std::size_t>(u)]) continue;
        const std::int32_t i0 = u % g.w, j0 = u / g.w;
        for (std::int32_t dj = -1; dj <= 1; ++dj) {
            for (std::int32_t di = -1; di <= 1; ++di) {
                if (di == 0 && dj == 0) continue;
                const std::int32_t i = i0 + di, j = j0 + dj;
                if (i < 0 || j < 0 || i >= g.w || j >= g.h) continue;
                const std::size_t v = g.I(i, j);
                if (pass[v] == 0) continue;
                const std::int32_t nd = du + (di != 0 && dj != 0 ? dg : di != 0 ? ax : ay);
                if (d[v] < 0 || nd < d[v]) {
                    d[v] = nd;
                    q.emplace(nd, static_cast<std::int32_t>(v));
                }
            }
        }
    }
    return d;
}

namespace {

struct Fit {
    std::vector<Pt> starts;   // in cycle order
    std::vector<Pair> pairs;  // the neighbours by land, indices into `starts`
    std::int64_t score = -1;
};

}  // namespace

// The judge (mapgen_sea.hpp).
std::int64_t Judge(const SeaGrid& gg, const std::vector<std::uint8_t>& land, std::vector<Pt> pts,
                   const std::vector<std::int8_t>& island, bool large, std::vector<Pt>* ordered,
                   std::vector<Pair>* pairs_out) {
    const std::size_t n = pts.size();
    auto isl = [&](std::size_t s) { return island.empty() ? std::int8_t{0} : island[s]; };
    std::vector<int> isize(n, 0);  // the players on the start's island
    for (std::size_t s = 0; s < n; ++s) {
        for (std::size_t q = 0; q < n; ++q) isize[s] += isl(q) == isl(s) ? 1 : 0;
    }
    std::vector<std::size_t> node(n);
    for (std::size_t s = 0; s < n; ++s) {
        const auto i =
            static_cast<std::int32_t>(std::clamp<std::int64_t>((pts[s].x + gg.step / 2) / gg.step, 0, gg.w - 1));
        const auto j =
            static_cast<std::int32_t>(std::clamp<std::int64_t>((pts[s].y + gg.step / 2) / gg.step, 0, gg.h - 1));
        node[s] = gg.I(i, j);
        if (land[node[s]] == 0) return -1;
    }
    std::vector<std::vector<std::int32_t>> g(n);
    for (std::size_t s = 0; s < n; ++s) g[s] = Paths(gg, land, node[s], gg.step, gg.step);
    std::vector<std::vector<std::int32_t>> m(n, std::vector<std::int32_t>(n, 0));
    std::array<std::int64_t, 9> e_lo{}, e_hi{};  // per island size (a Sea map: all under n)
    e_lo.fill(kInf);
    for (std::size_t s = 0; s < n; ++s) {
        std::int64_t e = kInf;
        for (std::size_t t = 0; t < n; ++t) {
            m[s][t] = g[s][node[t]];
            if (t == s || isl(t) != isl(s)) continue;
            if (m[s][t] < 0) return -2;
            e = std::min<std::int64_t>(e, m[s][t]);
        }
        if (isize[s] < 2) continue;
        const auto sz = static_cast<std::size_t>(isize[s]);
        e_lo[sz] = std::min(e_lo[sz], e), e_hi[sz] = std::max(e_hi[sz], e);
    }
    std::int64_t geo = 1000;
    for (std::size_t sz = 2; sz < e_lo.size(); ++sz) {
        if (e_hi[sz] > 0) geo = std::min(geo, e_lo[sz] * 1000 / e_hi[sz]);
    }
    if (geo < 850) return -3;
    // The shortest cycle through all starts by land: the order of the zones (teammates side by side). Islands keep
    // the given order (island by island).
    std::vector<std::size_t> order(n);
    for (std::size_t s = 0; s < n; ++s) order[s] = s;
    if (n >= 4 && island.empty()) {
        std::vector<std::size_t> perm(order), best_perm(order);
        std::int64_t best = -1;
        do {
            std::int64_t sum = 0;
            for (std::size_t k = 0; k < n; ++k) sum += m[perm[k]][perm[(k + 1) % n]];
            if (best < 0 || sum < best) best = sum, best_perm = perm;
        } while (std::next_permutation(perm.begin() + 1, perm.end()));
        order = best_perm;
    }
    // The Voronoi cells by land path: each player's land, and the owner of every node. Then what LandTerritories
    // will reach: the cells by land path plus a weight per start, the weights moved 24 rounds toward equal land
    // (its rule on the coarse grid, every node as land); a set whose land cannot be evened to 0.88 fails (the check
    // wants 0.85 of buildable land, and a weight cannot move land across the water).
    std::vector<std::int8_t> owner(gg.N(), -1);
    std::vector<std::int64_t> share(n, 0), w(n, 0);
    std::int64_t balance = -1;
    for (int it = 0; it < 24; ++it) {
        std::fill(share.begin(), share.end(), 0);
        for (std::size_t v = 0; v < gg.N(); ++v) {
            std::int64_t best = 0;
            owner[v] = -1;
            for (std::size_t s = 0; s < n; ++s) {
                if (g[s][v] < 0) continue;
                const std::int64_t d = g[s][v] + w[s];
                if (owner[v] < 0 || d < best) best = d, owner[v] = static_cast<std::int8_t>(s);
            }
            if (owner[v] >= 0) ++share[static_cast<std::size_t>(owner[v])];
        }
        bool homes = true;
        for (std::size_t s = 0; s < n; ++s) homes = homes && owner[node[s]] == static_cast<std::int8_t>(s);
        const std::int64_t lmin = *std::min_element(share.begin(), share.end());
        const std::int64_t lmax = std::max<std::int64_t>(1, *std::max_element(share.begin(), share.end()));
        if (homes && lmin * 1000 / lmax > balance) balance = lmin * 1000 / lmax;
        if (balance >= 950 || it == 23) break;
        for (std::size_t s = 0; s < n; ++s) {  // toward the mean of its island (what LandTerritories does)
            std::int64_t sum = 0, cnt = 0;
            for (std::size_t q = 0; q < n; ++q) {
                if (isl(q) == isl(s)) sum += share[q], ++cnt;
            }
            const std::int64_t mean = std::max<std::int64_t>(1, sum / std::max<std::int64_t>(1, cnt));
            w[s] += (share[s] - mean) * gg.step * (900 - 30 * it) / 1000 / mean * 10;
        }
    }
    if (balance < 880) return -4;
    // The borders and places below on the plain cells by land path (as LandTerritories starts).
    for (std::size_t v = 0; v < gg.N(); ++v) {
        std::int32_t best = -1;
        owner[v] = -1;
        for (std::size_t s = 0; s < n; ++s) {
            if (g[s][v] >= 0 && (best < 0 || g[s][v] < best)) best = g[s][v], owner[v] = static_cast<std::int8_t>(s);
        }
    }
    // The neighbours by land: the pairs whose cells share a border (adjacent nodes of two owners), 6 node pairs or
    // more (about 1,000 px) and six contested places or more below; with 2 players the one pair.
    std::vector<std::vector<int>> border(n, std::vector<int>(n, 0));
    for (std::int32_t j = 0; j < gg.h; ++j) {
        for (std::int32_t i = 0; i < gg.w; ++i) {
            const std::int8_t a = owner[gg.I(i, j)];
            if (a < 0) continue;
            for (int q = 0; q < 2; ++q) {
                const std::int32_t x = i + (q == 0 ? 1 : 0), y = j + (q == 0 ? 0 : 1);
                if (x >= gg.w || y >= gg.h) continue;
                const std::int8_t b = owner[gg.I(x, y)];
                if (b >= 0 && b != a) ++border[static_cast<std::size_t>(a)][static_cast<std::size_t>(b)];
            }
        }
    }
    const bool two = n == 2 && island.empty();  // a Sea map of two: the one pair, two places 4,000 px apart
    const int need = island.empty() ? 6 : 4;    // border nodes and contested places (an island is small)
    std::vector<std::array<std::size_t, 2>> pairs;
    for (std::size_t a = 0; a < n; ++a) {
        for (std::size_t b = a + 1; b < n; ++b) {
            if (two || border[a][b] + border[b][a] >= need) pairs.push_back({a, b});
        }
    }
    // Whether one window of distance [w, w * ratio / 100] holds `need` places of every item (the deposit groups take
    // room, so one node is not enough). Each item's distances sorted.
    auto window = [](std::vector<std::vector<std::int32_t>>& items, std::int64_t ratio, std::ptrdiff_t need) {
        for (auto& v : items) std::sort(v.begin(), v.end());
        for (const auto& first : items) {
            for (const std::int32_t w : first) {
                const std::int64_t top = std::int64_t{w} * ratio / 100;
                bool all = true;
                for (const auto& v : items) {
                    const auto lo = std::lower_bound(v.begin(), v.end(), w);
                    const auto hi = std::upper_bound(v.begin(), v.end(), top);
                    all = all && hi - lo >= need;
                    if (!all) break;
                }
                if (all) return true;
            }
        }
        return false;
    };
    // Six contested places or more (nodes, about the room of a group of three deposits) between every pair of
    // neighbours (plan 5.6: within 0.92, every other start 1.1 times as far, outside the zones and 600 px more); with
    // 2 players two places 4,000 px apart. A pair without them is no pair of neighbours (a third start's land lies
    // between); a start without any fails. (No common window of distance over the pairs: neighbours by land stand at
    // quite different distances, and the free deposits step takes each pair's nearest window.)
    const std::int32_t gap = static_cast<std::int32_t>(kZoneR + 600);
    std::vector<std::array<std::size_t, 2>> kept;
    std::vector<int> degree(n, 0);
    for (const auto& pr : pairs) {
        int mine = 0;
        std::vector<std::size_t> spots;
        for (std::size_t v = 0; v < gg.N(); ++v) {
            const std::int32_t a = g[pr[0]][v], b = g[pr[1]][v];
            if (a < gap || b < gap) continue;
            const std::int32_t mx = std::max(a, b), mn = std::min(a, b);
            if (mn * 100 < mx * 92) continue;
            bool clear = true;
            for (std::size_t q = 0; q < n && clear; ++q) {
                if (q != pr[0] && q != pr[1] && g[q][v] >= 0 && g[q][v] * 10 < mx * 11) clear = false;
            }
            if (!clear) continue;
            ++mine;
            if (two) spots.push_back(v);
        }
        if (mine < need) {
            if (two) return -5;
            continue;
        }
        if (two) {
            bool apart = false;
            for (std::size_t a = 0; a < spots.size() && !apart; ++a) {
                for (std::size_t b = a + 1; b < spots.size() && !apart; ++b) {
                    const std::int64_t dx = (static_cast<std::int64_t>(spots[a] % gg.w) - spots[b] % gg.w) * gg.step;
                    const std::int64_t dy = (static_cast<std::int64_t>(spots[a] / gg.w) - spots[b] / gg.w) * gg.step;
                    apart = dx * dx + dy * dy >= 4000 * 4000;
                }
            }
            if (!apart) return -6;
        }
        kept.push_back(pr);
        ++degree[pr[0]], ++degree[pr[1]];
    }
    for (std::size_t s = 0; s < n; ++s) {
        if (degree[s] == 0 && isize[s] >= 2) return -5;
    }
    // An own place for everyone in its own cell (every other start 1.25 times as far), in one common window of 1.15:
    // four places (Large eight: two own groups, 2,200 px apart).
    std::vector<std::vector<std::int32_t>> owns(n);
    for (std::size_t s = 0; s < n; ++s) {
        std::int64_t hi = -1;
        for (std::size_t v = 0; v < gg.N(); ++v) {
            if (owner[v] != static_cast<std::int8_t>(s)) continue;
            const std::int32_t d = g[s][v];
            if (d < (island.empty() ? 2000 : 1700) || d > (large ? 7000 : 5500)) continue;
            bool clear = true;
            for (std::size_t q = 0; q < n && clear; ++q) {
                if (q != s && g[q][v] >= 0 && g[q][v] * 100 < d * 125) clear = false;
            }
            if (clear) hi = std::max<std::int64_t>(hi, d), owns[s].push_back(d);
        }
        if (hi < 0) return -8;
    }
    if (!window(owns, 115, large ? 8 : 4)) return -9;
    std::vector<std::size_t> place(n);  // each start's place in the cycle
    ordered->clear();
    for (std::size_t k = 0; k < n; ++k) ordered->push_back(pts[order[k]]), place[order[k]] = k;
    pairs_out->clear();
    for (const auto& pr : kept)
        pairs_out->push_back({static_cast<std::int8_t>(place[pr[0]]), static_cast<std::int8_t>(place[pr[1]])});
    return std::min(geo, balance);
}

std::int64_t BodyValue(const Body& b, std::int64_t x, std::int64_t y) {
    std::int64_t best = -kDeep;
    for (std::size_t k = 0; k < b.line.size(); ++k) {
        const Pt a = b.line[k], c = k + 1 < b.line.size() ? b.line[k + 1] : a;
        if (k + 1 == b.line.size() && b.line.size() > 1) break;
        const std::int64_t vx = c.x - a.x, vy = c.y - a.y, vv = std::max<std::int64_t>(1, vx * vx + vy * vy);
        const std::int64_t px = x - a.x, py = y - a.y;
        const std::int64_t t = std::clamp<std::int64_t>((px * vx + py * vy) * 1024 / vv, 0, 1024);
        const std::int64_t dx = px - vx * t / 1024, dy = py - vy * t / 1024;
        const std::int64_t r = b.rad[k] + ((k + 1 < b.rad.size() ? b.rad[k + 1] : b.rad[k]) - b.rad[k]) * t / 1024;
        const std::int64_t d2 = dx * dx + dy * dy, lim = r - best;
        if (lim <= 0 || d2 >= lim * lim) continue;  // no better than what is known
        best = std::max(best, r - ISqrt(d2));
    }
    return best;
}

std::int64_t SeaDesignAt(const SeaDesign& d, std::int64_t x, std::int64_t y) {
    x = std::clamp<std::int64_t>(x, 0, std::int64_t{d.gw - 1} * kSeaStep);
    y = std::clamp<std::int64_t>(y, 0, std::int64_t{d.gh - 1} * kSeaStep);
    const auto i = static_cast<std::int32_t>(std::min<std::int64_t>(x / kSeaStep, d.gw - 2));
    const auto j = static_cast<std::int32_t>(std::min<std::int64_t>(y / kSeaStep, d.gh - 2));
    const std::int64_t fx = x - std::int64_t{i} * kSeaStep, fy = y - std::int64_t{j} * kSeaStep;
    auto at = [&](std::int32_t a, std::int32_t b) {
        return std::int64_t{
            d.value[static_cast<std::size_t>(b) * static_cast<std::size_t>(d.gw) + static_cast<std::size_t>(a)]};
    };
    const std::int64_t top = at(i, j) * (kSeaStep - fx) + at(i + 1, j) * fx;
    const std::int64_t bot = at(i, j + 1) * (kSeaStep - fx) + at(i + 1, j + 1) * fx;
    return FloorDiv(top * (kSeaStep - fy) + bot * fy, kSeaStep * kSeaStep);
}

namespace {

// SeaPlan for one band: the starts sea_near to sea_far px from the sea.
bool FitSea(const MapGenSettings& g, std::uint64_t key, std::int64_t sea_near, std::int64_t sea_far, SeaDesign* design,
            std::vector<Pt>* starts, std::vector<Pair>* neighbours) {
    Rng r{key};
    const bool large = g.size == 1;
    const int n = g.players;
    const std::int64_t wpx = large ? 300 * 96 : 200 * 96, hpx = large ? 600 * 24 : 400 * 24;
    const std::int64_t min_d = !large && n >= 7 ? 2800 : 2 * kZoneR + 400;
    const std::int64_t x_lo = kBandX + kZoneR + 48, x_hi = wpx - x_lo, y_lo = kBandY + kZoneR + 24, y_hi = hpx - y_lo;
    SeaGrid dg;
    dg.w = static_cast<std::int32_t>(wpx / kSeaStep) + 1, dg.h = static_cast<std::int32_t>(hpx / kSeaStep) + 1;
    dg.step = kSeaStep;
    SeaGrid gg;  // the land paths: every other node
    gg.w = (dg.w + 1) / 2, gg.h = (dg.h + 1) / 2, gg.step = kGeoStep;
    for (int draw = 0; draw < 24; ++draw) {
        Draft dr;
        std::int64_t share = 0;
        Draw(r, n, large, wpx, hpx, &dr, &share);
        // The share of water (per kind, 20 to 33 % of the nodes; the whole water blocks come out about 2.5 points
        // lower, over the check's 18 %). A drawn edge sea is scaled to it (every depth and width by one factor, so a
        // deep sea stays deep against a fjord; up to three rounds), then the level moves the main sea (8-connected)
        // the rest of the way, raised up to twice while lakes and cut off bays leave it short; an edge level more
        // than 450 px (Large 675) from the drawing would flatten the design: the next one.
        std::vector<std::int32_t> val(dg.N());
        std::vector<std::uint8_t> wet(dg.N()), sea;
        auto main_sea = [&](std::int32_t level) {
            for (std::size_t k = 0; k < dg.N(); ++k) wet[k] = val[k] > level ? 1 : 0;
            sea = Biggest(dg, wet, true);
            std::int64_t got = 0;
            for (std::uint8_t v : sea) got += v;
            return got * 1000 / static_cast<std::int64_t>(dg.N());
        };
        for (int round = 0; round < 4; ++round) {
            for (std::int32_t j = 0; j < dg.h; ++j) {
                for (std::int32_t i = 0; i < dg.w; ++i)
                    val[dg.I(i, j)] =
                        static_cast<std::int32_t>(DraftAt(dr, std::int64_t{i} * kSeaStep, std::int64_t{j} * kSeaStep));
            }
            const std::int64_t got = main_sea(0);
            if (!dr.edge || round == 3 || (got >= share - 12 && got <= share + 12)) break;
            const std::int64_t f = std::clamp<std::int64_t>(share * 1000 / std::max<std::int64_t>(got, 1), 700, 1400);
            for (Body& b : dr.bodies) {
                if (b.land) continue;
                for (std::int64_t& rad : b.rad) rad = rad * f / 1000;
            }
            for (Shore& sh : dr.shores) sh.depth = sh.depth * f / 1000;
        }
        std::vector<std::int32_t> sorted(val);
        std::sort(sorted.begin(), sorted.end());
        std::int64_t want = share;
        std::int32_t level = 0;
        for (int lv = 0; lv < 3; ++lv) {
            level = sorted[sorted.size() -
                           sorted.size() * static_cast<std::size_t>(std::min<std::int64_t>(want, 600)) / 1000];
            const std::int64_t got = main_sea(level);
            if (got + 10 >= share) break;
            want = std::min<std::int64_t>(want + share - got, 490);
        }
        if (dr.edge && std::abs(level) > (large ? 675 : 450)) continue;
        for (std::int32_t& v : val) v -= level;
        // The main land (4-connected, the rest of the water counted as land).
        std::vector<std::uint8_t> dry(dg.N());
        for (std::size_t k = 0; k < dg.N(); ++k) dry[k] = sea[k] == 0 ? 1 : 0;
        const std::vector<std::uint8_t> main_land = Biggest(dg, dry, false);
        const std::vector<std::int32_t> to_sea = Chamfer(dg, sea), to_land = Chamfer(dg, dry);
        std::vector<std::uint8_t> wide(dg.N());
        // The ships' open water (CoarseWater: a block of 5 x 6 coarse cells, 480 x 288 px, all water), about 320 px
        // from the land; the ships' sea is its biggest body (4-connected), and every start lies near it.
        for (std::size_t k = 0; k < dg.N(); ++k) wide[k] = sea[k] != 0 && to_land[k] >= 320 ? 1 : 0;
        const std::vector<std::int32_t> to_wide = Chamfer(dg, Biggest(dg, wide, false));
        std::vector<std::uint8_t> geo_land(gg.N(), 0);
        for (std::int32_t j = 0; j < gg.h; ++j) {
            for (std::int32_t i = 0; i < gg.w; ++i)
                geo_land[gg.I(i, j)] = main_land[dg.I(std::min(2 * i, dg.w - 1), std::min(2 * j, dg.h - 1))];
        }
        // The candidates: nodes of the land path grid on the main land, inside the frame, sea_near to sea_far px
        // from the sea (the band's, SeaBandOf) and within 400 more of the ships' wide water (ports and a coast for
        // everyone); at most about 320 of them (400 with 7 or 8 players; every k-th by a hash), for the land path
        // matrix below.
        const std::size_t most_cand = n >= 7 ? 400 : 320;
        std::vector<std::size_t> cand;  // land path grid nodes
        for (std::int32_t j = 0; j < gg.h; ++j) {
            for (std::int32_t i = 0; i < gg.w; ++i) {
                const std::size_t k = dg.I(std::min(2 * i, dg.w - 1), std::min(2 * j, dg.h - 1));
                const std::int64_t x = std::int64_t{i} * kGeoStep, y = std::int64_t{j} * kGeoStep;
                if (geo_land[gg.I(i, j)] == 0 || x < x_lo || x > x_hi || y < y_lo || y > y_hi) continue;
                if (to_sea[k] < sea_near || to_sea[k] > sea_far || to_wide[k] > sea_far + 400) continue;
                cand.push_back(gg.I(i, j));
            }
        }
        if (cand.size() > most_cand) {
            const std::uint32_t thin = r.next(), keep = static_cast<std::uint32_t>(cand.size() / most_cand + 1);
            std::vector<std::size_t> some;
            for (std::size_t c : cand) {
                if (Hash(thin, static_cast<std::int64_t>(c), 3) % keep == 0) some.push_back(c);
            }
            cand = std::move(some);
        }
        const std::size_t nc = cand.size();
        if (nc < static_cast<std::size_t>(n)) continue;
        auto pt = [&](std::size_t c) {
            return Pt{static_cast<std::int64_t>(cand[c] % static_cast<std::size_t>(gg.w)) * kGeoStep,
                      static_cast<std::int64_t>(cand[c] / static_cast<std::size_t>(gg.w)) * kGeoStep};
        };
        // The land path between every two candidates (-1: none) and their straight distance.
        // (The land paths only once the straight lines leave room for n starts: until then `geo` is `line`.)
        std::vector<std::int32_t> line(nc * nc);
        for (std::size_t a = 0; a < nc; ++a) {
            for (std::size_t b = 0; b < nc; ++b)
                line[a * nc + b] = static_cast<std::int32_t>(ISqrt(Dist2(pt(a), pt(b))));
        }
        std::vector<std::int32_t> geo(line);
        bool paths = false;
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
            if (t == 3 && !paths) break;  // three tries without room in straight lines: the next design
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
            if (sc < 0) continue;
            if (!paths) {  // room in straight lines: now by land, and this set climbs on
                paths = true;
                for (std::size_t a = 0; a < nc; ++a) {
                    const std::vector<std::int32_t> d = Paths(gg, geo_land, cand[a], kGeoStep, kGeoStep);
                    for (std::size_t b = 0; b < nc; ++b) geo[a * nc + b] = d[cand[b]];
                }
                --t;
                continue;
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
        design->gw = dg.w, design->gh = dg.h;
        design->value = std::move(val);
        design->words = dr.words;
        return true;
    }
    return false;
}

}  // namespace

int SeaBandDrawn(const MapGenSettings& g) {
    if (g.size == 0 && g.players >= 7) return 0;
    return static_cast<int>(Hash(g.seed ^ 0x5eab0d57u, g.players, g.size) % 3);
}

std::array<std::int64_t, 2> SeaBandOf(const MapGenSettings& g, int band) {
    if (g.size == 0 && g.players >= 7) return {kSeaNear, kSeaFar + 600};
    return kSeaBands[static_cast<std::size_t>(std::clamp(band, 0, 2))];
}

bool SeaPlan(const MapGenSettings& g, std::uint64_t key, SeaDesign* design, std::vector<Pt>* starts,
             std::vector<Pair>* neighbours, std::uint8_t* band, std::string* why) {
    for (int b = SeaBandDrawn(g); b >= 0; --b) {
        const std::array<std::int64_t, 2> near_far = SeaBandOf(g, b);
        if (FitSea(g, key, near_far[0], near_far[1], design, starts, neighbours)) {
            *band = static_cast<std::uint8_t>(b);
            return true;
        }
    }
    *why = "positions: no fair starts on 24 sea designs";
    return false;
}

// ---- Islands (decision 53; water-first since 2026-11-08). ----

namespace {

// The islands' places: per island a centre, a direction for its spine and a name for the layout.
struct IslandSlot {
    Pt c;
    Pt u;
};

// The radius a body of island i may have at point q: `half` px short of the midline between island i's centre and
// every other island's (half the channel: 400 plus half of what the coast noise and the warp can move the coasts),
// and never over rad_max.
std::int64_t IslandRadiusCap(const std::vector<Pt>& centres, const std::vector<std::int64_t>& rad_max, int i, Pt q,
                             std::int64_t half) {
    std::int64_t cap = rad_max[static_cast<std::size_t>(i)];
    const Pt ci = centres[static_cast<std::size_t>(i)];
    for (std::size_t j = 0; j < centres.size(); ++j) {
        if (static_cast<int>(j) == i) continue;
        const std::int64_t between = std::max<std::int64_t>(1, ISqrt(Dist2(ci, centres[j])));
        const std::int64_t to_mid = (Dist2(q, centres[j]) - Dist2(q, ci)) / (2 * between);
        cap = std::min(cap, to_mid - half);
    }
    return std::max<std::int64_t>(cap, 300);
}

// One islands design: the layout drawn among those the sizes allow (an island of 3 or more players needs a long
// side to stretch along: no column for it on a Normal map), the slots given to the islands in a drawn order, each
// island a spine of points (its players plus one; a single player an egg of two) with radii of its own, 1 to 3
// lobes, sometimes a peninsula, 0 to 2 bays cut in. `share`: the water's share per mille.
void DrawIslands(Rng& r, bool large, const std::vector<int>& sizes, std::int64_t wpx, std::int64_t hpx, Draft* d,
                 std::int64_t* share, std::vector<Pt>* centres, std::vector<int>* body_isl) {
    auto S = [&](std::int64_t v) { return large ? v * 3 / 2 : v; };
    const int k = static_cast<int>(sizes.size());
    int n = 0, biggest = 0;
    for (int v : sizes) n += v, biggest = std::max(biggest, v);
    const bool crowded = !large && n >= 6;
    const bool tight = !large && biggest >= 3;  // a Normal island of 3 or more: little room beside its starts
    d->ocean = true;
    d->wpx = wpx, d->hpx = hpx;
    d->wk = r.next(), d->k1 = r.next(), d->k2 = r.next();
    d->ws = S(r.range(5000, 8500));
    d->wa = d->ws * r.range(4, 7) / 100;  // (a strong warp would join two islands)
    d->s1 = S(r.range(1800, 3200));
    d->a1 = crowded ? r.range(200, 350) : r.range(250, 500);
    d->s2 = r.range(650, 1100);
    d->a2 = r.range(70, 160);
    // The water's share: two islands 50 to 60 % (a crowded Normal map 45 to 52), three or four 54 to 62 (more
    // channels). The zones' keeps add land round the starts later, and the water step wants 34.5 % of the lattice
    // left (the check allows up to 60).
    *share = crowded ? r.range(450, 520) : k == 2 ? r.range(500, 600) : r.range(540, 620);
    // The layouts: the slots as fractions of the map (per mille), the spine's direction (x, y per mille), and the
    // most players an island may hold there (0: any; a column on a Normal map holds 2, on Large 3; the islands
    // across a diagonal channel 2, since a long island shifted along the other would face it unevenly), and the
    // axis of a row (1 along x, 2 along y, 0 none): in a row the islands take their room along the axis by their
    // players' shares (a 3+1 split: three quarters and a quarter); the other layouts want equal islands.
    struct Layout {
        const char* name;
        std::vector<std::array<std::int64_t, 4>> slots;  // x, y, ux, uy
        int column_max;
        int axis;
    };
    std::vector<Layout> layouts;
    const int col_max = large ? 3 : 2;
    if (k == 2) {
        layouts.push_back({"two islands, north and south", {{500, 260, 1000, 0}, {500, 740, 1000, 0}}, 0, 2});
        layouts.push_back({"two islands, west and east", {{250, 500, 0, 1000}, {750, 500, 0, 1000}}, large ? 4 : 3, 1});
        layouts.push_back({"two islands across a diagonal channel", {{300, 300, 1000, 0}, {700, 700, 1000, 0}}, 2, 0});
        layouts.push_back({"two islands across a diagonal channel", {{700, 300, 1000, 0}, {300, 700, 1000, 0}}, 2, 0});
    } else if (k == 3) {
        layouts.push_back(
            {"three islands in a row", {{170, 500, 0, 1000}, {500, 500, 0, 1000}, {830, 500, 0, 1000}}, col_max, 1});
        layouts.push_back({"three islands, two north, one south",
                           {{270, 270, 1000, 0}, {730, 270, 1000, 0}, {500, 740, 1000, 0}},
                           large ? 4 : 3,
                           0});
        layouts.push_back({"three islands, one north, two south",
                           {{500, 260, 1000, 0}, {270, 730, 1000, 0}, {730, 730, 1000, 0}},
                           large ? 4 : 3,
                           0});
    } else {
        layouts.push_back({"four islands in the corners",
                           {{260, 270, 1000, 0}, {740, 270, 1000, 0}, {740, 730, 1000, 0}, {260, 730, 1000, 0}},
                           large ? 3 : 2,
                           0});
        layouts.push_back({"four islands in a diamond",
                           {{500, 230, 1000, 0}, {800, 500, 0, 1000}, {500, 770, 1000, 0}, {200, 500, 0, 1000}},
                           large ? 3 : 2,
                           0});
        if (large)
            layouts.push_back({"four islands in a row",
                               {{125, 500, 0, 1000}, {375, 500, 0, 1000}, {625, 500, 0, 1000}, {875, 500, 0, 1000}},
                               2,
                               1});
    }
    bool equal = true;
    for (int v : sizes) equal = equal && v == sizes[0];
    std::vector<std::size_t> fit;
    for (std::size_t l = 0; l < layouts.size(); ++l) {
        if ((layouts[l].column_max == 0 || biggest <= layouts[l].column_max) && (equal || layouts[l].axis != 0))
            fit.push_back(l);
    }
    if (fit.empty()) fit.push_back(0);
    const Layout& lay = layouts[fit[r.below(static_cast<std::uint32_t>(fit.size()))]];
    // The islands take the slots in a drawn order (the largest is not always first).
    std::vector<int> slot(static_cast<std::size_t>(k));
    for (int i = 0; i < k; ++i) slot[static_cast<std::size_t>(i)] = i;
    for (int i = k - 1; i > 0; --i)
        std::swap(slot[static_cast<std::size_t>(i)], slot[r.below(static_cast<std::uint32_t>(i + 1))]);
    const bool flip_x = r.below(2) != 0, flip_y = r.below(2) != 0;
    int lobes = 0, peninsulas = 0, bays = 0;
    centres->clear(), body_isl->clear();
    auto add = [&](const Body& b, int i) { d->bodies.push_back(b), body_isl->push_back(i); };
    // In a row: the slots in axis order, each island's place along the axis the middle of its players' share.
    std::vector<std::int64_t> along(static_cast<std::size_t>(k), 0);
    if (lay.axis != 0) {
        std::vector<int> order(static_cast<std::size_t>(k));  // islands by their slot
        for (int i = 0; i < k; ++i) order[static_cast<std::size_t>(slot[static_cast<std::size_t>(i)])] = i;
        std::int64_t before = 0;
        for (int q = 0; q < k; ++q) {
            const int i = order[static_cast<std::size_t>(q)];
            const std::int64_t mid = (2 * before + sizes[static_cast<std::size_t>(i)]) * 1000 / (2 * n);
            // (A tight Normal island reaches the map's edge: its centre nearer the edge.)
            along[static_cast<std::size_t>(i)] = 500 + (mid - 500) * (crowded ? 1240 : tight ? 1150 : 1000) / 1000;
            before += sizes[static_cast<std::size_t>(i)];
        }
    }
    for (int i = 0; i < k; ++i) {
        const auto& sl = lay.slots[static_cast<std::size_t>(slot[static_cast<std::size_t>(i)])];
        // (A tight Normal island keeps its place across the row: its starts' strip is only a few hundred px.)
        const std::int64_t jx = tight && lay.axis == 1 ? 0 : r.range(-60, 60);
        const std::int64_t jy = tight && lay.axis == 2 ? 0 : r.range(-40, 40);
        const std::int64_t px = lay.axis == 1 ? along[static_cast<std::size_t>(i)] : sl[0];
        const std::int64_t py = lay.axis == 2 ? along[static_cast<std::size_t>(i)] : sl[1];
        Pt c{wpx * (px + jx) / 1000, hpx * (py + jy) / 1000};
        if (flip_x) c.x = wpx - c.x;
        if (flip_y) c.y = hpx - c.y;
        c.x = std::clamp<std::int64_t>(c.x, 2300, wpx - 2300), c.y = std::clamp<std::int64_t>(c.y, 1800, hpx - 1800);
        centres->push_back(c);
    }
    // Every point of an island stays on its own side: nearer its own centre than any other island's by `by` px.
    auto own_side = [&](Pt p, int i, std::int64_t by) {
        const std::int64_t mine_d = ISqrt(Dist2(p, (*centres)[static_cast<std::size_t>(i)]));
        for (int j = 0; j < k; ++j) {
            if (j != i && ISqrt(Dist2(p, (*centres)[static_cast<std::size_t>(j)])) < mine_d + by) return false;
        }
        return true;
    };
    // Per island the most its radius may be (half the way to the nearest island less 500 px), and at a point of
    // island i the radius that keeps 600 px from every other island's largest circle (IslandRadiusCap).
    std::vector<std::int64_t> rad_max(static_cast<std::size_t>(k));
    for (int i = 0; i < k; ++i) {
        std::int64_t nearest = kInf;
        for (int j = 0; j < k; ++j) {
            if (j != i)
                nearest = std::min(nearest, ISqrt(Dist2((*centres)[static_cast<std::size_t>(i)],
                                                        (*centres)[static_cast<std::size_t>(j)])));
        }
        rad_max[static_cast<std::size_t>(i)] = nearest / 2 - 400;
    }
    const std::int64_t margin = (crowded ? 250 : 400) + (d->a1 + d->a2 + d->wa) / 2;  // half the channel
    for (int i = 0; i < k; ++i) {
        const int p = sizes[static_cast<std::size_t>(i)];
        const auto& sl = lay.slots[static_cast<std::size_t>(slot[static_cast<std::size_t>(i)])];
        const Pt c = (*centres)[static_cast<std::size_t>(i)];
        const std::int64_t nearest = rad_max[static_cast<std::size_t>(i)] * 2 + 1000;
        // The spine: turned up to 25 degrees off the slot's direction (12 with 3 players or more: a long island
        // askew would face the other one unevenly), half as long as the players need (more on a crowded map: its
        // starts stand along it), its points inside the map; its radius at most what leaves the channel to the
        // nearest island.
        const std::int64_t turn = p >= 3 ? r.range(-210, 210) : r.range(-450, 450);
        // In a row the spine runs along the island's longer extent (its share of the row against the row's width).
        std::int64_t ux = sl[2], uy = sl[3];
        if (lay.axis == 1 && wpx * p / n > hpx) ux = 1000, uy = 0;
        if (lay.axis == 2 && hpx * p / n > wpx) ux = 0, uy = 1000;
        Pt u = Unit((flip_x ? -ux : ux) * 1024 - uy * turn, (flip_y ? -uy : uy) * 1024 + ux * turn);
        const std::int64_t L = std::max<std::int64_t>(900, (p - 1) * (crowded ? 1900 : 1650) + 900);
        // No radius beyond half the way to the nearest island less 500 px (the channel), at any point.
        const std::int64_t rad0 = std::min(large     ? r.range(2400, 3000)
                                           : crowded ? r.range(1800, 2200)
                                                     : r.range(2000, 2600),
                                           nearest / 2 - 500);
        const std::int64_t rad_lo = tight ? 90 : 70, rad_hi = tight ? 115 : 130;
        const int m = p <= 1 ? 2 : p + 1;
        Body spine;
        spine.land = true;
        for (int j = 0; j < m; ++j) {
            const std::int64_t t = -L + 2 * L * j / (m - 1);
            const std::int64_t jt = r.range(-L / (m - 1) * 3 / 10, L / (m - 1) * 3 / 10);
            const std::int64_t aside = r.range(-L / 5, L / 5);
            Pt q = Toward(Toward(c, u, t + jt), Perp(u), aside);
            for (int pull = 0; pull < 6 && !own_side(q, i, 800); ++pull)       // a point too near another island
                q = Pt{c.x + (q.x - c.x) * 3 / 4, c.y + (q.y - c.y) * 3 / 4};  // moves toward its own centre
            q.x = std::clamp<std::int64_t>(q.x, 1200, wpx - 1200), q.y = std::clamp<std::int64_t>(q.y, 900, hpx - 900);
            spine.line.push_back(q);
            spine.rad.push_back(
                std::min(rad0 * r.range(rad_lo, rad_hi) / 100, IslandRadiusCap(*centres, rad_max, i, q, margin)));
        }
        add(spine, i);
        // Lobes, a peninsula, bays: off a drawn point of the spine in a drawn direction, on the island's own side
        // (up to 6 draws, else left out).
        auto off = [&](std::int64_t dist_lo, std::int64_t dist_hi, std::int64_t by, Pt* from, Pt* dir) {
            for (int tries = 0; tries < 6; ++tries) {
                const std::size_t j = r.below(static_cast<std::uint32_t>(spine.line.size()));
                const std::int64_t dx = r.range(-1024, 1024);
                const std::int64_t dy = r.range(-1024, 1024);
                *dir = Unit(dx == 0 && dy == 0 ? 1 : dx, dy);
                const std::int64_t dist = spine.rad[j] * r.range(dist_lo, dist_hi) / 100;
                *from = Toward(spine.line[j], *dir, dist);
                if (own_side(*from, i, by)) return true;
            }
            return false;
        };
        const int lb = static_cast<int>(r.range(large ? 2 : tight ? 0 : 1, tight ? 1 : 3));
        for (int q = 0; q < lb; ++q) {
            Pt from, dir;
            if (!off(85, 130, 1500, &from, &dir)) continue;
            Body lobe;
            lobe.land = true;
            lobe.line = {from};
            lobe.rad = {std::min(rad0 * r.range(35, 60) / 100, IslandRadiusCap(*centres, rad_max, i, from, margin))};
            add(lobe, i);
            ++lobes;
        }
        if (!tight && r.below(100) < 35) {
            Pt from, dir;
            const std::int64_t len = r.range(1500, 3000);
            if (off(60, 90, len + 1200, &from, &dir) && own_side(Toward(from, dir, len), i, 1200)) {
                const std::int64_t w0 = rad0 * r.range(25, 35) / 100;
                const std::int64_t w1 = rad0 * r.range(12, 18) / 100;
                Body arm = Arm(r, from, dir, len, w0, w1);
                arm.land = true;
                for (std::size_t j = 0; j < arm.rad.size(); ++j)
                    arm.rad[j] = std::min(arm.rad[j], IslandRadiusCap(*centres, rad_max, i, arm.line[j], margin));
                add(arm, i);
                ++peninsulas;
            }
        }
        const int bq = static_cast<int>(r.range(0, tight ? 1 : 2));
        for (int q = 0; q < bq; ++q) {
            Pt from, dir;
            if (!off(95, 115, 0, &from, &dir)) continue;
            Body bay;
            bay.cut = true;
            bay.line = {from}, bay.rad = {rad0 * r.range(30, 50) / 100};
            add(bay, i);
            ++bays;
        }
    }
    char buf[200];
    std::string plan = std::to_string(k) + " islands (";
    for (int i = 0; i < k; ++i) plan += (i ? "+" : "") + std::to_string(sizes[static_cast<std::size_t>(i)]);
    plan += ")";
    std::snprintf(buf, sizeof buf, "%s, %s, %d lobes, %d peninsulas, %d bays", plan.c_str(), lay.name, lobes,
                  peninsulas, bays);
    d->words = buf;
}

// Labels the 4-connected components of the nodes with on != 0 (0, 1, 2, ...; -1 elsewhere); the count.
std::int32_t Components(const SeaGrid& g, const std::vector<std::uint8_t>& on, std::vector<std::int32_t>* comp) {
    comp->assign(g.N(), -1);
    std::int32_t next = 0;
    std::vector<std::int32_t> todo;
    for (std::size_t k0 = 0; k0 < g.N(); ++k0) {
        if (on[k0] == 0 || (*comp)[k0] >= 0) continue;
        todo.assign(1, static_cast<std::int32_t>(k0));
        (*comp)[k0] = next;
        for (std::size_t t = 0; t < todo.size(); ++t) {
            const std::int32_t i0 = todo[t] % g.w, j0 = todo[t] / g.w;
            static constexpr std::array<std::array<std::int32_t, 2>, 4> kNext = {{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
            for (const auto& nx : kNext) {
                const std::int32_t i = i0 + nx[0], j = j0 + nx[1];
                if (i < 0 || j < 0 || i >= g.w || j >= g.h) continue;
                const std::size_t k = g.I(i, j);
                if (on[k] == 0 || (*comp)[k] >= 0) continue;
                (*comp)[k] = next;
                todo.push_back(static_cast<std::int32_t>(k));
            }
        }
        ++next;
    }
    return next;
}

}  // namespace

bool IslandsPlan(const MapGenSettings& g, std::uint64_t key, const std::vector<int>& sizes, SeaDesign* design,
                 std::vector<Pt>* starts, std::vector<std::int8_t>* island, std::vector<Pair>* neighbours,
                 std::string* why) {
    Rng r{key};
    const bool large = g.size == 1;
    const int n = g.players, k = static_cast<int>(sizes.size());
    const bool crowded = !large && n >= 6;
    const std::int64_t min_d = !large && n >= 7 ? 2800 : 2 * kZoneR + 400;
    const std::int64_t wpx = large ? 300 * 96 : 200 * 96, hpx = large ? 600 * 24 : 400 * 24;
    const std::int64_t x_lo = kBandX + kZoneR + 48, x_hi = wpx - x_lo, y_lo = kBandY + kZoneR + 24, y_hi = hpx - y_lo;
    // The starts stand 1,900 to 2,400 px from the drawn sea on a Large map (the keeps round the zones reach 2,200
    // and would eat the water), 1,700 on Normal; a crowded Normal map keeps 100 px of margin round a zone instead of
    // 300 (decision 53: a slightly smaller start area for everyone), so there 1,300.
    const std::int64_t sea_near = crowded ? 1300 : !large && sizes[0] >= 3 ? 1500 : !large ? 1700 : 1900;
    const std::int64_t sea_far = crowded ? sea_near + 400 : 2400;  // (crowded: an even row, even port distances)
    SeaGrid dg;
    dg.w = static_cast<std::int32_t>(wpx / kSeaStep) + 1, dg.h = static_cast<std::int32_t>(hpx / kSeaStep) + 1;
    dg.step = kSeaStep;
    SeaGrid gg;
    gg.w = (dg.w + 1) / 2, gg.h = (dg.h + 1) / 2, gg.step = kGeoStep;
    std::vector<std::int8_t> isl_of;  // per start (island by island) its island
    for (int i = 0; i < k; ++i) {
        for (int q = 0; q < sizes[static_cast<std::size_t>(i)]; ++q) isl_of.push_back(static_cast<std::int8_t>(i));
    }
    for (int draw = 0; draw < 24; ++draw) {
        Draft dr;
        std::int64_t share = 0;
        std::vector<Pt> centres;
        std::vector<int> body_isl;
        DrawIslands(r, large, sizes, wpx, hpx, &dr, &share, &centres, &body_isl);
        // Each island's land scaled (its bodies stretched about its centre, their radii by one factor per round, up
        // to five rounds) until the land per player is equal over the islands within 8 % (15 at the last round) and
        // the water takes 43 to 60 % (the zones' keeps add land later, the check wants 34.5 to 60), its land being the
        // 4-connected land holding its centre (or the nearest land to it); two islands in one land drop the design.
        std::vector<std::int32_t> val(dg.N());
        std::vector<std::uint8_t> land(dg.N());
        std::vector<std::int32_t> comp;
        std::vector<std::int32_t> mine(static_cast<std::size_t>(k), -1);  // per island its component
        std::vector<std::int64_t> got(static_cast<std::size_t>(k), 0), target(static_cast<std::size_t>(k), 0);
        const std::int64_t per_player = static_cast<std::int64_t>(dg.N()) * (1000 - share) / 1000 / n;
        for (int i = 0; i < k; ++i)
            target[static_cast<std::size_t>(i)] = per_player * sizes[static_cast<std::size_t>(i)];
        bool ok = false;
        for (int round = 0; round < 5 && !ok; ++round) {
            for (std::int32_t j = 0; j < dg.h; ++j) {
                for (std::int32_t i = 0; i < dg.w; ++i) {
                    const std::size_t q = dg.I(i, j);
                    val[q] =
                        static_cast<std::int32_t>(DraftAt(dr, std::int64_t{i} * kSeaStep, std::int64_t{j} * kSeaStep));
                    land[q] = val[q] <= 0 ? 1 : 0;
                }
            }
            Components(dg, land, &comp);
            std::fill(got.begin(), got.end(), 0);
            bool merged = false;
            for (int i = 0; i < k && !merged; ++i) {
                const auto ui = static_cast<std::size_t>(i);
                // The land nearest the centre (within 1,500 px).
                std::int64_t best = -1;
                const Pt c = centres[ui];
                const std::int32_t ci = static_cast<std::int32_t>(c.x / kSeaStep),
                                   cj = static_cast<std::int32_t>(c.y / kSeaStep);
                mine[ui] = -1;
                for (std::int32_t j = std::max(0, cj - 12); j <= std::min(dg.h - 1, cj + 12); ++j) {
                    for (std::int32_t ii = std::max(0, ci - 12); ii <= std::min(dg.w - 1, ci + 12); ++ii) {
                        const std::size_t q = dg.I(ii, j);
                        if (land[q] == 0) continue;
                        const std::int64_t d2 = Dist2(Pt{ii * kSeaStep, j * kSeaStep}, c);
                        if (best < 0 || d2 < best) best = d2, mine[ui] = comp[q];
                    }
                }
                if (mine[ui] < 0) merged = true;  // no land at the centre: as good as lost
                for (int j = 0; j < i && !merged; ++j) merged = mine[static_cast<std::size_t>(j)] == mine[ui];
            }
            if (merged) break;
            for (std::size_t q = 0; q < dg.N(); ++q) {
                for (int i = 0; i < k; ++i) {
                    if (comp[q] == mine[static_cast<std::size_t>(i)]) ++got[static_cast<std::size_t>(i)];
                }
            }
            std::int64_t land_all = 0;
            for (std::int64_t v : got) land_all += v;
            const std::int64_t mean_pp = std::max<std::int64_t>(1, land_all / n);  // land per player
            const std::int64_t water = 1000 - land_all * 1000 / static_cast<std::int64_t>(dg.N());
            ok = water >= (round == 4 ? 400 : 430) && water <= (round == 4 ? 620 : 600);
            for (int i = 0; i < k; ++i) {
                const auto ui = static_cast<std::size_t>(i);
                const std::int64_t pp = got[ui] / sizes[ui];
                if (std::abs(pp - mean_pp) * 1000 / mean_pp > (round == 4 ? 150 : 80)) ok = false;
            }
            if (ok || round == 4) break;
            // The whole toward the drawn share when the water is out of its range, each island toward the mean.
            const std::int64_t land_want = static_cast<std::int64_t>(dg.N()) * (1000 - share) / 1000;
            const std::int64_t g_all =
                water < 430 || water > 600 ? ISqrt(land_want * 1000000 / std::max<std::int64_t>(1, land_all)) : 1000;
            for (int i = 0; i < k; ++i)
                target[static_cast<std::size_t>(i)] = mean_pp * sizes[static_cast<std::size_t>(i)] * g_all / 1000;
            // The factor per island (areas grow with the square of the radius): its land bodies stretch about its
            // centre and their radii grow, but no radius beyond half the way to the nearest island less 500 px (the
            // channel), and no point off the map or onto another island's side; bays keep their size.
            std::vector<std::int64_t> rad_max(static_cast<std::size_t>(k));
            for (int i = 0; i < k; ++i) {
                std::int64_t nearest = kInf;
                for (int j = 0; j < k; ++j) {
                    if (j != i)
                        nearest = std::min(nearest, ISqrt(Dist2(centres[static_cast<std::size_t>(i)],
                                                                centres[static_cast<std::size_t>(j)])));
                }
                rad_max[static_cast<std::size_t>(i)] = nearest / 2 - 400;
            }
            const std::int64_t margin = (crowded ? 250 : 400) + (dr.a1 + dr.a2 + dr.wa) / 2;
            for (int i = 0; i < k; ++i) {
                const auto ui = static_cast<std::size_t>(i);
                const std::int64_t f = std::clamp<std::int64_t>(
                    ISqrt(target[ui] * 1000000 / std::max<std::int64_t>(1, got[ui])), 850, 1200);
                const Pt c = centres[ui];
                auto own_side = [&](Pt q) {
                    const std::int64_t mine_d = ISqrt(Dist2(q, c));
                    for (int j = 0; j < k; ++j) {
                        if (j != i && ISqrt(Dist2(q, centres[static_cast<std::size_t>(j)])) < mine_d + 800)
                            return false;
                    }
                    return true;
                };
                for (std::size_t b = 0; b < dr.bodies.size(); ++b) {
                    Body& body = dr.bodies[b];
                    if (body.cut || body_isl[b] != i) continue;
                    for (std::size_t j = 0; j < body.line.size(); ++j) {
                        Pt& q = body.line[j];
                        q = Pt{c.x + (q.x - c.x) * f / 1000, c.y + (q.y - c.y) * f / 1000};
                        for (int pull = 0; pull < 6 && !own_side(q); ++pull)
                            q = Pt{c.x + (q.x - c.x) * 3 / 4, c.y + (q.y - c.y) * 3 / 4};
                        q.x = std::clamp<std::int64_t>(q.x, 1200, wpx - 1200);
                        q.y = std::clamp<std::int64_t>(q.y, 900, hpx - 900);
                        if (j < body.rad.size())
                            body.rad[j] =
                                std::min(body.rad[j] * f / 1000, IslandRadiusCap(centres, rad_max, i, q, margin));
                    }
                }
            }
        }
        if (!ok) continue;
        // The water: the main sea (8-connected) with the rest of the water; the ships' open water 320 px from land
        // and its biggest body, which must hold water near every island.
        std::vector<std::uint8_t> wet(dg.N()), dry(dg.N());
        for (std::size_t q = 0; q < dg.N(); ++q) wet[q] = land[q] == 0 ? 1 : 0, dry[q] = land[q];
        const std::vector<std::uint8_t> sea = Biggest(dg, wet, true);
        const std::vector<std::int32_t> to_sea = Chamfer(dg, sea), to_land = Chamfer(dg, dry);
        std::vector<std::uint8_t> wide(dg.N());
        for (std::size_t q = 0; q < dg.N(); ++q) wide[q] = sea[q] != 0 && to_land[q] >= 320 ? 1 : 0;
        const std::vector<std::int32_t> to_wide = Chamfer(dg, Biggest(dg, wide, false));
        // Two islands whose land comes within 640 px of each other leave the ships no way between: the next design.
        std::vector<std::uint8_t> close(dg.N());
        for (std::size_t q = 0; q < dg.N(); ++q) close[q] = to_land[q] < 320 ? 1 : 0;
        std::vector<std::int32_t> close_comp;
        Components(dg, close, &close_comp);
        {
            bool touch = false;
            std::vector<std::int32_t> cc(static_cast<std::size_t>(k), -1);
            for (std::size_t q = 0; q < dg.N() && !touch; ++q) {
                for (int i = 0; i < k; ++i) {
                    if (comp[q] != mine[static_cast<std::size_t>(i)]) continue;
                    std::int32_t& v = cc[static_cast<std::size_t>(i)];
                    if (v < 0) v = close_comp[q];
                }
            }
            for (int i = 0; i < k && !touch; ++i) {
                for (int j = 0; j < i; ++j)
                    touch = touch || cc[static_cast<std::size_t>(i)] == cc[static_cast<std::size_t>(j)];
            }
            if (touch) continue;
        }
        std::vector<std::uint8_t> geo_land(gg.N(), 0);
        std::vector<std::int32_t> geo_comp(gg.N(), -1);
        for (std::int32_t j = 0; j < gg.h; ++j) {
            for (std::int32_t i = 0; i < gg.w; ++i) {
                const std::size_t q = dg.I(std::min(2 * i, dg.w - 1), std::min(2 * j, dg.h - 1));
                geo_land[gg.I(i, j)] = land[q], geo_comp[gg.I(i, j)] = comp[q];
            }
        }
        // The candidates per island: its land, inside the frame, sea_near to sea_far from the sea, within 3,000 px
        // of the ships' water; about 320 in all.
        std::vector<std::size_t> cand;
        std::vector<std::int8_t> cand_isl;
        for (std::int32_t j = 0; j < gg.h; ++j) {
            for (std::int32_t i = 0; i < gg.w; ++i) {
                const std::size_t q = dg.I(std::min(2 * i, dg.w - 1), std::min(2 * j, dg.h - 1));
                const std::int64_t x = std::int64_t{i} * kGeoStep, y = std::int64_t{j} * kGeoStep;
                if (geo_land[gg.I(i, j)] == 0 || x < x_lo || x > x_hi || y < y_lo || y > y_hi) continue;
                if (to_sea[q] < sea_near || to_sea[q] > sea_far || to_wide[q] > 3000) continue;
                for (int w = 0; w < k; ++w) {
                    if (geo_comp[gg.I(i, j)] == mine[static_cast<std::size_t>(w)]) {
                        cand.push_back(gg.I(i, j)), cand_isl.push_back(static_cast<std::int8_t>(w));
                    }
                }
            }
        }
        if (cand.size() > 320) {
            const std::uint32_t thin = r.next(), keep = static_cast<std::uint32_t>(cand.size() / 320 + 1);
            std::vector<std::size_t> some;
            std::vector<std::int8_t> some_isl;
            for (std::size_t c = 0; c < cand.size(); ++c) {
                if (Hash(thin, static_cast<std::int64_t>(cand[c]), 3) % keep == 0)
                    some.push_back(cand[c]), some_isl.push_back(cand_isl[c]);
            }
            cand = std::move(some), cand_isl = std::move(some_isl);
        }
        const std::size_t nc = cand.size();
        std::vector<std::vector<std::size_t>> of(static_cast<std::size_t>(k));  // per island its candidates
        for (std::size_t c = 0; c < nc; ++c) of[static_cast<std::size_t>(cand_isl[c])].push_back(c);
        bool room = true;
        for (int i = 0; i < k; ++i)
            room = room && of[static_cast<std::size_t>(i)].size() >=
                               static_cast<std::size_t>(sizes[static_cast<std::size_t>(i)]);
        if (!room) continue;
        auto pt = [&](std::size_t c) {
            return Pt{static_cast<std::int64_t>(cand[c] % static_cast<std::size_t>(gg.w)) * kGeoStep,
                      static_cast<std::int64_t>(cand[c] / static_cast<std::size_t>(gg.w)) * kGeoStep};
        };
        std::vector<std::int32_t> line(nc * nc);
        for (std::size_t a = 0; a < nc; ++a) {
            for (std::size_t b = 0; b < nc; ++b)
                line[a * nc + b] = static_cast<std::int32_t>(ISqrt(Dist2(pt(a), pt(b))));
        }
        std::vector<std::int32_t> geo(line);
        bool paths = false;
        // The spacing per mille: the least of the nearest island mate by land (smallest over largest, per island
        // size) and the nearest start of another island in a straight line (the same); minus what the too close
        // pairs lack below min_d. `worst`: the start to move.
        std::size_t worst = 0;
        auto spacing = [&](const std::vector<std::size_t>& set) {
            std::array<std::int64_t, 9> e_lo{}, e_hi{};
            e_lo.fill(kInf);
            std::int64_t a_lo = kInf, a_hi = 0, lack = 0, l_min = kInf;
            for (std::size_t s = 0; s < set.size(); ++s) {
                std::int64_t e = kInf, a = kInf, l = kInf;
                for (std::size_t t = 0; t < set.size(); ++t) {
                    if (t == s) continue;
                    const std::int64_t straight = line[set[s] * nc + set[t]];
                    if (straight < min_d) lack += min_d - straight;
                    l = std::min(l, straight);
                    if (isl_of[t] != isl_of[s]) {
                        a = std::min(a, straight);
                        continue;
                    }
                    const std::int64_t v = geo[set[s] * nc + set[t]];
                    if (v < 0) return std::int64_t{-100000};
                    e = std::min(e, v);
                }
                if (l < l_min) l_min = l, worst = s;
                a_lo = std::min(a_lo, a), a_hi = std::max(a_hi, a);
                const auto sz = static_cast<std::size_t>(sizes[static_cast<std::size_t>(isl_of[s])]);
                if (sz >= 2) e_lo[sz] = std::min(e_lo[sz], e), e_hi[sz] = std::max(e_hi[sz], e);
            }
            if (lack > 0) return -lack;
            std::int64_t score = a_lo * 1000 / std::max<std::int64_t>(1, a_hi);
            for (std::size_t sz = 2; sz < e_lo.size(); ++sz) {
                if (e_hi[sz] > 0) score = std::min(score, e_lo[sz] * 1000 / e_hi[sz]);
            }
            return score;
        };
        Fit best;
        int judged = 0;
        for (int t = 0; t < 12 && judged < 6 && best.score < 900; ++t) {
            if (t == 3 && !paths) break;
            // Per island a randomised farthest-point pick among its candidates.
            std::vector<std::size_t> set;
            for (int i = 0; i < k; ++i) {
                const std::vector<std::size_t>& o = of[static_cast<std::size_t>(i)];
                std::vector<std::size_t> picked{o[r.below(static_cast<std::uint32_t>(o.size()))]};
                while (picked.size() < static_cast<std::size_t>(sizes[static_cast<std::size_t>(i)])) {
                    std::vector<std::int64_t> near(o.size(), 0);
                    std::int64_t far = 0;
                    for (std::size_t c = 0; c < o.size(); ++c) {
                        std::int64_t d = kInf;
                        for (std::size_t p : picked) d = std::min<std::int64_t>(d, line[o[c] * nc + p]);
                        near[c] = d, far = std::max(far, d);
                    }
                    std::vector<std::size_t> pool;
                    for (std::size_t c = 0; c < o.size(); ++c) {
                        if (near[c] * 100 >= far * 72) pool.push_back(o[c]);
                    }
                    picked.push_back(pool[r.below(static_cast<std::uint32_t>(pool.size()))]);
                }
                for (std::size_t c : picked) set.push_back(c);
            }
            std::int64_t sc = spacing(set);
            for (int step = 0, idle = 0; step < 40 * n && idle < 3 * n && sc < 960; ++step) {
                const bool slide = step % 2 == 1;
                const std::size_t s = slide ? r.below(static_cast<std::uint32_t>(n)) : worst;
                const std::size_t was = set[s];
                std::size_t best_c = was;
                std::int64_t best_s = sc;
                for (std::size_t c : of[static_cast<std::size_t>(isl_of[s])]) {
                    if ((slide && line[was * nc + c] > 1500) || std::find(set.begin(), set.end(), c) != set.end())
                        continue;
                    set[s] = c;
                    const std::int64_t s2 = spacing(set);
                    if (s2 > best_s) best_s = s2, best_c = c;
                }
                set[s] = best_c;
                sc = spacing(set);
                idle = best_c == was ? idle + 1 : 0;
            }
            if (sc < 0) continue;
            if (!paths) {
                paths = true;
                for (std::size_t a = 0; a < nc; ++a) {
                    const std::vector<std::int32_t> d = Paths(gg, geo_land, cand[a], kGeoStep, kGeoStep);
                    for (std::size_t b = 0; b < nc; ++b) geo[a * nc + b] = d[cand[b]];
                }
                --t;
                continue;
            }
            if (sc < 820) continue;  // (the hard checks want 0.80 across the water and 0.77 by land)
            ++judged;
            std::vector<Pt> pts, ordered;
            std::vector<Pair> pairs;
            for (std::size_t c : set) pts.push_back(pt(c));
            const std::int64_t s = Judge(gg, geo_land, pts, isl_of, large, &ordered, &pairs);
            if (s < 0) continue;
            const std::int64_t score = std::min(s, sc);
            if (score > best.score) best.score = score, best.starts = ordered, best.pairs = pairs;
        }
        if (best.score < 0) continue;
        *starts = best.starts;
        *island = isl_of;
        *neighbours = best.pairs;
        std::sort(neighbours->begin(), neighbours->end());
        design->gw = dg.w, design->gh = dg.h;
        design->value = std::move(val);
        design->words = dr.words;
        return true;
    }
    *why = "positions: no fair starts on 24 islands designs";
    return false;
}

void LandTerritories(const MapData& m, const std::vector<std::uint8_t>& land, const std::vector<Pt>& starts,
                     std::vector<std::int8_t>* territory) {
    const std::int32_t W = m.width, H = m.height;
    // Nodes of 2 cells by 4 rows (192 x 96 world px): passable where at least half the cells are dry.
    SeaGrid g;
    g.w = (W + 1) / 2, g.h = (H + 3) / 4, g.step = 96;
    std::vector<std::int32_t> dry(g.N(), 0), cells(g.N(), 0), build(g.N(), 0);
    for (std::int32_t y = 0; y < H; ++y) {
        for (std::int32_t x = 0; x < W; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * W + x, k = g.I(x / 2, y / 4);
            ++cells[k];
            dry[k] += m.cells[i].object != kWaterMark ? 1 : 0;
            build[k] += land[i];
        }
    }
    std::vector<std::uint8_t> pass(g.N());
    for (std::size_t k = 0; k < g.N(); ++k) pass[k] = dry[k] * 2 >= cells[k] ? 1 : 0;
    const std::size_t n = starts.size();
    std::vector<std::vector<std::int32_t>> d(n);
    std::vector<std::size_t> home(n);
    for (std::size_t s = 0; s < n; ++s) {
        std::int32_t cx = 0, cy = 0;
        CellAt(starts[s], &cx, &cy);
        home[s] = g.I(std::clamp(cx, 0, W - 1) / 2, std::clamp(cy, 0, H - 1) / 4);
        d[s] = Paths(g, pass, home[s], 192, 96);
    }
    // The weights: a territory with more than its share of buildable land moves its borders in, step by step.
    std::vector<std::int64_t> w(n, 0), best_w(n, 0), got(n, 0);
    std::vector<std::int8_t> own(g.N(), -1), best_own;
    std::int64_t best_ratio = -1;
    for (int it = 0; it < 48; ++it) {
        std::fill(got.begin(), got.end(), 0);
        for (std::size_t k = 0; k < g.N(); ++k) {
            std::int64_t bv = 0;
            own[k] = -1;
            for (std::size_t s = 0; s < n; ++s) {
                if (d[s][k] < 0) continue;
                const std::int64_t v = d[s][k] + w[s];  // may be below 0
                if (own[k] < 0 || v < bv) bv = v, own[k] = static_cast<std::int8_t>(s);
            }
            if (own[k] >= 0) got[static_cast<std::size_t>(own[k])] += build[k];
        }
        bool homes = true;
        for (std::size_t s = 0; s < n; ++s) homes = homes && own[home[s]] == static_cast<std::int8_t>(s);
        const std::int64_t lo = *std::min_element(got.begin(), got.end());
        const std::int64_t hi = std::max<std::int64_t>(1, *std::max_element(got.begin(), got.end()));
        if (homes && lo * 1000 / hi > best_ratio) best_ratio = lo * 1000 / hi, best_own = own, best_w = w;
        if (best_ratio >= 985) break;
        // Per island (the starts each start reaches) the mean of its starts' land.
        for (std::size_t s = 0; s < n; ++s) {
            std::int64_t sum = 0, cnt = 0;
            for (std::size_t q = 0; q < n; ++q) {
                if (d[s][home[q]] >= 0) sum += got[q], ++cnt;
            }
            const std::int64_t mean = std::max<std::int64_t>(1, sum / std::max<std::int64_t>(1, cnt));
            w[s] += (got[s] - mean) * (900 - 15 * it) / mean;
        }
    }
    if (best_own.empty()) best_own = own;
    territory->assign(static_cast<std::size_t>(W) * static_cast<std::size_t>(H), -1);
    for (std::int32_t y = 0; y < H; ++y) {
        for (std::int32_t x = 0; x < W; ++x)
            (*territory)[static_cast<std::size_t>(y) * W + x] = best_own[g.I(x / 2, y / 4)];
    }
}

}  // namespace antanta::sim::mapgen
