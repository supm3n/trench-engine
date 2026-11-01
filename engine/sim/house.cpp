



#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "control_group.hpp"
#include "movement.hpp"
#include "original_ids.hpp"

namespace antanta::sim {

namespace {


struct Stock {
    double Player::* field;
    int cost;  // 0 Food, 1 Wood, 2 Iron, 3 Gold, 4 Oil
};

}  // namespace



bool Player_CanAfford(const GameState& s, std::uint8_t p, std::uint16_t type, std::uint8_t nation) {
    const Player& pl = s.players[p];
    const UnitType& t = s.unit_types[nation != 0xff ? nation : pl.nation][type];
    constexpr Stock order[5] = {
        {&Player::gold, 3}, {&Player::food, 0}, {&Player::wood, 1}, {&Player::iron, 2}, {&Player::oil, 4}};
    for (const Stock& r : order) {
        const float cost = t.cost[static_cast<std::size_t>(r.cost)];
        if (cost > 1.0f && static_cast<double>(cost) > pl.*r.field) return false;
    }
    return pl.units_alive < static_cast<std::int32_t>(pl.unit_cap);
}



bool Player_PayForUnit(GameState& s, std::uint8_t p, std::uint16_t type, std::uint8_t nation) {
    if (!Player_CanAfford(s, p, type, nation)) return false;
    Player& pl = s.players[p];
    const UnitType& t = s.unit_types[nation != 0xff ? nation : pl.nation][type];
    constexpr Stock order[5] = {
        {&Player::gold, 3}, {&Player::wood, 1}, {&Player::food, 0}, {&Player::iron, 2}, {&Player::oil, 4}};
    for (const Stock& r : order) {
        double& v = pl.*r.field;
        v = v - static_cast<double>(t.cost[static_cast<std::size_t>(r.cost)]);
        double& st = pl.*StatOf(r.field);
        st = st - static_cast<double>(t.cost[static_cast<std::size_t>(r.cost)]);
        if (v < 0.0) v = 0.0;
    }
    return true;
}




void House_Enqueue(GameState& s, std::uint8_t p, std::uint16_t k, std::uint16_t type, bool replace) {
    if (p >= s.players.size() || s.players[p].is_lost != 0 || k >= s.players[p].houses.size()) return;
    Player& pl = s.players[p];
    House& h = pl.houses[k];
    const HouseType& ht = House_Type(s, p, h);  // THouse::Basic: a merged house lists its own nation's types
    std::size_t i = 0;
    for (; i < 10; ++i) {
        if (ht.produce[i] == 0xffff) return;
        if (ht.produce[i] == type) break;
    }
    if (i == 10) return;
    if (h.queue_count == 0xffff) {
        if (!h.queue.empty() && h.queue[0] == type) return;
        if (!h.queue.empty()) House_CancelTrain(s, p, k, h.queue[0], true);  // the repeated type (refund)
        h.queue_count = 0;
    }
    if (replace) {  // every queued unit, head first (refund)
        while (h.queue_count != 0 && !h.queue.empty()) {
            const std::uint16_t before = h.queue_count;
            House_CancelTrain(s, p, k, h.queue[0], true);
            if (h.queue_count == before) break;  // the port's ship: unported, counted there
        }
        h.queue_count = 0;
    }
    const UnitType& t = s.unit_types[House_TrainNation(s, p, k)][type];  // B263: a merged house's own nation
    if (h.queue_count == 0) {
        pl.power_used = static_cast<float>(static_cast<double>(t.cost[5]) + pl.power_used);
        h.queue.clear();
        h.field_044 = static_cast<float>(t.build_time[p]);
    }
    h.queue.push_back(type);
    ++h.queue_count;
    if (replace) h.queue_count = 0xffff;
}

namespace {



bool House_PayUpkeep(GameState& s, std::uint8_t p, const House& h) {
    // Entente Engine Plus, F5 (docs/plus-balance.md 2): an academy (types 2 and 5, 1.0 food and 0.8 gold a tick)
    // pays only while a research runs (its timer, the one the house loop counts down).
    if (s.plus_rules && (h.type == 2 || h.type == 5) && h.field_05c == 0) return true;
    Player& pl = s.players[p];
    const HouseType& ht = House_Type(s, p, h);
    constexpr Stock order[5] = {
        {&Player::gold, 3}, {&Player::food, 0}, {&Player::iron, 2}, {&Player::wood, 1}, {&Player::oil, 4}};
    for (const Stock& r : order) {
        double& v = pl.*r.field;
        v = v - static_cast<double>(ht.upkeep[static_cast<std::size_t>(r.cost)]);
        double& st = pl.*StatOf(r.field);
        st = st - static_cast<double>(ht.upkeep[static_cast<std::size_t>(r.cost)]);
        if (v < 0.0) v = 0.0;
    }
    for (const Stock& r : order) {
        if (!(pl.*r.field >= 0.0)) return false;
    }
    return true;
}


bool House_DoorFree(const GameState& s, const House& h) {
    const Cell c = WorldToCell(h.exit_x, h.exit_y);
    if (c.x < 0 || c.y < 0 || c.x >= s.map.width || c.y >= s.map.height) return false;
    const auto i = static_cast<std::size_t>(c.y * s.map.width + c.x);
    if (h.type == 0x13) return s.map.pass_large[i] == 0;
    if (h.type == 0x16) return true;
    return s.map.pass_land[i] == 0;
}

}  // namespace




Unit CreateUnit(GameState& s, float x, float y, std::uint16_t type, std::uint8_t nation, std::uint8_t player) {
    const UnitType& t = s.unit_types[nation][type];
    Unit u;
    u.serial = s.next_serial++;
    u.num_sprite = 0, u.num_work = 3, u.field_078 = 100;
    u.frame_slot = static_cast<std::uint8_t>(static_cast<std::int32_t>(u.serial) % 50);
    u.field_006 = 1;
    u.orient = static_cast<std::uint8_t>(Rng_Next(s) & 7);
    u.target_orient = u.orient;
    u.field_0ca = 0xffff, u.attack_unit_id = 0xffff;
    u.is_stopped = 1, u.work = 0xff, u.field_09d = 1, u.player_unit_attack = 0xff, u.player_house_attack = 0xff;
    u.field_116 = 0xff, u.field_174 = 1, u.field_178 = 0xff;
    u.type = type;
    // The constructor: reload countdown, angle from the orientation (stays on the stack for the way back).
    u.field_0ee = static_cast<std::uint16_t>(t.reload[0] >> 1);
    const double a = static_cast<double>(static_cast<std::int8_t>(u.orient)) * static_cast<double>(0.7853982f) + 0.0001;
    u.angle = static_cast<float>(a);
    u.orient = static_cast<std::uint8_t>(Ftol(a * static_cast<double>(1.2732395f)));
    u.z = 50.0f;
    u.hp = static_cast<float>(t.max_hp[0]);
    u.field_118 = static_cast<float>(t.field_594);
    u.field_0ec = t.field_434;

    u.weapon_count = static_cast<std::uint8_t>(t.weapons.size());
    // CreateUnit: position, Dest and Final at the point, the cell.
    u.x = x, u.y = y;
    u.dest_x = x, u.dest_y = y, u.final_x = x, u.final_y = y;
    const Cell c = WorldToCell(x, y);
    u.map_x = static_cast<std::uint16_t>(c.x), u.map_y = static_cast<std::uint16_t>(c.y);
    if (t.is_air != 0) Air_CreateUnit(t, player, u);
    return u;
}


std::uint16_t Player_AllocUnitId(Player& pl) {
    if (pl.free_ids.empty()) return pl.next_id++;
    const std::uint16_t id = pl.free_ids.front();
    pl.free_ids.erase(pl.free_ids.begin());
    return id;
}





void UnitType_AddGraphicRef(GameState& s, std::uint8_t nation, std::uint16_t type, std::uint8_t own) {
    auto ref = [&s](std::uint8_t n, std::uint16_t t) {
        UnitType& b = s.unit_types[n][t];
        const std::uint32_t c = ++s.graphic_refs[b.graphic];
        if (c == 1 && s.short_anims != 0) b.anim_size[4] = 0;
        return c;
    };
    // `this` is the unit's own record (nation own), the argument the owner's nation: its type 1 unless that is `this`.
    if (own == 0xff) own = nation;
    if (ref(own, type) == 1 && s.unit_types[own][type].move_class == 6 && (type != 1 || own != nation)) ref(nation, 1);
}




Unit& AddUnitToPlayer(GameState& s, std::uint8_t p, Unit u) {
    Player& pl = s.players[p];
    ++pl.unit_count[u.type % 40];
    ++pl.units_alive;
    const std::uint8_t own = u.nation != 0xff ? u.nation : pl.nation;  // TUnit::Basic (a merged unit keeps its own)
    u.hp = static_cast<float>(s.unit_types[own][u.type].max_hp[p]);
    u.id = Player_AllocUnitId(pl);
    if (s.unit_types[own][u.type].is_ship == 0) UnitType_AddGraphicRef(s, pl.nation, u.type, own);
    u.in_cell_list = false;
    Unit& v = pl.unit_slots[Unit_Add(s, p, u, true)];
    TUnit_SetCell(s, p, v, WorldToCell(v.x, v.y));
    TUnit_UpdateGroundHeight(s, p, v);
    if (s.unit_types[own][v.type].is_ship != 0) Ship_Created(s, p, v);  // CreateUnit's ship part
    return v;
}



void House_ProduceUnit(GameState& s, std::uint8_t p, std::uint16_t k) {
    Player& pl = s.players[p];
    House& h = pl.houses[k];
    const std::uint8_t nation = House_TrainNation(s, p, k);  // B263: a merged house trains its own nation's unit
    if (h.queue_count == 0xffff && !Player_CanAfford(s, p, h.queue[0], nation)) {
        h.field_044 = 0.01f;
        return;
    }
    if (h.queue_count == 0 || h.queue.empty()) return;
    const UnitType& t = s.unit_types[nation][h.queue[0]];
    pl.power_used = static_cast<float>(static_cast<double>(pl.power_used) - static_cast<double>(t.cost[5]));
    if (!h.has_field_0b0) {
        const float x = h.x, y = h.y;
        Unit made = CreateUnit(s, x, y, h.queue[0], nation, p);
        if (nation != pl.nation) made.nation = nation;  // B263: it then behaves as a merged unit (Unit::nation)
        Unit& u = AddUnitToPlayer(s, p, made);
        u.field_116 = h.field_0b8;
        if (t.is_air == 0) {
            u.y = y, u.x = x;
            u.work = 9;
            u.in_house = true;  // House = this house (the door walk)
            u.house_player = p, u.house_slot = k;
            u.dest_x = h.exit_x, u.dest_y = h.exit_y;
            u.final_x = u.dest_x, u.final_y = u.dest_y;
            u.field_0f8 = 1;
            u.field_0bc = 1;
            u.waypoints[0] = Unit::Waypoint{h.rally_x, h.rally_y, 0xff, 0xffff};
            TUnit_ComputeVelocity(s, p, u);
            TUnit_SetMoveAnimation(s, p, u);
            u.field_078 = u.num_work;
            TUnit_SetOrientToDest(s, p, u);
            u.orient = u.target_orient;
            u.num_work = u.field_078;
            u.field_078 = 100;
            u.path.clear();
            u.is_stopped = 0;
            u.angle = static_cast<float>(static_cast<double>(static_cast<std::int8_t>(u.target_orient)) *
                                             static_cast<double>(0.7853982f) +
                                         static_cast<double>(0.0001f));
        } else {
            Air_ProduceUnit(s, p, k, u);
        }
    } else {
        if (Unit* ship = Unit_Find(s, h.field_0b0)) {
            ship->in_house = false;
        } else {
            Unported(s, orig::kHouseProduceUnit);  // a ship the port cannot find
        }
        h.has_field_0b0 = false, h.field_0b0 = UnitRef{};
    }
    if (h.queue_count == 0xffff) {
        if (!Player_CanAfford(s, p, h.queue[0], nation)) return;
        Player_PayForUnit(s, p, h.queue[0], nation);
        const UnitType& n = s.unit_types[nation][h.queue[0]];
        h.field_044 = static_cast<float>(n.build_time[p]);
        pl.power_used = static_cast<float>(static_cast<double>(n.cost[5]) + pl.power_used);
        return;
    }
    h.queue.erase(h.queue.begin());
    --h.queue_count;
    if (h.queue_count != 0) {
        const UnitType& n = s.unit_types[nation][h.queue[0]];
        h.field_044 = static_cast<float>(n.build_time[p]);
        pl.power_used = static_cast<float>(static_cast<double>(n.cost[5]) + pl.power_used);
    }
}





float House_PlusNextOutput(const GameState& s, std::uint8_t p, const House& h, int step) {
    const double v = h.field_070;
    if (step == 0) return static_cast<float>(v * kPlusLevel1);
    if (step == 1) return static_cast<float>(v * kPlusLevel2);
    const HouseType& ht = s.house_types[s.players[p].nation % 6][h.type % 25];
    return static_cast<float>(v + kPlusLevelAdd * static_cast<double>(std::bit_cast<float>(ht.field_190)));
}

float Mint_Factor(const GameState& s) { return s.plus_rules ? 1.5f : 2.0f; }

namespace {

// An upgrade cost row: Food, Wood, Iron, Gold, Oil, (unused). Tables of 6 levels.
using CostRow = std::array<std::int32_t, 6>;
constexpr CostRow kMineCost[6] = {{1000, 500, 0, 1000, 0, 0},      {5000, 1500, 0, 3000, 0, 0},
                                  {25000, 4500, 0, 9000, 0, 0},    {125000, 13500, 0, 27000, 0, 0},
                                  {625000, 41500, 0, 81000, 0, 0}, {243000, 124500, 0, 243000, 0, 0}};
constexpr CostRow kOilCost[6] = {{1000, 0, 1000, 1000, 0, 0},     {5000, 0, 3000, 3000, 0, 0},
                                 {25000, 0, 9000, 9000, 0, 0},    {125000, 0, 27000, 27000, 0, 0},
                                 {625000, 0, 81000, 81000, 0, 0}, {243000, 0, 243000, 243000, 0, 0}};
// Entente Engine Plus, F3 (docs/plus-balance.md 2, docs/bugs.md B302): the original's last two rows slip (level 5
// costs 625,000 food, level 6 only 243,000; the mine's wood 41,500 and 124,500 break its x3 steps). Plus: 250,000
// and 500,000 food, 40,500 and 121,500 wood; the rest as the original's.
constexpr CostRow kPlusMineCost[6] = {{1000, 500, 0, 1000, 0, 0},      {5000, 1500, 0, 3000, 0, 0},
                                      {25000, 4500, 0, 9000, 0, 0},    {125000, 13500, 0, 27000, 0, 0},
                                      {250000, 40500, 0, 81000, 0, 0}, {500000, 121500, 0, 243000, 0, 0}};
constexpr CostRow kPlusOilCost[6] = {{1000, 0, 1000, 1000, 0, 0},     {5000, 0, 3000, 3000, 0, 0},
                                     {25000, 0, 9000, 9000, 0, 0},    {125000, 0, 27000, 27000, 0, 0},
                                     {250000, 0, 81000, 81000, 0, 0}, {500000, 0, 243000, 243000, 0, 0}};
// Entente Engine Plus, O4 (docs/plus-balance.md 3, 2026-10-23): the sawmill's six levels, which the original does not
// have. Food as the Plus mine rows; iron and gold 500, 1,500, 4,500, then the mine rows' x3 steps; no wood or oil.
constexpr CostRow kPlusSawCost[6] = {{1000, 0, 500, 500, 0, 0},       {5000, 0, 1500, 1500, 0, 0},
                                     {25000, 0, 4500, 4500, 0, 0},    {125000, 0, 13500, 13500, 0, 0},
                                     {250000, 0, 40500, 40500, 0, 0}, {500000, 0, 121500, 121500, 0, 0}};
const CostRow& MineCost(const GameState& s, std::uint8_t level) {
    return s.plus_rules ? kPlusMineCost[level] : kMineCost[level];
}
const CostRow& OilCost(const GameState& s, std::uint8_t level) {
    return s.plus_rules ? kPlusOilCost[level] : kOilCost[level];
}


constexpr std::uint16_t kMineTime[6] = {500, 700, 900, 1100, 1300, 1500};
constexpr std::int32_t kHqCost[6] = {200, 400, 800, 1600, 3000, 5000};
constexpr std::uint16_t kHqTime[6] = {300, 500, 800, 1200, 1600, 2200};



bool Lacks(const Player& pl, const CostRow& c) {
    return !(c[0] <= pl.food && c[3] <= pl.gold && c[1] <= pl.wood && c[2] <= pl.iron && c[4] <= pl.oil);
}


bool CanUpgrade(const Player& pl, const House& h, const CostRow& c) { return h.field_05c == 0 && !Lacks(pl, c); }


void StartUpgrade(Player& pl, House& h, const CostRow& c, std::uint16_t time) {
    h.field_05c = time;
    h.field_064 = static_cast<std::uint8_t>(h.field_065 + 3);
    h.field_06a = 1;
    h.field_060 = static_cast<float>(time);
    pl.food = pl.food - c[0], pl.wood = pl.wood - c[1], pl.iron = pl.iron - c[2];
    pl.gold = pl.gold - c[3], pl.oil = pl.oil - c[4];
    pl.food_stat = pl.food_stat - c[0], pl.wood_stat = pl.wood_stat - c[1], pl.iron_stat = pl.iron_stat - c[2];
    pl.gold_stat = pl.gold_stat - c[3], pl.oil_stat = pl.oil_stat - c[4];
}



void House_Staff(GameState& s, std::uint8_t p, const House& h, bool flag) {
    Player& pl = s.players[p];
    if (pl.field_9b2b3c == 0) return;
    const bool now = flag ? s.tick % 300 == h.num % 300u : s.tick % 1000 == h.num;
    if (!(h.units_inside < h.capacity) || !now) return;
    if (pl.control_type != 1 && pl.field_9b2d94 == 0) return;
    float best = 50000.0f;
    Unit* pick = nullptr;
    for (const std::uint32_t k : Player_UnitSlots(pl)) {
        Unit& u = pl.unit_slots[k];
        if (u.is_deleted != 0 || Unit_Type(s, p, u).move_class != 6 || u.is_stopped == 0 || u.work != 0xff) continue;
        const double dx = static_cast<double>(h.exit_x) - u.x, dy = static_cast<double>(h.exit_y) - u.y;
        const double d = std::sqrt(dx * dx + dy * dy);
        if (d < best) best = static_cast<float>(d), pick = &u;
    }
    if (pick != nullptr) Worker_SendToHouse(s, p, *pick, h.num, 0);
}



void House_Income(GameState& s, std::uint8_t p, const House& h, double factor, double Player::* stock) {
    Player& pl = s.players[p];
    std::uint8_t workers = 0;
    for (std::uint8_t i = 0; i < h.units_inside; i = static_cast<std::uint8_t>(i + 1)) {
        const Unit* u = i < h.inside.size() ? Unit_Find(s, h.inside[i]) : nullptr;
        if (u == nullptr) {
            Unported(s, orig::kIronMineCallback);  // a listed unit the port does not know
            continue;
        }
        if (u->type == 0) ++workers;
    }
    const std::int32_t others = h.units_inside - workers;
    const double v = factor * h.field_070 * (static_cast<double>(others) * 0.5 + workers);
    double& r = pl.*stock;
    r = v + r;
    pl.*StatOf(stock) = v + pl.*StatOf(stock);
    if (r < 0.0) r = 0.0;
    double& mined = pl.mined[MinedIndex(stock)];  // GoldMined, IronMined, WoodMined
    mined = v + mined;
}

// The staffing flag of iron and wood: the resource's Stat fell between the last two history samples.
bool HistoryFell(const Player& pl, double HistSample::* r) {
    if (pl.history_index <= 1) return false;
    return pl.history[pl.history_index - 1].*r - pl.history[pl.history_index - 2].*r < 0.0;
}


std::uint8_t CellHeight(const GameState& s, std::int32_t x, std::int32_t y) {
    const auto cx = static_cast<std::uint16_t>(x), cy = static_cast<std::uint16_t>(y);
    if (cx >= s.map.width || cy >= s.map.height) return 0;
    return s.map.heights[static_cast<std::size_t>(cy) * s.map.width + cx];
}

constexpr CostRow kTowerCost[4] = {{1000, 0, 0, 100, 0, 0},
                                   {1000, 0, 0, 200, 0, 0},
                                   {1000, 0, 0, 400, 0, 0},
                                   {1000, 0, 0, 800, 0, 0}};
constexpr std::uint16_t kTowerTime[4] = {200, 300, 400, 500};
constexpr CostRow kBunkerCost[4] = {{1000, 0, 0, 200, 0, 0},
                                    {1000, 0, 0, 400, 0, 0},
                                    {1000, 0, 0, 800, 0, 0},
                                    {1000, 0, 0, 1600, 0, 0}};
constexpr std::uint16_t kBunkerTime[4] = {300, 400, 600, 800};

constexpr CostRow kDwellCost[6] = {{100, 100, 100, 100, 100, 0}, {150, 150, 150, 150, 150, 0},
                                   {200, 200, 200, 200, 200, 0}, {250, 250, 250, 250, 250, 0},
                                   {300, 300, 300, 300, 300, 0}, {350, 350, 350, 350, 350, 0}};
constexpr std::uint16_t kDwellTime[6] = {300, 500, 600, 800, 1000, 1200};


constexpr CostRow kHqRows[6] = {{200, 200, 200, 200, 200, 3},      {400, 400, 400, 400, 400, 4},
                                {800, 800, 800, 800, 800, 5},      {1600, 1600, 1600, 1600, 1600, 6},
                                {3000, 3000, 3000, 3000, 3000, 7}, {5000, 5000, 5000, 5000, 5000, 8}};
constexpr CostRow kMintCost = {10000, 0, 5000, 1000, 0, 0};




constexpr CostRow kBuildAcademyCost[3] = {
    {10000, 5000, 5000, 5000, 0, 0}, {10000, 5000, 5000, 5000, 0, 0}, {10000, 5000, 5000, 5000, 0, 0}};
constexpr std::uint16_t kBuildAcademyTime[3] = {1000, 2000, 2000};



bool HouseTargetOut(const GameState& s, const House& h, std::uint8_t tp, const Unit& t) {
    if (Unit_Type(s, tp, t).move_class == 3 && t.type != 0x18) return true;
    const double dx = static_cast<double>(h.x) - t.x, dy = static_cast<double>(h.y) - t.y;
    return !(std::sqrt(dy * dy + dx * dx) <= static_cast<double>(h.field_080));
}





bool HouseSearch(GameState& s, std::uint8_t hp, House& h) {
    const bool any = h.type == 0x15;
    const auto rx =
        static_cast<std::uint8_t>(Ftol(static_cast<double>(h.field_080) * static_cast<double>(0.010416667f)));
    const auto ry =
        static_cast<std::uint8_t>(Ftol(static_cast<double>(h.field_080) * static_cast<double>(0.020833334f)));
    double bound = h.field_080;
    std::array<UnitRef, 10> list{};
    std::uint8_t n = 0;
    const std::int32_t w = s.map.width;
    const auto cells = static_cast<std::int64_t>(s.map.cell_units.size());
    for (std::int32_t dx = -rx; dx <= rx; ++dx) {
        for (std::int32_t j = 0; j <= 2 * ry; ++j) {
            const std::int64_t k = static_cast<std::int64_t>(h.map_y + ry - j) * w + h.map_x - dx;
            if (k < 0 || k >= cells) continue;
            const std::vector<UnitRef>& here = s.map.cell_units[static_cast<std::size_t>(k)];
            for (std::size_t i = 0; i < here.size() && i < 40; ++i) {
                const Unit* u = Unit_Find(s, here[i]);
                if (u == nullptr || u->is_deleted != 0) continue;
                const std::uint8_t up = here[i].player;
                if (up == hp) continue;
                if (up != 0xff && hp != 0xff) {
                    const Player& a = s.players[hp];
                    const Player& b = s.players[up];
                    if (a.is_lost != 0 || b.is_lost != 0) continue;
                    if (b.is_ally[hp] == a.is_ally[up] && b.is_ally[hp] != 0) continue;
                }
                const double ux = static_cast<double>(u->x) - h.x, uy = static_cast<double>(u->y) - h.y;
                const double d = std::sqrt(uy * uy + ux * ux);
                const auto df = static_cast<float>(d);
                if (!(d < bound)) continue;
                if (!any && Unit_Type(s, up, *u).is_air != 0) continue;
                if (n < 10) {
                    list[n++] = here[i];
                    continue;
                }
                for (std::size_t m = 0; m < 10; ++m) {
                    const Unit* e = Unit_Find(s, list[m]);
                    const double ex = static_cast<double>(e->x) - h.x, ey = static_cast<double>(e->y) - h.y;
                    if (std::sqrt(ey * ey + ex * ex) > static_cast<double>(df)) {
                        for (std::size_t q = 9; q > m; --q) list[q] = list[q - 1];
                        list[m] = here[i];
                    }
                }
                const Unit* e = Unit_Find(s, list[n - 1]);
                const auto ey = static_cast<float>(static_cast<double>(e->y) - h.y);
                const double ex = static_cast<double>(e->x) - h.x;
                bound = std::sqrt(ex * ex + static_cast<double>(ey) * ey);
            }
        }
    }
    if (n == 0) return false;
    h.field_088 = list[s.rng_table[s.rng_index] % n];
    ++s.rng_index;
    h.field_084 = h.field_088.player;
    const Unit* t = Unit_Find(s, h.field_088);
    MarkZone(s, hp, t->x, t->y, 0x1e);
    MarkZone(s, hp, h.x, h.y, 0x1e);
    return true;
}





void HouseFire(GameState& s, std::uint8_t hp, House& h) {
    const auto search_again = [&] {
        h.field_084 = 0xff;
        h.field_088 = UnitRef{};
        HouseSearch(s, hp, h);
    };
    Unit* t = Unit_Find(s, h.field_088);  // a freed unit counts as deleted
    const std::uint8_t tp = h.field_088.player;
    if (t == nullptr || t->is_deleted != 0 || HouseTargetOut(s, h, tp, *t)) {
        search_again();
        return;
    }
    Event_HouseSound(s, hp, h, 6, 4);
    const HouseType& ht = House_Type(s, hp, h);
    const auto ax = static_cast<float>(static_cast<double>(h.x) - ht.field_140);
    auto ay = static_cast<float>(static_cast<double>(h.y) - ht.field_142);
    ay = static_cast<float>(static_cast<double>(ay) - CellHeight(s, h.map_x, h.map_y));
    House_MuzzleFlash(s, hp, h);
    const std::uint32_t r = s.rng_table[s.rng_index] % 0x65;
    ++s.rng_index;
    const double dx = static_cast<double>(ax) - t->x, dy = static_cast<double>(ay) - t->y;
    const auto d = static_cast<float>(std::sqrt(dy * dy + dx * dx));
    if (static_cast<double>(s.players[hp].accuracy_factor) * static_cast<double>(50.0f) < static_cast<double>(r)) {
        return;
    }
    const UnitType& tt = Unit_Type(s, tp, *t);
    const auto def = static_cast<float>(static_cast<double>(100.0f) - tt.defense[tp][1]);
    const double base = static_cast<double>(def) / ExpFactor(s, tp, *t);
    const double half = static_cast<double>(h.field_080) * static_cast<double>(0.5f);
    double f = 1.0;
    if (!(static_cast<double>(d) < half)) f = (static_cast<double>(h.field_080) - d) / half;
    auto dmg = static_cast<float>(f * base);
    if (dmg < 1.0f) dmg = 1.0f;
    double v = dmg;
    if (t->work == 3 || t->work == 0x14) {
        v = static_cast<double>(dmg) * static_cast<double>(0.1f);
    } else if (t->num_work == 1 || t->num_work == 2 || t->num_work == 0x14 || t->num_work == 8 || t->num_work == 0xe ||
               t->num_work == 0x15) {
        v = static_cast<double>(dmg) * static_cast<double>(0.4f);
    }
    const double left = static_cast<double>(t->hp) - v;
    t->hp = static_cast<float>(left);
    if (left < 0.0) {
        OnUnitKilled(s, tp, *t, hp);
        search_again();
    }
}





void House_Fortified(GameState& s, std::uint8_t p, House& h) {
    Player& pl = s.players[p];
    const bool due = s.tick % 1000 == h.num || (s.tick <= 1000 && s.tick % 100 == h.num % 100u);
    if (h.field_065 < 4 && due && (pl.control_type == 1 || pl.field_9b2d9c != 0 || pl.field_9b2d94 != 0)) {
        // Entente Engine Plus: upgraded by the smart rule instead, only near a threat or when damaged (plus_brain.cpp)
        if (!s.plus_rules && (pl.control_type == 1 || (pl.field_9b2d94 != 0 && pl.field_9b2b3b != 0))) {
            const bool tower = h.type == 0x15;
            const CostRow& c = tower ? kTowerCost[h.field_065] : kBunkerCost[h.field_065];
            if (CanUpgrade(pl, h, c)) {
                StartUpgrade(pl, h, c, tower ? kTowerTime[h.field_065] : kBunkerTime[h.field_065]);
            }
        }
        if ((pl.control_type == 1 || (pl.field_9b2d9c != 0 && pl.field_9b2a98[0] != 0)) &&
            h.units_inside < h.capacity) {
            Unit* u = AI_FindIdle(s, p, 6, h.exit_x, h.exit_y, 2000.0f);
            if (u == nullptr) u = AI_FindIdle(s, p, 2, h.exit_x, h.exit_y, 2000.0f);
            if (u != nullptr) Worker_SendToHouse(s, p, *u, h.num, 0);
        }
    }
    if (h.field_074 == 0 || h.units_inside == 0) return;
    if (h.field_088.player == 0xff) h.field_084 = 0xff;
    if (h.field_084 == 0xff) {
        if (s.tick % 30 == h.num % 30u) HouseSearch(s, p, h);
        return;
    }
    for (std::uint8_t g = 0; g < h.field_074; ++g) {
        if (h.field_07c <= 0.0f) {
            h.field_07c = h.field_078;
            HouseFire(s, p, h);
            House_MuzzleFlash(s, p, h);
        } else {
            h.field_07c = static_cast<float>(static_cast<double>(h.field_07c) - 1.0);
        }
    }
}

}  // namespace

const std::int32_t* House_UpgradeCostRow(const GameState& s, std::uint16_t type, std::uint16_t row) {
    if (s.plus_rules && (type == 10 || type == 11)) return row < 6 ? kPlusMineCost[row].data() : nullptr;
    if (s.plus_rules && type == 13) return row < 6 ? kPlusOilCost[row].data() : nullptr;
    if (s.plus_rules && type == 12) return row < 6 ? kPlusSawCost[row].data() : nullptr;      // O4
    if (s.plus_rules && type == 3) return row < 3 ? kBuildAcademyCost[row].data() : nullptr;  // F7
    return House_UpgradeCostRow(type, row);
}

const std::int32_t* House_UpgradeCostRow(std::uint16_t type, std::uint16_t row) {
    switch (type) {
        case 0: return row < 6 ? kHqRows[row].data() : nullptr;
        case 1:
        case 7: return row < 4 ? kBunkerCost[row].data() : nullptr;
        case 9: return row < 6 ? kDwellCost[row].data() : nullptr;
        case 10:
        case 11: return row < 6 ? kMineCost[row].data() : nullptr;
        case 13: return row < 6 ? kOilCost[row].data() : nullptr;
        case 14: return row == 0 ? kMintCost.data() : nullptr;
        case 0x15: return row < 4 ? kTowerCost[row].data() : nullptr;
        default: return nullptr;
    }
}



void Farm_SetRally(GameState& s, std::uint8_t p, std::uint16_t type, float x, float y, bool flag) {
    Player& pl = s.players[p];
    if (pl.field_9b2b3d == 0) return;
    std::size_t k = 0;
    if (flag && pl.houses.size() > 10) {
        k = s.rng_table[s.rng_index] % pl.houses.size();
        ++s.rng_index;
    }
    double bound = 9999999.0f;
    House* pick = nullptr;
    for (; k < pl.houses.size(); ++k) {
        House& h = pl.houses[k];
        if (h.is_destroyed != 0 || h.type != type || h.field_020 != 0) continue;
        const double dx = static_cast<double>(h.x) - x, dy = static_cast<double>(h.y) - y;
        const double d = std::sqrt(dy * dy + dx * dx);
        if (!(d < bound)) continue;
        pick = &h;
        bound = d;
        if (flag) break;
    }
    if (pick != nullptr) House_SetRally(*pick, x, y);
}





void House_PlantFields(GameState& s, std::uint8_t p, const House& h) {
    Map& m = s.map;
    const std::int32_t W = m.width, H = m.height;
    const HouseFootprint fp = House_Footprint(s, p, h);
    std::uint16_t base = 0xfffd;
    switch (s.players[p].nation) {  // the owner's nation (TStrateg::Nation), not the record's
        case 0:
        case 2: base = 0x25e0; break;
        case 1: base = 0x25de; break;
        case 3:
        case 4: base = 0x25e2; break;
        default: break;
    }
    auto door = [&](Cell c) {
        Cell d = Cell_Step(Cell{h.map_x, h.map_y}, 6);
        for (std::uint8_t i = 0; i < fp.a; ++i) {
            d = Cell_Step(d, 1);
            if (d.x == c.x && d.y == c.y) return true;
        }
        return false;
    };
    Cell row{h.map_x, h.map_y + 6};
    for (std::uint16_t r = 0; r < fp.b + 6; ++r) {
        Cell c = row;
        for (std::uint16_t k = 0; k < fp.a + 6; ++k) {
            if (c.x > 3 && c.y > 9 && c.x < W - 3 && c.y < H - 9 && !door(c)) {
                const auto idx = static_cast<std::size_t>(W * c.y + c.x);
                const std::uint16_t o = m.objects[idx];
                if (!Cell_Obstacle(m, c.x, c.y) && (o < 0x65 || o > 8999) && o != 99 && o != 0x2581 && o != 0x2582 &&
                    o != 0x2583) {
                    m.objects[idx] = static_cast<std::uint16_t>(base + (Rng_Next(s) & 1));
                }
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
}

namespace {




void House_Farm(GameState& s, std::uint8_t p, House& h) {
    if (s.players[p].control_type != 1 || s.tick % 1000 != h.num) return;
    const bool flag = s.rng_table[s.rng_index] > 0x55f0;
    ++s.rng_index;
    Farm_SetRally(s, p, 6, static_cast<float>(static_cast<double>(h.exit_x) + 40.0f),
                  static_cast<float>(static_cast<double>(h.exit_y) + 40.0f), flag);
    const std::uint16_t r = s.rng_table[s.rng_index];
    ++s.rng_index;
    if (r < 0x3e80 && s.players[p].gold > 7000.0) House_PlantFields(s, p, h);
}



// HP += 5 (hangar: aircraft) or 10 (tank plant: MoveClass 3 and type 0x17) up to MaxHP[owner], if within the house
// range: the hangar measures from the footprint's cell (x = (map_x * 2 - (~map_y & 1)) * 48, y = map_y * 24, all on

// IsEnemy written out inline (an own unit, a lost player, or allied both ways). Units in each cell are taken in
// slot order, at most 40.
void House_RepairNear(GameState& s, std::uint8_t p, const House& h, bool hangar) {
    if (s.tick % 20 != 0) return;
    const auto r = Ftol(static_cast<double>(h.field_080) * static_cast<double>(0.0125f));
    const HouseFootprint& fp = House_Footprint(s, p, h);
    const std::int32_t mx = static_cast<std::uint16_t>(h.map_x), my = static_cast<std::uint16_t>(h.map_y);
    std::int16_t box[4] = {static_cast<std::int16_t>(mx - fp.a - r), static_cast<std::int16_t>(my - fp.b - 2 * r),
                           static_cast<std::int16_t>(mx + fp.a + r), static_cast<std::int16_t>(my + fp.b + 2 * r)};
    for (int k = 0; k < 4; k += 2) {
        const std::int32_t w = s.map.width, ht = s.map.height;
        if (box[k] <= 3) box[k] = 4;
        if (box[k + 1] <= 9) box[k + 1] = 10;
        if (box[k] >= w - 3) box[k] = static_cast<std::int16_t>(w - 4);
        if (box[k + 1] >= ht - 9) box[k + 1] = static_cast<std::int16_t>(ht - 10);
    }
    const double cx = static_cast<double>((static_cast<std::int32_t>(h.map_x) * 2 - (~h.map_y & 1)) * 48);
    const double cy = static_cast<double>(h.map_y * 24);
    for (std::int32_t x = box[0]; x < box[2]; ++x) {
        for (std::int32_t y = box[1]; y < box[3]; ++y) {
            const std::vector<UnitRef>& here = s.map.cell_units[static_cast<std::size_t>(y * s.map.width + x)];
            for (std::size_t i = 0; i < here.size() && i < 40; ++i) {
                Unit* u = Unit_Find(s, here[i]);
                if (u == nullptr) continue;
                const std::uint8_t up = here[i].player;
                if (IsEnemy(s, p, up) || u->is_deleted != 0) continue;
                const UnitType& t = Unit_Type(s, up, *u);
                double d = 0;
                if (hangar) {
                    if (t.is_air == 0) continue;
                    const double dx = static_cast<double>(u->x) - cx, dy = static_cast<double>(u->y) - cy;
                    d = std::sqrt(dy * dy + dx * dx);
                } else {
                    if (t.move_class != 3 && u->type != 0x17) continue;
                    const auto dy = static_cast<float>(static_cast<double>(u->y) - h.y);
                    const double dx = static_cast<double>(u->x) - h.x;
                    d = std::sqrt(dx * dx + static_cast<double>(dy) * dy);
                }
                if (!(d < static_cast<double>(h.field_080))) continue;
                const std::uint16_t max = t.max_hp[up];
                if (!(static_cast<float>(max) > u->hp)) continue;
                const double v = static_cast<double>(u->hp) + (hangar ? 5.0 : 10.0);
                u->hp = static_cast<float>(v);
                if (v > static_cast<double>(static_cast<float>(max))) u->hp = static_cast<float>(max);
            }
        }
    }
}






void House_Heal(GameState& s, std::uint8_t p, const House& h) {
    if (s.tick % 20 != 0) return;
    // Entente Engine Plus, F6 (docs/plus-balance.md 2): the base rate 0.6 instead of 0.2 HP per 20 ticks
    const float base = s.plus_rules ? 0.6f : 0.2f;
    auto f = static_cast<float>(static_cast<double>(s.players[p].factor_9b3318) * static_cast<double>(base));
    if (h.type == 0x11) f = static_cast<float>((static_cast<double>(h.units_inside) + 1.0) * f);
    const auto r = Ftol(static_cast<double>(h.field_080) * static_cast<double>(0.0125f));
    const HouseFootprint& fp = House_Footprint(s, p, h);
    const std::int32_t mx = static_cast<std::uint16_t>(h.map_x), my = static_cast<std::uint16_t>(h.map_y);
    std::int16_t box[4] = {static_cast<std::int16_t>(mx - fp.a - r), static_cast<std::int16_t>(my - fp.b - 2 * r),
                           static_cast<std::int16_t>(mx + fp.a + r), static_cast<std::int16_t>(my + fp.b + 2 * r)};
    for (int k = 0; k < 4; k += 2) {
        const std::int32_t w = s.map.width, ht = s.map.height;
        if (box[k] <= 3) box[k] = 4;
        if (box[k + 1] <= 9) box[k + 1] = 10;
        if (box[k] >= w - 3) box[k] = static_cast<std::int16_t>(w - 4);
        if (box[k + 1] >= ht - 9) box[k + 1] = static_cast<std::int16_t>(ht - 10);
    }
    const double cx = static_cast<double>((static_cast<std::int32_t>(h.map_x) * 2 - (~h.map_y & 1)) * 48);
    const double cy = static_cast<double>(h.map_y * 24);
    for (std::int32_t x = box[0]; x < box[2]; ++x) {
        for (std::int32_t y = box[1]; y < box[3]; ++y) {
            const std::vector<UnitRef>& here = s.map.cell_units[static_cast<std::size_t>(y * s.map.width + x)];
            for (std::size_t i = 0; i < here.size() && i < 40; ++i) {
                Unit* u = Unit_Find(s, here[i]);
                if (u == nullptr) continue;
                const std::uint8_t up = here[i].player;
                if (IsEnemy(s, p, up) || u->is_deleted != 0) continue;
                const UnitType& t = Unit_Type(s, up, *u);
                if (t.field_00c == 0 && (t.is_air != 0 || t.move_class == 3 || t.is_ship != 0)) continue;
                const double dx = static_cast<double>(u->x) - cx, dy = static_cast<double>(u->y) - cy;
                if (!(std::sqrt(dy * dy + dx * dx) < static_cast<double>(h.field_080))) continue;
                const std::uint16_t max = t.max_hp[up];
                if (!(static_cast<float>(max) > u->hp)) continue;
                const double v = static_cast<double>(f) + u->hp;
                u->hp = static_cast<float>(v);
                if (v > static_cast<double>(static_cast<float>(max))) u->hp = static_cast<float>(max);
            }
        }
    }
}


void House_TypeCallback(GameState& s, std::uint8_t p, House& h) {
    enum class Fn : std::uint8_t {
        None,
        Headquarters,
        Fortified,
        Dwelling,
        IronMine,
        GoldMine,
        Sawmill,
        OilWell,
        Farm,
        Heal,
        RepairVehicles,
        RepairAircraft,
        RepairShips
    };
    using enum Fn;
    static constexpr Fn kCallbacks[25] = {
        Headquarters, Fortified,      None,           None,      None,        None, None, Fortified, None,
        Dwelling,     IronMine,       GoldMine,       Sawmill,   OilWell,     None, Farm, None,      Heal,
        Heal,         RepairVehicles, RepairAircraft, Fortified, RepairShips, None, None};
    const Fn f = h.type < 25 ? kCallbacks[h.type] : None;
    if (f == None) return;
    Player& pl = s.players[p];
    const bool helped = pl.control_type == 1 || pl.field_9b2d94 != 0;
    // Entente Engine Plus: no blind upgrades of the power station, the mines, the oil well and the dwelling; the smart
    // rule upgrades them by need (sim/plus_brain.cpp, docs/plus-balance.md 6).
    const bool level_due =
        s.tick % 200 == h.num % 200u && pl.field_9b2b3b != 0 && h.field_065 < 5 && helped && !s.plus_rules;
    switch (f) {
        case Headquarters: {  // headquarters: an upgrade when the power used exceeds the power made
            if (s.tick % 1000 != h.num || h.field_065 >= 5 || !helped || pl.field_9b2b3b == 0 || s.plus_rules) return;
            const auto made = static_cast<std::uint16_t>(Ftol(static_cast<double>(pl.power_6e4) * pl.power_made));
            const auto used = static_cast<std::uint16_t>(Ftol(static_cast<double>(pl.power_used) / pl.power_6c4));
            if (!(made < used)) return;
            const std::int32_t c = kHqCost[h.field_065];
            if (!CanUpgrade(pl, h, CostRow{c, c, c, c, c, 0})) return;

            h.field_05c = kHqTime[h.field_065];
            h.field_064 = static_cast<std::uint8_t>(h.field_065 + 3);
            h.field_060 = static_cast<float>(h.field_05c);
            h.field_06a = 1;
            for (double Player::* r : {&Player::food, &Player::gold, &Player::wood, &Player::iron, &Player::oil}) {
                pl.*r = pl.*r - c;
                pl.*StatOf(r) = pl.*StatOf(r) - c;
                if (pl.*r < 0.0) pl.*r = 0.0;
            }
            return;
        }
        case IronMine:  // iron mine
            if (level_due && CanUpgrade(pl, h, MineCost(s, h.field_065))) {
                StartUpgrade(pl, h, MineCost(s, h.field_065), kMineTime[h.field_065]);
            }
            House_Staff(s, p, h, HistoryFell(pl, &HistSample::iron));
            House_Income(s, p, h, pl.factor_684, &Player::iron);
            return;
        case GoldMine:
            if (level_due && CanUpgrade(pl, h, MineCost(s, h.field_065))) {
                StartUpgrade(pl, h, MineCost(s, h.field_065), kMineTime[h.field_065]);
            }
            House_Staff(s, p, h, true);
            House_Income(s, p, h, static_cast<double>(pl.gold_factor) * pl.factor_684, &Player::gold);
            return;
        case Sawmill:  // sawmill
            House_Staff(s, p, h, HistoryFell(pl, &HistSample::wood));
            House_Income(s, p, h, pl.factor_664, &Player::wood);
            return;
        case OilWell: {  // oil well: no workers inside
            if (pl.field_9b2b3b != 0 && s.tick % 1000 == h.num && h.field_065 < 5 && helped && !s.plus_rules &&
                CanUpgrade(pl, h, OilCost(s, h.field_065))) {
                StartUpgrade(pl, h, OilCost(s, h.field_065), kMineTime[h.field_065]);
            }
            const double v = static_cast<double>(pl.factor_644) * h.field_070;
            pl.oil = v + pl.oil;
            pl.oil_stat = v + pl.oil_stat;
            if (pl.oil < 0.0) pl.oil = 0.0;
            pl.mined[4] = v + pl.mined[4];  // OilMined
            return;
        }
        case Dwelling:  // the dwelling house (type 9): a level up when the units fill the cap
            if (pl.field_9b2b3b == 0 || h.field_065 >= 5 || s.plus_rules) return;
            if (s.tick % 1000 != h.num) return;
            if (!helped) return;
            if (pl.units_alive < static_cast<std::int32_t>(pl.unit_cap)) return;

            if (CanUpgrade(pl, h, kDwellCost[h.field_065])) {
                StartUpgrade(pl, h, kDwellCost[h.field_065], kDwellTime[h.field_065]);
            }
            return;
        case Fortified: House_Fortified(s, p, h); return;
        case Farm: House_Farm(s, p, h); return;
        case RepairAircraft: House_RepairNear(s, p, h, true); return;
        case RepairVehicles: House_RepairNear(s, p, h, false); return;
        case Heal: House_Heal(s, p, h); return;               // hospital, first aid post
        case RepairShips: Port_RepairShips(s, p, h); return;  // port
        case None: return;
    }
}


void House_ResearchDone(GameState& s, std::uint8_t p, House& h) {
    Player& pl = s.players[p];
    s.flag_8e836e = 1;  // "a research finished" (mission condition 9)
    h.field_06c = {};
    h.field_06a = 0;
    if (h.field_064 == 2) {
        Unported(s, orig::kHouseLevelStep);
        return;
    }
    const int c = h.field_064 - 3;
    switch (h.type) {
        case 0:
            ++h.field_065;
            if (c < 0 || c > 5) return;  // a debug message only
            if (c == 0) {
                pl.power_made = static_cast<float>(static_cast<double>(h.field_070) * 0.15 + pl.power_made);
                h.field_070 = static_cast<float>(static_cast<double>(h.field_070) * 1.15);
            } else {
                pl.power_made = static_cast<float>(static_cast<double>(h.field_070) * 0.2 + pl.power_made);
                h.field_070 = static_cast<float>(static_cast<double>(h.field_070) * 1.2);
            }
            return;
        case 10:
        case 11:
        case 13:
            ++h.field_065;
            if (c < 0 || c > 5) return;
            // Entente Engine Plus: x1.3, x1.6, then levels 3 to 6 add instead of multiply (House_PlusNextOutput)
            if (s.plus_rules) {
                h.field_070 = House_PlusNextOutput(s, p, h, c);
                return;
            }
            h.field_070 = static_cast<float>(static_cast<double>(h.field_070) * (c == 0         ? 1.25
                                                                                 : h.type == 13 ? 1.35
                                                                                                : 1.5));
            return;
        case 12:  // Entente Engine Plus, O4: the sawmill's level, as a mine's (the original's table has nothing here)
            if (!s.plus_rules) return;
            ++h.field_065;
            if (c < 0 || c > 5) return;
            h.field_070 = House_PlusNextOutput(s, p, h, c);
            return;
        case 1:
        case 7:
            ++h.field_065;
            switch (h.field_064) {
                case 3:
                case 6: {  // MaxHP and HP + MaxHP / 2
                    const std::uint16_t half = h.max_hp >> 1;
                    h.max_hp = static_cast<std::uint16_t>(h.max_hp + half);
                    h.hp = static_cast<float>(static_cast<double>(half) + h.hp);
                    return;
                }
                case 4: h.field_080 = static_cast<float>(static_cast<double>(h.field_080) * 1.3f); return;
                case 5: h.field_074 = static_cast<std::uint8_t>(h.field_074 + 2); return;
                default: return;  // a debug message only
            }
        case 0x15:
            ++h.field_065;
            switch (h.field_064) {
                case 3: h.field_08c = static_cast<std::uint8_t>(h.field_08c + 5); return;
                case 4: h.field_074 = static_cast<std::uint8_t>(h.field_074 + 2); return;
                case 5: h.field_080 = static_cast<float>(static_cast<double>(h.field_080) * 0.5 + h.field_080); return;
                case 6: h.field_078 = static_cast<float>(h.field_078 - static_cast<double>(h.field_078) * 0.35); return;
                default: return;
            }
        case 3:
            if (s.plus_rules) return House_BuildAcademyDone(s, p);
            ++s.house_types[pl.nation % 6][3].field_2ac;
            return;
        case 5:  // the Military Academy: ApplyUpgrade(h, id - 2) (movzx cx, cl; sub ecx, 2: 16 bits)
            ApplyUpgrade(s, p, h, static_cast<std::uint16_t>(h.field_064 - 2));
            return;
        case 9: {
            ++h.field_065;
            if (c < 0 || c > 5) return;                                   // a debug message only
            constexpr double kGrow[6] = {1.15, 1.2, 1.3, 1.4, 1.5, 1.6};
            pl.unit_cap = UnitCap_Sub(s, pl.unit_cap, static_cast<std::uint16_t>(Ftol(h.field_070)));
            const double v = static_cast<double>(h.field_070) * kGrow[c];
            h.field_070 = static_cast<float>(v);
            // The original adds ftol of the unrounded double (fst) but takes off ftol of the stored float later: 100 x
            // 1.15 adds 114 and takes off 115. Ours adds what it will take off (docs/bugs.md B310, the owner's yes
            // 2026-10-22); original_rules keeps the slip.
            const double add = s.original_rules ? v : static_cast<double>(h.field_070);
            pl.unit_cap = UnitCap_Add(s, pl.unit_cap, static_cast<std::uint16_t>(Ftol(add)));
            return;
        }
        case 2: {
            // Entente Engine Plus: Deep drilling, the Military Academy's k27, researched here (kPlusDeepDrilling)
            if (s.plus_rules && c == kPlusDeepDrilling)
                return ApplyUpgrade(s, p, h, static_cast<std::uint16_t>(kPlusDeepDrilling + 1));
            if (c < 0 || c >= 20) {
                Unported(s, orig::kHouseResearchDone);  // an id past the per-player flag rows
                return;
            }
            pl.research_9b39f0[static_cast<std::size_t>(c)] = 1;
            pl.research_9b3950[static_cast<std::size_t>(c)] = 1;

            if (c <= 11) pl.research_flags[static_cast<std::size_t>(c)] = 1, pl.research_flags[c + 12u] = 1;
            auto scale = [](float& v, double k) { v = static_cast<float>(static_cast<double>(v) * k); };
            // Entente Engine Plus (docs/plus-balance.md 5): r3 to r5, the building durability, x1.25 each (x1.95 in
            // all); r19, Steam sawmills, wood x1.35 (the factor the original has and never raises; x1.5 until the top
            // end retune of 2026-10-23 late, which also takes the mining researches r10 and r11 from x1.5 to x1.35).
            if (s.plus_rules && c >= 3 && c <= 5) return scale(pl.factor_724, 1.25);
            if (s.plus_rules && c == 19) return scale(pl.factor_664, kPlusMiningResearch);
            switch (c) {
                case 0: scale(pl.factor_744, 1.35); return;
                case 1: scale(pl.factor_744, 1.15); return;
                case 2: scale(pl.factor_744, 1.5); return;
                case 3: scale(pl.factor_724, 1.1); return;
                case 4: scale(pl.factor_724, 1.2); return;
                case 5: scale(pl.factor_724, 1.15); return;
                case 6: scale(pl.factor_744, 2.0); return;                                        // fadd st0, st0
                case 7: scale(pl.factor_744, static_cast<double>(3.0f)); return;
                case 8: scale(pl.power_6e4, 1.15); return;
                case 9: scale(pl.power_6e4, 1.2); return;
                case 10:
                case 11: scale(pl.factor_684, s.plus_rules ? kPlusMiningResearch : 1.5); return;
                case 15: scale(pl.factor_9e28fc, static_cast<double>(0.75f)); return;
                case 16:  // ReloadTime[p] of the light and heavy tank (types 11, 12) * 0.85f, truncated
                    for (const std::uint16_t ty : {std::uint16_t{11}, std::uint16_t{12}}) {
                        std::uint16_t& r = s.unit_types[pl.nation][ty].reload[p];
                        r = static_cast<std::uint16_t>(Ftol(static_cast<double>(r) * static_cast<double>(0.85f)));
                    }
                    return;
                case 12: return;  // the gate for 13 and 14
                case 13:  // a special shell (repeatable: both bytes back to 0); Entente Engine Plus: ten (section 5)
                    pl.special_shells += s.plus_rules ? 10 : 1;
                    pl.research_9b39f0[13] = 0, pl.research_9b3950[13] = 0;
                    return;
                case 14: scale(pl.factor_9b32f8, static_cast<double>(1.2f)); return;
                case 17:  // "delousing" (repeatable): both bytes back to 0, the units' HP to MaxHP
                    pl.research_9b39f0[17] = 0, pl.research_9b3950[17] = 0;
                    // Entente Engine Plus, F6: the next Delousing costs twice as much (Research_Price)
                    if (s.plus_rules && pl.plus_delousing < 255) ++pl.plus_delousing;
                    Research_HealAll(s, p);
                    return;
                case 18: Research_Vaccinate(s, p); return;  // "vaccination": MaxHP * 1.1 and HP with it
                default: return;                            // 19: the flags only
            }
        }
        case 14:
            pl.mint_done = 1;
            if (pl.houses_done[14] != 0) pl.gold_factor = Mint_Factor(s);  // Plus: x1.5
            return;
        case 4:
        case 6:
        case 8:
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20: return;  // the jump table has nothing for these types
        default: return;  // types past 0x15: ja past the table, nothing
    }
}

}  // namespace

namespace {


bool BuildAcademyGarrison(std::uint16_t type) { return type == 1 || type == 7 || type == 0x15; }
}  // namespace


// research id 2, which nothing starts (it stays Unported in House_ResearchDone). Entente Engine Plus, F7 (docs/bugs.md
// B303): the Building Academy's research done, by the player's level instead of the type's, on the player's houses
// instead of the houses of one type (the original's loop takes the type of the house that researched, here the




// every level (nothing to do), and the level + 1. The original's loop also walks destroyed houses; ours skips them.
void House_BuildAcademyDone(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    switch (pl.plus_build_level) {
        case 0:
            for (House& h : pl.houses) {
                if (h.is_destroyed == 0 && BuildAcademyGarrison(h.type))
                    h.capacity = static_cast<std::uint16_t>(h.capacity << 1);
            }
            break;
        case 1: break;  // House_Durability reads the level
        case 2:
            for (House& h : pl.houses) {
                if (h.is_destroyed != 0) continue;
                h.max_hp = static_cast<std::uint16_t>((h.max_hp >> 1) + h.max_hp);
                h.hp = static_cast<float>(static_cast<double>(h.hp) * static_cast<double>(1.5f));
            }
            break;
        default: break;
    }
    if (pl.plus_build_level < 3) ++pl.plus_build_level;
}

bool House_BuildAcademyBusy(const Player& pl) {
    for (const House& h : pl.houses) {
        if (h.type == 3 && h.is_destroyed == 0 && (h.field_05c != 0 || h.field_06a != 0)) return true;
    }
    return false;
}

std::uint16_t House_TypeMaxHp(const GameState& s, std::uint8_t p, const House& h) {
    const std::uint16_t m = House_Type(s, p, h).max_hp;
    if (!s.plus_rules || s.players[p].plus_build_level < 3) return m;
    return static_cast<std::uint16_t>(Ftol(static_cast<double>(m) * 1.5));
}

void House_PlusNew(const GameState& s, std::uint8_t p, House& h) {
    if (!s.plus_rules || p >= s.players.size()) return;
    const std::uint8_t level = s.players[p].plus_build_level;
    if (level >= 1 && BuildAcademyGarrison(h.type)) h.capacity = static_cast<std::uint16_t>(h.capacity << 1);
    if (level >= 3) {
        h.max_hp = House_TypeMaxHp(s, p, h);
        h.hp = static_cast<float>(h.max_hp);
    }
}






std::uint8_t House_BuildStage(const HouseType& ht, std::uint16_t work_left) {
    const std::uint16_t n = ht.field_1b1;
    if (n == 0) return 0;  // the original divides by zero (no shipped type has 0 stages)
    const auto q = static_cast<std::uint16_t>(ht.work_rate / n);
    std::uint32_t v = 0;
    if (q >= work_left) {
        v = n - 1u;
    } else if (2u * q >= work_left) {
        v = n - 2u;
    } else if (3u * q >= work_left) {
        v = n - 3u;
    } else if (4u * q >= work_left) {
        v = n - 4u;
    } else {
        return 0;  // (q >> 6) << 8: the callers keep the low byte
    }
    return static_cast<std::uint16_t>(v) < n ? static_cast<std::uint8_t>(v) : 0;
}

void House_SetGraphic(House& h, std::uint8_t phase, std::uint8_t stage) {
    h.graphic_phase = phase, h.graphic_stage = stage;
}




void House_ReleaseUnit(GameState& s, std::uint8_t p, House& h, Unit& u) {
    if (u.work != 3 || h.units_inside == 0) return;
    u.work = 0xff;
    Player& pl = s.players[p];
    const auto slot = static_cast<std::uint16_t>(&h - pl.houses.data());
    for (std::uint8_t i = 0; i < h.units_inside && i < h.inside.size(); ++i) {
        const UnitRef r = h.inside[i];
        if (Unit_Find(s, r) != &u) continue;
        u.field_006 = 1;
        u.field_142 = 0;
        u.final_x = h.exit_x, u.final_y = h.exit_y;
        u.dest_x = h.exit_x, u.dest_y = h.exit_y;
        u.x = h.x, u.y = h.y;
        u.field_050 = h.x, u.field_054 = h.y;
        u.field_0f8 = 1;
        u.in_house = true, u.house_player = p, u.house_slot = slot;
        u.has_attack_unit = false, u.player_unit_attack = 0xff;
        u.player_house_attack = 0xff, u.has_house_attack = false;
        TUnit_ComputeVelocity(s, r.player, u);
        TUnit_SetOrientToDest(s, r.player, u);
        u.orient = u.target_orient;
        TUnit_SetMoveAnimation(s, r.player, u);
        u.path.clear();
        u.is_stopped = 0;
        u.work = 9;
        h.inside.erase(h.inside.begin() + i);
        --h.units_inside;
        u.field_0bc = 1;
        u.waypoints[0] = Unit::Waypoint{h.rally_x, h.rally_y, 0xff, 0xffff};
        break;
    }
    if (h.units_inside == 0) h.inside.clear();
}



void House_FreeDeadUnit(GameState& s, Unit& u) {
    if (!u.in_house || u.house_player >= s.players.size() || u.house_slot >= s.players[u.house_player].houses.size()) {
        Unported(s, orig::kFreeDeadUnitFromHouse);  // a unit in Work 3 without a house: the original reads a null house
        return;
    }
    House& h = s.players[u.house_player].houses[u.house_slot];
    if (h.units_inside == 0) return;
    for (std::uint8_t i = 0; i < h.units_inside && i < h.inside.size(); ++i) {
        if (Unit_Find(s, h.inside[i]) != &u) continue;
        h.inside.erase(h.inside.begin() + i);
        --h.units_inside;
        u.work = 0xff;
        break;
    }
    if (h.units_inside == 0) h.inside.clear();
}

namespace {




void Farm_DecayFields(GameState& s, std::uint8_t nation, const House& h) {
    const Map& m = s.map;
    const std::int32_t W = m.width, H = m.height;
    const HouseFootprint fp = s.house_footprints[nation % 6][h.type % 25];
    Cell row{h.map_x, h.map_y + 6};
    for (std::uint16_t r = 0; r < static_cast<std::uint16_t>(fp.b + 6); ++r) {
        Cell c = row;
        for (std::uint16_t k = 0; k < static_cast<std::uint16_t>(fp.a + 6); ++k) {
            if (c.x > 3 && c.y > 9 && c.x < W - 3 && c.y < H - 9) {
                const std::uint16_t o = m.objects[static_cast<std::size_t>(W) * static_cast<std::size_t>(c.y) +
                                                  static_cast<std::size_t>(c.x)];
                if (o > 0x25dd && o < 0x25e4) {
                    s.decaying_fields.push_back(static_cast<std::uint32_t>(static_cast<std::uint16_t>(c.x)) |
                                                static_cast<std::uint32_t>(static_cast<std::uint16_t>(c.y)) << 16);
                }
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
}

}  // namespace


void House_DestroyCallback(GameState& s, std::uint8_t p, House& h) {
    Player& pl = s.players[p];
    switch (h.type) {
        case 0:
            pl.power_made = static_cast<float>(
                static_cast<double>(pl.power_made) -
                static_cast<double>(static_cast<float>(static_cast<std::uint16_t>(Ftol(h.field_070)))));
            break;
        case 6:
        case 9:
            pl.unit_cap = UnitCap_Sub(s, pl.unit_cap, static_cast<std::uint16_t>(Ftol(h.field_070)));
            break;
        case 14:
            if (--pl.mint_count == 0) pl.gold_factor = 1.0f;
            break;
        case 15: Farm_DecayFields(s, House_Nation(s, p, h), h); break;
        case 22: Port_Destroyed(s, p, h); break;
        default: break;
    }
}





void House_OnDestroyed(GameState& s, std::uint8_t p, House& h, std::uint8_t killer) {
    Player& pl = s.players[p];
    const HouseType& ht = House_Type(s, p, h);
    const HouseFootprint fp = House_Footprint(s, p, h);
    Map& m = s.map;
    const std::int32_t W = m.width, H = m.height;
    ControlGroup_RemoveHouse(pl, static_cast<std::uint16_t>(&h - pl.houses.data()));
    if (h.is_destroyed == 0 || !(h.hp <= -10000.0f)) {  // (hp < -10000) == (hp == -10000): neither holds
        Event_HouseSound(s, p, h, 4, 8);
        h.is_destroyed = 1;
        h.hp = -1.0f;
        // The fire effects at the fire points, one after another (delay 3 k): R % 3 picks the picture (a game RNG
        // read for a picture only; docs/render.md 14). hc: the house cell's height, 0 off the map.
        const std::int32_t hc = static_cast<std::uint32_t>(h.map_x) < static_cast<std::uint32_t>(W) &&
                                        static_cast<std::uint32_t>(h.map_y) < static_cast<std::uint32_t>(H)
                                    ? m.heights[static_cast<std::size_t>(h.map_y) * W + h.map_x]
                                    : 0;
        for (std::uint8_t i = 0; i < ht.field_1f2; ++i) {
            static constexpr std::uint8_t kFire[3] = {4, 18, 20};
            const std::uint16_t r = Rng_Next(s);
            const std::size_t k = i < 10 ? i : 9;  // 10 points in the record
            const std::int32_t x = ht.fire_x[k] - ht.field_140 + Ftol(h.x);
            const std::int32_t y = ht.fire_y[k] - ht.field_142 + Ftol(h.y) - hc;
            if (SimEvent::EffectArgs* e = Event_Effect(s, 3, false, x, y, hc, kFire[r % 3])) {
                e->delay = static_cast<std::uint16_t>(3 * i);
            }
        }
        for (std::uint32_t n = h.units_inside; n != 0; --n) {
            Unit* w = h.inside.empty() ? nullptr : Unit_Find(s, h.inside[0]);
            if (w == nullptr) {
                Unported(s, orig::kOnHouseDestroyed);  // a listed unit the port cannot find
                break;
            }
            const std::uint8_t wp = h.inside[0].player;
            const UnitType& wt = Unit_Type(s, wp, *w);
            const std::uint16_t mh = wt.max_hp[wp];
            if (mh > 6) w->hp = static_cast<float>(mh % (mh - 5) + 4);
            House_ReleaseUnit(s, p, h, *w);
            if (wt.is_air != 0) Air_TakeOff(s, wp, *w, p, h);
        }
        if (h.field_060 > 0.0f) {
            if (std::uint8_t* b = Research_FlagByte(pl, h.field_06c)) *b = 0;
        }
        h.selected = 0;
        if (ht.field_1b0 < 2 || h.field_020 != 0) {
            h.hp = -10001.0f;
            House_OnDestroyed(s, p, h, 0xff);  // straight to the removal
        } else {
            if (h.type < 25) --pl.houses_done[h.type];
            House_DestroyCallback(s, p, h);
            pl.power_used = static_cast<float>(static_cast<double>(pl.power_used) - static_cast<double>(ht.power_use));
            House_SetGraphic(h, 2, 0);
        }
        h.field_020 = 0;
        if (killer != 0xff) ++s.players[killer].houses_destroyed;
        ++pl.houses_lost;
        return;
    }


    h.selected = 0;
    h.hp = -150001.0f;

    Cell row{h.map_x, h.map_y};
    for (std::uint8_t a = 0; a < fp.a; ++a) {
        Cell c = row;
        for (std::uint8_t b = 0; b < fp.b; ++b) {
            const auto idx = static_cast<std::uint32_t>(W * c.y + c.x);
            if (idx < m.objects.size()) {
                if (h.type == 0x16 && !Obstacle_UnderCell(m, c.x, c.y)) m.heights[idx] = 0x3c;
                m.objects[idx] = 0, m.pass_land[idx] = 0, m.pass_large[idx] = 0;
            }
            c = Cell_Step(c, 4);
        }
        row = Cell_Step(row, 1);
    }
    PathGrid_UpdateRegion(s, h.map_x, h.map_y - fp.b, fp.b + 3);
    auto inside = [&](Cell c) { return c.x >= 0 && c.y >= 0 && c.x < W && c.y < H; };
    auto at = [&](Cell c) { return static_cast<std::size_t>(W * c.y + c.x); };
    const Cell hc{h.map_x, h.map_y};
    switch (h.type) {
        case 10:
            if (inside(hc)) m.objects[at(hc)] = 0x2583;
            break;
        case 11:
            if (inside(hc)) m.objects[at(hc)] = 0x2582;
            break;
        case 13:
            if (inside(hc)) m.objects[at(hc)] = 0x2581;
            break;
        case 15: {  // the farm's area: objects 100 go
            Cell r{h.map_x, h.map_y + 6};
            for (std::int32_t i = 0; i < fp.b + 6; ++i) {
                Cell c = r;
                for (std::int32_t j = 0; j < fp.a + 6; ++j) {
                    if (c.x > 3 && c.y > 9 && c.x < W - 3 && c.y < H - 9 && m.objects[at(c)] == 100) {
                        m.objects[at(c)] = 0;
                    }
                    c = Cell_Step(c, 1);
                }
                r = Cell_Step(r, 4);
            }
            break;
        }
        case 19:
        case 20: {  // 19: 2 steps in direction 2, 2 rows, objects 99 go; 20: 6 steps, 6 rows, objects 100
            const int n = h.type == 19 ? 2 : 6;
            const std::uint16_t o = h.type == 19 ? 99 : 100;
            Cell r = hc;
            for (int i = 0; i < n; ++i) r = Cell_Step(r, 2);
            for (int i = 0; i < n; ++i) {
                Cell c = r;
                for (std::int32_t j = 0; j < fp.a; ++j) {
                    if (inside(c) && m.objects[at(c)] == o) m.objects[at(c)] = 0;
                    c = Cell_Step(c, 1);
                }
                r = Cell_Step(r, 4);
            }
            break;
        }
        default: break;
    }
}

void TStrateg_UpdateHouses(GameState& s, std::uint8_t p) {
    Player& pl = s.players[p];
    const std::size_t n = pl.houses.size();
    for (std::size_t k = 0; k < n; ++k) {
        House& h = pl.houses[k];
        const HouseType& ht = House_Type(s, p, h);
        if (h.is_destroyed == 0) Grid_PushHouse(s, p, static_cast<std::uint16_t>(k));
        if (h.is_destroyed != 0) {
            if (h.hp < -149000.0f) continue;
            const bool was_low = ht.field_1b0 >= 2 && h.hp < -500.0f;
            h.hp = static_cast<float>(static_cast<double>(h.hp) - (h.type == 0x16 ? 100.0 : 5.0));
            if (ht.field_1b0 >= 2 && h.hp < -500.0f && !was_low) House_SetGraphic(h, 2, 1);  // the burning ruin
            if (h.hp <= -10000.0f) House_OnDestroyed(s, p, h, 0xff);                         // the ruin is removed
            continue;
        }
        if (h.field_020 != 0) continue;  // under construction
        const auto half = static_cast<std::int32_t>(h.max_hp >> 1);
        if (static_cast<float>(half) > h.hp) {
            const double v = static_cast<double>(h.hp) - static_cast<double>(0.1f);
            h.hp = static_cast<float>(v);
            if (v < 1.0) {
                h.hp = -10.0f;
                House_OnDestroyed(s, p, h, 0xff);
                continue;
            }
        }
        if (h.field_0b4 != 0) --h.field_0b4;
        if (!House_PayUpkeep(s, p, h)) continue;
        House_TypeCallback(s, p, h);

        const auto bx = static_cast<std::uint16_t>(Ftol(static_cast<double>(pl.power_used) / pl.power_6c4));
        const auto bp = static_cast<std::uint16_t>(Ftol(static_cast<double>(pl.power_6e4) * pl.power_made));
        if (h.field_05c != 0) {
            if (bx > bp) {
                h.field_060 = static_cast<float>(static_cast<double>(h.field_060) - static_cast<double>(bp) / bx);
            } else {
                h.field_060 = static_cast<float>(static_cast<double>(h.field_060) - 1.0);
            }
            if (h.field_060 <= 0.0f) {
                h.field_05c = 0;
                House_ResearchDone(s, p, h);
            }
        }
        if (h.queue_count != 0) {
            if (h.type == 0x16 && !h.has_field_0b0) Port_StartShip(s, p, static_cast<std::uint16_t>(k));
            if (ht.power_use > 0.01) {
                double rate = static_cast<double>(bp) / bx;  // 0/0 is NaN on the x87 (masked), kept as is
                if (rate < static_cast<double>(0.3f)) rate = static_cast<double>(0.3f);
                h.field_044 = static_cast<float>(static_cast<double>(h.field_044) - (bx > bp ? rate : 1.0));
            } else {
                h.field_044 = static_cast<float>(static_cast<double>(h.field_044) - 1.0);
            }
            if (h.type == 0x16 && h.has_field_0b0) Port_ShipStage(s, p, static_cast<std::uint16_t>(k));
            if (h.field_044 <= 0.0f) {
                if (static_cast<std::int32_t>(pl.unit_cap) <= pl.units_alive || !House_DoorFree(s, h)) {
                    h.field_044 = 1.0f;
                    continue;
                }
                if (h.has_field_0b0) Port_ReleaseShip(s, p, h);
                House_ProduceUnit(s, p, static_cast<std::uint16_t>(k));
            }
        }



        House& hk = pl.houses[k];
        for (std::uint16_t i = 0; i < hk.units_inside; ++i) {
            Unit* u = i < hk.inside.size() ? Unit_Find(s, hk.inside[i]) : nullptr;
            if (u == nullptr) {
                Unported(s, orig::kHouseReleaseUnit);  // a listed unit the port cannot find
                break;
            }
            if (!u->in_group && u->work == 3 && u->in_house && u->house_player == p && u->house_slot == k) continue;
            u->work = 3;
            House_ReleaseUnit(s, p, hk, *u);
        }
    }
}



void Order_Train(GameState& s, std::uint8_t p, std::uint16_t house, std::uint16_t type, std::uint16_t count) {
    if (p >= s.players.size()) return;
    const std::uint8_t nation = House_TrainNation(s, p, house);  // B263
    if (count == 0xffff) {
        if (!Player_CanAfford(s, p, type, nation)) Event_TrainRefused(s, p);
        if (Player_CanAfford(s, p, type, nation)) {
            Player_PayForUnit(s, p, type, nation);
            House_Enqueue(s, p, house, type, true);
        }
        return;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!Player_CanAfford(s, p, type, nation)) Event_TrainRefused(s, p);
        if (!Player_CanAfford(s, p, type, nation)) break;
        Player_PayForUnit(s, p, type, nation);
        House_Enqueue(s, p, house, type, false);
    }
}





House CreateHouse(const GameState& s, std::int32_t x, std::int32_t y, std::uint16_t type, std::uint8_t nation) {
    const HouseType& ht = s.house_types[nation % 6][type % 25];
    House h;
    h.map_x = x, h.map_y = y, h.type = type;
    h.max_hp = ht.max_hp;
    h.capacity = ht.field_14c;
    h.field_030 = 1;
    h.field_020 = ht.work_rate;
    h.hp = static_cast<float>(h.max_hp);
    auto u2f = [](std::int32_t v) {
        return static_cast<float>(v < 0 ? static_cast<double>(v) + 4294967296.0 : static_cast<double>(v));
    };
    h.x = u2f(x * 0x60 - ht.anchor_x + ((y & 1) != 0 ? 0x30 : 0) + ht.field_140);
    h.y = u2f((y * 3 + 3) * 8 - ht.anchor_y + ht.field_142);
    h.exit_x = static_cast<float>(static_cast<double>(ht.exit_x - ht.field_140) + h.x);
    const double ey = static_cast<double>(ht.exit_y - ht.field_142) + h.y;
    h.exit_y = static_cast<float>(ey);
    const auto dy = static_cast<float>(ey - h.y);
    const auto dx = static_cast<float>(static_cast<double>(h.exit_x) - h.x);
    const double len = std::sqrt(0.0 * 0.0 + static_cast<double>(dy) * dy + static_cast<double>(dx) * dx);
    const double inv = 1.0 / len;
    const auto rx = static_cast<float>(dx * inv * 100.0);
    const auto ry = static_cast<float>(static_cast<double>(static_cast<float>(inv * dy)) * 100.0);
    h.rally_x = static_cast<float>(static_cast<double>(rx) + h.exit_x);
    h.rally_y = static_cast<float>(static_cast<double>(ry) + h.exit_y);
    h.field_070 = std::bit_cast<float>(ht.field_190);
    h.field_078 = h.field_07c = 20.0f;
    h.field_074 = 1;
    h.field_080 = type == 0x16 ? 1000.0f : type == 0x14 ? 1500.0f : 700.0f;
    h.field_084 = 0xff;
    h.field_08c = type == 0x15 ? 18 : 13;
    return h;
}

namespace {





std::vector<std::array<std::int32_t, 2>> House_FreeCellsAround(const GameState& s, std::uint8_t p, const House& h) {
    const Map& m = s.map;
    const HouseFootprint fp = House_Footprint(s, p, h);
    const auto id = static_cast<std::uint16_t>((p + 1) * 1000 + h.num);
    const std::int32_t W = m.width, H = m.height, a = fp.a, b = fp.b;
    std::vector<std::array<std::int32_t, 2>> out;
    Cell row{h.map_x, h.map_y + 2};
    for (std::int32_t j = 0; j < b + 2; ++j) {
        Cell c = row;
        for (std::int32_t i = 0; i < a + 2; ++i) {
            if (c.x > 3 && c.y > 9 && c.x < W - 3 && c.y < H - 9) {
                const auto idx = static_cast<std::uint32_t>(W * c.y + c.x);
                if (m.pass_land[idx] == 0) {
                    const std::uint16_t o = idx < m.objects.size() ? m.objects[idx] : 0;  // else logged, 0
                    if (o != id) {
                        std::int32_t px = (c.x * 2 - (~c.y & 1)) * 0x30, py = c.y * 0x18;
                        if (i == 0) {
                            if (j == 0) {
                                px += 0x30, py -= 0x14;
                            } else {
                                px += 0x4a;
                            }
                        } else if (i == a + 1) {
                            if (j != 0 && j == b + 1) {
                                px += 0x30, py += 0x14;
                            } else {
                                px += 5;
                            }
                        } else if (j == b + 1) {
                            py += 0x13, px += 0x30;
                        } else if (j == 0) {
                            px += 4;
                        }
                        out.push_back({px, py});
                    }
                }
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
    return out;
}



std::array<std::int32_t, 2> NearestPoint(const std::vector<std::array<std::int32_t, 2>>& list, std::int32_t x,
                                         std::int32_t y) {
    if (list.empty()) return {0, 0};
    std::array<std::int32_t, 2> best = list[0];
    std::int32_t d = 10000;
    for (const auto& q : list) {
        const auto dx = static_cast<std::uint32_t>(std::abs(q[0] - x)),
                   dy = static_cast<std::uint32_t>(std::abs(q[1] - y));
        const auto e = static_cast<std::int32_t>(dx * dx + dy * dy * 4);  // int arithmetic, wraps as in the original
        if (e < d) d = e, best = q;
    }
    return best;
}



// the original kills them from an uninitialised array (counted as unported). Each listed unit of a type that may




// knows it only for the build click, whose command carries it (`selected`; docs/building.md 3).
void House_ClearCell(GameState& s, std::uint8_t hp, std::uint16_t slot, std::int32_t x, std::int32_t y,
                     std::span<const UnitRef> selected) {
    const Map& m = s.map;
    if (x < 0 || y < 0 || x >= m.width || y >= m.height) return;
    const std::vector<UnitRef> list =
        m.cell_units[static_cast<std::size_t>(m.width) * static_cast<std::size_t>(y) + static_cast<std::size_t>(x)];
    if (list.empty()) return;
    const std::vector<std::array<std::int32_t, 2>> free = House_FreeCellsAround(s, hp, s.players[hp].houses[slot]);
    if (free.empty()) {
        Unported(s, orig::kHouseClearCell);  // OnUnitKilled on stack garbage
        return;
    }
    for (const UnitRef& r : list) {
        Unit* pu = Unit_Find(s, r);
        if (pu == nullptr) continue;
        Unit& u = *pu;
        if (!StandsOnSite(Unit_Type(s, r.player, u), u.type) || u.is_deleted != 0 || u.work == 3) continue;
        const std::array<std::int32_t, 2> n = NearestPoint(free, Ftol(u.x), Ftol(u.y));
        u.has_attack_unit = false, u.player_unit_attack = 0xff;
        u.player_house_attack = 0xff, u.has_house_attack = false;
        TUnit_StopMoving(s, r.player, u);
        u.final_x = static_cast<float>(n[0]), u.final_y = static_cast<float>(n[1]);
        u.field_050 = u.x, u.field_054 = u.y;
        u.field_0f8 = 1;
        u.dest_x = u.final_x, u.dest_y = u.final_y;
        TUnit_SetOrientToDest(s, r.player, u);
        u.orient = u.target_orient;
        u.num_work = 0;
        TUnit_ComputeVelocity(s, r.player, u);
        TUnit_SetMoveAnimation(s, r.player, u);
        u.work = 9;
        u.in_house = true, u.house_player = hp, u.house_slot = slot;
        u.is_stopped = 0;
        u.field_107 = 0;
        u.field_160 = 0;
        const auto same = [&r](const UnitRef& q) {
            return q.player == r.player && q.id == r.id && q.serial == r.serial;
        };
        if (u.type == 0 && std::any_of(selected.begin(), selected.end(), same)) {
            const House& h = s.players[hp].houses[slot];
            u.field_0bc = 1;
            u.waypoints[0] = Unit::Waypoint{h.x, h.y, 0xff, 0xffff};
        }
    }
}



void Sawmill_ClearTrees(GameState& s, const House& h) {
    Map& m = s.map;
    const std::int32_t W = m.width, H = m.height;
    std::int32_t x0 = h.map_x - 5, y0 = h.map_y - 10, x1 = h.map_x + 5, y1 = h.map_y + 10;
    auto clamp = [&](std::int32_t& cx, std::int32_t& cy) {
        if (cx <= 3) cx = 4;
        if (cy <= 9) cy = 10;
        if (cx >= W - 3) cx = W - 4;
        if (cy >= H - 9) cy = H - 10;
    };
    clamp(x0, y0);
    clamp(x1, y1);
    for (std::int32_t cx = x0; cx < x1; ++cx) {
        for (std::int32_t cy = y0; cy < y1; ++cy) {
            const bool on = static_cast<std::uint16_t>(cx) < W && static_cast<std::uint16_t>(cy) < H;
            const std::size_t i =
                on ? static_cast<std::size_t>(W) * static_cast<std::uint16_t>(cy) + static_cast<std::uint16_t>(cx) : 0;
            const std::uint16_t o = on ? m.objects[i] : 0;  // off the map: logged, 0
            if (Rng_Next(s) < 16000 && o > 8999 && o < 0x251c && on) m.objects[i] = 0;
        }
    }
}




void Farm_ClearFields(GameState& s, const House& h, std::uint16_t id, HouseFootprint fp) {
    Map& m = s.map;
    const std::int32_t W = m.width, H = m.height;
    Cell row{h.map_x, h.map_y + 6};
    for (int r = 0; r < fp.b + 6; ++r) {
        Cell c = row;
        for (int k = 0; k < fp.a + 6; ++k) {
            if (c.x >= 4 && c.y >= 10 && c.x < W - 3 && c.y < H - 9) {
                const std::size_t i =
                    static_cast<std::size_t>(W) * static_cast<std::size_t>(c.y) + static_cast<std::size_t>(c.x);
                const std::uint16_t o = m.objects[i];
                if (o != id && o > 0x25dd && o < 0x25e4) m.objects[i] = 100;
            }
            c = Cell_Step(c, 1);
        }
        row = Cell_Step(row, 4);
    }
}

}  // namespace







void Terrain_Foundation(GameState& s, const House& h, HouseFootprint fp, std::uint16_t id) {
    Map& m = s.map;
    const std::uint32_t W = m.width, total = W * m.height;
    const int a = fp.a, b = fp.b;
    Cell row = Cell_Step(Cell{h.map_x, h.map_y}, 6);
    for (int o = 0; o <= a + 1; ++o) {
        Cell c = row;
        for (int i = 0; i <= b + 1; ++i) {
            const std::uint32_t idx = W * static_cast<std::uint32_t>(c.y) + static_cast<std::uint32_t>(c.x);
            if (idx < total) {
                TerrainCell spare;  // a state without a tile layer (hand-made tests) still draws
                TerrainCell& t = idx < m.terrain.size() ? m.terrain[idx] : spare;
                const std::uint16_t rim = static_cast<std::uint16_t>((t.tile == 0 ? 1 : t.tile) | 0x8000);
                const bool o_in = o >= 1 && o <= a, i_in = i >= 1 && i <= b;
                if (o_in && i_in) {
                    if (o == 1 || o == a || i == 1 || i == b) {
                        t.tile = static_cast<std::uint16_t>(id | 0x8000);
                        t.sub.fill(id);
                    } else {
                        t.tile = id;
                    }
                } else if (i_in) {  // o = 0 or a + 1
                    t.tile = rim;
                    for (int k : o == 0 ? std::array<int, 3>{2, 5, 8} : std::array<int, 3>{0, 3, 6}) t.sub[k] = id;
                } else if (o_in) {  // i = 0 or b + 1
                    t.tile = rim;
                    for (int k : i == 0 ? std::array<int, 3>{0, 1, 2} : std::array<int, 3>{6, 7, 8}) t.sub[k] = id;
                } else {  // a corner
                    const int sub = o == 0 ? (i == 0 ? 2 : 8) : (i == 0 ? 0 : 6);
                    const int chance = (o == 0) == (i == 0) ? 50 : 20;
                    t.tile = rim;
                    if (Crt_rand(s) % 100 < chance) t.sub[sub] = id;
                }
            }
            c = Cell_Step(c, 4);
        }
        row = Cell_Step(row, 1);
    }
}






std::uint16_t AddHouseToPlayer(GameState& s, std::uint8_t p, House h, std::span<const UnitRef> selected) {
    Player& pl = s.players[p];
    Map& m = s.map;
    h.num = static_cast<std::uint16_t>(pl.houses.size());
    const auto id = static_cast<std::uint16_t>((p + 1) * 1000 + h.num);
    const HouseFootprint fp = s.house_footprints[House_Nation(s, p, h) % 6][h.type % 25];
    pl.houses.push_back(h);
    const std::uint16_t slot = h.num;
    switch (h.type) {
        case 0xd:
        case 0xb:
        case 0x15:
        case 0x12:
        case 10:
        case 7:
        case 1:
        case 0x18:
        case 0x16: break;
        default: Terrain_Foundation(s, h, fp, House_Type(s, p, h).field_2ae != 0 ? 0x88 : 0x34);
    }
    if (h.type == 0x16) pl.houses[slot].field_0ac = Port_ShipSide(s, h.map_x, h.map_y, fp);
    House_PlaceOnMap(s, p, slot, House_Nation(s, p, h), h.type, h.map_x, h.map_y, pl.nation);
    Cell row{h.map_x, h.map_y};
    for (int a = 0; a < fp.a; ++a) {
        Cell c = row;
        for (int b = 0; b < fp.b; ++b) {
            House_ClearCell(s, p, slot, c.x, c.y, selected);
            c = Cell_Step(c, 4);
        }
        row = Cell_Step(row, 1);
    }
    switch (h.type) {
        case 0xc: Sawmill_ClearTrees(s, pl.houses[slot]); break;
        case 0xf: Farm_ClearFields(s, pl.houses[slot], id, fp); break;
        case 0x16: Port_SetDoor(s, House_Nation(s, p, h), pl.houses[slot]); break;
        default: break;
    }
    PathGrid_UpdateRegion(s, h.map_x, h.map_y, fp.a + fp.b);
    CoarseWater_RebuildAroundHouse(s, h.map_x, h.map_y, fp);
    (void)m;
    if (pl.control_type == 1) {
        for (std::size_t q = 0; q < s.players.size(); ++q) {
            Player& o = s.players[q];
            if (o.is_lost != 0 || o.control_type != 1) continue;
            for (House& r : o.houses) {
                if (r.is_destroyed != 0) continue;
                if (r.type == 0x16) {
                    CoarseWater_FindWaterNear(s, r.rally_x, r.rally_y);
                    continue;
                }
                Cell c = WorldToCell(r.rally_x, r.rally_y);
                if (!Cell_Acceptable(s, c)) {
                    const Cell n = RelocateTargetLand(s, c);
                    r.rally_x = static_cast<float>((2 * n.x - (~n.y & 1)) * 0x30 + 0x30);
                    r.rally_y = static_cast<float>(n.y * 0x18);
                }
            }
        }
        if (h.type == 0xf) {
            const House& nh = pl.houses[slot];
            Farm_SetRally(s, p, 6, static_cast<float>(static_cast<double>(nh.exit_x) + 40.0f),
                          static_cast<float>(static_cast<double>(nh.exit_y) + 40.0f), false);
        }
    }
    ++pl.houses_built;
    return slot;
}






std::uint16_t House_Place(GameState& s, std::uint8_t p, float x, float y, std::uint16_t type) {
    Player& pl = s.players[p];
    const Cell c = PickCell(s.map, static_cast<float>(static_cast<double>(x) + static_cast<double>(2.0f)), y);
    House h = CreateHouse(s, c.x, c.y, type, pl.nation);
    House_PlusNew(s, p, h);
    h.num = static_cast<std::uint16_t>(pl.houses.size());
    const std::uint16_t slot = h.num;
    pl.houses.push_back(h);
    House_PlaceOnMap(s, p, slot, pl.nation, type, c.x, c.y);
    ++pl.houses_built;
    return slot;
}




bool PayForHouse(GameState& s, std::uint8_t p, std::uint16_t k) {
    Player& pl = s.players[p];
    const HouseType& ht = s.house_types[pl.nation % 6][pl.houses[k].type % 25];  // the owner's nation, not Basic
    // ht.cost: Food, Wood, Iron, Gold, Oil
    static constexpr int kCheck[5] = {3, 0, 1, 2, 4};
    static constexpr double Player::* kStockOf[5] = {&Player::food, &Player::wood, &Player::iron, &Player::gold,
                                                     &Player::oil};
    static constexpr double Player::* kStatOf[5] = {&Player::food_stat, &Player::wood_stat, &Player::iron_stat,
                                                    &Player::gold_stat, &Player::oil_stat};
    for (int c : kCheck) {
        const float cost = ht.cost[static_cast<std::size_t>(c)];
        if (cost > 1.0f && static_cast<double>(cost) > pl.*kStockOf[c]) return false;
    }
    static constexpr int kPay[5] = {3, 1, 0, 2, 4};
    for (int c : kPay) {
        const double cost = ht.cost[static_cast<std::size_t>(c)];
        pl.*kStockOf[c] = pl.*kStockOf[c] - cost;
        pl.*kStatOf[c] = pl.*kStatOf[c] - cost;
        if (pl.*kStockOf[c] < 0.0) pl.*kStockOf[c] = 0.0;
    }
    return true;
}


void House_SetRally(House& h, float x, float y) { h.rally_x = x, h.rally_y = y; }

// ---- The house panel's orders (docs/interface.md B.8, slice I6) ----

namespace {



void Player_RefundUnit(GameState& s, std::uint8_t p, std::uint16_t type, std::uint8_t nation) {
    Player& pl = s.players[p];
    const UnitType& t = s.unit_types[nation][type % 40];
    constexpr Stock order[5] = {
        {&Player::gold, 3}, {&Player::wood, 1}, {&Player::food, 0}, {&Player::iron, 2}, {&Player::oil, 4}};
    for (const Stock& r : order) {
        const double c = t.cost[static_cast<std::size_t>(r.cost)];
        double& v = pl.*r.field;
        v = c + v;
        double& st = pl.*StatOf(r.field);
        st = c + st;
        if (v < 0.0) v = 0.0;
    }
}

}  // namespace







void House_CancelTrain(GameState& s, std::uint8_t p, std::uint16_t k, std::uint16_t type, bool refund) {
    if (p >= s.players.size() || k >= s.players[p].houses.size()) return;
    Player& pl = s.players[p];
    House& h = pl.houses[k];
    const std::uint8_t nation = House_TrainNation(s, p, k);  // B263
    auto power = [&](std::uint16_t t, double sign) {
        pl.power_used = static_cast<float>(static_cast<double>(pl.power_used) +
                                           sign * static_cast<double>(s.unit_types[nation][t % 40].cost[5]));
    };
    const std::uint16_t qc = h.queue_count;
    if (qc == 0 || h.queue.empty()) return;
    if ((qc == 1 && h.queue[0] == type) || qc == 0xffff) {
        if (qc == 1 && h.has_field_0b0) Port_CancelShip(s, h, true);  // the port's ship in the making goes
        h.queue.clear();
        h.queue_count = 0;
        power(type, -1.0);
        if (refund) Player_RefundUnit(s, p, type, nation);
        return;
    }
    for (std::uint16_t i = static_cast<std::uint16_t>(qc - 1); i >= 1 && i < h.queue.size(); --i) {
        if (h.queue[i] != type) continue;
        h.queue.erase(h.queue.begin() + i);
        --h.queue_count;
        if (refund) Player_RefundUnit(s, p, type, nation);
        return;
    }
    if (h.queue[0] != type) return;
    h.queue.erase(h.queue.begin());
    --h.queue_count;
    if (h.has_field_0b0) Port_CancelShip(s, h, false);  // the port's ship in the making goes
    power(type, -1.0);
    h.field_044 = static_cast<float>(s.unit_types[nation][h.queue[0] % 40].build_time[p]);
    if (!s.original_rules) power(h.queue[0], 1.0);  // docs/bugs.md B16
    if (refund) Player_RefundUnit(s, p, type, nation);
}


void Order_CancelTrain(GameState& s, std::uint8_t p, std::uint16_t k, std::uint16_t type, std::uint8_t count) {
    for (std::uint32_t i = 0; i < count; ++i) House_CancelTrain(s, p, k, type, true);
}



void House_FireOn(GameState& s, House& h) {
    h.field_030 = 1;
    for (std::size_t i = 0; i < h.units_inside && i < h.inside.size(); ++i) {
        Unit* u = Unit_Find(s, h.inside[i]);
        if (u == nullptr) {
            Unported(s, orig::kHouseMayFire);  // a listed unit the port cannot find
            continue;
        }
        u->field_006 = 1;
        u->field_142 = 0;
        MarkZone(s, h.inside[i].player, u->x, u->y, 0x1e);
    }
}



void House_FireOff(GameState& s, House& h) {
    h.field_030 = 0;
    for (std::size_t i = 0; i < h.units_inside && i < h.inside.size(); ++i) {
        const UnitRef r = h.inside[i];
        Unit* u = Unit_Find(s, r);
        if (u == nullptr) {
            Unported(s, orig::kHouseHoldFire);  // a listed unit the port cannot find
            continue;
        }
        u->field_006 = 0;
        TUnit_AttackEnd(s, r.player, *u);
        u->field_142 = 0;
    }
}



std::array<std::int32_t, 5> House_SellRefund(const GameState& s, std::uint8_t p, const House& h) {
    const HouseType& ht = House_Type(s, p, h);
    std::array<std::int32_t, 5> out{};
    if (ht.work_rate == 0) return out;        // a type without work is never under construction
    constexpr int kCol[5] = {3, 0, 1, 2, 4};  // ht.cost: Food, Wood, Iron, Gold, Oil
    const double left = h.field_020, work = ht.work_rate;
    for (std::size_t i = 0; i < 5; ++i) {
        out[i] = Ftol(left * static_cast<double>(ht.cost[static_cast<std::size_t>(kCol[i])]) / work);
    }
    return out;
}




void House_Sell(GameState& s, std::uint8_t p, House& h, const std::array<std::int32_t, 5>& refund) {
    if (h.field_020 == 0) return;
    Player& pl = s.players[p];
    constexpr double Player::* kStock[5] = {&Player::gold, &Player::food, &Player::wood, &Player::iron, &Player::oil};
    for (std::size_t i = 0; i < 5; ++i) {
        double& v = pl.*kStock[i];
        v = static_cast<double>(refund[i]) + v;
        pl.*StatOf(kStock[i]) = static_cast<double>(refund[i]) + pl.*StatOf(kStock[i]);
        if (v < 0.0) v = 0.0;
    }
    House_OnDestroyed(s, p, h, 0xff);
}



void House_Sow(GameState& s, std::uint8_t p, House& h) {
    House_PlantFields(s, p, h);
    Player& pl = s.players[p];
    pl.gold = pl.gold - 100.0;
    pl.gold_stat = pl.gold_stat - 100.0;
    if (pl.gold < 0.0) pl.gold = 0.0;
}



void House_ReleaseAircraft(GameState& s, std::uint8_t p, House& h, std::uint8_t up, Unit& u) {
    if (u.work != 3) return;
    House_ReleaseUnit(s, p, h, u);
    Air_TakeOff(s, up, u, p, h);
}

// The time of an upgrade window's row (the tables next to House_UpgradeCostRow), 0 when there is none.
std::uint16_t House_UpgradeTime(std::uint16_t type, std::uint16_t row) {
    switch (type) {
        case 0: return row < 6 ? kHqTime[row] : 0;
        case 1:
        case 7: return row < 4 ? kBunkerTime[row] : 0;
        case 9: return row < 6 ? kDwellTime[row] : 0;
        case 10:
        case 11:
        case 13: return row < 6 ? kMineTime[row] : 0;
        case 12: return row < 6 ? kMineTime[row] : 0;         // Plus O4, the sawmill (the original has no window)
        case 3: return row < 3 ? kBuildAcademyTime[row] : 0;
        case 14: return row == 0 ? 500 : 0;
        case 0x15: return row < 4 ? kTowerTime[row] : 0;
        default: return 0;
    }
}

}  // namespace antanta::sim
