// The event blocks of a .mis mission (sim/mission_events.hpp, docs/missions.md 3).
#include "mission_events.hpp"

#include <cmath>
#include <map>

#include "control_group.hpp"
#include "endgame.hpp"
#include "formats/campaign.hpp"
#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

namespace {

std::string Utf8(std::u16string_view t) {
    std::string out;
    for (std::size_t i = 0; i < t.size(); ++i) {
        std::uint32_t c = t[i];
        if (c >= 0xd800 && c < 0xdc00 && i + 1 < t.size() && t[i + 1] >= 0xdc00 && t[i + 1] < 0xe000) {
            c = 0x10000 + ((c - 0xd800) << 10) + (t[++i] - 0xdc00);
        }
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xc0 | c >> 6), out += static_cast<char>(0x80 | (c & 0x3f));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xe0 | c >> 12), out += static_cast<char>(0x80 | (c >> 6 & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | c >> 18), out += static_cast<char>(0x80 | (c >> 12 & 0x3f));
            out += static_cast<char>(0x80 | (c >> 6 & 0x3f)), out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

// The text table key of a block's text from the mission path ("Data\Campaigns\missions\Karp.mis": folder
// "missions", stem "Karp"); empty without a path.
std::string BlockKey(const GameState& s, std::uint8_t p, std::size_t k, std::uint8_t which) {
    const std::string& path = s.mission_path;
    const std::size_t slash = path.find_last_of("\\/");
    if (path.empty() || slash == std::string::npos) return {};
    const std::size_t dot = path.find_last_of('.');
    const std::string stem = path.substr(slash + 1, dot != std::string::npos && dot > slash ? dot - slash - 1 : dot);
    const std::size_t prev = slash == 0 ? std::string::npos : path.find_last_of("\\/", slash - 1);
    const std::string folder =
        path.substr(prev == std::string::npos ? 0 : prev + 1, prev == std::string::npos ? slash : slash - prev - 1);
    static constexpr const char* kWhich[3] = {"B", "C", "UC"};
    return mission_block_key(folder, stem, p, static_cast<unsigned>(k), kWhich[which % 3]);
}




std::uint16_t UnitCountAt(const GameState& s, std::uint32_t i) {
    return i / 40 < s.players.size() ? s.players[i / 40].unit_count[i % 40] : 0;
}
std::uint8_t HousesDoneAt(const GameState& s, std::uint32_t i) {
    return i / 25 < s.players.size() ? s.players[i / 25].houses_done[i % 25] : 0;
}


bool NoLivingEnemy(const GameState& s, std::uint8_t p) {
    if (s.players[p].is_lost != 0) return false;
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        if (q != p && s.players[q].is_lost == 0 && IsEnemy(s, p, static_cast<std::uint8_t>(q))) return false;
    }
    return true;
}


bool AllOthersLost(const GameState& s, std::uint8_t p) {
    if (s.players[p].is_lost != 0) return false;
    int lost = 0;
    for (const Player& o : s.players) lost += o.is_lost != 0 ? 1 : 0;
    return static_cast<int>(s.players.size()) - 1 <= lost;
}





bool UnitInArea(const GameState& s, std::uint8_t p, float x, float y, float rx, float ry) {
    const std::uint32_t cx = static_cast<std::uint32_t>(Ftol(x)) & 0xffff;
    const std::uint32_t cy = static_cast<std::uint32_t>(Ftol(y)) & 0xffff;
    const std::int32_t r = Ftol(rx);
    std::int32_t rr = Ftol(ry);
    if (static_cast<std::int16_t>(rr) == -1) rr = r;
    const std::uint32_t w = s.map.width;
    const std::uint32_t cells = w * s.map.height;
    const std::int32_t jmax = static_cast<std::int32_t>(static_cast<std::int16_t>(rr)) * 2;
    for (auto i = static_cast<std::int16_t>(-r); i <= static_cast<std::int16_t>(r); ++i) {
        for (auto j = static_cast<std::int16_t>(-rr * 2); j <= jmax; ++j) {
            const std::uint32_t idx = (cy - static_cast<std::uint32_t>(static_cast<std::int32_t>(j))) * w -
                                      static_cast<std::uint32_t>(static_cast<std::int32_t>(i)) + cx;
            if (idx >= cells) continue;
            if (!s.original_rules) {
                const std::int64_t col = static_cast<std::int64_t>(cx) - i;
                if (col < 0 || col >= static_cast<std::int64_t>(w)) continue;
            }
            for (const UnitRef& r2 : s.map.cell_units[idx]) {
                if (r2.player == p) return true;
            }
        }
    }
    return false;
}

void Horn(GameState& s, bool win) { Event_Sound(s, 9, win ? 1 : 2, 7); }

// The game's result of a win or lose block. Under our rules the block of an enemy of MyPlayer counts from MyPlayer's
// side: its win is your loss and its loss your win (docs/bugs.md B160). Returns true for a win.
bool BlockWins(const GameState& s, std::uint8_t p, bool win) {
    if (!s.original_rules && p != s.my_player && IsEnemy(s, s.my_player, p)) return !win;
    return win;
}

// The timer starts (countdown = timer when it is 0) and MyPlayer sees the "B" text: a block that became active.
void Activate(GameState& s, std::uint8_t p, std::size_t k) {
    MisBlock& b = s.players[p].blocks[k];
    if (b.timer != 0 && b.countdown == 0) b.countdown = b.timer;
    if (p == s.my_player) Mission_Message(s, p, k, 0, false);
}



using RefMap = std::map<std::uint64_t, UnitRef>;
std::uint64_t RefKey(const UnitRef& r) { return static_cast<std::uint64_t>(r.player) << 48 | r.serial; }
void Remap(const RefMap& m, UnitRef& r) {
    const auto it = m.find(RefKey(r));
    if (it != m.end() && it->second.serial == r.serial) r = it->second;
}
void RemapAll(GameState& s, const RefMap& m) {
    if (m.empty()) return;
    for (Player& pl : s.players) {
        for (Unit& u : pl.unit_slots) {
            Remap(m, u.attack_ref), Remap(m, u.future_transporter), Remap(m, u.transporter), Remap(m, u.mg_target);
            for (UnitRef& r : u.passengers) Remap(m, r);
        }
        for (House& h : pl.houses) {
            for (UnitRef& r : h.inside) Remap(m, r);
            Remap(m, h.field_088), Remap(m, h.field_0b0);
        }
    }
    for (UnitRef& r : s.ships) Remap(m, r);
    for (ShipRec& r : s.map.water.recs) Remap(m, r.unit);
    for (Shell& v : s.shells) Remap(m, v.owner);
    for (Bullet& b : s.bullets) Remap(m, b.owner);
}

}  // namespace

