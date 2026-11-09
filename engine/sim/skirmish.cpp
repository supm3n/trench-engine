




#include "skirmish.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dmath.hpp"
#include "mapgen.hpp"
#include "movement.hpp"

namespace antanta::sim {

namespace {

// The setup block of RandomMap_Setup (its stack, 0x57c bytes): per map position after the shuffle.
struct Block {
    std::array<std::uint8_t, 8> nation{}, colour{}, team{}, level{};
};






void Script_CoastPoints(GameState& s, std::uint8_t p, bool land_map) {
    AIScript& sc = *s.players[p].script;
    if (land_map && s.players[p].ai_kind != 5) return;
    const std::int32_t bx = sc.base_x, by = sc.base_y;
    sc.items.assign(20, 0);
    auto put = [&sc](std::size_t k, std::int32_t x, std::int32_t y) {
        if (k >= sc.items.size()) sc.items.resize(k + 1, 0);  // past the 20 slots: a write past the end there
        sc.items[k] = static_cast<std::uint32_t>(x) | static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) << 32;
    };
    std::size_t kept = 0;
    float a = 0.0f;
    do {
        const std::int32_t dx = Ftol(dmath::cos(static_cast<double>(a)) * static_cast<double>(100.0f));
        const std::int32_t dy = Ftol(dmath::sin(static_cast<double>(a)) * static_cast<double>(100.0f));
        std::int32_t x = (bx * 2 - (~by & 1)) * 48 + 48, y = by * 24;
        for (;;) {
            put(kept, x, y);
            x += dx, y += dy;
            const Cell c = WorldToCell(static_cast<float>(x), static_cast<float>(y));
            if (c.x <= 3 || c.y <= 9 || c.x >= s.map.width - 3 || c.y >= s.map.height - 9) break;
            if (Obstacle_UnderCell(s.map, c.x, c.y)) {
                put(kept++, x, y);
                break;
            }
        }
        a = a + 0.31415927f;
    } while (a < 6.2831855f);
    sc.items.resize(kept);
}





void AI_InitBases(GameState& s, bool land_map) {
    for (std::size_t pi = 0; pi < s.players.size(); ++pi) {
        const auto p = static_cast<std::uint8_t>(pi);
        Player& pl = s.players[p];
        auto set = [&](std::int32_t x, std::int32_t y) {
            if (!pl.script || pl.script->base_x != 99999) return;
            AIScript& sc = *pl.script;
            sc.field_6f4 = x, sc.field_6f8 = y, sc.base_x = x, sc.base_y = y;
            Script_CoastPoints(s, p, land_map);
        };
        bool done = false;
        for (const House& h : pl.houses) {
            if (h.type == 6) {
                set(h.map_x, h.map_y);
                done = true;
                break;
            }
        }
        if (done) continue;
        std::int32_t x = 0x14, y = 0x28;
        double sx = 0, sy = 0;
        std::uint16_t n = 0;
        for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
            const Unit& u = pl.unit_slots[k];
            if (Unit_Type(s, p, u).move_class == 6) ++n, sx += static_cast<double>(u.x), sy += static_cast<double>(u.y);
        }
        if (n != 0) {
            const float inv = static_cast<float>(1.0 / static_cast<double>(n));
            const Cell c = PickCell(s.map, static_cast<float>(static_cast<double>(inv) * sx),
                                    static_cast<float>(static_cast<double>(inv) * sy));
            x = c.x, y = c.y;
        } else if (!pl.houses.empty()) {
            set(pl.houses[0].map_x, pl.houses[0].map_y);
        }
        set(x, y);
    }
}



// build state and HP = MaxHP come from the record and the type, a port gets its water side from the record and its

// type] (the player index, not the nation: the original's slip) to the power used, runs its built callback (table

// B5): a position whose house array was freed (a closed slot) gets no houses; the original copied them into the

