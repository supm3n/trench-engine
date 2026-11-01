#include "render/iso.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "formats/image.hpp"
#include "formats/les.hpp"

namespace antanta {
namespace {

constexpr double kWaterKey = 110.0;

int floor_div(int a, int b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0)) ? 1 : 0); }

float f32_at(ByteSpan b, std::size_t pos) {
    const std::uint32_t u = read_u32(b, pos);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

std::string slashes(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
}

// Height, shade and tile lookups with the prototype's clamping (Terrain.at) and its vertex lattice.
struct Ground {
    const MapData& m;
    const MapCell& cell(int cx, int row) const {
        cx = std::clamp(cx, 0, m.width - 1);
        row = std::clamp(row, 0, m.height - 1);
        return m.cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(m.width) + cx];
    }
    double height(int cx, int row) const { return cell(cx, row).height; }
    double shade(int cx, int row) const { return cell(cx, row).shade; }

    // Diamond coordinates: A along (48,-24), B along (48,+24); the vertex of cell (cx,row) sits at integer (A,B).
    static void ab_of_cell(int cx, int row, int* a, int* b) {
        const int e = row % 2 == 0 ? 1 : 0;
        *a = floor_div(2 * cx + 1 - e - row, 2);
        *b = floor_div(2 * cx + 1 - e + row, 2);
    }
    static void cell_of_ab(int a, int b, int* cx, int* row) {
        *row = b - a;
        const int e = (*row & 1) == 0 ? 1 : 0;
        *cx = floor_div(a + b - 1 + e, 2);
    }
    // Bilinear over the vertex lattice. Guess (render.md 11): the game averages two edge interpolations.
    double interp(bool use_shade, double a, double b) const {
        const double fa0 = std::floor(a), fb0 = std::floor(b);
        const int a0 = static_cast<int>(fa0), b0 = static_cast<int>(fb0);
        const double fa = a - a0, fb = b - b0;
        auto v = [&](int aa, int bb) {
            int cx = 0, row = 0;
            cell_of_ab(aa, bb, &cx, &row);
            return use_shade ? shade(cx, row) : height(cx, row);
        };
        return (1 - fa) * (1 - fb) * v(a0, b0) + fa * (1 - fb) * v(a0 + 1, b0) + (1 - fa) * fb * v(a0, b0 + 1) +
               fa * fb * v(a0 + 1, b0 + 1);
    }
    double ground_height(double wx, double wy) const {
        const double x = (wx + 48) / 48.0, y = wy / 24.0;
        return interp(false, (x - y) / 2, (x + y) / 2);
    }
};

const HouseParams* house_params(const WorldGraphics& g, int nation, int type, int depth = 0) {
    if (nation < 0 || nation >= static_cast<int>(g.params.size()) || !g.params[nation] || type < 0 ||
        type >= static_cast<int>(g.params[nation]->houses.size()) || depth > 4) {
        return nullptr;
    }
    const HouseParams& h = g.params[nation]->houses[type];
    if (all_digits(h.data_path)) {
        return house_params(g, h.data_path[0] - '0' - 1, std::atoi(h.data_path.c_str() + 1), depth + 1);
    }
    return &h;
}

std::string unit_path(const WorldGraphics& g, int nation, int type, int depth = 0) {
    if (nation < 0 || nation >= static_cast<int>(g.params.size()) || !g.params[nation] || type < 0 ||
        type >= static_cast<int>(g.params[nation]->units.size()) || depth > 4) {
        return {};
    }
    const std::string& p = g.params[nation]->units[type].data_path;
    if (all_digits(p)) return unit_path(g, p[0] - '0' - 1, std::atoi(p.c_str() + 1), depth + 1);  // "NTT"
    return p;
}

std::optional<LesTable> open_les(const Bytes& file, const std::string& path, std::string* error) {
    auto t = parse_les(file, error);
    if (!t) *error = path + ": " + *error;
    return t;
}

std::optional<Bytes> les_id(const Bytes& file, const LesTable& t, int id) {
    const LesEntry* e = find_entry_id(t, static_cast<std::uint16_t>(id));
    if (!e) return std::nullopt;
    std::string err;
    return read_entry(file, *e, &err);
}

// The player's colour index (Colors.txt line); without one, the player index.
WorldRect rect_union(WorldRect a, WorldRect b) {
    return {std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1)};
}

int colour_of(const WorldView& v, int player) {
    return player >= 0 && player < static_cast<int>(v.colours.size()) ? v.colours[player] : player;
}

int nation_of(const WorldView& v, int player) {
    if (player == 8) return 5;  // a neutral house: BasicParams5.dat
    return player >= 0 && player < static_cast<int>(v.nations.size()) ? v.nations[player] : -1;
}

int graphic_of(const WorldView& v, const MapHouse& h) {
    const auto i = static_cast<std::size_t>(&h - v.houses.data());
    if (i < v.house_graphic.size()) return v.house_graphic[i];
    return h.build_state == 0 ? 0 : 10;  // phase: 0 normal, 10k build stage k (only stage 1 used)
}

}  // namespace

// Linear along the edges of the terrain triangles L-T-B and T-B-R (and of the neighbours' triangles for the points
// outside the cell); inner points average two edge points. Each value is a float, as the game stores it to a float
// global.
SubtileLattice subtile_lattice(const MapData& map, int cx, int row) {
    const Ground g{map};
    SubtileLattice s;
    const int p = row & 1;
    const float third = 1.0f / 3.0f, half = 0.5f, two_thirds = 2.0f / 3.0f;
    const float to_light = 1.0f / 255.0f;


    const MapCell* corner[4] = {&g.cell(cx, row), &g.cell(cx + p, row - 1), &g.cell(cx + p, row + 1),
                                &g.cell(cx + 1, row)};
    const MapCell& n1 = g.cell(cx, row + 2);
    const MapCell& n3 = g.cell(cx + p, row + 3);
    const MapCell& n4 = g.cell(cx + 1, row + 2);
    auto fill = [&](std::array<float, 23>& v, auto get) {
        v[0] = get(*corner[0]);
        v[3] = get(*corner[1]);
        v[15] = get(*corner[2]);
        v[18] = get(*corner[3]);
        float d = (v[3] - v[0]) * third;  // L -> T
        v[1] = v[0] + d;
        v[2] = v[1] + d;
        d = (v[15] - v[0]) * third;  // L -> B
        v[5] = v[0] + d;
        v[10] = v[5] + d;
        d = (v[15] - v[3]) * third;  // T -> B
        v[7] = v[3] + d;
        v[11] = v[7] + d;
        v[6] = (v[10] + v[2]) * half;
        d = (v[18] - v[3]) * third;  // T -> R
        v[8] = v[3] + d;
        v[13] = v[8] + d;
        d = (v[18] - v[15]) * third;  // B -> R
        v[16] = v[15] + d;
        v[17] = v[16] + d;
        v[12] = (v[16] + v[8]) * half;
        const float f1 = get(n1);
        v[4] = (f1 - v[0]) * third + v[0];
        v[14] = (v[15] - f1) * two_thirds + f1;
        v[9] = (v[14] + v[4]) * half;
        v[19] = (get(n3) - v[15]) * third + v[15];
        const float f4 = get(n4);
        v[20] = (f4 - v[15]) * third + v[15];
        v[22] = (f4 - v[18]) * third + v[18];
        v[21] = (v[22] + v[20]) * half;
    };
    fill(s.h, [](const MapCell& c) { return static_cast<float>(c.height); });
    fill(s.light, [&](const MapCell& c) { return static_cast<float>(c.shade) * to_light; });

    const MapCell& own = *corner[0];
    const MapCell& up = *corner[1];
    const MapCell& down = *corner[2];
    const MapCell& left_down = g.cell(cx + p - 1, row + 1);
    const MapCell& left_up = g.cell(cx + p - 1, row - 1);
    const MapCell& up2 = g.cell(cx, row - 2);
    static constexpr int kOwn[9] = {5, 6, 7, 10, 11, 12, 15, 16, 17};
    for (int k = 0; k < 9; ++k) s.id[kOwn[k]] = own.subtile[k];
    s.id[0] = left_up.subtile[6];
    s.id[1] = left_up.subtile[7];
    s.id[2] = left_up.subtile[8];
    s.id[3] = up2.subtile[6];
    s.id[4] = left_down.subtile[2];
    s.id[9] = left_down.subtile[5];
    s.id[14] = left_down.subtile[8];
    s.id[8] = up.subtile[0];
    s.id[13] = up.subtile[3];
    s.id[18] = up.subtile[6];
    s.id[19] = n1.subtile[2];
    s.id[20] = down.subtile[0];
    s.id[21] = down.subtile[1];
    s.id[22] = down.subtile[2];
    return s;
}

