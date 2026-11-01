


#include <algorithm>
#include <bit>
#include <cmath>

#include "dmath.hpp"
#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

namespace {

constexpr std::uint8_t kOrientToDir[8] = {5, 4, 8, 3, 6, 2, 7, 1};
constexpr std::int32_t kOrientSide[8][2] = {{-1, 0}, {-1, 1}, {0, 1},  {1, 1},
                                            {1, 0},  {1, -1}, {0, -1}, {-1, -1}};

constexpr UnitRef kNoMember{};

Unit* Member(GameState& s, const Group& g, std::size_t i) {
    const UnitRef& r = g.members[i];
    return r.player == 0xff ? nullptr : Unit_Find(s, r);
}

const UnitType& TypeOf(const GameState& s, const Group& g, const Unit& u) { return Unit_Type(s, g.player, u); }


std::uint8_t OrientToDir(std::uint8_t o) { return o < 8 ? kOrientToDir[o] : 1; }


std::uint8_t DirToOrient(std::uint8_t d) {
    constexpr std::uint8_t kDirToOrient[9] = {0, 7, 5, 3, 1, 0, 4, 6, 2};
    return d >= 1 && d <= 8 ? kDirToOrient[d] : 0;
}

void Group_Dismiss(GameState& s, Group& g, bool keep);
void Group_PathDone(GameState& s, Group& g);




std::uint8_t GroupPathWave(GameState& s, std::int32_t sx, std::int32_t sy, std::int32_t tx, std::int32_t ty,
                           std::vector<std::uint8_t>& dirs) {
    const Map& m = s.map;
    const std::int32_t w = m.width, h = m.height;
    const Cell tg = WorldToCell(static_cast<float>(tx), static_cast<float>(ty));
    const Cell st = WorldToCell(static_cast<float>(sx), static_cast<float>(sy));
    if (st.x == tg.x && st.y == tg.y) return 0xff;
    if (st.x < 0 || st.y < 0 || st.x >= w || st.y >= h || tg.x <= 3 || tg.y <= 9 || tg.x >= w - 3 || tg.y >= h - 9) {
        return 0;
    }
    std::vector<std::uint16_t> g = m.pass_land;
    const auto at = [&](Cell c) -> std::uint16_t& { return g[static_cast<std::size_t>(c.y * w + c.x)]; };
    const auto inside = [&](Cell c) { return c.x >= 0 && c.y >= 0 && c.x < w && c.y < h; };
    at(st) = 1;
    at(tg) = 0xfffe;
    s.path_budget_used += s.path_budget_max;
    std::uint16_t v = 1;
    std::vector<Cell> cur{st}, next;
    bool found = false;
    while (!cur.empty()) {
        next.clear();
        for (const Cell c : cur) {
            if (at(c) != v) {
                next.push_back(c);
                continue;
            }
            for (std::uint8_t d = 1; d <= 8; ++d) {
                const Cell n = Cell_Step(c, d);
                if (n.x == tg.x && n.y == tg.y) {
                    at(n) = static_cast<std::uint16_t>(v + 1);
                    found = true;
                    break;
                }
                if (!inside(n) || at(n) != 0) continue;
                at(n) =
                    static_cast<std::uint16_t>(kStepCost[d] + m.weight[static_cast<std::size_t>(n.y * w + n.x)] + v);
                next.push_back(n);
            }
            if (found) break;
        }
        ++v;
        if (v > 8000) return 0;
        if (found) break;
        cur.swap(next);
    }
    if (!found) return 0;
    // Backtrack from the target to labels <= p, directions 8 down to 1.
    std::uint16_t p = static_cast<std::uint16_t>(at(tg) - 1);
    if (p == 0) return 0xff;
    at(tg) = 0xffff;
    std::vector<std::uint8_t> buf;
    Cell c = tg;
    for (;;) {
        std::uint8_t best = 0;
        for (std::uint8_t d = 8; d >= 1; --d) {
            const Cell n = Cell_Step(c, d);
            const std::uint16_t lv = inside(n) ? at(n) : 0;
            if (lv != 0 && lv <= p) best = d, p = lv;
        }
        if (best != 0) {
            c = Cell_Step(c, best);
            buf.push_back(best);
            if (buf.size() > 4999) return 0;
        }
        if (p == 1) break;
        --p;
    }
    dirs.resize(buf.size());
    for (std::size_t j = 0; j < buf.size(); ++j) dirs[j] = kOpposite[buf[buf.size() - 1 - j]];
    return 1;
}




bool Group_PlanPath(GameState& s, Group& g) {
    if (g.wp_count != 0) {
        g.wp.clear();
        g.wp_orient.clear();
        g.wp_count = 0;
    }
    if (g.large != 0) {
        Unported(s, orig::kGroupPlanPath);  // large-unit groups (g_PassLarge, no relocation)
        return false;
    }
    const Cell r = RelocateTargetLand(s, WorldToCell(static_cast<float>(g.tx), static_cast<float>(g.ty)));
    g.tx = (r.x * 2 - (~r.y & 1)) * 48 + 45;
    g.ty = r.y * 24;
    std::vector<std::uint8_t> dirs;
    // Our rules: down the shared field with the wave's weights (docs/bugs.md B88); the wave when the field cannot say.
    FlowResult flow = FlowResult::Unknown;
    const Cell st = WorldToCell(static_cast<float>(g.slots[0][0]), static_cast<float>(g.slots[0][1]));
    const Cell tg = WorldToCell(static_cast<float>(g.tx), static_cast<float>(g.ty));
    if (!s.original_rules && tg.x > 3 && tg.y > 9 && tg.x < s.map.width - 3 && tg.y < s.map.height - 9) {
        flow = Flow_Path(s, st, tg, true, dirs);
    }
    std::uint8_t wave = flow == FlowResult::Path ? 1 : 0;
    if (flow == FlowResult::Unknown) wave = GroupPathWave(s, g.slots[0][0], g.slots[0][1], g.tx, g.ty, dirs);
    if (wave != 1) {
        if (g.ai_owned != 0) {
            g.kind = 1;
            g.tx = g.slots[0][0], g.ty = g.slots[0][1];
            g.dead = 1;
            Group_Dismiss(s, g, false);
        } else {
            Group_PathDone(s, g);
        }
        return false;
    }
    const auto n = static_cast<std::uint16_t>(dirs.size());
    if (n < 10) {
        Group_PathDone(s, g);
        return false;
    }
    g.wp.assign(n, {});
    g.wp_orient.assign(n, 0);
    g.wp_next = 0;
    g.wp_count = n;
    g.wp[0] = g.slots[0];
    for (std::size_t i = 1; i < n; ++i) {
        const std::uint8_t d = dirs[i - 1];
        g.wp_orient[i] = DirToOrient(d);
        g.wp[i] = {g.wp[i - 1][0] + kDirWorld[d][0], g.wp[i - 1][1] + kDirWorld[d][1]};
    }
    g.wp_orient[0] = g.wp_orient[1];
    return true;
}



std::array<std::int32_t, 2> Group_NextWaypoint(Group& g) {
    const std::uint16_t k = g.wp_next;
    std::uint16_t next = static_cast<std::uint16_t>(g.spacing + k);
    if (g.wp_count <= next) next = g.wp_count;
    const std::uint8_t old = g.orient;
    g.orient = g.wp_orient[k];
    if (g.orient != old && k != 0) {
        const float f = g.speed_factor;
        g.speed_factor = 0.8f;
        g.speed = static_cast<float>(static_cast<double>(g.speed) / f);
        g.speed = static_cast<float>(static_cast<double>(g.speed) * static_cast<double>(0.8f));
        g.slow = static_cast<std::uint8_t>(g.members.size() / g.cols + 2);
    }
    g.wp_next = next;
    return g.wp[k];
}

void SetGroup(Unit& u, std::uint32_t serial, std::uint16_t index) {
    u.in_group = serial != 0;
    u.group = serial;
    if (serial != 0) u.group_index = index;
}


void Group_OrderMove(GameState& s, const Group& g, Unit& u, float x, float y, std::uint8_t flag) {
    TUnit_OrderMove(s, g.player, u, x, y, flag);
}


void Unit_FaceTo(GameState& s, const Group& g, Unit& u, std::uint8_t o) {
    u.field_178 = o;
    if ((u.num_work == 3 || u.num_work == 4) && o != static_cast<std::uint8_t>(u.orient)) {
        u.target_orient = o;
        if (TypeOf(s, g, u).anim_size[5] != 0) {
            u.num_work = 5;
            u.num_sprite = 0;
            return;
        }
        u.orient = o;
    }
}



void Group_PickLeader(GameState& s, Group& g) {
    const std::uint16_t count = static_cast<std::uint16_t>(g.members.size());
    const std::uint16_t c = static_cast<std::uint16_t>(g.cols >> 1);
    if (c < count) {
        for (std::int32_t i = c; i >= 0; --i) {
            g.leader = static_cast<std::uint16_t>(i);
            if (Member(s, g, static_cast<std::size_t>(i)) != nullptr) return;
        }
    }
    g.leader = 0;
    if (count == 0 || Member(s, g, 0) != nullptr) return;
    for (std::uint16_t i = 0; i < count; ++i) {
        const Unit* u = Member(s, g, i);
        if (u != nullptr && TypeOf(s, g, *u).move_class == 7) {
            g.leader = i;
            return;
        }
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        const Unit* u = Member(s, g, i);
        if (u != nullptr && TypeOf(s, g, *u).is_air == 0) {
            g.leader = i;
            return;
        }
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        if (Member(s, g, i) != nullptr) {
            g.leader = i;
            return;
        }
    }
}



bool Group_CheckSlot(GameState& s, Group& g, std::size_t i) {
    auto& p = g.slots[i];
    const Cell c = WorldToCell(static_cast<float>(p[0]), static_cast<float>(p[1]));
    const bool bad = c.x < 4 || c.y < 10 || s.map.width - 3 <= c.x || s.map.height - 9 <= c.y ||
                     s.map.pass_land[static_cast<std::size_t>(s.map.width * c.y + c.x)] != 0;
    if (!bad) return true;
    if (g.slots.empty()) return false;
    if (p == g.slots[0]) {
        const Cell r = RelocateTargetLand(s, c);
        p = {(r.x * 2 - (~r.y & 1)) * 48 + 45, r.y * 24};
        return true;
    }
    ++g.blocked;
    if (Rng_Next(s) < 500) p = g.slots[0];
    return true;
}


void Group_UpdateFlags(GameState& s, Group& g) {
    g.has_mc7 = 0, g.has_mc10 = 0;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        const Unit* u = Member(s, g, i);
        if (u == nullptr) continue;
        const std::uint8_t mc = TypeOf(s, g, *u).move_class;
        if (mc == 7) {
            g.has_mc7 = 1;
            if (g.has_mc10 != 0) return;
        } else if (mc == 10) {
            g.has_mc10 = 1;
            if (g.has_mc7 != 0) return;
        }
    }
}