void Map_PlaceHouses(GameState& s, const MapData& map) {
    for (const MapHouse& mh : map.houses) {
        if (mh.player < 0 || static_cast<std::size_t>(mh.player) >= s.players.size()) continue;
        const auto p = static_cast<std::uint8_t>(mh.player);
        Player& pl = s.players[p];
        if (pl.house_array_freed != 0) continue;
        std::int32_t x = mh.cell_x, y = mh.cell_y;
        if (x >= 0 && y >= 0 && x < s.map.width && y < s.map.height) {
            y = y * 24 - s.map.heights[static_cast<std::size_t>(mh.cell_y) * s.map.width + mh.cell_x];
            x = (mh.cell_x * 2 - (~mh.cell_y & 1)) * 48;
        }
        const std::uint16_t slot = House_Place(s, p, static_cast<float>(x), static_cast<float>(y), mh.type);
        House& h = pl.houses[slot];
        const HouseType& ht = s.house_types[pl.nation % 6][h.type % 25];                    // THouse.Basic
        h.graphic_phase = 1, h.graphic_stage = 0, h.depth_phase = 0xff, h.depth_stage = 0;
        h.field_020 = mh.build_state;
        h.hp = static_cast<float>(ht.max_hp);
        if (h.type == 0x16) {
            h.field_0ac = mh.port_side;
            Port_SetDoor(s, pl.nation, h);
        }
        if (h.field_020 != 0) {
            House_SetGraphic(h, 1, House_BuildStage(ht, h.field_020));
            continue;
        }
        House_SetGraphic(h, 0, 0);  // CreateHouse leaves IsDestroyed 0
        if (p < 6) {
            const float use = s.house_types[p][h.type % 25].power_use;
            pl.power_used = static_cast<float>(static_cast<double>(use) + pl.power_used);
        } else {
            // g_HouseBasic[p * 25 + type] is past the 150 records for players 6 and 7 (Learn2): the original adds
            // whatever lies there. Deliberately different in both modes, as the start runs before the rules are
            // chosen (docs/bugs.md B261, like B5): the house's own nation record.
            pl.power_used = static_cast<float>(static_cast<double>(ht.power_use) + pl.power_used);
        }
        House_BuiltCallback(s, p, h);
        if (h.type < 25) ++pl.houses_done[h.type];
    }
}

// A UTF-8 text as UTF-16 (the map's player names, from UTF-16 by the reader).
std::u16string utf16(const std::string& in) {
    std::u16string out;
    for (std::size_t i = 0; i < in.size();) {
        const auto c = static_cast<std::uint8_t>(in[i]);
        const std::size_t n = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        std::uint32_t cp = n == 1 ? c : c & (0xff >> (n + 1));
        for (std::size_t k = 1; k < n && i + k < in.size(); ++k) cp = cp << 6 | (in[i + k] & 0x3f);
        i += n;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<char16_t>(0xd800 + (cp >> 10)), out += static_cast<char16_t>(0xdc00 + (cp & 0x3ff));
        } else {
            out += static_cast<char16_t>(cp);
        }
    }
    return out;
}

}  // namespace

void SkirmishSetup_CompNames(SkirmishSetup* setup, const std::map<std::string, std::string>& strings) {
    for (std::size_t i = 0; i < setup->comp_names.size(); ++i) {
        auto it = strings.find("IDS_COMP" + std::to_string(i + 1));
        setup->comp_names[i] = it != strings.end() ? utf16(it->second) : std::u16string();
    }
}

namespace {

// Generated maps (docs/map-generator-plan.md 4.4; the owner took the recommendation, to confirm): teammates on
// neighbouring positions, which the generator numbers around its ring. After the shuffle's draws (the same draws as
// always), the slots are regrouped in the order of their drawn positions, each team's members together where its
// first member was drawn. Teams 1..8 count; without two slots of one team nothing changes. On an Islands map
// (decision 53) the teams go largest first (a player without a team counts as a team of one, ties in drawn order), as
// the generator numbers its islands largest first and the setup screen writes the team sizes into the code.
void TeamsSideBySide(std::array<std::uint8_t, 8>& perm, std::size_t count, const std::array<std::uint8_t, 8>& team,
                     bool by_size) {
    auto real = [&team](std::size_t i) { return team[i] >= 1 && team[i] <= 8; };
    bool any = false;
    for (std::size_t i = 0; i < count; ++i) {
        for (std::size_t j = 0; j < i; ++j) any = any || (real(i) && team[i] == team[j]);
    }
    if (!any) return;
    std::array<std::uint8_t, 8> by_pos{};
    for (std::size_t i = 0; i < count; ++i) by_pos[perm[i]] = static_cast<std::uint8_t>(i);
    std::array<bool, 8> done{};
    std::vector<std::vector<std::uint8_t>> groups;  // the slots of each team, in the order of their drawn positions
    for (std::size_t p = 0; p < count; ++p) {
        const std::uint8_t i = by_pos[p];
        if (done[i]) continue;
        done[i] = true;
        groups.push_back({i});
        if (!real(i)) continue;
        for (std::size_t q = p + 1; q < count; ++q) {
            const std::uint8_t j = by_pos[q];
            if (!done[j] && team[j] == team[i]) done[j] = true, groups.back().push_back(j);
        }
    }
    if (by_size) {
        std::stable_sort(groups.begin(), groups.end(),
                         [](const auto& a, const auto& b) { return a.size() > b.size(); });
    }
    std::uint8_t next = 0;
    for (const auto& gr : groups) {
        for (std::uint8_t i : gr) perm[i] = next++;
    }
}

}  // namespace

