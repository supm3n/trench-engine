


#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

namespace {

// The record a cell object names: (object - 0x25e4) as u16 into the list; a record past the ones in use is the
// default an unused record has (kind 0xff).
SpecialObject* Record(GameState& s, std::uint16_t o) {
    const std::size_t k = static_cast<std::uint16_t>(o - 0x25e4);
    return k < s.map.specials.size() ? &s.map.specials[k] : nullptr;
}

std::uint16_t CellObject(const Map& m, std::int32_t x, std::int32_t y) {
    if (x < 0 || y < 0 || x >= m.width || y >= m.height) return 0;
    return m.objects[static_cast<std::size_t>(y) * m.width + static_cast<std::size_t>(x)];
}

}  // namespace


bool Special_IsPontoon(const GameState& s, std::uint16_t o) {
    if (o <= 0x25e3 || o >= 11000) return false;
    const std::size_t k = static_cast<std::uint16_t>(o - 0x25e4);
    return k < s.map.specials.size() && s.map.specials[k].kind >= 0x14 && s.map.specials[k].kind <= 0x15;
}





bool Pontoon_CanPlace(const GameState& s, std::uint8_t kind, std::uint16_t x, std::uint16_t y) {
    const Map& m = s.map;
    if (x <= 3 || y <= 9 || x >= m.width - 3 || y >= m.height - 9) return false;
    const auto wx = static_cast<float>((x * 2 - (~y & 1)) * 48 + 48);


    if (Water_CellValueAt(s, wx, static_cast<float>(y * 24)) == 0) return false;
    const std::uint16_t own = CellObject(m, x, y);
    if (own >= 0x25e4 && own < 0x2af8) return false;
    for (std::uint8_t d = 0; d < 8; ++d) {
        const Cell n = Cell_Step(Cell{x, y}, d);
        if (n.x < 0 || n.y < 0 || n.x >= m.width || n.y >= m.height) continue;
        const std::uint16_t o = CellObject(m, n.x, n.y);
        if (o >= 0x65 && o < 0x2328) return false;
    }
    if (Cell_Obstacle(m, x, y - 2) || Cell_Obstacle(m, x, y)) return kind >= 0x14 && kind <= 0x15;
    for (std::uint8_t d = 0; d < 9; ++d) {
        const Cell n = Cell_Step(Cell{x, y}, d);
        if (Cell_Obstacle(m, n.x, n.y)) return true;
    }
    return false;
}




void Special_Add(GameState& s, std::uint8_t kind, std::uint16_t x, std::uint16_t y) {
    if (!Pontoon_CanPlace(s, kind, x, y)) return;
    Map& m = s.map;
    const auto n = static_cast<std::uint16_t>(m.specials.size());
    m.objects[static_cast<std::size_t>(y) * m.width + x] = static_cast<std::uint16_t>(n + 0x25e4);
    if (m.special_capacity <= n) m.special_capacity = static_cast<std::uint16_t>(m.special_capacity + 300);
    SpecialObject r;
    r.x = x, r.y = y, r.kind = kind, r.alive = 1, r.f_a = 50.0f, r.hp = 1000.0f;
    m.specials.push_back(r);
    PathGrid_UpdateRegion(s, x, y, 2);
}



std::uint8_t Line_Step(Cell& c, std::int32_t tx, std::int32_t ty) {
    const std::int32_t x = c.x, y = c.y;
    std::uint8_t d = 0;
    if (x == tx) {
        if (y == ty) return 0;
        if (y <= ty) {
            d = y - ty == -1 ? static_cast<std::uint8_t>((y & 1) | 2) : 3;
        } else {
            d = y - ty != 1 ? 4 : (y & 1) != 0 ? 4 : 1;
        }
    } else if (y == ty) {
        d = x <= tx ? 1 : 4;
    } else if (tx < x) {
        d = ty < y ? 4 : 3;
    } else {
        d = y <= ty ? 2 : 1;
    }
    c = Cell_Step(c, d);
    return d;
}




std::vector<PontoonCell> Pontoon_Line(const GameState& s, std::int32_t x0, std::int32_t y0, std::int32_t cx,
                                      std::int32_t cy) {
    Cell c = PickCell(s.map, static_cast<float>(x0), static_cast<float>(y0));
    std::vector<Cell> cells{c};
    std::vector<std::uint8_t> dirs{0};
    for (std::uint8_t d = Line_Step(c, cx, cy); d != 0; d = Line_Step(c, cx, cy)) {
        cells.push_back(c);
        dirs.back() = d;
        dirs.push_back(d);
    }
    std::vector<PontoonCell> out;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const std::uint8_t kind = dirs[i] == 1 || dirs[i] == 3 ? 0x14 : 0x15;
        out.push_back(
            PontoonCell{static_cast<std::uint16_t>(cells[i].x), static_cast<std::uint16_t>(cells[i].y), kind});
    }
    return out;
}