void Mission_Message(GameState& s, std::uint8_t p, std::size_t k, std::uint8_t which, bool end) {
    const MisBlock& b = s.players[p].blocks[k];
    const std::u16string_view t = mis_text(which == 0 ? b.text_b : which == 1 ? b.text_c : b.text_uc);
    if (t.empty()) return;
    SimEvent e;
    e.kind = SimEvent::Kind::MissionMessage;
    e.player = p;
    e.text = Utf8(t);
    e.mission.block = static_cast<std::uint8_t>(k);
    e.mission.which = which;
    e.mission.end = end;
    e.mission.pause = true;
    e.mission.key = BlockKey(s, p, k, which);
    s.events.push_back(std::move(e));
    if (end) {  // the window with the end flag: closing it opens the statistics, the game stays paused
        s.paused = 1;
        s.end_message = 3;
        s.end_text = std::u16string(t);
    }
}

void MissionEvents_Start(GameState& s) {
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        const auto p = static_cast<std::uint8_t>(q);
        Player& pl = s.players[q];
        for (std::size_t k = 0; k < pl.blocks.size(); ++k) {
            const MisBlock& b = pl.blocks[k];
            if (b.done == 0 && b.stage <= pl.field_2cc9a && b.open_prereq == 0) Activate(s, p, k);
        }
    }
}

