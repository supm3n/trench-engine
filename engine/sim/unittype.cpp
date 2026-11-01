// Unit type stats (TBasicUnit, docs/structs.md): the movement fields LoadBasicParams derives from the records of
// Data/BasicParams<nation>.dat.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "dmath.hpp"
#include "formats/les.hpp"
#include "state.hpp"

namespace antanta::sim {





std::uint8_t UnitType_NoMeleeStepsBack(const UnitType& t) {
    // speed[0] is the record's Speed until research raises it (types 4 and 16 only, which melee or are faster than
    // 0.1 already, so the answer stays); a ship's animations are cleared, but a ship never has the flag.
    return t.anim_size[7] == 0 && t.speed[0] > 0.1f && t.is_air == 0 && t.is_ship == 0 && t.move_class != 3 ? 1 : 0;
}

std::optional<GameData> GameData_FromParams(std::span<const BasicParams> params, std::string* error) {
    GameData d;
    if (params.size() > d.unit_types.size()) {
        *error = "more BasicParams files than nations";
        return std::nullopt;
    }
    for (std::size_t nation = 0; nation < params.size(); ++nation) {

        for (std::size_t type = 0; type < params[nation].houses.size() && type < 25; ++type) {
            const HouseParams& h = params[nation].houses[type];
            d.house_footprints[nation][type] = HouseFootprint{h.footprint_a, h.footprint_b};
            d.house_types[nation][type].present = true;
            d.house_types[nation][type].upkeep = h.upkeep;
            d.house_types[nation][type].power_use = h.power_use;
            d.house_types[nation][type].produce = h.produce;
            d.house_types[nation][type].field_1b0 = h.field_188;
            d.house_types[nation][type].field_1b1 = h.field_189;
            d.house_types[nation][type].field_1b3 = h.field_2c1;
            d.house_types[nation][type].field_2ae = h.field_2c0;
            d.house_types[nation][type].field_1f2 = h.field_1c8;
            d.house_types[nation][type].fire_x = h.fire_x, d.house_types[nation][type].fire_y = h.fire_y;
            d.house_types[nation][type].flash_x = h.flash_x, d.house_types[nation][type].flash_y = h.flash_y;
            d.house_types[nation][type].work_rate = static_cast<std::uint16_t>(static_cast<std::int16_t>(h.work) / 10);
            d.house_types[nation][type].max_hp = h.max_hp;
            d.house_types[nation][type].field_14c = h.capacity;
            d.house_types[nation][type].field_2ac = h.capacity == 0 ? 1 : 0;
            d.house_types[nation][type].field_26e = h.point_count_b;
            HouseType& ht = d.house_types[nation][type];
            ht.sprite_w = h.width, ht.sprite_h = h.height, ht.field_140 = h.offset_x, ht.field_142 = h.offset_y;
            ht.field_148 = h.field_142, ht.hit_count = h.hit_count, ht.hit_x = h.hit_x, ht.hit_y = h.hit_y;
            ht.anchor_x = h.anchor_x, ht.anchor_y = h.anchor_y, ht.exit_x = h.exit_x, ht.exit_y = h.exit_y;


            for (std::size_t k = 0; k < 5; ++k) {
                float v = static_cast<float>(static_cast<std::int32_t>(h.cost[k]));
                if (static_cast<std::int32_t>(h.cost[k]) < 0) v = v + 4294967296.0f;
                ht.cost[k] = v;
            }
            ht.field_190 = h.field_2bc;


            const HouseParams& src = params[0].houses.size() > type ? params[0].houses[type] : h;
            ht.prereq.clear();
            for (std::uint16_t v : src.prereq) {
                if (v == 0xffff) break;
                ht.prereq.push_back(v);
            }
        }
        if (params[nation].units.size() > 40) {
            *error = "BasicParams" + std::to_string(nation) + ".dat has more than 40 unit types";
            return std::nullopt;
        }
        for (std::size_t type = 0; type < params[nation].units.size(); ++type) {
            const UnitParams& r = params[nation].units[type];
            UnitType t;
            t.present = true;

            t.graphic = static_cast<std::uint16_t>(nation * 40 + type);
            const std::string& path = r.data_path;
            if (std::atol(path.c_str()) != 0) {
                const auto n = static_cast<std::uint8_t>(std::atol(path.substr(0, 1).c_str()) - 1);
                const auto tt = static_cast<std::uint8_t>(std::atol(path.substr(1, 2).c_str()));
                t.graphic = static_cast<std::uint16_t>(n * 40 + tt);
            }
            t.move_class = r.move_class;
            t.is_ship = r.move_class >= 13 && r.move_class <= 15;
            t.is_air = r.move_class == 1 || r.move_class == 11 || r.move_class == 12;
            t.field_006 = (r.move_class == 2 || r.move_class == 7 || r.move_class == 16) ? 1 : 0;

            const bool no_group = t.is_air != 0 || t.is_ship != 0 || r.move_class == 3 || r.speed < 0.5f || type == 0;
            t.field_009 = no_group ? 0 : 1;

            const std::uint8_t m = r.move_class;
            t.field_00b =
                (m == 2 || m == 4 || m == 5 || m == 7 || m == 8 || m == 9 || m == 10 || m == 16 || type == 0) ? 1 : 0;


            t.field_00c = (m == 1 || m == 3 || (m >= 11 && m <= 15) || type == 5) ? 0 : 1;
            t.anim_size = r.anim_size;
            t.anim_speed = r.anim_speed;
            t.anim_size[3] = 1;
            if (type == 0x17) {
                t.anim_size[6] = static_cast<std::uint8_t>(t.anim_size[6] >> 1);
                t.anim_speed[6] = 0.3f;
            }
            t.speed.fill(r.speed);
            t.upkeep = r.upkeep;
            // Costs: the record's u32 values loaded as integers (fild) into floats; BuildTime for every player.
            for (std::size_t k = 0; k < 6; ++k) t.cost[k] = static_cast<float>(static_cast<std::int32_t>(r.cost[k]));
            t.build_time.fill(r.build_time);
            // The per-player tables, the same for all 8 players (a loaded save brings its own: upgrades change them).
            t.accuracy.fill(r.accuracy), t.range.fill(r.range), t.max_hp.fill(r.max_hp);
            t.reload.fill(static_cast<std::uint16_t>(type == 0x17 ? r.reload + 100 : r.reload));
            for (std::size_t p = 0; p < 8; ++p) {
                for (std::size_t k = 0; k < 10; ++k) t.attack[p][k] = r.attack[k], t.defense[p][k] = r.defense[k];
            }
            // TurnRate = turn degrees * 0.017453292 (pi/180), replaced below for ground units.
            t.turn_rate = static_cast<float>(static_cast<double>(r.turn_degrees) * 0.017453292);

            const double tr = static_cast<double>(r.turn_degrees) * static_cast<double>(0.017453292f);
            const double ctr = dmath::cos(tr);
            t.turn_radius = static_cast<float>(static_cast<double>(r.speed) / std::sqrt(2.0 - (ctr + ctr)));
            t.field_434 = r.field_170, t.field_48a = r.field_1bc;
            const bool big = type == 0xb || type == 0xc || type == 0x17 || type == 0x18;
            t.field_468 = big ? 1 : 0;
            const float factor = big ? 3.0f : 1.15f;
            t.waypoint_radius.fill(static_cast<float>(static_cast<double>(r.speed) * static_cast<double>(factor)));
            if (t.is_air == 0 && t.is_ship == 0) {
                // (AnimationSpeed[5] / AnimationSize[5]) * 0.7853982f
                t.turn_rate = static_cast<float>(static_cast<double>(t.anim_speed[5]) /
                                                 static_cast<double>(t.anim_size[5]) * static_cast<double>(0.7853982f));
            } else if (t.is_ship != 0) {
                t.anim_size.fill(0);
            }
            if (t.move_class == 3) t.field_468 = 1;
            t.field_594 = t.anim_size[0x16] != 0 ? 3 : 0;
            if (nation == 3 && type == 0xe) t.field_594 = 10;
            t.field_598 = 0.001f;
            t.size = r.size;


            t.field_5d8 = nation > 4 ? 0 : type == 0xc || (type == 0xb && nation == 1) ? 3 : type == 0xb ? 1 : 0;
            t.field_5dc = t.field_5d8 != 0 ? 700.0f : 0.0f;
            t.field_588 = r.field_786;
            // The switch on MoveClass in LoadBasicParams: +7 (attacks houses) and +8 (only with grenades).
            const std::uint8_t mc = r.move_class;
            bool b7 = mc == 12 || mc == 5 || mc == 8 || mc == 10 || mc == 9 || mc == 14 || mc == 15 ||
                      (mc == 3 && type != 0x18 && type != 0xd);
            const bool grenade = r.anim_size[0x16] != 0;
            t.field_008 = grenade && !b7 ? 1 : 0;
            if (grenade) b7 = true;
            t.field_007 = b7 ? 1 : 0;


            t.field_5e0 = UnitType_NoMeleeStepsBack(t);
            for (const WeaponParams& w : r.weapons) {
                WeaponType v{w.kind, w.attack, w.reload, w.fire_range, w.min_range, w.field_48, w.point_a};


                const double a = static_cast<double>(0.7853982f), sn = dmath::sin(a), cs = dmath::cos(a);
                const double x = w.point_a[0], y = w.point_a[1];
                v.point_a[0] = static_cast<float>(cs * x - sn * y);
                v.point_a[1] = static_cast<float>(x * sn + cs * y);
                t.weapons.push_back(v);
            }
            d.unit_types[nation][type] = t;
        }
    }
    return d;
}

namespace {


struct TowerRow {
    std::uint16_t attack = 0, reload = 0, bullets = 0;
    float fire_range = 0, min_range = 0;
};


std::vector<std::string> Tokens(ByteSpan b) {
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


void TurnQuarter(std::array<float, 3>& v) {
    const double a = static_cast<double>(0.7853982f), sn = dmath::sin(a), cs = dmath::cos(a);
    const double x = v[0], y = v[1];
    v[0] = static_cast<float>(cs * x - sn * y);
    v[1] = static_cast<float>(x * sn + cs * y);
}

struct Reader {
    ByteSpan b;
    std::size_t p = 0;
    bool ok = true;
    std::uint8_t u8() {
        if (p + 1 > b.size()) {
            ok = false;
            return 0;
        }
        return b[p++];
    }
    float f32() {
        if (p + 4 > b.size()) {
            ok = false;
            return 0;
        }
        const std::uint32_t v = read_u32(b, p);
        p += 4;
        float f = 0;
        std::memcpy(&f, &v, 4);
        return f;
    }
    void skip(std::size_t n) {
        if (p + n > b.size()) ok = false;
        p = std::min(p + n, b.size());
    }
};



bool LoadShipGraphic(ShipGraphic& g, ByteSpan les, std::uint8_t type, std::uint8_t nation,
                     const std::vector<TowerRow>& rows, std::string* error) {
    auto table = parse_les(les, error);
    if (!table) return false;
    const LesEntry* e0 = find_entry_id(*table, 0);
    if (e0 == nullptr) return true;  // the loader returns, G stays zeroed
    auto data = read_entry(les, *e0, error);
    if (!data) return false;
    Reader r{*data};
    r.u8();  // G+6
    const std::uint8_t n1 = r.u8();
    std::vector<std::uint8_t> steps(n1);
    for (auto& v : steps) v = r.u8();
    r.skip(static_cast<std::size_t>(n1) + 1);
    const std::uint8_t g7 = r.u8();
    const std::uint8_t nw = r.u8();
    constexpr float kDeg = 0.017453292f;
    auto rad = [](float v) { return static_cast<float>(static_cast<double>(v) * static_cast<double>(kDeg)); };
    for (std::uint8_t i = 0; i < nw && r.ok; ++i) {
        ShipWeaponType w;
        for (float& v : w.pos) v = r.f32();
        TurnQuarter(w.pos);
        w.f28 = rad(r.f32());
        for (float& v : w.arcs) v = r.f32();
        w.arc_count = r.u8();
        for (std::uint8_t k = 0; k < w.arc_count && k < 2; ++k) {
            for (int m = 0; m < 3; ++m) w.arcs[3 * k + m] = rad(w.arcs[3 * k + m]);
        }
        w.slot = r.u8();
        w.barrels = r.u8();
        for (std::uint8_t k = 0; k < w.barrels; ++k) {
            std::array<float, 3> pt{r.f32(), r.f32(), r.f32()};
            TurnQuarter(pt);
            w.points.push_back(pt);
        }
        if (w.slot != 0) {
            if (w.slot > steps.size()) {
                *error = "ship weapon turret slot past the step array";
                return false;
            }
            w.steps = steps[w.slot - 1u];
            w.turn_step = static_cast<float>(static_cast<double>(0.7853982f) / static_cast<double>(w.steps));

            int row = -1;
            if (type == 0x12) {
                row = w.slot == 2 ? 1 : 2;
                w.kind = w.slot == 2 ? 7 : 1;
            } else if (type == 0x13) {
                row = nation != 0 ? 3 : w.slot == 1 ? 3 : 4;
                w.kind = 1;
            } else if (type == 0x14) {
                row = 5;
                w.kind = 1;
            }
            if (row >= 0 && static_cast<std::size_t>(row) < rows.size()) {
                const TowerRow& t = rows[static_cast<std::size_t>(row)];
                w.attack = t.attack, w.reload = t.reload, w.fire_range = t.fire_range, w.min_range = t.min_range;
            }
        }
        g.weapons.push_back(w);
    }
    const std::uint8_t c = r.u8();
    r.skip(static_cast<std::size_t>(c) * 13);
    const std::uint8_t n2 = r.u8();
    for (std::uint8_t k = 0; k < n2; ++k) {
        std::array<float, 3> pt{};
        for (float& v : pt) v = static_cast<float>(static_cast<double>(r.f32()) * static_cast<double>(3.4105608f));
        g.hull.push_back(pt);
    }
    const std::uint8_t n3 = r.u8();
    r.skip(static_cast<std::size_t>(n3) * 12);
    g.frames = r.u8();
    r.u8(), r.u8();
    r.skip(4u * g7);
    if (!r.ok) {
        *error = "ship graphic entry 0 is truncated";
        return false;
    }
    g.loaded = true;
    if (const LesEntry* ef = find_entry_id(*table, 0xff)) {
        auto text = read_entry(les, *ef, error);
        if (!text) return false;
        const std::vector<std::string> t = Tokens(*text);
        g.has_text = true;
        g.sink_count = t.size() > 1 ? static_cast<std::uint8_t>(std::atoi(t[1].c_str())) : 0;
    }
    return true;
}

}  // namespace

bool GameData_LoadShips(GameData& d, std::span<const BasicParams> params, ByteSpan towers_txt,
                        const std::function<std::optional<Bytes>(const std::string&)>& read_les, std::string* error) {


    std::vector<TowerRow> rows;
    {
        const std::vector<std::string> t = Tokens(towers_txt);
        std::size_t i = 0;
        auto next = [&]() -> std::string {
            while (i < t.size() && !t[i].empty() && t[i][0] == '{') ++i;
            return i < t.size() ? t[i++] : std::string();
        };
        const auto count = static_cast<std::uint8_t>(std::atoi(next().c_str()));
        for (std::uint8_t k = 0; k < count; ++k) {
            ++i;  // [name]
            TowerRow row;
            row.attack = static_cast<std::uint16_t>(std::atol(next().c_str()));
            row.reload = static_cast<std::uint16_t>(std::atol(next().c_str()));
            row.bullets = static_cast<std::uint16_t>(std::atol(next().c_str()));
            row.fire_range = static_cast<float>(std::atof(next().c_str()));
            row.min_range = static_cast<float>(std::atof(next().c_str()));
            ++i;  // '#'
            rows.push_back(row);
        }
        if (rows.size() != count || i > t.size()) {
            *error = "ShipTowersParams.txt is truncated";
            return false;
        }
    }



    std::array<std::array<int, 40>, 6> gi{};
    for (auto& a : gi) a.fill(-1);
    std::vector<ShipGraphic> graphics;
    auto make = [&](std::size_t n, std::size_t t) -> bool {
        const UnitParams& r = params[n].units[t];
        ShipGraphic g;
        auto file = read_les(r.data_path);
        if (!file) {  // B262: Russia's cruiser while its own graphic is missing
            const std::string stand_in = unit_data_path_stand_in(static_cast<int>(n), static_cast<int>(t), r.data_path);
            if (!stand_in.empty()) file = read_les(stand_in);
        }
        if (file) {
            std::string err;
            if (!LoadShipGraphic(g, *file, static_cast<std::uint8_t>(t), static_cast<std::uint8_t>(n), rows, &err)) {
                *error = r.data_path + ".les: " + err;
                return false;
            }
        }
        gi[n][t] = static_cast<int>(graphics.size());
        graphics.push_back(std::move(g));
        return true;
    };
    for (std::size_t n = 0; n < params.size() && n < 6; ++n) {
        for (std::size_t t = 0; t < params[n].units.size() && t < 40; ++t) {
            const UnitParams& r = params[n].units[t];
            if (r.move_class < 13 || r.move_class > 15) continue;
            if (gi[n][t] != -1) continue;
            if (std::atol(r.data_path.c_str()) != 0) {
                const auto sn = static_cast<std::size_t>(std::atol(r.data_path.substr(0, 1).c_str()) - 1);
                const auto st = static_cast<std::size_t>(std::atol(r.data_path.substr(1, 2).c_str()));
                if (sn >= params.size() || sn >= 6 || st >= params[sn].units.size() || st >= 40) {
                    *error = "ship DataPath " + r.data_path + " names a missing type";
                    return false;
                }
                if (gi[sn][st] == -1 && !make(sn, st)) return false;
                gi[n][t] = gi[sn][st];
            } else if (!make(n, t)) {
                return false;
            }
            d.unit_types[n][t].anim_size[5] = graphics[static_cast<std::size_t>(gi[n][t])].frames;
        }
    }
    for (std::size_t n = 0; n < 6; ++n) {
        for (std::size_t t = 0; t < 40; ++t) {
            if (gi[n][t] != -1) d.unit_types[n][t].ship = graphics[static_cast<std::size_t>(gi[n][t])];
        }
    }
    return true;
}
}  // namespace antanta::sim
