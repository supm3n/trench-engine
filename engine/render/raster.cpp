#include "render/raster.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace antanta {
namespace {

constexpr double kEdge = -1e-7;  // barycentric tolerance, as render_proto.py

// nibble / 15 in float, as the prototype's argb4444(): float32 array / 15.0
const std::array<float, 16> kNibble = [] {
    std::array<float, 16> t{};
    for (int i = 0; i < 16; ++i) t[static_cast<std::size_t>(i)] = static_cast<float>(i) / 15.0f;
    return t;
}();

}  // namespace

Texture texture_from_image(const Image& img) {
    Texture t;
    t.width = img.width;
    t.height = img.height;
    t.rgba.resize(img.rgba.size());
    for (std::size_t i = 0; i < img.rgba.size(); ++i) t.rgba[i] = static_cast<std::uint8_t>(img.rgba[i] / 17);
    return t;
}

void Target::reset(int w, int h) {
    width = w;
    height = h;
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    rgb.assign(n * 3, 0.0f);
    key.assign(n, kFarKey);
    water.assign(n, 0);
}

#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
namespace {

// The band threads, started once and kept (docs/bugs.md B59: starting 7 threads for each of the 3 or 4 passes of a
// frame cost a millisecond or more). Band i + 1 goes to worker i, band 0 to the caller, as before.
class BandPool {
  public:
    explicit BandPool(int workers) {
        for (int i = 0; i < workers; ++i) threads_.emplace_back([this, i] { loop(i + 1); });
    }
    // false when another call is using the pool (a band of it calling parallel_bands again): the caller then starts
    // its own threads.
    bool run(int bands, int height, const std::function<void(int top, int bottom)>& fn) {
        if (busy_.exchange(true)) return false;
        {
            std::lock_guard lock(m_);
            fn_ = &fn, height_ = height, bands_ = bands, pending_ = static_cast<int>(threads_.size());
            ++job_;
        }
        start_.notify_all();
        fn(0, height / bands);
        std::unique_lock lock(m_);
        done_.wait(lock, [this] { return pending_ == 0; });
        fn_ = nullptr;
        busy_ = false;
        return true;
    }

  private:
    void loop(int band) {
        std::uint64_t seen = 0;
        for (;;) {
            const std::function<void(int, int)>* fn;
            int height, bands;
            {
                std::unique_lock lock(m_);
                start_.wait(lock, [&] { return job_ != seen; });
                seen = job_;
                fn = fn_, height = height_, bands = bands_;
            }
            if (band < bands) (*fn)(height* band / bands, height * (band + 1) / bands);
            std::lock_guard lock(m_);
            if (--pending_ == 0) done_.notify_one();
        }
    }
    std::atomic<bool> busy_{false};
    std::mutex m_;
    std::condition_variable start_, done_;
    const std::function<void(int, int)>* fn_ = nullptr;
    int height_ = 0, bands_ = 0, pending_ = 0;
    std::uint64_t job_ = 0;
    std::vector<std::thread> threads_;
};

}  // namespace
#endif

namespace {
std::atomic<int> g_spare_cores{0};  // parallel_bands_spare
}  // namespace

void parallel_bands_spare(int cores) { g_spare_cores = std::max(cores, 0); }

