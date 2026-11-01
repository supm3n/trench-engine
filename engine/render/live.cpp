#include "render/live.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

#include "formats/les.hpp"
#include "render/effects_emit.hpp"
#include "sim/prefetch.hpp"

namespace antanta {
namespace {

// Ships (MoveClass 13..15) have per-direction .shp parts instead of a .ugr; their drawing is not decoded
// (render.md, "Corrections from the C++ port").
bool is_ship_class(int move_class) { return move_class >= 13 && move_class <= 15; }

// One NumWork entry of the unit archive at `path` as frames (the .ugr layers 0 and, if flagged, 1).
UnitAnim load_anim(const GameFileLoader& load, const std::string& path, int num_work, std::vector<std::string>* warn) {
    UnitAnim a;
    std::string err;
    auto file = load(path, &err);
    if (!file) {
        warn->push_back(path + ": " + err);
        return a;
    }
    auto table = parse_les(*file, &err);
    if (!table) {
        warn->push_back(path + ": " + err);
        return a;
    }
    const LesEntry* e = find_entry_id(*table, static_cast<std::uint16_t>(num_work));
    if (e == nullptr) return a;  // the type has no such animation (infantry has no entry 3, "stay")
    auto bytes = read_entry(*file, *e, &err);
    auto ugr = bytes ? decode_ugr(*bytes, &err) : std::nullopt;
    if (!ugr || ugr->frames_per_dir <= 0 || ugr->layers.empty()) {
        warn->push_back(path + ": entry " + std::to_string(num_work) + ": " + err);
        return a;
    }
    a.frames_per_dir = ugr->frames_per_dir;
    auto take = [&](const std::vector<SpriteFrame>& layer, std::vector<UnitFrame>& out) {
        out.resize(layer.size());
        for (std::size_t i = 0; i < layer.size(); ++i) {
            if (layer[i].has_pixels)
                out[i] = {true, layer[i].off_x, layer[i].off_y, texture_from_image(layer[i].image)};
        }
    };
    take(ugr->layers[0], a.sprite);
    if ((ugr->flags & 2) && ugr->layers.size() > 1) take(ugr->layers[1], a.mask);
    const std::size_t shadow = (ugr->flags & 2) ? 2 : 1;  // layers in the order sprite, mask, shadow (formats.md)
    if ((ugr->flags & 1) && ugr->layers.size() > shadow) take(ugr->layers[shadow], a.shadow);
    a.ok = true;
    return a;
}

// ---- ships (docs/render.md "Ships") ----

// One entry of a .les archive by id; nullopt with a warning when the archive or the entry cannot be read.
std::optional<Bytes> les_entry(const GameFileLoader& load, const std::string& path, std::uint16_t id,
                               std::vector<std::string>* warn) {
    std::string err;
    auto file = load(path, &err);
    auto table = file ? parse_les(*file, &err) : std::nullopt;
    const LesEntry* e = table ? find_entry_id(*table, id) : nullptr;
    auto bytes = e != nullptr ? read_entry(*file, *e, &err) : std::nullopt;
    if (!bytes) warn->push_back(path + ": entry " + std::to_string(id) + ": " + (e == nullptr ? "missing" : err));
    return bytes;
}



bool read_ship_frame(Cursor& c, UnitFrame* f) {
    const std::uint16_t ox = c.u16(), oy = c.u16(), cols = c.u16(), rows = c.u16();
    c.skip(4);  // texture size
    const std::uint32_t size = c.u32();
    c.skip(4);
    if (!c.ok || size != static_cast<std::uint32_t>(cols) * rows * 2u) return false;
    const ByteSpan px = c.take(size);
    if (!c.ok) return false;
    *f = UnitFrame{cols > 0 && rows > 0, ox, oy, texture_from_image(decode_argb4444(px, cols, rows))};
    return true;
}



ShipEntry load_ship_entry(const GameFileLoader& load, const std::string& path, std::uint16_t id, int singles,
                          int groups, int trailing, std::vector<std::string>* warn) {
    ShipEntry e;
    auto bytes = les_entry(load, path, id, warn);
    if (!bytes) return e;
    Cursor c{*bytes};
    e.singles.resize(static_cast<std::size_t>(singles));
    for (UnitFrame& f : e.singles) {
        if (!read_ship_frame(c, &f)) break;
    }
    e.groups.resize(static_cast<std::size_t>(groups));
    for (auto& gr : e.groups) {
        gr.resize(c.u8());
        for (UnitFrame& f : gr) {
            if (!read_ship_frame(c, &f)) break;
        }
    }
    e.reflection.resize(static_cast<std::size_t>(trailing));
    for (UnitFrame& f : e.reflection) {
        if (!read_ship_frame(c, &f)) break;
    }
    e.ok = c.ok;
    if (!e.ok) warn->push_back(path + ": entry " + std::to_string(id) + " is cut off");
    return e;
}



ShipGraphics load_ship_graphics(const GameFileLoader& load, const std::string& path, std::vector<std::string>* warn) {
    ShipGraphics g;
    auto bytes = les_entry(load, path, 0, warn);
    if (!bytes) return g;
    Cursor c{*bytes};
    g.parts = c.u8();  // G+6
    const std::uint8_t turrets = c.u8();
    for (std::uint8_t i = 0; i < turrets; ++i) g.turret_steps.push_back(c.u8());
    for (int i = 0; i <= turrets; ++i) g.turret_base.push_back(c.u8());
    const std::uint8_t order = c.u8();                                            // G+7
    const std::uint8_t weapons = c.u8();
    for (std::uint8_t i = 0; i < weapons; ++i) {
        c.skip(12 + 4 + 24 + 1 + 1);
        c.skip(12u * c.u8());         // the barrel points
    }
    c.skip(13u * c.u8());      // records the loader reads and drops
    c.skip(12u * c.u8());
    const int smoke = c.u8();
    for (int i = 0; i < smoke; ++i) {
        std::array<float, 3> pt{};
        for (float& v : pt) v = static_cast<float>(static_cast<double>(c.f32()) * static_cast<double>(3.4105608f));
        g.smoke.push_back(pt);
    }
    g.frames = c.u8();
    g.launch_frames = c.u8();
    g.death_frames = c.u8();
    for (auto& o : g.order) {
        o.resize(order);
        for (std::uint8_t& b : o) b = c.u8();
    }
    g.ok = c.ok && g.frames > 0;
    if (!g.ok) warn->push_back(path + ": ship params.dat is cut off");
    return g;
}

ShipGraphics& ship_graphics(UnitSprites& sprites, const GameFileLoader& load, const std::string& path) {
    auto it = sprites.ships.find(path);
    if (it == sprites.ships.end())
        it = sprites.ships.emplace(path, load_ship_graphics(load, path, &sprites.warnings)).first;
    return it->second;
}




const ShipEntry* ship_entry(UnitSprites& sprites, const GameFileLoader& load, const std::string& path, ShipGraphics& g,
                            int id) {
    auto it = g.entries.find(id);
    if (it == g.entries.end()) {
        int singles = 0, groups = 1, trailing = 0;
        if (id >= 1 && id <= 8) {
            singles = (g.parts + 1) * g.frames, groups = g.frames, trailing = g.frames;
        } else if (id == 9 || id == 10) {
            groups = 2 * g.launch_frames;
        } else if (id >= 0xb && id <= 0x12) {
            groups = 2 * g.death_frames;
        }
        it = g.entries
                 .emplace(id, load_ship_entry(load, path, static_cast<std::uint16_t>(id), singles, groups, trailing,
                                              &sprites.warnings))
                 .first;
    }
    return it->second.ok ? &it->second : nullptr;
}







bool ship_in_dead_list(const sim::Unit& u) { return u.is_deleted != 0 && u.field_024 <= 1.0f; }

bool ship_slot_loaded(const sim::GameState& s, const sim::UnitType& t, int kind, int dir) {
    const std::size_t nation = t.graphic / 40u, type = t.graphic % 40u;
    if (nation >= 6 || type < 0x11 || type > 0x14 || dir < 0 || dir > 7) return false;
    std::size_t k = static_cast<std::size_t>(dir);
    if (kind == 1) {
        if (dir != 5 && dir != 7) return false;
        k = dir == 5 ? 8 : 9;
    } else if (kind == 2) {
        k = 10 + static_cast<std::size_t>(dir);
    }
    const std::size_t i = (nation * 4 + (type - 0x11)) * 18 + k;
    if (i >= s.ship_sprites.size()) return false;
    const sim::GameState::ShipSprite& e = s.ship_sprites[i];
    return e.state == 2 || (e.state == 1 && s.tick >= e.ready_tick);
}





struct ShipQuad {
    const UnitFrame* frame;
    double x, y, base;
    bool tint;
    bool reflection = false;
    double ground = 0, scale = 1;
};


void ship_group(std::vector<ShipQuad>* out, const std::vector<UnitFrame>& group, double x, double y, double base) {
    for (const UnitFrame& f : group) {
        if (f.ok) out->push_back({&f, x, y, base, false});
    }
}



float add_angle(float a, float b) {
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

constexpr double kQuarterPi = 0.7853981852531433;
constexpr double kFourByPi = 1.2732394933700562;



void ship_launch(std::vector<ShipQuad>* out, UnitSprites& sprites, const GameFileLoader& load, const std::string& path,
                 ShipGraphics& g, const sim::GameState& s, const sim::UnitType& t, float angle, int frame, double x,
                 double y) {
    const auto octant = static_cast<std::uint8_t>(static_cast<std::int64_t>(angle * kFourByPi));
    if (octant != 5 && octant != 7) return;
    if (!ship_slot_loaded(s, t, 1, octant)) return;
    const ShipEntry* e = ship_entry(sprites, load, path, g, octant == 5 ? 10 : 9);
    if (e == nullptr || frame < 0 || frame >= g.launch_frames) return;
    ship_group(out, e->groups[static_cast<std::size_t>(g.launch_frames + frame)], x, y, 55);
    ship_group(out, e->groups[static_cast<std::size_t>(frame)], x, y, 55);
}


void ship_sinking(std::vector<ShipQuad>* out, UnitSprites& sprites, const GameFileLoader& load, const std::string& path,
                  ShipGraphics& g, float angle, int stage, double x, double y) {
    const auto octant = static_cast<std::uint8_t>(static_cast<std::int64_t>(angle * kFourByPi));
    if (octant > 7) return;
    const ShipEntry* e = ship_entry(sprites, load, path, g, 0xb + octant);
    if (e == nullptr || stage >= g.death_frames) return;
    ship_group(out, e->groups[static_cast<std::size_t>(g.death_frames + stage)], x, y, 55);
    ship_group(out, e->groups[static_cast<std::size_t>(stage)], x, y, 55);
}



void ship_turret(std::vector<ShipQuad>* out, UnitSprites& sprites, const GameFileLoader& load, const std::string& path,
                 ShipGraphics& g, const sim::ShipWeaponType& w, float gun_angle, float hq, float x, float y) {
    if (w.slot == 0 || w.slot > g.turret_steps.size()) return;  // a fixed gun draws no turret
    const float a = add_angle(hq, gun_angle);
    const double sn = std::sin(static_cast<double>(hq)), cs = std::cos(static_cast<double>(hq));
    const auto px = static_cast<float>(cs * w.pos[0] - sn * w.pos[1]);
    const auto py = static_cast<float>(cs * w.pos[1] + sn * w.pos[0]);
    const auto ox = static_cast<float>((static_cast<double>(px) - py) * 0.8660253882408142);
    const auto oy = static_cast<float>(-((static_cast<double>(py) + px) * 0.5 + w.pos[2]));
    const float tx = ox + x, ty = oy + y;
    const int steps = g.turret_steps[w.slot - 1u];
    auto q = static_cast<std::uint8_t>(static_cast<std::int64_t>(a * kFourByPi));
    auto r = static_cast<std::uint8_t>(static_cast<std::int64_t>((a - q * kQuarterPi) * steps * kFourByPi + 0.5));
    if (q == 7 && r >= steps) q = 0, r = 0;
    const ShipEntry* e = ship_entry(sprites, load, path, g, 0x12 + w.slot);
    if (e == nullptr || e->groups.empty()) return;
    const std::size_t fi = static_cast<std::size_t>(q) * static_cast<std::size_t>(steps) + r;
    if (fi < e->groups[0].size() && e->groups[0][fi].ok) out->push_back({&e->groups[0][fi], tx, ty, 70, false});
}



void ship_hull(std::vector<ShipQuad>* out, UnitSprites& sprites, const GameFileLoader& load, const std::string& path,
               ShipGraphics& g, const sim::GameState& s, const sim::UnitType& t, const sim::Unit& u, float x, float y,
               double ground, double scale) {
    const auto quarter = static_cast<std::uint8_t>(static_cast<std::int64_t>(u.angle * 0.6366197466850281));
    const int o = u.orient;
    const auto frame =
        static_cast<std::uint8_t>(static_cast<std::int64_t>((u.angle - o * kQuarterPi) * g.frames * kFourByPi));
    if (!ship_slot_loaded(s, t, 0, o)) return;
    const ShipEntry* e = ship_entry(sprites, load, path, g, o + 1);
    if (e == nullptr || frame >= g.frames || quarter > 3) return;
    const auto hq = static_cast<float>(static_cast<double>(g.frames * o + frame) / g.frames * kQuarterPi);

    if (frame < e->reflection.size() && e->reflection[frame].ok)
        out->push_back({&e->reflection[frame], x, y, 0, false, true, ground, scale});
    ship_group(out, e->groups[frame], x, y, 55);
    for (const std::uint8_t b : g.order[quarter]) {
        if (b & 0x80) {
            if (u.work == 0xe) continue;  // no turrets while in the port
            const std::size_t k = (b >> 4) & 7;
            if (k >= g.turret_base.size()) continue;
            const std::size_t wi = static_cast<std::size_t>((b & 0xf) + g.turret_base[k] - 1);
            if (wi < t.ship.weapons.size() && wi < u.guns.size()) {
                ship_turret(out, sprites, load, path, g, t.ship.weapons[wi], u.guns[wi].angle, hq, x, y);
            }
        } else {
            const int part = (b & 0xf) - 1;
            if (part < 0 || part >= g.parts) continue;
            const UnitFrame& f = e->singles[static_cast<std::size_t>(part * g.frames + frame)];
            if (f.ok) out->push_back({&f, x, y, 55, false});
        }
    }
    const UnitFrame& m = e->singles[static_cast<std::size_t>(g.parts * g.frames + frame)];
    if (m.ok) out->push_back({&m, x, y, 55, true});
}


void ship_quads(std::vector<ShipQuad>* out, const WorldGraphics& gr, UnitSprites& sprites, const GameFileLoader& load,
                const sim::GameState& s, std::uint8_t player, const sim::Unit& u, ViewPos pos, bool dead) {
    const int nation = sim::Unit_Nation(s, player, u) % 6;  // TUnit::Basic's graphic
    const int type = u.type % 40;
    const auto& params = gr.params[static_cast<std::size_t>(nation)];
    if (!params || type >= static_cast<int>(params->units.size())) return;
    const UnitParams& up = params->units[static_cast<std::size_t>(type)];
    const sim::UnitType& t = s.unit_types[static_cast<std::size_t>(nation)][static_cast<std::size_t>(type)];
    const std::string path = unit_graphic_path(gr, nation, type);
    if (path.empty()) return;
    ShipGraphics& g = ship_graphics(sprites, load, path);
    if (!g.ok) return;


    const float x = (u.x - static_cast<float>(view_x(pos))) - (static_cast<float>(up.anchor_x) + up.width * 0.5f);
    const float y = ((u.y - static_cast<float>(view_y(pos))) - (u.z + u.field_024)) -
                    (static_cast<float>(up.anchor_y) + up.height * 0.5f);

    const auto ix = static_cast<double>(static_cast<std::int16_t>(static_cast<std::int64_t>(x)));
    const auto iy = static_cast<double>(static_cast<std::int16_t>(static_cast<std::int64_t>(y)));
    const double ground = (static_cast<double>(u.y) - view_y(pos)) - u.z;
    const double scale = up.width * static_cast<double>(0.00390625f);
    if (dead) {
        if (u.field_139 == 1 && u.field_0ca < 2) return;
        const auto stage = static_cast<std::uint8_t>((0xffff - static_cast<std::int32_t>(u.field_0ca)) / 600);




        if (stage < 4) ship_sinking(out, sprites, load, path, g, u.angle, stage, x, y);
        return;
    }
    if (u.work == 0xe && u.field_0ca < 2) {
        ship_launch(out, sprites, load, path, g, s, t, u.angle, u.field_0ca & 0xff, ix, iy);
    } else if (ship_slot_loaded(s, t, 0, u.orient)) {
        ship_hull(out, sprites, load, path, g, s, t, u, x, y, ground, scale);
    } else if (u.field_139 != 0) {
        ship_launch(out, sprites, load, path, g, s, t, u.angle, g.launch_frames - 1, ix, iy);
    }
}

const UnitAnim* anim_of(UnitSprites& sprites, const GameFileLoader& load, const std::string& path, int num_work) {
    std::map<int, UnitAnim>& per = sprites.anims[path];
    auto it = per.find(num_work);
    if (it == per.end()) it = per.emplace(num_work, load_anim(load, path, num_work, &sprites.warnings)).first;
    return it->second.ok ? &it->second : nullptr;
}

int nation_of_player(const std::vector<int>& nations, int player) {
    if (player == 8) return 5;  // a neutral house (a bridge): BasicParams5.dat
    return player >= 0 && player < static_cast<int>(nations.size()) ? nations[static_cast<std::size_t>(player)] : -1;
}

int house_nation_or(const std::vector<int>& house_nation, std::size_t i, const std::vector<int>& nations, int player) {
    return i < house_nation.size() && house_nation[i] >= 0 ? house_nation[i] : nation_of_player(nations, player);
}



void sync_terrain(MapData& map, const sim::Map& m, std::vector<std::size_t>* changed) {
    const std::size_t n = std::min(map.cells.size(), m.terrain.size());
    for (std::size_t i = 0; i < n; ++i) {
        const sim::TerrainCell& t = m.terrain[i];
        MapCell& c = map.cells[i];
        if (c.tile == t.tile && c.shade == t.shade && c.unknown4 == t.unused &&
            std::equal(t.sub.begin(), t.sub.end(), c.subtile.begin())) {
            continue;
        }
        changed->push_back(i);
        c.tile = t.tile;
        c.shade = t.shade;
        c.unknown4 = t.unused;
        std::copy(t.sub.begin(), t.sub.end(), c.subtile.begin());
    }
}

}  // namespace

std::vector<bool> players_with_ship_slots(const WorldGraphics& g, const sim::GameState& s) {
    std::vector<bool> out(s.players.size(), false);
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        const sim::Player& pl = s.players[p];
        std::array<std::uint64_t, 6> ships{};  // per nation of a unit's record (a merged unit keeps its own)
        for (std::size_t nation = 0; nation < 6; ++nation) {
            const auto& params = g.params[nation];
            const auto& types = s.unit_types[nation];
            for (std::size_t t = 0; t < 40; ++t) {
                const bool by_params = params && t < params->units.size() && is_ship_class(params->units[t].move_class);
                ships[nation] |= static_cast<std::uint64_t>(by_params || types[t].is_ship != 0) << t;
            }
        }
        for (const sim::Unit& u : pl.unit_slots) {
            if ((ships[sim::Unit_Nation(s, static_cast<std::uint8_t>(p), u) % 6] >> (u.type % 40)) & 1) {
                out[p] = true;
                break;
            }
        }
    }
    return out;
}

void ship_smoke(Effects& fx, EffectGraphics& eg, const WorldGraphics& gr, UnitSprites& sprites,
                const GameFileLoader& load, const sim::GameState& s, ViewPos pos, int w, int h,
                const std::vector<bool>* ship_slots) {
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        if (ship_slots != nullptr && p < ship_slots->size() && !(*ship_slots)[p]) continue;  // no ship in its slots
        const sim::Player& pl = s.players[p];
        // Only ships smoke, so a player without a ship in its list draws nothing and the list walk (slow on long lists,
        // docs/bugs.md B59) is skipped: no living ship counted, and none in the list (a sinking one is no longer
        // counted), found in the slots in memory order (a slot is in the list when its id points back to it). The
        // generator is used as before, so the smoke is the same.
        // A unit's record's nation (TUnit::Basic; a merged unit keeps its own).
        auto ship_type = [&](int nation, int type) {
            const auto& params = gr.params[static_cast<std::size_t>(nation)];
            return params && type < static_cast<int>(params->units.size()) &&
                   is_ship_class(params->units[static_cast<std::size_t>(type)].move_class);
        };
        bool any = false;
        for (int t = 0; t < 40 && !any; ++t) {
            any = pl.unit_count[static_cast<std::size_t>(t)] != 0 && ship_type(pl.nation % 6, t);
        }
        for (std::uint32_t k = 0; k < pl.unit_slots.size() && !any; ++k) {
            const sim::Unit& u = pl.unit_slots[k];
            any = ship_type(sim::Unit_Nation(s, static_cast<std::uint8_t>(p), u) % 6, u.type % 40) && u.serial != 0 &&
                  u.id < pl.id_slots.size() && pl.id_slots[u.id] == k;
        }
        if (!any) continue;
        for (std::uint32_t k = pl.first_unit; k != sim::kNoSlot; k = pl.unit_slots[k].next) {
            const sim::Unit& u = pl.unit_slots[k];
            const int type = u.type % 40;
            const int nation = sim::Unit_Nation(s, static_cast<std::uint8_t>(p), u) % 6;
            if (!ship_type(nation, type)) continue;
            const sim::UnitType& t = s.unit_types[static_cast<std::size_t>(nation)][static_cast<std::size_t>(type)];

            if (ship_in_dead_list(u)) continue;  // drawn from the dead list, without its hull

            if (u.work == 0xe && u.field_0ca < 2) continue;
            if (!ship_slot_loaded(s, t, 0, u.orient)) continue;
            if (u.work == 0xe) continue;
            if (!unit_in_view(gr, nation, type, u.x, u.y, u.z + u.field_024, pos, w, h)) continue;
            const std::string path = unit_graphic_path(gr, nation, type);
            if (path.empty()) continue;
            ShipGraphics& g = ship_graphics(sprites, load, path);
            if (!g.ok || g.frames == 0) continue;
            const int o = u.orient;
            const auto frame =
                static_cast<std::uint8_t>(static_cast<std::int64_t>((u.angle - o * kQuarterPi) * g.frames * kFourByPi));
            const auto hq = static_cast<float>(static_cast<double>(g.frames * o + frame) / g.frames * kQuarterPi);
            const auto turn = static_cast<float>(static_cast<double>(hq) + kQuarterPi);
            for (const std::array<float, 3>& pt : g.smoke) {
                if (effect_rand(fx) % 2 != 0) continue;  // rand() % 2 == 0

                const double sn = std::sin(static_cast<double>(turn)), cs = std::cos(static_cast<double>(turn));
                const auto px = static_cast<float>(cs * pt[0] - sn * pt[1]);
                const auto py = static_cast<float>(sn * pt[0] + cs * pt[1]);
                const auto ox = static_cast<float>((static_cast<double>(px) - py) * 0.8660253882408142);
                const auto oy = static_cast<float>(-((static_cast<double>(py) + px) * 0.5 + pt[2]));
                const auto X = static_cast<float>(static_cast<double>(ox) + u.x);
                const auto Y = static_cast<float>(static_cast<double>(oy) + u.y + pt[2]);
                EffectSpawn e;
                e.type = 9;  // SM1
                e.z = static_cast<int>(static_cast<double>(pt[2]) + u.z + 100.0);
                e.y = static_cast<int>(static_cast<double>(Y) - effect_rand(fx) % 5 + 100.0);
                e.x = static_cast<int>(static_cast<double>(static_cast<float>(static_cast<double>(X) + 5.0)) -
                                       effect_rand(fx) % 10);
                e.frame = 7.0f;
                e.rise_step = 2;
                effects_add(fx, eg, load, e);
            }
        }
    }
}