void Group_Recalc(GameState& s, Group& g) {
    double min = 999.0f;
    std::int16_t size = 0;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        const Unit* u = Member(s, g, i);
        if (u == nullptr) continue;
        const UnitType& t = TypeOf(s, g, *u);
        if (min > static_cast<double>(t.speed[g.player])) min = t.speed[g.player];
        if (size < t.size) size = t.size;
    }
    if (g.keep_spacing == 0) g.spacing = static_cast<std::uint8_t>(size / 18 + 1);
    if (g.spacing > 2) g.spacing = 2;
    if (g.live == 1) g.spacing = 1;
    g.speed = static_cast<float>(min - static_cast<double>(0.05f) * min);
}


void Group_Clear(GameState& s, Group& g) {
    g.wp_count = 0;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        if (Unit* u = Member(s, g, i)) SetGroup(*u, 0, 0);
    }
    g.members.clear();
    g.slots.clear();
    g.live = 0, g.quorum = 0;
    g.selected = 0;
    g.field_048 = 0;
}


void Unit_MoveToFront(Player& pl, const Unit& u) {
    const std::uint32_t slot = pl.id_slots[u.id];
    Unit& v = pl.unit_slots[slot];
    if (pl.first_unit == slot) return;
    if (v.prev != kNoSlot) pl.unit_slots[v.prev].next = v.next;
    if (v.next != kNoSlot) pl.unit_slots[v.next].prev = v.prev;
    v.prev = kNoSlot;
    v.next = pl.first_unit;
    pl.unit_slots[pl.first_unit].prev = slot;
    pl.first_unit = slot;
}

// The x and y vectors of one formation row and column for orientation o (2.5 in docs/groups.md).
struct Axes {
    std::int32_t row_x, row_y, col_x, col_y;
    bool ew;  // facing east or west (direction 7 or 8)
};

Axes Group_Axes(GameState& s, const Group& g, std::uint8_t o) {
    const std::uint8_t dir = OrientToDir(o);
    const std::int32_t sp = g.spacing;
    const auto& side = kOrientSide[o < 8 ? o : 0];
    Axes a{sp * kDirWorld[dir][0], sp * kDirWorld[dir][1], sp * -side[0] * 18, sp * -side[1] * 6, dir == 7 || dir == 8};
    if (g.spacing_override > 0.0f) Unported(s, orig::kGroupStep);
    return a;
}

