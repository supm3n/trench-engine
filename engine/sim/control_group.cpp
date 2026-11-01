// The number-key control groups (control_group.hpp, docs/interface.md D.3).
#include "control_group.hpp"

#include <algorithm>

#include "movement.hpp"

namespace antanta::sim {

void ControlGroup_RemoveUnit(Player& pl, Unit& u) {
    const std::uint8_t k = u.field_0ea;
    if (pl.is_lost != 0 || k >= pl.control_groups.size()) return;
    Player::ControlGroup& g = pl.control_groups[k];
    if (g.ids.empty()) return;
    const auto it = std::find(g.ids.begin(), g.ids.end(), u.id);
    if (it == g.ids.end()) return;  // not listed: the digit stays, as there
    g.ids.erase(it);
    if (g.ids.empty()) g.kind = 0;
    u.field_0ea = 0xff;
}

void ControlGroup_RemoveHouse(Player& pl, std::uint16_t house_slot) {
    if (pl.is_lost != 0) return;
    for (Player::ControlGroup& g : pl.control_groups) {
        if (!g.ids.empty() && g.kind == 0 && g.ids[0] == house_slot) g.ids.clear();
    }
}

void ControlGroup_Bind(GameState& s, const Command& c) {
    if (c.args.size() != 2) return;
    const auto p = static_cast<std::uint8_t>(c.args[0].raw & 0xff);
    const std::uint32_t digit = c.args[1].raw;
    if (p >= s.players.size() || s.players[p].is_lost != 0 || digit == 0 || digit >= 10) return;
    Player& pl = s.players[p];
    Player::ControlGroup& g = pl.control_groups[digit];
    if (!g.ids.empty()) {  // the old members lose the digit (a unit entry only)
        if (g.kind == 1) {
            for (std::uint16_t id : g.ids) {
                Unit* u = Unit_FindById(s, p, id);
                if (u != nullptr && u->field_0ea == digit) u->field_0ea = 0xff;
            }
        }
        g.ids.clear();
        g.kind = 0;
    }
    if (!c.houses.empty()) {  // NumSelectedHouses != 0: the group is the selected house alone
        const std::uint16_t slot = c.houses[0].slot;
        if (slot >= pl.houses.size()) return;
        ControlGroup_RemoveHouse(pl, slot);
        g.kind = 0;
        g.ids = {slot};
        return;
    }
    for (const UnitRef& r : c.units) {
        Unit* u = Unit_Find(s, UnitRef{p, r.id, r.serial});
        if (u == nullptr) continue;
        if (u->field_0ea != digit) ControlGroup_RemoveUnit(pl, *u);
        u->field_0ea = static_cast<std::uint8_t>(digit);
        g.kind = 1;
        g.ids.push_back(u->id);
    }
}

}  // namespace antanta::sim