std::string unit_graphic_path(const WorldGraphics& g, int nation, int type) {
    const std::string p = unit_path(g, nation, type);
    return p.empty() ? p : "Data/Units/" + slashes(p) + ".les";
}

int view_house_nation(const WorldView& v, std::size_t i) {
    if (i < v.house_nation.size() && v.house_nation[i] >= 0) return v.house_nation[i];
    return i < v.houses.size() ? nation_of(v, v.houses[i].player) : -1;
}

const HouseParams* house_graphic_params(const WorldGraphics& g, int nation, int type) {
    return house_params(g, nation, type);
}

WorldView world_view_from_map(const MapData& map) {
    WorldView v;
    v.map = &map;
    v.houses = map.houses;
    v.units = map.units;
    static const std::pair<const char*, int> kKnown[] = {
        {"Германия", 3}, {"Франция", 2}, {"Россия", 0}, {"Англия", 1}, {"Австро-Венгрия", 4}};
    for (std::size_t i = 0; i < map.player_names.size(); ++i) {
        int n = static_cast<int>(i % 5);
        for (const auto& [name, nation] : kKnown) {
            if (map.player_names[i] == name) n = nation;
        }
        v.nations.push_back(n);
    }
    return v;
}

ViewPos clamp_view(const MapData& map, ViewPos p) {
    p.col = std::clamp(p.col, 0, std::max(0, map.width - kViewCols - 2));
    p.row = std::clamp(p.row, 0, std::max(0, map.height - kViewRows - 10));
    return p;
}

std::optional<WorldGraphics> load_world_graphics(const GameFileLoader& load, std::string* error) {
    WorldGraphics g;
    auto file = [&](const std::string& path) {
        auto f = load(path, error);
        if (!f && error->find(path) == std::string::npos) *error = path + ": " + *error;
        return f;
    };
    auto fail = [&](const std::string& path) {
        *error = path + ": " + *error;
        return std::nullopt;
    };

    const std::string land_path = "Data/LandTextures/LandTextures.dat";
    auto land_file = file(land_path);
    if (!land_file) return std::nullopt;
    auto land = parse_land_textures(*land_file, error);
    if (!land) return fail(land_path);
    for (const Image& img : land->textures) g.tiles.push_back(texture_from_image(img));



    const std::string m44_path = "Data/LandTextures/m44.bmp";
    auto m44_file = file(m44_path);
    if (!m44_file) return std::nullopt;
    auto m44 = decode_bmp(*m44_file, error);
    if (!m44) return fail(m44_path);
    for (const Texture& t : g.tiles) {
        Texture s;
        s.width = s.height = 128;
        s.rgba.resize(128 * 128 * 4);
        for (int y = 0; y < 128; ++y) {
            for (int x = 0; x < 128; ++x) {
                const std::size_t src = (static_cast<std::size_t>(y % t.height) * t.width + x % t.width) * 4;
                std::memcpy(&s.rgba[(static_cast<std::size_t>(y) * 128 + x) * 4], &t.rgba[src], 4);
            }
        }
        for (int oy : {0, 42, 85}) {
            for (int ox : {0, 42, 85}) {
                for (int y = 0; y < std::min(m44->height, 128 - oy); ++y) {
                    for (int x = 0; x < std::min(m44->width, 128 - ox); ++x) {
                        const std::uint8_t blue = m44->rgba[(static_cast<std::size_t>(y) * m44->width + x) * 4 + 2];
                        s.rgba[(static_cast<std::size_t>(oy + y) * 128 + ox + x) * 4 + 3] = blue >> 4;
                    }
                }
            }
        }
        g.splats.push_back(std::move(s));
    }

    const std::string obj_path = "Data/Objects/objects.les";
    auto obj_file = file(obj_path);
    if (!obj_file) return std::nullopt;
    auto obj_table = open_les(*obj_file, obj_path, error);
    if (!obj_table) return std::nullopt;
    for (int cid = 0; cid < 3; ++cid) {
        auto d = les_id(*obj_file, *obj_table, cid);
        if (!d) {
            *error = obj_path + ": entry " + std::to_string(cid) + " missing or unreadable";
            return std::nullopt;
        }
        auto ogr = decode_ogr(*d, error);
        if (!ogr) return fail(obj_path);
        g.objects[cid].entries = std::move(ogr->entries);
        for (const Image& p : ogr->pages) g.objects[cid].pages.push_back(texture_from_image(p));
    }

    const std::string water_path = "Data/Misc/Water/Anim0/Anim_0.les";
    auto water_file = file(water_path);
    if (!water_file) return std::nullopt;
    auto water_table = open_les(*water_file, water_path, error);
    if (!water_table) return std::nullopt;
    auto water0 = les_id(*water_file, *water_table, 0);
    if (!water0) {
        *error = water_path + ": entry 0 missing or unreadable";
        return std::nullopt;
    }
    auto water = decode_444(*water0, error);
    if (!water || water->width <= 0 || water->height <= 0) return fail(water_path);
    g.water = texture_from_image(*water);



    std::string trench_error;
    if (auto trench = load("Data/Misc/TrenchAndPonton.les", &trench_error)) {
        if (auto table = parse_les(*trench, &trench_error)) {
            for (const int id : {0xc8, 0xc9, 0xd2, 0xd3}) {
                auto d = les_id(*trench, *table, id);
                auto img = d ? decode_444(*d, &trench_error) : std::nullopt;
                if (img && img->width > 0 && img->height > 0) g.specials[id] = texture_from_image(*img);
            }
        }
    }

    // Landscape filter (render.md 3): dest = dest * (f + d - f * d) + dest * (1 - a), d = 0xE5 / 255 rounded as
    // the prototype does (0.898). Guess (render.md 11): the exact D3D7 ADDSMOOTH behaviour is unchecked.
    const std::string filter_path = "Data/LandTextures/Filter.tga";
    auto filter_file = file(filter_path);
    if (!filter_file) return std::nullopt;
    auto filter = decode_tga(*filter_file, error);
    if (!filter || filter->width <= 0 || filter->height <= 0) return fail(filter_path);
    g.filter_w = filter->width;
    g.filter_h = filter->height;
    const float d = 0.898f;
    for (std::size_t i = 0; i + 3 < filter->rgba.size(); i += 4) {
        std::array<float, 4> f{};
        for (int ch = 0; ch < 3; ++ch) {
            const float v = static_cast<float>(filter->rgba[i + ch]) / 255.0f;
            f[ch] = v + d - v * d;
        }
        f[3] = 1.0f - static_cast<float>(filter->rgba[i + 3]) / 255.0f;
        g.filter.push_back(f);
    }

    const std::string colours_path = "Data/Colors.txt";
    auto colours_file = file(colours_path);
    if (!colours_file) return std::nullopt;
    auto colours = parse_player_colours(*colours_file, error);
    if (!colours) return fail(colours_path);
    g.player_rgb = *colours;
    for (const auto& c : *colours) {
        g.player_colour.push_back(
            {static_cast<float>(c[0]) / 255.0f, static_cast<float>(c[1]) / 255.0f, static_cast<float>(c[2]) / 255.0f});
    }

    for (int n = 0; n < 6; ++n) {
        const std::string path = "Data/BasicParams" + std::to_string(n) + ".dat";
        auto f = file(path);
        if (!f) return std::nullopt;
        g.params[n] = parse_basic_params(*f, error);
        if (!g.params[n]) return fail(path);
        // B262: a unit whose graphic file is missing and has a stand-in (Russia's cruiser) draws the stand-in, as the
        // simulation's ship data (GameData_LoadShips) does.
        for (std::size_t t = 0; t < g.params[n]->units.size(); ++t) {
            UnitParams& u = g.params[n]->units[t];
            const std::string stand_in = unit_data_path_stand_in(n, static_cast<int>(t), u.data_path);
            std::string missing;
            if (!stand_in.empty() && !load("Data/Units/" + slashes(u.data_path) + ".les", &missing))
                u.data_path = stand_in;
        }
    }
    load_ground_fx_graphics(g.fx, load);
    load_house_fx_graphics(g.house_fx, load);
    return g;
}