MapData map_from_save(const SvgSave& save) {
    MapData m;
    m.width = save.map_width;
    m.height = save.map_height;
    const std::size_t n = static_cast<std::size_t>(m.width) * static_cast<std::size_t>(m.height);
    m.cells.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        MapCell& c = m.cells[i];
        if (i < save.heights.size()) c.height = save.heights[i];
        if (i < save.objects.size()) c.object = save.objects[i];
        if ((i + 1) * 22 > save.cell_data.size()) continue;

        const std::uint8_t* r = &save.cell_data[i * 22];
        c.tile = static_cast<std::uint16_t>(r[0] | r[1] << 8);
        c.shade = r[2];
        c.unknown4 = r[3];
        for (int k = 0; k < 9; ++k) c.subtile[k] = static_cast<std::uint16_t>(r[4 + 2 * k] | r[5 + 2 * k] << 8);
    }
    m.obstacle_grid = save.obstacle_grid;
    m.routes = save.routes;
    m.decorations = save.decorations;
    return m;
}

bool live_view_sync(LiveView& lv, const sim::GameState& s) {
    lv.changed_houses.clear();
    lv.changed_cells.clear();
    lv.changed_all = lv.signature == 0;
    const int w = std::max(1, lv.map.width);
    const std::size_t n = std::min(lv.map.cells.size(), std::min(s.map.objects.size(), s.map.heights.size()));
    for (std::size_t i = 0; i < n; ++i) {
        MapCell& c = lv.map.cells[i];
        // A height moves the houses and objects on and around it as well: all is drawn again (the sim does not
        // change heights in play so far).
        if (c.height != s.map.heights[i]) lv.changed_all = true;
        if (c.object != s.map.objects[i]) {
            lv.changed_cells.push_back({static_cast<int>(i % w), static_cast<int>(i / w), c.object});
        }
        c.object = s.map.objects[i];
        c.height = s.map.heights[i];
    }
    std::vector<std::size_t> ground;
    sync_terrain(lv.map, s.map, &ground);
    for (std::size_t i : ground) {
        lv.changed_cells.push_back({static_cast<int>(i % w), static_cast<int>(i / w), lv.map.cells[i].object});
    }
    const std::vector<MapHouse> old_houses = std::move(lv.houses);
    const std::vector<sim::HouseRef> old_refs = std::move(lv.house_refs);
    const std::vector<int> old_graphic = std::move(lv.view.house_graphic);
    const std::vector<int> old_house_nation = std::move(lv.view.house_nation);
    const std::vector<int> old_nations = std::move(lv.view.nations);
    const std::vector<int> old_colours = std::move(lv.view.colours);
    lv.houses.clear();
    lv.house_refs.clear();
    lv.view.house_graphic.clear();
    lv.view.house_nation.clear();
    lv.view.nations.clear();
    lv.view.colours.clear();
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        lv.view.nations.push_back(s.players[p].nation);
        lv.view.colours.push_back(s.players[p].colour);
        for (std::size_t k = 0; k < s.players[p].houses.size(); ++k) {
            const sim::House& sh = s.players[p].houses[k];

            if (!(sh.hp >= -149000.0f) || sh.map_x < 0 || sh.map_y < 0 || sh.map_x >= lv.map.width ||
                sh.map_y >= lv.map.height) {
                continue;
            }
            MapHouse mh;
            mh.player = static_cast<int>(p);
            mh.cell_x = sh.map_x;
            mh.cell_y = sh.map_y;
            mh.type = static_cast<std::uint8_t>(sh.type);
            mh.build_state = sh.field_020;
            mh.hp = sh.hp;
            const int graphic = house_les_id(sh.graphic_phase, sh.graphic_stage, 0);
            lv.houses.push_back(mh);
            lv.house_refs.push_back(sim::HouseRef{static_cast<std::uint8_t>(p), static_cast<std::uint16_t>(k), 0});
            lv.view.house_graphic.push_back(graphic);
            lv.view.house_nation.push_back(sh.nation != 0xff ? sh.nation : -1);
        }
    }


    for (std::size_t k = 0; k < s.map.neutral_houses.size(); ++k) {
        const sim::NeutralHouse& nh = s.map.neutral_houses[k];
        MapHouse mh;
        mh.player = 8;
        mh.cell_x = nh.map_x;
        mh.cell_y = nh.map_y;
        mh.type = nh.type;
        lv.houses.push_back(mh);
        lv.house_refs.push_back(sim::HouseRef{8, static_cast<std::uint16_t>(k), 0});
        lv.view.house_graphic.push_back(0);
        lv.view.house_nation.push_back(-1);
    }
    if (lv.view.nations != old_nations || lv.view.colours != old_colours) lv.changed_all = true;
    // Houses whose look changed, appeared or went: as they were and as they are (both places are redrawn).
    auto same = [](const MapHouse& a, int ga, const MapHouse& b, int gb) {
        return a.cell_x == b.cell_x && a.cell_y == b.cell_y && a.type == b.type && ga == gb;
    };
    std::size_t j = 0;  // both lists are in (player, slot) order
    for (std::size_t i = 0; i < lv.houses.size(); ++i) {
        const sim::HouseRef& r = lv.house_refs[i];
        auto before = [&](const sim::HouseRef& o) {
            return o.player < r.player || (o.player == r.player && o.slot < r.slot);
        };
        for (; j < old_houses.size() && before(old_refs[j]); ++j) {
            lv.changed_houses.push_back({old_houses[j],
                                         house_nation_or(old_house_nation, j, old_nations, old_houses[j].player),
                                         old_graphic[j]});
        }
        const bool had = j < old_houses.size() && old_refs[j].player == r.player && old_refs[j].slot == r.slot;
        if (had && same(old_houses[j], old_graphic[j], lv.houses[i], lv.view.house_graphic[i])) {
            ++j;
            continue;
        }
        if (had) {
            lv.changed_houses.push_back({old_houses[j],
                                         house_nation_or(old_house_nation, j, old_nations, old_houses[j].player),
                                         old_graphic[j]});
            ++j;
        }
        lv.changed_houses.push_back({lv.houses[i],
                                     house_nation_or(lv.view.house_nation, i, lv.view.nations, lv.houses[i].player),
                                     lv.view.house_graphic[i]});
    }
    for (; j < old_houses.size(); ++j) {
        lv.changed_houses.push_back(
            {old_houses[j], house_nation_or(old_house_nation, j, old_nations, old_houses[j].player), old_graphic[j]});
    }


    std::vector<SpecialDraw> specials;
    for (const sim::SpecialObject& o : s.map.specials) {
        if (o.alive != 0 && o.kind >= 0x14 && o.kind <= 0x15) specials.push_back({o.x, o.y, o.kind, o.f_a < 0.0f});
    }
    lv.changed_specials.clear();
    if (specials != lv.view.specials) {
        const std::vector<SpecialDraw>& was = lv.view.specials;
        for (std::size_t i = 0; i < std::max(was.size(), specials.size()); ++i) {
            if (i < was.size() && i < specials.size() && was[i] == specials[i]) continue;
            if (i < was.size()) lv.changed_specials.push_back(was[i]);
            if (i < specials.size()) lv.changed_specials.push_back(specials[i]);
        }
        lv.view.specials = std::move(specials);
    }
    lv.view.map = &lv.map;
    lv.view.houses = lv.houses;
    lv.view.units = {};
    // Changed: anything the static tiles show (the first sync always).
    const bool changed =
        lv.changed_all || !lv.changed_cells.empty() || !lv.changed_houses.empty() || !lv.changed_specials.empty();
    lv.signature = 1;
    return changed;
}