// The layout loop shared by Group_Step and Group_Arrange: member i at column i % cols, row i / cols, from the base;
// every slot but the anchor gets (3 - T % 6) * 2 per coordinate (I += 2 per read).
void Group_Layout(GameState& s, Group& g, const Axes& a, std::int32_t bx, std::int32_t by, std::size_t anchor) {
    if (g.type == 0 || g.type > 3) return;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        const auto q = static_cast<std::int32_t>(i / g.cols), r = static_cast<std::int32_t>(i % g.cols);
        if (i == anchor) {
            g.slots[i] = {r * a.col_x - q * a.row_x + bx, r * a.col_y - q * a.row_y + by};
            Group_CheckSlot(s, g, i);
            continue;
        }
        const auto jx = static_cast<std::int32_t>((3 - s.rng_table[s.rng_index] % 6) * 2);
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
        g.slots[i][0] = r * a.col_x + (jx - q * a.row_x) + bx;
        const auto jy = static_cast<std::int32_t>((3 - s.rng_table[s.rng_index] % 6) * 2);
        s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
        g.slots[i][1] = r * a.col_y + (jy - q * a.row_y) + by;
    }
}



void Group_Step(GameState& s, Group& g, bool init) {
    if (g.live == 0 || g.dead != 0) return;
    g.stepping = 1;
    Group_PickLeader(s, g);
    if (g.large != 0) {
        Unported(s, orig::kGroupStep);  // large-unit groups
        return;
    }
    if (init) {
        const Unit* l = Member(s, g, g.leader);
        if (l == nullptr) return;
        std::int32_t ax = Ftol(l->x), ay = Ftol(l->y);
        if (g.kind != 0) {
            // The anchor moves toward the target by count / cols * spacing * 57 (fpatan, fcos, fsin on the stack).
            const double ang = dmath::atan2(static_cast<double>(g.ty - ay), static_cast<double>(g.tx - ax));
            const double r = static_cast<double>(g.members.size()) / g.cols * g.spacing * static_cast<double>(57.0f);
            const std::int32_t nx = Ftol(dmath::cos(ang) * r + ax);
            ay = Ftol(dmath::sin(ang) * r + ay);
            ax = nx;
        }
        const Cell r = RelocateTargetLand(s, WorldToCell(static_cast<float>(ax), static_cast<float>(ay)));
        g.slots[0] = {(r.x * 2 - (~r.y & 1)) * 48 + 48, r.y * 24};
        if (g.tx == 0 && g.ty == 0) g.tx = g.slots[0][0], g.ty = g.slots[0][1];
        if (!Group_CheckSlot(s, g, 0)) {
            g.queued = 0;
            g.dead = 1;
            return;
        }
        if (g.kind != 0) Group_PlanPath(s, g);
    }
    if (g.dead != 0) return;
    if (g.wp_count < 2) {
        if (g.leader == 0xffff) {
            g.orient = static_cast<std::uint8_t>(Rng_Next(s) & 7);
        } else {
            const Unit* l = Member(s, g, g.leader);
            g.orient = l != nullptr ? l->orient : g.orient;
        }
    } else {
        g.slots[0] = Group_NextWaypoint(g);
        g.orient = g.wp_next < g.wp_count ? g.wp_orient[g.wp_next] : g.wp_orient[g.wp_count - 1u];
    }
    Axes a = Group_Axes(s, g, g.orient);
    if (a.ew && g.spacing_override < 0.0f) a.col_y *= 2;
    Group_Layout(s, g, a, g.slots[0][0], g.slots[0][1], 0);
    g.stepping = 1;
    ++s.formation_budget;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        Unit* u = Member(s, g, i);
        if (u == nullptr) continue;
        u->field_106 = 1;
        Group_OrderMove(s, g, *u, static_cast<float>(g.slots[i][0]), static_cast<float>(g.slots[i][1]), 0);
    }
}



void Group_Arrange(GameState& s, Group& g, bool flag) {
    if (g.queued != 0 || g.members.empty() || g.dead != 0 || g.arrange_orient == 0xff || g.leader == 0xffff ||
        g.leader >= g.members.size() || Member(s, g, g.leader) == nullptr || (g.tx == 0 && g.ty == 0)) {
        return;
    }
    g.stepping = 1;
    g.orient = g.arrange_orient;
    Axes a = Group_Axes(s, g, g.orient);
    if (a.ew) {
        if (g.spacing_override < 0.0f) a.col_y *= 2;
        a.row_x /= 2;
    }
    const std::int32_t cols = g.cols;
    const std::int32_t bx = g.tx - cols * a.col_x / 2, by = g.ty - cols * a.col_y / 2;
    Group_Layout(s, g, a, bx, by, g.leader);
    g.stepping = 1;
    if (!flag) return;
    ++s.formation_budget;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        Unit* u = Member(s, g, i);
        if (u == nullptr) continue;
        const float fy = static_cast<float>(g.slots[i][1]), fx = static_cast<float>(g.slots[i][0]);
        const double dx = static_cast<double>(u->x) - fx, dy = static_cast<double>(u->y) - fy;
        if (std::sqrt(dy * dy + dx * dx) > static_cast<double>(10.0f)) Group_OrderMove(s, g, *u, fx, fy, 1);
        Unit_FaceTo(s, g, *u, g.arrange_orient);
    }
}




void Group_Dismiss(GameState& s, Group& g, bool keep) {
    if (g.ai_owned == 0) g.ai_owned = 1;  // (UI group panel)
    if (g.live == 0) return;
    const auto n =
        static_cast<std::uint32_t>(static_cast<std::uint16_t>(Ftol(std::sqrt(static_cast<double>(g.live))) + 1));
    std::uint32_t k = 0;
    const Unit* first = nullptr;  // the first member: the mover of kinds 2 and 6
    // g_Strateg[tp] is set and not lost (kind 3 also tests tp < NumPlayers; past the players: no player).
    const bool target_live = g.target_player < s.players.size() && s.players[g.target_player].is_lost == 0;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        Unit* u = Member(s, g, i);
        if (u == nullptr) continue;
        Unit_MoveToFront(s.players[g.player], *u);
        if (!keep) {
            SetGroup(*u, 0, 0);
            u->group_index = 0xffff;
        }
        if (first == nullptr) first = u;


        const bool skip = keep && g.has_mc10 != 0 && Unit_Type(s, g.player, *u).move_class != 10;
        switch (g.kind) {
            case 0:
            case 1: {
                const std::uint32_t r0 = s.rng_table[s.rng_index];
                const std::uint32_t r1 = s.rng_table[static_cast<std::uint16_t>(s.rng_index + 1)];
                s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
                const auto x = static_cast<std::int32_t>(((k % n) * 2 - n + 1) * 15 - r0 % 30) + g.tx;
                const auto y = static_cast<std::int32_t>(((k / n) * 2 - n + 1) * 15 - r1 % 30) + g.ty;
                Group_OrderMove(s, g, *u, static_cast<float>(x), static_cast<float>(y), 1);
                break;
            }
            case 2:
                if (skip) continue;
                if (!target_live) break;
                if (g.target_id >= s.players[g.target_player].houses.size()) {
                    Unported(s, orig::kGroupDismiss);  // a house slot the port does not have
                    break;
                }
                TUnit_AttackHouse(s, g.player, *u, *first, g.target_player, g.target_id, static_cast<std::uint16_t>(k),
                                  1);
                break;
            case 3:
                if (skip) continue;
                if (target_live) {
                    if (Unit* t = Unit_FindById(s, g.target_player, g.target_id)) {
                        t->field_141 = 0xc;
                        TUnit_OnAttackedBy(s, g.player, *u, g.target_player, *t, 1);
                    }
                }
                break;
            case 4: {
                const auto r = static_cast<std::int32_t>(s.rng_table[s.rng_index] % 30);
                s.rng_index = static_cast<std::uint16_t>(s.rng_index + 2);
                const auto c = static_cast<std::int32_t>(n * 15);
                const auto ix = static_cast<std::int32_t>(k % n) * 30, iy = static_cast<std::int32_t>(k / n) * 30;
                // x87: (tx + 15.0f - r + ix) - n * 15, all integers well inside a float's exact range.
                const auto x = static_cast<float>(static_cast<double>(g.tx) + 15.0 - r + ix - c);
                const auto y = static_cast<float>(static_cast<double>(g.ty) + 15.0 - r + iy - c);
                TUnit_OrderXy(s, g.player, *u, x, y, g.target_id);
                break;
            }
            case 6:
                if (target_live) Worker_SendToHouse(s, g.player, *u, g.target_id, static_cast<std::uint16_t>(k), first);
                break;
            default: break;  // kind 5: nothing
        }
        ++k;
    }
    g.kind = 0;
}