void load_view_graphics(WorldGraphics& g, const WorldView& view, const GameFileLoader& load,
                        std::vector<std::string>* warnings) {
    for (const MapHouse& h : view.houses) {
        const int nation = view_house_nation(view, static_cast<std::size_t>(&h - view.houses.data()));
        const int base = graphic_of(view, h);
        const std::array<int, 3> key{nation, h.type, base};
        if (g.houses.count(key)) continue;
        g.houses[key];  // tried; stays empty (no picture) on failure
        const HouseParams* hp = house_params(g, nation, h.type);
        if (!hp) {
            warnings->push_back("no house record nation " + std::to_string(nation) + " type " + std::to_string(h.type));
            continue;
        }
        const std::string path = "Data/Objects/Houses/" + slashes(hp->data_path) + ".les";
        std::string err;
        auto file = load(path, &err);
        auto table = file ? parse_les(*file, &err) : std::nullopt;
        if (!table) {
            warnings->push_back(path + ": " + err);
            continue;
        }
        auto img_data = les_id(*file, *table, base);
        auto img = img_data ? decode_img(*img_data, &err) : std::nullopt;
        if (!img) {
            warnings->push_back(path + ": no picture for phase " + std::to_string(base));
            continue;
        }
        HouseGraphics hg;
        hg.anchor_x = hp->anchor_x;
        hg.anchor_y = hp->anchor_y;
        if (auto d = les_id(*file, *table, base + 3)) {
            auto m = decode_img(*d, &err);
            if (m && m->width == img->width && m->height == img->height) hg.mask = std::move(*m);
        }
        if (auto d = les_id(*file, *table, base + 2)) {
            const std::size_t n = static_cast<std::size_t>(img->width) * static_cast<std::size_t>(img->height);
            if (d->size() >= 6 && std::memcmp(d->data(), "~cHMap", 6) == 0) {
                auto z = decode_hzb(*d, &err);
                if (z && z->width == img->width && z->height == img->height) {
                    for (std::uint16_t v : z->depth) hg.depth.push_back(static_cast<std::int16_t>(v));
                    hg.blit_depth = std::move(z->blit);
                }
            } else if (d->size() >= n * 2) {  // raw u16[w * h] with the picture's size
                for (std::size_t i = 0; i < n; ++i) hg.depth.push_back(static_cast<std::int16_t>(read_u16(*d, i * 2)));
                hg.blit_depth = hg.depth;
            }
            if (hg.depth.empty()) warnings->push_back(path + ": depth map does not fit the picture");
        }
        if (auto d = les_id(*file, *table, base + 1)) {
            if (auto s = decode_g4m(*d, &err)) {
                hg.shadow_texture = texture_from_image(s->image);
                hg.shadow = std::move(*s);
            }
        }
        hg.picture = std::move(*img);
        g.houses[key] = std::move(hg);
    }
    for (const MapUnit& u : view.units) {
        const int nation = nation_of(view, u.player);
        const std::array<int, 2> key{nation, u.type};
        if (g.units.count(key)) continue;
        g.units[key];  // tried
        if (nation < 0 || nation >= 6 || !g.params[nation] || u.type >= g.params[nation]->units.size()) {
            warnings->push_back("no unit record nation " + std::to_string(nation) + " type " + std::to_string(u.type));
            continue;
        }
        const UnitParams& up = g.params[nation]->units[u.type];
        if (up.move_class >= 13 && up.move_class <= 15) {  // ships: per-direction .shp parts, drawing not decoded
            warnings->push_back("ships are not drawn (nation " + std::to_string(nation) + " type " +
                                std::to_string(u.type) + ")");
            continue;
        }
        const std::string path = "Data/Units/" + slashes(unit_path(g, nation, u.type)) + ".les";
        std::string err;
        auto file = load(path, &err);
        auto table = file ? parse_les(*file, &err) : std::nullopt;
        if (!table) {
            warnings->push_back(path + ": " + err);
            continue;
        }
        auto d = les_id(*file, *table, 0);  // NumWork 0 (walk)
        auto ugr = d ? decode_ugr(*d, &err) : std::nullopt;
        if (!ugr) {
            warnings->push_back(path + ": no walk animation " + err);
            continue;
        }
        UnitGraphics ug;
        ug.width = up.width;
        ug.height = up.height;
        ug.anchor_x = up.anchor_x;
        ug.anchor_y = up.anchor_y;
        auto take = [&](const std::vector<SpriteFrame>& layer, std::array<UnitFrame, 5>& out) {
            for (int dir = 0; dir < 5; ++dir) {
                const SpriteFrame& f = layer[static_cast<std::size_t>(dir * ugr->frames_per_dir)];
                if (!f.has_pixels) continue;
                out[dir] = {true, f.off_x, f.off_y, texture_from_image(f.image)};
            }
        };
        take(ugr->layers[0], ug.sprite);
        if (ugr->flags & 2) take(ugr->layers[1], ug.mask);
        g.units[key] = std::move(ug);
    }
}