std::array<std::uint8_t, 8> SkirmishSetup_SlotPlayers(const SkirmishSetup& setup, std::size_t positions) {
    std::array<std::uint8_t, 8> out;
    out.fill(0xff);
    if (positions == 0 || positions > 8) return out;
    // As GameState_NewSkirmish: the open slots compacted in slot order, at most the map's positions.
    const std::size_t players = std::min<std::size_t>(setup.players, positions);
    std::array<std::uint8_t, 8> game_slot{};
    std::size_t open = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        if (i < players && setup.slots[i].level != 0) game_slot[open++] = static_cast<std::uint8_t>(i);
    }
    // The shuffle draws from the table of the second fill (RNG_FillTable with the seed, index 0; the Random nations
    // drew from the first one): the CRT generator from the seed, the table's 65,536 entries over again after its end.
    std::uint32_t hold = setup.seed;
    std::uint32_t drawn = 0;
    auto next = [&]() {
        if (drawn++ == 0x10000) hold = setup.seed, drawn = 1;
        hold = hold * 214013u + 2531011u;
        return static_cast<std::uint16_t>(hold >> 16 & 0x7fff);
    };
    std::array<std::uint8_t, 8> perm{};
    for (std::size_t i = 0; i < positions; ++i) {
        for (;;) {
            perm[i] = static_cast<std::uint8_t>(next() % positions);
            bool used = false;
            for (std::size_t k = 0; k < i; ++k) used = used || perm[k] == perm[i];
            if (!used) break;
        }
    }
    if (const auto gen = MapGen_ParseCode(setup.map, nullptr)) {
        std::array<std::uint8_t, 8> team;
        team.fill(0xff);
        for (std::size_t i = 0; i < open; ++i) team[i] = setup.slots[game_slot[i]].team;
        TeamsSideBySide(perm, positions, team, gen->islands);
    }
    for (std::size_t i = 0; i < open; ++i) out[game_slot[i]] = perm[i];
    return out;
}

bool NewGame_CheckMap(const MapData& map, std::string* error) {
    if (map.width < 8 || map.height < 20 || map.width > 0xffff / 2 || map.height > 0xffff) {
        *error = "map size out of range";
        return false;
    }
    const std::size_t positions = map.player_nations.size();
    if (positions == 0 || positions > 8) {
        *error = "the map has no 1..8 player records";
        return false;
    }
    const std::size_t n = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);
    if (map.cells.size() != n || map.obstacle_grid.size() != n * 2) {
        *error = "map cells or obstacle grid do not match the map size";
        return false;
    }
    return true;
}

void NewGame_InitGame(GameState& s, const GameData& data, std::uint32_t seed) {
    s.unit_types = data.unit_types;
    s.house_footprints = data.house_footprints;
    s.house_types = data.house_types;
    s.object_blocking = data.object_blocking;
    s.speed_ms = data.speed_ms;
    s.short_anims = data.short_anims;



    s.rng_seed = seed;
    RNG_FillTable(s);
    s.tick = 0;
    s.fog_553ae0 = 1;
    s.my_player = 0;
}