void static_tiles_apply_changes(StaticTiles& tiles, const WorldGraphics& g, const LiveView& lv) {
    if (lv.changed_all) {
        static_tiles_clear(tiles);
        return;
    }
    for (const LiveView::HouseLook& hl : lv.changed_houses) {
        static_tiles_invalidate(tiles, house_static_rect(g, lv.map, hl.house, hl.nation, hl.graphic));
    }
    for (const SpecialDraw& o : lv.changed_specials) static_tiles_invalidate(tiles, special_static_rect(o));
    for (const auto& [cx, row, object] : lv.changed_cells) {
        static_tiles_invalidate(tiles, cell_static_rect(g, lv.map, cx, row, object));
        static_tiles_invalidate(
            tiles, cell_static_rect(g, lv.map, cx, row,
                                    lv.map.cells[static_cast<std::size_t>(row) * lv.map.width + cx].object));
    }
}

UnitPose unit_pose(const sim::GameState& s, std::uint8_t player, const sim::Unit& u) {
    UnitPose p;

    if (u.work == 3 || u.work == 0x14 || u.field_108 == 0 || player >= s.players.size()) return p;
    // The unit's record (TUnit::Basic, a merged unit's own nation); Work 0xb draws with g_BasicUnit[Owner->Nation * 40
    // + 1], the owner's: its graphic, sizes and animation lengths.
    const sim::UnitType& own = s.unit_types[sim::Unit_Nation(s, player, u) % 6][u.type % 40];
    const int nation = (u.work == 0xb ? s.players[player].nation : sim::Unit_Nation(s, player, u)) % 6;
    const int type = u.work == 0xb ? 1 : u.type % 40;
    const sim::UnitType& t = s.unit_types[nation][type];
    const std::uint8_t orient = u.orient;
    const bool mirror = orient > 4;
    std::uint8_t dir = orient;
    auto sprite = static_cast<std::uint8_t>(static_cast<std::int32_t>(u.num_sprite));  // ftol, low byte
    std::uint8_t nw = u.num_work;
    if (nw == 5) {  // the turn: mirrored directions run the frames backwards from 7 - Orient
        if (mirror) {
            dir = static_cast<std::uint8_t>(7 - orient);
            sprite = static_cast<std::uint8_t>(t.anim_size[5] - sprite - 1);
        }
    } else if (mirror) {
        if (t.is_air == 0 || nw != 0) {
            dir = static_cast<std::uint8_t>(8 - orient);
        } else {
            dir = static_cast<std::uint8_t>(7 - orient);
            sprite = static_cast<std::uint8_t>(t.anim_size[0] - sprite - 1);
        }
    }
    if (own.move_class == 4 && nw == 5) {  // cavalry turns show the standing frame
        nw = 3;
        sprite = 0;
    }
    if (nw > 0x1d) return p;
    if (dir > 7) dir = 0;  // the assert path: logged, then direction 0
    if (sprite >= t.anim_size[nw]) sprite = 0;
    p.visible = true;
    p.nation = nation;
    p.type = type;
    p.num_work = nw;
    p.frame = sprite;
    p.dir = dir;
    p.mirror = mirror;
    return p;
}