namespace {

// The static layer for screen rows band_top <= y < band_bottom only. Every pixel gets exactly the operations it gets in
// a full-screen pass, so bands can run in parallel and the result does not depend on the band split.
void static_band(const WorldGraphics& g, const WorldView& view, int ox, int oy, Target& rt, int band_top,
                 int band_bottom) {
    const MapData& m = *view.map;
    const Ground ground{m};
    auto band = [&](DrawState s) {
        s.clip_top = band_top;
        s.clip_bottom = band_bottom;
        return s;
    };
    // The corner (ox, oy) in world pixels; (sc, sr) the cell it lies in. The picture of a pixel does not depend on the
    // corner, so tiles drawn for their own corners join without seams (StaticTiles).
    const int sc = floor_div(ox, 96), sr = floor_div(oy, 24);
    const int vx = ox, vy = oy;
    // The view in cells: 11 x 34 at 1024 x 768 (kViewCols, kViewRows); larger views get as many more as they need.
    const int view_cols = (rt.width + (ox - sc * 96) + 95) / 96, view_rows = (rt.height + (oy - sr * 24) + 23) / 24 + 2;


    struct HouseDraw {
        const MapHouse* h;
        const HouseGraphics* gfx;
        double hgt;
        int cxs, cys;
    };
    std::vector<const MapHouse*> visible;
    for (const MapHouse& h : view.houses) {
        if (sc - 6 < h.cell_x && h.cell_x < sc + view_cols + 6 && sr - 30 < h.cell_y &&
            h.cell_y < sr + view_rows + 30) {
            visible.push_back(&h);
        }
    }
    std::stable_sort(visible.begin(), visible.end(), [](const MapHouse* a, const MapHouse* b) {
        return std::int64_t{a->cell_y} * 100 + a->cell_x < std::int64_t{b->cell_y} * 100 + b->cell_x;
    });
    std::vector<HouseDraw> house_draw;
    for (const MapHouse* h : visible) {
        const int nation = view_house_nation(view, static_cast<std::size_t>(h - view.houses.data()));
        const auto it = g.houses.find({nation, h->type, graphic_of(view, *h)});
        if (it == g.houses.end() || it->second.picture.width == 0) continue;
        const HouseGraphics& hg = it->second;
        const double hgt = house_draw_height(m, *h), key_hgt = house_key_height(m, *h);
        const int cxs = h->cell_x * 96 - (h->cell_y % 2 == 0 ? 48 : 0) - vx + 48;
        const int cys = h->cell_y * 24 - vy;
        house_draw.push_back({h, &hg, hgt, cxs, cys});
        const int x0 = cxs - hg.anchor_x;
        const int y0 = static_cast<int>(cys + 24 - hgt - hg.anchor_y);
        std::uint32_t p565 = 0;
        const int colour = colour_of(view, h->player);
        const bool tint = hg.mask && colour >= 0 && colour < static_cast<int>(g.player_colour.size());
        if (tint) {
            std::array<std::uint32_t, 3> p{};
            for (int ch = 0; ch < 3; ++ch) p[ch] = static_cast<std::uint32_t>(g.player_colour[colour][ch] * 255.0f);
            p565 = (p[0] >> 3) << 11 | (p[1] >> 2) << 5 | (p[2] >> 3);
        }
        const Image& pic = hg.picture;
        for (int py = std::max(0, band_top - y0); py < std::min(pic.height, band_bottom - y0); ++py) {
            for (int px = std::max(0, -x0); px < std::min(pic.width, rt.width - x0); ++px) {
                if (view.see_through && ((px + py) & 1) != 0) continue;
                const std::size_t si = static_cast<std::size_t>(py) * pic.width + px;
                const std::size_t di = static_cast<std::size_t>(y0 + py) * rt.width + (x0 + px);
                float* dst = &rt.rgb[di * 3];
                if (pic.rgba[si * 4 + 3]) {
                    for (int ch = 0; ch < 3; ++ch) dst[ch] = static_cast<float>(pic.rgba[si * 4 + ch] / 255.0);
                }
                if (tint && hg.mask->rgba[si * 4 + 3]) {
                    const std::uint8_t* q = &hg.mask->rgba[si * 4];
                    const std::uint32_t q565 = ((std::uint32_t{q[0]} >> 3) << 11 | (std::uint32_t{q[1]} >> 2) << 5 |
                                                (std::uint32_t{q[2]} >> 3)) &
                                               p565;
                    dst[0] = static_cast<float>((q565 >> 11) * 255 / 31 / 255.0);
                    dst[1] = static_cast<float>((q565 >> 5 & 63) * 255 / 63 / 255.0);
                    dst[2] = static_cast<float>((q565 & 31) * 255 / 31 / 255.0);
                }
                if (!hg.blit_depth.empty() && hg.blit_depth[si] < 0x7FFF) {  // key = pixel y + hzb + 2 * key height
                    rt.key[di] = static_cast<float>((y0 + py) + hg.blit_depth[si] + 2 * key_hgt);
                    rt.water[di] = 0;
                }
            }
        }
    }



    DrawState water_state;
    water_state.tint = {0.1f, 0.1f, 0.3f};
    water_state.water = true;
    water_state = band(water_state);
    auto wv = [](double x, double y) {
        Vertex v;
        v.x = x;
        v.y = y;
        v.key = y + kWaterKey;
        return v;
    };
    for (int gy = sr; gy < std::min(sr + view_rows + 1, m.height); ++gy) {
        for (int gx = 2 * sc; gx < std::min(2 * sc + 2 * view_cols + 2, 2 * m.width); ++gx) {
            const std::size_t gi = static_cast<std::size_t>(gy) * 2 * m.width + gx;
            if (gi >= m.obstacle_grid.size()) continue;
            const int v = m.obstacle_grid[gi];
            if (v == 0) continue;
            const int x = gx * 48 - vx, y = gy * 24 - vy;
            if (v & 0x8000) {
                const std::size_t r = v & 0x7FFF;
                if (r >= m.routes.size()) continue;
                for (const auto& poly : m.routes[r].lists) {
                    for (std::size_t i = 1; i + 1 < poly.size(); ++i) {
                        draw_triangle(rt, wv(x + (poly[0] & 0xFF), y + (poly[0] >> 8)),
                                      wv(x + (poly[i] & 0xFF), y + (poly[i] >> 8)),
                                      wv(x + (poly[i + 1] & 0xFF), y + (poly[i + 1] >> 8)), water_state);
                    }
                }
            } else {
                draw_quad(rt, {wv(x, y), wv(x + 48, y), wv(x, y + 24), wv(x + 48, y + 24)}, water_state);
            }
        }
    }


    struct Splat {
        int cx, row, x, row_y;
    };
    struct Object {
        int value, cx, row, x, row_y;
        double h;
    };
    std::vector<Splat> splats;
    std::vector<Object> objects;
    DrawState terrain;
    terrain.gouraud = true;
    terrain = band(terrain);
    for (int row = sr; row < std::min(sr + view_rows + 20, m.height); ++row) {
        const int row_y = row * 24 - vy;
        for (int cx = std::max(sc - 1, 0); cx < std::min(sc + view_cols + 1, m.width); ++cx) {
            const MapCell& c = ground.cell(cx, row);
            if (c.tile == 0xFFFF) continue;
            const int p = row & 1;
            const double hl = c.height, ht = ground.height(cx + p, row - 1), hb = ground.height(cx + p, row + 1),
                         hr = ground.height(cx + 1, row);
            const int x = cx * 96 - vx - (row % 2 == 0 ? 48 : 0);
            Vertex l{static_cast<double>(x), row_y - hl, row_y + hl, 0, 0, c.shade / 255.0};
            Vertex t{x + 48.0, row_y - 24 - ht, row_y - 24 + ht, 1, 0, ground.shade(cx + p, row - 1) / 255.0};
            Vertex b{x + 48.0, row_y + 24 - hb, row_y + 24 + hb, 0, 1, ground.shade(cx + p, row + 1) / 255.0};
            Vertex r{x + 96.0, row_y - hr, row_y + hr, 1, 1, ground.shade(cx + 1, row) / 255.0};
            int idx = (c.tile & 0x7FFF) - 1;
            if (idx < 0 || idx >= static_cast<int>(g.tiles.size())) idx = 0;
            if (!g.tiles.empty()) {
                terrain.texture = &g.tiles[idx];
                draw_triangle(rt, l, t, b, terrain);
                draw_triangle(rt, t, b, r, terrain);
            }
            if (c.tile & 0x8000) splats.push_back({cx, row, x, row_y});
            const int o = c.object & 0x7FFF;
            if (o >= 9000 && o <= 0x25E3) objects.push_back({o, cx, row, x, row_y, hl});
        }
    }



    static constexpr int kCentre[9] = {5, 6, 7, 10, 11, 12, 15, 16, 17};
    static constexpr double kU0[9] = {0, -.5, 0, 0, -.5, 0, 0, -.5, 0};
    static constexpr double kV0[9] = {0, 0, 0, .5, .5, .5, 0, 0, 0};
    static constexpr int kRing[7] = {-5, -4, 1, 5, 4, -1, -5};
    DrawState splat_state;
    splat_state.gouraud = true;
    splat_state.z_write = false;
    splat_state.blend = true;
    splat_state = band(splat_state);
    for (const Splat& sp : splats) {
        const SubtileLattice lat = subtile_lattice(m, sp.cx, sp.row);
        const MapCell& c = ground.cell(sp.cx, sp.row);
        for (int s = 0; s < 9; ++s) {
            int sid = c.subtile[s];
            if (sid == 0) continue;
            sid = sid - 1 < static_cast<int>(g.splats.size()) ? sid - 1 : 0;
            if (g.splats.empty()) continue;
            splat_state.texture = &g.splats[sid];

            float du = 0.002f * static_cast<float>(s), dv = 0.001f * static_cast<float>(s);
            static constexpr float kNudge[6][2] = {{0, .05f},  {-.01f, .01f}, {-.05f, 0},
                                                   {0, -.05f}, {.01f, -.01f}, {.05f, 0}};
            for (int j = 0; j < 6; ++j) {
                if (lat.id[kCentre[s] + kRing[j]] == lat.id[kCentre[s]]) {
                    du += kNudge[j][0];
                    dv += kNudge[j][1];
                }
            }
            std::array<Vertex, 8> pts;
            for (int j = 0; j < 8; ++j) {
                const int k = kCentre[s] + (j == 0 ? 0 : kRing[j - 1]);  // lattice index e = 5b + a
                const int a = (k + 1) % 5 - 1, b = (k + 1) / 5;
                const double fx = sp.x + 16 * a + 16 * b, fy = sp.row_y - 8 * a + 8 * b;
                const double h = lat.h[k];

                const double shade = static_cast<int>(lat.light[k] * 255.0f) / 255.0;
                pts[j] = {fx,
                          fy - h,
                          fy + h + 0.5,
                          kU0[s] + 0.5 + a / 6.0 + (j == 0 ? du : 0.0f),
                          kV0[s] + b / 6.0 + (j == 0 ? dv : 0.0f),
                          shade};
            }
            for (int i = 1; i < 7; ++i) draw_triangle(rt, pts[0], pts[i], pts[i + 1], splat_state);
        }
    }





    DrawState special_state;
    special_state.colour_key = true;
    special_state.blend = true;
    special_state = band(special_state);
    for (const SpecialDraw& o : view.specials) {
        const auto it = g.specials.find(10 * o.kind + (o.built ? 0 : 1));
        if (it == g.specials.end()) continue;
        const Texture& t = it->second;
        const int cx = (o.x * 2 - (~o.y & 1)) * 48 + (o.kind == 0x15 ? -0x4f : -0x30) + 0x70;
        const double x0 = cx - t.width / 2 - vx, y0 = o.y * 24 - 0x44 - t.height / 2 - vy;
        const double x1 = x0 + t.width, y1 = y0 + t.height;
        special_state.texture = &t;
        draw_quad(rt,
                  {Vertex{x0, y0, y0 + 2 * 0x38, 0, 0}, Vertex{x1, y0, y0 + 2 * 0x38, 0.99999, 0},
                   Vertex{x0, y1, y1 + 2 * 0x38, 0, 0.99999}, Vertex{x1, y1, y1 + 2 * 0x38, 0.99999, 0.99999}},
                  special_state);
    }

    // 5. Map objects: trees, stones, miscs; each list from its end (bottom rows first); shadow then sprite.
    // Animated deposits 0x2581/0x2582 and the fields 0x25de.. are drawn every frame (render/ground_fx, render.md 12);
    // here only the fields' depth box (step 7).
    std::array<std::vector<std::pair<int, const Object*>>, 3> lists;
    for (const Object& o : objects) {
        if (o.value < 0x251C) {
            lists[0].push_back({o.value - 9000, &o});
        } else if (o.value < 0x2580) {
            lists[1].push_back({o.value - 0x251C, &o});
        } else if (o.value != 0x2581 && o.value != 0x2582 && o.value < 0x25DE) {
            lists[2].push_back({o.value - 0x2580, &o});
        }
    }
    DrawState shadow_state;
    shadow_state.z_write = false;
    shadow_state.blend = true;
    shadow_state = band(shadow_state);
    DrawState sprite_state;
    sprite_state.colour_key = true;
    sprite_state.blend = true;
    sprite_state = band(sprite_state);
    for (int cid = 0; cid < 3; ++cid) {
        const ObjectAtlas& atlas = g.objects[cid];
        const std::size_t n_recs = atlas.entries.size() / 56;
        for (auto it = lists[cid].rbegin(); it != lists[cid].rend(); ++it) {
            const auto [i, o] = *it;
            if (i < 0 || static_cast<std::size_t>(i) >= n_recs) continue;
            const ByteSpan r = ByteSpan(atlas.entries).subspan(static_cast<std::size_t>(i) * 56, 56);
            const int page = r[1];
            const double u0 = f32_at(r, 4), u1 = f32_at(r, 8), v0 = f32_at(r, 12), v1 = f32_at(r, 16);
            const int w = r[0x14], hs = r[0x15];
            const int cxs = o->x + 48;
            // Guess (render.md 11, medium-high): bottom = rowY + 18 - h.
            const double bottom = o->row_y - 24 - o->h + 42;
            const double kb = bottom + 2 * o->h;
            const double kt = o->value != 0x2583 ? kb + hs : bottom - hs / 2 + 2 * o->h;
            const double x0 = cxs - w / 2, x1 = cxs + w / 2, y0 = bottom - hs;
            if (r[0x1C] != 0xFF && r[0x1C] < atlas.pages.size()) {
                const double su0 = f32_at(r, 0x20), su1 = f32_at(r, 0x24), sv0 = f32_at(r, 0x28), sv1 = f32_at(r, 0x2c);
                const int sw = r[0x30], sh = r[0x31];
                const int sox = static_cast<std::int16_t>(read_u16(r, 0x32)),
                          soy = static_cast<std::int16_t>(read_u16(r, 0x34));
                const double sx0 = x0 + sox, sy0 = y0 + soy;
                shadow_state.texture = &atlas.pages[r[0x1C]];
                draw_quad(rt,
                          {Vertex{sx0, sy0, kt + soy, su0, sv0}, Vertex{sx0 + sw, sy0, kt + soy, su1, sv0},
                           Vertex{sx0, sy0 + sh, kb + soy, su0, sv1}, Vertex{sx0 + sw, sy0 + sh, kb + soy, su1, sv1}},
                          shadow_state);
            }
            if (static_cast<std::size_t>(page) < atlas.pages.size()) {
                sprite_state.texture = &atlas.pages[page];
                draw_quad(rt,
                          {Vertex{x0, y0, kt, u0, v0}, Vertex{x1, y0, kt, u1, v0}, Vertex{x0, bottom, kb, u0, v1},
                           Vertex{x1, bottom, kb, u1, v1}},
                          sprite_state);
            }
        }
    }


    for (const HouseDraw& hd : house_draw) {
        if (hd.h->type == 0 || hd.h->type == 1 || hd.h->type == 0x16 || !hd.gfx->shadow) continue;
        const G4m& s = *hd.gfx->shadow;
        const double hc = hd.hgt - 1;
        const double x0 = hd.cxs - hd.gfx->anchor_x + s.off_x;
        const double y0 = hd.cys + 24 - hc - hd.gfx->anchor_y + s.off_y;
        const double k = hd.cys - 24 + 2 * hc;
        const double w = s.image.width, h = s.image.height;
        shadow_state.texture = &hd.gfx->shadow_texture;
        draw_quad(rt,
                  {Vertex{x0, y0, k, 0, 0}, Vertex{x0 + w, y0, k, 0.99999, 0}, Vertex{x0, y0 + h, k, 0, 0.99999},
                   Vertex{x0 + w, y0 + h, k, 0.99999, 0.99999}},
                  shadow_state);
    }




    static const Texture kClear{1, 1, {0, 0, 0, 0}};
    DrawState box_state;
    box_state.texture = &kClear;
    box_state.blend = true;
    box_state = band(box_state);
    for (const Object& o : objects) {
        const int a = anim_index(o.value);
        if (a < 2) continue;
        const double H = g.fx.anims[static_cast<std::size_t>(a)].raise;
        const int p = o.row & 1;
        const double hl = o.h, ht = ground.height(o.cx + p, o.row - 1), hb = ground.height(o.cx + p, o.row + 1),
                     hr = ground.height(o.cx + 1, o.row);
        const Vertex l{static_cast<double>(o.x), o.row_y - hl, o.row_y + hl, 0, 0};
        const Vertex t{o.x + 48.0, o.row_y - 24 - ht, o.row_y - 24 + ht, 0, 0};
        const Vertex b{o.x + 48.0, o.row_y + 24 - hb, o.row_y + 24 + hb, 0, 0};
        const Vertex r{o.x + 96.0, o.row_y - hr, o.row_y + hr, 0, 0};
        auto up = [H](Vertex v) {
            v.y -= H, v.key += H;
            return v;
        };
        draw_quad(rt, {up(l), up(b), l, b}, box_state);
        draw_quad(rt, {up(l), up(t), up(b), up(r)}, box_state);
        draw_quad(rt, {up(b), up(r), b, r}, box_state);
    }
}

}  // namespace

