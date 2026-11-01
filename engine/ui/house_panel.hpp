

// (name, nation, defense, inside, health, the train row with its queue bar, Set_Exit, the research bar, the attack
// types for new units) or the house under construction (its progress and Cancel), and the parts of each type: the city
// centre's Workers and Diplomacy, the barn's sow button, the hangar's aircraft, the mint, and the upgrade buttons of
// the power station, the dwelling, the mines, the oil rig, the pillboxes and the watch tower, the academies' research
// buttons and the market's trade panel (slice I7, docs/interface.md C). What a click does comes back as the same
// sim::Command the recorder writes (or the app's release_house and market_auto inputs).
//
// The city centre's Diplomacy button asks the app for the diplomacy window (slice I8). Cancelling the ship a port
// builds is not ported in the sim: a right click on a port's train button while a ship is being built says so and sends
// nothing.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "sim/market.hpp"
#include "sim/state.hpp"
#include "ui/hud.hpp"
#include "ui/panel.hpp"
#include "ui/unit_panel.hpp"

namespace antanta::ui {

// The actions of the house panel's buttons (PanelWidget::action), apart from the unit panel's.
enum HousePanelAction : int {
    kHpDestroy = 100,
    kHpCancelBuild,
    kHpReleaseUnit,
    kHpFireOn,
    kHpFireOff,
    kHpTrain,
    kHpUntrain,
    kHpSetExit,
    kHpMode,
    kHpWorkers,
    kHpSow,
    kHpReleaseAircraft,
    kHpUpgrade,
    kHpUpgradeCancel,
    kHpMint,
    kHpResearch,
    kHpResearchCancel,
    kHpMarketSell,
    kHpMarketUnsell,
    kHpMarketBuy,
    kHpMarketClear,
    kHpMarketExchange,
    kHpMarketSend,
    kHpMarketAuto,
    kHpMarketTarget,
    kHpDiplomacy,
    kHpStrike,           // arg: the strike (sim::StrikeId): Entente Engine Plus's call-in strike, the app's target mode
};






struct MarketUi {
    sim::MarketAmounts sell{};
    std::uint8_t buy = 0;
    float receive = 0;
    std::uint8_t row = 0;
    std::vector<std::string> names;
};


float market_step(bool shift, bool ctrl);


std::vector<std::uint8_t> market_players(const sim::GameState& s, const MarketUi& m);



std::optional<sim::HouseRef> house_panel_house(const sim::GameState& s, const Selection& sel);



int house_panel_width(std::uint16_t type);


std::string house_title(const sim::GameState& s, const StringTable& strings, const sim::HouseRef& h);



std::string train_tooltip(const sim::GameState& s, std::uint8_t nation, std::uint16_t type, const StringTable& strings);


std::string append_cost_text(std::string text, const std::int32_t* row, const StringTable& strings);

// Entente Engine Plus (docs/plus-balance.md F1): the Military Academy research ids whose effect there differs from the
// game's IDS_UPACADWARHELP<id> (0x7 +15 % rifle attack, 0x12 the howitzers' rate, 0x14 cannon and howitzer, 0x17 tanks
// -30 %, 0x1a ships). Under plus_rules their tooltip is our text plus_research_key(id) (engine/lang/*/ui.txt) when
// `strings` holds it; the app puts our texts of these keys into the panel's table.
inline constexpr std::array<int, 5> kPlusResearchTexts = {0x7, 0x12, 0x14, 0x17, 0x1a};
std::string plus_research_key(int id);  // "research.plus_war<id>", the id in decimal as the game's keys
// The same for the items Plus merges or adds (docs/plus-balance.md 5): k0 Marksmanship, k3 Officer school, k9 Gas
// masks, k15 Remounts, k26 Steel helmets, k27 Deep drilling, and the items of the freed rows k1 Light machine gun, k8
// Conscription, k11 Cavalry shock tactics and k16 Fire direction staff (the call-in strikes' unlock) (ids 1, 4, 10, 16,
// 27, 28, 2, 9, 12, 17), and the Academy of Sciences' r3 to r5 (buildings +25 % each), r10 and r11 (mining +35 %,
// 2026-10-23 late), r13 (ten gas shells), r17 Delousing (its price rises) and r19 Steam sawmills, plus_science_key(row)
// "research.plus_sci<row>" next to the game's IDS_UPACADNAUKHELP<row>.
inline constexpr std::array<int, 10> kPlusNewResearchTexts = {1, 4, 10, 16, 27, 28, 2, 9, 12, 17};
inline constexpr std::array<int, 8> kPlusScienceTexts = {3, 4, 5, 10, 11, 13, 17, 19};
std::string plus_science_key(int row);
// Entente Engine Plus, F7 (docs/bugs.md B303): the effect of the Building Academy's research row 0 to 2,
// "research.plus_build<row>", shown in brackets after the game's IDS_UPGRADEBASICHELP<row> without its price.
std::string build_academy_key(int row);
// Entente Engine Plus (docs/plus-balance.md 3, "the top end", 2026-10-23 late): our text of an upgrade level of an iron
// or gold mine, sawmill or oil well, "" for other houses: "research.plus_mine<level>", "research.plus_saw<level>" or
// "research.plus_oil<level>" for levels 1 and 2 (+30 %, +60 %; the original's sawmill has no levels, its mines and oil
// well say +25 % and +50 % or +35 %), "research.plus_level_more" for levels 3 to 6 (+50 % of the starting output).
std::string plus_upgrade_key(std::uint16_t type, int level);
// Every key of our texts the panels read from their table under Plus (the research keys above, plus.upkeep,
// plus.reload_slow, plus.reload_slow_why, plus.market_price and the strikes' strike.*): the app copies them in
// (engine/lang/*/ui.txt).
std::vector<std::string> plus_panel_keys();
// The status line of your economic advisor under Entente Engine Plus (docs/plus-balance.md 6 and 11), from
// Player::plus_status: what its last pick was and why ("Researching: Seed selection. Pays back in 4 min"). Empty for
// Classic, with the advisor or its "Makes upgrades" off, and before its first pick (the status is not saved).
std::string plus_advisor_status(const sim::GameState& s, std::uint8_t player, const StringTable& strings);

// The house panel (empty widgets when house_panel_house gives none): panel window (w + 190) x 200 at (10, H - 213).
// `market`: the market panel's amounts (none: all 0).
Panel house_panel(const sim::GameState& s, const Selection& sel, const StringTable& strings,
                  const MarketUi* market = nullptr);




std::vector<sim::UnitRef> idle_workers(const sim::GameState& s, std::uint8_t player);




struct HousePanelResult {
    std::vector<sim::Command> order;
    std::string what;
    std::string unported;  // set: tell the player this is not in the new engine yet, nothing is sent
    bool set_exit = false;
    std::vector<std::string> messages;
    bool diplomacy = false;  // open the diplomacy window (slice I8)
    int strike = -1;         // Entente Engine Plus: the strike (sim::StrikeId) whose target the next map click picks
};
HousePanelResult house_panel_click(const sim::GameState& s, Selection& sel, PanelClick click, bool shift, bool ctrl,
                                   MarketUi* market = nullptr, const StringTable* strings = nullptr);
// Entente Engine Plus: the plus_strike order of the player (sim/strike.hpp) for the target mode's world points (a
// one-point strike passes its point twice).
sim::Command strike_order(std::uint8_t player, int strike, std::int32_t x, std::int32_t y, std::int32_t x2,
                          std::int32_t y2);





std::optional<std::array<float, 2>> rally_flag_point(const sim::GameState& s, const Selection& sel);



struct FlagPhase {
    float value = 0;
    double ms = 0;  // real time not yet turned into steps
    void frame();
    void advance(double elapsed_ms, double period_ms);
    int sprite() const { return static_cast<int>(value) & 7; }
};

}  // namespace antanta::ui
