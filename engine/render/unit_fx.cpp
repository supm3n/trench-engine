#include "render/unit_fx.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <sstream>

#include "formats/les.hpp"
#include "formats/params.hpp"
#include "render/effects_emit.hpp"
#include "render/ground_fx.hpp"
#include "render/live.hpp"
#include "sim/movement.hpp"

namespace antanta {
namespace {

constexpr float kTint = 229.0f / 255.0f;
constexpr double kSin60 = 0.8660253882408142;
constexpr float kQuarterPiF = 0.7853981852531433f;


float AddAngle(float a, float b) {
    double r = static_cast<double>(a) + static_cast<double>(b);
    if (r < 0.0) {
        r += 6.2831854820251465;
    } else if (r >= 6.2831854820251465) {
        r -= 6.2831854820251465;
    }
    if (r >= 6.2821855545043945) return 0.0f;
    if (r < 0.0) return 6.281185626983643f;
    return static_cast<float>(r);
}

struct P3 {
    float x = 0, y = 0, z = 0;
};


P3 Turn(P3 p, float a) {
    const double sn = std::sin(static_cast<double>(a)), cs = std::cos(static_cast<double>(a));
    const double x = p.x, y = p.y;
    return {static_cast<float>(cs * x - sn * y), static_cast<float>(x * sn + cs * y), p.z};
}


std::array<float, 2> Project(const P3& p) {
    return {static_cast<float>((static_cast<double>(p.x) - p.y) * kSin60),
            static_cast<float>(-((static_cast<double>(p.x) + p.y) * 0.5 + p.z))};
}



bool Step(ShotAnim& a, int count) {
    if (a.done) return false;
    a.frame = a.frame + 1.0f;
    if (a.frame < static_cast<float>(a.end) && a.frame < static_cast<float>(count)) return true;
    a.frame = 0.0f;
    if (a.loops != 0 && --a.loops == 0) {
        a.done = true;
        return false;
    }
    return true;
}



void Place(ShotAnim& a, const UnitFxGraphics::ShotSet& set, int index, float px, float py, std::int16_t z,
           float angle) {
    a.set = index;
    a.end = static_cast<std::uint16_t>(set.count);
    a.px = px, a.py = py;
    const int cx = sim::Ftol(static_cast<double>(set.dx) + px), cy = sim::Ftol(static_cast<double>(set.dy) + py);
    const auto x0 = static_cast<float>(cx - (set.w >> 1)), y0 = static_cast<float>(cy - (set.h >> 1));
    const auto x1 = static_cast<float>(static_cast<double>(set.w) + x0);
    const auto y1 = static_cast<float>(static_cast<double>(set.h) + y0);
    a.x = {x0, x1, x0, x1};
    a.y = {y0, y0, y1, y1};
    a.z.fill(static_cast<float>(z));
    a.done = false, a.loops = 1, a.angle = angle, a.frame = 0.0f, a.shown = false;
}


std::array<int, 2> FlashSets(const sim::UnitType& t, std::uint16_t type, std::uint8_t heavy) {
    if (t.move_class == 9) return {6, 4};                      // flamethrower
    if (t.move_class == 10) return {type == 0x17 ? 3 : 2, 4};  // mortars and howitzers
    if (t.move_class == 16) return {-1, -1};
    if (type == 0x12) return heavy == 1 ? std::array<int, 2>{0, 4} : std::array<int, 2>{-1, -1};
    return heavy == 0 ? std::array<int, 2>{0, 4} : std::array<int, 2>{1, 5};
}

const UnitFxGraphics::ShotSet* SetOf(const UnitFxGraphics& ug, int set) {
    if (set < 0 || set >= kShotSets) return nullptr;
    const UnitFxGraphics::ShotSet& s = ug.shots[static_cast<std::size_t>(set)];
    return s.count > 0 ? &s : nullptr;
}



bool Drawn(const WorldGraphics& g, const sim::GameState& s, const sim::UnitRef& r, ViewPos pos, int w, int h) {
    const sim::Unit* u = sim::Unit_Find(s, r);
    if (u == nullptr || r.player >= s.players.size()) return false;
    const int nation = sim::Unit_Nation(s, r.player, *u) % 6;  // TUnit::Basic
    if (!unit_in_view(g, nation, u->type, u->x, u->y, u->z + u->field_024, pos, w, h)) return false;
    return s.unit_types[static_cast<std::size_t>(nation)][u->type % 40].is_ship != 0 ||
           unit_pose(s, r.player, *u).visible;
}

std::vector<std::string> Tokens(const Bytes& b) {
    std::istringstream in(std::string(b.begin(), b.end()));
    std::vector<std::string> out;
    for (std::string t; in >> t;) out.push_back(t);
    return out;
}

}  // namespace

void shot_set_load(UnitFxGraphics& g, int set, const GameFileLoader& load) {
    const std::string dir = "Data/Misc/Shots/";
    if (!g.listed) {
        g.listed = true;
        std::string err;
        if (auto f = load(dir + "Info.txt", &err)) {
            const std::vector<std::string> t = Tokens(*f);
            const int n = t.empty() ? 0 : std::atoi(t[0].c_str());
            for (int i = 1; i <= n && i < static_cast<int>(t.size()); ++i)
                g.names.push_back(t[static_cast<std::size_t>(i)]);
        } else {
            g.warnings.push_back(dir + "Info.txt: " + err);
        }
    }
    if (set < 0 || set >= kShotSets) return;
    UnitFxGraphics::ShotSet& s = g.shots[static_cast<std::size_t>(set)];
    if (s.tried) return;
    s.tried = true;
    if (set >= static_cast<int>(g.names.size())) return;
    const std::string path = dir + g.names[static_cast<std::size_t>(set)];
    std::string err;
    auto file = load(path, &err);
    auto table = file ? parse_les(*file, &err) : std::nullopt;
    if (!table) {
        g.warnings.push_back(path + ": " + err);
        return;
    }

    const LesEntry* info = find_entry(*table, "Info.txt");
    const int count = static_cast<int>(table->entries.size()) - (info != nullptr ? 1 : 0);
    s.frames.resize(static_cast<std::size_t>(std::max(0, count)));
    for (int i = 0; i < count; ++i) {
        const LesEntry* e = find_entry_id(*table, static_cast<std::uint16_t>(i));
        if (e == nullptr) continue;
        auto b = read_entry(*file, *e, &err);
        auto img = b ? decode_444(*b, &err) : std::nullopt;
        if (!img) {
            g.warnings.push_back(path + " entry " + std::to_string(i) + ": " + err);
            continue;
        }
        if (i == 0) s.w = img->width, s.h = img->height;
        s.frames[static_cast<std::size_t>(i)] = texture_from_image(*img);
    }
    s.count = count;
    if (info != nullptr) {  // two numbers a, b: dx = ftol(w * 0.5 - a), dy = ftol(h * 0.5 - b)
        if (auto b = read_entry(*file, *info, &err)) {
            const std::vector<std::string> t = Tokens(*b);
            const auto a0 = static_cast<std::int16_t>(t.size() > 0 ? std::atol(t[0].c_str()) : 0);
            const auto a1 = static_cast<std::int16_t>(t.size() > 1 ? std::atol(t[1].c_str()) : 0);
            s.dx = static_cast<std::int16_t>(sim::Ftol(static_cast<double>(s.w) * 0.5 - a0));
            s.dy = static_cast<std::int16_t>(sim::Ftol(static_cast<double>(s.h) * 0.5 - a1));
        }
    }
}

void unit_fx_stills_load(UnitFxGraphics& g, const GameFileLoader& load) {
    if (g.stills_tried) return;
    g.stills_tried = true;
    std::string err;
    if (auto f = load("Data/Misc/UnitFlash.444", &err)) {
        if (auto img = decode_444(*f, &err)) g.rifle = texture_from_image(*img);
    }
    if (!g.rifle) g.warnings.push_back("Data/Misc/UnitFlash.444: " + err);
    const std::string path = "Data/Misc/BurntStains.les";
    auto file = load(path, &err);
    auto table = file ? parse_les(*file, &err) : std::nullopt;
    if (!table) {
        g.warnings.push_back(path + ": " + err);
        return;
    }
    for (std::uint16_t i = 0; i < 8; ++i) {
        const LesEntry* e = find_entry_id(*table, i);
        auto b = e ? read_entry(*file, *e, &err) : std::nullopt;
        auto img = b ? decode_444(*b, &err) : std::nullopt;
        if (img) {
            g.burnt[i] = texture_from_image(*img);
        } else {
            g.warnings.push_back(path + " id " + std::to_string(i) + ": " + (e ? err : "missing"));
        }
    }
}

void unit_fx_mute(const UnitFx& fx, std::vector<sim::SimEvent>& events) {
    for (std::size_t i = 1; i < events.size(); ++i) {
        const sim::SimEvent& ev = events[i];
        if (ev.kind != sim::SimEvent::Kind::Flash || events[i - 1].kind != sim::SimEvent::Kind::Sound) continue;
        const bool muted = std::any_of(fx.flashes.begin(), fx.flashes.end(), [&](const UnitFx::Flash& f) {
            return f.flag && f.unit.serial == ev.flash.serial && f.weapon == ev.flash.weapon &&
                   f.barrel == ev.flash.barrel;
        });
        if (muted) {
            events.erase(events.begin() + static_cast<std::ptrdiff_t>(i - 1));
            --i;
        }
    }
}

void unit_fx_tick(UnitFx& fx, UnitFxGraphics& ug, const GameFileLoader& load, const WorldGraphics& g,
                  const sim::GameState& s, const std::vector<sim::SimEvent>& events, ViewPos pos, int w, int h) {
    if (s.tick == fx.stepped_tick) return;
    fx.stepped_tick = s.tick;
    unit_fx_stills_load(ug, load);
    fx.rifle.clear();
    const auto vx = static_cast<float>(view_x(pos)), vy = static_cast<float>(view_y(pos));
    for (const sim::SimEvent& ev : events) {
        if (ev.kind == sim::SimEvent::Kind::RifleFlash) {
            fx.rifle.push_back(sim::UnitRef{ev.player, ev.id, ev.flash.serial});
            continue;
        }
        if (ev.kind != sim::SimEvent::Kind::Flash) continue;
        const sim::SimEvent::FlashArgs& a = ev.flash;
        auto it = std::find_if(fx.flashes.begin(), fx.flashes.end(), [&](const UnitFx::Flash& f) {
            return f.unit.serial == a.serial && f.weapon == a.weapon && f.barrel == a.barrel;
        });
        if (it != fx.flashes.end() && it->flag) continue;

        if (!unit_in_view(g, ev.nation, ev.type, ev.x, ev.y, a.z + a.alt, pos, w, h)) continue;
        if (it == fx.flashes.end()) {
            UnitFx::Flash f;
            f.unit = sim::UnitRef{ev.player, ev.id, a.serial}, f.weapon = a.weapon, f.barrel = a.barrel;
            fx.flashes.push_back(f);
            it = fx.flashes.end() - 1;
        }
        it->flag = true;
        if (a.barrel != 0) continue;
        const sim::UnitType& t = s.unit_types[ev.nation % 6][ev.type % 40];
        std::uint8_t heavy = 0;
        int frames = 0;
        if (a.ship != 0) {
            if (a.weapon >= t.ship.weapons.size()) continue;
            heavy = t.ship.weapons[a.weapon].slot;
            frames = t.ship.frames;
        } else {
            if (a.weapon >= t.weapons.size()) continue;
            heavy = t.weapons[a.weapon].field_48;
            frames = t.anim_size[5];
        }
        const std::array<int, 2> sets = FlashSets(t, ev.type, heavy);
        if (sets[0] < 0 || frames == 0) continue;
        for (int k : sets) shot_set_load(ug, k, load);
        // The angle on the turn animation's steps: aq = f32(ftol(angle / step) * step), step = (pi/4) / frames.
        const double step = static_cast<double>(kQuarterPiF) / static_cast<double>(frames);
        const auto aq = static_cast<float>(static_cast<double>(sim::Ftol(static_cast<double>(a.angle) / step)) * step);


        P3 p;
        if (a.ship != 0) {
            const sim::ShipWeaponType& sw = t.ship.weapons[a.weapon];
            const std::array<float, 3> pa = sw.points.empty() ? std::array<float, 3>{} : sw.points[0];
            p = {pa[0], pa[1], pa[2]};
            if (sw.slot != 0) {
                const P3 d = Turn({pa[0] - sw.pos[0], pa[1] - sw.pos[1], pa[2] - sw.pos[2]}, a.gun);
                p = {d.x + sw.pos[0], d.y + sw.pos[1], d.z + sw.pos[2]};
            }
            p = Turn(p, aq);
        } else {
            const std::array<float, 3>& pa = t.weapons[a.weapon].point_a;
            p = Turn({pa[0], pa[1], pa[2]}, aq);
        }
        const std::array<float, 2> pp = Project(p);
        const auto sx = static_cast<float>(static_cast<double>(ev.x) - vx);
        const auto sy = static_cast<float>((static_cast<double>(ev.y) - vy) - (static_cast<double>(a.z) + a.alt));
        const auto mx = static_cast<float>(static_cast<double>(sx) + pp[0]);
        const auto my = static_cast<float>(static_cast<double>(sy) + pp[1]);
        std::int16_t base = 0x37;
        float ang = 0;
        if (a.ship != 0) {  // the picture along the barrel: atan2(cos a, -2 sin a) - pi/2 with a = aq + the turret's
            const float abs = AddAngle(aq, a.gun);
            ang = static_cast<float>(
                std::atan2(std::cos(static_cast<double>(abs)), -2.0 * std::sin(static_cast<double>(abs))) -
                static_cast<double>(1.5707963705062866f));
        } else {  // from PointsB[0] (the barrel's back) to the muzzle on the screen
            const auto& params = g.params[static_cast<std::size_t>(ev.nation % 6)];
            P3 b;
            if (params && ev.type % 40 < params->units.size() &&
                a.weapon < params->units[ev.type % 40].weapons.size()) {
                const std::array<float, 3>& pb = params->units[ev.type % 40].weapons[a.weapon].point_b;
                b = Turn({pb[0], pb[1], pb[2]}, kQuarterPiF);
            }
            const std::array<float, 2> qp = Project(Turn(b, aq));
            const auto qx = static_cast<float>(static_cast<double>(sx) + qp[0]);
            const auto qy = static_cast<float>(static_cast<double>(sy) + qp[1]);
            base = static_cast<std::int16_t>(
                sim::Ftol((static_cast<double>(a.z) + a.z + (static_cast<double>(my) - qy)) + 55.0));
            ang = static_cast<float>(static_cast<double>(-1.5707963705062866f) -
                                     std::atan2(static_cast<double>(my) - qy, static_cast<double>(mx) - qx));
        }
        const auto px = static_cast<float>(static_cast<double>(vx) + mx);
        const auto py = static_cast<float>(static_cast<double>(vy) + my);

        it->before = a.angle < 1.5707963705062866f || a.angle >= 4.71238899230957f;
        for (int k = 0; k < 2; ++k) {
            if (const UnitFxGraphics::ShotSet* set = SetOf(ug, sets[static_cast<std::size_t>(k)])) {
                Place(it->obj[static_cast<std::size_t>(k)], *set, sets[static_cast<std::size_t>(k)], px, py, base,
                      -ang);
            }
        }
    }

    for (UnitFx::Flash& f : fx.flashes) {
        if (!f.flag || !Drawn(g, s, f.unit, pos, w, h)) continue;
        int done = 0;
        for (ShotAnim& o : f.obj) {
            const UnitFxGraphics::ShotSet* set = SetOf(ug, o.set);
            o.shown = Step(o, set != nullptr ? set->count : 0) && set != nullptr;
            if (o.done) ++done;
        }
        if (done == 2) f.flag = false;
    }
    std::erase_if(fx.flashes, [&s](const UnitFx::Flash& f) { return !f.flag || sim::Unit_Find(s, f.unit) == nullptr; });
}

void unit_fx_quads(const UnitFx& fx, const UnitFxGraphics& ug, const sim::GameState& s, ViewPos pos,
                   std::vector<UnitFxQuad>* out) {
    const auto vx = static_cast<float>(view_x(pos)), vy = static_cast<float>(view_y(pos));
    for (const UnitFx::Flash& f : fx.flashes) {
        for (const ShotAnim& o : f.obj) {
            const UnitFxGraphics::ShotSet* set = SetOf(ug, o.set);
            if (!o.shown || set == nullptr) continue;

            const Texture* tex = nullptr;
            for (int i = std::min(sim::Ftol(o.frame), set->count - 1); i >= 0 && tex == nullptr; --i) {
                const auto& fr = set->frames[static_cast<std::size_t>(i)];
                if (fr) tex = &*fr;
            }
            if (tex == nullptr) continue;

            UnitFxQuad q;
            q.serial = f.unit.serial;
            q.order = f.before ? UnitFxQuad::Order::WeaponBefore : UnitFxQuad::Order::WeaponAfter;
            q.texture = tex;
            q.tint = kTint;
            const auto sn = static_cast<float>(std::sin(static_cast<double>(o.angle)));
            const auto cs = static_cast<float>(std::cos(static_cast<double>(o.angle)));
            const auto cx = static_cast<float>(static_cast<double>(o.px) - vx);
            const auto cy = static_cast<float>(static_cast<double>(o.py) - vy);
            for (std::size_t k = 0; k < 4; ++k) {
                const auto dx = static_cast<float>((static_cast<double>(o.x[k]) - vx) - cx);
                const auto dy = static_cast<float>((static_cast<double>(o.y[k]) - vy) - cy);
                const auto x = static_cast<float>((static_cast<double>(dx) * cs - static_cast<double>(dy) * sn) + cx);
                const auto y = static_cast<float>(static_cast<double>(dx) * sn + static_cast<double>(dy) * cs + cy);
                const double key = static_cast<double>(y) + 2.0 * o.z[k] + (k < 2 ? set->h : 0);
                q.v[k] = Vertex{x, y, key, (k & 1) != 0 ? 0.99999 : 0.0, k >= 2 ? 0.99999 : 0.0};
            }
            out->push_back(q);
        }
    }

    if (!ug.rifle) return;
    for (const sim::UnitRef& r : fx.rifle) {
        const sim::Unit* u = sim::Unit_Find(s, r);
        if (u == nullptr) continue;
        const auto x0 = static_cast<float>(static_cast<double>(u->x) - vx - 24.0);
        const auto y0 = static_cast<float>(static_cast<float>(static_cast<double>(u->y) - vy - 12.0) - u->z);
        const double kb = (static_cast<double>(u->y) - vy + 12.0) + u->z;
        UnitFxQuad q;
        q.serial = r.serial;
        q.order = UnitFxQuad::Order::Rifle;
        q.texture = &*ug.rifle;
        q.tint = kTint;
        q.v = {Vertex{x0, y0, kb + 24, 0, 0}, Vertex{x0 + 48.0, y0, kb + 24, 0.99999, 0},
               Vertex{x0, y0 + 24.0, kb, 0, 0.99999}, Vertex{x0 + 48.0, y0 + 24.0, kb, 0.99999, 0.99999}};
        out->push_back(q);
    }
}

void burnt_stain_quads(const UnitFxGraphics& ug, const MapData* map, const sim::Unit& u, int anchor_x, ViewPos pos,
                       std::vector<UnitFxQuad>* out) {
    if (u.type != 0xb && u.type != 0xc) return;


    const std::size_t index = (u.type == 0xc ? 4u : 0u) + (u.orient & 3u);
    if (!ug.burnt[index]) return;
    const auto vx = static_cast<float>(view_x(pos)), vy = static_cast<float>(view_y(pos));
    const auto sx = static_cast<float>(static_cast<double>(u.x) - vx);
    const auto sy = static_cast<float>((static_cast<double>(u.y) - vy) - (static_cast<double>(u.z) + u.field_024));
    const int x0 = sim::Ftol(static_cast<double>(anchor_x) + (static_cast<double>(sx) - 64.0));
    const int y0 = sim::Ftol(static_cast<double>(sy) - 64.0);
    const float alpha = static_cast<float>(u.field_0ca >> 8) / 255.0f;
    // The original's corners are at depth 0, in front of everything (a house or a tree in front of the tank shows the
    // stain over it). Ours: 4 x 4 pieces whose corners lie on the ground under them (+ 5, as a crater's).
    constexpr int kPieces = 4, kSize = 128;
    auto key = [&](int px, int py) {
        const double wx = static_cast<double>(px) + vx, wy = static_cast<double>(py) + vy;
        const double gh = map != nullptr ? screen_ground_height(*map, wx, wy) : static_cast<double>(u.z);
        return static_cast<double>(py) + 2.0 * (gh + 5.0);
    };
    for (int j = 0; j < kPieces; ++j) {
        for (int i = 0; i < kPieces; ++i) {
            const int xa = x0 + i * kSize / kPieces, xb = x0 + (i + 1) * kSize / kPieces;
            const int ya = y0 + j * kSize / kPieces, yb = y0 + (j + 1) * kSize / kPieces;
            const double ua = static_cast<double>(i) / kPieces, ub = static_cast<double>(i + 1) / kPieces * 0.99999;
            const double va = static_cast<double>(j) / kPieces, vb = static_cast<double>(j + 1) / kPieces * 0.99999;
            UnitFxQuad q;
            q.serial = u.serial;
            q.order = UnitFxQuad::Order::Dead;
            q.texture = &*ug.burnt[index];
            q.alpha = alpha;
            q.v = {Vertex{static_cast<double>(xa), static_cast<double>(ya), key(xa, ya), ua, va},
                   Vertex{static_cast<double>(xb), static_cast<double>(ya), key(xb, ya), ub, va},
                   Vertex{static_cast<double>(xa), static_cast<double>(yb), key(xa, yb), ua, vb},
                   Vertex{static_cast<double>(xb), static_cast<double>(yb), key(xb, yb), ub, vb}};
            out->push_back(q);
        }
    }
}

void unit_fx_draw(const UnitFxQuad& q, float ox, float oy, Target& rt, int band_top, int band_bottom) {
    DrawState st;
    st.texture = q.texture;
    st.tint = {q.tint, q.tint, q.tint};
    st.alpha = q.alpha;
    st.z_write = false;
    st.blend = true;
    st.clip_top = band_top;
    st.clip_bottom = band_bottom;
    std::array<Vertex, 4> v = q.v;
    for (Vertex& p : v) p.x += ox, p.y += oy, p.key += oy;
    draw_quad(rt, v, st);
}

}  // namespace antanta
