



#include "movement.hpp"

namespace antanta::sim {

namespace {

std::size_t Index(const GameState& s, Cell c) { return static_cast<std::size_t>(c.y * s.map.width + c.x); }

bool InMap(const GameState& s, Cell c) { return c.x >= 0 && c.y >= 0 && c.x < s.map.width && c.y < s.map.height; }

}  // namespace


bool Cell_LargeOpen(const GameState& s, std::size_t idx) { return s.map.pass_large[idx] == 0 || s.map.flags[idx] == 2; }




bool TUnit_CanEnterCell(const GameState& s, std::uint8_t player, const Unit& u, std::int32_t cell_index) {
    const auto idx = static_cast<std::uint32_t>(cell_index);
    if (idx >= static_cast<std::uint32_t>(s.map.width * s.map.height)) return false;
    if (Unit_Type(s, player, u).field_468 != 0) return s.map.pass_large[idx] == 0;
    return s.map.pass_land[idx] == 0;
}




void TUnit_MoveToLarge(GameState& s, std::uint8_t player, Unit& u, Cell c, float x, float y) {
    if (!TUnit_CanEnterCell(s, player, u, c.y * s.map.width + c.x)) {
        const Cell r = RelocateTargetLarge(s, c, player, u);
        x = static_cast<float>((r.x * 2 - (~r.y & 1)) * 48 + 48);
        y = static_cast<float>(r.y * 24);
    }
    TUnit_RequestPath(s, player, u, x, y, 1);
}





void PathSearch_LargeStart(GameState& s, const Unit& u) {
    const Cell start{u.map_x, u.map_y};
    for (std::uint8_t d = 0; d <= 8; ++d) {
        const Cell n = Cell_Step(start, d);
        if (!InMap(s, n) || s.map.flags[Index(s, n)] != 2) continue;
        std::uint16_t& label = s.map.wave[Index(s, n)];
        label = 0;
        for (std::uint8_t k = 0; k <= 8 && label == 0; ++k) {
            const Cell m = Cell_Step(n, k);
            if (!InMap(s, m)) continue;
            for (const UnitRef& r : s.map.cell_units[Index(s, m)]) {
                const Unit* o = Unit_Find(s, r);
                if (o == nullptr || o == &u) continue;
                if (Unit_Type(s, r.player, *o).field_468 != 0) {
                    label = 0xffff;
                    break;
                }
            }
        }
    }
}




void ApproachPoint_LargeTarget(GameState& s, std::uint8_t player, Unit& u, float sx, float sy) {
    for (;;) {
        const Cell c = WorldToCell(u.dest_x, u.dest_y);
        if (!CellInside(s, c) || TUnit_CanEnterCell(s, player, u, c.y * s.map.width + c.x)) return;
        u.dest_x = sx + u.dest_x;
        u.dest_y = sy + u.dest_y;
    }
}

}  // namespace antanta::sim