std::optional<std::array<float, 4>> unit_mark_frame(const WorldGraphics& g, UnitSprites& sprites,
                                                    const GameFileLoader& load, const sim::GameState& s,
                                                    std::uint8_t player, const sim::Unit& u) {
    if (player >= s.players.size()) return std::nullopt;
    const int own_nation = sim::Unit_Nation(s, player, u) % 6;
    const auto& own_params = g.params[static_cast<std::size_t>(own_nation)];
    const int own_type = u.type % 40;
    if (!own_params || own_type >= static_cast<int>(own_params->units.size()) ||
        is_ship_class(own_params->units[static_cast<std::size_t>(own_type)].move_class)) {
        return std::nullopt;
    }
    const UnitPose pose = unit_pose(s, player, u);
    if (!pose.visible) return std::nullopt;
    const auto& params = g.params[static_cast<std::size_t>(pose.nation)];
    if (!params || pose.type >= static_cast<int>(params->units.size())) return std::nullopt;
    const UnitParams& up = params->units[static_cast<std::size_t>(pose.type)];
    const std::string path = unit_graphic_path(g, pose.nation, pose.type);
    if (path.empty()) return std::nullopt;
    const UnitAnim* a = anim_of(sprites, load, path, 0);
    if (a == nullptr || a->sprite.empty() || !a->sprite[0].ok) return std::nullopt;
    const UnitFrame& f = a->sprite[0];
    const double cols = f.texture.width, rows = f.texture.height;
    const double ax = up.anchor_x + up.width / 2.0 - (pose.mirror ? up.width - cols - f.off_x : f.off_x);
    const double ay = up.anchor_y + up.height / 2.0 - f.off_y;
    return std::array<float, 4>{static_cast<float>(-ax), static_cast<float>(-ay), static_cast<float>(cols),
                                static_cast<float>(rows)};
}