bool MissionEvents_Update(GameState& s, std::uint8_t p) {
    for (std::size_t k = 0; k < s.players[p].blocks.size(); k = static_cast<std::uint8_t>(k + 1)) {
        Player& pl = s.players[p];
        MisBlock& b = pl.blocks[k];
        if (pl.field_2cc9a < b.stage || b.open_prereq != 0 || b.done != 0) continue;
        bool holds = false;
        switch (b.condition) {
            case 0:  // no living enemy, or (with a timer) the countdown reached 1: "hold out N checks"
                if (b.timer == 0) b.lose = 0, b.win = 0;
                if (pl.is_lost == 0) {
                    holds = b.countdown == 1 || NoLivingEnemy(s, p);
                } else {
                    b.timer = 1, b.countdown = 0;  // fails at once
                }
                break;
            case 1: holds = pl.is_lost == 0 && AllOthersLost(s, p); break;
            case 2: holds = UnitInArea(s, p, b.x, b.y, 3.0f, -1.0f); break;
            case 3: {  // player N lost (a player past NumPlayers: a null pointer in the original, docs/bugs.md B167)
                const auto q = static_cast<std::uint8_t>(b.n);
                holds = q < s.players.size() && s.players[q].is_lost != 0;
                break;
            }
            case 4:
                holds = UnitCountAt(s, static_cast<std::uint32_t>(b.unit_type) +
                                           static_cast<std::uint32_t>(static_cast<std::uint8_t>(b.n)) * 40) == 0;
                break;
            case 5: holds = static_cast<double>(static_cast<std::uint16_t>(b.n)) <= pl.gold; break;
            case 6: holds = b.n <= UnitCountAt(s, b.unit_type + static_cast<std::uint32_t>(p) * 40); break;
            case 7: holds = HousesDoneAt(s, static_cast<std::uint32_t>(p) * 25 + b.house_type) >= b.n; break;
            case 8:
                if (HousesDoneAt(s, static_cast<std::uint32_t>(p) * 25 + b.house_type) == 0) {
                    b.countdown = 0;
                    holds = true;
                } else {
                    holds = b.timer != 0 && b.countdown == 1;
                }
                break;
            case 9: holds = s.flag_8e836e != 0; break;
            case 10:
                holds = HousesDoneAt(
                            s, static_cast<std::uint32_t>(static_cast<std::uint8_t>(b.n)) * 25 + b.house_type) == 0;
                break;
            case 11: holds = HousesDoneAt(s, static_cast<std::uint32_t>(p) * 25 + b.house_type) != 0; break;
            case 12:
                if (UnitCountAt(s, b.unit_type + static_cast<std::uint32_t>(p) * 40) == 0) {
                    b.timer = 1, b.countdown = 0;
                } else {
                    holds = b.timer != 0 && b.countdown == 1;
                }
                break;
            case 13: {
                const auto u = static_cast<std::int16_t>(b.unit_type);
                holds = (u == -1 && s.sel_8ab774 != 0xfffe) || s.sel_8ab774 == static_cast<std::uint16_t>(u);
                break;
            }
            case 14: {
                const auto h = static_cast<std::int16_t>(b.house_type);
                holds = (h == -1 && s.sel_8ab770 != 0xfffe) || s.sel_8ab770 == static_cast<std::uint16_t>(h);
                break;
            }
            case 15: holds = s.flag_8ef4d4 != 0; break;
            case 16: holds = s.flag_8ef4d5 != 0; break;
            case 17: holds = s.flag_8ef4d6 != 0; break;
            case 18: holds = s.flag_8ab825 != 0; break;
            case 19: holds = s.flag_8ab824 != 0; break;
            case 23: holds = pl.units_alive < b.n; break;
            case 24:
                holds = UnitInArea(s, p, b.x, b.y, static_cast<float>(static_cast<std::uint8_t>(b.n)), -1.0f);
                break;
            default: break;  // 20, 21, 22 and above: pure timers
        }
        if (holds) {
            if (MissionEvents_Complete(s, p, k)) return true;
        } else {
            if (b.timer == 0) continue;
            if (b.countdown >= 1) {
                --b.countdown;
                if (b.lose != 0 || b.win != 0) {
                    SimEvent e;
                    e.kind = SimEvent::Kind::MissionTimer;
                    e.player = p;
                    e.mission.block = static_cast<std::uint8_t>(k);
                    e.mission.countdown = b.countdown;
                    e.mission.timer = b.timer;
                    s.events.push_back(std::move(e));
                }
                continue;
            }
            // The timer ran out: a lose block loses with its "UC" text, a win block wins with its "C" text, any
            // other block shows its "UC" text and runs its action.
            if (b.lose != 0) {
                const bool win = BlockWins(s, p, false);
                Mission_Message(s, p, k, 2, true);
                s.game_result = win ? 0 : 1;
                Horn(s, win);
            } else if (b.win != 0) {
                const bool win = BlockWins(s, p, true);
                Mission_Message(s, p, k, 1, true);
                s.game_result = win ? 0 : 1;
                Horn(s, win);
            } else {
                Mission_Message(s, p, k, 2, false);
                MissionEvent_Execute(s, p, s.players[p].blocks[k]);
            }
        }
        MisBlock& d = s.players[p].blocks[k];
        d.done = 1;
        if (d.condition != 0 || d.win != 0 || d.lose != 0) break;  // one block a check, but plain kind-0 blocks go on
    }
    return false;
}

