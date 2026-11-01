#include "render/shells.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <unordered_map>

#include "formats/image.hpp"
#include "render/live.hpp"

namespace antanta {
namespace {



struct PictureFile {
    const char* name;
    int w, h;
};
constexpr std::array<PictureFile, 9> kPictures = {{{"Snar_S.444", 8, 8},
                                                   {"Snar_M.444", 8, 8},
                                                   {"Snar_L.444", 16, 16},
                                                   {"Snar_gr.444", 8, 8},
                                                   {"Snar_bo.444", 8, 8},
                                                   {"torpeda.444", 32, 64},
                                                   {"Oskolok_1.444", 8, 8},
                                                   {"Oskolok_2.444", 8, 8},
                                                   {"Oskolok_3.444", 8, 8}}};


constexpr std::array<std::uint8_t, 13> kPicture = {0, 1, 0, 0, 1, 2, 3, 4, 0, 5, 6, 7, 8};
constexpr std::array<float, 13> kHeightFactor = {0.5f, 0.5f, 0.3f, 0.2f, 0.2f, 0.2f, 1.0f,
                                                 1.0f, 1.0f, 1.0f, 0.1f, 0.3f, 0.2f};

constexpr float kHalfPi = 1.5707963705062866f;
constexpr float kSpin = 18.84955596923828f;


bool HasTrail(int kind) { return kind < 6 || kind == 7 || kind == 8; }

int Ftol(double v) { return static_cast<int>(v); }



float FlightHeight(const sim::Shell& sh, int n) {
    const auto sub = static_cast<float>(sh.v0 / static_cast<double>(sh.substep));
    if (!(0.0f < sub)) return sh.cz;
    const double i = std::ceil(static_cast<double>(sub)) - 1.0;
    const double t = i / static_cast<double>(sub) + n;
    const double tt = t / static_cast<double>(sh.a4);
    return static_cast<float>((tt * sh.vz - tt * tt * sh.g * 0.5) + sh.z0);
}




ShellQuad Rotated(const Image* pic, float x, float y, int w, int h, float angle, float alpha) {
    const float hw = static_cast<float>(w >> 1), hh = static_cast<float>(h >> 1);
    const std::array<float, 4> ox = {hw, -hw, hw, -hw}, oy = {-hh, -hh, hh, hh};
    const double c = std::cos(static_cast<double>(angle)), sn = std::sin(static_cast<double>(angle));
    ShellQuad q;
    q.pic = pic, q.alpha = alpha;
    for (int k = 0; k < 4; ++k) {
        q.x[static_cast<std::size_t>(k)] = static_cast<float>(oy[k] * c + ox[k] * sn + x);
        q.y[static_cast<std::size_t>(k)] = static_cast<float>(ox[k] * c - oy[k] * sn + y);
    }
    return q;
}

}  // namespace

int shell_picture(int kind) { return kind >= 0 && kind < 13 ? kPicture[static_cast<std::size_t>(kind)] : -1; }

float shell_height_factor(int kind) {
    return kind >= 0 && kind < 13 ? kHeightFactor[static_cast<std::size_t>(kind)] : 1.0f;
}

float shell_draw_height(const sim::Shell& sh, float cz) {
    return static_cast<float>((static_cast<double>(cz) - sh.z0) * shell_height_factor(sh.kind) + sh.z0);
}

void shell_graphics_load(ShellGraphics& g, const GameFileLoader& load) {
    if (g.tried) return;
    g.tried = true;
    for (std::size_t i = 0; i < kPictures.size(); ++i) {
        const std::string path = std::string("Data/Misc/") + kPictures[i].name;
        std::string err;
        const auto bytes = load(path, &err);
        std::optional<Image> img;
        if (bytes) img = decode_444(*bytes, &err);
        if (img) {
            g.pics[i] = std::move(*img);
        } else {
            g.warnings.push_back(path + ": " + err);
        }
    }
    std::string err;
    const auto bytes = load("Data/Misc/Trace.bmp", &err);
    std::optional<Image> img;
    if (bytes) img = decode_bmp(*bytes, &err);
    if (img) {


        for (std::size_t i = 0; i + 3 < img->rgba.size(); i += 4) {
            if (img->rgba[i] == 255 && img->rgba[i + 1] == 0 && img->rgba[i + 2] == 255) img->rgba[i + 3] = 0;
        }
        g.trace = std::move(*img);
    } else {
        g.warnings.push_back("Data/Misc/Trace.bmp: " + err);
    }
}

void shell_trails_step(ShellTrails& t, const sim::GameState& s) {
    if (t.stepped && t.stepped_tick == s.tick) return;
    t.stepped = true, t.stepped_tick = s.tick;


    auto key_of = [](const sim::Shell& sh) { return std::array<float, 6>{sh.x0, sh.y0, sh.z0, sh.x1, sh.y1, sh.z1}; };
    auto hash_of = [](const std::array<float, 6>& k, std::uint8_t kind) {
        std::uint64_t h = kind;
        for (float f : k) h = h * 1000003u ^ static_cast<std::uint64_t>(std::hash<float>{}(f));
        return h;
    };
    std::unordered_multimap<std::uint64_t, std::size_t> open;
    for (std::size_t i = 0; i < t.trails.size(); ++i) {
        if (t.trails[i].attached) open.emplace(hash_of(t.trails[i].key, t.trails[i].kind), i);
    }
    std::vector<char> found(t.trails.size(), 0);
    // The list's head is the newest shell; trails are made oldest first.
    for (auto it = s.shells.rbegin(); it != s.shells.rend(); ++it) {
        const sim::Shell& sh = *it;
        if (!HasTrail(sh.kind)) continue;
        const std::array<float, 6> key = key_of(sh);
        bool matched = false;
        const auto range = open.equal_range(hash_of(key, sh.kind));
        for (auto o = range.first; o != range.second; ++o) {
            const std::size_t i = o->second;
            if (found[i] != 0 || t.trails[i].key != key || t.trails[i].kind != sh.kind) continue;
            found[i] = 1, matched = true;
            ShellTrails::Trail& tr = t.trails[i];

            if (s.tick > tr.made + 2) tr.points.push_front({sh.cx, sh.cy, shell_draw_height(sh, sh.cz), 128});
            break;
        }
        if (!matched) {
            ShellTrails::Trail tr;
            tr.key = key, tr.kind = sh.kind, tr.made = s.tick;
            t.trails.push_back(std::move(tr));
            found.push_back(1);
        }
    }
    for (std::size_t i = 0; i < t.trails.size(); ++i) {
        ShellTrails::Trail& tr = t.trails[i];
        if (found[i] == 0) tr.attached = false;

        for (std::size_t k = 1; k < tr.points.size(); ++k) tr.points[k].alpha = std::max(0, tr.points[k].alpha - 10);
        if (tr.points.size() > 1 && tr.points.back().alpha == 0) tr.points.pop_back();
    }


    std::erase_if(t.trails, [](const ShellTrails::Trail& tr) { return !tr.attached && tr.points.size() < 2; });
}

void shell_quads(ShellGraphics& g, const GameFileLoader& load, const sim::GameState& s, ViewPos pos,
                 const DrawBlend* blend, const ShellTrails* trails, std::vector<ShellQuad>* out) {
    if (s.shells.empty() && (trails == nullptr || trails->trails.empty())) return;
    shell_graphics_load(g, load);
    const auto vx = static_cast<float>(view_x(pos)), vy = static_cast<float>(view_y(pos));

    for (const sim::Shell& sh : s.shells) {


        if (sh.flown == 0 || sh.kind > 12) continue;
        const int pic = kPicture[sh.kind];
        const Image& img = g.pics[static_cast<std::size_t>(pic)];
        if (img.width <= 0 || img.height <= 0) continue;
        float cx = sh.cx, cy = sh.cy, cz = sh.cz;
        if (blend != nullptr && blend->alpha < 1) {  // the same shell one tick earlier
            for (const DrawBlend::ShellWas& w : blend->shells) {
                if (w.x0 != sh.x0 || w.y0 != sh.y0 || w.x1 != sh.x1 || w.y1 != sh.y1 || w.flown + 1 != sh.flown ||
                    w.owner_player != sh.owner.player || w.owner_serial != sh.owner.serial) {
                    continue;
                }
                const float k = 1 - std::max(0.0f, blend->alpha);
                if (std::fabs(w.cx - cx) < DrawBlend::kTeleport && std::fabs(w.cy - cy) < DrawBlend::kTeleport) {
                    cx += (w.cx - cx) * k, cy += (w.cy - cy) * k, cz += (w.cz - cz) * k;
                }
                break;
            }
        }
        const float z = shell_draw_height(sh, cz);
        const float X = cx - vx, Y = (cy - vy) - z;
        const int hw = kPictures[static_cast<std::size_t>(pic)].w / 2,
                  hh = kPictures[static_cast<std::size_t>(pic)].h / 2;
        if (sh.kind == 9) {
            const auto a = static_cast<float>(std::atan2(-sh.vy, sh.vx));
            out->push_back(Rotated(&img, X, Y, hw * 2, hh * 2, a, 1));
            continue;
        }
        ShellQuad q;
        q.pic = &img;
        if (sh.kind == 10 || sh.kind == 11) {



            const int n = Ftol(sh.flight) + 1;
            const double a = static_cast<double>(sh.flown) * static_cast<double>(kSpin) / n;
            const double c = std::cos(a), sn = std::sin(a);
            const double w = hw, h = hh;
            q.x = {static_cast<float>(-w * c + h * sn + X), static_cast<float>(w * c + h * sn + X),
                   static_cast<float>(-w * c - h * sn + X), static_cast<float>(w * c - h * sn + X)};
            q.y = {static_cast<float>(-h * c - w * sn + Y), static_cast<float>(w * sn - h * c + Y),
                   static_cast<float>(h * c - w * sn + Y), static_cast<float>(h * c + w * sn + Y)};
        } else {


            const float before = sh.flown == 1 ? sh.z0 : FlightHeight(sh, sh.flown - 2);
            const double dz = static_cast<double>(shell_draw_height(sh, sh.cz)) - shell_draw_height(sh, before);
            const double a = std::atan2(-dz, sh.vx) + static_cast<double>(kHalfPi);
            const auto C = static_cast<float>(Ftol(hw * std::cos(a))), S = static_cast<float>(Ftol(hh * std::sin(a)));
            q.x = {X - C + S, X + C + S, X - C - S, X + C - S};
            q.y = {Y - C - S, Y - C + S, Y + C - S, Y + C + S};
        }
        out->push_back(q);
    }
    if (trails == nullptr || g.trace.width <= 0) return;


    for (const ShellTrails::Trail& tr : trails->trails) {
        for (std::size_t i = 1; i < tr.points.size(); ++i) {
            const ShellTrails::Point& p = tr.points[i];
            const ShellTrails::Point& q = tr.points[i - 1];
            const float dx = p.x - q.x;
            const float dy = (p.y - p.z) - (q.y - q.z);
            const float len = std::sqrt(dx * dx + dy * dy) + 5.0f;
            const auto a = static_cast<float>(std::atan2(dx, dy) + static_cast<double>(kHalfPi));
            out->push_back(Rotated(&g.trace, p.x - vx, (p.y - p.z) - vy, 5, static_cast<std::uint16_t>(Ftol(len)), a,
                                   static_cast<float>(p.alpha) / 255.0f));
        }
    }
}

void shell_quads_draw(std::span<const ShellQuad> quads, std::vector<std::uint8_t>& rgba, int w, int h) {
    constexpr float kTint = 0xe5 / 255.0f;
    for (const ShellQuad& q : quads) {
        const Image& t = *q.pic;
        if (t.width <= 0 || t.height <= 0 || q.alpha <= 0) continue;
        const float e1x = q.x[1] - q.x[0], e1y = q.y[1] - q.y[0], e2x = q.x[2] - q.x[0], e2y = q.y[2] - q.y[0];
        const float det = e1x * e2y - e1y * e2x;
        if (std::fabs(det) < 1e-6f) continue;
        const float x_lo = *std::min_element(q.x.begin(), q.x.end()), x_hi = *std::max_element(q.x.begin(), q.x.end());
        const float y_lo = *std::min_element(q.y.begin(), q.y.end()), y_hi = *std::max_element(q.y.begin(), q.y.end());
        const int x0 = std::max(0, static_cast<int>(std::ceil(x_lo))), x1 = std::min(w - 1, static_cast<int>(x_hi));
        const int y0 = std::max(0, static_cast<int>(std::ceil(y_lo))), y1 = std::min(h - 1, static_cast<int>(y_hi));
        // Pixel centres on whole coordinates (Direct3D 7); texture coordinates 0 .. 0.99999, point sampled.
        for (int py = y0; py <= y1; ++py) {
            for (int px = x0; px <= x1; ++px) {
                const float dx = static_cast<float>(px) - q.x[0], dy = static_cast<float>(py) - q.y[0];
                const float u = (dx * e2y - dy * e2x) / det, v = (e1x * dy - e1y * dx) / det;
                if (u < 0 || u >= 1 || v < 0 || v >= 1) continue;
                const int tx = std::min(t.width - 1, static_cast<int>(u * 0.99999f * static_cast<float>(t.width)));
                const int ty = std::min(t.height - 1, static_cast<int>(v * 0.99999f * static_cast<float>(t.height)));
                const std::uint8_t* c = &t.rgba[(static_cast<std::size_t>(ty) * t.width + tx) * 4];
                const float a = static_cast<float>(c[3]) / 255.0f * q.alpha;
                if (a <= 0) continue;
                std::uint8_t* d = &rgba[(static_cast<std::size_t>(py) * w + px) * 4];
                for (int ch = 0; ch < 3; ++ch) {
                    const float src = static_cast<float>(c[ch]) * kTint;
                    d[ch] = static_cast<std::uint8_t>(static_cast<float>(d[ch]) + (src - d[ch]) * a + 0.5f);
                }
            }
        }
    }
}

}  // namespace antanta