std::array<float, 2> unit_screen_point(const sim::Unit& u, ViewPos pos) {
    return {u.x - static_cast<float>(view_x(pos)), u.y - static_cast<float>(view_y(pos)) - u.z - u.field_024};
}

void draw_blend_snapshot(DrawBlend& b, const sim::GameState& s) {
    constexpr float kNone = std::numeric_limits<float>::quiet_NaN();
    std::fill(b.units.begin(), b.units.end(), std::array<float, 2>{kNone, 0});
    for (const sim::Player& pl : s.players) {
        // The slots in memory order (much faster than the list on long lists); a slot is in the list when its id
        // points back to it.
        for (std::uint32_t k = 0; k < pl.unit_slots.size(); ++k) {
            const sim::Unit& u = pl.unit_slots[k];
            if (u.serial == 0 || u.id >= pl.id_slots.size() || pl.id_slots[u.id] != k) continue;
            if (u.serial >= b.units.size()) b.units.resize(std::size_t{u.serial} + 1, {kNone, 0});
            b.units[u.serial] = {u.x, u.y - u.z - u.field_024};
        }
    }
    b.shells.clear();
    for (const sim::Shell& sh : s.shells) {
        b.shells.push_back(
            {sh.x0, sh.y0, sh.x1, sh.y1, sh.flown, sh.owner.player, sh.owner.serial, sh.cx, sh.cy, sh.cz});
    }
}

std::array<float, 2> draw_blend_offset(const DrawBlend* b, const sim::Unit& u) {
    if (b == nullptr || b->alpha >= 1) return {0, 0};
    if (u.serial >= b->units.size() || std::isnan(b->units[u.serial][0])) return {0, 0};
    const std::array<float, 2>& was = b->units[u.serial];
    const float dx = was[0] - u.x, dy = was[1] - (u.y - u.z - u.field_024);
    if (std::fabs(dx) > DrawBlend::kTeleport || std::fabs(dy) > DrawBlend::kTeleport) return {0, 0};
    const float k = 1 - std::max(0.0f, b->alpha);
    return {dx * k, dy * k};
}

void render_live_frame(const WorldGraphics& g, UnitSprites& sprites, const GameFileLoader& load,
                       const sim::GameState& s, ViewPos pos, const Target& static_layer, Target* work,
                       std::vector<std::uint8_t>* rgba) {
    render_frame_begin(g, pos, static_layer, work);
    render_live_units(g, sprites, load, s, pos, *work, rgba);
}

namespace {

// One mark quad (UnitMarkQuad) into rt: each whole pixel of the quad samples the atlas texel under its centre (point
// sampling, the original's quads are pixel aligned), times the quad's colour; drawn where its key passes the depth
// test (key >= the stored key - 0.001, as draw_triangle), blended by alpha, no z write.
void mark_draw(const UnitMarks& m, const UnitMarkQuad& q, Target& rt, int band_top, int band_bottom) {
    const int x0 = std::max(0, static_cast<int>(q.x0)), x1 = std::min(rt.width, static_cast<int>(q.x1));
    const int y0 = std::max({0, band_top, static_cast<int>(q.y0)});
    const int y1 = std::min({rt.height, band_bottom, static_cast<int>(q.y1)});
    if (x0 >= x1 || y0 >= y1) return;
    const float ca = static_cast<float>(q.argb >> 24) / 255.0f, cr = static_cast<float>(q.argb >> 16 & 0xff) / 255.0f,
                cg = static_cast<float>(q.argb >> 8 & 0xff) / 255.0f, cb = static_cast<float>(q.argb & 0xff) / 255.0f;
    const float qw = q.x1 - q.x0, qh = q.y1 - q.y0;
    for (int y = y0; y < y1; ++y) {
        const float fv = q.v0 + (q.v1 - q.v0) * ((static_cast<float>(y) - q.y0 + 0.5f) / qh);
        const int ty = std::clamp(static_cast<int>(std::floor(fv)), 0, m.atlas_h - 1);
        for (int x = x0; x < x1; ++x) {
            const float fu = q.u0 + (q.u1 - q.u0) * ((static_cast<float>(x) - q.x0 + 0.5f) / qw);
            const int tx = std::clamp(static_cast<int>(std::floor(fu)), 0, m.atlas_w - 1);
            const std::uint8_t* px = m.atlas + (static_cast<std::size_t>(ty) * m.atlas_w + tx) * 4;
            const float a = static_cast<float>(px[3]) / 255.0f * ca;
            if (a <= 0) continue;
            const std::size_t i = static_cast<std::size_t>(y) * rt.width + x;
            if (!(q.key >= rt.key[i] - 0.001f)) continue;
            float* c = &rt.rgb[i * 3];
            c[0] = static_cast<float>(px[0]) / 255.0f * cr * a + c[0] * (1 - a);
            c[1] = static_cast<float>(px[1]) / 255.0f * cg * a + c[1] * (1 - a);
            c[2] = static_cast<float>(px[2]) / 255.0f * cb * a + c[2] * (1 - a);
        }
    }
}

}  // namespace

