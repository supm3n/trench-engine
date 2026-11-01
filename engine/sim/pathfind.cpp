// Pathfinding for land units (docs/movement.md 4): greedy walk, shared path cache, Lee wave search, target
// relocation and the free-spot test at arrival.
#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

namespace {

constexpr std::size_t kCacheMax = 80;
constexpr std::uint32_t kCacheAge = 160;
constexpr int kCacheScoreLimit = 10;

std::uint16_t& Grid(std::vector<std::uint16_t>& g, const GameState& s, Cell c) {
    return g[static_cast<std::size_t>(c.y * s.map.width + c.x)];
}

bool InMap(const GameState& s, Cell c) { return c.x >= 0 && c.y >= 0 && c.x < s.map.width && c.y < s.map.height; }

std::uint16_t PassLand(const GameState& s, Cell c) {
    if (!InMap(s, c)) return 0xffff;  // the original reads outside the grid here; never reached on these maps
    return s.map.pass_land[static_cast<std::size_t>(c.y * s.map.width + c.x)];
}


int PathCache_Score(const PathRecord& r, Cell st, Cell tg) {
    if (st.x == r.sx && st.y == r.sy && r.ey == tg.y && r.ex == tg.x) return 0;
    if (std::abs(st.x - r.sx) >= 2 || std::abs(st.y - r.sy) >= 4) return 10000;
    auto sign = [](std::int32_t v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); };
    if (st.x == r.sx) {
        if (st.y != r.sy && sign(r.ey - r.sy) != sign(tg.y - st.y)) return 10000;
    } else if (sign(r.ex - r.sx) != sign(tg.x - st.x)) {
        return 10000;
    }
    return std::abs(tg.y - r.ey) + std::abs(tg.x - r.ex) * 2;
}


int PathCache_Find(const GameState& s, Cell st, Cell tg) {
    int best = -1, limit = kCacheScoreLimit + 1;
    for (std::size_t i = 0; i < s.path_cache.size(); ++i) {
        const int sc = PathCache_Score(s.path_cache[i], st, tg);
        if (sc < limit) {
            best = static_cast<int>(i), limit = sc;
            if (sc == 0) break;
        }
    }
    return best;
}


bool PathCache_Splice(const GameState& s, Cell from, Cell to, std::vector<std::uint8_t>& path, int mode) {
    std::vector<std::uint8_t> steps;
    Cell c = from;
    for (std::uint8_t d = GreedyStep(c, to); d != 0; d = GreedyStep(c, to)) {
        if (PassLand(s, c) != 0) return false;
        steps.push_back(d);
    }
    if (steps.empty()) return true;
    if (mode == 1) {
        path.insert(path.end(), steps.begin(), steps.end());
    } else {
        steps.insert(steps.end(), path.begin(), path.end());
        path = std::move(steps);
    }
    return true;
}



void PathCache_Store(GameState& s, Cell st, Cell tg, const std::vector<std::uint8_t>& path) {
    for (const PathRecord& r : s.path_cache) {
        if (r.sx != st.x || r.sy != st.y) break;
        if (r.ex == tg.x && r.ey == tg.y) return;
    }
    if (s.path_cache.size() >= kCacheMax) s.path_cache.erase(s.path_cache.begin(), s.path_cache.begin() + 5);
    s.path_cache.push_back(PathRecord{st.x, st.y, tg.x, tg.y, s.tick, path});
}




int PathSearch_Fail(GameState& s, Unit& u, Cell target, std::vector<std::uint16_t>& grid) {
    bool any = false;
    for (std::uint8_t d = 1; d <= 8 && !any; ++d) {
        const Cell n = Cell_Step(target, d);
        any = InMap(s, n) && grid[static_cast<std::size_t>(n.y * s.map.width + n.x)] != 0xffff;
    }
    if (!any) Grid(grid, s, target) = 0xffff;
    ++u.field_145;
    return 0;
}