bool MissionEvents_Complete(GameState& s, std::uint8_t p, std::size_t k) {
    {
        const MisBlock& b = s.players[p].blocks[k];
        // the block's dialog (no shipped block)
        if (p == s.my_player && b.dialogs[0].on) Unported(s, orig::kMissionEventDialog);
        const bool end = b.win != 0 || b.lose != 0;
        bool win = false;
        if (end) {
            win = BlockWins(s, p, b.win != 0);
            s.game_result = win ? 0 : 1;
        }
        Mission_Message(s, p, k, 1, end);
        const MisAction& a = s.players[p].blocks[k].action;
        if (a.kind == 14 && static_cast<std::uint32_t>(a.ints[0]) == p) {  // p joins another player and dies
            if (s.players[p].is_lost == 0) MissionEvent_Execute(s, p, s.players[p].blocks[k]);
            return true;
        }
        MissionEvent_Execute(s, p, s.players[p].blocks[k]);
        const MisBlock& c = s.players[p].blocks[k];
        if (c.win != 0 || c.lose != 0) {
            win = BlockWins(s, p, c.win != 0);
            s.game_result = win ? 0 : 1;
            Horn(s, win);
        }
    }
    // The stage: past this block's with "advance", or when this block is the only one of its stage.
    Player& pl = s.players[p];
    const MisBlock& b = pl.blocks[k];
    const std::uint8_t old = pl.field_2cc9a;
    if (b.advance != 0 && old <= b.stage) pl.field_2cc9a = static_cast<std::uint8_t>(b.stage + 1);
    std::uint8_t same = 0;
    for (const MisBlock& o : pl.blocks) same = static_cast<std::uint8_t>(same + (o.stage == b.stage ? 1 : 0));
    if (same == 1) pl.field_2cc9a = static_cast<std::uint8_t>(b.stage + 1);
    if (old < pl.field_2cc9a) {
        for (std::size_t j = 0; j < pl.blocks.size(); ++j) {  // done ones too: the original does not test it
            if (pl.blocks[j].stage == pl.field_2cc9a && pl.blocks[j].open_prereq == 0) Activate(s, p, j);
        }
    }
    // The blocks that wait for this one drop it; one with nothing left and its stage reached becomes active.
    const std::uint8_t id = s.players[p].blocks[k].id;
    for (std::size_t j = 0; j < s.players[p].blocks.size(); ++j) {
        MisBlock& o = s.players[p].blocks[j];
        for (std::uint8_t a = 0; a < o.open_prereq; ++a) {
            if (o.prereq[a] != static_cast<std::uint8_t>(id - 1)) continue;
            for (std::uint8_t c = a; c + 1 < o.open_prereq; ++c) o.prereq[c] = o.prereq[c + 1];
            --o.open_prereq;
            if (o.open_prereq == 0 && o.stage <= s.players[p].field_2cc9a) Activate(s, p, j);
        }
    }
    return false;
}