void render_live_units(const WorldGraphics& g, UnitSprites& sprites, const GameFileLoader& load,
                       const sim::GameState& s, ViewPos pos, Target& rt, std::vector<std::uint8_t>* rgba,
                       const DrawBlend* blend, std::span<const HouseFxQuad> anims, std::span<const EffectQuad> effects,
                       const UnitMarks* marks, const ShellTrails* shell_trails, const RingTexture* ring_tex,
                       std::span<const RingDot> house_ring, const UnitFxDraw* unit_fx) {

    auto colour = [&s](int player) {
        return player >= 0 && player < static_cast<int>(s.players.size()) ? static_cast<int>(s.players[player].colour)
                                                                          : player;
    };





    struct UnitDraw {
        double sort_y, sort_x;
        const UnitFrame* sprite;
        const UnitFrame* mask;
        const UnitFrame* shadow;
        int player, width, height, anchor_x, anchor_y;
        bool mirror, flat;
        double c4, alt, sx, sy;
        int ship;       // index into ships, or -1
        int anim = -1;  // index into anims (a house's animation, render/house_fx), or -1
        // the unit's serial (its marks and flashes)
        std::uint32_t serial = 0;
        float ox = 0, oy = 0;  // the blend offset (whole pixels) for the unit's flashes
        bool dead = false;
        float alpha = 1;
    };
    std::vector<UnitFxQuad> stains;
    std::vector<UnitDraw> drawn;
    std::vector<std::vector<ShipQuad>> ships;
    std::vector<std::array<int, 2>> dead_ships;  // index into ships, player
    // Each graphic's .les path and its animations, found once a frame instead of once a unit (anim_of builds the path
    // and looks it up by name).
    struct Graphic {
        bool found = false;
        std::string path;
        std::map<int, UnitAnim>* per = nullptr;
    };
    constexpr int kTypes = 64;
    std::vector<Graphic> graphic(6 * kTypes);
    const auto anim_in = [&](int nation, int type, int num_work) -> std::pair<const std::string*, const UnitAnim*> {
        if (nation < 0 || nation >= 6 || type < 0 || type >= kTypes) {
            static thread_local std::string other;
            other = unit_graphic_path(g, nation, type);
            if (other.empty()) return {nullptr, nullptr};
            return {&other, anim_of(sprites, load, other, num_work)};
        }
        Graphic& gr = graphic[static_cast<std::size_t>(nation * kTypes + type)];
        if (!gr.found) {
            gr.found = true;
            gr.path = unit_graphic_path(g, nation, type);
            if (!gr.path.empty()) gr.per = &sprites.anims[gr.path];
        }
        if (gr.per == nullptr) return {nullptr, nullptr};
        auto it = gr.per->find(num_work);
        if (it == gr.per->end()) {
            it = gr.per->emplace(num_work, load_anim(load, gr.path, num_work, &sprites.warnings)).first;
        }
        return {&gr.path, it->second.ok ? &it->second : nullptr};
    };
    // The widest test below (a ship's 1200 pixels) plus the largest blend offset and some rounding: a unit farther
    // off the view than this is left out by the tests below, whatever its blend offset (at most kTeleport pixels).
    constexpr float kFar = 1200 + DrawBlend::kTeleport + 8;
    // Following a long list through memory waits for nearly every unit (docs/bugs.md B59). So the slots are read in
    // memory order first: each one's successor into a small array, and whether it is near the view; the list order
    // is then followed in that array, and only the units near the view are read again (loaded into the cache now).
    std::vector<std::uint32_t> next_of;
    std::vector<std::uint8_t> near;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        const sim::Player& pl = s.players[p];
        next_of.resize(pl.unit_slots.size());
        near.resize(pl.unit_slots.size());
        for (std::size_t k = 0; k < pl.unit_slots.size(); ++k) {
            const sim::Unit& u = pl.unit_slots[k];
            next_of[k] = u.next;
            const auto [fx, fy] = unit_screen_point(u, pos);
            near[k] = -kFar < fx && fx < rt.width + kFar && -kFar < fy && fy < rt.height + kFar;
            if (near[k]) sim::Prefetch_Lines(&u, 0, sizeof(sim::Unit));
        }
        for (std::uint32_t k = pl.first_unit; k != sim::kNoSlot; k = next_of[k]) {
            if (!near[k]) continue;
            const sim::Unit& u = pl.unit_slots[k];
            // Between the last two ticks: the whole unit moved by its offset, as if the view were moved the other
            // way (whole pixels).
            const auto [bx, by] = draw_blend_offset(blend, u);
            const ViewPos upos{pos.col, pos.row, pos.dx - static_cast<int>(std::lround(bx)),
                               pos.dy - static_cast<int>(std::lround(by))};
            const auto [sx, sy] = unit_screen_point(u, upos);
            const int own_type = u.type % 40;
            const int own_nation = sim::Unit_Nation(s, static_cast<std::uint8_t>(p), u) % 6;  // TUnit::Basic
            const auto& own_params = g.params[static_cast<std::size_t>(own_nation)];
            if (own_params && own_type < static_cast<int>(own_params->units.size()) &&
                is_ship_class(own_params->units[static_cast<std::size_t>(own_type)].move_class)) {
                // Ship canvases are several hundred pixels wide (frame offsets up to about 600).
                if (!(-1200 < sx && sx < rt.width + 1200 && -1200 < sy && sy < rt.height + 1200)) continue;
                const bool dead = ship_in_dead_list(u);
                std::vector<ShipQuad> q;
                ship_quads(&q, g, sprites, load, s, static_cast<std::uint8_t>(p), u, upos, dead);
                if (q.empty()) continue;
                ships.push_back(std::move(q));
                const int si = static_cast<int>(ships.size() - 1);
                if (dead) {
                    dead_ships.push_back({si, static_cast<int>(p)});
                } else {
                    drawn.push_back({sy, sx, nullptr, nullptr, nullptr, static_cast<int>(p), 0, 0, 0, 0, false, false,
                                     0, 0, sx, sy, si});
                    drawn.back().serial = u.serial;
                    drawn.back().ox = static_cast<float>(std::lround(bx));
                    drawn.back().oy = static_cast<float>(std::lround(by));
                }
                continue;
            }
            if (!(-200 < sx && sx < rt.width + 200 && -200 < sy && sy < rt.height + 300)) continue;
            const UnitPose pose = unit_pose(s, static_cast<std::uint8_t>(p), u);
            if (!pose.visible) continue;
            const auto& params = g.params[static_cast<std::size_t>(pose.nation)];
            if (!params || pose.type >= static_cast<int>(params->units.size())) continue;
            const UnitParams& up = params->units[static_cast<std::size_t>(pose.type)];
            if (is_ship_class(up.move_class)) continue;
            auto [path, a] = anim_in(pose.nation, pose.type, pose.num_work);
            if (path == nullptr) continue;
            int frame = pose.frame;
            if (a == nullptr) {
                // Guess (render.md 11, low): an animation the archive lacks (infantry has no entry 3, "stay")
                // shows the first frame of super_stay (4), else of the walk (0).
                for (int alt : {4, 0}) {
                    a = anim_in(pose.nation, pose.type, alt).second;
                    if (a != nullptr) break;
                }
                frame = 0;
                if (a == nullptr) continue;
            }
            frame = std::min(frame, a->frames_per_dir - 1);
            const std::size_t fi = static_cast<std::size_t>(pose.dir * a->frames_per_dir + frame);
            if (pose.dir > 4 || fi >= a->sprite.size() || !a->sprite[fi].ok) continue;
            const UnitFrame& f = a->sprite[fi];
            const UnitFrame* m = fi < a->mask.size() && a->mask[fi].ok ? &a->mask[fi] : nullptr;
            const double c4 = static_cast<double>(u.z) + static_cast<double>(u.field_024);
            const double ay = up.anchor_y + up.height / 2.0 - f.off_y;
            const bool flat = s.unit_types[pose.nation][pose.type].is_air != 0;
            // The shadow layer: aircraft only (TUnit_Draw tests IsAir of the drawn type).
            const UnitFrame* sh = flat && fi < a->shadow.size() && a->shadow[fi].ok ? &a->shadow[fi] : nullptr;
            drawn.push_back({sy - ay + f.texture.height, sx, &f, m, sh, static_cast<int>(p), up.width, up.height,
                             up.anchor_x, up.anchor_y, pose.mirror, flat, c4, static_cast<double>(u.field_024), sx, sy,
                             -1});
            UnitDraw& d = drawn.back();
            d.serial = u.serial;
            d.ox = static_cast<float>(std::lround(bx));
            d.oy = static_cast<float>(std::lround(by));

            const sim::UnitType& ot =
                s.unit_types[static_cast<std::size_t>(own_nation)][static_cast<std::size_t>(own_type)];
            const auto dnw = static_cast<std::int8_t>(u.num_work);
            if (u.is_deleted != 0 && u.field_024 <= 1.0f && dnw >= 0 && dnw < 30 &&
                u.num_sprite == static_cast<float>(ot.anim_size[static_cast<std::size_t>(dnw)] - 1)) {
                d.dead = true;
                d.alpha = static_cast<float>(u.field_0ca >> 8) / 255.0f;
                if (unit_fx != nullptr && unit_fx->graphics != nullptr && own_params &&
                    own_type < static_cast<int>(own_params->units.size())) {
                    burnt_stain_quads(*unit_fx->graphics, unit_fx->map, u,
                                      own_params->units[static_cast<std::size_t>(own_type)].anchor_x, upos, &stains);
                }
            }
        }
    }


    for (std::size_t i = 0; i < anims.size(); ++i) {
        UnitDraw d{};
        d.sort_y = static_cast<double>(anims[i].y0 + anims[i].h);
        d.sort_x = static_cast<double>(anims[i].x0 + anims[i].w);
        d.player = -1;
        d.ship = -1;
        d.anim = static_cast<int>(i);
        drawn.push_back(d);
    }
    std::stable_sort(drawn.begin(), drawn.end(), [](const UnitDraw& a, const UnitDraw& b) {
        return a.sort_y < b.sort_y || (a.sort_y == b.sort_y && a.sort_x < b.sort_x);
    });
    // Drawn in row bands on several threads: each band gets every quad clipped to its rows, in the same order, so the
    // picture does not depend on the split.
    const std::array<float, 3> white{1, 1, 1};
    std::unordered_map<std::uint32_t, std::vector<const UnitMarkQuad*>> mark_index;  // by the unit's serial
    if (marks != nullptr && marks->atlas != nullptr) {
        for (const UnitMarkQuad& q : marks->quads) mark_index[q.serial].push_back(&q);
    }
    std::unordered_map<std::uint32_t, std::vector<const UnitFxQuad*>> fx_index;  // flashes by the unit's serial
    if (unit_fx != nullptr) {
        for (const UnitFxQuad& q : unit_fx->quads) fx_index[q.serial].push_back(&q);
    }
    // The rows each sorted item can touch, so a strip skips the units outside it without reading them: a plain unit's
    // shadow (moved down by its altitude), sprite and mask, with a margin. Everything else (an animation, a ship, a
    // unit with marks or flashes) is drawn by every strip, as before.
    struct Rows {
        int top, bottom;
    };
    std::vector<Rows> rows(drawn.size(), Rows{std::numeric_limits<int>::min(), std::numeric_limits<int>::max()});
    std::vector<std::size_t> dead_items;  // the dead list's land units, in the sorted order
    for (std::size_t i = 0; i < drawn.size(); ++i) {
        const UnitDraw& d = drawn[i];
        if (d.dead) dead_items.push_back(i);
        if (d.anim >= 0 || d.ship >= 0 || mark_index.count(d.serial) != 0 || fx_index.count(d.serial) != 0) continue;
        double top = std::numeric_limits<double>::infinity(), bottom = -top;
        for (int layer = -1; layer < 2; ++layer) {  // as draw_unit below
            const UnitFrame* f = layer < 0 ? d.shadow : layer == 0 ? d.sprite : d.mask;
            if (f == nullptr) continue;
            double y0 = d.sy - (d.anchor_y + d.height / 2.0 - f->off_y);
            if (layer < 0) y0 += d.alt;
            top = std::min(top, y0);
            bottom = std::max(bottom, y0 + f->texture.height);
        }
        if (top > bottom) continue;  // no layer
        constexpr int kMargin = 4;
        rows[i] = Rows{static_cast<int>(std::floor(top)) - kMargin, static_cast<int>(std::ceil(bottom)) + kMargin};
    }
    // Drawn in strips of kStrip rows taken one by one from a counter by the band threads: units cluster, so fixed
    // bands (one a thread) were uneven.
    constexpr int kStrip = 16;
    const int strips = (rt.height + kStrip - 1) / kStrip;
    std::atomic<int> next_strip{0};
    const auto draw_band = [&](int band_top, int band_bottom) {
        DrawState unit_state;
        unit_state.z_write = false;
        unit_state.blend = true;
        unit_state.clip_top = band_top;
        unit_state.clip_bottom = band_bottom;


        auto draw_ship = [&](const std::vector<ShipQuad>& quads, int player) {
            for (const ShipQuad& q : quads) {
                if (q.reflection) continue;  // ship_reflections_draw
                const int c = colour(player);
                if (q.tint && (c < 0 || c >= static_cast<int>(g.player_colour.size()))) continue;
                const double rows = q.frame->texture.height, cols = q.frame->texture.width;
                const double x0 = q.x + q.frame->off_x, y0 = q.y + q.frame->off_y;
                const double key = y0 + rows + 2 * q.base;
                unit_state.texture = &q.frame->texture;
                unit_state.tint = q.tint ? g.player_colour[static_cast<std::size_t>(c)] : white;
                draw_quad(rt,
                          {Vertex{x0, y0, key, 0, 0}, Vertex{x0 + cols, y0, key, 0.99999, 0},
                           Vertex{x0, y0 + rows, key, 0, 0.99999}, Vertex{x0 + cols, y0 + rows, key, 0.99999, 0.99999}},
                          unit_state);
            }
        };

        for (const EffectQuad& q : effects) {
            if (q.ground) effect_draw(q, rt, band_top, band_bottom);
        }

        auto draw_marks = [&](std::uint32_t serial, bool before) {
            const auto it = mark_index.find(serial);
            if (it == mark_index.end()) return;
            for (const UnitMarkQuad* q : it->second) {
                if (q->before == before) mark_draw(*marks, *q, rt, band_top, band_bottom);
            }
        };
        // The unit's flashes of one order (render/unit_fx), moved with the unit between two ticks.
        auto draw_fx = [&](const UnitDraw& d, UnitFxQuad::Order order) {
            const auto it = fx_index.find(d.serial);
            if (it == fx_index.end()) return;
            for (const UnitFxQuad* q : it->second) {
                if (q->order == order) unit_fx_draw(*q, d.ox, d.oy, rt, band_top, band_bottom);
            }
        };


        for (const auto& [si, player] : dead_ships) draw_ship(ships[static_cast<std::size_t>(si)], player);
        for (const UnitFxQuad& q : stains) unit_fx_draw(q, 0, 0, rt, band_top, band_bottom);
        auto draw_unit = [&](const UnitDraw& d) {
            unit_state.alpha = d.alpha;
            for (int layer = -1; layer < 2; ++layer) {
                const UnitFrame* f = layer < 0 ? d.shadow : layer == 0 ? d.sprite : d.mask;
                if (f == nullptr) continue;
                const int c = colour(d.player);
                if (layer == 1 && (c < 0 || c >= static_cast<int>(g.player_colour.size()))) continue;
                const double rows = f->texture.height, cols = f->texture.width;
                const double ax = d.anchor_x + d.width / 2.0 - (d.mirror ? d.width - cols - f->off_x : f->off_x);
                const double ay = d.anchor_y + d.height / 2.0 - f->off_y;
                double y0 = d.sy - ay;
                const double x0 = d.sx - ax;
                double kb = y0 + rows + 2 * d.c4, kt = d.flat ? kb : kb + rows;
                if (layer < 0) {  // the aircraft shadow: on the ground (+ altitude), flat at its bottom key + 2 c4
                    y0 += d.alt;
                    kb = kt = y0 + rows + 2 * d.c4;
                }
                const double u0 = d.mirror ? 0.99999 : 0, u1 = d.mirror ? 0 : 0.99999;
                unit_state.texture = &f->texture;
                unit_state.tint = layer == 1 ? g.player_colour[static_cast<std::size_t>(c)] : white;
                draw_quad(rt,
                          {Vertex{x0, y0, kt, u0, 0}, Vertex{x0 + cols, y0, kt, u1, 0},
                           Vertex{x0, y0 + rows, kb, u0, 0.99999}, Vertex{x0 + cols, y0 + rows, kb, u1, 0.99999}},
                          unit_state);
            }
            unit_state.alpha = 1;
        };
        const auto outside = [&](std::size_t i) { return rows[i].bottom <= band_top || rows[i].top >= band_bottom; };
        for (const std::size_t i : dead_items) {
            if (!outside(i)) draw_unit(drawn[i]);
        }
        for (std::size_t i = 0; i < drawn.size(); ++i) {
            if (outside(i)) continue;
            const UnitDraw& d = drawn[i];
            if (d.anim >= 0) {
                house_fx_draw(anims[static_cast<std::size_t>(d.anim)], rt, band_top, band_bottom);
                continue;
            }
            if (d.ship >= 0) {
                draw_ship(ships[static_cast<std::size_t>(d.ship)], d.player);
                // Ours: a ship's flashes after its whole hull (the original's after its turret or its mask).
                draw_fx(d, UnitFxQuad::Order::WeaponBefore);
                draw_fx(d, UnitFxQuad::Order::WeaponAfter);
                continue;
            }
            if (d.dead) continue;


            draw_marks(d.serial, true);
            draw_fx(d, UnitFxQuad::Order::WeaponBefore);
            draw_unit(d);
            draw_fx(d, UnitFxQuad::Order::Rifle);
            draw_marks(d.serial, false);
            draw_fx(d, UnitFxQuad::Order::WeaponAfter);
        }



        for (const EffectQuad& q : effects) {
            if (!q.ground) effect_draw(q, rt, band_top, band_bottom);
        }
    };
    parallel_bands(rt.height, [&](int, int) {
        for (int k = next_strip++; k < strips; k = next_strip++) {
            draw_band(k * kStrip, std::min(rt.height, (k + 1) * kStrip));
        }
    });
    render_frame_finish(g, pos, rt, rgba);

    if (ring_tex != nullptr) ring_dots_draw_rgba(*ring_tex, house_ring, rt, *rgba);

    std::vector<ShellQuad> shell_draw;
    shell_quads(sprites.shells, load, s, pos, blend, shell_trails, &shell_draw);
    shell_quads_draw(shell_draw, *rgba, rt.width, rt.height);
}

