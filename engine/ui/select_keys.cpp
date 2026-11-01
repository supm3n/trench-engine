// The game keys of the original (select_keys.hpp, docs/interface.md D.3).
#include "ui/select_keys.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

#include "ui/house_panel.hpp"

namespace antanta::ui {

namespace {

const sim::UnitType& type_of(const sim::GameState& s, std::uint8_t player, const sim::Unit& u) {
    return s.unit_types[sim::Unit_Nation(s, player, u) % 6][u.type % 40];  // TUnit::Basic
}

// The selected units by id and serial, to look a unit up in constant time (docs/bugs.md B65).
std::unordered_set<std::uint64_t> selected_set(const Selection& sel) {
    std::unordered_set<std::uint64_t> out;
    out.reserve(sel.units.size());
    for (const sim::UnitRef& r : sel.units) out.insert(std::uint64_t{r.id} << 32 | r.serial);
    return out;
}

bool selected(const std::unordered_set<std::uint64_t>& set, const sim::Unit& u) {
    return set.count(std::uint64_t{u.id} << 32 | u.serial) != 0;
}

bool own_player(const sim::GameState& s, const Selection& sel) {
    return sel.player < s.players.size() && s.players[sel.player].is_lost == 0;
}

// The units of the player's list, in list order (the original walks FirstUnit -> Next and puts each in front).
template <typename F>
void each_unit(const sim::GameState& s, std::uint8_t player, F f) {
    const sim::Player& pl = s.players[player];
    for (std::uint32_t k = pl.first_unit; k != sim::kNoSlot; k = pl.unit_slots[k].next) f(pl.unit_slots[k]);
}


template <typename F>
void add_each(const sim::GameState& s, Selection& sel, F pick) {
    std::vector<sim::UnitRef> list;
    each_unit(s, sel.player, [&](const sim::Unit& u) {
        if (pick(u)) list.push_back(sim::UnitRef{sel.player, u.id, u.serial});
    });
    select_units(sel, list);
}



bool on_screen(const sim::Unit& u, ScreenBox b, bool altitude) {
    const float y = altitude ? (u.y - u.z) - u.field_024 : u.y - u.z;
    return u.x <= static_cast<float>(b.x + b.w) && y <= static_cast<float>(b.y + b.h) &&
           static_cast<float>(b.x) <= u.x && static_cast<float>(b.y) <= y;
}


std::vector<std::uint16_t> selected_types(const sim::GameState& s, const Selection& sel) {
    std::vector<std::uint16_t> out;
    for (const sim::UnitRef& r : sel.units) {
        const sim::Unit* u = sim::Unit_Find(s, r);
        if (u != nullptr && std::find(out.begin(), out.end(), u->type) == out.end()) out.push_back(u->type);
    }
    return out;
}

void select_house_alone(Selection& sel, std::uint8_t player, std::uint16_t slot) {
    clear_selection(sel);
    const sim::HouseRef h{player, slot, 0};
    sel.house = h;
    sel.flagged.push_back(h);
}

}  // namespace

KeyAction game_key(const KeyPress& k, bool dev) {
    using A = KeyAct;
    const bool plain = !k.ctrl && !k.shift;
    if (dev) {  // the debug HUD's keys of milestone 3
        switch (k.key) {
            case Key::Space: return {A::DevPause};
            case Key::Period: return {A::DevStep, 0, true};
            case Key::Tab: return {A::DevPlayers};
            case Key::F1: return {A::DevHelp};
            case Key::Escape: return {A::DevEscape};
            case Key::Letter:
                if (k.ch == 'p') return {A::DevPause};
                if (k.ch == 'n') return {A::DevStep, 0, true};
                if (k.ch == 's') return {A::DevStop};
                if (k.ch == 'b') return {A::DevBuild};
                if (k.ch == 'h') return {A::DevHelp};
                break;
            case Key::Digit:
                if (plain && k.ch >= '1' && k.ch <= '4') return {A::DevSpeed, k.ch == '1' ? 1 : k.ch == '2' ? 2 : 4};
                break;
            default: break;
        }
    }
    switch (k.key) {
        case Key::Left: return {A::ScrollLeft, 0, true};
        case Key::Right: return {A::ScrollRight, 0, true};
        case Key::Up: return {A::ScrollUp, 0, true};
        case Key::Down: return {A::ScrollDown, 0, true};
        case Key::Tab: return {A::Tab, k.ctrl ? 1 : 0, true};
        case Key::Space: return {A::Centre, 0, true};
        case Key::Pause: return {A::Pause};
        case Key::Delete: return {A::Delete, 0, true};
        case Key::Grave: return {A::Health};
        case Key::F1: return {A::Mission};
        case Key::F2: return {A::SeeThrough};
        case Key::F5: return {A::QuickSave};
        case Key::F6: return {A::QuickLoad};
        case Key::F12: return {A::DevSwitch};
        case Key::Escape: return {A::Escape};
        case Key::KpPlus: return {A::SpeedUp};
        case Key::KpMinus: return {A::SpeedDown};
        case Key::Plus: return k.ctrl ? KeyAction{} : KeyAction{A::ZoomIn, 0, true};  // Ctrl: the browser's zoom
        case Key::Minus: return k.ctrl ? KeyAction{} : KeyAction{A::ZoomOut, 0, true};
        case Key::Backspace: return k.ctrl ? KeyAction{} : KeyAction{A::ZoomReset};
        case Key::Digit: {
            const int d = k.ch - '0';
            if (k.ctrl && k.shift) return {};  // nothing (the original's debug key)
            if (k.ctrl) return d == 0 ? KeyAction{} : KeyAction{A::GroupBind, d};
            return {A::GroupSelect, d + (k.shift ? 10 : 0)};
        }
        case Key::Letter:
            switch (k.ch) {
                case 'a': return {k.ctrl ? A::SelectAllMap : k.shift ? A::SelectWorkersScreen : A::SelectAllScreen};
                case 'z': return k.ctrl ? KeyAction{A::SelectTypesMap} : KeyAction{A::SelectTypesScreen, k.shift};
                case 'f': return k.ctrl ? KeyAction{A::IdleWorkers} : KeyAction{};
                case 'j': return {A::Market};
                case 'm': return {A::Minimap};
                default: {
                    const int t = house_key_type(k.ch, k.ctrl, k.shift);
                    return t < 0 ? KeyAction{} : KeyAction{A::HouseKey, t};
                }
            }
        default: return {};
    }
}

int house_key_type(char letter, bool ctrl, bool shift) {

    struct Row {
        char letter;
        int type[4];
    };
    static constexpr Row kRows[] = {
        {'e', {21, 7, 1, -1}},   {'q', {4, -1, 23, -1}},  {'r', {2, 8, 5, -1}},   {'t', {9, 18, 6, 17}},
        {'u', {12, 14, 15, -1}}, {'w', {16, 20, 19, 22}}, {'y', {0, 10, 11, 13}},
    };
    for (const Row& r : kRows) {
        if (r.letter == letter) return r.type[(ctrl ? 1 : 0) + (shift ? 2 : 0)];
    }
    return -1;
}

bool key_selectable(const sim::GameState& s, std::uint8_t player, const sim::Unit& u) {
    const std::uint8_t w = u.work;
    if (u.is_deleted != 0 || u.field_154 != 0 || w == 0x0e || w == 9 || w == 3 || w == 0x14) return false;
    if (w == 2 && u.path.empty() && u.field_107 == 0) return false;
    return type_of(s, player, u).is_air == 0 || (w != 5 && w != 6);
}

void select_all_map(const sim::GameState& s, Selection& sel) {
    if (!own_player(s, sel)) return;
    clear_selection(sel);
    add_each(s, sel, [&](const sim::Unit& u) { return key_selectable(s, sel.player, u); });
}

void select_all_screen(const sim::GameState& s, Selection& sel, ScreenBox box) {
    if (!own_player(s, sel)) return;
    clear_selection(sel);
    add_each(s, sel, [&](const sim::Unit& u) { return on_screen(u, box, true) && key_selectable(s, sel.player, u); });
}

void select_workers_screen(const sim::GameState& s, Selection& sel, ScreenBox box) {
    if (!own_player(s, sel)) return;
    clear_selection(sel);
    add_each(s, sel, [&](const sim::Unit& u) {
        return type_of(s, sel.player, u).move_class == 6 && on_screen(u, box, false) &&
               key_selectable(s, sel.player, u);
    });
}

void select_types_map(const sim::GameState& s, Selection& sel) {
    if (!own_player(s, sel) || sel.units.empty()) return;
    const std::vector<std::uint16_t> types = selected_types(s, sel);
    sel.flagged.clear(), sel.house.reset();
    const auto was = selected_set(sel);
    add_each(s, sel, [&](const sim::Unit& u) {
        return !selected(was, u) && key_selectable(s, sel.player, u) &&
               std::find(types.begin(), types.end(), u.type) != types.end();
    });
}

void select_types_screen(const sim::GameState& s, Selection& sel, ScreenBox box, bool keep) {
    if (!own_player(s, sel) || sel.units.empty()) return;
    const std::vector<std::uint16_t> types = selected_types(s, sel);
    if (keep) {
        sel.flagged.clear(), sel.house.reset();
    } else {
        clear_selection(sel);
    }
    add_each(s, sel, [&](const sim::Unit& u) {
        return on_screen(u, box, true) && key_selectable(s, sel.player, u) &&
               std::find(types.begin(), types.end(), u.type) != types.end();
    });
}

bool select_idle_workers(const sim::GameState& s, Selection& sel) {
    if (!own_player(s, sel)) return false;
    const std::vector<sim::UnitRef> list = idle_workers(s, sel.player);
    if (list.empty()) return false;
    clear_selection(sel);
    select_units(sel, list);
    return true;
}

std::optional<sim::Command> bind_group_order(const sim::GameState& s, const Selection& sel, int digit) {
    if (!own_player(s, sel) || digit <= 0 || digit >= 10) return std::nullopt;
    sim::Command c;
    c.kind = sim::Command::Kind::BindGroup;
    c.player = sel.player;
    sim::OrderArg p, d;
    p.raw = sel.player, d.raw = static_cast<std::uint32_t>(digit);
    c.args = {p, d};
    if (const auto h = house_panel_house(s, sel)) {
        c.houses = {*h};
    } else if (!sel.units.empty()) {
        c.units = sel.units;
    } else {
        return std::nullopt;
    }
    return c;
}

bool select_group(const sim::GameState& s, Selection& sel, int digit, bool keep) {
    if (!own_player(s, sel) || digit < 0 || digit >= 10) return false;
    const sim::Player& pl = s.players[sel.player];
    const sim::Player::ControlGroup& g = pl.control_groups[static_cast<std::size_t>(digit)];
    if (g.ids.empty()) return false;
    if (g.kind == 0) {  // a house group: that house alone
        const std::uint16_t slot = g.ids[0];
        if (slot < pl.houses.size() && pl.houses[slot].is_destroyed == 0) select_house_alone(sel, sel.player, slot);
        return false;
    }
    // The members: the units listed whose digit is still this one (a dead unit's id can come back on a new unit).
    std::vector<const sim::Unit*> members;
    for (std::uint16_t id : g.ids) {
        if (id >= pl.id_slots.size() || pl.id_slots[id] == sim::kNoSlot) continue;
        const sim::Unit& u = pl.unit_slots[pl.id_slots[id]];
        if (u.field_0ea == digit && u.is_deleted == 0) members.push_back(&u);
    }
    const auto was = selected_set(sel);
    const auto before = static_cast<std::size_t>(
        std::count_if(members.begin(), members.end(), [&](const sim::Unit* u) { return selected(was, *u); }));
    if (keep) {
        sel.flagged.clear(), sel.house.reset();
    } else {
        clear_selection(sel);
    }
    std::vector<sim::UnitRef> list;
    for (const sim::Unit* u : members) {
        if (key_selectable(s, sel.player, *u)) list.push_back(sim::UnitRef{sel.player, u->id, u->serial});
    }
    select_units(sel, list);
    return !members.empty() && before == members.size();
}

bool tab_key(const sim::GameState& s, Selection& sel, int* index, bool back) {
    if (!own_player(s, sel)) return false;
    if (!sel.units.empty()) {
        ++*index;
        if (*index >= static_cast<int>(sel.units.size())) *index = 0;
        return true;
    }
    const auto ref = house_panel_house(s, sel);
    if (!ref) return false;
    const std::vector<sim::House>& hs = s.players[sel.player].houses;
    const std::uint16_t type = hs[ref->slot].type;
    auto fits = [&](std::size_t i) { return hs[i].is_destroyed == 0 && hs[i].type == type; };
    std::size_t n = 0;
    for (std::size_t i = 0; i < hs.size(); ++i) n += fits(i) ? 1 : 0;
    if (n < 2) return false;
    const std::size_t cur = ref->slot, size = hs.size();
    for (std::size_t step = 1; step < size; ++step) {  // the next (back: previous) of the type, wrapping
        const std::size_t i = back ? (cur + size - step) % size : (cur + step) % size;
        if (fits(i)) {
            select_house_alone(sel, sel.player, static_cast<std::uint16_t>(i));
            return false;
        }
    }
    return false;
}

std::optional<std::array<std::int32_t, 2>> centre_cell(const sim::GameState& s, const Selection& sel, int* index) {
    if (!sel.units.empty()) {
        if (*index < 0 || *index >= static_cast<int>(sel.units.size())) *index = 0;
        const sim::Unit* u = sim::Unit_Find(s, sel.units[static_cast<std::size_t>(*index)]);
        if (u == nullptr) return std::nullopt;
        return std::array<std::int32_t, 2>{u->map_x, u->map_y};
    }
    if (const auto ref = house_panel_house(s, sel)) {
        const sim::House& h = s.players[ref->player].houses[ref->slot];
        return std::array<std::int32_t, 2>{h.map_x, h.map_y};
    }
    return std::nullopt;
}

ViewPos centre_view(ViewCells cells, std::int32_t x, std::int32_t y) {
    ViewPos p;
    p.col = x > cells.cols + 2 ? x - cells.cols / 2 : 2;
    p.row = y > cells.rows + 2 ? y - cells.rows / 2 : 2;
    if (p.col % 2 == 0) --p.col;
    if (p.row % 2 == 0) --p.row;
    return p;
}

bool select_house_type(const sim::GameState& s, Selection& sel, int type) {
    if (!own_player(s, sel)) return false;
    const std::vector<sim::House>& hs = s.players[sel.player].houses;
    for (std::size_t i = 0; i < hs.size(); ++i) {
        if (hs[i].is_destroyed == 0 && hs[i].type == type) {
            select_house_alone(sel, sel.player, static_cast<std::uint16_t>(i));
            return true;
        }
    }
    return false;
}

}  // namespace antanta::ui
