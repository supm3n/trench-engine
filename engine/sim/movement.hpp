// Functions shared by the movement port (docs/movement.md): map cells, pathfinding, unit movement, orders.
// Internal to sim/; the public entry points are in state.hpp.
#pragma once

#include <array>
#include <cstdint>
#include <span>

#include "state.hpp"

namespace antanta::sim {


inline double Player::* StatOf(double Player::* stock) {
    if (stock == &Player::gold) return &Player::gold_stat;
    if (stock == &Player::wood) return &Player::wood_stat;
    if (stock == &Player::iron) return &Player::iron_stat;
    if (stock == &Player::oil) return &Player::oil_stat;
    return &Player::food_stat;
}

// The index of a stock in StartRes and the *Mined doubles (Gold, Food, Wood, Iron, Oil).
inline std::size_t MinedIndex(double Player::* stock) {
    if (stock == &Player::gold) return 0;
    if (stock == &Player::food) return 1;
    if (stock == &Player::wood) return 2;
    if (stock == &Player::iron) return 3;
    return 4;
}

struct Cell {
    std::int32_t x = 0, y = 0;
};

// Direction tables (docs/movement.md 4.1), index = direction code 0..8.
extern const std::int32_t kDirCellEven[9][2];
extern const std::int32_t kDirCellOdd[9][2];
extern const std::int32_t kDirWorld[9][2];
extern const std::uint8_t kOpposite[9];
extern const std::uint16_t kStepCost[9];

// sim/map.cpp
std::int32_t Ftol(double v);
std::uint16_t Rng_Next(GameState& s);
Cell WorldToCell(float x, float y);
Cell PickCell(const Map& m, float x, float y);
Cell CellToWorldPoint(const Map& m, Cell c);
Cell Cell_Step(Cell c, std::uint8_t dir);
// TUnit::Basic: g_BasicUnit[type + nation * 40]. Inline: called millions of times a tick.
inline const UnitType& Unit_Type(const GameState& s, std::uint8_t player, const Unit& u) {
    return s.unit_types[Unit_Nation(s, player, u)][u.type];
}
UnitRef Unit_Ref(std::uint8_t player, const Unit& u);
Unit* Unit_FindById(GameState& s, std::uint8_t player, std::uint16_t id);
void Cell_RemoveUnit(GameState& s, std::uint8_t player, Unit& u);

void Cell_ListRemove(GameState& s, std::uint8_t player, Unit& u);
void TUnit_SetCell(GameState& s, std::uint8_t player, Unit& u, Cell c);
void TUnit_UpdateGroundHeight(GameState& s, std::uint8_t player, Unit& u);
std::int32_t GroundHeightAt(const GameState& s, float x, float y);
bool IsLowBridgeCell(const GameState& s, Cell c);
void TUnit_UpdateCellIfMoved(GameState& s, std::uint8_t player, Unit& u);
void TUnit_ResyncCell(GameState& s, std::uint8_t player, Unit& u);
void TUnit_SlopeSpeedScale(GameState& s, Unit& u);

// sim/pathfind.cpp
bool CellInside(const GameState& s, Cell c);  // 3 < x < W-3 and 9 < y < H-9
bool TUnit_CanStepDir(GameState& s, std::uint8_t player, const Unit& u, std::uint8_t dir);
std::uint8_t GreedyStep(Cell& c, Cell to);
Cell RelocateTargetLand(GameState& s, Cell c);
bool Cell_Acceptable(GameState& s, Cell& c);
Cell RelocateTargetLarge(GameState& s, Cell c, std::uint8_t player, const Unit& u);
bool TUnit_IsSpotFree(GameState& s, std::uint8_t player, const Unit& u);
int PathSearch(GameState& s, std::uint8_t player, Unit& u, Cell target);
int TUnit_RequestPath(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t flag);
// Our rules: a long path request of this unit uses the shared route field (a small land unit, !original_rules).
bool Flow_Usable(const GameState& s, std::uint8_t player, const Unit& u);
void PathHelper_TickRefresh(GameState& s);

// sim/flow.cpp (our rules only: the shared route fields of big orders, docs/movement.md 3.4, docs/bugs.md B88)
enum class FlowResult { Path, Unreachable, Unknown };
// The cell directions from start to goal (the PathSearch format) down the kept field of goal's block, the last cells
// by a greedy splice or a small search. Unreachable: no walk from start to goal exists. Unknown: the field gives no
// answer (a blocked goal, a start walled in, a path too long): the caller takes the original's search.
// weighted: the group wave's costs (Weight grid).
FlowResult Flow_Path(GameState& s, Cell start, Cell goal, bool weighted, std::vector<std::uint8_t>& dirs);
// Tests: the land grid and every kept field that counts as current equal ones built now (a missed Map::pass_gen
// bump or a wrong repair fails it).
bool Flow_Check(const GameState& s);
// Tests: the small search Flow_Path ends with, on its own (from c to goal over the free cells of g_PassLand inside
// the box round both, widened by 8 columns and 16 rows; the steps are appended to dirs).
bool Flow_LocalSearch(const Map& m, Cell c, Cell goal, std::vector<std::uint8_t>& dirs);

// sim/unitmove.cpp
void TUnit_ClearPath(Unit& u);
void TUnit_ClearTargets(Unit& u);
double TUnit_Speed(GameState& s, std::uint8_t player, const Unit& u, std::uint32_t caller);
void TUnit_StopMoving(GameState& s, std::uint8_t player, Unit& u);
void TUnit_ComputeVelocity(GameState& s, std::uint8_t player, Unit& u);
void TUnit_SetOrientToDest(GameState& s, std::uint8_t player, Unit& u);
void TUnit_SetMoveAnimation(GameState& s, std::uint8_t player, Unit& u);
void TUnit_MoveTo(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t flag);
void TUnit_OrderMove(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t flag);
void TUnit_OrderEnd(GameState& s, std::uint8_t player, Unit& u, std::uint8_t flag);
void EventUnitEndMove(GameState& s, std::uint8_t player, Unit& u);
void TUnit_GoToWaypoint(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t work,
                        std::uint16_t arg);
void TUnit_TurnStep(GameState& s, std::uint8_t player, Unit& u);
void TUnit_Repath(GameState& s, std::uint8_t player, Unit& u);
void TUnit_OrderXy(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint16_t object);
void TUnit_FaceTowards(GameState& s, std::uint8_t player, Unit& u, float x, float y);
void TUnit_MoveFunc(GameState& s, std::uint8_t player, Unit& u);
// Returns true when the unit was deleted (removed from its player's list).
bool MakeMoveForDead(GameState& s, std::uint8_t player, std::uint32_t slot);

// sim/pathgrid.cpp
void PathGrid_Build(GameState& s);
void PathGrid_UpdateRegion(GameState& s, std::int32_t x, std::int32_t y, std::int32_t r);
// Our rules (sim/flow.cpp): the land cell as PathGrid_Build computes it without the large-unit marks (bit 2 of
// g_CellFlags); true when free; *weight: the cell's weight before smoothing.
bool Cell_LandStatic(const GameState& s, std::int32_t x, std::int32_t y, std::uint8_t* weight);
// Our rules (docs/ai.md 10.17): the land mass of a cell from the terrain alone (water and bridges), 0 for water.
std::uint16_t Land_Region(GameState& s, std::int32_t x, std::int32_t y);
bool Cell_Obstacle(const Map& m, std::int32_t x, std::int32_t y);
bool Obstacle_UnderCell(const Map& m, std::int32_t x, std::int32_t y);
void Terrain_SetObject(GameState& s, std::int32_t x, std::int32_t y, std::uint16_t object);
void House_PlaceOnMap(GameState& s, std::uint8_t player, std::uint16_t slot, std::uint8_t nation, std::uint16_t type,
                      std::int32_t x, std::int32_t y, std::uint8_t strip_nation = 0xff);
void NeutralHouse_Place(GameState& s, std::uint8_t type, std::int32_t x, std::int32_t y);
void SpecialObjects_Load(GameState& s, std::uint16_t capacity, std::span<const SpecialObject> records);

// sim/special.cpp: the special map objects, the pontoon bridge (docs/transports.md 19)
struct PontoonCell {
    std::uint16_t x = 0, y = 0;
    std::uint8_t kind = 0x14;
};
bool Special_IsPontoon(const GameState& s, std::uint16_t object);
bool Pontoon_CanPlace(const GameState& s, std::uint8_t kind, std::uint16_t x, std::uint16_t y);
void Special_Add(GameState& s, std::uint8_t kind, std::uint16_t x, std::uint16_t y);
std::uint8_t Line_Step(Cell& c, std::int32_t tx, std::int32_t ty);

std::vector<PontoonCell> Pontoon_Line(const GameState& s, std::int32_t x0, std::int32_t y0, std::int32_t cx,
                                      std::int32_t cy);
// What act_6cf80 would do with a line (the preview and the order use the same decision): the sections it lays and,
// with our rules (!original_rules), why it lays none: a water cell of the line where no section may stand (a gap,
// docs/bugs.md B93; `refused` lists those cells) or too little wood for every section (B94).
struct PontoonPlan {
    enum class Refusal : std::uint8_t { None, Gap, Wood };
    std::vector<PontoonCell> laid;     // the sections laid, in line order (none when refused)
    std::vector<PontoonCell> would;    // refused: the sections the line would have
    std::vector<PontoonCell> refused;  // Gap: the water cells refused; Wood: the sections the wood does not pay for
    Refusal refusal = Refusal::None;
};
PontoonPlan Pontoon_Plan(const GameState& s, std::uint8_t player, std::int32_t x0, std::int32_t y0, std::int32_t cx,
                         std::int32_t cy);
void Pontoon_Lay(GameState& s, std::uint8_t player, std::int32_t x0, std::int32_t y0, std::int32_t cx,
                 std::int32_t cy);
void Special_Hit(GameState& s, std::uint16_t object, const Shell* shell);
void Worker_BuildSpecial(GameState& s, std::uint8_t player, Unit& u);

// sim/house.cpp
void TStrateg_UpdateHouses(GameState& s, std::uint8_t player);
void Order_Train(GameState& s, std::uint8_t player, std::uint16_t house, std::uint16_t type, std::uint16_t count);
void House_SetRally(House& h, float x, float y);

void Farm_SetRally(GameState& s, std::uint8_t p, std::uint16_t type, float x, float y, bool flag);

// sim/worker.cpp (workers, docs/workers.md)
bool Worker_JobSearch(GameState& s, std::uint8_t player, Unit& u);


void Worker_NextJob(GameState& s, std::uint8_t player, Unit& u);
void Worker_SendToHouse(GameState& s, std::uint8_t player, Unit& u, std::uint16_t id, std::uint16_t spot,
                        const Unit* mover = nullptr);
void Worker_Arrive(GameState& s, std::uint8_t player, Unit& u);
bool Worker_WorkStep(GameState& s, std::uint8_t player, Unit& u);  // true: the MoveFunc returns at once

std::uint8_t House_BuildStage(const HouseType& ht, std::uint16_t work_left);

void House_SetGraphic(House& h, std::uint8_t phase, std::uint8_t stage);


void House_OnDestroyed(GameState& s, std::uint8_t p, House& h, std::uint8_t killer);

void House_ReleaseUnit(GameState& s, std::uint8_t p, House& h, Unit& u);

void House_FreeDeadUnit(GameState& s, Unit& u);
void House_PlantFields(GameState& s, std::uint8_t p, const House& h);
House CreateHouse(const GameState& s, std::int32_t x, std::int32_t y, std::uint16_t type, std::uint8_t nation);



void House_PlusNew(const GameState& s, std::uint8_t p, House& h);

std::uint16_t House_TypeMaxHp(const GameState& s, std::uint8_t p, const House& h);

void House_BuildAcademyDone(GameState& s, std::uint8_t p);
// A Building Academy of the player researches now (Plus F7: one research at a time per player).
bool House_BuildAcademyBusy(const Player& pl);

std::uint16_t AddHouseToPlayer(GameState& s, std::uint8_t p, House h,
                               std::span<const UnitRef> selected = {});

std::uint16_t House_Place(GameState& s, std::uint8_t p, float x, float y, std::uint16_t type);

void House_BuiltCallback(GameState& s, std::uint8_t p, House& h);

void House_DestroyCallback(GameState& s, std::uint8_t p, House& h);
// The unit cap with `add` more places (docs/bugs.md B310): ours stops at 65,535, original_rules lets the 16-bit sum
// wrap as the original's does.
std::uint16_t UnitCap_Add(const GameState& s, std::uint16_t cap, std::uint16_t add);
// The unit cap with `sub` places less (docs/bugs.md B310, the owner's yes 2026-10-22): ours stops at 0, original_rules
// lets the 16-bit difference wrap to about 65,535 as the original's does.
std::uint16_t UnitCap_Sub(const GameState& s, std::uint16_t cap, std::uint16_t sub);
void Terrain_Foundation(GameState& s, const House& h, HouseFootprint fp, std::uint16_t id);
bool PayForHouse(GameState& s, std::uint8_t p, std::uint16_t k);
void Order_DeleteKey(GameState& s, std::uint8_t player, const UnitRef* first);
void Worker_GoToField(GameState& s, std::uint8_t player, Unit& u, float x, float y);
bool Worker_OnField(GameState& s, std::uint8_t player, const Unit& u);
void Worker_FindField(GameState& s, std::uint8_t player, Unit& u);
void Worker_HarvestStep(GameState& s, std::uint8_t player, Unit& u);
void TUnit_EnterHouseStep(GameState& s, std::uint8_t player, Unit& u);

// sim/combat.cpp (combat, docs/combat.md)
void TUnit_StartTurn(GameState& s, std::uint8_t player, Unit& u);
Unit* AttackTarget(GameState& s, const Unit& u);
House* HouseTarget(GameState& s, const Unit& u);

inline bool IsEnemy(const GameState& s, std::uint8_t a, std::uint8_t b) {
    if (a == b) return false;
    if (a == 0xff || b == 0xff) return true;
    if (a >= s.players.size() || b >= s.players.size()) return true;
    const Player& pa = s.players[a];
    const Player& pb = s.players[b];
    if (pa.is_lost != 0 || pb.is_lost != 0) return false;
    const std::uint8_t x = pb.is_ally[a], y = pa.is_ally[b];
    if (x != y) return true;
    return x == 0;
}
void MarkZone(GameState& s, std::uint8_t p, float x, float y, std::uint8_t v);
void Grid_Init(GameState& s);
void Grid_PushUnit(GameState& s, std::uint8_t player, const Unit& u);
void Grid_PushHouse(GameState& s, std::uint8_t player, std::uint16_t slot);
void Grid_TickReset(GameState& s);
void SaveOrder(Unit& u);
void FaceToPoint(GameState& s, std::uint8_t player, Unit& u, float px, float py);
void FaceAttackTarget(GameState& s, std::uint8_t player, Unit& u);
void StartFireAnim(GameState& s, std::uint8_t player, Unit& u);
bool Unit_Touches(const GameState& s, const Unit& u, std::uint16_t id, std::uint8_t n);
bool TUnit_CanGrenade(const GameState& s, std::uint8_t player, const Unit& u);
void TUnit_StartGrenade(GameState& s, std::uint8_t player, Unit& u, float px, float py);
void TUnit_StartMelee(GameState& s, std::uint8_t player, Unit& u);
bool TargetClear(GameState& s, std::uint8_t p, const Unit& t);
double ExpFactor(GameState& s, std::uint8_t player, const Unit& u);
double CalcHitDamage(GameState& s, std::uint8_t ap, const Unit& a, std::uint8_t tp, const Unit& t);
void Unit_ClearReferences(GameState& s, const UnitRef& dead);
void OnUnitKilled(GameState& s, std::uint8_t player, Unit& u, std::uint8_t killer);
bool StandsOnSite(const UnitType& ut, std::uint16_t t);
void AlertNearby(GameState& s, std::uint8_t p, std::uint16_t mx, std::uint16_t my, std::uint8_t ap, Unit& a);
void Group_Alert(GameState& s, Group& g, std::uint8_t ap, Unit* a);
void RifleShot(GameState& s, std::uint8_t ap, Unit& a, std::uint8_t tp, Unit& t);
void House_MuzzleFlash(GameState& s, std::uint8_t hp, const House& h);
void MgBurst(GameState& s, std::uint8_t ap, Unit& a, std::uint8_t tp, Unit& t);
void FlameToPoint(GameState& s, std::uint8_t ap, Unit& a, float x, float y, float r, bool any);
void CombatFireFlameThrower(GameState& s, std::uint8_t player, Unit& u);
void MeleeHitUnit(GameState& s, std::uint8_t ap, Unit& a, std::uint8_t tp, Unit& t);
void MeleeHitHouse(GameState& s, std::uint8_t ap, Unit& a);
void UnitHit(GameState& s, std::uint8_t player, Unit& u, std::uint16_t v, std::uint8_t killer, std::uint8_t flag);
void TUnit_AttackEnd(GameState& s, std::uint8_t player, Unit& u);
void TUnit_CheckTargets(GameState& s, std::uint8_t player, Unit& u);
bool TUnit_TryAttack(GameState& s, std::uint8_t player, Unit& u, std::uint8_t tp, Unit& t, float d);
void TUnit_OnAttackedBy(GameState& s, std::uint8_t player, Unit& u, std::uint8_t tp, Unit& t, std::uint8_t flag);
void TUnit_AttackHouse(GameState& s, std::uint8_t player, Unit& u, const Unit& src, std::uint8_t hp, std::uint16_t slot,
                       std::uint16_t k, std::uint8_t flag);
int TUnit_FindTarget(GameState& s, std::uint8_t player, Unit& u);
void TStrateg_AutoTarget(GameState& s, std::uint8_t player, Unit& u);

// sim/house.cpp (production)
bool Player_CanAfford(const GameState& s, std::uint8_t p, std::uint16_t type, std::uint8_t nation = 0xff);
bool Player_PayForUnit(GameState& s, std::uint8_t p, std::uint16_t type, std::uint8_t nation = 0xff);
void House_Enqueue(GameState& s, std::uint8_t p, std::uint16_t k, std::uint16_t type, bool replace);
// The house panel's orders (slice I6, docs/interface.md B.8).
void House_CancelTrain(GameState& s, std::uint8_t p, std::uint16_t k, std::uint16_t type, bool refund);
void Order_CancelTrain(GameState& s, std::uint8_t p, std::uint16_t k, std::uint16_t type, std::uint8_t count);
void House_FireOn(GameState& s, House& h);
void House_FireOff(GameState& s, House& h);
std::array<std::int32_t, 5> House_SellRefund(const GameState& s, std::uint8_t p, const House& h);
void House_Sell(GameState& s, std::uint8_t p, House& h, const std::array<std::int32_t, 5>& refund);
void House_Sow(GameState& s, std::uint8_t p, House& h);
void House_ReleaseAircraft(GameState& s, std::uint8_t p, House& h, std::uint8_t up, Unit& u);
std::uint16_t House_UpgradeTime(std::uint16_t type, std::uint16_t row);




float House_PlusNextOutput(const GameState& s, std::uint8_t p, const House& h, int step);
inline constexpr double kPlusLevel1 = 1.3, kPlusLevel2 = 1.6, kPlusLevelAdd = 0.5;
// The production researches under Entente Engine Plus (r10 and r11 for both mines, r19 Steam sawmills, k27 Deep
// drilling): each x1.35 (x1.5 until 2026-10-23 late).
inline constexpr double kPlusMiningResearch = 1.35;

float Mint_Factor(const GameState& s);

// sim/group.cpp (formation groups, docs/groups.md)
Group* Unit_Group(GameState& s, std::uint8_t player, const Unit& u);  // nullptr: no group, or one not loaded
void Group_Disband(GameState& s, Group& g, bool keep);
void AI_UpdateGroups(GameState& s, std::uint8_t player);


void UI_FacingOrder(GameState& s, const Command& c);

std::uint8_t Orient_ToPoint(float x1, float y1, float x2, float y2);
void UI_GroupCreate(GameState& s, std::uint8_t player, std::span<const UnitRef> selection);
// now: the player's right click under our rules orders every group at once, past FormationBudgetMax (B91).
void Group_Order(GameState& s, Group& g, std::uint8_t kind, std::int32_t x, std::int32_t y, std::uint8_t tplayer,
                 std::uint16_t tid, bool now = false);
void Group_RemoveMember(GameState& s, Group& g, Unit& u);
void Player_MoveUnitToFront(Player& pl, const Unit& u);
void AI_FormGroups(GameState& s, std::uint8_t player, std::vector<Unit*> list, std::uint8_t a3, std::uint8_t type,
                   std::uint8_t cols, std::uint8_t kind, std::int32_t& x, std::int32_t& y, std::uint8_t tplayer,
                   std::uint16_t tid, std::uint8_t ai_owned, bool direct, bool player_split = false,
                   std::int32_t count = -1);
void Group_DeferredCreate(GameState& s, std::uint8_t player, const CommandNode& n);
void Group_DeferredOrder(GameState& s, std::uint8_t player, const CommandNode& n);

// sim/ai.cpp (the computer player's planner, docs/ai.md)
void AI_HouseAttacked(GameState& s, std::uint8_t p, House& h);
void AI_Think(GameState& s, std::uint8_t p);
Unit* AI_FindIdle(GameState& s, std::uint8_t p, std::uint16_t type, float x, float y, float best);
std::int32_t AI_Threat(const GameState& s, std::uint8_t p, std::int32_t x, std::int32_t y, std::int32_t px,
                       std::int32_t py, std::uint8_t depth);
void AIScript_Run(GameState& s, std::uint8_t p);
void AIScript_Start(GameState& s, std::uint8_t p);


bool House_CanPlace(const GameState& s, std::uint8_t p, std::int32_t x, std::int32_t y, std::uint16_t type);



bool House_CanBuild(const GameState& s, std::uint8_t p, std::uint16_t type);
// Entente Engine Plus, A3 (docs/plus-balance.md 4): true when a Difficult computer or an advisor (any player that is
// not a computer of a lower level) must not order unit type t now: a mortar man (5) at 1 per 12 riflemen (types 2, 3,
// 14) or more, a howitzer or cannon (0x15, 0x16) at 1 gun (0x15 to 0x17) per 25 riflemen or more. Always false
// otherwise.
bool AI_MixFull(const GameState& s, std::uint8_t p, std::uint16_t t);



bool AI_Build(GameState& s, std::uint8_t p, std::uint16_t t);



void Market_Buy(GameState& s, std::uint8_t p, float tf, float tg, float ti, float tw, float to);
std::uint16_t AddHouseAtPoint(GameState& s, std::uint8_t p, float x, float y, std::uint16_t type,
                              std::span<const UnitRef> selected = {});
std::uint16_t AddHouseToPointFull(GameState& s, std::uint8_t p, float x, float y, std::uint16_t type,
                                  std::span<const UnitRef> selection);
void Order_Stop(GameState& s, std::uint8_t player, std::span<const UnitRef> selection);
void Order_GroupDismiss(GameState& s, std::uint8_t player, std::span<const UnitRef> selection);



void Order_LogMove(GameState& s, std::uint32_t parent, std::uint8_t player, const Unit& u, float x, float y,
                   std::uint8_t flag);
// Applies one top-level order (parent 0); all: every order of this tick (the children name the click kind).
void Order_Apply(GameState& s, const Command& c, std::span<const Command> all);
// Slice MP9 (docs/multiplayer.md "The computer takes over"): the net_control order. level 1..3: the computer plays the
// human player p at that level (ControlType 1, the computer's switches; the human's kept in Player::net_kept); 0: its
// human plays it again (ControlType 4, the kept values back). A player that is not a human's (a computer of the setup)
// is left as it is.
void Player_NetControl(GameState& s, std::uint8_t p, std::uint8_t level);
// Our rules (docs/bugs.md B317), the Shift-click order queue (Unit::order_queue): true when the unit has queued orders
// or something to do (walking, fighting, a resume point, a Work other than idle or a worker's field round), so a
// Shift + right click appends to its queue instead of ordering it at once.
bool Unit_RouteBusy(const Unit& u);
// In the unit's update: an idle unit takes its next queued order; the queue ends in a house, a transport or a group.
void OrderQueue_Next(GameState& s, std::uint8_t p, Unit& u);

// sim/shell.cpp (shells, docs/combat.md 9)
void Weapon_Fire(GameState& s, std::uint8_t player, Unit& o);
void Grenade_Fire(GameState& s, std::uint8_t player,
                  Unit& u);
void Shells_Update(GameState& s);


const HouseDepthMap* House_DepthMap(const GameState& s, std::uint8_t player, const House& h);
void Bomb_Fire(GameState& s, std::uint8_t player, Unit& u, std::uint16_t delay);
// Entente Engine Plus, the call-in strikes (sim/strike.cpp): one shell of player p without an owner unit, from (x0, y0,
// z0) to (x1, y1) (clamped to the map), after `delay` ticks; speed 0: the kind's. False when it has no trajectory.
bool Strike_Shell(GameState& s, std::uint8_t p, std::uint8_t kind, float x0, float y0, float z0, float x1, float y1,
                  std::int32_t damage, std::uint16_t delay, bool gas, float speed = 0.0f);



double Shell_SpreadDivisor(const GameState& s, std::uint8_t player, std::uint8_t kind);



void Effect_Damage(GameState& s, float x, float y, float z, std::int32_t dmg, std::int32_t r, std::uint8_t cc = 1,
                   bool gas = false);




void TUnit_OrderPoint(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t gas);

void Order_ApplyPoint(GameState& s, const Command& c);
void Gas_Add(GameState& s, GasCloud g);
void Particles_Update(GameState& s);

// sim/worker.cpp, sim/combat.cpp (used by the aircraft)
void PutInHouse(GameState& s, std::uint8_t player, Unit& u, std::uint16_t slot);
void BulletToPoint(GameState& s, float x, float y, std::uint8_t ap, Unit& a, bool any, float r);

// sim/air.cpp (aircraft: MoveClass 1, 11, 12; docs/movement.md 14, docs/combat.md 15)
void Air_CreateUnit(const UnitType& t, std::uint8_t player, Unit& u);
void Air_ProduceUnit(GameState& s, std::uint8_t p, std::uint16_t slot, Unit& u);
void Air_TakeOff(GameState& s, std::uint8_t up, Unit& u, std::uint8_t hp, House& h);
void Air_MoveFunc(GameState& s, std::uint8_t player, Unit& u);
void Air_OrderMove(GameState& s, std::uint8_t player, Unit& u, float x, float y);
void Air_OrderEnd(GameState& s, std::uint8_t player, Unit& u);
void Air_GoToWaypoint(Unit& u, float x, float y, std::uint8_t work, std::uint16_t arg);
void Air_OrderXy(Unit& u, float x, float y);
void Air_OnAttackedBy(GameState& s, std::uint8_t player, Unit& u, std::uint8_t tp, Unit& t);
void Air_AttackHouse(GameState& s, std::uint8_t player, Unit& u, House& h, std::uint8_t flag);
int Air_FindTarget(GameState& s, std::uint8_t player, Unit& u, bool air_only = false);
bool Air_ResumeAfterFight(GameState& s, std::uint8_t player, Unit& u);    // our rules, B277 (from AttackEnd)
void Air_OnKilled(GameState& s, std::uint8_t player, Unit& u);
bool Air_Falling(GameState& s, std::uint8_t player, std::uint32_t slot);
void Projectiles_Update(GameState& s);

// ---- MoveClass 3: vehicles (docs/movement.md "MoveClass 3: vehicles", docs/combat.md vehicle sections) ----



void LargeCells_Move(GameState& s, const Unit& u, Cell c);
void LargeCells_Leave(GameState& s, const Unit& u);
void LargeCells_Flush(GameState& s);
void TankRedirectAllArround(GameState& s, std::uint8_t player, Unit& u);
// Entente Engine Plus, E1 "supply with teeth" (docs/plus-balance.md 3, sim/tick.cpp): true on every second tick while
// the player's iron, gold or oil stock that the type's upkeep uses is below 1 (after this tick's upkeep; food and wood
// never count). The reload countdowns skip their step then, so an unpaid unit reloads in twice the time.
bool Unit_ReloadHeld(const GameState& s, std::uint8_t p, const UnitType& t);
void UnitActionVehicle(GameState& s, std::uint8_t player, Unit& u);
void MakeMoveVehicle(GameState& s, std::uint8_t player, Unit& u);
bool TUnit_ActionGate(GameState& s, std::uint8_t player, Unit& u);

void BulletToPoint(GameState& s, float x, float y, std::uint8_t ap, Unit& a, bool any, float r);


bool Cell_LargeOpen(const GameState& s, std::size_t idx);  // g_PassLarge free or g_CellFlags == 2

bool TUnit_CanEnterCell(const GameState& s, std::uint8_t player, const Unit& u, std::int32_t cell_index);

void TUnit_MoveToLarge(GameState& s, std::uint8_t player, Unit& u, Cell c, float x, float y);
void PathSearch_LargeStart(GameState& s, const Unit& u);
void ApproachPoint_LargeTarget(GameState& s, std::uint8_t player, Unit& u, float sx, float sy);

// sim/ship.cpp (ships, MoveClass 13, 14, 15: docs/movement.md "MoveClass 13, 14, 15: ships", docs/combat.md)
bool TUnit_SetOrient(Unit& u, std::uint8_t o);
std::uint32_t Port_ShipSide(const GameState& s, std::int32_t x, std::int32_t y, HouseFootprint fp);
void Port_SetDoor(GameState& s, std::uint8_t nation, House& h);
void Port_RepairShips(GameState& s, std::uint8_t p, const House& h);
void CoarseWater_Rebuild(GameState& s, const std::int32_t* rect);
void CoarseWater_Init(GameState& s);
void CoarseWater_RebuildAroundHouse(GameState& s, std::int32_t x, std::int32_t y, HouseFootprint fp);
bool CoarseWater_FindWaterNear(GameState& s, float& x, float& y);
void CoarseWater_RefreshMarks(GameState& s);
bool Water_IsFreeForShip(GameState& s, std::uint8_t k, float x, float y);
bool Ship_FindWaypoint(GameState& s, Unit& u, float& px, float& py);
bool Ship_CanMoveTo(GameState& s, const Unit& u, float x, float y);
std::uint8_t Water_CellValueAt(const GameState& s, float x, float y);
bool CoarseWater_PointFree(GameState& s, float x, float y, bool strict);


void Ship_Created(GameState& s, std::uint8_t p, Unit& u);
void Ship_SetupSprites(GameState& s, std::uint8_t player, Unit& u);
void Ship_Deleted(GameState& s, std::uint8_t p, const Unit& u);
void Ship_Arrive(GameState& s, std::uint8_t player, Unit& u);
void Ship_BeginMove(GameState& s, std::uint8_t player, Unit& u);
void Ship_Replan(GameState& s, std::uint8_t player, Unit& u);
void Ship_FixTarget(GameState& s, Unit& u);
void Ship_OrderMove(GameState& s, std::uint8_t player, Unit& u, float x, float y);
void Ship_MoveTo(GameState& s, std::uint8_t player, Unit& u, float x, float y);
void Ship_OrderEnd(GameState& s, std::uint8_t player, Unit& u);
void Ship_OrderQueued(GameState& s, std::uint8_t player, Unit& u, float x, float y, std::uint8_t work,
                      std::uint16_t arg);
void Ship_EndMove(GameState& s, std::uint8_t player, Unit& u);
void Ship_LeavePort(GameState& s, std::uint8_t player, Unit& u, const House& h);
void Ship_UpdateWeapons(GameState& s, std::uint8_t player, Unit& u);
void MakeMoveShip(GameState& s, std::uint8_t player, Unit& u);
void Ship_ReleaseSprites(GameState& s, std::uint8_t player, Unit& u);


void Port_StartShip(GameState& s, std::uint8_t p, std::uint16_t k);
void Port_ShipStage(GameState& s, std::uint8_t p, std::uint16_t k);

void Port_ReleaseShip(GameState& s, std::uint8_t p, House& h);
std::uint16_t AddUnit(GameState& s, float x, float y, std::uint8_t p, std::uint16_t type, bool force,
                      std::uint8_t own = 0xff);
void Port_Destroyed(GameState& s, std::uint8_t p, House& h);
void Port_CancelShip(GameState& s, House& h, bool clear_grid);
void Ship_OnAttackedBy(GameState& s, std::uint8_t player, Unit& u, std::uint8_t tp, Unit& t);
void Ship_AttackHouse(GameState& s, std::uint8_t player, Unit& u, const House& h);
bool Ship_FindTarget(GameState& s, std::uint8_t player, Unit& u);
void Ship_Killed(GameState& s, std::uint8_t player, Unit& u, std::uint8_t killer);
bool Ship_MoveForDead(GameState& s, std::uint8_t player, std::uint32_t slot);
bool Shell_HitShip(GameState& s, float x, float y, float z, Shell& v);

void Weapon_FireShip(GameState& s, std::uint8_t player, Unit& o, const ShipWeaponType& w, const float m[3],
                     std::uint8_t weapon = 0, std::uint8_t barrel = 0, float gun = 0.0f);
void House_ProduceUnit(GameState& s, std::uint8_t p, std::uint16_t k);


Unit CreateUnit(GameState& s, float x, float y, std::uint16_t type, std::uint8_t nation, std::uint8_t player);
Unit& AddUnitToPlayer(GameState& s, std::uint8_t p, Unit u);

void UnitType_AddGraphicRef(GameState& s, std::uint8_t nation, std::uint16_t type, std::uint8_t own = 0xff);

// Transports (sim/transport.cpp, docs/transports.md): the transport ship 0x11 and the motor vehicle 0x18.
void Unit_SetFutureTransporter(Unit& u, const UnitRef& t);
void Unit_ClearFutureTransporter(Unit& u);
Unit* Unit_FutureTransporter(GameState& s, const Unit& u);
bool Unit_MayBoard(const GameState& s, std::uint8_t player, const Unit& u, const Unit& t);
bool BoardPointShip(GameState& s, const Unit& t, float& x, float& y, std::uint8_t up, Unit* u);
void BoardPointVehicle(GameState& s, const Unit& v, float& x, float& y, const Unit& u);
bool Transport_CanUnload(GameState& s, const Unit& t);
bool Transport_Board(GameState& s, std::uint8_t up, Unit& u, std::uint8_t tp, Unit& t, bool by_player);
bool Transport_PutIn(GameState& s, std::uint8_t up, Unit& u, std::uint8_t tp, Unit& t);
bool Transport_GetOut(GameState& s, std::uint8_t up, Unit& u);

bool Transport_EndMove(GameState& s, std::uint8_t up, Unit& u);
void Transport_IdleCheck(GameState& s, Unit& u);
void UI_Board(GameState& s, std::uint8_t p, const std::vector<Unit*>& list, std::uint8_t tp, Unit& t,
              std::uint32_t parent);
void Transport_Unload(GameState& s, std::uint8_t p, std::span<const UnitRef> selection);
void Transport_OnKilled(GameState& s, std::uint8_t player, Unit& u, std::uint8_t killer);
void Transport_ClearLinks(Unit& v, const UnitRef& dead);
bool Transport_TurnToLand(GameState& s, std::uint8_t p, Unit& t);
// The O line of a board call made by a click (sim/order.cpp).
void Order_LogBoard(GameState& s, std::uint32_t parent, std::uint8_t player, const Unit& u, const UnitRef& t);



extern const std::array<std::array<std::int32_t, 6>, 20> kSciencesCost;
extern const std::array<std::uint16_t, 20> kSciencesTime;
// Entente Engine Plus: Deep drilling, the Military Academy's k27, researched in the Academy of Sciences as its row 27
// (button id 30). Its started and done bytes stay the Military Academy's research_9b3860[27] and research_9b3770[27],
// so the save format does not change; the effect is ApplyUpgrade 0x1c.
inline constexpr std::uint16_t kPlusDeepDrilling = 27;


const std::int32_t* Research_CostRow(std::uint16_t type, std::uint16_t row);
std::uint16_t Research_Time(std::uint16_t type, std::uint16_t row);
// The same under the game's rule set. Entente Engine Plus (docs/plus-balance.md 5) has its own prices (the merges,
// the repricing, r3 to r5, r16, and r8, r9, r11 with gold for most of their oil), the new rows k26 (Military Academy,
// 1000 ticks) and r19 (Academy of Sciences), stage 5's k1, k8 and k11 in rows the merges freed, and k16 Fire direction
// staff (3000 ticks, 2026-10-23), the call-in strikes' unlock (sim/strike.hpp), in the last. k27 Deep drilling is
// offered by the Academy of Sciences as its row 27 (kPlusDeepDrilling, 1000 ticks, plus-balance.md 3, 2026-10-23); the
// Military Academy's row 27 keeps its price (an old save's running research is refunded) but has time 0 there, never
// offered.
const std::int32_t* Research_CostRow(const GameState& s, std::uint16_t type, std::uint16_t row);
std::uint16_t Research_Time(const GameState& s, std::uint16_t type, std::uint16_t row);
// The price player p pays for an academy row (Food, Wood, Iron, Gold, Oil, unused): Research_CostRow(s, ...), but under
// Entente Engine Plus Delousing (Academy of Sciences row 17) doubles with each use (F6, Player::plus_delousing, at most
// x1024). False when the row has no price.
bool Research_Price(const GameState& s, std::uint8_t p, std::uint16_t type, std::uint16_t row,
                    std::array<std::int32_t, 6>& out);

std::uint8_t* Research_FlagByte(Player& pl, House::ResearchFlag f);
void Research_Start(GameState& s, std::uint8_t p, House& h, std::uint16_t time, std::uint16_t id);
void Research_Button(GameState& s, std::uint8_t p, House& h, std::uint16_t time, std::uint16_t id);
void Research_Cancel(GameState& s, std::uint8_t p, House& h, const std::array<std::int32_t, 5>& cost);
void ApplyUpgrade(GameState& s, std::uint8_t p, House& h, std::uint16_t id);
void Research_HealAll(GameState& s, std::uint8_t p);
void Research_Vaccinate(GameState& s, std::uint8_t p);
// The cost row (Food, Wood, Iron, Gold, Oil) of the upgrade windows of house types 0, 1, 7, 9, 10, 11, 13, 14 and
// 0x15 (sim/house.cpp), or nullptr.
const std::int32_t* House_UpgradeCostRow(std::uint16_t type, std::uint16_t row);
// The same under the game's rule set: Entente Engine Plus has its own mine and oil well rows (F3, docs/bugs.md B302)
// and the sawmill's six levels (type 12, O4, docs/plus-balance.md 3).
const std::int32_t* House_UpgradeCostRow(const GameState& s, std::uint16_t type, std::uint16_t row);

}  // namespace antanta::sim