void Group_PathDone(GameState& s, Group& g) {
    if (!g.follow_up.empty()) {

        const std::array<std::uint8_t, 16> f = g.follow_up.front();
        g.follow_up.erase(g.follow_up.begin());
        const auto i32 = [&f](std::size_t k) {
            return static_cast<std::int32_t>(f[k] | f[k + 1] << 8 | f[k + 2] << 16 |
                                             static_cast<std::uint32_t>(f[k + 3]) << 24);
        };
        Group_Order(s, g, f[3], i32(4), i32(8), f[12], static_cast<std::uint16_t>(f[14] | f[15] << 8));
        return;
    }
    g.wp_count = 0, g.wp_next = 0;
    if (g.ai_owned != 0) {
        g.dead = 1;
        return;
    }
    bool flag = true;
    if (g.kind == 3 || g.kind == 2) {
        flag = false;
        Group_Dismiss(s, g, true);
        g.ai_owned = 0;
    }
    if (g.arrange_orient != 0xff) {
        Group_Arrange(s, g, flag);
        g.stepping = 1;
        return;
    }
    g.arrange_orient = g.orient;
    Group_Arrange(s, g, flag);
    g.arrange_orient = 0xff;
    g.stepping = 1;
}




void Group_CheckArrival(GameState& s, Group& g) {
    if (g.field_048 != 0) return;
    double sum = 0.0;
    std::uint16_t arrived = 0, wait = 0;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        Unit* u = Member(s, g, i);
        if (u == nullptr) continue;
        const auto& sl = g.slots[i];
        float local = 0.0f;
        if (i == 0 || sl != g.slots[0]) {
            local = static_cast<float>(std::fabs(static_cast<double>(u->y) - sl[1]) +
                                       std::fabs(static_cast<double>(u->x) - sl[0]));
        }
        sum = sum + local;
        const float sumf = static_cast<float>(sum);
        if (local < 40.0f) {
            sum = sum - local;
            ++arrived;
            if (g.wp_next < 2) {
                if (g.quorum <= arrived) g.stepping = 0;
            } else if (arrived == g.live) {
                g.stepping = 0;
            }
        } else if (g.slow == 0 && (u->num_work == 3 || u->num_work == 4) && u->field_107 == 0 && u->field_07c == 0 &&
                   u->field_09f == 0) {
            Group_OrderMove(s, g, *u, static_cast<float>(sl[0]), static_cast<float>(sl[1]), 0);
            if ((u->num_work == 3 || u->num_work == 4) && u->field_107 == 0) {
                if (!Group_CheckSlot(s, g, i) || u->field_107 == 0) g.stepping = 0;
            }
            sum = sumf;
        } else if (g.large == 0) {
            if (u->field_09f != 0 || u->field_07c != 0) wait = 0x14;
        } else {
            Unported(s, orig::kGroupCheckArrival);  // large-unit groups
        }
        if (g.stepping == 0) {
            if (local > 500.0f && s.plus_rules && g.ai_owned != 0 && u->vel_x == 0.0f && u->vel_y == 0.0f) {
                // Entente Engine Plus (docs/ai.md 11): a computer's member that stands still more than 500 from its
                // slot (one on a closed cell, or one whose route request waits) leaves the group; the original adds
                // 999999, which costs the whole group two 100-tick waits at every waypoint while it stands there.
                Group_RemoveMember(s, g, *u);
                if (g.dead != 0) return;
                continue;
            }
            if (local > 500.0f) {
                sum = sum + static_cast<double>(999999.0f);
                u->field_106 = 1;
            } else {
                u->field_106 = local > 200.0f ? 1 : 0;
            }
        }
    }
    if (g.stepping != 0) {
        if (static_cast<double>(g.live) * static_cast<double>(0.1f) < static_cast<double>(g.blocked)) {
            const double v = static_cast<double>(g.speed) / g.speed_factor;
            g.speed_factor = 0.8f;
            g.slow = 10;
            g.blocked = 0;
            g.stepping = 0;
            g.speed = static_cast<float>(v);
            g.speed = static_cast<float>(static_cast<double>(g.speed) * static_cast<double>(0.8f));
            return;
        }
        if (g.slow != 0 && sum < static_cast<double>(999999.0f)) g.stepping = 0;
        return;
    }
    if (wait != 0 && g.retries < 3) {
        ++g.retries;
        g.wait = wait;
    }
    if (g.slow != 0) return;
    if (sum / static_cast<double>(g.live) > static_cast<double>(500.0f) && g.retries < 2) {
        ++g.retries;
        g.stepping = 1;
        g.wait = 100;
    }
}




