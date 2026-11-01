#include "apps/common/view_input.hpp"

#include <cstdio>
#include <cstdlib>

#include "formats/les.hpp"
#include "formats/sprites.hpp"

namespace antanta {
namespace {


std::string house_les_path(const std::vector<BasicParams>& params, int nation, int type, int depth = 0) {
    if (nation < 0 || nation >= static_cast<int>(params.size()) || type < 0 ||
        type >= static_cast<int>(params[nation].houses.size()) || depth > 4) {
        return {};
    }
    const std::string& p = params[nation].houses[type].data_path;
    if (std::atol(p.c_str()) != 0 && p.size() >= 3) {
        return house_les_path(params, p[0] - '0' - 1, std::atoi(p.substr(1, 2).c_str()), depth + 1);
    }
    std::string out = "Data/Objects/Houses/";
    for (char ch : p) out += ch == '\\' ? '/' : ch;
    return out + ".les";
}

}  // namespace

bool view_box_for(int w, int h, int col, int row, ViewBox* box) {
    int vw = 0, vh = 0;
    if (w == 1024 && h == 768) vw = 0xb, vh = 0x22;
    if (w == 1152 && h == 864) vw = 0xc, vh = 0x26;
    if (w == 1280 && (h == 960 || h == 1024)) vw = 0xe, vh = h == 1024 ? 0x2c : 0x2a;
    if (w == 1600 && h == 1200) vw = 0x11, vh = 0x33;
    if (vw == 0) return false;
    box->x0 = col - 6, box->x1 = vw + 6 + col;
    box->y0 = row - 30, box->y1 = vh + 30 + row;
    return true;
}

ViewBox view_box_whole_map(const sim::GameState& s) {
    return ViewBox{-1, static_cast<int>(s.map.width), -1, static_cast<int>(s.map.height)};
}

std::vector<sim::Command> view_inputs(const ViewBox& box, const sim::GameState& s) {
    std::vector<sim::Command> out;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        if (s.players[p].is_lost != 0) continue;
        for (std::size_t slot = 0; slot < s.players[p].houses.size(); ++slot) {
            const sim::House& h = s.players[p].houses[slot];
            if (!(h.hp >= -149000.0f) || !(box.x0 < h.map_x && h.map_x < box.x1) ||
                !(box.y0 < h.map_y && h.map_y < box.y1)) {
                continue;
            }
            if (h.depth_phase == h.graphic_phase && h.depth_stage == h.graphic_stage) continue;
            sim::Command c;
            c.kind = sim::Command::Kind::HouseDepth;
            c.tick = s.tick + 1;
            c.player = static_cast<std::int32_t>(p);
            sim::OrderArg a, ty, ph, st;
            a.tag = sim::OrderArg::Tag::House;
            a.house = sim::HouseRef{static_cast<std::uint8_t>(p), static_cast<std::uint16_t>(slot), 0};
            ty.raw = h.type, ph.raw = h.graphic_phase, st.raw = h.graphic_stage;
            c.args = {a, ty, ph, st};
            out.push_back(std::move(c));
        }
    }
    return out;
}

void load_house_depth(const FileLoader& load, const std::vector<BasicParams>& params,
                      std::span<const sim::Command> commands, sim::GameState& s, std::vector<std::string>* warnings) {
    for (const sim::Command& c : commands) {
        if (c.kind != sim::Command::Kind::HouseDepth || c.args.size() != 4 || c.args[2].raw > 2) continue;
        const std::uint8_t p = c.args[0].house.player;
        if (p >= s.players.size()) continue;
        sim::HouseDepthMap m;
        const std::uint16_t slot = c.args[0].house.slot;  // the house's own record (a merged house keeps its nation)
        m.nation = slot < s.players[p].houses.size() ? sim::House_Nation(s, p, s.players[p].houses[slot])
                                                     : s.players[p].nation;
        m.type = static_cast<std::uint8_t>(c.args[1].raw);
        m.phase = static_cast<std::uint8_t>(c.args[2].raw), m.stage = static_cast<std::uint8_t>(c.args[3].raw);
        bool have = false;
        for (const sim::HouseDepthMap& o : s.house_depth_maps) {
            have = have || (o.nation == m.nation && o.type == m.type && o.phase == m.phase && o.stage == m.stage);
        }
        if (have || m.type >= 25) continue;
        const std::string path = house_les_path(params, m.nation, m.type);
        std::string err;
        std::optional<Bytes> file;
        if (!path.empty()) file = load(path, &err);
        std::optional<LesTable> table;
        if (file) table = parse_les(*file, &err);
        const LesEntry* e = table ? find_entry_id(*table, house_les_id(m.phase, m.stage, 2)) : nullptr;
        std::optional<Bytes> bytes;
        if (e != nullptr) bytes = read_entry(*file, *e, &err);
        std::optional<DepthMap> d;
        if (bytes) d = decode_hzb(*bytes, &err, 0x8000);
        m.spans = s.house_types[m.nation % 6][m.type].field_1b3 == 0xff;
        if (!d || d->spans != m.spans) {
            if (d) err = "the file's kind is not THouseBasic+0x1b3's";
            char line[96];
            std::snprintf(line, sizeof line, "no depth map for nation %u type %u phase %u stage %u (", m.nation, m.type,
                          m.phase, m.stage);
            warnings->push_back(line + path + ": " + err + ")");
            continue;
        }
        m.width = d->width, m.height = d->height, m.depth = std::move(d->depth);
        s.house_depth_maps.push_back(std::move(m));
    }
}

void load_saved_depth(const FileLoader& load, const std::vector<BasicParams>& params, sim::GameState& s,
                      std::vector<std::string>* warnings) {
    std::vector<sim::Command> want;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        for (std::size_t slot = 0; slot < s.players[p].houses.size(); ++slot) {
            const sim::House& h = s.players[p].houses[slot];
            if (h.depth_phase == 0xff) continue;
            sim::Command c;
            c.kind = sim::Command::Kind::HouseDepth;
            sim::OrderArg a, ty, ph, st;
            a.tag = sim::OrderArg::Tag::House;
            a.house = sim::HouseRef{static_cast<std::uint8_t>(p), static_cast<std::uint16_t>(slot), 0};
            ty.raw = h.type, ph.raw = h.depth_phase, st.raw = h.depth_stage;
            c.args = {a, ty, ph, st};
            want.push_back(std::move(c));
        }
    }
    load_house_depth(load, params, want, s, warnings);
}

}  // namespace antanta
