// Player orders applied between ticks (docs/movement.md 2): a right click on plain ground, the house orders
// (sim/house.cpp) and the selection buttons (sim/group.cpp).
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

#include "control_group.hpp"
#include "endgame.hpp"
#include "market.hpp"
#include "movement.hpp"
#include "original_ids.hpp"
#include "strike.hpp"

namespace antanta::sim {

namespace {


void CancelAttackForMove(GameState& s, std::uint8_t player, Unit& u) {
    u.field_006 = 0;
    u.has_attack_unit = false;
    u.has_house_attack = false;
    u.field_142 = 0;
    u.player_unit_attack = 0xff;
    u.player_house_attack = 0xff;
    u.return_target = UnitRef{};  // our rules (docs/bugs.md B277): never set under the original's
    const std::uint8_t nw = u.num_work;
    if (nw == 6 || nw == 0x12 || nw == 2 || nw == 0x14 || nw == 0x15 || nw == 7 || nw == 0xd) {
        EventUnitEndMove(s, player, u);
    }
}

}  // namespace

void Order_LogMove(GameState& s, std::uint32_t parent, std::uint8_t player, const Unit& u, float x, float y,
                   std::uint8_t flag) {
    Command r;
    r.kind = Command::Kind::OrderMove, r.tick = s.tick + 1, r.oseq = ++s.order_seq, r.parent = parent;
    r.player = player, r.rng_index = s.rng_index;
    OrderArg a;
    a.tag = OrderArg::Tag::Unit, a.unit = Unit_Ref(player, u);
    r.args.push_back(a);
    for (const std::uint32_t v :
         {std::bit_cast<std::uint32_t>(x), std::bit_cast<std::uint32_t>(y), static_cast<std::uint32_t>(flag)}) {
        OrderArg b;
        b.raw = v;
        r.args.push_back(b);
    }
    r.units.push_back(a.unit);
    s.orders.push_back(std::move(r));
}

namespace {

// Our rules (docs/bugs.md B90): one spacing for the whole square, that of the type most units have (on a tie the
// larger), so mixed types stand on one grid; the original takes each unit's own size for the spacing and the
// square's corner, which lays every type on its own grid of its own width. The jitter stays each unit's own.
std::int32_t SpreadGrid(const GameState& s, std::uint8_t player, const std::vector<Unit*>& list) {
    std::array<std::uint32_t, 40> per_type{};
    std::array<std::int32_t, 40> size{};  // a type's size from its first unit's record (merged units: their own)
    for (std::uint8_t k = 0; k < 40; ++k) size[k] = s.unit_types[s.players[player].nation][k].size;
    std::uint8_t best = 0;
    for (const Unit* u : list) {
        if (per_type[u->type % 40]++ == 0) size[u->type % 40] = Unit_Type(s, player, *u).size;
    }
    for (std::uint8_t k = 0; k < 40; ++k) {
        const std::int32_t sk = size[k];
        const std::int32_t sb = size[best];
        if (per_type[k] > per_type[best] || (per_type[k] == per_type[best] && per_type[k] != 0 && sk > sb)) {
            best = k;
        }
    }
    return size[best];
}



void UI_OrderMoveSpread(GameState& s, std::uint8_t player, const std::vector<Unit*>& list, float x, float y) {
    const auto count = static_cast<std::uint16_t>(list.size());
    const std::int32_t cols = Ftol(std::sqrt(static_cast<double>(count)));
    if (count == 1) {
        list[0]->field_07c = 0;
        list[0]->field_0bc = 0;
        TUnit_OrderMove(s, player, *list[0], x, y, 1);
        return;
    }
    const auto colsu = static_cast<std::uint32_t>(cols & 0xffff);
    const float colsf = static_cast<float>(colsu);
    const std::int32_t grid = s.original_rules ? -1 : SpreadGrid(s, player, list);  // B90
    for (std::uint32_t i = 0; i < list.size(); ++i) {
        Unit& u = *list[i];
        const std::int32_t size = Unit_Type(s, player, u).size;
        const std::int32_t spacing = grid >= 0 ? grid : size;
        const double t = 2 * spacing + 1;
        const double s1 = size + 1;
        const double g1 = spacing + 1;
        const float half = static_cast<float>(0.5 * s1);
        const int r = s.rng_table[s.rng_index] % Ftol(s1);
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
        u.field_07c = 0;
        u.field_0bc = 0;
        const double rf = static_cast<float>(r);
        const double m = g1 * colsf;
        const auto row = static_cast<double>((i & 0xffff) / colsu), col = static_cast<double>((i & 0xffff) % colsu);
        const float ty = static_cast<float>((((static_cast<double>(half) + y) - rf) + row * t) - m);
        const float tx = static_cast<float>((((static_cast<double>(half) + x) - rf) + col * t) - m);
        TUnit_OrderMove(s, player, u, tx, ty, 1);
    }
}


void Order_LogGroupOrder(GameState& s, std::uint32_t parent, const Group& g, std::uint8_t kind, std::int32_t x,
                         std::int32_t y, std::uint8_t tplayer, std::uint16_t tid) {
    Command r;
    r.kind = Command::Kind::GroupOrder, r.tick = s.tick + 1, r.oseq = ++s.order_seq, r.parent = parent;
    r.player = g.player, r.rng_index = s.rng_index;
    OrderArg a;
    a.tag = OrderArg::Tag::Group;
    r.args.push_back(a);
    OrderArg k;
    k.raw = kind;
    r.args.push_back(k);
    OrderArg xy;
    xy.tag = OrderArg::Tag::Array, xy.values = {x, y};
    r.args.push_back(xy);
    OrderArg tp, ti;
    tp.raw = tplayer, ti.raw = tid;
    r.args.push_back(tp);
    r.args.push_back(ti);
    for (const UnitRef& m : g.members) {
        if (m.player != 0xff) r.units.push_back(m);
    }
    s.orders.push_back(std::move(r));
}




void Order_LogChild(GameState& s, std::uint32_t parent, Command::Kind kind, std::uint8_t player, const Unit& u,
                    std::vector<OrderArg> tail) {
    Command r;
    r.kind = kind, r.tick = s.tick + 1, r.oseq = ++s.order_seq, r.parent = parent;
    r.player = player, r.rng_index = s.rng_index;
    OrderArg a;
    a.tag = OrderArg::Tag::Unit, a.unit = Unit_Ref(player, u);
    r.args.push_back(a);
    for (OrderArg& t : tail) r.args.push_back(std::move(t));
    r.units.push_back(a.unit);
    s.orders.push_back(std::move(r));
}

OrderArg RawArg(std::uint32_t v) {
    OrderArg a;
    a.raw = v;
    return a;
}

OrderArg UnitArg(const UnitRef& r) {
    OrderArg a;
    a.tag = OrderArg::Tag::Unit, a.unit = r;
    return a;
}

}  // namespace


void Order_LogBoard(GameState& s, std::uint32_t parent, std::uint8_t player, const Unit& u, const UnitRef& t) {
    Order_LogChild(s, parent, Command::Kind::Board, player, u, {UnitArg(t), RawArg(1)});
}

namespace {


bool Flying(const GameState& s, std::uint8_t player, const Unit& u) {
    return Unit_Type(s, player, u).is_air == 0 ||
           static_cast<double>(400.0f) <= static_cast<double>(u.z) + static_cast<double>(u.field_024);
}






void UI_SendToHouse(GameState& s, std::uint8_t player, const std::vector<Unit*>& list, std::uint16_t id,
                    std::uint32_t parent) {
    std::vector<House>& houses = s.players[player].houses;
    House* h = id < 1000 && id < houses.size() ? &houses[id] : nullptr;
    if (id < 1000 && h == nullptr) {
        Unported(s, orig::kUiSendToHouse);  // a slot past NumHouse
        return;
    }
    bool full = false;
    std::uint8_t free = 0;
    if (h != nullptr && h->is_destroyed == 0 && h->field_020 == 0 && h->hp >= static_cast<float>(h->max_hp)) {
        free = static_cast<std::uint8_t>(h->capacity - h->units_inside);
        full = true;
    }
    std::uint16_t sent = 0;
    for (Unit* u : list) {
        u->field_07c = 0;
        if (!Flying(s, player, *u)) continue;
        if (u->in_group) {
            if (Group* g = Unit_Group(s, player, *u)) {
                Group_RemoveMember(s, *g, *u);
            } else {
                Unported(s, orig::kGroupRemoveMember);  // a group loaded from a save
            }
        }
        if (full && free <= sent) return;
        Order_LogChild(s, parent, Command::Kind::EnterHouse, player, *u,
                       {UnitArg(Unit_Ref(player, *u)), RawArg(id), RawArg(0)});
        Worker_SendToHouse(s, player, *u, id, 0);
        if (full) s.deselected.push_back(Unit_Ref(player, *u));
        ++sent;
    }
}




void UI_AttackHouse(GameState& s, std::uint8_t player, const std::vector<Unit*>& list, std::uint8_t hp,
                    std::uint16_t slot, std::uint32_t parent) {
    if (hp >= s.players.size() || slot >= s.players[hp].houses.size()) {
        Unported(s, orig::kUiAttackHouse);  // a slot past NumHouse
        return;
    }
    if (s.players[hp].houses[slot].is_destroyed != 0) return;
    for (Unit* u : list) {
        u->field_07c = 0;
        if ((u->work == 9 || u->work == 2) && u->path.empty()) continue;
        if (!Flying(s, player, *u)) continue;
        if (u->field_116 == 4) u->field_116 = 0xff;
        OrderArg h;
        h.tag = OrderArg::Tag::House, h.house = HouseRef{hp, slot, 0};
        Order_LogChild(s, parent, Command::Kind::AttackHouse, player, *u,
                       {UnitArg(Unit_Ref(player, *u)), h, RawArg(0), RawArg(0)});
        TUnit_AttackHouse(s, player, *u, *u, hp, slot, 0, 0);
    }
}




void UI_OrderXy(GameState& s, std::uint8_t player, const std::vector<Unit*>& list, float x, float y,
                std::uint16_t object, std::uint32_t parent) {
    for (Unit* u : list) {
        u->field_07c = 0;
        Order_LogChild(
            s, parent, Command::Kind::OrderXy, player, *u,
            {RawArg(std::bit_cast<std::uint32_t>(x)), RawArg(std::bit_cast<std::uint32_t>(y)), RawArg(object)});
        TUnit_OrderXy(s, player, *u, x, y, object);
    }
}




void UI_AttackUnit(GameState& s, std::uint8_t player, const std::vector<Unit*>& list, const UnitRef& target,
                   std::uint32_t parent) {
    Unit* t = Unit_Find(s, target);
    if (t == nullptr) return;  // checked by the caller
    for (Unit* u : list) {
        u->field_07c = 0;
        if ((u->work == 9 || u->work == 2) && u->path.empty()) continue;
        s.flag_8ef4d6 = 1;
        if (!Flying(s, player, *u)) continue;
        Order_LogChild(s, parent, Command::Kind::Attack, player, *u, {UnitArg(target), RawArg(0)});
        TUnit_OnAttackedBy(s, player, *u, target.player, *t, 0);
        t->field_141 = 0xc;
    }
}




struct Click {
    int kind = -1;
    std::uint32_t other = 0;  // the address of a child kind the port does not execute
    UnitRef target;
};

Click Click_FromChildren(GameState& s, const Command& c, std::span<const Command> all) {
    Click k;
    for (const Command& ch : all) {
        if (c.oseq == 0 || ch.parent != c.oseq) continue;
        int kind = -1;
        switch (ch.kind) {
            case Command::Kind::OrderMove: kind = 0; break;
            case Command::Kind::EnterHouse: kind = 1; break;
            case Command::Kind::AttackHouse: kind = 2; break;
            case Command::Kind::OrderXy: kind = 5; break;
            case Command::Kind::Board:  // ahead of a group's move child: units in a group get a move (kind 7 too)
                kind = 7;
                k.kind = 7;
                if (ch.args.size() >= 2 && ch.args[1].tag == OrderArg::Tag::Unit) {
                    for (std::size_t p = 0; p < s.players.size(); ++p) {
                        const UnitRef r{static_cast<std::uint8_t>(p), ch.args[1].unit.id, ch.args[1].unit.serial};
                        if (Unit_Find(s, r) != nullptr) k.target = r;
                    }
                }
                break;
            case Command::Kind::Attack:
                kind = 4;
                if (ch.args.size() >= 2 && ch.args[1].tag == OrderArg::Tag::Unit) {
                    for (std::size_t p = 0; p < s.players.size();
                         ++p) {  // the O line has no player: serials are unique
                        const UnitRef r{static_cast<std::uint8_t>(p), ch.args[1].unit.id, ch.args[1].unit.serial};
                        if (Unit_Find(s, r) != nullptr) k.target = r;
                    }
                }
                break;
            case Command::Kind::GroupOrder: {
                const std::uint32_t g = ch.args.size() >= 5 ? ch.args[1].raw & 0xff : 0;
                kind = g == 1 ? 0 : g == 6 ? 1 : g == 2 ? 2 : g == 3 ? 4 : g == 4 ? 5 : g == 5 ? 6 : -1;
                if (kind == 4 && k.target.player == 0xff) {
                    const auto tp = static_cast<std::uint8_t>(ch.args[3].raw & 0xff);
                    const auto tid = static_cast<std::uint16_t>(ch.args[4].raw & 0xffff);
                    if (tp < s.players.size()) {
                        if (const Unit* t = Unit_FindById(s, tp, tid)) {
                            k.target = Unit_Ref(tp, *t);
                        }
                    }
                }
                if (kind < 0 && k.other == 0) k.other = Command_Address(ch.kind);
                break;
            }
            default:
                if (k.other == 0) k.other = Command_Address(ch.kind);
                break;
        }
        if (kind >= 0 && k.kind < 0) k.kind = kind;
    }
    return k;
}



bool Click_TransportOk(GameState& s, std::uint8_t player, std::uint16_t obj, const Click& click) {
    if (obj / 1000 - 1u == player || (obj >= 0x2580 && obj <= 0x25e3)) return false;
    const auto other = static_cast<std::uint8_t>(obj / 1000 - 1);
    if (obj >= 1000 && obj <= 8999 && other < s.players.size() && IsEnemy(s, player, other)) return false;
    const Unit* t = Unit_Find(s, click.target);
    return t != nullptr && click.target.player == player && (t->type == 0x11 || t->type == 0x18);
}







// c.args = (player, world x int, world y int), c.units = the player's selection in list order.
void UI_OrderSelected(GameState& s, const Command& c, const Click& click) {
    const auto player = static_cast<std::uint8_t>(c.args[0].raw & 0xff);
    const auto px = static_cast<std::int32_t>(c.args[1].raw), py = static_cast<std::int32_t>(c.args[2].raw);
    Command rec = c;  // the ui_order O line, written on entry like the recorder's hook
    s.order_seq = c.oseq != 0 ? c.oseq : s.order_seq + 1;
    rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
    const float fx = static_cast<float>(px) + (px < 0 ? 4294967296.0f : 0.0f);
    const float fy0 = static_cast<float>(py) + (py < 0 ? 4294967296.0f : 0.0f);
    const Cell cell = PickCell(s.map, fx, fy0);
    const bool on_map = cell.x >= 0 && cell.y >= 0 && cell.x < s.map.width && cell.y < s.map.height;
    if (on_map) rec.cell_x = cell.x, rec.cell_y = cell.y;
    s.orders.push_back(std::move(rec));
    const std::uint32_t parent = s.order_seq;
    s.log_moves = true, s.log_parent = parent;
    if (c.units.empty() || player >= s.players.size()) return;
    s.flag_8ef4d4 = 1;
    if (!on_map) return;
    const std::size_t ci = static_cast<std::size_t>(cell.y) * s.map.width + static_cast<std::size_t>(cell.x);
    const std::int32_t y = py + s.map.heights[ci];
    const std::uint16_t obj = s.map.objects[ci];
    // The house of kinds 1 and 2 is the object of the clicked cell: own (obj / 1000 - 1 == player) or another's.
    const std::uint16_t own_slot = static_cast<std::uint16_t>(obj % 1000);
    const auto other = static_cast<std::uint8_t>(obj / 1000 - 1);

    const bool pontoon = click.kind == 1 && Special_IsPontoon(s, obj);
    if ((click.kind == 1 && obj / 1000 - 1u != player && !pontoon) ||
        (click.kind == 2 && (obj < 1000 || obj > 8999 || other >= s.players.size() || !IsEnemy(s, player, other))) ||
        (click.kind == 4 && Unit_Find(s, click.target) == nullptr) ||
        (click.kind == 5 && (obj < 0x2580 || obj > 0x25e3)) ||
        (click.kind == 7 && !Click_TransportOk(s, player, obj, click))) {
        // the click classification disagrees with the cell (or the target is unknown)
        Unported(s, orig::kUiOrderToPoint);
        return;
    }
    Event_CommandVoice(s, player, c.units.front());
    if (click.kind == 0 && player == s.my_player) {


        const bool inner = 1 < cell.x && cell.x < s.map.width - 2 && 2 < cell.y && cell.y < s.map.height - 3;
        if (SimEvent::EffectArgs* e = Event_Effect(s, 3, false, px, y, inner ? 0 : 50, 0x25)) {
            e->rise_step = 0;
            if (inner) e->view_rand = 2, e->view_x = fx, e->view_y = fy0;
        }
    }
    const bool fire = click.kind == 2 || click.kind == 4;
    std::vector<Unit*> list;
    std::vector<std::uint32_t> seen;  // groups ordered already (each one once, when its first member comes)
    for (const UnitRef& sel : c.units) {
        Unit* u = Unit_Find(s, UnitRef{player, sel.id, sel.serial});
        if (u == nullptr) continue;
        u->order_queue.clear();  // ours (B317): a right click without Shift replaces the queue (empty in the original)
        Unit_ClearFutureTransporter(*u);
        u->field_0e4 = 0;
        if (!fire) {
            if (u->field_006 != 0) {
                CancelAttackForMove(s, player, *u);
                u->field_142 = Unit_Type(s, player, *u).is_air != 0 ? 65000 : 300;
                u->field_006 = 0;
                if (u->in_group) {
                    Group* g = Unit_Group(s, player, *u);
                    if (g == nullptr) {
                        Unported(s, orig::kGroupRemoveMember);  // a group loaded from a save
                    } else if (g->ai_owned != 0) {
                        Group_RemoveMember(s, *g, *u);
                    }
                }
            }
        } else {
            u->field_142 = 0;
            u->field_006 = 1;
        }
        if (!u->in_group) {
            list.push_back(u);  // reversed below: a prepend per unit is quadratic (docs/bugs.md B65)
            continue;
        }
        Group* g = Unit_Group(s, player, *u);
        if (g == nullptr) {
            Unported(s, orig::kGroupOrder);  // a group loaded from a save
            continue;
        }
        if (std::find(seen.begin(), seen.end(), g->serial) != seen.end()) continue;
        seen.push_back(g->serial);
        std::uint8_t gk = 1, tp = 0xff;
        std::uint16_t tid = 0xffff;
        if (click.kind == 1) {

            gk = 6, tp = player, tid = pontoon ? obj : own_slot;
        } else if (click.kind == 2) {
            gk = 2, tp = other, tid = own_slot;
        } else if (click.kind == 4) {
            gk = 3, tp = click.target.player, tid = click.target.id;
        } else if (click.kind == 5) {
            gk = 4, tid = obj;
        }
        Order_LogGroupOrder(s, parent, *g, gk, px, y, tp, tid);
        Group_Order(s, *g, gk, px, y, tp, tid, !s.original_rules);  // all at once under our rules (B91)
    }
    if (list.empty()) return;
    std::reverse(list.begin(), list.end());
    switch (click.kind) {
        case 0: {
            const float fy = static_cast<float>(y) + (y < 0 ? 4294967296.0f : 0.0f);
            UI_OrderMoveSpread(s, player, list, fx, fy);
            break;
        }
        case 1: UI_SendToHouse(s, player, list, pontoon ? obj : own_slot, parent); break;
        case 2: UI_AttackHouse(s, player, list, other, own_slot, parent); break;
        case 4: UI_AttackUnit(s, player, list, click.target, parent); break;
        case 5: {
            const float fy = static_cast<float>(y) + (y < 0 ? 4294967296.0f : 0.0f);
            UI_OrderXy(s, player, list, fx, fy, obj, parent);
            break;
        }
        case 7: UI_Board(s, player, list, click.target.player, *Unit_Find(s, click.target), parent); break;
        default: break;
    }
}





bool Unit_Busy(const Unit& u) {
    const bool field = u.work == 10 || u.work == 11;
    return u.is_stopped == 0 || !u.path.empty() || u.has_attack_unit || u.has_house_attack || u.field_0bc != 0 ||
           u.field_107 != 0 || u.field_160 != 0 || (u.work != 0xff && !field);
}





bool OrderQueue_Take(GameState& s, std::uint8_t p, Unit& u, const Unit::QueuedOrder& q) {
    House* h = q.kind == 1 || q.kind == 2 ? House_Find(s, q.house) : nullptr;
    Unit* t = q.kind == 4 || q.kind == 7 ? Unit_Find(s, q.unit) : nullptr;
    if ((q.kind == 1 || q.kind == 2) && (h == nullptr || h->is_destroyed != 0)) return false;
    if ((q.kind == 4 || q.kind == 7) && (t == nullptr || t->is_deleted != 0)) return false;
    if (q.kind == 1 && h->field_020 == 0 && h->hp >= static_cast<float>(h->max_hp) &&
        static_cast<std::uint8_t>(h->capacity - h->units_inside) == 0) {
        return false;
    }
    if (q.kind == 7 && (t == &u || !Transport_CanUnload(s, *t) || !Unit_MayBoard(s, p, u, *t))) return false;
    Unit_ClearFutureTransporter(u);
    u.field_0e4 = 0;
    u.field_07c = 0;
    if (q.kind == 2 || q.kind == 4) {
        u.field_142 = 0;
        u.field_006 = 1;
    } else if (u.field_006 != 0) {
        CancelAttackForMove(s, p, u);
        u.field_142 = Unit_Type(s, p, u).is_air != 0 ? 65000 : 300;
        u.field_006 = 0;
    }
    switch (q.kind) {
        case 1: Worker_SendToHouse(s, p, u, q.house.slot, 0); break;
        case 2:
            if (u.field_116 == 4) u.field_116 = 0xff;
            TUnit_AttackHouse(s, p, u, u, q.house.player, q.house.slot, 0, 0);
            break;
        case 4:
            s.flag_8ef4d6 = 1;
            TUnit_OnAttackedBy(s, p, u, q.unit.player, *t, 0);
            t->field_141 = 0xc;
            break;
        case 7: Transport_Board(s, p, u, q.unit.player, *t, true); break;
        default:
            u.field_0bc = 0;
            TUnit_OrderMove(s, p, u, q.x, q.y, 1);
            break;
    }
    return true;
}

// Our rules (docs/bugs.md B317): queue_order, Shift + right click. args: player, world x, world y (as ui_order's),
// the click kind, the target (a unit for kinds 4 and 7, a house for 1 and 2). The point is raised by the ground of

// point. Per selected unit outside a formation group (groups keep the original's orders): an aircraft in flight takes
// the order at once (it circles and never ends a leg), so does a unit that has nothing to do (Unit_Busy) and no
// queue; any other appends it, up to kOrderQueueMax. A move gives each unit its place in the selection's square
// (UI_OrderMoveSpread's, one spacing, B90) without the jitter: the queue reads no random number. Nothing under
// original_rules (the app sends a plain right click there).
void Order_ApplyQueue(GameState& s, const Command& c) {
    if (s.original_rules || c.args.size() != 5) return;
    const auto player = static_cast<std::uint8_t>(c.args[0].raw & 0xff);
    if (player >= s.players.size()) return;
    const auto px = static_cast<std::int32_t>(c.args[1].raw), py = static_cast<std::int32_t>(c.args[2].raw);
    const float fx = static_cast<float>(px) + (px < 0 ? 4294967296.0f : 0.0f);
    const float fy0 = static_cast<float>(py) + (py < 0 ? 4294967296.0f : 0.0f);
    const Cell cell = PickCell(s.map, fx, fy0);
    if (cell.x < 0 || cell.y < 0 || cell.x >= s.map.width || cell.y >= s.map.height) return;
    const std::size_t ci = static_cast<std::size_t>(cell.y) * s.map.width + static_cast<std::size_t>(cell.x);
    const std::int32_t y = py + s.map.heights[ci];
    Unit::QueuedOrder q;
    q.kind = static_cast<std::uint8_t>(c.args[3].raw & 0xff);
    q.x = fx, q.y = static_cast<float>(y) + (y < 0 ? 4294967296.0f : 0.0f), q.px = px, q.py = py;
    const OrderArg& a = c.args[4];
    if ((q.kind == 1 || q.kind == 2) && a.tag == OrderArg::Tag::House) {
        const House* h = House_Find(s, HouseRef{a.house.player, a.house.slot, 0});
        const bool own = a.house.player == player;
        if (h != nullptr && h->is_destroyed == 0 && (q.kind == 1) == own &&
            (own || IsEnemy(s, player, a.house.player))) {
            q.house = HouseRef{a.house.player, a.house.slot, h->generation};
        } else {
            q.kind = 0;
        }
    } else if ((q.kind == 4 || q.kind == 7) && a.tag == OrderArg::Tag::Unit) {
        for (std::size_t k = 0; k < s.players.size() && q.unit.player == 0xff; ++k) {  // the O line has no player
            const UnitRef r{static_cast<std::uint8_t>(k), a.unit.id, a.unit.serial};
            const Unit* t = Unit_Find(s, r);
            if (t != nullptr && t->is_deleted == 0) q.unit = r;
        }
        const Unit* t = Unit_Find(s, q.unit);
        if (t == nullptr || (q.kind == 7) != (q.unit.player == player) ||
            (q.kind == 7 && t->type != 0x11 && t->type != 0x18)) {
            q.kind = 0, q.unit = UnitRef{};
        }
    } else {
        q.kind = 0;
    }
    std::vector<Unit*> list;
    for (const UnitRef& sel : c.units) {
        Unit* u = Unit_Find(s, UnitRef{player, sel.id, sel.serial});
        if (u == nullptr || u->is_deleted != 0 || u->in_group) continue;
        list.push_back(u);
    }
    if (list.empty()) return;
    std::reverse(list.begin(), list.end());
    Event_CommandVoice(s, player, c.units.front());
    const std::uint32_t cols = static_cast<std::uint32_t>(Ftol(std::sqrt(static_cast<double>(list.size())))) & 0xffff;
    const std::int32_t grid = SpreadGrid(s, player, list);
    for (std::uint32_t i = 0; i < list.size(); ++i) {
        Unit& u = *list[i];
        Unit::QueuedOrder e = q;
        if (e.kind == 0 && list.size() > 1) {  // UI_OrderMoveSpread's square without its jitter
            const double t = 2 * grid + 1, g1 = grid + 1;
            const float half = static_cast<float>(0.5 * (Unit_Type(s, player, u).size + 1));
            const double m = g1 * static_cast<float>(cols);
            const auto row = static_cast<double>(i / cols), col = static_cast<double>(i % cols);
            e.x = static_cast<float>(((static_cast<double>(half) + q.x) + col * t) - m);
            e.y = static_cast<float>(((static_cast<double>(half) + q.y) + row * t) - m);
        }
        const bool air = Unit_Type(s, player, u).is_air != 0;
        if (air && !Flying(s, player, u)) continue;
        if (air || (u.order_queue.empty() && !Unit_Busy(u))) {
            u.order_queue.clear();
            OrderQueue_Take(s, player, u, e);
        } else if (u.order_queue.size() < kOrderQueueMax) {
            u.order_queue.push_back(e);
        }
    }
}



void Order_ApplyHouse(GameState& s, const Command& c) {
    Command rec = c;
    s.order_seq = c.oseq != 0 ? c.oseq : s.order_seq + 1;
    rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
    s.orders.push_back(std::move(rec));
    if (c.kind == Command::Kind::Act_b9730) {
        if (c.args.size() != 4) return;
        Order_Train(s, static_cast<std::uint8_t>(c.args[0].raw & 0xff), static_cast<std::uint16_t>(c.args[1].raw),
                    static_cast<std::uint16_t>(c.args[2].raw), static_cast<std::uint16_t>(c.args[3].raw));
        return;
    }
    if (c.args.size() != 3 || c.args[0].tag != OrderArg::Tag::House) return;
    House* h = House_Find(s, c.args[0].house);
    if (h == nullptr) return;
    House_SetRally(*h, std::bit_cast<float>(c.args[1].raw), std::bit_cast<float>(c.args[2].raw));
}





void Order_ApplyResearch(GameState& s, const Command& c) {
    const bool start = c.kind == Command::Kind::Act_a5290;
    if (c.args.empty() || c.args[0].tag != OrderArg::Tag::House) return;
    const HouseRef ref = c.args[0].house;
    House* h = House_Find(s, ref);
    if (h == nullptr || ref.player >= s.players.size()) {
        Unported(s, Command_Address(c.kind));  // a house the port cannot find
        return;
    }
    if (start && h->field_06a != 0) return;
    Command rec = c;
    s.order_seq = c.oseq != 0 ? c.oseq : s.order_seq + 1;
    rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
    s.orders.push_back(std::move(rec));
    if (start) {
        if (c.args.size() != 3) return;
        Research_Button(s, ref.player, *h, static_cast<std::uint16_t>(c.args[1].raw),
                        static_cast<std::uint16_t>(c.args[2].raw));
        return;
    }
    if (c.args.size() != 2 || c.args[1].tag != OrderArg::Tag::Array || c.args[1].values.size() != 5) return;
    std::array<std::int32_t, 5> cost{};
    for (std::size_t i = 0; i < 5; ++i) cost[i] = c.args[1].values[i];
    Research_Cancel(s, ref.player, *h, cost);
}

// A unit in house h given by an O line's unit token (no player: the house's units are searched by id and serial).
Unit* Order_UnitInHouse(GameState& s, const House& h, const OrderArg& a, std::uint8_t* player) {
    if (a.tag != OrderArg::Tag::Unit) return nullptr;
    for (std::size_t i = 0; i < h.units_inside && i < h.inside.size(); ++i) {
        const UnitRef& r = h.inside[i];
        if (r.id != a.unit.id || r.serial != a.unit.serial) continue;
        *player = r.player;
        return Unit_Find(s, r);
    }
    return nullptr;
}

// The house panel's orders (slice I6, docs/interface.md B.8; sim/house.cpp): act_ba0a0 and act_b9fc0 (house), the fire
// switch; act_b9c70 (player, house, type, count), a train button's right click; act_baa60 (player, mode) with the
// selected house listed, the attack type for new units; act_bbad0 (house), the icon's right click; sell_house (house,
// int[5] refund), Cancel; act_c0c20 (house), the barn's sow button; release_unit (house, unit), the hangar's aircraft;


// which calls sell_house: the sim writes that O line too, so the one a recording has right after it is skipped. False
// when the kind is not one of these.
bool Order_ApplyHousePanel(GameState& s, const Command& c, std::span<const Command> all) {
    using K = Command::Kind;
    const K k = c.kind;
    if (k != K::Act_ba0a0 && k != K::Act_b9fc0 && k != K::Act_b9c70 && k != K::Act_baa60 && k != K::Act_bbad0 &&
        k != K::SellHouse && k != K::Act_c0c20 && k != K::ReleaseUnit && k != K::ReleaseHouseUnit) {
        return false;
    }
    if (k == K::SellHouse && !c.args.empty() && c.args[0].tag == OrderArg::Tag::House) {
        const Command* before = nullptr;
        for (const Command& o : all) {
            if (&o == &c) break;
            if (o.parent == 0) before = &o;
        }
        if (before != nullptr && before->kind == K::Act_bbad0 && !before->args.empty() &&
            before->args[0].tag == OrderArg::Tag::House && before->args[0].house.player == c.args[0].house.player &&
            before->args[0].house.slot == c.args[0].house.slot) {
            return true;  // written and applied by act_bbad0
        }
    }
    auto log = [&s](const Command& o) {
        Command rec = o;
        s.order_seq = o.oseq != 0 ? o.oseq : s.order_seq + 1;
        rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
        s.orders.push_back(std::move(rec));
    };
    if (k == K::Act_b9c70 || k == K::Act_baa60) {
        log(c);
        const std::uint8_t p = c.args.empty() ? 0xff : static_cast<std::uint8_t>(c.args[0].raw & 0xff);
        if (p >= s.players.size()) return true;
        if (k == K::Act_b9c70) {
            if (c.args.size() != 4) return true;
            Order_CancelTrain(s, p, static_cast<std::uint16_t>(c.args[1].raw),
                              static_cast<std::uint16_t>(c.args[2].raw), static_cast<std::uint8_t>(c.args[3].raw));
            return true;
        }


        if (c.args.size() != 2 || c.houses.empty() || c.houses[0].player != p) return true;
        if (House* h = House_Find(s, HouseRef{p, c.houses[0].slot, 0})) {
            h->field_0b8 = static_cast<std::uint8_t>(c.args[1].raw);
        }
        return true;
    }
    if (c.args.empty() || c.args[0].tag != OrderArg::Tag::House) {
        if (k != K::ReleaseHouseUnit) log(c);
        return true;
    }
    const HouseRef ref = c.args[0].house;
    House* h = House_Find(s, ref);
    if (h == nullptr) {
        Unported(s, Command_Address(k));  // a house the port cannot find
        return true;
    }
    const std::uint8_t p = ref.player;
    if (k != K::ReleaseHouseUnit) log(c);
    switch (k) {
        case K::Act_ba0a0: House_FireOn(s, *h); break;
        case K::Act_b9fc0: House_FireOff(s, *h); break;
        case K::Act_c0c20: House_Sow(s, p, *h); break;
        case K::SellHouse: {
            if (c.args.size() != 2 || c.args[1].tag != OrderArg::Tag::Array || c.args[1].values.size() != 5) break;
            std::array<std::int32_t, 5> refund{};
            for (std::size_t i = 0; i < 5; ++i) refund[i] = c.args[1].values[i];
            House_Sell(s, p, *h, refund);
            break;
        }
        case K::Act_bbad0:
            if (h->is_destroyed != 0) break;
            if (h->field_020 == 0) {
                s.flag_8ab824 = 1;
                h->hp = -1.0f;
                House_OnDestroyed(s, p, *h, p);
            } else {
                const std::array<std::int32_t, 5> refund = House_SellRefund(s, p, *h);
                Command sell;
                sell.kind = K::SellHouse, sell.player = p;
                sell.args.push_back(c.args[0]);
                OrderArg a;
                a.tag = OrderArg::Tag::Array;
                a.values.assign(refund.begin(), refund.end());
                sell.args.push_back(std::move(a));
                log(sell);
                House_Sell(s, p, *h, refund);
            }
            break;
        case K::ReleaseUnit:
        case K::ReleaseHouseUnit: {
            if (c.args.size() != 2) break;
            std::uint8_t up = 0xff;
            Unit* u = Order_UnitInHouse(s, *h, c.args[1], &up);
            if (u == nullptr) break;  // not (or no longer) in the house: the button does nothing
            if (k == K::ReleaseUnit) {
                House_ReleaseAircraft(s, p, *h, up, *u);
            } else {
                House_ReleaseUnit(s, p, *h, *u);
            }
            break;
        }
        default: break;
    }
    return true;
}






bool Order_ApplyButton(GameState& s, const Command& c) {
    using K = Command::Kind;
    if (c.kind != K::GroupCreate && c.kind != K::Stop && c.kind != K::GroupDismiss && c.kind != K::NoFire &&
        c.kind != K::MayFire && c.kind != K::SetMode) {
        return false;
    }
    Command rec = c;
    s.order_seq = c.oseq != 0 ? c.oseq : s.order_seq + 1;
    rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
    s.orders.push_back(std::move(rec));
    if (c.args.empty() || (c.kind == K::SetMode && c.args.size() < 2)) return true;
    const auto player = static_cast<std::uint8_t>(c.args[0].raw & 0xff);
    if (player >= s.players.size()) return true;
    const bool parent = c.kind == K::GroupCreate || c.kind == K::Stop || c.kind == K::GroupDismiss;
    s.log_moves = true, s.log_parent = parent ? s.order_seq : 0;
    switch (c.kind) {
        case K::GroupCreate: UI_GroupCreate(s, player, c.units); break;
        case K::Stop:
            for (const UnitRef& sel : c.units) {  // ours (B317): Stop clears the queue (empty in the original)
                if (Unit* u = Unit_Find(s, UnitRef{player, sel.id, sel.serial})) u->order_queue.clear();
            }
            Order_Stop(s, player, c.units);
            break;
        case K::GroupDismiss: Order_GroupDismiss(s, player, c.units); break;
        default:
            for (const UnitRef& sel : c.units) {
                Unit* u = Unit_Find(s, UnitRef{player, sel.id, sel.serial});
                if (u == nullptr) continue;
                if (c.kind == K::NoFire) {
                    CancelAttackForMove(s, player, *u);
                } else if (c.kind == K::MayFire) {
                    u->field_142 = 0;
                    u->field_006 = 1;
                } else {
                    u->field_116 = static_cast<std::uint8_t>(c.args[1].raw);
                }
            }
            break;
    }
    s.log_moves = false;
    return true;
}






void Order_QueueAddHouse(GameState& s, const Command& c) {
    if (c.args.size() != 4) return;
    const auto player = static_cast<std::uint8_t>(c.args[0].raw & 0xff);
    if (player >= s.players.size()) return;
    CommandNode n;
    n.seq = s.next_node_seq++;
    n.cls = 2, n.cmd = 7;
    n.x = std::bit_cast<float>(c.args[1].raw), n.y = std::bit_cast<float>(c.args[2].raw);
    n.a2c = c.args[3].raw;
    for (const UnitRef& r : c.units) n.list.push_back(UnitRef{player, r.id, r.serial});
    s.players[player].commands.push_back(std::move(n));
}

}  // namespace

void Player_NetControl(GameState& s, std::uint8_t p, std::uint8_t level) {
    Player& pl = s.players[p];
    if (pl.net_ai == 0 && pl.control_type != 4) return;



    if (s.original_rules) {
        if (level != 0) TStrateg_Die(s, p);
        return;
    }
    level = std::min<std::uint8_t>(level, 3);
    std::array<std::uint8_t*, 11> kept = {&pl.ai_level,        &pl.field_9b2b38,    &pl.field_9b2b39,
                                          &pl.field_9b2b3a,    &pl.field_9b2b3b,    &pl.field_9b2b3c,
                                          &pl.field_9b2b3d,    &pl.field_9b2a98[0], &pl.field_9b2a98[1],
                                          &pl.field_9b2a98[2], &pl.field_9b2a98[3]};
    if (level != 0) {
        if (pl.net_ai == 0) {
            // As a computer player of the setup has them (InitGame: the switch rows are 1 for every player but
            // MyPlayer).
            for (std::size_t k = 0; k < kept.size(); ++k) pl.net_kept[k] = *kept[k], *kept[k] = 1;
            pl.control_type = 1;
        }
        pl.ai_level = level, pl.net_ai = level;
        return;
    }
    if (pl.net_ai == 0) return;
    for (std::size_t k = 0; k < kept.size(); ++k) *kept[k] = pl.net_kept[k];
    pl.net_kept = {};
    pl.control_type = 4, pl.net_ai = 0;
}





void Order_Apply(GameState& s, const Command& c, std::span<const Command> all) {




    const bool select = c.kind == Command::Kind::SelectHouses;
    const std::uint8_t hp = select && !c.args.empty() ? static_cast<std::uint8_t>(c.args[0].raw & 0xff)
                            : !c.houses.empty()       ? c.houses[0].player
                                                      : 0xff;
    if (hp < s.players.size() && (select || !c.houses.empty())) {
        std::vector<House>& hs = s.players[hp].houses;
        for (House& h : hs) h.selected = 0;
        for (const HouseRef& r : c.houses) {
            if (r.player == hp && r.slot < hs.size()) hs[r.slot].selected = 1;
        }
    }
    if (select) return;
    if (c.kind == Command::Kind::ToggleAdvisor) {
        const std::uint8_t p = c.args.empty() ? 0xff : static_cast<std::uint8_t>(c.args[0].raw & 0xff);
        if (p >= s.players.size() || c.args.size() != 2) return;
        std::uint8_t& flag = c.args[1].raw == 0 ? s.players[p].field_9b2d94 : s.players[p].field_9b2d9c;
        flag = flag == 0 ? 1 : 0;
        return;
    }
    if (c.kind == Command::Kind::MarketAuto) {
        Market_SetAuto(s, c);
        return;
    }
    if (c.kind == Command::Kind::BindGroup) {
        ControlGroup_Bind(s, c);
        return;
    }
    if (c.kind == Command::Kind::FacingOrder) {
        UI_FacingOrder(s, c);
        return;
    }
    if (c.kind == Command::Kind::NetControl) {  // slice MP9: the server's control change, a network game only
        if (s.net_game != 0 && c.args.size() == 2 && c.args[0].raw < s.players.size()) {
            Player_NetControl(s, static_cast<std::uint8_t>(c.args[0].raw), static_cast<std::uint8_t>(c.args[1].raw));
        }
        return;
    }
    if (c.kind ==
        Command::Kind::PlusStrike) {  // Entente Engine Plus: the town centre's call-in strike (sim/strike.hpp)
        if (c.args.size() == 6 && c.args[0].raw < s.players.size()) {
            const auto i32 = [&c](std::size_t i) { return static_cast<std::int32_t>(c.args[i].raw); };
            Strike_Call(s, static_cast<std::uint8_t>(c.args[0].raw), static_cast<std::uint8_t>(c.args[1].raw & 0xff),
                        i32(2), i32(3), i32(4), i32(5));
        }
        return;
    }
    if (c.kind == Command::Kind::QueueOrder) {  // ours (docs/bugs.md B317): Shift + right click, no O line
        Order_ApplyQueue(s, c);
        return;
    }
    if (c.kind == Command::Kind::SelectSummary) {
        if (c.args.size() != 3) return;
        if (c.args[1].raw != 0xffff) s.sel_8ab774 = static_cast<std::uint16_t>(c.args[1].raw);
        if (c.args[2].raw != 0xffff) s.sel_8ab770 = static_cast<std::uint16_t>(c.args[2].raw);
        return;
    }
    if (c.kind == Command::Kind::Act_c26e0 || c.kind == Command::Kind::Act_c2d80) {
        // The market's exchange and gift (docs/interface.md C.3): the O line is written on entry, as by the hook.
        Command rec = c;
        s.order_seq = c.oseq != 0 ? c.oseq : s.order_seq + 1;
        rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
        s.orders.push_back(std::move(rec));
        auto u8 = [&c](std::size_t i) { return static_cast<std::uint8_t>(c.args[i].raw & 0xff); };
        auto f = [&c](std::size_t i) { return std::bit_cast<float>(c.args[i].raw); };
        if (c.kind == Command::Kind::Act_c26e0 && c.args.size() == 8) {
            Market_Exchange(s, u8(0), f(1), f(2), f(3), f(4), f(5), u8(6), f(7));
        } else if (c.kind == Command::Kind::Act_c2d80 && c.args.size() == 7) {
            Market_Send(s, u8(0), u8(1), f(2), f(3), f(4), f(5), f(6));
        }
        return;
    }
    if (c.kind == Command::Kind::Act_6cf80) {


        Command rec = c;
        s.order_seq = c.oseq != 0 ? c.oseq : s.order_seq + 1;
        rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
        s.orders.push_back(std::move(rec));
        if (c.args.size() != 5) return;
        auto i32 = [&c](std::size_t i) { return static_cast<std::int32_t>(c.args[i].raw); };
        Pontoon_Lay(s, static_cast<std::uint8_t>(c.args[0].raw & 0xff), i32(1), i32(2), i32(3), i32(4));
        return;
    }
    if (c.kind == Command::Kind::Act_b66a0) {




        Command rec = c;
        s.order_seq = c.oseq != 0 ? c.oseq : s.order_seq + 1;
        rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
        s.orders.push_back(std::move(rec));
        if (c.args.size() != 2) return;
        const auto p = static_cast<std::uint8_t>(c.args[0].raw & 0xff);
        std::array<std::uint8_t, 8> table{};
        for (std::size_t i = 0; i < table.size() && i < c.args[1].values.size(); ++i) {
            table[i] = static_cast<std::uint8_t>(c.args[1].values[i] & 0xff);
        }
        const std::uint8_t self = s.net_game != 0 ? p : s.my_player;
        if (self < table.size()) table[self] = 1;
        if (p >= s.players.size()) return;
        for (std::size_t i = 0; i < s.players.size() && i < table.size(); ++i) {
            s.players[p].is_ally[i] = table[i];
            Player& o = s.players[i];
            if (table[i] == 0 && o.is_lost == 0 && o.control_type == 1) o.is_ally[p] = 0;
        }
        return;
    }
    if (c.kind == Command::Kind::Act_a5380 || c.kind == Command::Kind::Act_a53f0) {



        Command rec = c;
        s.order_seq = c.oseq != 0 ? c.oseq : s.order_seq + 1;
        rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
        s.orders.push_back(std::move(rec));
        const bool military = c.kind == Command::Kind::Act_a53f0;
        if (c.args.size() != (military ? 3u : 2u) || c.args[1].values.size() < (military ? 4u : 6u)) return;
        const auto p = static_cast<std::uint8_t>(c.args[0].raw & 0xff);
        if (p >= s.players.size()) return;
        Player& pl = s.players[p];
        const std::vector<std::int32_t>& v = c.args[1].values;
        auto on = [&v](std::size_t k) { return static_cast<std::uint8_t>(v[k] != 0 ? 1 : 0); };
        if (military) {
            for (std::size_t k = 0; k < pl.field_9b2a98.size(); ++k) pl.field_9b2a98[k] = on(k);
            pl.ai_kind = static_cast<std::uint8_t>(c.args[2].raw & 0xff);
        } else {
            pl.field_9b2b38 = on(0), pl.field_9b2b39 = on(1), pl.field_9b2b3a = on(2);
            pl.field_9b2b3b = on(3), pl.field_9b2b3c = on(4), pl.field_9b2b3d = on(5);
        }
        return;
    }
    if (c.kind == Command::Kind::Act_ea260) {
        Command rec = c;
        s.order_seq = c.oseq != 0 ? c.oseq : s.order_seq + 1;
        rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
        s.orders.push_back(std::move(rec));
        const std::uint8_t player = c.args.empty() ? 0xff : static_cast<std::uint8_t>(c.args[0].raw & 0xff);
        if (player < s.players.size()) {
            const UnitRef first = c.units.empty() ? UnitRef{} : UnitRef{player, c.units[0].id, c.units[0].serial};
            Order_DeleteKey(s, player, c.units.empty() ? nullptr : &first);
        }
        return;
    }
    if (c.kind == Command::Kind::AddHouse) {
        Order_QueueAddHouse(s, c);
        return;
    }
    if (c.kind == Command::Kind::Act_b9730 || c.kind == Command::Kind::Rally) {
        Order_ApplyHouse(s, c);
        return;
    }
    if (c.kind == Command::Kind::Act_a5290 || c.kind == Command::Kind::Act_a52c0) {
        Order_ApplyResearch(s, c);
        return;
    }
    if (c.kind == Command::Kind::Unload) {
        Command rec = c;
        s.order_seq = c.oseq != 0 ? c.oseq : s.order_seq + 1;
        rec.tick = s.tick + 1, rec.oseq = s.order_seq, rec.parent = 0, rec.rng_index = s.rng_index;
        s.orders.push_back(std::move(rec));
        const std::uint8_t p = c.args.empty() ? 0xff : static_cast<std::uint8_t>(c.args[0].raw & 0xff);
        s.log_moves = true, s.log_parent = 0;  // the passengers' moves aside are top-level O lines after it
        if (p < s.players.size()) Transport_Unload(s, p, c.units);
        s.log_moves = false;
        return;
    }
    if (c.kind == Command::Kind::OrderPoint) {
        Order_ApplyPoint(s, c);
        return;
    }
    if (Order_ApplyButton(s, c)) return;
    if (Order_ApplyHousePanel(s, c, all)) return;
    if (c.kind == Command::Kind::OrderMove && c.parent == 0) {
        // The recorder writes the moves a button order makes as top-level lines after it; the sim makes (and
        // writes) them itself when it applies that order.
        const Command* before = nullptr;
        for (const Command& k : all) {
            if (&k == &c) break;
            if (k.parent == 0 && k.kind != Command::Kind::OrderMove) before = &k;
        }
        if (before != nullptr &&
            (before->kind == Command::Kind::GroupCreate || before->kind == Command::Kind::Stop ||
             before->kind == Command::Kind::GroupDismiss || before->kind == Command::Kind::Unload)) {
            return;
        }
    }
    if (c.kind != Command::Kind::UiOrder) {
        Unported(s, Command_Address(c.kind));
        return;
    }
    bool raw3 = c.args.size() == 3;
    for (const OrderArg& a : c.args) raw3 = raw3 && a.tag == OrderArg::Tag::Raw;
    const Click click = Click_FromChildren(s, c, all);
    if (!raw3 || click.other != 0 || click.kind < 0 || click.kind == 3 || click.kind == 6) {

        Unported(s, click.other != 0 ? click.other : Command_Address(c.kind));
        return;
    }
    UI_OrderSelected(s, c, click);
    s.log_moves = false;
}

bool Unit_RouteBusy(const Unit& u) { return !u.order_queue.empty() || Unit_Busy(u); }

// Our rules (docs/bugs.md B317), in the unit's update before its MoveFunc: a unit with queued orders that has nothing
// to do takes the next one (one whose target is gone is dropped and the one after it taken). Entering a house or a
// transport ends the route, as does joining a formation group (groups keep the original's orders) or a player the
// computer took over; a dead unit drops it.
void OrderQueue_Next(GameState& s, std::uint8_t p, Unit& u) {
    if (u.order_queue.empty()) return;

    if (s.original_rules || u.in_group || u.is_deleted != 0 || u.work == 3 || u.work == 0x14 || !Player_IsYou(s, p)) {
        u.order_queue.clear();
        return;
    }
    if (Unit_Busy(u)) return;
    while (!u.order_queue.empty()) {
        const Unit::QueuedOrder q = u.order_queue.front();
        u.order_queue.erase(u.order_queue.begin());
        if (OrderQueue_Take(s, p, u, q)) return;
    }
}

}  // namespace antanta::sim