bool NewGame_LoadMap(GameState& s, const MapData& map, const std::string& name, const std::array<MapPosition, 8>& pos,
                     const std::array<std::int8_t, 8>* teams, const AiTemplate* tpl, std::string* error) {
    const std::size_t positions = map.player_nations.size();
    const std::size_t n = static_cast<std::size_t>(map.width) * static_cast<std::size_t>(map.height);



    s.climate =
        (!name.empty() && (name[0] == 'w' || name[0] == 'W')) || name == "random1r.dat" || name == "random2r.dat" ? 3
                                                                                                                  : 1;
    const auto gen = MapGen_ParseCode(name, nullptr);
    if (gen) s.climate = gen->winter ? 3 : 1;
    bool land_map = true;

    // The cells: an object's high bit sets the cell flag. Every deposit 0x2581..0x2583 is written to the mine site
    // list while it has fewer than 199; one within 6 columns or 15 rows of the edge is removed from the cell and
    // its entry is overwritten by the next (the count is not raised).
    Map& m = s.map;
    s.map_width = m.width = static_cast<std::uint16_t>(map.width);
    s.map_height = m.height = static_cast<std::uint16_t>(map.height);
    m.heights.resize(n), m.objects.resize(n), m.flags.assign(n, 0);
    m.terrain.resize(n);
    m.obstacle.assign(n * 2, 0);
    m.cell_units.assign(n, {});
    m.wave.assign(n, 0);
    for (std::size_t i = 0; i < n; ++i) {
        m.heights[i] = map.cells[i].height;
        m.terrain[i] = TerrainCell{map.cells[i].tile, map.cells[i].shade, map.cells[i].unknown4, map.cells[i].subtile};
        std::uint16_t o = map.cells[i].object;
        if ((o & 0x8000) != 0) o &= 0x7fff, m.flags[i] = 1;
        if (o > 0x2580 && o < 0x2584 && s.mine_site_count < 199) {
            const auto x = static_cast<std::int32_t>(i % map.width), y = static_cast<std::int32_t>(i / map.width);
            s.mine_sites[s.mine_site_count] = {x, y};
            if (x < 6 || x > map.width - 6 || y < 15 || y > map.height - 15) {
                o = 0;
            } else {
                ++s.mine_site_count;
            }
        }
        m.objects[i] = o;
    }



    for (std::size_t p = 0; p < positions; ++p) {
        s.players.emplace_back();
        Player& pl = s.players.back();
        const bool closed = pos[p].closed;
        pl.nation = pos[p].nation;
        if (pl.nation > 4) {
            *error = "player " + std::to_string(p) + " has no nation 0..4";
            return false;
        }
        pl.control_type = !closed && (p == s.my_player || pos[p].human) ? 4 : 1;


        if (pos[p].map_name && !map.player_names[p].empty()) {
            pl.name = utf16(map.player_names[p]);
        } else {
            pl.name = pos[p].name;
        }
        pl.field_20099 = pos[p].field_20099;



        const std::int32_t zw = static_cast<std::int32_t>(static_cast<std::uint32_t>(map.width) * 0x60 / 300) + 1;
        const std::int32_t zh = static_cast<std::int32_t>(static_cast<std::uint32_t>(map.height) * 0x18 / 300) + 1;
        pl.zone_width = static_cast<std::uint16_t>(zw);
        pl.zone_grid.assign(static_cast<std::uint16_t>(zw * zh), 0);
        pl.ai_kind = pl.ai_type, pl.ai_level = 2;
        if (pl.control_type == 4) pl.ai_type = 2, pl.ai_kind = 2;
        AIScript sc;
        if (tpl != nullptr) {
            sc.nsteps = static_cast<std::uint8_t>(tpl->steps.size());
            for (const AiTemplateStep& st : tpl->steps) {
                auto conv = [](const std::vector<AiTemplateEntry>& v) {
                    std::vector<ScriptEntry> out;
                    for (const AiTemplateEntry& e : v) out.push_back(ScriptEntry{e.kind, e.type, e.count});
                    return out;
                };
                sc.main.push_back(conv(st.main));
                sc.list2.push_back(conv(st.list2));
            }
        }
        pl.script = std::move(sc);
        AIScript_Start(s, static_cast<std::uint8_t>(p));
        if (closed) pl.is_lost = 1, pl.house_array_freed = 1;

        static constexpr std::array<std::uint32_t, 8> kColours = {0x3c14dc, 0x32cd32, 0xa82135, 0x10d1ff,
                                                                  0xff901e, 0x8b008b, 0x2fffad, 0x0045ff};
        pl.stat_colour = kColours[pos[p].colour & 7];
        pl.colour = pos[p].colour;
    }
    // With a setup block: SetTeam(team) for each position, then TStrateg_UpdateAllies: allied with every player of
    // the same team, itself included, unless the team is -1 (none).
    if (teams != nullptr) {
        for (std::size_t p = 0; p < positions; ++p) s.players[p].team = (*teams)[p];
        for (std::size_t p = 0; p < positions; ++p) {
            for (std::size_t q = 0; q < positions; ++q) {
                s.players[p].is_ally[q] = (*teams)[p] != -1 && (*teams)[q] == (*teams)[p] ? 1 : 0;
            }
        }
    }

    s.slot_count = static_cast<std::uint8_t>(positions);
    s.map_name = name;
    Map_PlaceHouses(s, map);
    // Our rules (docs/bugs.md B315, the owner's yes 2026-10-22): the mines the map places join the mine site list
    // after the deposits, as the original's own save load lists them (savegame.cpp), so the computer players and your
    // advisor build there again when one is lost. original_rules keeps the running game's list (deposits only).
    if (!s.original_rules) {
        for (const Player& pl : s.players) {
            if (pl.is_lost != 0) continue;
            for (const House& h : pl.houses) {
                if ((h.type == 0xd || h.type == 0xb || h.type == 10) && s.mine_site_count < 255) {
                    s.mine_sites[s.mine_site_count++] = {h.map_x, h.map_y};
                }
            }
        }
    }
    PathGrid_Build(s);
    // The units of every position whose house array was not freed: AddUnit(x, y, p, type, force), then the map's
    // orientation (0 = keep CreateUnit's random one) for units that do not fly.
    for (const MapUnit& mu : map.units) {
        if (mu.player < 0 || static_cast<std::size_t>(mu.player) >= positions) continue;
        const auto p = static_cast<std::uint8_t>(mu.player);
        if (s.players[p].is_lost != 0) continue;
        const std::uint16_t id = AddUnit(s, mu.world_x, mu.world_y, p, mu.type, true);
        if (id == 0xffff) continue;
        Player& pl = s.players[p];
        Unit& u = pl.unit_slots[pl.first_unit];  // AddUnitToPlayer links the new unit at the head
        const UnitType& t = Unit_Type(s, p, u);
        if (t.is_air != 0) continue;
        if (mu.orient_plus1 != 0) TUnit_SetOrient(u, static_cast<std::uint8_t>(mu.orient_plus1 - 1));
        if (t.is_ship != 0) {
            u.field_139 = 0;
            Ship_SetupSprites(s, p, u);
        }
    }




    m.obstacle = map.obstacle_grid;
    {
        std::int32_t blocked = 0;
        for (std::uint16_t v : m.obstacle) blocked += v != 0 && static_cast<std::int16_t>(v) >= 0 ? 1 : 0;
        if (static_cast<std::int32_t>(m.obstacle.size()) / 3 < blocked) land_map = false;
    }
    if (gen) land_map = !gen->sea;
    s.land_map = land_map ? 1 : 0;
    CoarseWater_Init(s);
    for (const MapHouse& h : map.neutral_houses) NeutralHouse_Place(s, h.type, h.cell_x, h.cell_y);
    std::vector<SpecialObject> specials;
    for (const MapSpecial& r : map.specials) specials.push_back(SpecialObject{r.x, r.y, r.kind, r.b, r.f_a, r.f_b});
    SpecialObjects_Load(s, map.special_capacity, specials);
    s.port_sites = map.port_sites;
    for (std::int32_t y = 0; y < map.height; ++y) {
        for (std::int32_t x = 0; x < map.width; ++x) {
            if (Obstacle_UnderCell(m, x, y)) m.heights[static_cast<std::size_t>(y) * map.width + x] = 0x1e;
        }
    }
    PathGrid_Build(s);
    return true;
}

