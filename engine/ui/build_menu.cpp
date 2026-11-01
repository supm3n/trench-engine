#include "ui/build_menu.hpp"

#include <algorithm>

#include "sim/movement.hpp"

namespace antanta::ui {

namespace {


constexpr int kGrid[25][2] = {{5, 50},   {365, 5},  {140, 95}, {185, 95}, {95, 5},  {230, 95}, {5, 5},
                              {365, 50}, {320, 50}, {50, 5},   {95, 50},  {50, 50}, {140, 50}, {185, 50},
                              {275, 50}, {230, 50}, {185, 5},  {5, 95},   {50, 95}, {230, 5},  {275, 5},
                              {320, 5},  {95, 95},  {140, 5},  {275, 95}};


constexpr char kKeys[] = "exq\0bucymhiglodfrkntjwpsv";
constexpr const char* kKeyLabels[25] = {" (E)", " (X)", " (Q)", " (E)", " (B)", " (U)", " (C)", " (Y)", " (M)",
                                        " (H)", " (I)", " (G)", " (L)", " (O)", " (D)", " (F)", " (R)", " (K)",
                                        " (N)", " (T)", " (J)", " (W)", " (P)", " (S)", " (V)"};
// The string table's nation keys of the house names (as the unit names: "AUSTR0HUNGARY").
const char* const kNationKeys[] = {"RUSSIA", "ENGLAND", "FRANCE", "GERMANY", "AUSTR0HUNGARY"};

std::string text_of(const StringTable& t, const std::string& key) {
    const auto it = t.find(key);
    return it != t.end() ? it->second : key;
}

}  // namespace

IRect build_button_rect(std::uint16_t type) {
    if (type == kPontoonType) return IRect{190 + 5, 7 + 140, 40, 40};
    if (type >= 25) return IRect{};
    return IRect{190 + kGrid[type][0], 7 + kGrid[type][1], 40, 40};
}

char build_key(std::uint16_t type) { return type < 25 ? kKeys[type] : '\0'; }

std::string house_type_name(const StringTable& strings, std::uint8_t nation, std::uint16_t type) {
    if (nation >= 5) return "house type " + std::to_string(type);
    return text_of(strings, std::string("IDS_HOUSE_NAME_") + kNationKeys[nation] + "_" + std::to_string(type));
}

std::string build_tooltip(const sim::GameState& s, std::uint8_t player, std::uint16_t type,
                          const StringTable& strings) {
    if (player >= s.players.size() || type >= 25) return {};
    const std::uint8_t nation = s.players[player].nation;
    const sim::HouseType& ht = s.house_types[nation % 6][type];
    std::string t = house_type_name(strings, nation, type) + text_of(strings, "IDS_COST");

    const struct {
        int index;
        const char* key;
    } kCosts[] = {{0, "IDS_FOOD"}, {3, "IDS_GOLD"}, {1, "IDS_WOOD"}, {2, "IDS_IRON"}, {4, "IDS_OIL"}};
    for (const auto& c : kCosts) {
        const float cost = ht.cost[static_cast<std::size_t>(c.index)];
        if (cost == 0.0f) continue;
        t += text_of(strings, c.key) + "(" + std::to_string(sim::Ftol(static_cast<double>(cost))) + "), ";
    }
    // The original overwrites the second last character with a space and ends the text at the last one.
    if (t.size() >= 2) {
        t[t.size() - 2] = ' ';
        t.pop_back();
    }
    if (type != 3)
        t += kKeyLabels[type];  // Plus F7: the Building Academy has no key (its " (E)" is the power station's)
    t += ". ";
    // Up to ten prerequisites; a list of ten without its end mark empties the whole text.
    if (ht.prereq.size() >= 10) return {};
    bool missing = false;
    const sim::Player& pl = s.players[player];
    for (std::uint16_t req : ht.prereq) {
        if (req < 25 && pl.houses_done[req] != 0) continue;
        t += text_of(strings, missing ? "IDS_STROKE" : "IDS_HOUSE_REQUIRE_FOR_BUILD");
        t += house_type_name(strings, nation, req);
        missing = true;
    }
    if (!missing) t.resize(t.size() - 2);  // without the last ". "
    // Entente Engine Plus: the stationary howitzer's site becomes a unit of type 0x17 that pays upkeep (ours, A2)
    if (type == 0x18) t += plus_upkeep_text(s, nation, 0x17, strings);
    return t;
}

bool shows_build_menu(const UnitPanelSummary& u) { return u.types.size() == 1 && u.move_class == 6; }

std::string pontoon_tooltip(const StringTable& strings) { return text_of(strings, "IDS_PONTON"); }

void build_menu_add(Panel& p, const sim::GameState& s, std::uint8_t player, const StringTable& strings) {
    if (player >= s.players.size()) return;
    const sim::Player& pl = s.players[player];
    const auto& types = s.house_types[pl.nation % 6];
    auto add = [&](PanelWidget w, bool can) {
        w.kind = PanelWidget::Kind::Button;
        w.font = PanelFont::Serif18;
        w.disabled = !can;
        if (!can) w.key = 0;
        p.widgets.push_back(std::move(w));
    };
    for (std::uint16_t t = 0; t < 25; ++t) {
        if (!types[t].present || (t == 3 && !s.plus_rules)) continue;  // Plus F7: the Building Academy (B303)
        if (t == 15 && s.climate == 3) continue;
        if (t == 14 && pl.houses_done[14] != 0) continue;
        PanelWidget w;
        w.pic = house_icon(t);  // entry id = house type, cache slot 400 + type
        w.rect = build_button_rect(t);
        w.tooltip = build_tooltip(s, player, t, strings);
        w.key = build_key(t);
        w.action = kUpBuild, w.arg = t;
        add(std::move(w), sim::House_CanBuild(s, player, t));
    }


    PanelWidget w;
    w.pic = pic_id(kHouseIconsLes, 101);
    w.rect = build_button_rect(kPontoonType);
    w.tooltip = pontoon_tooltip(strings);
    w.action = kUpBuild, w.arg = kPontoonType;
    add(std::move(w), sim::House_CanBuild(s, player, kPontoonType));
}

sim::PontoonPlan pontoon_preview(const sim::GameState& s, std::uint8_t player, std::int32_t x0, std::int32_t y0,
                                 std::int32_t cx, std::int32_t cy) {

    return sim::Pontoon_Plan(s, player, x0, y0, cx, cy);
}

std::string pontoon_refusal_text(const Language& l, const sim::PontoonPlan& plan) {
    switch (plan.refusal) {
        case sim::PontoonPlan::Refusal::Gap: return lang_text(l, "build.pontoon_gap");
        case sim::PontoonPlan::Refusal::Wood:
            return lang_text(l, "build.pontoon_wood", {{"wood", std::to_string(300 * plan.would.size())}});
        case sim::PontoonPlan::Refusal::None: break;
    }
    return {};
}

sim::Command pontoon_order(std::uint8_t player, std::int32_t x0, std::int32_t y0, std::int32_t cx, std::int32_t cy) {
    auto raw = [](std::int32_t v) {
        sim::OrderArg a;
        a.raw = static_cast<std::uint32_t>(v);
        return a;
    };
    sim::Command c;
    c.kind = sim::Command::Kind::Act_6cf80;
    c.player = player;
    c.args = {raw(player), raw(x0), raw(y0), raw(cx), raw(cy)};
    return c;
}

bool FitCells::fits(std::int32_t cx, std::int32_t cy) const {
    if (cx < x0 || cx > x1 || cy < y0 || cy > y1) return false;
    return fit[static_cast<std::size_t>(cy - y0) * static_cast<std::size_t>(x1 - x0 + 1) +
               static_cast<std::size_t>(cx - x0)] != 0;
}

bool fit_cells_update(FitCells& c, const sim::GameState& s, std::uint8_t player, std::uint16_t type, std::int32_t x0,
                      std::int32_t y0, std::int32_t x1, std::int32_t y1) {
    constexpr std::int32_t kMargin = 4;  // cells beyond the request, so a scroll does not ask again every pixel
    constexpr std::uint32_t kTicks = 3;  // answers older than this many ticks are asked again
    x0 = std::max(x0, 0), y0 = std::max(y0, 0);
    x1 = std::min<std::int32_t>(x1, s.map.width - 1), y1 = std::min<std::int32_t>(y1, s.map.height - 1);
    const bool inside = x0 >= c.x0 && x1 <= c.x1 && y0 >= c.y0 && y1 <= c.y1;
    if (c.player == player && c.type == type && inside && s.tick - c.tick < kTicks && s.tick >= c.tick) return false;
    c.player = player, c.type = type, c.tick = s.tick;
    c.x0 = std::max(x0 - kMargin, 0), c.y0 = std::max(y0 - kMargin, 0);
    c.x1 = std::min<std::int32_t>(x1 + kMargin, s.map.width - 1),
    c.y1 = std::min<std::int32_t>(y1 + kMargin, s.map.height - 1);
    c.fit.assign(c.x1 < c.x0 || c.y1 < c.y0
                     ? 0
                     : static_cast<std::size_t>(c.x1 - c.x0 + 1) * static_cast<std::size_t>(c.y1 - c.y0 + 1),
                 0);
    if (player >= s.players.size() || type >= 25) return true;
    std::size_t i = 0;
    for (std::int32_t y = c.y0; y <= c.y1; ++y) {
        for (std::int32_t x = c.x0; x <= c.x1; ++x) c.fit[i++] = sim::House_CanPlace(s, player, x, y, type) ? 1 : 0;
    }
    return true;
}

std::uint32_t ghost_colour(const BuildGhost& g) { return g.refused ? 0xff0000u : 0xffffffu; }

BuildGhost build_ghost(const sim::GameState& s, std::uint8_t player, std::uint16_t type, std::int32_t cx,
                       std::int32_t cy) {
    BuildGhost g;
    if (player >= s.players.size() || type >= 25) return g;
    const std::uint8_t nation = s.players[player].nation;
    const sim::HouseType& ht = s.house_types[nation % 6][type];

    g.refused = !sim::House_CanPlace(s, player, cx, cy, type) || !sim::House_CanBuild(s, player, type);


    const sim::Map& m = s.map;
    int height = 0;
    if (cx >= 0 && cy >= 0 && cx < m.width && cy < m.height) {
        height =
            m.heights[static_cast<std::size_t>(cy) * static_cast<std::size_t>(m.width) + static_cast<std::size_t>(cx)];
    }
    g.x = cx * 96 - (cy % 2 == 0 ? 48 : 0) + 48 - ht.anchor_x;
    g.y = (cy - 1) * 24 + 48 - (height + 1) - ht.anchor_y;  // ours: the static layer's height + 1 (B27)
    g.w = ht.sprite_w, g.h = ht.sprite_h;
    if (type != 15) return g;
    switch (nation) {
        case 0:
        case 2: g.field_object = 0x25e0; break;
        case 1: g.field_object = 0x25de; break;
        case 3:
        case 4: g.field_object = 0x25e2; break;
        default: return g;
    }
    // The barn's fields: walk b + 6 rows (direction 4) of a + 6 cells (direction 1) from (cx, cy + 6) to the far end,
    // step back once (direction 3), then draw from there: a + 6 cells in direction 3 per row, the rows in direction 2.
    const sim::HouseFootprint fp = s.house_footprints[nation % 6][type];
    const int rows = fp.b + 6, cols = fp.a + 6;
    sim::Cell row{cx, cy + 6}, end = row;
    for (int r = 0; r < rows; ++r) {
        end = row;
        for (int k = 0; k < cols; ++k) end = sim::Cell_Step(end, 1);
        row = sim::Cell_Step(row, 4);
    }




    auto on_strip = [&](sim::Cell c) {
        sim::Cell d = sim::Cell_Step(sim::Cell{cx, cy}, 6);
        for (int i = 0; i < fp.a; ++i) {
            d = sim::Cell_Step(d, 1);
            if (d.x == c.x && d.y == c.y) return true;
        }
        return false;
    };
    auto on_footprint = [&](sim::Cell c) {
        sim::Cell r = sim::Cell{cx, cy};
        for (int i = 0; i < fp.a; ++i) {
            sim::Cell f = r;
            for (int j = 0; j < fp.b; ++j) {
                if (f.x == c.x && f.y == c.y) return true;
                f = sim::Cell_Step(f, 4);
            }
            r = sim::Cell_Step(r, 1);
        }
        return false;
    };
    row = sim::Cell_Step(end, 3);
    for (int r = 0; r < rows; ++r) {
        sim::Cell c = row;
        for (int k = 0; k < cols; ++k) {
            if (c.x > 3 && c.y > 9 && c.x < m.width - 3 && c.y < m.height - 9 && !sim::Cell_Obstacle(m, c.x, c.y) &&
                !on_strip(c) && !on_footprint(c)) {
                const std::uint16_t o = m.objects[static_cast<std::size_t>(c.y) * static_cast<std::size_t>(m.width) +
                                                  static_cast<std::size_t>(c.x)];
                if ((o < 0x65 || o > 8999) && o != 99 && o != 0x2581 && o != 0x2582 && o != 0x2583) {
                    const sim::Cell w = sim::CellToWorldPoint(m, c);
                    g.fields.push_back({c.x, c.y, w.x + 66, w.y + 26});
                }
            }
            c = sim::Cell_Step(c, 3);
        }
        row = sim::Cell_Step(row, 2);
    }
    return g;
}

}  // namespace antanta::ui