void parallel_bands(int height, const std::function<void(int top, int bottom)>& fn) {
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    fn(0, height);  // a web build without threads (docs/web.md)
#else
    const int cores = std::max(static_cast<int>(std::thread::hardware_concurrency()), 1);
    const int n = std::clamp(cores - g_spare_cores.load(std::memory_order_relaxed), 1, 16);
    if (n <= 1 || height < 64) {
        fn(0, height);
        return;
    }
    // Kept until the process ends, with a worker for every band of the processors (the spare ones idle).
    static BandPool* const pool = new BandPool(std::min(cores, 16) - 1);
    if (pool->run(n, height, fn)) return;
    std::vector<std::thread> workers;
    for (int i = 1; i < n; ++i) workers.emplace_back(fn, height * i / n, height * (i + 1) / n);
    fn(0, height / n);
    for (std::thread& w : workers) w.join();
#endif
}
void draw_triangle(Target& t, const Vertex& a, const Vertex& b, const Vertex& c, const DrawState& s) {
    const double fx0 = std::max(std::floor(std::min({a.x, b.x, c.x})), 0.0);
    const double fy0 = std::max(std::floor(std::min({a.y, b.y, c.y})), std::max(0.0, double(s.clip_top)));
    const double fx1 = std::min(std::ceil(std::max({a.x, b.x, c.x})), t.width - 1.0);
    const double fy1 = std::min(std::ceil(std::max({a.y, b.y, c.y})), std::min(t.height, s.clip_bottom) - 1.0);
    if (!(fx0 <= fx1 && fy0 <= fy1)) return;  // also rejects NaN coordinates
    const int x0 = static_cast<int>(fx0), x1 = static_cast<int>(fx1);
    const int y0 = static_cast<int>(fy0), y1 = static_cast<int>(fy1);
    const double den = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (std::fabs(den) < 1e-9) return;
    const Texture* tex = s.texture;
    if (tex && (tex->width <= 0 || tex->height <= 0)) return;

    // Per row, the x range where all three weights can be >= kEdge, widened by 2 px; the exact test below decides.
    const double s1 = (c.y - a.y) / den, s2 = -(b.y - a.y) / den, s0 = -(s1 + s2);
    for (int y = y0; y <= y1; ++y) {
        const double dy = y - a.y;
        double lo = x0, hi = x1;
        bool empty = false;
        auto limit = [&](double slope, double value_at_x0) {
            if (slope > 0) {
                lo = std::max(lo, x0 + (kEdge - value_at_x0) / slope);
            } else if (slope < 0) {
                hi = std::min(hi, x0 + (kEdge - value_at_x0) / slope);
            } else if (value_at_x0 < kEdge - 1e-6) {
                empty = true;
            }
        };
        const double fx = x0 - a.x;
        const double w1_0 = (fx * (c.y - a.y) - dy * (c.x - a.x)) / den;
        const double w2_0 = ((b.x - a.x) * dy - (b.y - a.y) * fx) / den;
        limit(s1, w1_0);
        limit(s2, w2_0);
        limit(s0, 1 - w1_0 - w2_0);
        if (empty || !(lo <= hi + 4)) continue;
        const int xa = static_cast<int>(std::max(std::floor(lo) - 2, fx0));
        const int xb = static_cast<int>(std::min(std::ceil(hi) + 2, fx1));
        for (int x = xa; x <= xb; ++x) {
            const double px = x - a.x;
            const double w1 = (px * (c.y - a.y) - dy * (c.x - a.x)) / den;
            const double w2 = ((b.x - a.x) * dy - (b.y - a.y) * px) / den;
            const double w0 = 1 - w1 - w2;
            if (!(w0 >= kEdge && w1 >= kEdge && w2 >= kEdge)) continue;
            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(t.width) + x;
            const double k = w0 * a.key + w1 * b.key + w2 * c.key;
            if (!(k >= static_cast<double>(t.key[i] - 0.001f))) continue;
            float src[3] = {s.tint[0], s.tint[1], s.tint[2]};
            float alpha = s.alpha;
            if (tex) {
                const double fu = std::floor((w0 * a.u + w1 * b.u + w2 * c.u) * tex->width);
                const double fv = std::floor((w0 * a.v + w1 * b.v + w2 * c.v) * tex->height);
                const int tu = static_cast<int>(std::clamp(fu, 0.0, tex->width - 1.0));
                const int tv = static_cast<int>(std::clamp(fv, 0.0, tex->height - 1.0));
                const std::uint8_t* texel =
                    &tex->rgba[(static_cast<std::size_t>(tv) * static_cast<std::size_t>(tex->width) + tu) * 4];
                if (s.colour_key && texel[0] == 0 && texel[1] == 0 && texel[2] == 0 && texel[3] == 0) continue;
                for (int ch = 0; ch < 3; ++ch) src[ch] = kNibble[texel[ch]] * s.tint[ch];
                alpha = kNibble[texel[3]] * s.alpha;
            }
            float* dst = &t.rgb[i * 3];
            if (s.gouraud) {  // texel (float) times the interpolated shade (double), as numpy promotes it
                const double shade = w0 * a.shade + w1 * b.shade + w2 * c.shade;
                for (int ch = 0; ch < 3; ++ch) {
                    const double v = static_cast<double>(src[ch]) * shade;
                    dst[ch] = s.blend ? static_cast<float>(v * alpha + static_cast<double>(dst[ch] * (1.0f - alpha)))
                                      : static_cast<float>(v);
                }
            } else {
                for (int ch = 0; ch < 3; ++ch) dst[ch] = s.blend ? src[ch] * alpha + dst[ch] * (1.0f - alpha) : src[ch];
            }
            if (s.z_write) {
                t.key[i] = static_cast<float>(k);
                t.water[i] = s.water ? 1 : 0;
            }
        }
    }
}

void draw_quad(Target& t, const std::array<Vertex, 4>& v, const DrawState& s) {
    draw_triangle(t, v[0], v[1], v[2], s);
    draw_triangle(t, v[1], v[2], v[3], s);
}

}  // namespace antanta