void MissionEvent_Execute(GameState& s, std::uint8_t p, const MisBlock& block) {
    const MisAction& a = block.action;
    Player& pl = s.players[p];
    switch (a.kind) {
        case 1: {
            Bytes raw(kMisBlockSize, 0);
            write_mis_block(block, raw.data());
            const std::uint32_t at = static_cast<std::uint32_t>(pl.nation) * 25 + a.player;
            if (at >= 6 * 25) {
                Unported(s, orig::kMissionEventExecute);  // a house type past the table
                break;
            }
            std::array<std::uint16_t, 10>& list = s.house_types[at / 25][at % 25].produce;
            for (std::uint32_t i = 0; static_cast<std::int32_t>(i) < a.ints[0]; i = (i + 1) & 0xffff) {
                const std::size_t o = 0xc28 + 0xc + i * 4;
                const std::uint32_t v = o + 4 <= raw.size() ? read_u32(raw, o) : 0;
                std::uint16_t f = 0;
                while (f < 10 && list[f] != 0xffff && list[f] != v) ++f;
                if (f == 10) continue;
                // The original tests entry i instead of entry f for the type already listed (docs/bugs.md B163).
                bool listed = false;
                if (!s.original_rules) {
                    listed = list[f] == v;
                } else if (i < 10) {
                    listed = list[i] == v;
                } else {
                    Unported(s, orig::kMissionEventExecute);  // reads THouseBasic past the list
                }
                if (listed) continue;
                list[f] = static_cast<std::uint16_t>(v);
                if (f != 9) list[f + 1] = 0xffff;
            }
            break;
        }
        case 2:  // add to the stocks (the statistics doubles do not change)
            pl.gold = static_cast<double>(a.ints[0]) + pl.gold, pl.food = static_cast<double>(a.ints[1]) + pl.food;
            pl.wood = static_cast<double>(a.ints[2]) + pl.wood, pl.iron = static_cast<double>(a.ints[3]) + pl.iron;
            pl.oil = static_cast<double>(a.ints[4]) + pl.oil;
            break;
        case 3:
            pl.gold -= static_cast<double>(a.ints[0]), pl.food -= static_cast<double>(a.ints[1]);
            pl.wood -= static_cast<double>(a.ints[2]), pl.iron -= static_cast<double>(a.ints[3]);
            pl.oil -= static_cast<double>(a.ints[4]);
            break;
        case 4: {  // the float amounts; the original adds them to the stock taken as a float (docs/bugs.md B164)
            double* stock[5] = {&pl.gold, &pl.food, &pl.wood, &pl.iron, &pl.oil};
            for (int i = 0; i < 5; ++i) {
                *stock[i] = s.original_rules ? static_cast<double>(a.floats[i] + static_cast<float>(*stock[i]))
                                             : *stock[i] + static_cast<double>(a.floats[i]);
            }
            break;
        }
        case 7: s.fog_553ae0 = 0; break;  // the fog of war off
        case 8: {
            const auto hw = static_cast<std::uint32_t>((a.ints[2] - a.ints[0]) / 2);
            const auto hh = static_cast<std::uint32_t>((a.ints[3] - a.ints[1]) / 2);
            const std::uint32_t r = hw <= hh ? hh : hw;
            const std::int32_t cx = static_cast<std::int32_t>(hw) + a.ints[0];
            const std::int32_t cy = static_cast<std::int32_t>(hh) + a.ints[1];
            Reveal_Add(s, cx, cy, static_cast<std::uint8_t>(r));
            SimEvent e;
            e.kind = SimEvent::Kind::MissionCamera;
            e.player = p;
            e.cell_x = cx, e.cell_y = cy;
            e.mission.centred = true;
            s.events.push_back(std::move(e));
            break;
        }
        case 14: {  // a player joins another (reinforcements)
            const std::int32_t from = a.ints[0];
            if (from >= 0 && static_cast<std::size_t>(from) < s.players.size() && s.players[from].is_lost == 0) {
                Mission_MergePlayer(s, static_cast<std::uint8_t>(from), static_cast<std::uint8_t>(a.player),
                                    a.ints[1] != 0, a.ints[2] != 0, a.ints[3] != 0);
            } else if (from < 0 || static_cast<std::size_t>(from) >= 8) {
                Unported(s, orig::kMissionEventExecute);  // g_Strateg past its 8 entries (docs/bugs.md B167)
            }
            break;
        }
        case 20: {  // the view's top-left cell
            SimEvent e;
            e.kind = SimEvent::Kind::MissionCamera;
            e.player = p;
            e.cell_x = static_cast<std::uint16_t>(a.ints[0]), e.cell_y = static_cast<std::uint16_t>(a.ints[1]);
            s.events.push_back(std::move(e));
            break;
        }
        case 23:  // a player's AI type
            if (a.player < s.players.size() && s.players[a.player].is_lost == 0) {
                Player& o = s.players[a.player];
                o.ai_type = static_cast<std::uint8_t>(a.ints[0]), o.ai_kind = static_cast<std::uint8_t>(a.ints[0]);
            }
            break;
        case 24: {  // the idle units of a player near (f0, f1) walk to the cell (f2, f3)
            const auto q = static_cast<std::uint8_t>(a.player);
            if (q >= s.players.size()) {
                Unported(s, orig::kMissionEventExecute);
                break;
            }
            Player& o = s.players[q];
            std::vector<Unit*> list;
            for (std::uint32_t k = o.first_unit; k != kNoSlot; k = o.unit_slots[k].next) {
                Unit& u = o.unit_slots[k];
                if (u.is_deleted != 0 || u.work != 0xff) continue;
                const float dx = a.floats[0] - static_cast<float>(u.map_x);
                const float dy = a.floats[1] - static_cast<float>(u.map_y);
                if (std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy) <
                    static_cast<double>(a.floats[4])) {
                    list.insert(list.begin(), &u);
                }
            }
            const std::uint32_t ty = static_cast<std::uint32_t>(Ftol(a.floats[3])) & 0xffff;
            const std::uint32_t tx = static_cast<std::uint32_t>(Ftol(a.floats[2])) & 0xffff;
            std::int32_t x = static_cast<std::int32_t>((tx * 2 - (~ty & 1)) * 48);
            std::int32_t y = static_cast<std::int32_t>(ty * 24);
            AI_FormGroups(s, q, std::move(list), 4, 2, 0, 1, x, y, q, 0, 1, true);
            break;
        }
        default: break;  // 0, 5, 6, 9 to 13, 15 to 19, 21, 22: nothing
    }
}