void render_static_layer(const WorldGraphics& g, const WorldView& view, ViewPos pos, Target* out) {
    out->reset(view.width, view.height);
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    static_band(g, view, view_x(pos), view_y(pos), *out, 0, view.height);  // a web build without threads
#else
    const int n = static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 8u));
    std::vector<std::thread> workers;
    for (int i = 0; i < n; ++i) {
        workers.emplace_back(static_band, std::cref(g), std::cref(view), view_x(pos), view_y(pos), std::ref(*out),
                             view.height * i / n, view.height * (i + 1) / n);
    }
    for (std::thread& w : workers) w.join();
#endif
}

namespace {



void animate_water(const WorldGraphics& g, Target& rt, int vx, int vy, int top, int bottom) {
    for (int y = top; y < bottom; ++y) {
        for (int x = 0; x < rt.width; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * rt.width + x;
            if (!rt.water[i]) continue;
            const std::size_t ti =
                (static_cast<std::size_t>((y + vy) % g.water.height) * g.water.width + (x + vx) % g.water.width) * 4;
            for (int ch = 0; ch < 3; ++ch) rt.rgb[i * 3 + ch] = static_cast<float>(g.water.rgba[ti + ch]) / 15.0f;
        }
    }
}

}  // namespace

void render_frame_begin(const WorldGraphics& g, ViewPos pos, const Target& static_layer, Target* work) {
    Target& rt = *work;
    rt = static_layer;
    parallel_bands(rt.height,
                   [&](int top, int bottom) { animate_water(g, rt, view_x(pos), view_y(pos), top, bottom); });
}