bool CellAcceptable(GameState& s, Cell& c, const Unit* self, std::uint8_t self_player) {
    const std::int32_t w = s.map.width, h = s.map.height;
    if (c.x < 4 || c.y < 10 || c.x >= w - 3 || c.y >= h - 9) {
        if (c.x < 4) c.x = 4;
        if (c.y < 10) c.y = 10;
        if (c.x >= w - 3) c.x = w - 4;
        if (c.y >= h - 9) c.y = h - 10;
        return false;
    }
    if (self != nullptr && Unit_Type(s, self_player, *self).field_468 != 0) {
        if (s.map.pass_large[static_cast<std::size_t>(c.y * w + c.x)] != 0) return false;  // large: g_PassLarge only
    } else if (PassLand(s, c) != 0) {
        return false;
    }
    const float px = static_cast<float>((c.x * 2 - (~c.y & 1)) * 48 + 28);
    const float py = static_cast<float>(c.y * 24);
    const float rs = self != nullptr ? static_cast<float>(Unit_Type(s, self_player, *self).size + 10) : 0.0f;
    for (std::uint8_t d = 0; d <= 8; ++d) {
        const Cell n = Cell_Step(c, d);
        if (!InMap(s, n)) continue;
        for (const UnitRef& r : s.map.cell_units[static_cast<std::size_t>(n.y * w + n.x)]) {
            const Unit* o = Unit_Find(s, r);
            if (o == nullptr) continue;
            const float so = static_cast<float>(Unit_Type(s, r.player, *o).size);
            if (o->is_deleted == 0 && std::abs(static_cast<double>(o->x) - px) < so &&
                std::abs(static_cast<double>(o->y) - py) < so) {
                return false;
            }
            if (self != nullptr) {
                const float rr = so < rs ? rs : so;
                if (std::abs(static_cast<double>(px) - o->x) < rr && std::abs(static_cast<double>(py) - o->y) < rr) {
                    return false;
                }
            }
        }
    }
    return true;
}

// One step of the relocation walks: d = (RngTable[RngIndex++] & 7) + 1.
Cell RandomStep(GameState& s, Cell c) { return Cell_Step(c, static_cast<std::uint8_t>((Rng_Next(s) & 7) + 1)); }


bool PointInFootprint(const GameState& s, float x, float y, std::uint8_t player, const Unit& o) {
    const double size = Unit_Type(s, player, o).size;
    return std::abs(static_cast<double>(o.x) - x) < size && std::abs(static_cast<double>(o.y) - y) < size;
}

}  // namespace


bool Cell_Acceptable(GameState& s, Cell& c) { return CellAcceptable(s, c, nullptr, 0); }


bool CellInside(const GameState& s, Cell c) {
    return c.x > 3 && c.y > 9 && c.x < s.map.width - 3 && c.y < s.map.height - 9;
}


bool TUnit_CanStepDir(GameState& s, std::uint8_t player, const Unit& u, std::uint8_t dir) {
    const Cell n = Cell_Step(Cell{u.map_x, u.map_y}, dir);
    if (!CellInside(s, n)) return false;
    const auto idx = static_cast<std::size_t>(n.y * s.map.width + n.x);
    if (Unit_Type(s, player, u).field_468 != 0) return Cell_LargeOpen(s, idx);
    return PassLand(s, n) == 0;
}


std::uint8_t GreedyStep(Cell& c, Cell to) {
    std::uint8_t d = 0;
    const bool even = (c.y & 1) == 0;
    if (c.x == to.x) {
        if (c.y == to.y) return 0;
        if (to.y > c.y) {
            d = to.y == c.y + 1 ? (even ? 2 : 3) : 6;
        } else {
            d = to.y == c.y - 1 ? (even ? 1 : 4) : 5;
        }
    } else if (c.y == to.y) {
        d = to.x < c.x ? 8 : 7;
    } else if (to.x < c.x) {
        d = to.y < c.y ? 4 : 3;
    } else {
        d = to.y > c.y ? 2 : 1;
    }
    c = Cell_Step(c, d);
    return d;
}