void Group_Advance(GameState& s, Group& g) {
    if (g.wp_count != 0) {
        if (g.type == 0 || g.type > 3) return;
        for (std::int32_t i = static_cast<std::int32_t>(g.members.size()) - 1; i >= g.cols; --i) {
            g.slots[static_cast<std::size_t>(i)] = g.slots[static_cast<std::size_t>(i - g.cols)];
        }
        g.slots[0] = Group_NextWaypoint(g);
        const std::uint8_t o = g.orient < 8 ? g.orient : 0;
        std::int32_t cx = g.spacing * -kOrientSide[o][0] * 18, cy = g.spacing * -kOrientSide[o][1] * 6;
        if (g.spacing_override > 0.0f) Unported(s, orig::kGroupAdvance);
        const std::uint8_t dir = OrientToDir(g.orient);
        if (dir == 8 || dir == 7) cy *= g.spacing_override < 0.0f ? 4 : 2;
        for (std::uint16_t i = 0; i < g.cols; ++i) {
            auto& sl = g.slots[i];
            if (i != 0) {
                sl[0] = static_cast<std::int32_t>(s.rng_table[s.rng_index] % 6) + i * cx - 3 + g.slots[0][0];
                s.rng_index = static_cast<std::uint16_t>(s.rng_index + 1);
                sl[1] = static_cast<std::int32_t>(s.rng_table[s.rng_index] % 6) + i * cy - 3 + g.slots[0][1];
                s.rng_index = static_cast<std::uint16_t>(s.rng_index + 1);
            }
            const Cell c = WorldToCell(static_cast<float>(sl[0]), static_cast<float>(sl[1]));
            if (c.x < 4 || c.y < 10 || c.x >= s.map.width - 3 || c.y >= s.map.height - 9 ||
                s.map.pass_land[static_cast<std::size_t>(s.map.width * c.y + c.x)] != 0) {
                sl = g.slots[0];
                ++g.blocked;
                if (static_cast<double>(g.live) * static_cast<double>(0.1f) < static_cast<double>(g.blocked)) {
                    g.slow = 10;
                    const float f = g.speed_factor;
                    g.speed_factor = 0.8f;
                    g.blocked = 0;
                    g.stepping = 0;
                    g.speed = static_cast<float>(static_cast<double>(g.speed) / f);
                    g.speed = static_cast<float>(static_cast<double>(g.speed) * static_cast<double>(0.8f));
                }
            }
        }
    }
    if (g.slow != 0 && --g.slow == 0) {
        const float f = g.speed_factor;
        g.speed_factor = 1.0f;
        g.speed = static_cast<float>(static_cast<double>(g.speed) / f);
    }
}





void Group_TrackTarget(GameState& s, Group& g) {
    const std::uint8_t tp = g.target_player;
    if (g.kind == 2) {
        if (tp < s.players.size() && g.target_id < s.players[tp].houses.size() && s.players[tp].is_lost == 0) {
            const House& h = s.players[tp].houses[g.target_id];
            if (h.is_destroyed == 0) {
                if (g.large == 0) return;
                const Unit* l = Member(s, g, g.leader);
                if (l == nullptr) {
                    Unported(s, orig::kGroupTrackTarget);  // no leader: the original reads a null member
                    return;
                }
                const double dx = static_cast<double>(l->x) - h.x, dy = static_cast<double>(l->y) - h.y;
                const double d = std::sqrt(dx * dx + dy * dy);
                if (!(static_cast<double>(Unit_Type(s, g.player, *l).range[g.player]) > d)) return;
                Group_PathDone(s, g);
                return;
            }
        }
    } else if (g.kind == 3) {
        if (tp < s.players.size() && s.players[tp].is_lost == 0) {
            const Unit* t = Unit_FindById(s, tp, g.target_id);
            if (t != nullptr && t->is_deleted == 0) {
                const float fx = static_cast<float>(g.tx), fy = static_cast<float>(g.ty);
                const double dx = static_cast<double>(fx) - t->x, dy = static_cast<double>(fy) - t->y;
                if (std::sqrt(dx * dx + dy * dy) <= static_cast<double>(500.0f)) return;
                const std::int32_t x = Ftol(t->x), y = Ftol(t->y);
                g.tx = x, g.ty = y;
                Group_Order(s, g, g.kind, x, y, tp, g.target_id);
                return;
            }
        }
    } else {
        return;
    }
    g.kind = 1;
}


void Group_Update(GameState& s, Group& g) {
    if (g.field_048 != 0) --g.field_048;
    if (g.queued != 0 || g.dead != 0) return;
    if (g.wait != 0) {
        --g.wait;
        return;
    }
    if (g.stepping != 0) {
        Group_CheckArrival(s, g);
        if (g.stepping != 0) return;
        g.retries = 0;
    }
    if (g.wp_count != 0 && g.wp_count <= g.wp_next) {
        Group_PathDone(s, g);
        return;
    }
    if (g.kind != 0) Group_TrackTarget(s, g);
    if (g.stepping != 0 || g.queued != 0 || g.dead != 0 || g.wait != 0) return;
    Group_Advance(s, g);
    if (g.slow != 0) return;
    if (g.wp_count == 0) g.wait = 100;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        Unit* u = Member(s, g, i);
        if (u == nullptr) continue;
        if (u->is_deleted != 0) {
            Group_RemoveMember(s, g, *u);
            continue;
        }
        const auto& sl = g.slots[i];
        const double m = std::fabs(static_cast<double>(u->x) - sl[0]) + std::fabs(static_cast<double>(u->y) - sl[1]);
        if (m > static_cast<double>(5000.0f)) {
            if (s.plus_rules && g.ai_owned != 0) {
                // Entente Engine Plus (docs/ai.md 11): the straggler leaves a computer's group, which goes on; the
                // original ends the whole march here, short of its target.
                Group_RemoveMember(s, g, *u);
                if (g.dead != 0) return;
                continue;
            }
            Group_PathDone(s, g);
            return;
        }
        u->field_106 = m <= static_cast<double>(200.0f) ? 0 : 1;
        if (u->player_unit_attack == 0xff && u->player_house_attack == 0xff && m > static_cast<double>(20.0f)) {
            Group_OrderMove(s, g, *u, static_cast<float>(sl[0]), static_cast<float>(sl[1]), 0);
        }
        if (static_cast<std::uint8_t>(u->orient) != g.orient) Unit_FaceTo(s, g, *u, g.orient);
    }
    g.stepping = 1;
}



