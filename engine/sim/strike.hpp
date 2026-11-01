



#pragma once

#include <array>
#include <cstdint>

#include "state.hpp"

namespace antanta::sim {

// The strikes, in Player::plus_strike_ready's order and the PlusStrike command's strike argument.
enum StrikeId : std::uint8_t {
    kStrikeBarrage,   // artillery barrage: heavy shells at random within a circle
    kStrikeCreeping,  // creeping barrage: shells along a line from the first point to the second
    kStrikeGas,       // gas attack: a few gas shells within a circle (needs the gas research, Academy of Sciences r12)
    kStrikeBombing,   // bombing run: bombs one after another along a line
    kStrikeRecon,     // recon flight: the fog lifts over a circle for a while, for the caller only
    kStrikeCount,
};

// One strike's numbers (first guesses, docs/decisions.md 34: to tune after play).
struct StrikeInfo {
    std::uint8_t shell_kind = 0;  // the shell kind table's row (sim/shell.cpp kShellKinds); unused by the recon flight
    std::uint8_t shells = 0;      // how many
    std::int32_t damage = 0;      // each shell's damage (0: the kind's own)
    std::uint16_t span = 0;       // ticks from the first impact to the last (the recon flight: how long it reveals)
    std::int32_t radius = 0;      // the circle's radius or the line's half width in pixels (recon: cells)
    std::int32_t length = 0;      // the longest line in pixels (two-point strikes), else 0
    bool gas = false;             // gas shells
    bool two_points = false;      // the order names a start and an end point
    std::array<std::int32_t, 5> cost{};  // Food, Wood, Iron, Gold, Oil
    std::uint32_t cooldown = 0;          // ticks until the player may call this strike again
};
const StrikeInfo& Strike_Info(std::uint8_t strike);

// The Military Academy row that unlocks the strikes under Plus (k16, freed by the merge into k15 Remounts), its price
// (Research_CostRow) and time (Research_Time) are in sim/research.cpp.
inline constexpr std::uint16_t kStrikeUnlockRow = 16;

// Why a strike cannot be called now (the town centre's buttons show it).
enum class StrikeRefusal : std::uint8_t { None, NotPlus, Locked, NoGas, NoTownCentre, Cooling, Short };
StrikeRefusal Strike_Check(const GameState& s, std::uint8_t p, std::uint8_t strike);
// Ticks until the strike may be called again (0: now).
std::uint32_t Strike_Wait(const GameState& s, std::uint8_t p, std::uint8_t strike);

// The PlusStrike order: checks (Strike_Check), pays, starts the cooldown and sends the shells (or the recon flight).
// (x, y) is the target's world point, (x2, y2) the end point of a two-point strike. False and nothing changed when
// refused or the target is off the map.
bool Strike_Call(GameState& s, std::uint8_t p, std::uint8_t strike, std::int32_t x, std::int32_t y, std::int32_t x2,
                 std::int32_t y2);

// The computers and the military advisor (sim/tick.cpp, every 500 ticks at (tick + 50 p + 250) % 500 == 0): the
// enemy units the player's units and houses see, in blocks of 8 x 16 cells (as Plus_Sight); a block's score is the
// enemies' food-equivalent (doubled within 10 cells of an own house) less three times the own and allied units' there;
// a strike is called on the best block when the score reaches its food-equivalent price and the reserve rule
// (Plus_Budget) passes; ties go to the lower block. Easy never; Medium and the advisor only on blocks near their own
// houses; Difficult anywhere. Without the unlock, the first such block makes it research k16 instead. No random number.
void Strike_Brain(GameState& s, std::uint8_t p);

}  // namespace antanta::sim