void Mission_MergePlayer(GameState& s, std::uint8_t from, std::uint8_t to, bool houses, bool units, bool resources) {
    if (from >= s.players.size() || to >= s.players.size()) {
        Unported(s, orig::kMergePlayer);  // g_Strateg past NumPlayers (docs/bugs.md B167)
        return;
    }
    Player& src = s.players[from];
    Player& dst = s.players[to];
    if (src.is_lost != 0 || dst.is_lost != 0) return;
    // A unit or house keeps its type record (TBasicUnit, THouseBasic), so across nations it keeps its own nation
    // (Unit::nation, House::nation; Learn2, austria_3_3 and russia_3_3 have such a merge).
    if (resources) {
        dst.gold = src.gold + dst.gold, dst.food = src.food + dst.food, dst.iron = src.iron + dst.iron;
        dst.wood = src.wood + dst.wood, dst.oil = src.oil + dst.oil;
        for (std::size_t i = 0; i < 5; ++i) {
            dst.mined[i] = src.mined[i] + dst.mined[i];
            dst.start_res[i] = dst.start_res[i] + src.start_res[i];
        }
    }
    if (units) SendMyUnitsToPlayer(s, from, to);
    if (houses) SendMyHousesToPlayer(s, from, to, units);
    TStrateg_Die(s, from);
}

void SendMyUnitsToPlayer(GameState& s, std::uint8_t from, std::uint8_t to) {
    if (from == to) return;
    // The list as it is now (the original walks it with Next read first; a group dismissed on the way moves its
    // members to the front, which breaks that walk, docs/bugs.md B166).
    std::vector<std::uint32_t> order;
    for (std::uint32_t k = s.players[from].first_unit; k != kNoSlot; k = s.players[from].unit_slots[k].next) {
        order.push_back(k);
    }
    RefMap moved;
    for (const std::uint32_t k : order) {
        Unit& u = s.players[from].unit_slots[k];
        s.deselected.push_back(Unit_Ref(from, u));
        if (u.in_group) {
            if (Group* g = Unit_Group(s, from, u)) {
                Group_Disband(s, *g, false);
            } else {
                Unported(s, orig::kGroupDismiss);  // a group loaded from a save
            }
        }
        if (u.field_0ea != 0xff) ControlGroup_RemoveUnit(s.players[from], u);
        const float hp = u.hp;
        const UnitRef old = Unit_Ref(from, u);
        Cell_ListRemove(s, from, u);  // SetCell takes it out of its cell list before it lists it again

        if (!s.original_rules && Unit_Type(s, from, u).is_ship != 0) Ship_Deleted(s, from, u);
        Unit copy = u;
        const std::uint8_t own = Unit_Nation(s, from, u);  // TUnit::Basic stays
        copy.nation = own == s.players[to].nation ? 0xff : own;
        Unit& v = AddUnitToPlayer(s, to, std::move(copy));
        v.hp = hp;
        moved[RefKey(old)] = Unit_Ref(to, v);
        EventUnitEndMove(s, to, v);
    }
    RemapAll(s, moved);
    Player& src = s.players[from];
    src.units_alive = 0;
    src.num_units = 0;
    src.first_unit = kNoSlot;
    src.unit_slots.clear();
    src.id_slots.clear(), src.free_slots.clear(), src.freed_slots.clear();
    for (auto& c : s.unit_cells) c.clear();
}

