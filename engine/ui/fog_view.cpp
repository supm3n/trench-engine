#include "ui/fog_view.hpp"

#include <algorithm>
#include <cmath>

#include "sim/state.hpp"

namespace antanta::ui {

FogRamp fog_ramp(const std::vector<std::uint8_t>& rgba, int width, int height) {
    FogRamp r{};
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<std::size_t>(width) * height * 4) {
        for (std::size_t i = 0; i < r.size(); ++i) r[i] = static_cast<std::uint8_t>(255 - i * 255 / 31);
        return r;
    }
    for (std::size_t i = 0; i < r.size(); ++i) {
        const std::size_t row = i * static_cast<std::size_t>(height) / r.size();
        r[i] = rgba[row * static_cast<std::size_t>(width) * 4 + 3];
    }
    if (r.front() < r.back()) std::reverse(r.begin(), r.end());
    return r;
}

std::uint8_t fog_alpha(const FogRamp& ramp, double d) {
    if (d < kFogBlack) return 255;
    if (d >= kFogClear) return 0;
    const double t = (d - kFogBlack) / (kFogClear - kFogBlack) * 31.0;
    const auto i = static_cast<std::size_t>(t);
    const double f = t - static_cast<double>(i);
    const double v = ramp[i] * (1.0 - f) + ramp[std::min<std::size_t>(i + 1, 31)] * f;
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 255.0)));
}

namespace {

int floor_div(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

constexpr double kFar = 1e20;  // no sample in fog on the line (the usual finite stand-in for infinity)

// The squared distance transform of one line (Felzenszwalb and Huttenlocher): d[q] = min_p (f[p] + (q - p)^2).
void distance_1d(const std::vector<double>& f, std::vector<double>& d, std::vector<int>& v, std::vector<double>& z) {
    const int n = static_cast<int>(f.size());
    auto at = [](const auto& a, int i) { return a[static_cast<std::size_t>(i)]; };
    auto cross = [&](int q, int p) { return ((at(f, q) + q * q) - (at(f, p) + p * p)) / (2.0 * q - 2.0 * p); };
    int k = 0;
    v[0] = 0, z[0] = -kFar, z[1] = kFar;
    for (int q = 1; q < n; ++q) {
        double s = cross(q, at(v, k));
        while (s <= at(z, k)) s = cross(q, at(v, --k));
        ++k;
        v[static_cast<std::size_t>(k)] = q, z[static_cast<std::size_t>(k)] = s,
        z[static_cast<std::size_t>(k) + 1] = kFar;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (at(z, k + 1) < q) ++k;
        const int p = at(v, k);
        d[static_cast<std::size_t>(q)] = (q - p) * (q - p) + at(f, p);
    }
}

// The heights of the cells cx0 .. cx1 - 1, cy0 .. cy1 - 1, row by row (0 off the map).
std::vector<std::uint8_t> box_heights(const sim::Map& m, int cx0, int cy0, int cx1, int cy1) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(cx1 - cx0) * static_cast<std::size_t>(cy1 - cy0), 0);
    std::size_t k = 0;
    for (int y = cy0; y < cy1; ++y) {
        for (int x = cx0; x < cx1; ++x, ++k) {
            if (x >= 0 && y >= 0 && x < m.width && y < m.height) {
                out[k] = m.heights[static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width) +
                                   static_cast<std::size_t>(x)];
            }
        }
    }
    return out;
}

// Whether the cells both boxes hold have the heights `p` picked with (`now`: the heights of cx0 .. cx1, cy0 .. cy1).
bool same_heights(const FogPicks& p, const std::vector<std::uint8_t>& now, int cx0, int cy0, int cx1, int cy1) {
    const int x0 = std::max(cx0, p.cx0), x1 = std::min(cx1, p.cx1);
    const int y0 = std::max(cy0, p.cy0), y1 = std::min(cy1, p.cy1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t a = static_cast<std::size_t>(y - p.cy0) * static_cast<std::size_t>(p.cx1 - p.cx0) +
                                  static_cast<std::size_t>(x - p.cx0);
            const std::size_t b = static_cast<std::size_t>(y - cy0) * static_cast<std::size_t>(cx1 - cx0) +
                                  static_cast<std::size_t>(x - cx0);
            if (p.heights[a] != now[b]) return false;
        }
    }
    return true;
}

}  // namespace

