// Shared route fields for big orders (our rules only, docs/movement.md 3.4, docs/bugs.md B88). The original searches
// one path per unit and lets only a few long searches run per tick (PathBudgetMax 20, 40 or 100; the rest wait as
// command class 1 cmd 0x0d), so 10,000 units sent across the map wait minutes for their routes. Here the cost of
// the cheapest walk to a block of 16 x 32 cells is computed once for the whole map (Dijkstra with the Lee wave's step
// costs) and kept; every long request whose target lies in that block walks down it on cells that are free now and

// search. The fields leave out the marks of moving vehicles (Cell_LandStatic); when a house, a tree or the ground
// changes, a kept field is repaired around the changed cells. The cheapest costs are unique, so a kept field equals
// one built now and a load (which keeps none) changes nothing. Integer costs, fixed scan orders: the same paths on
// every compiler and target.
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <queue>
#include <utility>

#include "movement.hpp"

namespace antanta::sim {

namespace {

constexpr std::int32_t kBlockW = 16, kBlockH = 32;  // cells per block: a ground square of about 8 x 8 tiles
constexpr std::uint16_t kNone = 0xffff;
constexpr std::int32_t kSplice = 40;             // the greedy splice is tried from this many steps to the target on
constexpr std::size_t kMaxSteps = 5000;          // as the original's backtrack limit (4999)
constexpr std::size_t kCacheCells = 16'000'000;  // the kept fields hold at most this many cells (32 MB)
constexpr std::size_t kLogChanges = 64;          // the grid changes kept for repairs

std::size_t Idx(const Map& m, Cell c) {
    return static_cast<std::size_t>(c.y) * m.width + static_cast<std::size_t>(c.x);
}

bool InMap(const Map& m, Cell c) { return c.x >= 0 && c.y >= 0 && c.x < m.width && c.y < m.height; }

// Free on g_PassLand now (the cells a path may step on).
bool Free(const Map& m, Cell c) { return InMap(m, c) && m.pass_land[Idx(m, c)] == 0; }

// The land grid without the large-unit marks and its weights, smoothed as PathGrid_Build does (in place, the cell
// and its 8 neighbours / 9, over 4 <= y < H - 4, 2 <= x < W - 4), again after Map::pass_gen changed. Only a real change
// of the open cells or of the weights counts (GameState::flow_ver), with its cells logged for the repairs.
void Grid_Update(GameState& s) {
    const Map& m = s.map;
    if (s.flow_grid_gen == m.pass_gen && s.flow_open.size() == m.pass_land.size()) return;
    const std::size_t n = static_cast<std::size_t>(m.width) * m.height;
    std::vector<std::uint8_t> open(n, 0), weight(n, 0);
    for (std::int32_t y = 0; y < m.height; ++y) {
        for (std::int32_t x = 0; x < m.width; ++x) {
            const std::size_t i = Idx(m, Cell{x, y});
            open[i] = Cell_LandStatic(s, x, y, &weight[i]) ? 1 : 0;
        }
    }
    for (std::int32_t y = 4; y < m.height - 4; ++y) {
        for (std::int32_t x = 2; x < m.width - 4; ++x) {
            unsigned sum = 0;
            for (std::uint8_t d = 0; d <= 8; ++d) sum += weight[Idx(m, Cell_Step(Cell{x, y}, d))];
            weight[Idx(m, Cell{x, y})] = static_cast<std::uint8_t>((sum & 0xffff) / 9);
        }
    }
    s.flow_grid_gen = m.pass_gen;
    if (s.flow_open.size() != n) {  // a new map: every field is built again
        s.flow_open.swap(open), s.flow_weight.swap(weight);
        ++s.flow_ver;
        s.flow_log.clear();
        return;
    }
    FlowChange change;
    for (std::size_t i = 0; i < n; ++i) {
        if (open[i] != s.flow_open[i] || weight[i] != s.flow_weight[i])
            change.cells.push_back(static_cast<std::int32_t>(i));
    }
    if (change.cells.empty()) return;
    s.flow_open.swap(open), s.flow_weight.swap(weight);
    change.ver = ++s.flow_ver;
    if (s.flow_log.size() >= kLogChanges) s.flow_log.erase(s.flow_log.begin());
    s.flow_log.push_back(std::move(change));
}

// The field is the one a build now would give.
bool Current(const GameState& s, const FlowField& f) { return f.ver == s.flow_ver; }

bool Open(const GameState& s, Cell c) { return InMap(s.map, c) && s.flow_open[Idx(s.map, c)] != 0; }

// The cost of the step into n in direction d.
std::uint32_t StepInto(const GameState& s, std::uint8_t d, Cell n, bool weighted) {
    return kStepCost[d] + (weighted ? s.flow_weight[Idx(s.map, n)] : 0u);
}

// Dijkstra from every open cell of the block, with a ring of buckets (a step costs 9 to 16 + 255).
void Build(const GameState& s, FlowField& f) {
    const Map& m = s.map;
    const std::int32_t w = m.width;
    f.dist.assign(static_cast<std::size_t>(w) * m.height, kNone);
    // Open cells keep 3 cells off every edge (Cell_LandStatic), so their neighbours are on the map: flat offsets.
    std::int32_t off[2][9];
    for (std::uint8_t d = 0; d <= 8; ++d) {
        off[0][d] = kDirCellEven[d][1] * w + kDirCellEven[d][0];
        off[1][d] = kDirCellOdd[d][1] * w + kDirCellOdd[d][0];
    }
    const std::uint8_t* open = s.flow_open.data();
    const std::uint8_t* weight = s.flow_weight.data();
    std::uint16_t* dist = f.dist.data();
    constexpr std::size_t kRing = 512;  // a step costs 9 to 16 + 255
    std::vector<std::vector<std::int32_t>> ring(kRing);
    std::size_t pending = 0;
    for (std::int32_t y = f.by * kBlockH; y < std::min<std::int32_t>((f.by + 1) * kBlockH, m.height); ++y) {
        for (std::int32_t x = f.bx * kBlockW; x < std::min<std::int32_t>((f.bx + 1) * kBlockW, w); ++x) {
            if (open[y * w + x] == 0) continue;
            dist[y * w + x] = 0;
            ring[0].push_back(y * w + x);
            ++pending;
        }
    }
    for (std::uint32_t v = 0; pending != 0; ++v) {
        std::vector<std::int32_t>& b = ring[v % kRing];
        for (std::size_t k = 0; k < b.size(); ++k) {  // a step costs at least 9: nothing is added to b meanwhile
            const std::int32_t i = b[k];
            --pending;
            if (dist[i] != v) continue;  // a cheaper label came first
            const std::int32_t* o = off[(i / w) & 1];
            // A neighbour p steps to this cell in the opposite direction, at the same step cost, entering this cell.
            const std::uint32_t enter = v + (f.weighted ? weight[i] : 0u);
            for (std::uint8_t d = 1; d <= 8; ++d) {
                const std::int32_t p = i + o[d];
                if (open[p] == 0) continue;
                const std::uint32_t nv = enter + kStepCost[d];
                if (nv >= kNone || nv >= dist[p]) continue;
                dist[p] = static_cast<std::uint16_t>(nv);
                ring[nv % kRing].push_back(p);
                ++pending;
            }
        }
        b.clear();
    }
}

// Brings f up to date after the grid cells `changed` (sorted, unique) changed, with the same result as a build: the
// cheapest costs are unique. First the cells whose cost may have risen, in rising order of their old cost: a cell
// keeps its cost while a neighbour not risen still gives it exactly; the others lose it, and so may their
// neighbours. Then those cells and the changed ones take the cheapest cost their neighbours give, and the costs
// spread from them as in a build.
void Repair(const GameState& s, FlowField& f, const std::vector<std::int32_t>& changed) {
    const Map& m = s.map;
    const std::int32_t w = m.width;
    std::uint16_t* dist = f.dist.data();
    const auto source = [&](std::int32_t i) {
        const std::int32_t x = i % w, y = i / w;
        return s.flow_open[i] != 0 && x / kBlockW == f.bx && y / kBlockH == f.by;
    };
    // The neighbour of cell i in direction d, or -1 off the map.
    const auto step = [&](std::int32_t i, std::uint8_t d) {
        const Cell n = Cell_Step(Cell{i % w, i / w}, d);
        return InMap(m, n) ? static_cast<std::int32_t>(Idx(m, n)) : -1;
    };
    // The cost to the block through neighbour j (entered from i in direction d), or kNone.
    const auto via = [&](std::int32_t j, std::uint8_t d) -> std::uint32_t {
        if (j < 0 || s.flow_open[j] == 0 || dist[j] == kNone) return kNone;
        return dist[j] + (f.weighted ? s.flow_weight[j] : 0u) + kStepCost[d];
    };
    using Item = std::pair<std::uint32_t, std::int32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> q;
    std::vector<std::uint8_t> state(f.dist.size(), 0);  // 1: looked at, 2: its cost rose
    const auto look = [&](std::int32_t i) {
        if (i >= 0 && state[i] == 0 && dist[i] != kNone) state[i] = 1, q.emplace(dist[i], i);
    };
    for (const std::int32_t c : changed) {
        look(c);
        for (std::uint8_t d = 1; d <= 8; ++d) look(step(c, d));
    }
    std::vector<std::int32_t> risen;
    while (!q.empty()) {
        const auto [v, i] = q.top();
        q.pop();
        bool kept = false;
        if (s.flow_open[i] != 0) {
            if (source(i)) {
                kept = true;  // 0 or lower now: the second pass sets it
            } else {
                for (std::uint8_t d = 1; d <= 8 && !kept; ++d) {
                    const std::int32_t j = step(i, d);
                    kept = j >= 0 && state[j] != 2 && via(j, d) == v;
                }
            }
        }
        if (kept) continue;
        state[i] = 2;
        risen.push_back(i);
        for (std::uint8_t d = 1; d <= 8; ++d) look(step(i, d));
    }
    for (const std::int32_t i : risen) dist[i] = kNone;
    for (const std::vector<std::int32_t>* list : {static_cast<const std::vector<std::int32_t>*>(&risen), &changed}) {
        for (const std::int32_t i : *list) {
            if (s.flow_open[i] == 0) {
                dist[i] = kNone;
                continue;
            }
            std::uint32_t best = source(i) ? 0u : static_cast<std::uint32_t>(dist[i]);
            for (std::uint8_t d = 1; d <= 8; ++d) best = std::min(best, via(step(i, d), d));
            dist[i] = static_cast<std::uint16_t>(best);
            if (best != kNone) q.emplace(best, i);
        }
    }
    while (!q.empty()) {
        const auto [v, i] = q.top();
        q.pop();
        if (dist[i] != v) continue;
        const std::uint32_t enter = v + (f.weighted ? s.flow_weight[i] : 0u);
        for (std::uint8_t d = 1; d <= 8; ++d) {
            const std::int32_t p = step(i, d);
            if (p < 0 || s.flow_open[p] == 0) continue;
            const std::uint32_t nv = enter + kStepCost[d];
            if (nv >= kNone || nv >= dist[p]) continue;
            dist[p] = static_cast<std::uint16_t>(nv);
            q.emplace(nv, p);
        }
    }
}

// Up to date: repaired from the logged changes since it was, or built again when they are too many or gone.
void Update(GameState& s, FlowField& f) {
    std::vector<std::int32_t> cells;
    bool whole = s.flow_log.empty() || s.flow_log.front().ver > f.ver + 1;
    for (const FlowChange& c : s.flow_log) {
        if (whole) break;
        if (c.ver > f.ver) cells.insert(cells.end(), c.cells.begin(), c.cells.end());
        if (cells.size() > f.dist.size() / 16) whole = true;
    }
    if (whole) {
        Build(s, f);
    } else {
        std::sort(cells.begin(), cells.end());
        cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
        Repair(s, f, cells);
    }
    f.ver = s.flow_ver;
}

const FlowField& Field(GameState& s, Cell goal, bool weighted) {
    const Map& m = s.map;
    const std::int32_t bx = goal.x / kBlockW, by = goal.y / kBlockH;
    ++s.flow_clock;
    for (FlowField& f : s.flow_fields) {
        if (f.bx != bx || f.by != by || f.weighted != weighted) continue;
        if (!Current(s, f)) Update(s, f);
        f.used = s.flow_clock;
        return f;
    }
    const std::size_t cells = std::max<std::size_t>(1, static_cast<std::size_t>(m.width) * m.height);
    const std::size_t cap = std::clamp<std::size_t>(kCacheCells / cells, 4, 512);
    if (s.flow_fields.size() >= cap) {
        const auto old = std::min_element(s.flow_fields.begin(), s.flow_fields.end(),
                                          [](const FlowField& a, const FlowField& b) { return a.used < b.used; });
        s.flow_fields.erase(old);
    }
    FlowField f;
    f.bx = bx, f.by = by, f.weighted = weighted, f.used = s.flow_clock;
    f.ver = s.flow_ver;
    Build(s, f);
    s.flow_fields.push_back(std::move(f));
    return s.flow_fields.back();
}

// The squared distance between the cells' world points, for the tie-break toward the target.
std::int64_t Dist2(Cell a, Cell b) {
    const std::int64_t dx = static_cast<std::int64_t>(a.x * 2 - (~a.y & 1)) * 48 - (b.x * 2 - (~b.y & 1)) * 48;
    const std::int64_t dy = static_cast<std::int64_t>(a.y - b.y) * 24;
    return dx * dx + dy * dy;
}


bool Splice(const Map& m, Cell c, Cell goal, std::vector<std::uint8_t>& dirs) {
    if (std::max(std::abs(goal.x - c.x), (std::abs(goal.y - c.y) + 1) / 2) > kSplice) return false;
    const std::size_t base = dirs.size();
    for (std::uint8_t d = GreedyStep(c, goal); d != 0; d = GreedyStep(c, goal)) {
        if (!Free(m, c) || dirs.size() - base >= static_cast<std::size_t>(kSplice)) {
            dirs.resize(base);
            return false;
        }
        dirs.push_back(d);
    }
    return true;
}

// A breadth-first search over free cells from c to goal inside the box round both, widened by 8 columns and 16 rows.
// The box gets a border of 1 column and 2 rows (a step's reach) marked as seen, so the searches need no bounds checks;
// their buffers are kept between calls. The cells are taken first in, first out: the order of a search ring by ring.
// First a short search from the goal: every step has its opposite, so the free cells reachable from the goal are the
// ones from which the goal is reachable. A goal walled in with fewer than kPocket cells (in a crowd most failed
// requests are such pockets, a few cells big) has no path, seen from its small side; the result is the same.
bool Local(const Map& m, Cell c, Cell goal, std::vector<std::uint8_t>& dirs) {
    if (c.x == goal.x && c.y == goal.y) return true;
    const std::int32_t x0 = std::max(0, std::min(c.x, goal.x) - 8);
    const std::int32_t x1 = std::min<std::int32_t>(m.width, std::max(c.x, goal.x) + 9);
    const std::int32_t y0 = std::max(0, std::min(c.y, goal.y) - 16);
    const std::int32_t y1 = std::min<std::int32_t>(m.height, std::max(c.y, goal.y) + 17);
    const std::int32_t pw = x1 - x0 + 2, ph = y1 - y0 + 4;  // with the border
    constexpr std::uint8_t kUnseen = 0xff, kBorder = 0xfe;
    constexpr std::size_t kPocket = 256;
    struct Node {
        std::int32_t li, mi, odd;  // index in the box, index in the map, row parity
    };
    static thread_local std::vector<std::uint8_t> from;  // the step into each cell of the box, kUnseen or kBorder
    static thread_local std::vector<Node> queue;
    const auto clear = [&]() {
        from.assign(static_cast<std::size_t>(pw) * static_cast<std::size_t>(ph), kUnseen);
        std::uint8_t* b = from.data();
        for (std::int32_t x = 0; x < pw; ++x) b[x] = b[pw + x] = b[(ph - 2) * pw + x] = b[(ph - 1) * pw + x] = kBorder;
        for (std::int32_t y = 2; y < ph - 2; ++y) b[y * pw] = b[y * pw + pw - 1] = kBorder;
        return b;
    };
    const auto local = [&](Cell p) { return (p.y - y0 + 2) * pw + (p.x - x0 + 1); };
    std::int32_t loff[2][9], moff[2][9], flip[9];
    for (std::uint8_t d = 0; d <= 8; ++d) {
        loff[0][d] = kDirCellEven[d][1] * pw + kDirCellEven[d][0];
        loff[1][d] = kDirCellOdd[d][1] * pw + kDirCellOdd[d][0];
        moff[0][d] = kDirCellEven[d][1] * m.width + kDirCellEven[d][0];
        moff[1][d] = kDirCellOdd[d][1] * m.width + kDirCellOdd[d][0];
        flip[d] = kDirCellEven[d][1] & 1;  // the row step is the same for both parities
    }
    const std::int32_t start_li = local(c), goal_li = local(goal);
    const std::uint16_t* pass = m.pass_land.data();
    // From the goal (a free cell): c itself need not be free.
    std::uint8_t* seen = clear();
    queue.clear();
    queue.push_back(Node{goal_li, static_cast<std::int32_t>(Idx(m, goal)), goal.y & 1});
    seen[goal_li] = 0;
    bool found = false;
    std::size_t head = 0;
    for (; !found && head < queue.size() && head < kPocket; ++head) {
        const Node q = queue[head];
        for (std::uint8_t d = 1; d <= 8; ++d) {
            const std::int32_t li = q.li + loff[q.odd][d];
            if (seen[li] != kUnseen) continue;
            if (li == start_li) {
                found = true;
                break;
            }
            const std::int32_t mi = q.mi + moff[q.odd][d];
            if (pass[mi] != 0) continue;
            seen[li] = d;
            queue.push_back(Node{li, mi, q.odd ^ flip[d]});
        }
    }
    if (!found && head == queue.size()) return false;  // the goal's pocket holds no way to c
    // From c: the path.
    seen = clear();
    queue.clear();
    queue.push_back(Node{start_li, static_cast<std::int32_t>(Idx(m, c)), c.y & 1});
    seen[start_li] = 0;
    found = false;
    for (head = 0; !found && head < queue.size(); ++head) {
        const Node q = queue[head];
        for (std::uint8_t d = 1; d <= 8; ++d) {
            const std::int32_t li = q.li + loff[q.odd][d];
            if (seen[li] != kUnseen) continue;
            const std::int32_t mi = q.mi + moff[q.odd][d];
            if (pass[mi] != 0) continue;
            seen[li] = d;
            queue.push_back(Node{li, mi, q.odd ^ flip[d]});
            if (li == goal_li) {
                found = true;
                break;
            }
        }
    }
    if (!found) return false;
    std::vector<std::uint8_t> back;
    for (Cell q = goal; seen[local(q)] != 0; q = Cell_Step(q, kOpposite[seen[local(q)]]))
        back.push_back(seen[local(q)]);
    dirs.insert(dirs.end(), back.rbegin(), back.rend());
    return true;
}

}  // namespace

FlowResult Flow_Path(GameState& s, Cell start, Cell goal, bool weighted, std::vector<std::uint8_t>& dirs) {
    const Map& m = s.map;
    dirs.clear();
    if (!Free(m, goal) || !InMap(m, start) || (start.x == goal.x && start.y == goal.y)) return FlowResult::Unknown;
    Grid_Update(s);
    const FlowField& f = Field(s, goal, weighted);
    const std::size_t n = f.dist.size();
    if (s.flow_stamp.size() != n) s.flow_stamp.assign(n, 0);
    if (++s.flow_stamp_id == 0) {
        std::fill(s.flow_stamp.begin(), s.flow_stamp.end(), 0);
        s.flow_stamp_id = 1;
    }
    if (f.dist[Idx(m, start)] == kNone) {  // the field proves there is no walk (or the start stands on a closed cell)
        bool any = false, open = Open(s, start);
        for (std::uint8_t d = 1; d <= 8; ++d) {
            const Cell q = Cell_Step(start, d);
            if (InMap(m, q) && f.dist[Idx(m, q)] != kNone) any = true;
            open = open || Open(s, q);
        }
        if (!any) return open ? FlowResult::Unreachable : FlowResult::Unknown;
    }
    Cell c = start;
    s.flow_stamp[Idx(m, c)] = s.flow_stamp_id;
    std::size_t next_splice = 0;  // a failed splice is tried again 8 steps on
    while (dirs.size() < kMaxSteps) {
        if (dirs.size() >= next_splice) {
            const std::size_t at = dirs.size();
            if (Splice(m, c, goal, dirs)) return FlowResult::Path;
            if (std::max(std::abs(goal.x - c.x), (std::abs(goal.y - c.y) + 1) / 2) <= kSplice) next_splice = at + 8;
        }
        if (f.dist[Idx(m, c)] == 0) break;  // in the target's block: the rest by a small search
        // Down the field on cells free now and not seen yet: the cheapest, then the one nearest the target, then the
        // lowest direction.
        std::uint8_t bd = 0;
        std::uint32_t bv = 0;
        std::int64_t bd2 = 0;
        for (std::uint8_t d = 1; d <= 8; ++d) {
            const Cell q = Cell_Step(c, d);
            if (!Free(m, q)) continue;
            const std::size_t qi = Idx(m, q);
            if (f.dist[qi] == kNone || s.flow_stamp[qi] == s.flow_stamp_id) continue;
            const std::uint32_t v = f.dist[qi] + StepInto(s, d, q, weighted);
            if (bd != 0 && v > bv) continue;
            const std::int64_t d2 = Dist2(q, goal);
            if (bd == 0 || v < bv || d2 < bd2) bd = d, bv = v, bd2 = d2;
        }
        if (bd == 0) break;  // walled in by vehicles or other marks: the original's search
        dirs.push_back(bd);
        c = Cell_Step(c, bd);
        s.flow_stamp[Idx(m, c)] = s.flow_stamp_id;
    }
    if (dirs.size() < kMaxSteps && f.dist[Idx(m, c)] == 0 && Local(m, c, goal, dirs) && dirs.size() < kMaxSteps) {
        return FlowResult::Path;
    }
    dirs.clear();
    return FlowResult::Unknown;
}

bool Flow_LocalSearch(const Map& m, Cell c, Cell goal, std::vector<std::uint8_t>& dirs) {
    return Local(m, c, goal, dirs);
}

bool Flow_Check(const GameState& s) {
    if (s.flow_grid_gen != s.map.pass_gen) return true;  // the next use looks at the map again
    for (std::int32_t y = 0; y < s.map.height; ++y) {
        for (std::int32_t x = 0; x < s.map.width; ++x) {
            std::uint8_t w = 0;
            if (Cell_LandStatic(s, x, y, &w) != (s.flow_open[Idx(s.map, Cell{x, y})] != 0)) return false;
        }
    }
    for (const FlowField& f : s.flow_fields) {
        if (!Current(s, f)) continue;
        FlowField fresh = f;
        Build(s, fresh);
        if (fresh.dist != f.dist) return false;
    }
    return true;
}

}  // namespace antanta::sim