Cell RelocateTargetLand(GameState& s, Cell c) {
    Cell w = RandomStep(s, c);
    if (CellAcceptable(s, w, nullptr, 0)) return w;
    for (int tries = 1;; ++tries) {
        w = RandomStep(s, w);
        if (tries > 20000) return c;
        if (CellAcceptable(s, w, nullptr, 0)) return w;
    }
}


Cell RelocateTargetLarge(GameState& s, Cell c, std::uint8_t player, const Unit& u) {
    Cell w = RandomStep(s, c);
    if (CellAcceptable(s, w, &u, player)) return w;
    for (int tries = 1;; ++tries) {
        w = RandomStep(s, w);
        if (tries >= 10001 || CellAcceptable(s, w, &u, player)) return w;
    }
}


bool TUnit_IsSpotFree(GameState& s, std::uint8_t player, const Unit& u) {
    const UnitType& t = Unit_Type(s, player, u);
    if (t.is_ship != 0 && Cell_Obstacle(s.map, u.map_x, u.map_y)) return true;  // ships: water under the cell
    const Cell c{u.map_x, u.map_y};
    // Large units: a large-open own cell is free at once; otherwise the land test below runs.
    if (t.field_468 != 0 && Cell_LargeOpen(s, static_cast<std::size_t>(c.y * s.map.width + c.x))) return true;
    if (u.in_group) return PassLand(s, c) == 0;
    if (PassLand(s, c) != 0) return false;
    for (std::uint8_t d = 0; d <= 8; ++d) {
        const Cell n = Cell_Step(c, d);
        if (!InMap(s, n)) continue;
        for (const UnitRef& r : s.map.cell_units[static_cast<std::size_t>(n.y * s.map.width + n.x)]) {
            const Unit* o = Unit_Find(s, r);
            if (o == nullptr || o == &u || o->is_deleted != 0) continue;
            if (PointInFootprint(s, u.x, u.y, r.player, *o) || PointInFootprint(s, o->x, o->y, player, u)) {
                return false;
            }
        }
    }
    return true;
}