void SendMyHousesToPlayer(GameState& s, std::uint8_t from, std::uint8_t to, bool units_sent) {
    if (from == to) return;
    std::map<std::size_t, std::uint16_t> moved;  // from's slot -> to's slot
    for (std::size_t k = s.players[from].houses.size(); k-- > 0;) {
        Player& src = s.players[from];
        House& h = src.houses[k];
        if (!(h.hp > -10000.0f)) continue;                             // removed ruins stay
        ControlGroup_RemoveHouse(src, static_cast<std::uint16_t>(k));
        const std::uint8_t destroyed = h.is_destroyed;
        const float hp = h.hp;
        const std::uint16_t left = h.field_020;
        // Under our rules a ruin (destroyed, not yet removed) is passed as a ruin: no finished-house count, callback
        // or power on either side (docs/bugs.md B165).
        const bool counts = s.original_rules || destroyed == 0;
        h.field_088 = UnitRef{}, h.field_084 = 0xff;
        h.hp = -1000001.0f;
        h.is_destroyed = 1;
        if (src.nation == 5) {
            Unported(s, orig::kSendHousesToPlayer);
        } else if (counts) {
            if (h.type < 25) --src.houses_done[h.type];
            House_DestroyCallback(s, from, h);
            const HouseType& ht = House_Type(s, from, h);  // THouse::Basic
            src.power_used =
                static_cast<float>(static_cast<double>(src.power_used) - static_cast<double>(ht.power_use));
        }
        House_OnDestroyed(s, from, h, 0xff);  // the removal: the cells, the pass grids, a mine's deposit
        h.hp = hp, h.field_020 = left, h.is_destroyed = destroyed;
        House copy = s.players[from].houses[k];
        const std::uint8_t own = House_Nation(s, from, copy);  // the copy keeps THouse::Basic
        copy.nation = own == s.players[to].nation ? 0xff : own;
        const std::uint16_t slot = AddHouseToPlayer(s, to, std::move(copy));
        Player& dst = s.players[to];
        House& n = dst.houses[slot];
        if (n.field_020 == 0 && counts) {
            if (n.type < 25) ++dst.houses_done[n.type];
            House_BuiltCallback(s, to, n);
            const HouseType& ht = House_Type(s, to, n);
            dst.power_used = static_cast<float>(static_cast<double>(ht.power_use) + dst.power_used);
        }
        if (!units_sent) {
            n.units_inside = 0, n.inside.clear();
            n.has_field_0b0 = false, n.field_0b0 = UnitRef{};
        }
        moved[k] = slot;
        s.players[from].houses[k].units_inside = 0, s.players[from].houses[k].inside.clear();
    }

    for (Player& pl : s.players) {
        for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
            Unit& u = pl.unit_slots[k];
            if (u.in_house && u.house_player == from && moved.count(u.house_slot) != 0) {
                u.house_player = to, u.house_slot = moved[u.house_slot];
            }
            if (u.has_house_attack && u.house_attack_player == from && moved.count(u.house_attack_slot) != 0) {
                u.house_attack_player = to, u.house_attack_slot = moved[u.house_attack_slot];
            }
        }
    }
    for (auto& c : s.house_cells) c.clear();
}

std::uint16_t Reveal_Add(GameState& s, std::int32_t x, std::int32_t y, std::uint8_t radius) {
    for (std::uint16_t i = 0; i < s.reveal.size(); ++i) {
        GameState::Reveal& r = s.reveal[i];
        if (r.radius != 0) continue;
        std::uint8_t v = radius == 0 ? 1 : radius;

        const std::uint8_t max = s.original_rules ? 40 : 39;
        if (v > max) v = max;
        r.x = x, r.y = y, r.radius = v;
        return i;
    }
    return 0xffff;
}

}  // namespace antanta::sim