FogView fog_view(const sim::GameState& s, const FogRamp& ramp, int view_x, int view_y, int view_w, int view_h, int step,
                 FogPicks* picks) {
    FogView out;
    const sim::Fog& fog = s.fog;
    if (!fog.built || !fog.on || step <= 0 || view_w <= 0 || view_h <= 0) return out;
    out.step = step;
    // The samples of the view, one to spare on each side, and around them the margin the edge can reach in from.
    const int ix0 = floor_div(view_x, step) - 1, iy0 = floor_div(view_y, step) - 1;
    const int ix1 = floor_div(view_x + view_w, step) + 1, iy1 = floor_div(view_y + view_h, step) + 1;
    const int margin = static_cast<int>(std::ceil(kFogClear / step)) + 1;
    const int gx0 = ix0 - margin, gy0 = iy0 - margin;
    const int gw = ix1 - ix0 + 1 + 2 * margin, gh = iy1 - iy0 + 1 + 2 * margin;
    // The cells those samples can fall on (PickCell looks up to 16 rows further down for hills).
    const int cx0 = floor_div(gx0 * step, 96) - 2, cx1 = floor_div((gx0 + gw) * step, 96) + 3;
    const int cy0 = floor_div(gy0 * step, 24) - 4, cy1 = floor_div((gy0 + gh) * step, 24) + 20;
    const std::vector<std::uint8_t> mask = sim::Fog_CellMask(fog, cx0, cy0, cx1, cy1);
    const int mw = cx1 - cx0;
    // The last call's picks are kept with the same step and map size and the same heights where both boxes meet. The
    // box: the mask's and one cell more on every side (a pick reads up to three columns right of its sample's flat
    // cell, one more than the mask's box holds); a sample of both calls read only cells of both boxes.
    const int hx0 = cx0 - 1, hy0 = cy0 - 1, hx1 = cx1 + 1, hy1 = cy1 + 1;
    std::vector<std::uint8_t> heights;
    bool keep = false;
    if (picks) {
        heights = box_heights(s.map, hx0, hy0, hx1, hy1);
        keep = picks->step == step && picks->map_w == s.map.width && picks->map_h == s.map.height &&
               same_heights(*picks, heights, hx0, hy0, hx1, hy1);
    }
    std::vector<sim::Cell> cells(picks ? static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh) : 0);
    // f: 0 on a sample in fog (the sites), kFar on a lit one; the cell under a sample is the one the world shows
    // there (the screen pick with the heights).
    std::vector<double> f(static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh), 0.0);
    for (int j = 0; j < gh; ++j) {
        for (int i = 0; i < gw; ++i) {
            const float x = static_cast<float>((gx0 + i) * step), y = static_cast<float>((gy0 + j) * step);
            bool lit = false;
            if (x >= 0 && y >= 0) {
                const int pi = keep ? gx0 + i - picks->gx0 : -1, pj = keep ? gy0 + j - picks->gy0 : -1;
                const sim::Cell c = pi >= 0 && pj >= 0 && pi < picks->gw && pj < picks->gh
                                        ? picks->cells[static_cast<std::size_t>(pj) * picks->gw + pi]
                                        : sim::PickCell(s.map, x, y);
                if (picks) cells[static_cast<std::size_t>(j) * gw + i] = c;
                if (c.x >= cx0 && c.x < cx1 && c.y >= cy0 && c.y < cy1) {
                    lit = mask[static_cast<std::size_t>(c.y - cy0) * static_cast<std::size_t>(mw) +
                               static_cast<std::size_t>(c.x - cx0)] != 0;
                }
            }
            if (lit) f[static_cast<std::size_t>(j) * static_cast<std::size_t>(gw) + static_cast<std::size_t>(i)] = kFar;
        }
    }
    if (picks) {
        picks->step = step, picks->map_w = s.map.width, picks->map_h = s.map.height;
        picks->gx0 = gx0, picks->gy0 = gy0, picks->gw = gw, picks->gh = gh;
        picks->cx0 = hx0, picks->cy0 = hy0, picks->cx1 = hx1, picks->cy1 = hy1;
        picks->cells = std::move(cells);
        picks->heights = std::move(heights);
    }
    // The squared distance in samples to the nearest sample in fog: columns, then rows.
    const int n = std::max(gw, gh);
    std::vector<double> line(static_cast<std::size_t>(n)), dist(static_cast<std::size_t>(n)),
        z(static_cast<std::size_t>(n) + 1);
    std::vector<int> v(static_cast<std::size_t>(n));
    line.resize(static_cast<std::size_t>(gh)), dist.resize(static_cast<std::size_t>(gh));
    for (int i = 0; i < gw; ++i) {
        for (int j = 0; j < gh; ++j) line[static_cast<std::size_t>(j)] = f[static_cast<std::size_t>(j) * gw + i];
        distance_1d(line, dist, v, z);
        for (int j = 0; j < gh; ++j) f[static_cast<std::size_t>(j) * gw + i] = dist[static_cast<std::size_t>(j)];
    }
    line.resize(static_cast<std::size_t>(gw)), dist.resize(static_cast<std::size_t>(gw));
    for (int j = 0; j < gh; ++j) {
        for (int i = 0; i < gw; ++i) line[static_cast<std::size_t>(i)] = f[static_cast<std::size_t>(j) * gw + i];
        distance_1d(line, dist, v, z);
        for (int i = 0; i < gw; ++i) f[static_cast<std::size_t>(j) * gw + i] = dist[static_cast<std::size_t>(i)];
    }
    out.x0 = ix0 * step, out.y0 = iy0 * step;
    out.cols = ix1 - ix0 + 1, out.rows = iy1 - iy0 + 1;
    out.alpha.resize(static_cast<std::size_t>(out.cols) * static_cast<std::size_t>(out.rows));
    for (int j = 0; j < out.rows; ++j) {
        for (int i = 0; i < out.cols; ++i) {
            const double d2 = f[static_cast<std::size_t>(j + margin) * gw + static_cast<std::size_t>(i + margin)];
            // A lit sample's distance to the mask edge: halfway to the nearest sample in fog.
            const double d = d2 == 0 ? -1.0 : (std::sqrt(d2) - 0.5) * step;
            out.alpha[static_cast<std::size_t>(j) * out.cols + i] = d2 >= kFar / 2 ? 0 : fog_alpha(ramp, d);
        }
    }
    return out;
}

}  // namespace antanta::ui