void Group_Create(GameState& s, Group& g, const std::vector<Unit*>& list, std::uint8_t a3, std::uint8_t type,
                  std::uint8_t cols, std::uint8_t kind, std::int32_t x, std::int32_t y, std::uint8_t tplayer,
                  std::uint16_t tid, std::uint8_t ai_owned, bool now = false) {
    g.dead = 0;
    g.ai_owned = ai_owned;
    float min_speed = 10000.0f;
    std::uint32_t max_size = 0;
    if (s.formation_budget >= 1 && !now) {


        std::vector<Unit*> kept = list;
        if (Rng_Next(s) < 3000) {
            kept.clear();
            for (Unit* u : list) {
                if (u->is_deleted == 0 && u->work != 3 && u->work != 0x14) kept.insert(kept.begin(), u);
            }
        }
        if (!kept.empty()) {
            CommandNode n;
            n.seq = s.next_node_seq++;
            n.cls = 1, n.cmd = 0x2d;
            n.group = g.serial;
            for (Unit* u : kept) n.list.push_back(Unit_Ref(g.player, *u));
            n.a3 = a3, n.type = type, n.cols = cols, n.kind = kind, n.px = x, n.py = y, n.tplayer = tplayer;
            n.tid = tid;
            s.players[g.player].commands.push_back(std::move(n));
            g.queued = 1;
            return;
        }
        g.ai_owned = 1;
        return;
    }
    g.queued = 0;
    if (!now) ++s.formation_budget;
    Group_Clear(s, g);
    g.kind = kind, g.tx = x, g.ty = y, g.target_id = tid, g.target_player = tplayer;
    const auto count = static_cast<std::uint16_t>(list.size());
    if (type != 0 && type < 4) g.type = type;
    g.cols = cols != 0 ? cols : static_cast<std::uint8_t>(count / 15 + 1);
    if (g.type == 2) {
        g.cols = static_cast<std::uint8_t>((Ftol(std::sqrt(static_cast<double>(count))) + 1) * 2);
    } else if (g.type == 3) {
        g.cols = static_cast<std::uint8_t>(count / g.cols + 1);
    }
    g.members.assign(count, kNoMember);


    g.capacity = static_cast<std::uint16_t>((count / 100 + 1) * 100);
    g.slots.assign(g.capacity, {0, 0});
    g.live = 0;
    g.large = 0;
    for (std::uint16_t i = 0; i < count; ++i) {
        Unit& u = *list[i];
        if (u.is_deleted != 0 || u.work == 3 || u.work == 0x14) continue;
        const UnitType& t = TypeOf(s, g, u);
        if (t.field_009 == 0) {
            TUnit_OrderMove(s, g.player, u, static_cast<float>(g.tx), static_cast<float>(g.ty), 1);
            continue;
        }
        if (t.field_468 != 0) {
            Unported(s, orig::kGroupCreate);  // large-unit groups
            continue;
        }
        ++g.live;
        if (t.speed[g.player] < min_speed) min_speed = t.speed[g.player];
        if (static_cast<std::int32_t>(max_size & 0xffff) < t.size) max_size = static_cast<std::uint32_t>(t.size);
        if (u.in_group) {
            if (Group* old = Unit_Group(s, g.player, u)) {
                Group_RemoveMember(s, *old, u);
            } else {
                Unported(s, orig::kGroupRemoveMember);  // a group loaded from a save
            }
        }
        g.members[i] = Unit_Ref(g.player, u);
        if (g.has_mc7 == 0 && t.move_class == 7) g.has_mc7 = 1;
        if (g.has_mc10 == 0 && t.move_class == 10) g.has_mc10 = 1;
        SetGroup(u, g.serial, i);
    }
    if (g.live == 0) {
        g.ai_owned = 1;
        g.dead = 1;
        return;
    }
    g.quorum = static_cast<std::uint16_t>(
        Ftol(static_cast<double>(g.live) * static_cast<double>(ai_owned != 0 ? 0.3f : 0.5f)));
    g.half = static_cast<std::uint16_t>(g.live >> 1);
    g.arg3 = a3;
    g.speed = static_cast<float>(static_cast<double>(min_speed) -
                                 static_cast<double>(min_speed) * static_cast<double>(0.05f));
    g.spacing = static_cast<std::uint8_t>((max_size & 0xffff) / 18 + 1);
    if (g.spacing > 2) g.spacing = 2;
    if (g.live == 1) g.spacing = 1;
    Group_Step(s, g, true);
    if (g.arg3 == 2 || g.arg3 == 4) {
        g.target_fx = static_cast<float>(g.tx);
        g.target_fy = static_cast<float>(g.ty);
    }
}






std::uint8_t AI_StagingProbe(GameState& s, std::uint8_t player, const std::vector<Unit*>& list, std::int32_t& tx,
                             std::int32_t& ty, std::uint16_t n) {
    double sx = static_cast<double>(0.0f), sy = static_cast<double>(0.0f);
    for (const Unit* u : list) sx += u->x, sy += u->y;
    const float syf = static_cast<float>(sy);
    const double inv = 1.0 / static_cast<double>(n);
    const float invf = static_cast<float>(inv);
    const double cx = sx * inv, cy = static_cast<double>(invf) * syf;
    const std::int32_t ix = Ftol(cx), iy = Ftol(cy);
    const double dx = cx - tx, dy = cy - ty;
    const double d = std::sqrt(dx * dx + dy * dy) * static_cast<double>(0.2f);
    float r = static_cast<float>(d);
    const std::int32_t k = Ftol(d);
    if (k != 0) {
        const std::int32_t m = static_cast<std::int32_t>(s.rng_table[s.rng_index]) % k;
        ++s.rng_index;
        r = static_cast<float>(static_cast<double>(m) + r);
    }
    if (r < 1000.0f) return 1;
    std::array<std::array<std::int32_t, 2>, 11> pts{};
    std::array<std::int32_t, 10> val{};
    pts[10] = {tx, ty};
    std::int32_t m1 = 999999, m2 = 999999;
    std::uint16_t i1 = 0xff, i2 = 0xff;
    for (std::uint16_t i = 0; i < 10; ++i) {
        const std::uint32_t a = s.rng_table[s.rng_index] % 0x168;
        ++s.rng_index;
        val[i] = 999999;
        const double ang = static_cast<double>(a) * static_cast<double>(0.017453292f);
        pts[i][0] = Ftol(dmath::cos(ang) * r + tx);
        pts[i][1] = Ftol(dmath::sin(ang) * r + ty);
        const Cell c = WorldToCell(static_cast<float>(pts[i][0]), static_cast<float>(pts[i][1]));
        if (!(c.x > 3 && c.y > 9 && c.x < s.map.width - 3 && c.y < s.map.height - 9)) continue;
        const std::int32_t e = AI_Threat(s, player, pts[i][0], pts[i][1], ix, iy, 0);
        val[i] = e;
        if (e < m1) {
            if (m2 == 999999) {
                m2 = e, i2 = i;
            } else {
                m2 = m1, i2 = i1;
            }
            m1 = e, i1 = i;
        } else if (e < m2) {
            m2 = e, i2 = i;
        }
    }
    if (i1 == 0xff || (i1 == 10 && i2 == 10)) return 1;
    std::uint8_t a3 = 4;
    std::uint16_t idx = 0;
    const std::uint32_t r1 = s.rng_table[s.rng_index] % 3;
    ++s.rng_index;
    if (r1 == 1) {
        idx = i2;
    } else if (r1 == 2) {
        a3 = 1, idx = 10;
    } else {
        const std::uint32_t r2 = s.rng_table[s.rng_index] % 3;
        ++s.rng_index;
        if (r2 != 1) {
            idx = i1;
        } else {
            idx = static_cast<std::uint16_t>(s.rng_table[s.rng_index] % 10);
            ++s.rng_index;
            if (val[idx] == 999999) a3 = 1, idx = 10;
        }
    }
    tx = pts[idx][0], ty = pts[idx][1];
    return a3;
}

}  // namespace