void ship_reflections_draw(const WorldGraphics& g, UnitSprites& sprites, const GameFileLoader& load,
                           const sim::GameState& s, ViewPos pos, Target& rt, const DrawBlend* blend,
                           const std::vector<bool>* ship_slots) {
    struct Reflection {
        double sort_y, sort_x;
        bool dead;
        ShipQuad q;
    };
    std::vector<Reflection> list;
    // Only ships have reflections: when no slot (in memory order, free ones too) holds a ship type, the list holds no
    // ship and its walk (slow on long lists, docs/bugs.md B59) is skipped.
    const std::vector<bool> own_slots = ship_slots != nullptr ? std::vector<bool>{} : players_with_ship_slots(g, s);
    const std::vector<bool>& slots = ship_slots != nullptr ? *ship_slots : own_slots;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        const sim::Player& pl = s.players[p];
        const int nation = pl.nation % 6;
        const auto& params = g.params[static_cast<std::size_t>(nation)];
        if (!params || p >= slots.size() || !slots[p]) continue;
        for (std::uint32_t k = pl.first_unit; k != sim::kNoSlot; k = pl.unit_slots[k].next) {
            const sim::Unit& u = pl.unit_slots[k];
            const int type = u.type % 40;
            if (type >= static_cast<int>(params->units.size()) ||
                !is_ship_class(params->units[static_cast<std::size_t>(type)].move_class)) {
                continue;
            }
            const auto [bx, by] = draw_blend_offset(blend, u);
            const ViewPos upos{pos.col, pos.row, pos.dx - static_cast<int>(std::lround(bx)),
                               pos.dy - static_cast<int>(std::lround(by))};
            const auto [sx, sy] = unit_screen_point(u, upos);
            if (!(-1200 < sx && sx < rt.width + 1200 && -1200 < sy && sy < rt.height + 1200)) continue;
            const bool dead = ship_in_dead_list(u);
            std::vector<ShipQuad> q;
            ship_quads(&q, g, sprites, load, s, static_cast<std::uint8_t>(p), u, upos, dead);
            for (const ShipQuad& r : q) {
                if (r.reflection) list.push_back({sy, sx, dead, r});
            }
        }
    }
    if (list.empty()) return;
    // In the order the unit pass draws the hulls: the dead list first, then by the ground point.
    std::stable_sort(list.begin(), list.end(), [](const Reflection& a, const Reflection& b) {
        if (a.dead != b.dead) return a.dead;
        return !a.dead && (a.sort_y < b.sort_y || (a.sort_y == b.sort_y && a.sort_x < b.sort_x));
    });



    constexpr double kWobble = 3.3333332538604736;
    constexpr double kStep = 0.20000000298023224;
    const double phase = 6.0 * static_cast<double>(s.tick);
    struct Mesh {
        const Texture* tex;
        std::array<WaterVertex, 36> v;
    };
    std::vector<Mesh> meshes;
    for (const Reflection& r : list) {
        const UnitFrame& f = *r.q.frame;
        const double sc = r.q.scale;
        const auto rows = static_cast<int>(f.texture.height * sc), cols = static_cast<int>(f.texture.width * sc);
        const auto ox = static_cast<int>(f.off_x * sc), oy = static_cast<int>(f.off_y * sc);
        const auto x = static_cast<float>(r.q.x), y = static_cast<float>(r.q.y);
        const auto top = static_cast<float>(r.q.ground - y - static_cast<float>(oy));  // T
        const auto x0 = static_cast<float>(static_cast<float>(ox) + x);
        const auto y0 = static_cast<float>(2.0 * top - rows + static_cast<float>(oy) + y);
        if (x0 + cols + 8 < 0 || x0 - 8 > rt.width || y0 + rows + 8 < 0 || y0 - 8 > rt.height) continue;
        Mesh m{&f.texture, {}};
        for (int i = 0; i < 6; ++i) {
            const double cy = std::cos(phase + 16.0 * i) * kWobble;
            for (int j = 0; j < 6; ++j) {
                WaterVertex& v = m.v[static_cast<std::size_t>(j * 6 + i)];
                v.x = static_cast<float>(std::sin(phase + 16.0 * j) * kWobble + i * (cols * kStep) + x0);
                v.y = static_cast<double>(static_cast<std::int64_t>(j * (rows * kStep) + cy + y0));
                v.u = i * kStep;
                v.v = 1.0 - j * kStep;
            }
        }
        meshes.push_back(m);
    }

    const std::array<float, 3> tint{0.0f, 0x7f / 255.0f, 0x19 / 255.0f};
    constexpr float kAlpha = 0x41 / 255.0f;
    parallel_bands(rt.height, [&](int top, int bottom) {
        for (const Mesh& m : meshes) {
            const Texture& t = *m.tex;
            auto sample = [&t](double u, double v) { return texture_texel(t, u, v); };
            for (int j = 0; j < 5; ++j) {
                for (int i = 0; i < 5; ++i) {


                    const WaterVertex& a = m.v[static_cast<std::size_t>(j * 6 + i)];
                    const WaterVertex& b = m.v[static_cast<std::size_t>(j * 6 + i + 1)];
                    const WaterVertex& c = m.v[static_cast<std::size_t>((j + 1) * 6 + i)];
                    const WaterVertex& d = m.v[static_cast<std::size_t>((j + 1) * 6 + i + 1)];
                    water_triangle(rt, sample, tint, kAlpha, c, a, b, top, bottom);
                    water_triangle(rt, sample, tint, kAlpha, c, b, d, top, bottom);
                }
            }
        }
    });
}

}  // namespace antanta