int PathSearch(GameState& s, std::uint8_t player, Unit& u, Cell target) {
    // Large units skip A and B and the cache store, search g_PassLarge and open the flag-2 cells by the start.
    const bool large = Unit_Type(s, player, u).field_468 != 0;
    std::vector<std::uint16_t>& pass = large ? s.map.pass_large : s.map.pass_land;
    // A. Close to Final with 4 free samples on the line.
    if (!large) {
        const double dx = static_cast<double>(u.final_x) - u.x, dy = static_cast<double>(u.final_y) - u.y;
        if (std::sqrt(dx * dx + dy * dy) < 120.0) {
            int i = 0;
            for (; i < 4; ++i) {
                const float px = static_cast<float>(dx * i * 0.25 + u.x);
                const float py = static_cast<float>(dy * i * 0.25 + u.y);
                if (PassLand(s, WorldToCell(px, py)) != 0) break;
            }
            if (i == 4) return -1;
        }
    }
    const Cell start{u.map_x, u.map_y};
    // B. Path cache.
    const int hit = large ? -1 : PathCache_Find(s, start, target);
    if (hit >= 0) {
        const PathRecord rec = s.path_cache[static_cast<std::size_t>(hit)];
        std::vector<std::uint8_t> path = rec.path;
        if (PathCache_Splice(s, start, Cell{rec.sx, rec.sy}, path, 0) &&
            PathCache_Splice(s, Cell{rec.ex, rec.ey}, target, path, 1)) {
            u.path = std::move(path);
            u.path_index = 0;
            s.path_budget_used += 2;
            return 1;
        }
    }
    // C. Lee wave. A short request first runs a windowed wave that falls back to the full one (with its budget
    // charge) as soon as it labels a cell away from the start; its output is the same as the full wave's.
    const std::int32_t w = s.map.width;
    const std::int32_t window = w / 15;
    const std::int32_t adx = std::abs(target.x - start.x), ady = std::abs(target.y - start.y);
    bool windowed = adx + ady / 2 < window;
    const std::int32_t far_mark = window - 1;
    for (;;) {
        if (!windowed) s.path_budget_used += ady / 2 + adx;
        std::vector<std::uint16_t>& g = s.map.wave;
        g = pass;
        if (large) PathSearch_LargeStart(s, u);
        Grid(g, s, start) = 1;
        Grid(g, s, target) = 0xfffe;
        std::uint16_t t = 1;
        std::int32_t reach = 0;
        std::vector<Cell> cur{start}, next;
        bool found = false, fallback = false;
        while (!found) {
            if (cur.empty()) return PathSearch_Fail(s, u, target, pass);
            next.clear();
            for (const Cell c : cur) {
                if (Grid(g, s, c) != t) {
                    next.push_back(c);
                    continue;
                }
                for (std::uint8_t d = 1; d <= 8; ++d) {
                    const Cell n = Cell_Step(c, d);
                    if (n.x == target.x && n.y == target.y) {
                        Grid(g, s, n) = static_cast<std::uint16_t>(t + 1);
                        next.assign(1, n);
                        found = true;
                        break;
                    }
                    std::uint16_t& v = Grid(g, s, n);
                    if (v == 0) {
                        v = static_cast<std::uint16_t>(t + kStepCost[d]);
                        next.push_back(n);
                        if (windowed) {
                            const std::int32_t dist = std::max(std::abs(start.x - n.x), std::abs(start.y - n.y) / 2);
                            if (reach < dist) reach = far_mark;
                        }
                    }
                }
                if (found) break;
            }
            ++t;
            if (t > 8000) {
                next.clear();
                found = true;
            }
            if (windowed && far_mark <= reach && !found) {
                fallback = true;
                break;
            }
            if (found && next.empty()) return PathSearch_Fail(s, u, target, pass);
            cur.swap(next);
        }
        if (fallback) {
            windowed = false;
            continue;
        }
        t = static_cast<std::uint16_t>(Grid(g, s, target) - 1);
        if (t == 0) return -1;
        // Backtrack from the target to labels <= t, directions 8 down to 1.
        std::vector<std::uint8_t> buf;
        Cell c = target;
        do {
            std::uint8_t best = 0;
            for (std::uint8_t d = 8; d >= 1; --d) {
                const std::uint16_t v = Grid(g, s, Cell_Step(c, d));
                if (v != 0 && v <= t) t = v, best = d;
            }
            if (best != 0) {
                c = Cell_Step(c, best);
                buf.push_back(best);
                if (buf.size() > 4999) return PathSearch_Fail(s, u, target, pass);
            }
            --t;
        } while (t != 0);
        if (!buf.empty()) {
            std::vector<std::uint8_t> path(buf.size());
            for (std::size_t j = 0; j < buf.size(); ++j) path[j] = kOpposite[buf[buf.size() - 1 - j]];
            u.path = path;
            u.path_index = 0;
            if (!large) PathCache_Store(s, start, target, path);
        }
        return 1;
    }
}



int PathFind(GameState& s, std::uint8_t player, Unit& u, Cell target, const FlowResult* flow,
             std::vector<std::uint8_t>* flow_dirs) {
    if (!InMap(s, target)) return 0;

    const bool open = Unit_Type(s, player, u).field_468 != 0
                          ? Cell_LargeOpen(s, static_cast<std::size_t>(target.y * s.map.width + target.x))
                          : PassLand(s, target) == 0;
    if (open && CellInside(s, target)) {
        if (!u.path.empty()) {
            u.path.clear();
            u.path_index = 0;
            u.vel_x = 0, u.vel_y = 0;
        }
        if (u.map_x == target.x && u.map_y == target.y) return -1;
        if (flow != nullptr && *flow == FlowResult::Unreachable) return PathSearch_Fail(s, u, target, s.map.pass_land);
        if (flow != nullptr && *flow == FlowResult::Path) {
            u.path = std::move(*flow_dirs);
            u.path_index = 0;
            return 1;
        }
        return PathSearch(s, player, u, target);
    }
    ++u.field_145;
    return 0;
}