void Player_MoveUnitToFront(Player& pl, const Unit& u) { Unit_MoveToFront(pl, u); }
void Group_Disband(GameState& s, Group& g, bool keep) { Group_Dismiss(s, g, keep); }

// The slot order of a group the player forms under our rules (docs/bugs.md B92): members come first in the group's
// list, which Group_Step lays out from the front row back. Riflemen, officers and machine gunners first, then
// flamethrowers (and any other), then mortars and guns, workers last.
namespace {
int FrontRank(const UnitType& t) {
    switch (t.move_class) {
        case 2:
        case 7:
        case 16: return 0;
        case 10:
        case 3: return 2;
        case 6: return 3;
        default: return 1;
    }
}
}  // namespace







void AI_FormGroups(GameState& s, std::uint8_t player, std::vector<Unit*> list, std::uint8_t a3, std::uint8_t type,
                   std::uint8_t cols, std::uint8_t kind, std::int32_t& x, std::int32_t& y, std::uint8_t tplayer,
                   std::uint16_t tid, std::uint8_t ai_owned, bool direct, bool player_split, std::int32_t count) {
    const bool all = player_split && !s.original_rules;
    // Entente Engine Plus: a computer's wave (sim/plus_army.cpp) takes up to 42 guns (MoveClass 10) per group, a third
    // of it, as its back rows; the original's 0x14 makes the rest small groups of guns alone.
    const std::uint32_t q_mc10_max = s.plus_rules && all && ai_owned != 0 ? 0x2a : 0x14;
    if (direct) {
        for (Unit* u : list) TUnit_OrderMove(s, player, *u, static_cast<float>(x), static_cast<float>(y), 1);
        return;
    }
    // The follow-up record (16 bytes): a3, type, cols, kind, x, y, tplayer, (pad), tid, as given.
    std::array<std::uint8_t, 16> follow{};
    follow[0] = a3, follow[1] = type, follow[2] = cols, follow[3] = kind;
    for (int b = 0; b < 4; ++b) {
        follow[4 + b] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(x) >> (8 * b));
        follow[8 + b] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(y) >> (8 * b));
    }
    follow[12] = tplayer, follow[14] = static_cast<std::uint8_t>(tid), follow[15] = static_cast<std::uint8_t>(tid >> 8);
    if (a3 == 4) {
        a3 = AI_StagingProbe(s, player, list, x, y,
                             static_cast<std::uint16_t>(count < 0 ? list.size() : static_cast<std::size_t>(count)));
    }
    while (!list.empty()) {
        const Unit& first = *list[0];
        std::vector<Unit*> take, rest;
        std::uint32_t n = 0;
        int q_mc4 = 0, q_mc10 = 0, q_air = 0;
        for (Unit* u : list) {
            const UnitType& t = Unit_Type(s, player, *u);
            const double dy = static_cast<double>(first.y) - u->y, dx = static_cast<double>(first.x) - u->x;
            bool ok = !(std::sqrt(dx * dx + dy * dy) >= static_cast<double>(2000.0f)) && n <= 0x7d;
            if (ok && !take.empty()) {
                ok = false;
                if (n < 0xa0) {
                    if (t.is_air == 0) {
                        if (t.move_class == 4 ? (n == static_cast<std::uint32_t>(q_mc4) && q_mc4 < 0x1e) : q_mc4 == 0) {
                            ok = t.move_class != 10 || static_cast<std::uint32_t>(q_mc10) < q_mc10_max;
                        }
                    } else {
                        ok = q_air != 0;
                    }
                }
            }
            if (!ok) {
                rest.push_back(u);  // both lists are prepended to, reversed below (a prepend each is quadratic, B65)
                continue;
            }
            take.push_back(u);
            if (t.field_006 == 0) {
                if (t.move_class == 4) {
                    ++q_mc4;
                } else if (t.move_class == 10) {
                    ++q_mc10;
                } else if (t.is_air != 0) {
                    ++q_air;
                }
            }
            ++n;
        }
        std::reverse(take.begin(), take.end());
        std::reverse(rest.begin(), rest.end());
        Player& pl = s.players[player];
        Group g;
        g.serial = pl.next_group++;
        ++pl.ai_next_serial;
        g.player = player;
        pl.groups.push_back(g);
        Group& ng = pl.groups.back();
        if (all) {
            std::stable_sort(take.begin(), take.end(), [&](const Unit* a, const Unit* b) {
                return FrontRank(Unit_Type(s, player, *a)) < FrontRank(Unit_Type(s, player, *b));
            });
        }
        Group_Create(s, ng, take, a3, type, cols, a3 == 4 ? 1 : kind, x, y, tplayer, tid, ai_owned, all);
        if (a3 == 4) ng.follow_up.push_back(follow);




        if (!all) break;
        list.assign(rest.rbegin(), rest.rend());
    }
}



void Group_DeferredCreate(GameState& s, std::uint8_t player, const CommandNode& n) {
    Group* g = nullptr;
    for (Group& k : s.players[player].groups) {
        if (k.serial == n.group) g = &k;
    }
    if (g == nullptr) return;  // (the original keeps a queued group alive until this runs)
    if (g->dead != 0) {
        g->queued = 0;
        return;
    }
    if (g->queued == 0) return;
    std::vector<Unit*> list;
    for (const UnitRef& r : n.list) {
        Unit* u = Unit_Find(s, r);
        if (u == nullptr) {
            // a unit freed while the command waited (the original reads freed memory)
            Unported(s, orig::kGroupDeferredCreate);
            continue;
        }
        list.push_back(u);
    }
    Group_Create(s, *g, list, n.a3, n.type, n.cols, n.kind, n.px, n.py, n.tplayer, n.tid, g->ai_owned);
}



void Group_DeferredOrder(GameState& s, std::uint8_t player, const CommandNode& n) {
    Group* g = nullptr;
    for (Group& k : s.players[player].groups) {
        if (k.serial == n.group) g = &k;
    }
    if (g == nullptr) return;  // (the original keeps a queued group alive until this runs)
    if (g->dead != 0) {
        g->queued = 0;
        return;
    }
    Group_Order(s, *g, n.kind, n.px, n.py, n.tplayer, n.tid);
}

Group* Unit_Group(GameState& s, std::uint8_t player, const Unit& u) {
    if (!u.in_group || u.group == 0) return nullptr;
    for (Group& g : s.players[player].groups) {
        if (g.serial == u.group) return &g;
    }
    return nullptr;
}