ViewPos view_pos_at(int x, int y) {
    const int col = floor_div(x, 96), row = floor_div(y, 24);
    return {col, row, x - col * 96, y - row * 24};
}

void static_tiles_clear(StaticTiles& t) { t.tiles.clear(); }

WorldRect house_static_rect(const WorldGraphics& g, const MapData& map, const MapHouse& h, int nation, int graphic) {
    // As static_band draws it (steps 1 and 6). No graphics: static_band draws nothing for it either.
    const auto it = g.houses.find({nation, h.type, graphic});
    if (it == g.houses.end() || it->second.picture.width == 0) return {};
    const HouseGraphics& hg = it->second;
    const int hgt = house_draw_height(map, h);
    const int cxs = h.cell_x * 96 - (h.cell_y % 2 == 0 ? 48 : 0) + 48, cys = h.cell_y * 24;
    const int x0 = cxs - hg.anchor_x, y0 = cys + 24 - hgt - hg.anchor_y;
    WorldRect r{x0, y0, x0 + hg.picture.width, y0 + hg.picture.height};
    if (hg.shadow) {
        const int sx = x0 + hg.shadow->off_x, sy = y0 + 1 + hg.shadow->off_y;
        r = rect_union(r, {sx, sy, sx + hg.shadow->image.width + 1, sy + hg.shadow->image.height + 1});
    }
    return {r.x0 - 2, r.y0 - 2, r.x1 + 2, r.y1 + 2};
}