// Our rules (docs/bugs.md B88): a long path request of a small land unit is served by the shared route field.
bool Flow_Usable(const GameState& s, std::uint8_t player, const Unit& u) {
    return !s.original_rules && Unit_Type(s, player, u).field_468 == 0;
}


int TUnit_RequestPath(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t flag) {
    if (TUnit_Speed(s, player, u, orig::kRequestPath) < 0.2) {
        ++u.field_145;
        return 0;
    }
    const Cell c = WorldToCell(x, y);
    if (!InMap(s, c)) {
        ++u.field_145;
        return 0;
    }
    const bool long_request = flag != 0 && s.map.width / 15 <= std::abs(c.y - u.map_y) / 2 + std::abs(c.x - u.map_x);
    // Our rules: a long request of a small unit takes the shared route field at once, without the budget; only when
    // the field has no answer does the original's budgeted search follow (docs/bugs.md B88).
    FlowResult flow = FlowResult::Unknown;
    std::vector<std::uint8_t> flow_dirs;
    if (long_request && Flow_Usable(s, player, u) && PassLand(s, c) == 0 && CellInside(s, c)) {
        flow = Flow_Path(s, Cell{u.map_x, u.map_y}, c, false, flow_dirs);
    }
    if (flow == FlowResult::Unknown && long_request) {
        if (s.path_budget_used >= s.path_budget_max) {
            // Cmd_New(1, 0x0d, x, y), pushed to the owner's queue (docs/movement.md 3.2).
            u.field_107 = 1;
            CommandNode n;
            n.seq = s.next_node_seq++;
            n.cls = 1, n.cmd = 0x0d, n.x = x, n.y = y;
            n.unit = Unit_Ref(player, u);
            u.field_160 = n.seq;
            u.field_060 = Ftol(x), u.field_064 = Ftol(y);
            s.players[player].commands.push_back(n);
            return 2;
        }
        s.path_budget_used += 10;
    }
    u.final_x = x, u.final_y = y;
    u.field_107 = 0;
    u.dest_x = u.x, u.dest_y = u.y;
    const int r = PathFind(s, player, u, c, flow == FlowResult::Unknown ? nullptr : &flow, &flow_dirs);
    if (r == -1) {
        u.dest_x = u.final_x, u.dest_y = u.final_y;
        TUnit_SetMoveAnimation(s, player, u);
        TUnit_ComputeVelocity(s, player, u);
        u.field_078 = u.num_work;
        TUnit_SetOrientToDest(s, player, u);
        if (u.target_orient == u.orient) u.field_078 = 100;
        u.field_09d = 0, u.is_stopped = 0;
        return -1;
    }
    if (r == 1 && !u.path.empty()) {
        u.field_09d = 0, u.is_stopped = 0;
        if (TUnit_CanStepDir(s, player, u, u.path[0])) {
            u.field_145 = 0;
            u.dest_x = static_cast<float>(kDirWorld[u.path[0]][0]) + u.x;
            u.dest_y = static_cast<float>(kDirWorld[u.path[0]][1]) + u.y;
            TUnit_SetMoveAnimation(s, player, u);
            TUnit_ComputeVelocity(s, player, u);
            u.path_index = 1;
            u.field_078 = u.num_work;
            TUnit_SetOrientToDest(s, player, u);
            if (u.target_orient == u.orient || u.field_078 == u.num_work) u.field_078 = 100;
        } else {
            TUnit_OrderEnd(s, player, u, 1);
        }
    }
    return r;
}



void PathHelper_TickRefresh(GameState& s) {
    if (s.tick % 100 != 2) return;
    std::vector<PathRecord> keep;
    for (PathRecord& r : s.path_cache) {
        if (s.tick - r.tick <= kCacheAge) keep.push_back(std::move(r));
    }
    s.path_cache = std::move(keep);
}

}  // namespace antanta::sim