void UI_FacingOrder(GameState& s, const Command& c) {
    if (c.args.size() != 3) return;
    const auto player = static_cast<std::uint8_t>(c.args[0].raw & 0xff);
    const auto dir = static_cast<std::uint8_t>(c.args[1].raw & 0xff);
    float len = std::bit_cast<float>(c.args[2].raw);
    if (player >= s.players.size() || dir > 7) return;
    if (len > 2000.0f) len = 2000.0f;
    std::vector<std::uint32_t> done;  // the groups that have it already
    for (const UnitRef& r : c.units) {
        Unit* u = r.player == player ? Unit_Find(s, r) : nullptr;
        if (u == nullptr) continue;
        if (u->in_group) {
            Group* g = Unit_Group(s, player, *u);
            if (g == nullptr || std::find(done.begin(), done.end(), g->serial) != done.end()) continue;
            done.push_back(g->serial);
            g->arrange_orient = dir;
            g->spacing_override = len;
            if (g->wp_count == 0) Group_Arrange(s, *g, true);
            continue;
        }
        u->field_178 = dir;
        if ((u->num_work == 3 || u->num_work == 4) && dir != static_cast<std::uint8_t>(u->orient)) {
            u->target_orient = dir;
            if (Unit_Type(s, player, *u).anim_size[5] != 0) {
                u->num_work = 5;
                u->num_sprite = 0;
            } else {
                u->orient = dir;
            }
        }
    }
}


void Group_RemoveMember(GameState& s, Group& g, Unit& u) {
    std::size_t i = u.group_index;
    const UnitRef r = Unit_Ref(g.player, u);
    const auto same = [&](std::size_t k) {
        return g.members[k].player == r.player && g.members[k].id == r.id && g.members[k].serial == r.serial;
    };
    if (i < g.members.size() && same(i)) {
        u.group_index = 0xffff;
    } else {
        for (i = 0; i < g.members.size() && !same(i); ++i) {
        }
        if (i == g.members.size()) return;
    }
    SetGroup(u, 0, 0);
    g.members[i] = kNoMember;
    --g.live;
    g.quorum = static_cast<std::uint16_t>(
        Ftol(static_cast<double>(g.live) * static_cast<double>(g.ai_owned != 0 ? 0.3f : 0.5f)));
    if (g.half > 1) --g.half;
    if (i == g.leader) Group_PickLeader(s, g);
    Group_Recalc(s, g);
    if (g.live != 0) {
        Group_UpdateFlags(s, g);
        return;
    }
    g.dead = 1;
    g.arg3 = 0;
}



void AI_UpdateGroups(GameState& s, std::uint8_t player) {
    Player& pl = s.players[player];
    for (std::size_t k = 0; k < pl.groups.size();) {
        Group& g = pl.groups[k];
        if (s.players[g.player].is_lost != 0) g.dead = 1;
        if (g.dead == 0 || g.queued != 0) {
            Group_Update(s, g);
            ++k;
            continue;
        }
        Group_Dismiss(s, g, false);
        Group_Clear(s, g);
        pl.groups.erase(pl.groups.begin() + static_cast<std::ptrdiff_t>(k));
    }
}



void UI_GroupCreate(GameState& s, std::uint8_t player, std::span<const UnitRef> selection) {
    std::vector<Unit*> list;
    for (const UnitRef& sel : selection) {
        Unit* u = Unit_Find(s, UnitRef{player, sel.id, sel.serial});
        if (u == nullptr || Unit_Type(s, player, *u).field_009 == 0) continue;
        if (u->in_group) {
            const Group* g = Unit_Group(s, player, *u);
            if (g == nullptr) {
                Unported(s, orig::kUiGroupCreate);  // a group loaded from a save
                continue;
            }
            if (g->ai_owned == 0) continue;
        }
        list.push_back(u);  // reversed below: a prepend per unit is quadratic (docs/bugs.md B65)
    }
    if (list.empty()) return;
    std::reverse(list.begin(), list.end());
    std::int32_t x = 0, y = 0;
    AI_FormGroups(s, player, std::move(list), 1, 2, 1, 0, x, y, 0, 0, 0, false, true);
}



void Group_Order(GameState& s, Group& g, std::uint8_t kind, std::int32_t x, std::int32_t y, std::uint8_t tplayer,
                 std::uint16_t tid, bool now) {
    if (!g.slots.empty() && g.large == 0) {
        const double dx = static_cast<double>(static_cast<float>(g.slots[0][0])) - static_cast<float>(x);
        const double dy = static_cast<double>(static_cast<float>(g.slots[0][1])) - static_cast<float>(y);
        const double d = std::sqrt(dx * dx + dy * dy);
        if ((d < static_cast<double>(1500.0f) && (kind == 2 || kind == 3)) || d < static_cast<double>(600.0f)) {
            g.tx = x, g.ty = y, g.kind = kind, g.target_player = tplayer, g.target_id = tid;
            Group_PathDone(s, g);
            return;
        }
    } else if (g.large != 0) {
        Unported(s, orig::kGroupOrder);  // large-unit groups
    }

    if (s.formation_budget >= 1 && !now) {
        CommandNode n;
        n.seq = s.next_node_seq++;
        n.cls = 1, n.cmd = 0x2f;
        n.group = g.serial;
        n.kind = kind, n.px = x, n.py = y, n.tplayer = tplayer, n.tid = tid;
        s.players[g.player].commands.push_back(std::move(n));
        g.queued = 1;
        return;
    }
    g.queued = 0;
    if (!now) ++s.formation_budget;
    g.kind = kind, g.tx = x, g.ty = y, g.target_player = tplayer, g.target_id = tid;
    if (g.members.empty() || g.live == 0) {
        g.dead = 1;
        return;
    }
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    float best = 999999.0f;
    for (std::size_t i = 0; i < g.members.size(); ++i) {
        const Unit* u = Member(s, g, i);
        if (u == nullptr) continue;
        const double dx = static_cast<double>(u->x) - fx, dy = static_cast<double>(u->y) - fy;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d < static_cast<double>(best)) {
            g.slots[0] = {Ftol(u->x), Ftol(u->y)};
            best = static_cast<float>(d);
        }
    }
    Group_PlanPath(s, g);
    if (g.dead == 0 && g.wp_next < g.wp_count) Group_Step(s, g, false);
}



void Order_Stop(GameState& s, std::uint8_t player, std::span<const UnitRef> selection) {
    for (const UnitRef& sel : selection) {
        Unit* u = Unit_Find(s, UnitRef{player, sel.id, sel.serial});
        if (u == nullptr) continue;
        if (!u->in_group) {
            TUnit_OrderEnd(s, player, *u, 1);
            continue;
        }
        Group* g = Unit_Group(s, player, *u);
        if (g == nullptr) {
            Unported(s, orig::kGroupStop);
            continue;
        }
        Group_PathDone(s, *g);
    }
}


void Order_GroupDismiss(GameState& s, std::uint8_t player, std::span<const UnitRef> selection) {
    for (const UnitRef& sel : selection) {
        Unit* u = Unit_Find(s, UnitRef{player, sel.id, sel.serial});
        if (u == nullptr || !u->in_group) continue;
        Group* g = Unit_Group(s, player, *u);
        if (g == nullptr) {
            Unported(s, orig::kOrderGroupDismiss);
            continue;
        }
        g->kind = 0;
        g->dead = 1;
    }
}

}  // namespace antanta::sim