void NewGame_AfterMap(GameState& s, const std::array<bool, 20>& economic, const std::array<bool, 20>& military) {
    AI_InitBases(s, s.land_map != 0);


    Grid_Init(s);
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        if (!Player_IsYou(s, static_cast<std::uint8_t>(p))) continue;  // a network game: every human (multiplayer.md)
        Player& me = s.players[p];
        me.field_9b2b38 = economic[0], me.field_9b2b39 = economic[1], me.field_9b2b3a = economic[2];
        me.field_9b2b3b = economic[3], me.field_9b2b3c = economic[4], me.field_9b2b3d = economic[5];
        for (std::size_t k = 0; k < 4; ++k) me.field_9b2a98[k] = military[k];
    }
}

void Plus_FixUnitData(GameState& s) {
    auto fix = [&s](std::uint16_t type, std::uint16_t typo, std::uint16_t meant) {
        for (auto& per_player : s.unit_types[2][type].attack) {  // nation 2: France
            if (per_player[1] == typo) per_player[1] = meant;
        }
    };
    fix(14, 95, 105);  // the special soldier's rifle attack: the other nations' soldiers have 105 or more
    fix(20, 10, 100);  // the battleship's rifle attack: the other four nations' have 100
}

void Plus_SetUpkeep(GameState& s) {
    for (auto& nation : s.unit_types) {
        // upkeep: Food, Wood, Iron, Gold, Oil, Power per tick
        if (nation[23].present) nation[23].upkeep[2] = 0.1f, nation[23].upkeep[3] = 0.1f;  // A2: was 0
        if (nation[5].present) nation[5].upkeep[2] = 0.02f, nation[5].upkeep[3] = 0.015f;  // E1: the mortar man
        for (std::uint16_t t : {std::uint16_t{21}, std::uint16_t{22}}) {  // E1 (tuned): the howitzer and the cannon
            if (nation[t].present) nation[t].upkeep[3] = 0.2f;
        }
        if (nation[6].present) {  // E1: the machine gunner
            nation[6].upkeep[3] = static_cast<float>(static_cast<double>(nation[6].upkeep[3]) + 0.005);
        }
    }
    for (auto& nation : s.house_types) {
        if (nation[0].present) nation[0].upkeep[4] = 0.25f;  // O1: the power station burns 0.25 oil a tick (was 0.5)
    }
}

