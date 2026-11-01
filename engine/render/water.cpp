#include "render/water.hpp"

#include <cstdio>
#include <cstdlib>
#include <limits>

#include "formats/les.hpp"
#include "formats/sprites.hpp"
#include "render/effects_emit.hpp"
#include "sim/state.hpp"

namespace antanta {
namespace {

constexpr float kTint = 0xe5 / 255.0f;
constexpr float kWaveAlpha = 0xaa / 255.0f;
constexpr double kQuarterPi = 0.7853981852531433;
constexpr double kFourByPi = 1.2732394933700562;
constexpr double kPi = 3.1415927410125732;
constexpr double kCos30 = 0.8660253882408142;
constexpr int kSpotTile = 256;


constexpr float kScaleU = 0.99999f / 27.0f * 3.0f;
constexpr float kScaleV = -0.99999f / 43.0f * 3.0f;

int floor_div(int a, int b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }

std::vector<std::string> tokens(const Bytes& b) {
    std::vector<std::string> out;
    std::string cur;
    for (const std::uint8_t c : b) {
        if (c == '\r' || c == ' ' || c == '\t' || c == '\n') {
            if (!cur.empty()) out.push_back(cur), cur.clear();
        } else {
            cur.push_back(static_cast<char>(c));
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::optional<Texture> read_444(ByteSpan b) {
    std::string err;
    auto img = decode_444(b, &err);
    if (!img || img->width <= 0 || img->height <= 0) return std::nullopt;
    return texture_from_image(*img);
}


struct LesFrames {
    std::vector<Texture> frames;
    std::string info;
};
LesFrames load_les_frames(const GameFileLoader& load, const std::string& path, std::vector<std::string>* warn) {
    LesFrames r;
    std::string err;
    auto file = load(path, &err);
    auto table = file ? parse_les(*file, &err) : std::nullopt;
    if (!table) {
        warn->push_back(path + ": " + err);
        return r;
    }
    for (int id = 0; id < 256; ++id) {
        const LesEntry* e = find_entry_id(*table, static_cast<std::uint16_t>(id));
        if (e == nullptr) break;
        auto bytes = read_entry(*file, *e, &err);
        auto t = bytes ? read_444(*bytes) : std::nullopt;
        if (!t) {
            warn->push_back(path + ": entry " + std::to_string(id) + " unreadable");
            break;
        }
        r.frames.push_back(std::move(*t));
    }
    if (const LesEntry* e = find_entry(*table, "info.txt")) {
        if (auto b = read_entry(*file, *e, &err)) r.info.assign(b->begin(), b->end());
    }
    if (r.frames.empty()) warn->push_back(path + ": no pictures");
    return r;
}


WaterPics::Set load_set(const GameFileLoader& load, const std::string& path, std::vector<std::string>* warn) {
    WaterPics::Set s;
    LesFrames f = load_les_frames(load, path, warn);
    s.frames = std::move(f.frames);
    if (!s.frames.empty()) s.w = s.frames[0].width, s.h = s.frames[0].height;
    if (std::sscanf(f.info.c_str(), "%d %d", &s.ax, &s.ay) != 2) s.ax = s.ay = 0;
    return s;
}



void load_pics(WaterPics& p, const GameFileLoader& load) {
    p.tried = true;
    p.anim = load_les_frames(load, "Data/Misc/Water/Anim0/Anim_0.les", &p.warnings).frames;
    p.foam = load_les_frames(load, "Data/Misc/Water/CoastWater0_0.les", &p.warnings).frames;
    for (std::size_t k = 0; k < p.spots.size(); ++k) {
        const std::string path = "Data/Misc/Water/LightSpots/Lspot" + std::to_string(k) + ".444";
        std::string err;
        auto b = load(path, &err);
        auto t = b ? read_444(*b) : std::nullopt;
        if (t) {
            p.spots[k] = std::move(*t);
        } else {
            p.warnings.push_back(path + ": unreadable");
        }
    }
    std::string err;
    auto waves = load("Data/Misc/ShipWaves/Info.txt", &err);
    const std::vector<std::string> names = waves ? tokens(*waves) : std::vector<std::string>{};
    if (names.size() >= 3) {
        p.bow = load_set(load, "Data/Misc/ShipWaves/" + names[1], &p.warnings);
        p.stern = load_set(load, "Data/Misc/ShipWaves/" + names[2], &p.warnings);
    } else {
        p.warnings.push_back("Data/Misc/ShipWaves/Info.txt: unreadable");
    }
    auto particles = load("Data/Misc/ParticleAnims/Info.txt", &err);
    const std::vector<std::string> pn = particles ? tokens(*particles) : std::vector<std::string>{};
    if (pn.size() >= 2) {
        p.bubbles = load_set(load, "Data/Misc/ParticleAnims/" + pn[1], &p.warnings);
    } else {
        p.warnings.push_back("Data/Misc/ParticleAnims/Info.txt: unreadable");
    }
}


bool point_in_polygon(int px, int py, const std::vector<std::uint16_t>& poly) {
    const std::size_t n = poly.size();
    bool inside = false;
    for (std::size_t i = 0; i < n; ++i) {
        const int x0 = poly[i] & 0xff, y0 = poly[i] >> 8;
        const int x1 = poly[(i + 1) % n] & 0xff, y1 = poly[(i + 1) % n] >> 8;
        bool horizontal_test = true;
        if (px == x0 && px == x1) {  // a vertical edge through the point's column
            if (py < y0) {
                if (y1 <= py) return true;
            } else {
                if (py <= y1) return true;
                if (py <= y0) {
                    if (y1 <= py) return true;
                } else {
                    horizontal_test = false;
                }
            }
        }
        if (horizontal_test && py == y0 && py == y1) {  // a horizontal edge through the point's row
            if (x0 <= px && px <= x1) return true;
            if (!(x0 < px) && x1 <= px) return true;
        }

        const float t = static_cast<float>(px - x1) / (static_cast<float>(x0 - x1) + 9.9999998245167e-14f);
        if (t >= 0.0f && t <= 1.0f) {
            const float yy = static_cast<float>(y0) * t + (1.0f - t) * static_cast<float>(y1);
            if (static_cast<float>(py) == yy) return true;
        }
        if ((py <= y1) != (py <= y0) && static_cast<double>(px - x0) < static_cast<double>(x1 - x0) *
                                                                           static_cast<double>(py - y0) /
                                                                           static_cast<double>(y1 - y0)) {
            inside = !inside;
        }
    }
    return inside;
}



bool segments_cross(const std::array<int, 2>& p1, const std::array<int, 2>& p2, const std::array<int, 2>& p3,
                    const std::array<int, 2>& p4, std::array<int, 2>* out) {
    const int rx = p2[0] - p1[0], ry = p2[1] - p1[1];
    const int ex = p3[0] - p4[0], ey = p3[1] - p4[1];
    const auto d = static_cast<float>(ex * ry - rx * ey);
    if (d == 0.0f) return false;
    const int wx = p3[0] - p1[0], wy = p3[1] - p1[1];
    const float inv = 1.0f / d;
    const float s = static_cast<float>(wx * ry - wy * rx) * inv;
    const float t = static_cast<float>(wy * ex - wx * ey) * inv;
    if (!(s <= 1.0f && s >= 0.0f && t >= 0.0f && t <= 1.0f)) return false;
    (*out)[0] = static_cast<int>(p1[0] + static_cast<double>(t) * rx);
    (*out)[1] = static_cast<int>(p1[1] + static_cast<double>(t) * ry);
    return true;
}


Water::Foam foam_quad(std::array<int, 2> p, std::array<int, 2> q, int ox, int oy) {
    const int px = p[0] + ox, py = p[1] + oy, qx = q[0] + ox, qy = q[1] + oy;
    const int dx = px - qx, dy = py - qy;
    const float cx = static_cast<float>(qx + px) * 0.5f, cy = static_cast<float>(qy + py) * 0.5f;
    const auto len = static_cast<float>(std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy));
    const double phi = std::atan2(static_cast<double>(dy), static_cast<double>(dx));
    Water::Foam f;
    f.x = {cx + len, cx - len, cx + len, cx - len};
    f.y = {cy - 16.0f, cy - 16.0f, cy + 16.0f, cy + 16.0f};
    for (std::size_t k = 0; k < 4; ++k) {
        const double rx = static_cast<double>(f.x[k]) - cx, ry = static_cast<double>(f.y[k]) - cy;
        const auto a = static_cast<float>(std::atan2(ry, rx));
        const double r = std::sqrt(rx * rx + ry * ry);
        const double turned = static_cast<double>(a) + (static_cast<double>(1.57f) - phi);
        f.x[k] = static_cast<float>(std::sin(turned) * r + cx);
        f.y[k] = static_cast<float>(std::cos(turned) * r + cy);
    }
    f.px = static_cast<float>(px), f.py = static_cast<float>(py);
    return f;
}


void build(Water& w, const MapData& m) {
    w.built = true;
    const int gw = 2 * m.width, gh = m.height;
    w.cell_type.assign(static_cast<std::size_t>(gw) * static_cast<std::size_t>(std::max(gh, 0)), 1);
    std::array<int, 2> stale{0, 0};  // the crossing point when none is found (the original's stale locals)
    for (int gy = 0; gy < gh; ++gy) {
        for (int gx = 0; gx < gw; ++gx) {
            const std::size_t gi = static_cast<std::size_t>(gy) * gw + gx;
            if (gi >= m.obstacle_grid.size()) continue;
            const std::uint16_t v = m.obstacle_grid[gi];
            if (v == 0) continue;
            if ((v & 0x8000) == 0) {
                w.cell_type[gi] = static_cast<std::uint8_t>(v & 0xf);

                if ((v & 0x70) != 0) w.spots.push_back({gx * 48 + 24, gy * 24 + 12, ((v >> 4) & 7) - 1});
                continue;
            }
            const std::size_t r = v & 0x7fff;
            if (r >= m.routes.size()) continue;
            w.cell_type[gi] = static_cast<std::uint8_t>(m.routes[r].v & 0xf);
            const auto& polys = m.routes[r].lists;
            for (std::size_t pm = 0; pm < polys.size(); ++pm) {
                const std::vector<std::uint16_t>& poly = polys[pm];
                const std::size_t n = poly.size();
                for (std::size_t e = 0; e < n; ++e) {
                    std::array<int, 2> p{poly[e] & 0xff, poly[e] >> 8};
                    std::array<int, 2> q{poly[(e + 1) % n] & 0xff, poly[(e + 1) % n] >> 8};
                    // An end inside another polygon of the block (or on its edge): the edge is cut where it leaves
                    // that polygon; both ends inside one: no foam.
                    int flag = 0;
                    bool cut = false;
                    for (std::size_t o = 0; o < polys.size() && !cut; ++o) {
                        if (o == pm) continue;
                        if (point_in_polygon(p[0], p[1], polys[o])) flag = 1;
                        if (point_in_polygon(q[0], q[1], polys[o])) flag += 2;
                        if (flag != 1 && flag != 2) continue;
                        const std::vector<std::uint16_t>& other = polys[o];
                        for (std::size_t k = 0; k < other.size(); ++k) {
                            const std::array<int, 2> a{other[k] & 0xff, other[k] >> 8};
                            const std::uint16_t nb = other[(k + 1) % other.size()];
                            const std::array<int, 2> b{nb & 0xff, nb >> 8};
                            if (segments_cross(p, q, a, b, &stale)) break;
                        }
                        (flag == 1 ? p : q) = stale;
                        cut = true;
                    }
                    if (!cut && flag != 0) continue;
                    // Only an edge with an end strictly inside the block: the block's own border has no foam.
                    auto inner = [](const std::array<int, 2>& c) {
                        return c[0] != 48 && c[0] != 0 && c[1] != 24 && c[1] != 0;
                    };
                    if (!inner(p) && !inner(q)) continue;
                    w.foam_quads.push_back(foam_quad(p, q, gx * 48, gy * 24));
                }
            }
        }
    }
}

// The light spots of one 256 x 256 world tile added up (empty: none reaches it).
std::vector<float> spot_tile(const Water& w, int tx, int ty) {
    std::vector<float> out;
    const int x0 = tx * kSpotTile, y0 = ty * kSpotTile;
    for (const Water::Spot& s : w.spots) {
        if (s.pic < 0 || s.pic >= static_cast<int>(w.pics.spots.size())) continue;
        const Texture& t = w.pics.spots[static_cast<std::size_t>(s.pic)];
        if (t.width == 0) continue;
        // The quad reaches the texture's own size to either side of the centre (2W x 2H), so a texel covers 2 x 2.
        const int qx = s.cx - t.width, qy = s.cy - t.height;
        const int ax = std::max(qx, x0), bx = std::min(qx + 2 * t.width, x0 + kSpotTile);
        const int ay = std::max(qy, y0), by = std::min(qy + 2 * t.height, y0 + kSpotTile);
        if (ax >= bx || ay >= by) continue;
        if (out.empty()) out.assign(static_cast<std::size_t>(kSpotTile) * kSpotTile * 3, 0.0f);
        for (int y = ay; y < by; ++y) {
            const int ty_ = (y - qy) / 2;
            for (int x = ax; x < bx; ++x) {
                const std::uint8_t* p =
                    &t.rgba[(static_cast<std::size_t>(ty_) * static_cast<std::size_t>(t.width) + (x - qx) / 2) * 4];
                if (p[3] == 0) continue;
                const float a = p[3] / 15.0f * kTint;
                float* d = &out[(static_cast<std::size_t>(y - y0) * kSpotTile + (x - x0)) * 3];
                for (std::size_t ch = 0; ch < 3; ++ch) d[ch] += p[ch] / 15.0f * a;
            }
        }
    }
    return out;
}


void anim_step(float& frame, float speed, std::size_t count) {
    frame += speed;
    if (!(frame < static_cast<float>(count))) frame = 0;
}


std::array<float, 2> turn(float x, float y, float a) {
    const double sn = std::sin(static_cast<double>(a)), cs = std::cos(static_cast<double>(a));
    return {static_cast<float>(cs * x - sn * y), static_cast<float>(sn * x + cs * y)};
}



struct Placed {
    float x = 0, y = 0;
    std::array<float, 4> qx{}, qy{};
};
Placed wave_place(const WaterPics::Set& set, float x, float y, float angle, bool mirror) {
    Placed p;
    p.x = x, p.y = y;
    const int gx = set.w / 2 - set.ax, gy = set.h / 2 - set.ay;
    const auto ix = static_cast<int>(static_cast<double>(x) + gx), iy = static_cast<int>(static_cast<double>(y) + gy);
    const float x0 = static_cast<float>(ix) - static_cast<float>(set.w / 2);
    const float y0 = static_cast<float>(iy) - static_cast<float>(set.h / 2);
    const float x1 = static_cast<float>(set.w) + x0, y1 = static_cast<float>(set.h) + y0;
    p.qx = {x0, x1, x0, x1};
    p.qy = {y0, y0, y1, y1};
    const double sn = std::sin(static_cast<double>(angle)), cs = std::cos(static_cast<double>(angle));
    const double d = (1.0 - std::sqrt(sn * sn * 0.25 + cs * cs)) * set.w * 0.5 + 32.0;
    p.qx[1] = static_cast<float>(p.qx[1] - d);
    p.qx[3] = static_cast<float>(p.qx[3] - d);
    if (mirror) {
        for (float& v : p.qy) v -= static_cast<float>(set.h);
    }
    return p;
}




void wave_draw(Water& w, Water::Wave& wave, const WaterPics::Set& set, float speed, const Placed& p, bool mirror,
               int vx, int vy, int vw, int vh) {
    if (set.frames.empty()) return;
    anim_step(wave.frame, speed, set.frames.size());
    const auto fvx = static_cast<float>(vx), fvy = static_cast<float>(vy);
    const auto rx = static_cast<float>(vx + vw), ry = static_cast<float>(vy + vh);
    bool visible = false;
    for (std::size_t k = 0; k < 4; ++k) {
        visible = visible || (p.qx[k] <= rx && p.qy[k] <= ry && fvx <= p.qx[k] && fvy <= p.qy[k]);
    }
    if (!visible) return;
    if (!wave.done) {
        const float f = wave.size - wave.rate;
        wave.size = f;
        if (f <= 0.0f) {
            wave.size = 0, wave.done = true;
        } else if (f >= 1.0f) {
            wave.size = 1, wave.done = true;
        }
    }
    const auto fi = static_cast<std::size_t>(static_cast<int>(wave.frame));
    if (fi >= set.frames.size()) return;
    Water::WaveQuad q;
    q.tex = &set.frames[fi];
    std::array<float, 4> x = {p.qx[0], 0, p.qx[2], 0}, y = p.qy;
    x[1] = x[3] = static_cast<float>((p.qx[1] - p.qx[0]) * wave.size + p.qx[0]);
    const float h = p.qy[2] - p.qy[0];
    q.v = {0, 0, 0.99999f, 0.99999f};
    if (mirror) {
        q.v = {0.99999f, 0.99999f, 0, 0};
        y[0] = y[1] = static_cast<float>(p.qy[2] - h * wave.size);
    } else {
        float d = static_cast<float>(h - h * wave.size);
        if (wave.half) {
            d = static_cast<float>(d * 0.5);
            y[0] = p.qy[0] + d;
            y[1] = p.qy[1] + d;
        }
        y[2] -= d;
        y[3] -= d;
    }
    const auto sn = static_cast<float>(std::sin(static_cast<double>(wave.angle)));
    const auto cs = static_cast<float>(std::cos(static_cast<double>(wave.angle)));
    const double cx = static_cast<double>(p.x) - vx, cy = static_cast<double>(p.y) - vy;
    for (std::size_t k = 0; k < 4; ++k) {
        const double ox = static_cast<double>(x[k]) - p.x, oy = static_cast<double>(y[k]) - p.y;
        const auto sx = static_cast<int>(ox * cs - oy * sn + cx);  // ftol in screen pixels
        const auto sy = static_cast<int>(ox * sn + oy * cs + cy);
        q.x[k] = static_cast<float>(sx + vx);
        q.y[k] = static_cast<float>(sy + vy);
    }
    w.wave_quads.push_back(q);
}



void ship_wake(Water& w, Water::Wake& wk, const sim::ShipGraphic& g, const sim::Unit& u, int vx, int vy, int vw,
               int vh) {
    const int f = g.frames, o = static_cast<std::int8_t>(u.orient);
    const auto frame = static_cast<std::uint8_t>(static_cast<std::int64_t>((u.angle - o * kQuarterPi) * f * kFourByPi));
    const auto hq = static_cast<float>(static_cast<double>(f * o + frame) / f * kQuarterPi);
    const auto t = static_cast<float>(static_cast<double>(hq) + kQuarterPi);
    const auto a = static_cast<float>(
        -(std::atan2(std::cos(static_cast<double>(hq)), -2.0 * std::sin(static_cast<double>(hq))) + kPi));
    const float yz = u.y - u.z;
    auto point = [&](std::array<float, 3> h) {
        const std::array<float, 2> r = turn(h[0], h[1], t);
        const auto lift = static_cast<float>(static_cast<double>(r[1] + r[0]) * 0.5 + h[2]);
        return std::array<float, 2>{static_cast<float>(static_cast<double>(r[0] - r[1]) * kCos30 + u.x), yz - lift};
    };
    std::array<float, 3> bow = g.hull[0];
    bow[0] = bow[0] + 5.0f;
    const std::array<float, 2> b = point(bow);
    const WaterPics& pics = w.pics;
    Placed p = wave_place(pics.bow, b[0], b[1], wk.bow.angle, false);
    wk.bow.angle = a;
    wave_draw(w, wk.bow, pics.bow, 0.2f, p, false, vx, vy, vw, vh);
    p = wave_place(pics.bow, b[0], b[1], a, true);
    wave_draw(w, wk.bow, pics.bow, 0.2f, p, true, vx, vy, vw, vh);
    const std::array<float, 2> s = point(g.hull[1]);
    p = wave_place(pics.stern, s[0], s[1], wk.stern.angle, false);
    wk.stern.angle = a;
    wave_draw(w, wk.stern, pics.stern, 0.3f, p, false, vx, vy, vw, vh);
}



const std::optional<std::array<std::array<float, 3>, 2>>& text_points(Water& w, const GameFileLoader& load,
                                                                      const std::string& path) {
    auto it = w.text_points.find(path);
    if (it != w.text_points.end()) return it->second;
    std::optional<std::array<std::array<float, 3>, 2>> pts;
    std::string err;
    auto file = load(path, &err);
    auto table = file ? parse_les(*file, &err) : std::nullopt;
    const LesEntry* e = table ? find_entry_id(*table, 0xff) : nullptr;
    auto bytes = e != nullptr ? read_entry(*file, *e, &err) : std::nullopt;
    const std::vector<std::string> t = bytes ? tokens(*bytes) : std::vector<std::string>{};
    if (t.size() >= 8) {
        pts.emplace();
        for (std::size_t k = 0; k < 2; ++k) {
            std::array<float, 3>& v = (*pts)[k];
            for (std::size_t c = 0; c < 3; ++c) {
                const auto f = static_cast<float>(std::atof(t[2 + 3 * k + c].c_str()));
                v[c] = static_cast<float>(static_cast<double>(f) * static_cast<double>(3.4105608f));
            }
            const std::array<float, 2> r = turn(v[0], v[1], 0.7853982f);
            v[0] = r[0], v[1] = r[1];
        }
    } else {
        w.pics.warnings.push_back(path + ": no sinking text (entry 0xff)");
    }
    return w.text_points.emplace(path, pts).first->second;
}



void sink_add(Water& w, const WorldGraphics& g, const GameFileLoader& load, const sim::SimEvent& e) {
    const std::string path = unit_graphic_path(g, e.nation % 6, e.type % 40);
    if (path.empty()) return;
    const auto& pts = text_points(w, load, path);
    if (!pts) return;
    const auto a = static_cast<float>(static_cast<float>(e.sink.orient) * static_cast<double>(0.7853982f) +
                                      static_cast<double>(9.9999997e-05f));
    auto screen = [&](const std::array<float, 3>& v) {
        const std::array<float, 2> r = turn(v[0], v[1], a);
        const float dx = r[0] - r[1];
        const auto lift = static_cast<float>(static_cast<double>(r[1] + r[0]) * 0.5 + v[2]);
        return std::array<float, 2>{static_cast<float>(dx * kCos30 + e.x), -lift + e.y};
    };
    const std::array<float, 2> p0 = screen((*pts)[0]), p1 = screen((*pts)[1]);
    Water::Sink s;
    s.x = p0[0], s.y = p0[1], s.z = 0;
    s.vx = (p0[0] - p1[0]) * 0.25f, s.vy = (p0[1] - p1[1]) * 0.25f, s.vz = 0;
    const auto ang =
        static_cast<float>(-std::atan2(std::cos(static_cast<double>(a)), -2.0 * std::sin(static_cast<double>(a))));
    for (const sim::SimEvent::SinkArgs::Bubble& b : e.sink.bubbles) {
        const std::array<float, 2> r = turn(static_cast<float>(b.a), static_cast<float>(b.b), ang);
        s.bubbles.push_back({r[0], r[1], b.delay, 0.0f});
    }
    w.sinks.push_back(std::move(s));
}


void sink_step(Water::Sink& s, std::size_t frames) {
    if (s.life == 0) return;
    if (s.life % 20 == 0) s.x += s.vx, s.y += s.vy, s.z += s.vz;
    --s.life;
    for (Water::Bubble& b : s.bubbles) {
        if (b.delay != 0) {
            --b.delay;
            continue;
        }
        if (frames != 0) anim_step(b.frame, 0.4f, frames);
    }
}

}  // namespace

void water_tick(Water& w, const MapData& map, const WorldGraphics& g, const GameFileLoader& load,
                const sim::GameState& s, const std::vector<sim::SimEvent>& events, ViewPos pos, int vw, int vh,
                const std::vector<bool>* ship_slots) {
    if (!w.pics.tried) load_pics(w.pics, load);
    if (!w.built) build(w, map);

    if (w.steps > 0) {
        w.anim += 0.5f;
        if (w.anim >= 16.0f) w.anim = 0;
        w.foam += 0.4f;
        if (w.foam >= 16.0f) w.foam = 0;
    }
    ++w.steps;

    // The bubbles: a group whose life ran out went after its last drawing; the step's new ones; one step of each.
    w.sinks.erase(std::remove_if(w.sinks.begin(), w.sinks.end(), [](const Water::Sink& k) { return k.life < 1; }),
                  w.sinks.end());
    for (const sim::SimEvent& e : events) {
        if (e.kind == sim::SimEvent::Kind::ShipSink) sink_add(w, g, load, e);
    }
    for (Water::Sink& k : w.sinks) sink_step(k, w.pics.bubbles.frames.size());


    for (const sim::SimEvent& e : events) {
        if (e.kind != sim::SimEvent::Kind::ShipWake) continue;
        Water::Wake& wk = w.wakes[{e.player, e.serial}];
        const float rate = e.flag ? -0.02f : 0.04f;
        wk.bow.done = false, wk.bow.half = false, wk.bow.rate = rate;
        wk.stern.done = false, wk.stern.half = true, wk.stern.rate = rate;
    }
    w.wave_quads.clear();
    const int vx = view_x(pos), vy = view_y(pos);
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        const sim::Player& pl = s.players[p];
        // Only ships make waves: when no slot (in memory order, free ones too) holds a ship type, the list holds no
        // ship and its walk (slow on long lists, docs/bugs.md B59) is skipped.
        if (ship_slots != nullptr && p < ship_slots->size()) {
            if (!(*ship_slots)[p]) continue;
        } else {
            bool any = false;
            for (const sim::Unit& u : pl.unit_slots) {
                if (s.unit_types[sim::Unit_Nation(s, static_cast<std::uint8_t>(p), u) % 6][u.type % 40].is_ship != 0) {
                    any = true;
                    break;
                }
            }
            if (!any) continue;
        }
        for (std::uint32_t k = pl.first_unit; k != sim::kNoSlot; k = pl.unit_slots[k].next) {
            const sim::Unit& u = pl.unit_slots[k];
            const int type = u.type % 40;
            const int nation = sim::Unit_Nation(s, static_cast<std::uint8_t>(p), u) % 6;  // TUnit::Basic
            const sim::UnitType& t = s.unit_types[static_cast<std::size_t>(nation)][static_cast<std::size_t>(type)];
            if (t.is_ship == 0) continue;
            Water::Wake& wk = w.wakes[{static_cast<std::uint8_t>(p), u.serial}];
            wk.seen = w.steps;
            if (u.is_deleted != 0 || t.ship.frames == 0 || t.ship.hull.size() < 2) continue;
            if (!unit_in_view(g, nation, type, u.x, u.y, u.z + u.field_024, pos, vw, vh)) continue;
            ship_wake(w, wk, t.ship, u, vx, vy, vw, vh);
        }
    }
    for (auto it = w.wakes.begin(); it != w.wakes.end();) {
        it = it->second.seen == w.steps ? std::next(it) : w.wakes.erase(it);
    }
}

void water_draw(Water& w, const MapData& map, ViewPos pos, Target& rt) {
    (void)map;
    if (!w.built || w.pics.anim.empty()) return;
    const int vx = view_x(pos), vy = view_y(pos);
    const Texture& surface = w.pics.anim[std::min(static_cast<std::size_t>(w.anim), w.pics.anim.size() - 1)];
    const int gw = static_cast<int>(w.cell_type.size()) / std::max(1, map.height);
    // The light spots' tiles under the view, made before the bands share them; the others more than a tile away go.
    const int tx0 = floor_div(vx, kSpotTile), tx1 = floor_div(vx + rt.width - 1, kSpotTile);
    const int ty0 = floor_div(vy, kSpotTile), ty1 = floor_div(vy + rt.height - 1, kSpotTile);
    if (!w.spots.empty()) {
        for (auto it = w.spot_tiles.begin(); it != w.spot_tiles.end();) {
            const auto& [tx, ty] = it->first;
            const bool near = tx >= tx0 - 1 && tx <= tx1 + 1 && ty >= ty0 - 1 && ty <= ty1 + 1;
            it = near ? std::next(it) : w.spot_tiles.erase(it);
        }
        for (int ty = ty0; ty <= ty1; ++ty) {
            for (int tx = tx0; tx <= tx1; ++tx) {
                if (!w.spot_tiles.count({tx, ty}))
                    w.spot_tiles.emplace(std::array<int, 2>{tx, ty}, spot_tile(w, tx, ty));
            }
        }
    }
    const std::size_t foam_frames = w.pics.foam.size();
    const WaterPics::Set& bubbles = w.pics.bubbles;
    const std::array<float, 3> tint{kTint, kTint, kTint};
    parallel_bands(rt.height, [&](int top, int bottom) {
        // 1. The surface (ZFUNC EQUAL: the water's own pixels): the frame of the timer, three repeats across 27 blocks
        // and down 43 rows, upside down, twice as fine on a river block (type 2); times 0xe5.
        for (int y = top; y < bottom; ++y) {
            const int wy = y + vy, gy = floor_div(wy, 24);
            const std::vector<float>* tile = nullptr;
            int tile_x = std::numeric_limits<int>::min();
            for (int x = 0; x < rt.width; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(rt.width) + x;
                if (!rt.water[i]) continue;
                const int wx = x + vx, gx = floor_div(wx, 48);
                int type = 1;
                if (gy >= 0 && gy < map.height && gx >= 0 && gx < gw) {
                    type = w.cell_type[static_cast<std::size_t>(gy) * gw + gx];
                }
                const double k = type == 2 ? 2.0 : 1.0;
                const double u = wx / 48.0 * kScaleU * k, v = wy / 24.0 * kScaleV * k;
                const std::array<float, 4> t = texture_texel(surface, u - std::floor(u), v - std::floor(v));
                float* d = &rt.rgb[i * 3];
                for (std::size_t ch = 0; ch < 3; ++ch) d[ch] = t[ch] * kTint;
                // 2. The light spots (DESTBLEND ONE): added.
                if (w.spots.empty()) continue;
                const int tx = floor_div(wx, kSpotTile);
                if (tx != tile_x) {
                    tile_x = tx;
                    const auto it = w.spot_tiles.find({tx, floor_div(wy, kSpotTile)});
                    tile = it != w.spot_tiles.end() && !it->second.empty() ? &it->second : nullptr;
                }
                if (tile == nullptr) continue;
                const float* a =
                    &(*tile)[(static_cast<std::size_t>(wy - floor_div(wy, kSpotTile) * kSpotTile) * kSpotTile +
                              (wx - tx * kSpotTile)) *
                             3];
                for (std::size_t ch = 0; ch < 3; ++ch) d[ch] += a[ch];
            }
        }
        // 3. The coast foam: alpha blended, LESSEQUAL on the depth (water and the low shore), no z write.
        if (foam_frames != 0) {
            DrawState st;
            st.blend = true;
            st.z_write = false;
            st.tint = tint;
            st.clip_top = top, st.clip_bottom = bottom;
            for (const Water::Foam& f : w.foam_quads) {
                const float x0 = std::min({f.x[0], f.x[1], f.x[2], f.x[3]}) - vx;
                const float x1 = std::max({f.x[0], f.x[1], f.x[2], f.x[3]}) - vx;
                const float y0 = std::min({f.y[0], f.y[1], f.y[2], f.y[3]}) - vy;
                const float y1 = std::max({f.y[0], f.y[1], f.y[2], f.y[3]}) - vy;
                if (x1 < 0 || x0 >= rt.width || y1 < top || y0 >= bottom) continue;
                const int fr = static_cast<int>(w.foam + 0.05f * std::sqrt(f.px * f.px + f.py * f.py)) % 16;
                st.texture = &w.pics.foam[static_cast<std::size_t>(fr) % foam_frames];
                std::array<Vertex, 4> q;
                const std::array<double, 4> us = {0, 0.99999, 0, 0.99999}, vs = {0, 0, 0.99999, 0.99999};
                for (std::size_t k = 0; k < 4; ++k) {
                    const double sx = static_cast<double>(f.x[k]) - vx, sy = static_cast<double>(f.y[k]) - vy;
                    q[k] = Vertex{sx, sy, sy + 110.0, us[k], vs[k]};
                }
                draw_quad(rt, q, st);
            }
        }

        if (!bubbles.frames.empty()) {
            const float hw = static_cast<float>(bubbles.w) * 0.5f, hh = static_cast<float>(bubbles.h) * 0.5f;
            for (const Water::Sink& k : w.sinks) {
                const float bx = k.x - (static_cast<float>(vx) + hw), by = k.y - (static_cast<float>(vy) + hh);
                for (const Water::Bubble& b : k.bubbles) {
                    const auto fi = static_cast<std::size_t>(static_cast<int>(b.frame) & 0xff);
                    if (fi >= bubbles.frames.size()) continue;
                    const Texture& t = bubbles.frames[fi];
                    const auto x0 = static_cast<int>(static_cast<double>(bx) + b.x);
                    const auto y0 = static_cast<int>(static_cast<double>(by) + b.y);
                    for (int y = std::max(y0, top); y < std::min(y0 + bubbles.h, bottom); ++y) {
                        for (int x = std::max(x0, 0); x < std::min(x0 + bubbles.w, rt.width); ++x) {
                            const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(rt.width) + x;
                            if (!rt.water[i]) continue;
                            const std::array<float, 4> c =
                                texture_texel(t, (x - x0 + 0.5) / bubbles.w, (y - y0 + 0.5) / bubbles.h);
                            if (c[3] <= 0) continue;
                            float* d = &rt.rgb[i * 3];
                            for (std::size_t ch = 0; ch < 3; ++ch) d[ch] = c[ch] * kTint * c[3] + d[ch] * (1 - c[3]);
                        }
                    }
                }
            }
        }

        for (const Water::WaveQuad& q : w.wave_quads) {
            const Texture& t = *q.tex;
            auto sample = [&t](double u, double v) { return texture_texel(t, u, v); };
            std::array<WaterVertex, 4> v;
            const std::array<double, 4> us = {0, 0.99999, 0, 0.99999};
            for (std::size_t k = 0; k < 4; ++k) {
                v[k] = {static_cast<double>(q.x[k]) - vx, static_cast<double>(q.y[k]) - vy, us[k], q.v[k]};
            }
            water_triangle(rt, sample, tint, kWaveAlpha, v[0], v[1], v[2], top, bottom);
            water_triangle(rt, sample, tint, kWaveAlpha, v[1], v[2], v[3], top, bottom);
        }
    });
}

}  // namespace antanta