int house_draw_height(const MapData& map, const MapHouse& h) {
    if ((h.player == 8 && h.type <= 1) || h.type == 0x16) return 55;
    return static_cast<int>(Ground{map}.height(h.cell_x, h.cell_y)) + 1;
}

int house_key_height(const MapData& map, const MapHouse& h) {
    if (h.type == 0x16) return 0x5a;
    return house_draw_height(map, h);
}

WorldRect cell_static_rect(const WorldGraphics& g, const MapData& map, int cx, int row, int object) {
    // The terrain quads of the cell and of its neighbours (their corners share its height and shade, their sub-tile
    // lattices read its sub-tiles), a height lifts a corner by up to 255 pixels.
    const int x = cx * 96 - (row % 2 == 0 ? 48 : 0), row_y = row * 24;
    WorldRect r{x - 112, row_y - 48 - 24 - 256 - 8, x + 96 + 112, row_y + 48 + 24 + 8};
    // The object on it (step 5): sprite and shadow from its atlas record.
    const int o = object & 0x7FFF;
    const int cid = o < 9000 || o > 0x25E3 ? -1 : o < 0x251C ? 0 : o < 0x2580 ? 1 : 2;
    if (cid < 0) return r;
    const int i = o - (cid == 0 ? 9000 : cid == 1 ? 0x251C : 0x2580);
    const ObjectAtlas& atlas = g.objects[cid];
    if (static_cast<std::size_t>(i) >= atlas.entries.size() / 56) return r;
    const ByteSpan rec = ByteSpan(atlas.entries).subspan(static_cast<std::size_t>(i) * 56, 56);
    const int w = rec[0x14], hs = rec[0x15];
    const int bottom = row_y - 24 - static_cast<int>(Ground{map}.height(cx, row)) + 42;
    const int x0 = x + 48 - w / 2 - 1, y0 = bottom - hs - 1;
    r = rect_union(r, {x0, y0, x0 + w + 3, bottom + 2});
    if (rec[0x1C] != 0xFF) {
        const int sx = x0 + static_cast<std::int16_t>(read_u16(rec, 0x32)),
                  sy = y0 + static_cast<std::int16_t>(read_u16(rec, 0x34));
        r = rect_union(r, {sx, sy, sx + rec[0x30] + 3, sy + rec[0x31] + 3});
    }
    return r;
}

WorldRect special_static_rect(const SpecialDraw& o) {
    // As step 4b draws it: 128 x 128 (TrenchAndPonton's pictures), two pixels more on each side.
    const int cx = (o.x * 2 - (~o.y & 1)) * 48 + (o.kind == 0x15 ? -0x4f : -0x30) + 0x70, cy = o.y * 24 - 0x44;
    return {cx - 64 - 2, cy - 64 - 2, cx + 64 + 2, cy + 64 + 2};
}

void static_tiles_invalidate(StaticTiles& t, WorldRect r) {
    if (r.x1 <= r.x0 || r.y1 <= r.y0) return;
    constexpr int T = StaticTiles::kSize;
    for (auto it = t.tiles.begin(); it != t.tiles.end();) {
        const auto [tx, ty] = it->first;
        const bool hit = tx * T < r.x1 && r.x0 < (tx + 1) * T && ty * T < r.y1 && r.y0 < (ty + 1) * T;
        it = hit ? t.tiles.erase(it) : std::next(it);
    }
}

void render_frame_begin_tiles(const WorldGraphics& g, const WorldView& view, StaticTiles& tiles, ViewPos pos, int w,
                              int h, Target* work, int prefetch, bool water) {
    constexpr int T = StaticTiles::kSize;
    const int x0 = view_x(pos), y0 = view_y(pos);
    const int tx0 = floor_div(x0, T), ty0 = floor_div(y0, T);
    const int tx1 = floor_div(x0 + w - 1, T), ty1 = floor_div(y0 + h - 1, T);
    for (auto it = tiles.tiles.begin(); it != tiles.tiles.end();) {  // keep the view and one tile around it
        const auto [tx, ty] = it->first;
        it = tx < tx0 - 1 || tx > tx1 + 1 || ty < ty0 - 1 || ty > ty1 + 1 ? tiles.tiles.erase(it) : std::next(it);
    }
    // The tiles under the view first (needed now), then up to `prefetch` of the ring (needed when the view moves on).
    std::vector<std::array<int, 2>> todo;
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            if (!tiles.tiles.count({tx, ty})) todo.push_back({tx, ty});
        }
    }
    int extra = 0;
    for (int ty = ty0 - 1; ty <= ty1 + 1 && extra < prefetch; ++ty) {
        for (int tx = tx0 - 1; tx <= tx1 + 1 && extra < prefetch; ++tx) {
            const bool inside = tx >= tx0 && tx <= tx1 && ty >= ty0 && ty <= ty1;
            if (inside || tx < 0 || ty < 0 || tiles.tiles.count({tx, ty})) continue;
            todo.push_back({tx, ty});
            ++extra;
        }
    }
    std::vector<Target*> out;
    for (const auto& k : todo) {
        Target& t = tiles.tiles[k];
        t.reset(T, T);
        out.push_back(&t);
    }
#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    for (std::size_t i = 0; i < todo.size(); ++i) {  // a web build without threads
        static_band(g, view, todo[i][0] * T, todo[i][1] * T, *out[i], 0, T);
    }
#else
    // Each new tile in kBands row bands, the jobs taken one by one from a counter by the kept band threads (a thread
    // per tile every frame cost more than the drawing; the picture does not depend on the band split, static_band).
    constexpr int kBands = 4;
    const int jobs = static_cast<int>(todo.size()) * kBands;
    std::atomic<int> next{0};
    if (jobs > 0) {
        parallel_bands(T, [&](int, int) {
            for (int j = next++; j < jobs; j = next++) {
                const std::size_t i = static_cast<std::size_t>(j / kBands);
                const int b = j % kBands;
                static_band(g, view, todo[i][0] * T, todo[i][1] * T, *out[i], T * b / kBands, T * (b + 1) / kBands);
            }
        });
    }