void Plus_SetHouseCost(GameState& s) {
    for (auto& nation : s.house_types) {
        for (std::uint16_t t : {std::uint16_t{2}, std::uint16_t{5}}) {  // R1: the two academies, wood, iron, gold
            if (nation[t].present) nation[t].cost[1] = nation[t].cost[2] = nation[t].cost[3] = 3000.0f;
        }
    }
}

std::optional<GameState> GameState_NewSkirmish(const SkirmishSetup& setup, const MapData& map, const GameData& data,
                                               const AiTemplate* tpl, std::string* error) {
    if (!NewGame_CheckMap(map, error)) return std::nullopt;

    if (setup.resources > 2) {
        *error = "start resources must be 0, 1 or 2";
        return std::nullopt;
    }
    if (setup.players < 2 || setup.players > 8) {
        *error = "the player count must be 2..8";
        return std::nullopt;
    }
    const std::size_t positions = map.player_nations.size();
    for (const SkirmishSlot& sl : setup.slots) {
        if (sl.level > 5 || sl.nation > 5 || sl.colour > 7 || sl.team > 8) {
            *error = "a slot's level, nation, colour or team is out of range";
            return std::nullopt;
        }
    }

    GameState s;
    NewGame_InitGame(s, data, setup.seed);
    s.original_rules = setup.original_rules;
    s.plus_rules = setup.plus && !setup.original_rules;  // Entente Engine Plus (docs/your-version.md)
    if (s.plus_rules) Plus_FixUnitData(s), Plus_SetUpkeep(s), Plus_SetHouseCost(s);
    s.net_game = setup.net ? 1 : 0;
    if (setup.net && setup.speed_ms != 0) s.speed_ms = setup.speed_ms, s.short_anims = setup.short_anims;




    const std::size_t players = setup.players < positions ? setup.players : positions;
    std::array<SkirmishSlot, 8> game{};
    std::array<std::uint8_t, 8> game_slot{};  // the screen slot of each open one
    std::size_t open = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        if (i < players && setup.slots[i].level != 0)
            game_slot[open] = static_cast<std::uint8_t>(i), game[open++] = setup.slots[i];
    }
    // A network game: this machine's slot, among the open ones, is the one MyPlayer follows through the shuffle.
    if (setup.net) {
        std::size_t k = 0;
        while (k < open && game_slot[k] != setup.my_slot) ++k;
        if (k == open || (game[k].level != 4 && game[k].level != 5)) {
            *error = "the network game's own slot is not an open human slot";
            return std::nullopt;
        }
        s.my_player = static_cast<std::uint8_t>(k);
    }


    Block slot;
    for (std::size_t i = 0; i < 8; ++i) {
        if (i < open) {
            slot.colour[i] = game[i].colour, slot.team[i] = game[i].team, slot.level[i] = game[i].level;
            slot.nation[i] = game[i].nation == 5 ? static_cast<std::uint8_t>(Rng_Next(s) % 5) : game[i].nation;
        } else {
            slot.colour[i] = 0, slot.team[i] = 0xff, slot.level[i] = 0, slot.nation[i] = 0;
        }
    }

    RNG_FillTable(s);






    const std::size_t count = positions;
    Block pos;
    std::array<std::uint8_t, 8> pos_slot{};  // the screen slot of each position (network game names)
    {
        const std::uint8_t me = s.my_player;
        std::array<std::uint8_t, 8> perm{};
        for (std::size_t i = 0; i < count; ++i) {
            for (;;) {
                perm[i] = static_cast<std::uint8_t>(Rng_Next(s) % count);
                bool used = false;
                for (std::size_t k = 0; k < i; ++k) used = used || perm[k] == perm[i];
                if (!used) break;
            }
        }
        if (const auto gen = MapGen_ParseCode(setup.map, nullptr))
            TeamsSideBySide(perm, count, slot.team, gen->islands);
        if (me < count) s.my_player = perm[me];
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint8_t p = perm[i];
            pos.nation[p] = slot.nation[i] > 5 ? 0 : slot.nation[i];
            pos.colour[p] = slot.colour[i] > 7 ? 0 : slot.colour[i];
            pos.team[p] = slot.team[i], pos.level[p] = slot.level[i];
            pos_slot[p] = game_slot[i];
        }
    }

    // LoadMap with RandomMap_Setup's block (the players after the shuffle): a closed position (level 0) keeps the
    // map record's nation and own name; a position of the game has the setup record's name (slot 0: yours, set by
    // the front end) and, with none, the position's IDS_COMP<p + 1>. SetTeam(team - 1) as a signed byte (a closed
    // position's 0xff gives -2).
    std::array<MapPosition, 8> spec{};
    std::array<std::int8_t, 8> team{};
    for (std::size_t p = 0; p < positions; ++p) {
        const bool closed = pos.level[p] == 0;
        spec[p].closed = closed;
        spec[p].nation = closed ? map.player_nations[p] : pos.nation[p];
        spec[p].colour = pos.colour[p];
        spec[p].map_name = closed;
        if (p != s.my_player || closed) spec[p].name = setup.comp_names[p];
        if (setup.net && !closed && (pos.level[p] == 4 || pos.level[p] == 5)) {
            spec[p].human = true;
            const std::u16string& n = setup.names[pos_slot[p]];
            spec[p].name = n.empty() ? setup.comp_names[p] : n;
        }
        team[p] = static_cast<std::int8_t>(pos.team[p] - 1);
    }
    if (!NewGame_LoadMap(s, map, setup.map, spec, &team, tpl, error)) return std::nullopt;


    for (std::size_t p = 0; p < s.slots.size(); ++p) {
        s.slots[p] = GameState::SlotRecord{u"", pos.nation[p], pos.colour[p], pos.team[p], pos.level[p]};
    }
    s.skirmish_flag = 1;

    // RandomMap_Setup, after the map: every player gets AIType 2, TAI +6 2, the level of its position (4 and 5
    // give 2) and the start stocks (f32 constants stored as doubles; food more on a winter map).
    static const float kStock[3] = {1000.0f, 10000.0f, 100000.0f};
    static const float kWinterFood[3] = {11000.0f, 60000.0f, 200000.0f};
    const double stock = kStock[setup.resources];
    const double food = s.climate == 3 ? kWinterFood[setup.resources] : stock;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        Player& pl = s.players[p];
        pl.ai_type = 2, pl.ai_kind = 2;
        pl.ai_level = pos.level[p] == 4 || pos.level[p] == 5 ? 2 : pos.level[p];
        pl.gold = stock, pl.iron = stock, pl.wood = stock, pl.oil = stock, pl.food = food;
        pl.start_res = {stock, food, stock, stock, stock};  // StartRes: Gold, Food, Wood, Iron, Oil
    }

    NewGame_AfterMap(s, setup.economic, setup.military);

    // Skirmish_Begin: on a water map every player's AIType and TAI +6 become 4, or 5 with port sites.
    if (s.land_map == 0) {
        const std::uint8_t kind = s.port_sites.empty() ? 4 : 5;
        for (Player& pl : s.players) pl.ai_type = kind, pl.ai_kind = kind;
    }
    Fog_Rebuild(s);
    return s;
}

}  // namespace antanta::sim