PontoonPlan Pontoon_Plan(const GameState& s, std::uint8_t player, std::int32_t x0, std::int32_t y0, std::int32_t cx,
                         std::int32_t cy) {
    PontoonPlan plan;
    if (player >= s.players.size()) return plan;
    const bool houses_ok = s.players[player].houses.size() <= 998;
    double wood = s.players[player].wood;
    for (const PontoonCell& c : Pontoon_Line(s, x0, y0, cx, cy)) {
        if (Pontoon_CanPlace(s, c.kind, c.x, c.y) && houses_ok) {
            plan.would.push_back(c);
            continue;
        }
        const std::uint16_t own = CellObject(s.map, c.x, c.y);
        if (Cell_Obstacle(s.map, c.x, c.y) && !(own >= 0x25e4 && own < 0x2af8)) plan.refused.push_back(c);
    }
    std::size_t paid = 0;
    while (paid < plan.would.size() && wood >= 300.0) wood -= 300.0, ++paid;
    if (s.original_rules) {
        plan.would.resize(paid);
        plan.laid = std::move(plan.would);
        plan.would.clear();
        plan.refused.clear();
        return plan;
    }
    if (!plan.refused.empty()) {
        plan.refusal = PontoonPlan::Refusal::Gap;
    } else if (paid < plan.would.size()) {
        plan.refusal = PontoonPlan::Refusal::Wood;
        plan.refused.assign(plan.would.begin() + static_cast<std::ptrdiff_t>(paid), plan.would.end());
    } else {
        plan.laid = std::move(plan.would);
        plan.would.clear();
    }
    return plan;
}




void Pontoon_Lay(GameState& s, std::uint8_t player, std::int32_t x0, std::int32_t y0, std::int32_t cx,
                 std::int32_t cy) {
    if (player >= s.players.size()) return;
    Player& p = s.players[player];
    if (!s.original_rules) {
        for (const PontoonCell& c : Pontoon_Plan(s, player, x0, y0, cx, cy).laid) {
            p.wood -= 300.0;
            p.wood_stat -= 300.0;
            if (p.wood < 0.0) p.wood = 0.0;
            Special_Add(s, c.kind, c.x, c.y);
        }
        return;
    }
    for (const PontoonCell& c : Pontoon_Line(s, x0, y0, cx, cy)) {
        if (!Pontoon_CanPlace(s, c.kind, c.x, c.y) || !House_CanBuild(s, player, 0xfffe)) continue;
        p.wood -= 300.0;
        p.wood_stat -= 300.0;
        if (p.wood < 0.0) p.wood = 0.0;
        Special_Add(s, c.kind, c.x, c.y);
    }
}




void Special_Hit(GameState& s, std::uint16_t o, const Shell* shell) {
    SpecialObject* r = Record(s, o);
    if (r == nullptr) return;
    if (!(r->f_a >= 0.0f)) {
        // fild; fsubr; fst: the hit points are stored as f32, the test reads the unrounded value (NaN keeps it).
        const std::int32_t dmg = shell != nullptr ? shell->damage : 200;
        const double hp = static_cast<double>(r->hp) - static_cast<double>(dmg);
        r->hp = static_cast<float>(hp);
        if (!(hp <= 0.0)) return;
    }
    r->alive = 0;
    Map& m = s.map;
    if (r->x < m.width && r->y < m.height) {
        std::uint16_t& cell = m.objects[static_cast<std::size_t>(r->y) * m.width + r->x];
        if (cell >= 0x25e4 && cell <= 10999) cell = 0;
    }
    PathGrid_UpdateRegion(s, r->x, r->y, 3);
}




void Worker_BuildSpecial(GameState& s, std::uint8_t player, Unit& u) {
    SpecialObject* r = Record(s, u.field_0ca);
    if (r == nullptr) {
        Unported(s, orig::kWorkerBuildSpecial);  // a record past the list's end
        return;
    }
    if (r->alive == 0) {
        Worker_NextJob(s, player, u);
        return;
    }
    r->f_a = static_cast<float>(static_cast<double>(r->f_a) - 1.0);
    if (!(r->f_a < 0.0f)) return;
    PathGrid_UpdateRegion(s, r->x, r->y, 3);

    u.work = 0xff;
    if (Worker_JobSearch(s, player, u)) return;
    TUnit_StopMoving(s, player, u);
    u.is_stopped = 1;
    if (u.work == 3) Unported(s, orig::kWorkerNextJob);  // the original logs an error here
    u.work = 0xff;
}

}  // namespace antanta::sim