#endif
    tiles.drawn += static_cast<int>(todo.size());

    // The view: rows of the tiles under it, keys moved from the tile's corner to the view's.
    Target& rt = *work;
    const std::size_t n_px = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    rt.width = w, rt.height = h;
    rt.rgb.resize(n_px * 3);
    rt.key.resize(n_px);
    rt.water.resize(n_px);
    parallel_bands(h, [&](int top, int bottom) {
        for (int y = top; y < bottom; ++y) {
            const int ty = floor_div(y0 + y, T), ry = y0 + y - ty * T;
            const auto key_shift = static_cast<float>(ty * T - y0);
            for (int tx = tx0; tx <= tx1; ++tx) {
                const int a = std::max(x0, tx * T), b = std::min(x0 + w, (tx + 1) * T);
                const Target& t = tiles.tiles.at({tx, ty});
                const std::size_t src = static_cast<std::size_t>(ry) * T + static_cast<std::size_t>(a - tx * T);
                const std::size_t dst = static_cast<std::size_t>(y) * w + static_cast<std::size_t>(a - x0);
                const auto len = static_cast<std::size_t>(b - a);
                std::memcpy(&rt.rgb[dst * 3], &t.rgb[src * 3], len * 3 * sizeof(float));
                std::memcpy(&rt.water[dst], &t.water[src], len);
                for (std::size_t i = 0; i < len; ++i) rt.key[dst + i] = t.key[src + i] + key_shift;
            }
        }
        if (water) animate_water(g, rt, x0, y0, top, bottom);
    });
}

void render_frame(const WorldGraphics& g, const WorldView& view, ViewPos pos, const Target& static_layer, Target* work,
                  std::vector<std::uint8_t>* rgba) {
    const MapData& m = *view.map;
    const Ground ground{m};
    render_frame_begin(g, pos, static_layer, work);
    Target& rt = *work;
    const int vx = view_x(pos), vy = view_y(pos);


    struct UnitDraw {
        double sort_y, sort_x;
        const MapUnit* u;
        const UnitGraphics* gfx;
        int dir;
        bool mirror;
        double c4, sx, sy;
    };
    std::vector<UnitDraw> drawn;
    for (const MapUnit& u : view.units) {
        const double sx0 = u.world_x - vx, sy0 = u.world_y - vy;
        if (!(-200 < sx0 && sx0 < rt.width + 200 && -200 < sy0 && sy0 < rt.height + 300)) continue;
        const auto it = g.units.find({nation_of(view, u.player), u.type});
        if (it == g.units.end()) continue;
        const UnitGraphics& ug = it->second;
        // Guess (render.md 11, low): orientation = map byte - 1; 0 ("random") becomes (int x + int y) & 7.
        const int orient =
            u.orient_plus1
                ? (u.orient_plus1 - 1) & 7
                : static_cast<int>((static_cast<std::int64_t>(u.world_x) + static_cast<std::int64_t>(u.world_y)) & 7);
        const bool mirror = orient > 4;
        const int dir = mirror ? 8 - orient : orient;
        const UnitFrame& f = ug.sprite[dir];
        if (!f.ok) continue;
        const double c4 = ground.ground_height(u.world_x, u.world_y);
        const double sx = u.world_x - vx, sy = u.world_y - vy - c4;
        const double ay = ug.anchor_y + ug.height / 2.0 - f.off_y;
        drawn.push_back({sy - ay + f.texture.height, sx, &u, &ug, dir, mirror, c4, sx, sy});
    }
    std::stable_sort(drawn.begin(), drawn.end(), [](const UnitDraw& a, const UnitDraw& b) {
        return a.sort_y < b.sort_y || (a.sort_y == b.sort_y && a.sort_x < b.sort_x);
    });
    DrawState unit_state;
    unit_state.z_write = false;
    unit_state.blend = true;
    for (const UnitDraw& d : drawn) {
        for (int layer = 0; layer < 2; ++layer) {
            const UnitFrame& f = layer == 0 ? d.gfx->sprite[d.dir] : d.gfx->mask[d.dir];
            if (!f.ok) continue;
            const int colour = colour_of(view, d.u->player);
            if (layer == 1 && (colour < 0 || colour >= static_cast<int>(g.player_colour.size()))) continue;
            const UnitGraphics& ug = *d.gfx;
            const double rows = f.texture.height, cols = f.texture.width;
            const double ax = ug.anchor_x + ug.width / 2.0 - (d.mirror ? ug.width - cols - f.off_x : f.off_x);
            const double ay = ug.anchor_y + ug.height / 2.0 - f.off_y;
            const double x0 = d.sx - ax, y0 = d.sy - ay;
            const double kb = y0 + rows + 2 * d.c4, kt = kb + rows;
            const double u0 = d.mirror ? 0.99999 : 0, u1 = d.mirror ? 0 : 0.99999;
            unit_state.texture = &f.texture;
            unit_state.tint = layer == 1 ? g.player_colour[colour] : std::array<float, 3>{1, 1, 1};
            draw_quad(rt,
                      {Vertex{x0, y0, kt, u0, 0}, Vertex{x0 + cols, y0, kt, u1, 0},
                       Vertex{x0, y0 + rows, kb, u0, 0.99999}, Vertex{x0 + cols, y0 + rows, kb, u1, 0.99999}},
                      unit_state);
        }
    }
    render_frame_finish(g, pos, rt, rgba);
}

void render_frame_finish(const WorldGraphics& g, ViewPos pos, Target& rt, std::vector<std::uint8_t>* rgba) {
    const int vx = view_x(pos), vy = view_y(pos);
    const std::size_t n = static_cast<std::size_t>(rt.width) * static_cast<std::size_t>(rt.height);
    // Landscape filter on everything but visible water, then 8-bit output (truncated like the prototype's PNG).
    rgba->resize(n * 4);
    parallel_bands(rt.height, [&](int top, int bottom) {
        for (int y = top; y < bottom; ++y) {
            for (int x = 0; x < rt.width; ++x) {
                const std::size_t i = static_cast<std::size_t>(y) * rt.width + x;
                const float* c = &rt.rgb[i * 3];
                std::uint8_t* o = &(*rgba)[i * 4];
                if (rt.water[i]) {
                    for (int ch = 0; ch < 3; ++ch)
                        o[ch] = static_cast<std::uint8_t>(std::clamp(c[ch], 0.0f, 1.0f) * 255.0f);
                } else {
                    const std::array<float, 4>& f =
                        g.filter[static_cast<std::size_t>((y + vy) % g.filter_h) * g.filter_w + (x + vx) % g.filter_w];
                    for (int ch = 0; ch < 3; ++ch) {
                        const float v = std::clamp(c[ch] * f[ch] + c[ch] * f[3], 0.0f, 1.0f);
                        o[ch] = static_cast<std::uint8_t>(v * 255.0f);
                    }
                }
                o[3] = 255;
            }
        }
    });
}

}  // namespace antanta
