// The lockstep rules every machine of a network game applies the same way (docs/multiplayer.md): which orders a
// player may give, how one tick's orders of all players become the commands of step(), and the trace of a network
// game. Pure functions of their arguments, so the server, every client and a headless replay of the server's log agree.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "net/protocol.hpp"
#include "sim/state.hpp"

namespace antanta::net {

// The most commands one group may hold (a ui_order with one child per selected unit, and the facing order).
inline constexpr std::size_t kMaxGroupCommands = 4096;

// Checks one player's group and makes it that player's: every command a kind the game's interface sends, its acting
// player p (the player argument, the house argument's player, Command::player), its unit list and listed houses p's;
// a ui_order's children only the child kinds of the recorder's table. Sets Command::player and the units' player to p.
// False (and why) when the group must be dropped: units, houses or groups of another player are never ordered.
bool Group_Authorize(OrderGroup& g, std::uint8_t p, std::string* why);

// A group whose first command is a ui_order: it takes the sim's next order number, so the server puts it into a tick
// whose frame has nothing before it but select_houses (which takes no number).
bool Group_IsUi(const OrderGroup& g);
// A group of select_houses only.
bool Group_IsSelect(const OrderGroup& g);
// Slice MP9: the server's own group of one net_control order (the computer takes a player over or gives it back).
bool Group_IsControl(const OrderGroup& g);
OrderGroup Group_Control(std::uint8_t player, std::uint8_t level);

// The commands of one tick for step() (without the view inputs, which each machine makes itself and puts first): the
// frame's groups in their order, numbered as ui::take_tick_orders (ui/hud.cpp) numbers one player's: a ui_order takes
// s.order_seq + 1 and its children the numbers after it, the other commands 0 (the sim numbers them). A ui_order that
// is not first in its frame (a server would not send one) is left out, the same on every machine.
std::vector<sim::Command> Lockstep_Commands(const sim::GameState& s, const std::vector<OrderGroup>& frame);

// One tick of a network game's trace: the lines antanta-headless writes (O, D, C, T, P, U, B), with the T line's
// MyPlayer as "-" (each machine has its own) and the G line carrying GameState_NetHash, so every machine of the game
// and a replay of the server's log write the same bytes.
std::string NetTrace_Tick(const sim::GameState& s, std::uint32_t enter);

// The build id a client and the server compare at the hello: the protocol's version, the sim's hash version and a hash
// of the simulation's code (engine/sim and engine/formats, written at build time by scripts/sim-hash.cmake), so two
// builds with different rules refuse each other at the join instead of running out of step. The compiler does not
// matter, and the same sources give the same id in any folder. The second form takes the code hash (the tests').
std::string Net_BuildId();
std::string Net_BuildId(const std::string& sim_code_hash);

// The server's order log and back: first the game's setup (`S <hex>`, the start message's bytes), for a resumed game
// (slice MP9) then the save it resumed (`R <tick> <hex>`, our own save after that tick), then one line per command
// (`N <tick> <player> <group> <O line>`). A tick without orders has no line. *resume (may be null) gets the save's
// bytes, empty for a game that started at tick 0.
std::string NetLog_Start(const sim::SkirmishSetup& setup);
std::string NetLog_Resume(std::uint32_t tick, const std::string& save);
std::string NetLog_Frame(std::uint32_t tick, const std::vector<OrderGroup>& frame);
bool NetLog_Read(const std::string& text, sim::SkirmishSetup* setup,
                 std::vector<std::pair<std::uint32_t, std::vector<OrderGroup>>>* frames, std::string* error,
                 std::string* resume = nullptr);

}  // namespace antanta::net
