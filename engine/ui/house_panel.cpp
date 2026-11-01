#include "ui/house_panel.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

#include "sim/movement.hpp"
#include "sim/strike.hpp"
#include "ui/build_menu.hpp"
#include "ui/lang.hpp"

namespace antanta::ui {

namespace {

using sim::Command;

constexpr const char* kUpgradesAcadLes = "Data/Interface/UpgradesAcad.les";
constexpr const char* kUpgradesWarLes = "Data/Interface/UpgradesWar.les";
constexpr const char* kInterfMarketLes = "Data/Interface/InterfMarket.les";

std::string text_of(const StringTable& t, const std::string& key) {
    const auto it = t.find(key);
    return it != t.end() ? it->second : key;
}

sim::OrderArg raw(std::uint32_t v) {
    sim::OrderArg a;
    a.raw = v;
    return a;
}

sim::OrderArg house_arg(const sim::HouseRef& r) {
    sim::OrderArg a;
    a.tag = sim::OrderArg::Tag::House;
    a.house = sim::HouseRef{r.player, r.slot, 0};
    return a;
}

sim::OrderArg unit_arg(const sim::UnitRef& r) {
    sim::OrderArg a;
    a.tag = sim::OrderArg::Tag::Unit;
    a.unit = r;
    return a;
}

sim::OrderArg array_arg(const std::int32_t* v, std::size_t n) {
    sim::OrderArg a;
    a.tag = sim::OrderArg::Tag::Array;
    a.values.assign(v, v + n);
    return a;
}

int ftol(double v) { return static_cast<int>(v); }  // truncation toward zero, as ftol

PanelWidget picture(const PanelPic& pic, IRect r, int frames) {
    PanelWidget w;
    w.kind = PanelWidget::Kind::Picture;
    w.pic = pic;
    w.rect = r;
    w.frames = frames;
    return w;
}

PanelWidget label(int x, int y, const std::string& text) {
    PanelWidget w;
    w.kind = PanelWidget::Kind::Label;
    w.rect = IRect{x, y, x == 5 ? kPanelInfoW : 0, 0};  // the info column's labels have their room (panel.info)
    w.text = text;
    w.font = PanelFont::Serif16;
    return w;
}


PanelWidget button(const PanelPic& pic, int x, int y, const std::string& tooltip, char key, int action, int arg = 0) {
    PanelWidget w;
    w.kind = PanelWidget::Kind::Button;
    w.pic = pic;
    w.rect = IRect{x, y, 40, 40};
    w.tooltip = tooltip;
    w.key = key;
    w.action = action;
    w.arg = arg;
    w.font = PanelFont::Serif18;
    return w;
}

// A live bar (B.6): the picture's first frame stretched to `width` (at least 1) x `h`.
PanelWidget bar(const char* name, int x, int y, int width, int h) {
    return picture(pic_named(kInterfaceLes, name), IRect{x, y, std::max(1, width), h}, 3);
}

PanelWidget star(int x, int y) { return picture(pic_named(kInterfaceLes, "Upstar.bmp"), {x, y, 8, 8}, 3); }


int research_width(const sim::House& h) {
    if (h.field_05c == 0) return 0;
    return ftol((static_cast<double>(h.field_05c) - static_cast<double>(h.field_060)) * 40.0 /
                static_cast<double>(h.field_05c));
}




struct UpgradeKind {
    const char* prefix;
    const char* help;
    int levels;
    int timer_dy;
    const char* pic = nullptr;
};
std::optional<UpgradeKind> upgrade_kind(std::uint16_t type, bool plus) {
    switch (type) {
        case 0: return UpgradeKind{"UpPowerPlant", "IDS_POWERPLANTHELP", 6, 42};
        case 3:
            if (plus) return UpgradeKind{"upbasic", "IDS_UPGRADEBASICHELP", 3, 40};
            return std::nullopt;
        case 1:
        case 7: return UpgradeKind{"upwatchtower", "IDS_UPDOTHELP", 4, 40};
        case 9: return UpgradeKind{"UpSmallHouse", "IDS_SMALLHOUSEHELP", 6, 40};
        case 10:
        case 11: return UpgradeKind{"UpGoldMaker", "IDS_UPIRONMAKERHELP", 6, 40};
        case 13: return UpgradeKind{"UpOilMaker", "IDS_UPOILMAKERHELP", 6, 40};
        case 12:  // the sawmill: no window in the original; Entente Engine Plus, O4, its six levels (the build icon)
            if (plus) return UpgradeKind{"", "research.plus_saw", 6, 40, "all_lesopilka.bmp"};
            return std::nullopt;
        case 0x15: return UpgradeKind{"UpWatchTower", "IDS_UPWATCHTOWERHELP", 4, 40};
        default: return std::nullopt;
    }
}

// Entente Engine Plus, F7: a Building Academy research's text: the game's name (IDS_UPGRADEBASICHELP<row> up to its
// bracket, which holds the price) and our effect in brackets (build_academy_key(row)).
std::string build_academy_name(const StringTable& strings, int row) {
    std::string name = text_of(strings, "IDS_UPGRADEBASICHELP" + std::to_string(row));
    if (const auto bracket = name.find(" ("); bracket != std::string::npos) name.resize(bracket);
    return name;
}
std::string build_academy_text(const StringTable& strings, int row) {
    const std::string name = build_academy_name(strings, row);
    const std::string key = build_academy_key(row);
    return strings.contains(key) ? name + " (" + text_of(strings, key) + ")" : name;
}


bool lacks(const sim::Player& pl, const std::int32_t* c) {
    return !(c[0] <= pl.food && c[3] <= pl.gold && c[1] <= pl.wood && c[2] <= pl.iron && c[4] <= pl.oil);
}


std::string elect(const sim::Player& pl, const StringTable& strings) {
    return pl.power_made < 0.1f ? text_of(strings, "IDS_ELECT_DEMAND") : std::string();
}

void add_upgrades(Panel& p, const sim::GameState& s, const sim::Player& pl, const sim::House& h,
                  const StringTable& strings) {
    const auto kind = upgrade_kind(h.type, s.plus_rules);
    // the level: the house's own; the Building Academy's is the player's (Plus F7), one research at a time
    const bool academy = h.type == 3;
    const int level = academy ? pl.plus_build_level : h.field_065;
    if (!kind || level >= kind->levels) return;
    auto add = [&](int level, bool disabled, bool cancel) {
        // the rule set's row: Entente Engine Plus has its own mine and oil rig rows (B302), which the sim charges
        const std::int32_t* row = sim::House_UpgradeCostRow(s, h.type, static_cast<std::uint16_t>(level));
        const bool off = disabled || row == nullptr || lacks(pl, row);


        std::string key = kind->help + std::to_string(level);
        // Entente Engine Plus: our text where the game has none (the sawmill) or other numbers (the mines and the oil
        // well, plus-balance.md 3)
        if (const std::string plus_key = plus_upgrade_key(h.type, level);
            s.plus_rules && !plus_key.empty() && strings.contains(plus_key))
            key = plus_key;
        std::string tip = academy ? build_academy_text(strings, level) : text_of(strings, key);
        if (row != nullptr) tip = append_cost_text(tip, row, strings);
        const std::string pic = kind->pic != nullptr ? kind->pic : kind->prefix + std::to_string(level + 1) + ".bmp";
        PanelWidget b = button(pic_named(kInterfaceLes, pic), 195, 5, tip + elect(pl, strings), 0, kHpUpgrade, level);
        b.disabled = off;
        if (off) b.action = 0;
        if (cancel) b.right_action = kHpUpgradeCancel, b.right_arg = level;
        p.widgets.push_back(std::move(b));
    };
    if (h.field_05c != 0 && h.field_064 > 2) {  // an upgrade runs: its button, disabled, the right click cancels it
        p.widgets.push_back(bar("iconsUpgradeTimer.bmp", 195, 5 + kind->timer_dy, research_width(h), 5));
        add(h.field_064 - 3, true, true);
        return;
    }
    add(level, h.field_05c != 0 || (academy && sim::House_BuildAcademyBusy(pl)), false);
}



void add_mint(Panel& p, const sim::Player& pl, const sim::House& h, const StringTable& strings, bool plus) {
    auto text = [&](const char* game, const char* ours) {
        return text_of(strings, plus && strings.contains(ours) ? ours : game);
    };
    const std::int32_t* row = sim::House_UpgradeCostRow(14, 0);
    if (h.field_05c != 0 && h.field_064 > 2)
        p.widgets.push_back(bar("iconsUpgradeTimer.bmp", 195, 45, research_width(h), 5));
    if (pl.mint_done != 0) {
        PanelWidget w = picture(pic_named(kInterfaceGroupLes, "MintAfter.bmp"), {195, 5, 40, 40}, 3);
        w.tooltip = text("IDS_MINT_AFTER_UPGRADE", "plus.mint_after") + elect(pl, strings);
        p.widgets.push_back(std::move(w));
        return;
    }
    const bool off = h.field_05c != 0 || lacks(pl, row);
    PanelWidget b = button(pic_named(kInterfaceGroupLes, "MintBefore.bmp"), 195, 5,
                           append_cost_text(text("IDS_MINT_UPGRADE", "plus.mint"), row, strings) + elect(pl, strings),
                           0, off ? 0 : kHpMint);
    b.disabled = off;
    p.widgets.push_back(std::move(b));
}






constexpr int kSciencesX[20] = {195, 237, 279, 321, 363, 405, 195, 195, 279, 321,
                                363, 405, 195, 279, 321, 363, 405, 195, 237, 279};
constexpr int kSciencesY[20] = {5, 5, 5, 5, 5, 5, 50, 50, 50, 50, 50, 50, 95, 95, 95, 95, 95, 140, 140, 140};
constexpr int kMilitaryX[28] = {195, 240, 285, 330, 195, 240, 285, 330, 375, 195, 240, 285, 330, 375,
                                420, 375, 420, 195, 240, 285, 330, 465, 510, 420, 465, 555, 465, 420};
constexpr int kMilitaryY[28] = {5,  5, 5, 5,   50,  50,  50,  50, 50, 95, 95, 95, 95, 95,
                                95, 5, 5, 140, 140, 140, 140, 95, 95, 50, 50, 95, 5,  5};

// The rows an academy can show: 19 and 26, and under Entente Engine Plus its new r19, k26 and k27.
int academy_rows(const sim::GameState& s, bool sciences) {
    if (sciences) return s.plus_rules ? 20 : 19;
    return s.plus_rules ? 28 : 26;
}

// Entente Engine Plus (2026-10-23, plus-balance.md 3): Deep drilling, the Military Academy's k27, is the Academy of
// Sciences' row 27 (sim::kPlusDeepDrilling) in its free place (321, 140), with k27's picture and text.
bool sciences_drilling(bool sciences, int row) { return sciences && row == sim::kPlusDeepDrilling; }

// A row the academy's window can show (its place in the tables, or Deep drilling's).
bool research_shown(const sim::GameState& s, bool sciences, int row) {
    return row < academy_rows(s, sciences) || (s.plus_rules && sciences_drilling(sciences, row));
}

IRect research_rect(bool sciences, int row) {
    if (sciences_drilling(sciences, row)) return IRect{321, 140, 40, 40};
    const auto r = static_cast<std::size_t>(row);
    return IRect{sciences ? kSciencesX[r] : kMilitaryX[r], sciences ? kSciencesY[r] : kMilitaryY[r], 40, 40};
}

// A research row's tooltip keys: the game's (IDS_UPACADNAUKHELP<row>, IDS_UPACADWARHELP<row + 1>) and ours under Plus.
std::string research_game_key(bool sciences, int row) {
    if (sciences_drilling(sciences, row)) sciences = false;
    return sciences ? "IDS_UPACADNAUKHELP" + std::to_string(row) : "IDS_UPACADWARHELP" + std::to_string(row + 1);
}
std::string research_plus_key(bool sciences, int row) {
    if (sciences_drilling(sciences, row)) sciences = false;
    return sciences ? plus_science_key(row) : plus_research_key(row + 1);
}

// A research row's picture: the archive's entry row + 1; Plus's new rows reuse pictures that exist (plus-balance.md 5):
// k26 Steel helmets the infantry rate of fire item's (row 4), k27 Deep drilling the oil rig's upgrade, r19 Steam
// sawmills the market's wood; the items of the freed rows k1, k8 and k11 the picture of the unit they improve (the
// machine gunner, the soldier, the cavalry; UnitsIcons.les, the train buttons' pictures).
PanelPic research_pic(bool plus, bool sciences, int row) {
    if (sciences && row == 19) return pic_named(kInterfaceLes, "IconsWood.bmp");
    if (sciences_drilling(sciences, row)) sciences = false;  // Deep drilling keeps k27's picture
    if (plus && !sciences && (row == 1 || row == 8 || row == 11)) {
        const std::uint16_t unit = row == 1 ? 6 : row == 8 ? 2 : 4;
        return pic_id(kUnitsIconsLes, unit);
    }
    if (!sciences && row == 26) return pic_id(kUpgradesWarLes, 5);
    if (!sciences && row == 27) return pic_named(kInterfaceLes, "UpOilMaker2.bmp");
    return pic_id(sciences ? kUpgradesAcadLes : kUpgradesWarLes, static_cast<std::uint16_t>(row + 1));
}






void add_research_button(Panel& p, const sim::GameState& s, std::uint8_t player, const sim::Player& pl,
                         const sim::House& h, int row, bool disabled, bool running, const StringTable& strings) {
    const bool sciences = h.type == 2;
    // the rule set's price: Entente Engine Plus has its own, Delousing's rising with each use
    std::array<std::int32_t, 6> price{};
    if (!sim::Research_Price(s, player, h.type, static_cast<std::uint16_t>(row), price) ||
        !research_shown(s, sciences, row)) {
        return;
    }
    const std::int32_t* cost = price.data();
    if (!disabled && lacks(pl, cost)) disabled = true;
    std::string help = research_game_key(sciences, row);
    // Entente Engine Plus: our text of an item that is new or does something else there (docs/plus-balance.md F1, 5)
    const std::string plus_key = research_plus_key(sciences, row);
    if (s.plus_rules && strings.contains(plus_key)) help = plus_key;
    const IRect at = research_rect(sciences, row);
    PanelWidget b = button(research_pic(s.plus_rules, sciences, row), at.x, at.y,
                           append_cost_text(text_of(strings, help), cost, strings) + elect(pl, strings), 0,
                           disabled ? 0 : kHpResearch, row);
    b.disabled = disabled;
    if (sciences && row == 13) b.text = std::to_string(pl.special_shells);
    if (running) b.right_action = kHpResearchCancel, b.right_arg = row;
    p.widgets.push_back(std::move(b));
}







void add_academy(Panel& p, const sim::GameState& s, std::uint8_t player, const sim::Player& pl, const sim::House& h,
                 const StringTable& strings) {
    const bool sciences = h.type == 2;
    const bool running = h.field_05c != 0;
    if (running && h.field_064 > 2) {
        const int row = h.field_064 - 3;
        if (research_shown(s, sciences, row)) {
            const IRect at = research_rect(sciences, row);
            p.widgets.push_back(bar("iconsupgradetimer.bmp", at.x, at.y + 40, research_width(h), 5));
        }
        add_research_button(p, s, player, pl, h, row, true, true, strings);
    }
    auto offer = [&](int row) { add_research_button(p, s, player, pl, h, row, running, false, strings); };
    if (!sciences) {
        for (int k = 0; k < academy_rows(s, false); ++k) {
            if (s.plus_rules && k == sim::kPlusDeepDrilling) continue;  // Plus: offered by the Academy of Sciences
            // Plus: an old save's k27 running here has k16's place (Fire direction staff waits for it)
            if (s.plus_rules && k == sim::kStrikeUnlockRow && running && h.field_064 == sim::kPlusDeepDrilling + 3)
                continue;
            if (pl.research_9b3860[static_cast<std::size_t>(k)] == 0 &&
                pl.research_9b3770[static_cast<std::size_t>(k)] == 0) {
                offer(k);
            }
        }
        return;
    }


    const auto& f = pl.research_flags;
    const auto chain = [&](int first, int second) {  // first, or second once first is done
        if (f[static_cast<std::size_t>(first + 12)] == 0) {
            if (f[static_cast<std::size_t>(first)] == 0) offer(first);
        } else if (f[static_cast<std::size_t>(second)] == 0) {
            offer(second);
        }
    };
    if (s.climate != 3) {
        chain(0, 2);
        if (f[1] == 0) offer(1);
    }
    chain(3, 4);
    if (f[5] == 0) offer(5);
    if (s.climate != 3) chain(6, 7);
    for (int r = 8; r <= 11; ++r) {
        if (f[static_cast<std::size_t>(r)] == 0) offer(r);
    }
    const auto& done = pl.research_9b3950;
    const auto& started = pl.research_9b39f0;
    if (done[12] == 0 && started[12] == 0) offer(12);
    for (int r = 13; r <= 14; ++r) {
        if (done[12] != 0 && done[static_cast<std::size_t>(r)] == 0 && started[static_cast<std::size_t>(r)] == 0)
            offer(r);
    }
    for (int r = 15; r < academy_rows(s, true); ++r) {  // Plus: r19 Steam sawmills too
        if (done[static_cast<std::size_t>(r)] == 0 && started[static_cast<std::size_t>(r)] == 0) offer(r);
    }
    if (s.plus_rules && pl.research_9b3860[sim::kPlusDeepDrilling] == 0 &&
        pl.research_9b3770[sim::kPlusDeepDrilling] == 0) {
        offer(sim::kPlusDeepDrilling);  // Deep drilling, by the Military Academy's k27 bytes
    }
}

// Entente Engine Plus, the call-in strikes (docs/decisions.md 34, sim/strike.hpp): the town centre's five buttons once
// the Military Academy's Fire direction staff (k16) is done, in two rows between the train row and Workers. The tooltip
// names the strike, what it does and its price; one that cannot be called now is disabled, says why, and shows the
// seconds left of its cooldown (30 ms a tick, the game's normal speed).
constexpr const char* kStrikeKeys[sim::kStrikeCount] = {"strike.barrage", "strike.creeping", "strike.gas",
                                                        "strike.bombing", "strike.recon"};

void add_strikes(Panel& p, const sim::GameState& s, std::uint8_t player, const StringTable& strings) {
    if (!s.plus_rules || s.players[player].research_9b3770[sim::kStrikeUnlockRow] == 0) return;
    const PanelPic pics[sim::kStrikeCount] = {
        pic_named(kInterfaceLes, "firetopoint.bmp"), pic_named(kUnitsIconsLes, "big_berta.bmp"),
        pic_named(kInterfaceGroupLes, "firetopointgas.bmp"), pic_named(kUnitsIconsLes, "heavybomber.bmp"),
        pic_named(kUnitsIconsLes, "airscout.bmp")};
    constexpr int kX[sim::kStrikeCount] = {195, 240, 285, 195, 240}, kY[sim::kStrikeCount] = {60, 60, 60, 108, 108};
    for (std::uint8_t k = 0; k < sim::kStrikeCount; ++k) {
        const std::string key = kStrikeKeys[k];
        std::string tip = append_cost_text(text_of(strings, key) + ". " + text_of(strings, key + "_tip"),
                                           sim::Strike_Info(k).cost.data(), strings);
        const sim::StrikeRefusal why = sim::Strike_Check(s, player, k);
        const std::uint32_t wait = sim::Strike_Wait(s, player, k);
        const std::string seconds = std::to_string((wait * 3 + 99) / 100);
        switch (why) {
            case sim::StrikeRefusal::Cooling:
                tip += " (" + lang_fill(text_of(strings, "strike.wait"), {{"n", seconds}}) + ")";
                break;
            case sim::StrikeRefusal::Short: tip += " (" + text_of(strings, "strike.short") + ")"; break;
            case sim::StrikeRefusal::NoGas: tip += " (" + text_of(strings, "strike.need_gas") + ")"; break;
            default: break;
        }
        const bool off = why != sim::StrikeRefusal::None;
        PanelWidget b = button(pics[k], kX[k], kY[k], tip, 0, off ? 0 : kHpStrike, k);
        b.disabled = off;
        if (wait != 0) b.text = seconds;
        p.widgets.push_back(std::move(b));
    }
}


void add_hangar(Panel& p, const sim::GameState& s, const sim::House& h, std::uint8_t nation,
                const StringTable& strings) {
    std::vector<std::pair<int, const sim::Unit*>> air;
    for (std::size_t i = 0; i < h.units_inside && i < h.inside.size(); ++i) {
        const sim::Unit* u = sim::Unit_Find(s, h.inside[i]);
        if (u == nullptr) continue;
        const sim::UnitType& t = s.unit_types[sim::Unit_Nation(s, h.inside[i].player, *u) % 6][u->type % 40];
        if (t.is_air != 0) air.push_back({static_cast<int>(i), u});
    }
    const int n = static_cast<int>(air.size());
    for (int k = 0; k < n; ++k) {
        const sim::Unit& u = *air[static_cast<std::size_t>(k)].second;
        p.widgets.push_back(button(pic_id(kUnitsIconsLes, u.type), 195 + 400 * k / n, 50,
                                   unit_type_name(strings, nation, u.type), 0, kHpReleaseAircraft,
                                   air[static_cast<std::size_t>(k)].first));
    }
}




struct MarketRes {
    int r;
    int x;
    const char* pic;
    const char* key;
};
constexpr MarketRes kMarketRes[5] = {{0, 202, "IconsFood.bmp", "FOOD"},
                                     {3, 273, "IconsGold.bmp", "GOLD"},
                                     {2, 345, "IconsIron.bmp", "IRON"},
                                     {1, 417, "IconsWood.bmp", "WOOD"},
                                     {4, 489, "IconsOil.bmp", "OIL"}};



float market_shown(const sim::GameState& s, std::uint8_t player, const MarketUi& m) {
    return s.plus_rules ? sim::Market_ReceivePlus(s.players[player], m.sell, m.buy) : m.receive;
}

// Entente Engine Plus: a sell button's tooltip adds our plus.market_price with the resource's price index now.
std::string market_price(const sim::GameState& s, std::uint8_t player, int r, const StringTable& strings) {
    if (!s.plus_rules || r < 0 || r >= 5) return {};
    const long pct =
        std::lround(static_cast<double>(s.players[player].market_index[static_cast<std::size_t>(r)]) * 100.0);
    return ". " + lang_fill(text_of(strings, "plus.market_price"), {{"percent", std::to_string(pct)}});
}

constexpr std::uint32_t kMarketText = 0xfff799;
constexpr std::uint32_t kMarketChosen = 0x969696;







void add_market(Panel& p, const sim::GameState& s, std::uint8_t player, const MarketUi& m, const StringTable& strings) {
    const sim::Player& pl = s.players[player];
    auto number = [&p](int x, int y, const std::string& text) {
        p.widgets.push_back(picture(pic_named(kInterfaceLes, "IconsNumbers.bmp"), {x, y, 70, 16}, 1));
        PanelWidget t = label(x + 3, y + 3, text);
        t.font = PanelFont::Sans10, t.rgb = kMarketText, t.shadow = false;
        p.widgets.push_back(std::move(t));
    };
    auto icon = [&](const MarketRes& c, int y, const char* tip, int action, int right, const std::string& tail = {}) {
        PanelWidget b = button(pic_named(kInterfaceLes, c.pic), c.x, y,
                               text_of(strings, std::string(tip) + c.key) + tail, 0, action, c.r);
        b.rect.w = 48, b.rect.h = 46;
        b.right_action = right, b.right_arg = c.r;
        p.widgets.push_back(std::move(b));
    };
    for (const MarketRes& c : kMarketRes) {
        icon(c, 5, "IDS_MARKET_SELL_", kHpMarketSell, kHpMarketUnsell, market_price(s, player, c.r, strings));
        number(c.x - 11, 51, std::to_string(ftol(m.sell[static_cast<std::size_t>(c.r)])));
    }
    const bool can = sim::Market_CanTrade(s, player, m.sell);
    const bool on = pl.market_auto != 0;
    auto wide = [&](const char* pic, int x, const char* tip, int action, bool off) {
        PanelWidget b = button(pic_named(kInterfMarketLes, pic), x, 70, text_of(strings, tip), 0, action);
        b.rect.w = 114, b.rect.h = 22;
        b.disabled = off;
        p.widgets.push_back(std::move(b));
    };
    wide("change.bmp", 210, "IDS_MARKET_CHANGE", kHpMarketExchange, !can || on);
    wide("contribution.bmp", 349, "IDS_MARKET_SENDTOPLAYER", kHpMarketSend, !can || on);
    wide("trade.bmp", 506, "IDS_MARKET_CHANGEALWAYS", kHpMarketAuto, !can);
    for (const MarketRes& c : kMarketRes) {
        // The button first: the number picture (96 to 111) covers its top row.
        icon(c, 111, "IDS_MARKET_BUY_", kHpMarketBuy, kHpMarketClear);
        number(c.x - 11, 96, m.buy == c.r ? std::to_string(ftol(market_shown(s, player, m))) : std::string());
    }
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "IconsUnderPlayers.bmp"), {621, 5, 82, 153}, 1));
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "SliderBack.bmp"), {703, 5, 19, 153}, 1));
    // The slider's buttons and thumb, shown at rest: eight players fit in the list's ten rows, so it never scrolls.
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "SliderBtnUp.bmp"), {706, 5, 16, 13}, 3));
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "SliderBtnDown.bmp"), {706, 145, 16, 13}, 3));
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "Slider.bmp"), {703, 18, 14, 15}, 1));
    const std::vector<std::uint8_t> list = market_players(s, m);
    const std::size_t chosen = m.row < list.size() ? m.row : 0;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const std::uint8_t q = list[i];
        PanelWidget b;
        b.kind = PanelWidget::Kind::Button;
        b.rect = IRect{636, 10 + 15 * static_cast<int>(i), 67, 15};  // the text at the list's (15, 5 + 15 i)
        b.text = m.names[q];
        b.font = PanelFont::Serif16;
        const std::uint32_t c = s.players[q].stat_colour;  // COLORREF
        b.rgb = i == chosen ? kMarketChosen : ((c & 0xff) << 16) | (c & 0xff00) | ((c >> 16) & 0xff);
        b.action = kHpMarketTarget, b.arg = static_cast<int>(i);
        p.widgets.push_back(std::move(b));
    }
}

sim::OrderArg f32_arg(float v) { return raw(std::bit_cast<std::uint32_t>(v)); }


Command market_auto(std::uint8_t p, const MarketUi* m) {
    Command c;
    c.kind = Command::Kind::MarketAuto;
    c.player = p;
    c.args = {raw(p), raw(m != nullptr ? 1 : 0)};
    if (m != nullptr) {
        const sim::MarketAmounts& a = m->sell;
        c.args.insert(c.args.end(), {f32_arg(a[3]), f32_arg(a[1]), f32_arg(a[0]), f32_arg(a[4]), f32_arg(a[2]),
                                     raw(m->buy), f32_arg(m->receive)});
    }
    return c;
}

// The name of resource r (Food, Wood, Iron, Gold, Oil) in the trade messages.
std::string res_name(int r, const StringTable& t) {
    constexpr const char* kKeys[5] = {"IDS_FOOD", "IDS_WOOD", "IDS_IRON", "IDS_GOLD", "IDS_OIL"};
    return r >= 0 && r < 5 ? text_of(t, kKeys[r]) : std::string();
}


std::string market_amounts(const sim::MarketAmounts& a, const StringTable& t) {
    std::string out;
    for (int r : {3, 1, 0, 4, 2}) {
        const float v = a[static_cast<std::size_t>(r)];
        if (v > 0.5f) out += res_name(r, t) + "(" + std::to_string(ftol(v)) + "),";
    }
    return out;
}




void market_click(HousePanelResult& r, const sim::GameState& s, std::uint8_t p, PanelClick click, bool shift, bool ctrl,
                  MarketUi& m, const StringTable* strings) {
    const StringTable none;
    const StringTable& t = strings != nullptr ? *strings : none;
    const bool on = s.players[p].market_auto != 0;
    const auto res = static_cast<std::size_t>(std::clamp(click.arg, 0, 4));
    auto off = [&] {
        if (on) r.order.push_back(market_auto(p, nullptr));
    };
    auto recompute = [&] {
        m.receive = sim::Market_Receive(m.sell, m.buy);
        off();
    };
    const sim::MarketAmounts& a = m.sell;
    switch (click.action) {
        case kHpMarketSell:
            m.sell[res] = static_cast<float>(static_cast<double>(market_step(shift, ctrl)) + m.sell[res]);
            recompute();
            r.what = "market: sell more";
            break;
        case kHpMarketUnsell:
            m.sell[res] = static_cast<float>(static_cast<double>(m.sell[res]) - market_step(shift, ctrl));
            if (m.sell[res] < 0.0f) m.sell[res] = 0.0f;
            recompute();
            r.what = "market: sell less";
            break;
        case kHpMarketBuy:
            m.buy = static_cast<std::uint8_t>(res);
            recompute();
            r.what = "market: the resource to buy";
            break;
        case kHpMarketClear:
            m.sell = {};
            m.receive = 0;
            if (on) r.order.push_back(market_auto(p, &m));
            r.what = "market: amounts cleared";
            break;
        case kHpMarketExchange: {
            if (sim::Market_CanTrade(s, p, a)) {
                Command c;
                c.kind = Command::Kind::Act_c26e0;
                c.player = p;
                c.args = {raw(p),        f32_arg(a[3]), f32_arg(a[1]), f32_arg(a[0]),
                          f32_arg(a[4]), f32_arg(a[2]), raw(m.buy),    f32_arg(m.receive)};
                r.order.push_back(std::move(c));

                std::string sold = text_of(t, "IDS_SELL") + market_amounts(a, t);
                sold.pop_back();
                r.messages.push_back(sold);
                r.messages.push_back(text_of(t, "IDS_BUY") + res_name(m.buy, t) +
                                     std::to_string(ftol(market_shown(s, p, m))));
            }
            off();
            r.what = "market: exchange";
            break;
        }
        case kHpMarketSend: {
            const std::vector<std::uint8_t> list = market_players(s, m);
            m.receive = sim::Market_Receive(m.sell, m.buy);
            off();
            if (m.row >= list.size() || !sim::Market_CanTrade(s, p, a)) break;
            const std::uint8_t to = list[m.row];
            Command c;
            c.kind = Command::Kind::Act_c2d80;
            c.player = p;
            c.args = {raw(p), raw(to), f32_arg(a[3]), f32_arg(a[1]), f32_arg(a[0]), f32_arg(a[4]), f32_arg(a[2])};
            r.order.push_back(std::move(c));

            std::string sent = text_of(t, "IDS_SEND_TO_PLAYER") + m.names[to] + ", " + market_amounts(a, t);
            sent.pop_back();
            r.messages.push_back(sent);
            r.what = "market: send to player " + std::to_string(to);
            break;
        }
        case kHpMarketAuto:
            r.order.push_back(market_auto(p, &m));
            r.messages.push_back(text_of(t, "IDS_CHANGE_AUTO"));
            r.what = "market: automatic trade on";
            break;
        case kHpMarketTarget:
            m.row = static_cast<std::uint8_t>(click.arg);
            off();
            r.what = "market: the player to send to";
            break;
        default: break;
    }
}

}  // namespace

float market_step(bool shift, bool ctrl) {
    return shift && ctrl ? 100000.0f : ctrl ? 10000.0f : shift ? 1000.0f : 100.0f;
}

std::vector<std::uint8_t> market_players(const sim::GameState& s, const MarketUi& m) {
    std::vector<std::uint8_t> out;
    for (std::size_t q = 0; q < s.players.size() && q < 8; ++q) {
        const sim::Player& pl = s.players[q];
        if (pl.house_array_freed != 0 || pl.is_lost != 0 || q >= m.names.size() || m.names[q].empty()) continue;
        out.push_back(static_cast<std::uint8_t>(q));
    }
    return out;
}

std::optional<sim::HouseRef> house_panel_house(const sim::GameState& s, const Selection& sel) {
    if (!sel.units.empty() || sel.flagged.size() != 1 || !sel.house) return std::nullopt;
    const sim::HouseRef r = sel.flagged[0];
    if (r.player != sel.player || sel.house->player != r.player || sel.house->slot != r.slot) return std::nullopt;
    if (r.player >= s.players.size() || r.slot >= s.players[r.player].houses.size()) return std::nullopt;
    if (s.players[r.player].houses[r.slot].is_destroyed != 0) return std::nullopt;
    return sim::HouseRef{r.player, r.slot, 0};
}

int house_panel_width(std::uint16_t type) {
    switch (type) {
        case 2:
        case 3:
        case 4:
        case 5:
        case 0x10:
        case 0x13:
        case 0x14:
        case 0x16:
        case 0x17: return 420;
        case 6: return 140;
        case 8: return 540;
        default: return 100;
    }
}

std::string house_title(const sim::GameState& s, const StringTable& strings, const sim::HouseRef& r) {
    const sim::Player& pl = s.players[r.player];
    const sim::House& h = pl.houses[r.slot];
    int k = 0;
    for (std::size_t i = 0; i <= r.slot && i < pl.houses.size(); ++i) {
        if (pl.houses[i].type == h.type) ++k;
    }
    return house_type_name(strings, pl.nation, h.type) + " (" + std::to_string(k) + ")";
}

std::string train_tooltip(const sim::GameState& s, std::uint8_t nation, std::uint16_t type,
                          const StringTable& strings) {
    const sim::UnitType& t = s.unit_types[nation % 6][type % 40];
    std::string text = unit_type_name(strings, nation, type) + text_of(strings, "IDS_COST");


    const struct {
        int index;
        const char* key;
    } kCosts[] = {{3, "IDS_GOLD"}, {0, "IDS_FOOD"}, {2, "IDS_IRON"}, {4, "IDS_OIL"}, {1, "IDS_WOOD"}};
    for (const auto& c : kCosts) {
        const float cost = t.cost[static_cast<std::size_t>(c.index)];
        if (!(std::abs(cost) > 0.001f)) continue;
        text += text_of(strings, c.key) + " (" + std::to_string(sim::Ftol(static_cast<double>(cost))) + "), ";
    }
    if (text.size() >= 2) text.resize(text.size() - 2);
    return text;
}

std::string append_cost_text(std::string text, const std::int32_t* row, const StringTable& strings) {
    text += text_of(strings, "IDS_COST");
    const struct {
        int index;
        const char* key;
    } kCosts[] = {{0, "IDS_FOOD"}, {3, "IDS_GOLD"}, {1, "IDS_WOOD"}, {2, "IDS_IRON"}, {4, "IDS_OIL"}};
    for (const auto& c : kCosts) {
        const std::int32_t v = row[c.index];
        if (v == 0) continue;
        text += text_of(strings, c.key) + "(" + std::to_string(v) + "), ";
    }
    if (text.size() >= 2) text.resize(text.size() - 2);
    return text;
}

std::string plus_research_key(int id) { return "research.plus_war" + std::to_string(id); }
std::string plus_science_key(int row) { return "research.plus_sci" + std::to_string(row); }
std::string build_academy_key(int row) { return "research.plus_build" + std::to_string(row); }
std::string plus_upgrade_key(std::uint16_t type, int level) {
    if (type < 10 || type > 13) return {};
    if (level >= 2) return "research.plus_level_more";
    const char* kind = type == 12 ? "research.plus_saw" : type == 13 ? "research.plus_oil" : "research.plus_mine";
    return kind + std::to_string(level);
}

namespace {

// A research's short name for the advisor's line: its tooltip (ours under Plus when we have one) up to the effect in
// brackets.
std::string research_name(const StringTable& strings, bool sciences, int row) {
    std::string key = research_game_key(sciences, row);
    const std::string plus_key = research_plus_key(sciences, row);
    if (strings.contains(plus_key)) key = plus_key;
    std::string name = text_of(strings, key);
    if (const auto bracket = name.find(" ("); bracket != std::string::npos) name.resize(bracket);
    return name;
}

// The rows the smart rule scores as economy items (sim/plus_brain.cpp): their line says the pay back, the others the
// units they improve.
bool economy_row(bool sciences, int row) {
    if (!sciences) return row == 27;
    return row <= 2 || row == 6 || row == 7 || row == 10 || row == 11 || row == 19 || row == sim::kPlusDeepDrilling;
}

}  // namespace

std::string plus_advisor_status(const sim::GameState& s, std::uint8_t player, const StringTable& strings) {
    if (!s.plus_rules || player >= s.players.size()) return {};
    const sim::Player& pl = s.players[player];
    const sim::Player::PlusStatus& st = pl.plus_status;
    if (pl.field_9b2d94 == 0 || pl.field_9b2b3b == 0 || st.kind == 0) return {};  // advisor off, "Makes upgrades" off
    const std::string item = std::to_string(st.value);
    switch (st.kind) {
        case 1:
        case 2: {
            const std::string name = research_name(strings, st.kind == 1, st.item);
            if (economy_row(st.kind == 1, st.item)) {
                return lang_fill(text_of(strings, "plus.advisor_payback"), {{"item", name}, {"n", item}});
            }
            if (st.value <= 0) return lang_fill(text_of(strings, "plus.advisor_counter"), {{"item", name}});
            return lang_fill(text_of(strings, "plus.advisor_units"), {{"item", name}, {"n", item}});
        }
        case 3:
            return lang_fill(text_of(strings, "plus.advisor_upgrade"),
                             {{"item", house_type_name(strings, pl.nation, st.item)}, {"n", item}});
        case 4: return text_of(strings, "plus.advisor_academy");
        case 5:
            if (st.item == 0) return text_of(strings, "plus.advisor_power_station");
            return lang_fill(text_of(strings, "plus.advisor_power"), {{"item", research_name(strings, true, st.item)}});
        case 6: return text_of(strings, "plus.advisor_room");
        case 7: return text_of(strings, "plus.advisor_tower");
        case 8:  // a Building Academy research (F7): against the enemy guns
            if (st.item > 2) return {};
            return lang_fill(text_of(strings, "plus.advisor_counter"),
                             {{"item", build_academy_name(strings, st.item)}});
        default: return {};
    }
}

std::vector<std::string> plus_panel_keys() {
    std::vector<std::string> keys;
    for (int id : kPlusResearchTexts) keys.push_back(plus_research_key(id));
    for (int id : kPlusNewResearchTexts) keys.push_back(plus_research_key(id));
    for (int row : kPlusScienceTexts) keys.push_back(plus_science_key(row));
    for (int row = 0; row < 3; ++row) keys.push_back(build_academy_key(row));
    for (const char* k :
         {"research.plus_saw0", "research.plus_saw1", "research.plus_oil0", "research.plus_oil1", "research.plus_mine0",
          "research.plus_mine1", "research.plus_level_more", "plus.mint", "plus.mint_after"})
        keys.emplace_back(k);
    for (const char* k :
         {"plus.upkeep", "plus.reload_slow", "plus.reload_slow_why", "plus.market_price", "plus.advisor_payback",
          "plus.advisor_units", "plus.advisor_counter", "plus.advisor_upgrade", "plus.advisor_academy",
          "plus.advisor_power", "plus.advisor_power_station", "plus.advisor_room", "plus.advisor_tower"})
        keys.emplace_back(k);
    for (const char* k : kStrikeKeys) keys.emplace_back(k), keys.emplace_back(std::string(k) + "_tip");
    for (const char* k : {"strike.wait", "strike.short", "strike.need_gas"}) keys.emplace_back(k);
    return keys;
}

Panel house_panel(const sim::GameState& s, const Selection& sel, const StringTable& strings, const MarketUi* market) {
    Panel p;
    const auto ref = house_panel_house(s, sel);
    if (!ref) return p;
    const sim::Player& pl = s.players[ref->player];
    const sim::House& h = pl.houses[ref->slot];
    const sim::HouseType& ht = sim::House_Type(s, ref->player, h);  // THouse::Basic (a merged house: its own)
    const std::uint8_t nation = pl.nation;
    p.x = 10, p.bottom = 213, p.w = house_panel_width(h.type) + 190, p.h = 200;

    // The frame's bevels: the right edge, the top and the divider at x 188; no left or bottom bevel.
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "RBevel.bmp"), {p.w - 4, 0, 4, 200}, 1));
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "TBevel.bmp"), {0, 0, p.w, 4}, 1));
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "RBevel.bmp"), {188, 0, 4, 200}, 1));


    PanelWidget icon =
        button(house_icon(h.type), 5, 7,
               house_type_name(strings, nation, h.type) + text_of(strings, "IDS_HOUSE_ICON_RIGHT_CLICK"), 0, 0);
    icon.right_action = kHpDestroy;
    p.widgets.push_back(std::move(icon));
    for (int k = 0; k < h.field_065; ++k) p.widgets.push_back(star(1 + 8 * k, 7));
    if (h.type == 9 || h.type == 6) {

        p.widgets.push_back(picture(pic_named(kInterfaceLes, "IconsCountUnits.bmp"), {47, 9, 16, 16}, 1));
        p.widgets.push_back(label(
            65, 12, std::to_string(pl.units_alive) + text_of(strings, "IDS_HOUSE_FROM") + std::to_string(pl.unit_cap)));
    }


    p.widgets.push_back(label(5, 52, house_title(s, strings, *ref)));
    p.widgets.push_back(label(5, 64, nation_label(strings, nation)));
    const bool trains = ht.produce[0] != 0xffff;
    if (h.field_020 == 0) {

        p.widgets.push_back(label(
            5, 77, text_of(strings, "IDS_UNIT_DEFEND") + std::to_string(sim::House_Durability(s, ref->player, h))));
        if (h.capacity > 0) {
            p.widgets.push_back(label(5, 91,
                                      text_of(strings, "IDS_HOUSE_IN") + std::to_string(h.units_inside) +
                                          text_of(strings, "IDS_BRACKETS") + std::to_string(h.capacity) +
                                          text_of(strings, "IDS_BRACKETS_CLOSE")));
        }
        const double max_hp = std::max<double>(1, h.max_hp);
        p.widgets.push_back(bar("Iconslife.bmp", 5, 49, ftol(static_cast<double>(h.hp) * 40 / max_hp), 3));

        // The train row: count of each type queued; in endless mode (0xffff) the head type shows "0".
        const bool endless = h.queue_count == 0xffff;
        // The unit records the row trains from (docs/bugs.md B263): a merged house's own nation in our rules.
        const std::uint8_t unit_nation = sim::House_TrainNation(s, ref->player, ref->slot);
        int head = 0, total = 0;
        for (int i = 0; i < 10 && ht.produce[static_cast<std::size_t>(i)] != 0xffff; ++i) {
            const std::uint16_t t = ht.produce[static_cast<std::size_t>(i)];
            const bool is_head = h.queue_count != 0 && !h.queue.empty() && h.queue[0] == t;
            if (is_head) head = i;
            int n = 0;  // endless: every type counts 1 toward the bar, none gets a number but the head's "0"
            if (endless) {
                ++total;
            } else {
                for (std::size_t q = 0; q < h.queue_count && q < h.queue.size(); ++q) n += h.queue[q] == t ? 1 : 0;
            }
            total += n;
            // Entente Engine Plus: the upkeep and the slow reload after the cost (ours; as IDS_ELECT_DEMAND's tail)
            std::string tip = train_tooltip(s, unit_nation, t, strings) + plus_upkeep_text(s, unit_nation, t, strings);
            if (const std::string hint = plus_reload_hint(s, ref->player, unit_nation, t, strings); !hint.empty())
                tip += " (" + hint + ")";
            PanelWidget b =
                button(pic_id(kUnitsIconsLes, unit_icon_id(unit_nation, t)), 195 + 42 * i, 7, tip, 0, kHpTrain, t);
            if (endless && is_head) {
                b.text = "0";
            } else if (n != 0) {
                b.text = std::to_string(n);
            }
            if (!sim::Player_CanAfford(s, ref->player, t, unit_nation)) b.disabled = true, b.action = 0;
            b.right_action = kHpUntrain, b.right_arg = t;
            p.widgets.push_back(std::move(b));
        }
        if (total != 0 && !h.queue.empty()) {  // kind 3: the time left of the head against its build time
            const sim::UnitType& t = s.unit_types[unit_nation % 6][h.queue[0] % 40];
            const double bt = t.build_time[ref->player % 8];
            const int w = bt != 0 ? ftol(40.0 - static_cast<double>(h.field_044) * 40.0 / bt) : 0;
            // The code names IconsUpgradetimer.bmp, but in picture slot 0xd, which the health bar above has just
            // filled with Iconslife.bmp: the slot keeps the first picture, so the bar shows green (house_tankplant).
            p.widgets.push_back(bar("Iconslife.bmp", 195 + 42 * head, 47, w, 4));
        }
        if (trains || h.capacity != 0) {
            p.widgets.push_back(button(pic_named(kInterfaceLes, "Set_Exit.bmp"), p.w - 45, 157,
                                       text_of(strings, "IDS_HOUSE_SET_EXIT_POINT"), 0, kHpSetExit));
        }
        if (h.field_05c != 0 && h.field_064 == 2) {
            p.widgets.push_back(bar("IconsUpgradeTimer.bmp", 195, 195, research_width(h), 5));
        }
        switch (h.type) {
            case 6: {
                p.widgets.push_back(button(pic_named(kInterfaceGroupLes, "workers.bmp"), 195, 157,
                                           text_of(strings, "IDS_FREE_WORKERS"), 'w', kHpWorkers));
                p.widgets.push_back(button(pic_named(kInterfaceGroupLes, "diplomatiy.bmp"), 240, 157,
                                           text_of(strings, "IDS_DIPLOMACY_BTN"), 0, kHpDiplomacy));
                add_strikes(p, s, ref->player, strings);  // Entente Engine Plus
                break;
            }
            case 14: add_mint(p, pl, h, strings, s.plus_rules); break;
            case 15: {
                const bool can = pl.gold >= 100.0;
                PanelWidget b = button(pic_named(kInterfaceLes, "MakeKukuruza.bmp"), 195, 5,
                                       text_of(strings, can ? "IDS_SOW" : "IDS_SOW_CANNOT"), 0, can ? kHpSow : 0);
                b.disabled = !can;
                p.widgets.push_back(std::move(b));
                break;
            }
            case 0x14: add_hangar(p, s, h, nation, strings); break;
            case 2:
            case 5: add_academy(p, s, ref->player, pl, h, strings); break;
            case 8: add_market(p, s, ref->player, market != nullptr ? *market : MarketUi{}, strings); break;
            default: add_upgrades(p, s, pl, h, strings); break;
        }
    } else {
        p.widgets.push_back(label(5, 77, text_of(strings, "IDS_HOUSE_NOW_BUILD")));
        const int work = ht.work_rate;
        const int w = work != 0 ? (work - static_cast<int>(h.field_020)) * 40 / work : 0;
        p.widgets.push_back(bar("Iconslife.bmp", 5, 49, std::clamp(w, 0, 40), 3));
        p.widgets.push_back(button(pic_named(kInterfaceGroupLes, "Cancel.bmp"), 5, 155,
                                   text_of(strings, "IDS_HOUSE_BUILD_CANCEL"), 0, kHpCancelBuild));
    }
    if (h.type != 6 && trains) {
        const struct {
            const char* pic;
            const char* tip;
            char key;
            int mode;
        } kModes[] = {{"attypefire.bmp", "IF_ONLY_FIRE_HOUSE", 'f', 1},
                      {"attypefight.bmp", "IF_ONLY_FIGHT_HOUSE", 'h', 2},
                      {"attypegrenade.bmp", "IF_ONLY_GRFIGHT_HOUSE", 'g', 3},
                      {"attypeundefined.bmp", "IF_ONLY_AUTOATTACK_HOUSE", 'j', 0xff},
                      {"defend.bmp", "IDS_DEFEND_ONLY_HOUSE", 'd', 4}};
        int x = 195;
        for (const auto& m : kModes) {
            PanelWidget b =
                button(pic_named(kInterfaceGroupLes, m.pic), x, 150, text_of(strings, m.tip), m.key, kHpMode, m.mode);
            b.active = h.field_0b8 == m.mode;
            p.widgets.push_back(std::move(b));
            x += 45;
        }
    }


    const int n = h.units_inside;
    if (n != 0) {
        const int step = n * 41 + 6 < 190 ? 41 : 150 / n;
        for (int i = 0; i < n && i < static_cast<int>(h.inside.size()); ++i) {
            const int x = 5 + i * step;
            const sim::UnitRef& r = h.inside[static_cast<std::size_t>(i)];
            const sim::Unit* u = sim::Unit_Find(s, r);
            if (u == nullptr) continue;
            const sim::UnitType& t = s.unit_types[sim::Unit_Nation(s, r.player, *u) % 6][u->type % 40];
            if (t.is_air != 0) continue;
            p.widgets.push_back(button(pic_id(kUnitsIconsLes, unit_icon_id(nation, u->type)), x, 155,
                                       text_of(strings, "IDS_HOUSE_FREE_UNIT"), 0, kHpReleaseUnit, i));
            for (int k = 0; k < unit_stars(u->field_08c); ++k) p.widgets.push_back(star(x + 10 * k, 155));
            p.widgets.push_back(bar("iconsupgradetimer.bmp", x, 196, 40, 3));
            const double max_hp = std::max<double>(1, t.max_hp[r.player % 8]);
            p.widgets.push_back(bar("IconsLife.bmp", x, 196, ftol(static_cast<double>(u->hp) * 40 / max_hp), 3));
        }
        if (h.field_030 == 0) {
            p.widgets.push_back(button(pic_named(kInterfaceLes, "MayFire.bmp"), 5, 110,
                                       text_of(strings, "IDS_UNIT_CAN_FIRE"), 'c', kHpFireOn));
        } else {
            p.widgets.push_back(button(pic_named(kInterfaceLes, "NoFire.bmp"), 5, 110,
                                       text_of(strings, "IDS_UNIT_NO_FIRE"), 'n', kHpFireOff));
        }
    }
    return p;
}

sim::Command strike_order(std::uint8_t player, int strike, std::int32_t x, std::int32_t y, std::int32_t x2,
                          std::int32_t y2) {
    Command c;
    c.kind = Command::Kind::PlusStrike;
    c.player = player;
    c.args = {raw(player),
              raw(static_cast<std::uint32_t>(strike)),
              raw(static_cast<std::uint32_t>(x)),
              raw(static_cast<std::uint32_t>(y)),
              raw(static_cast<std::uint32_t>(x2)),
              raw(static_cast<std::uint32_t>(y2))};
    return c;
}

std::vector<sim::UnitRef> idle_workers(const sim::GameState& s, std::uint8_t player) {
    std::vector<sim::UnitRef> out;
    if (player >= s.players.size()) return out;
    const sim::Player& pl = s.players[player];
    for (std::uint32_t k = pl.first_unit; k != sim::kNoSlot; k = pl.unit_slots[k].next) {
        const sim::Unit& u = pl.unit_slots[k];
        const sim::UnitType& t = s.unit_types[sim::Unit_Nation(s, player, u) % 6][u.type % 40];
        if (t.move_class == 6 && u.work == 0xff && !u.in_group && u.player_unit_attack == 0xff &&
            u.player_house_attack == 0xff && u.is_deleted == 0 && 0.5f < t.speed[player % 8] && u.field_154 == 0 &&
            (u.num_work == 3 || u.num_work == 4)) {
            out.push_back(sim::UnitRef{player, u.id, u.serial});
        }
    }
    return out;
}

HousePanelResult house_panel_click(const sim::GameState& s, Selection& sel, PanelClick click, bool shift, bool ctrl,
                                   MarketUi* market, const StringTable* strings) {
    HousePanelResult r;
    const auto ref = house_panel_house(s, sel);
    if (!ref) return r;
    const std::uint8_t p = ref->player;
    const sim::House& h = s.players[p].houses[ref->slot];
    auto order = [&](Command::Kind kind, std::vector<sim::OrderArg> args, const std::string& what) {
        Command c;
        c.kind = kind;
        c.player = p;
        c.args = std::move(args);
        r.order.push_back(std::move(c));
        r.what = what;
    };
    auto inside = [&](int i) -> const sim::UnitRef* {
        if (i < 0 || i >= h.units_inside || static_cast<std::size_t>(i) >= h.inside.size()) return nullptr;
        return &h.inside[static_cast<std::size_t>(i)];
    };
    const auto arg = static_cast<std::uint32_t>(click.arg);
    switch (click.action) {
        case kHpDestroy:
            order(Command::Kind::Act_bbad0, {house_arg(*ref)},
                  h.field_020 == 0 ? "destroy the building" : "give up the construction");
            break;
        case kHpCancelBuild: {
            const auto refund = sim::House_SellRefund(s, p, h);
            order(Command::Kind::SellHouse, {house_arg(*ref), array_arg(refund.data(), refund.size())},
                  "cancel the construction");
            break;
        }
        case kHpReleaseUnit:
            if (const sim::UnitRef* u = inside(click.arg)) {
                order(Command::Kind::ReleaseHouseUnit, {house_arg(*ref), unit_arg(*u)}, "let a unit out");
            }
            break;
        case kHpFireOn: order(Command::Kind::Act_ba0a0, {house_arg(*ref)}, "units inside: allow fire"); break;
        case kHpFireOff: order(Command::Kind::Act_b9fc0, {house_arg(*ref)}, "units inside: hold fire"); break;
        case kHpTrain: {
            const std::uint32_t n = shift && ctrl ? 0xffff : ctrl ? 50 : shift ? 5 : 1;
            order(Command::Kind::Act_b9730, {raw(p), raw(ref->slot), raw(arg), raw(n)},
                  n == 0xffff ? "train without end" : "train " + std::to_string(n));
            break;
        }
        case kHpUntrain: {
            const std::uint32_t n = shift ? 5 : ctrl ? 50 : 1;
            order(Command::Kind::Act_b9c70, {raw(p), raw(ref->slot), raw(arg), raw(n)},
                  "cancel training " + std::to_string(n));
            break;
        }
        case kHpSetExit:
            r.set_exit = true;
            r.what = "set the collection point: left click on the map";
            break;
        case kHpMode: {
            order(Command::Kind::Act_baa60, {raw(p), raw(arg)}, "attack type for new units");
            r.order.back().houses = {*ref};
            break;
        }
        case kHpWorkers: {
            const auto list = idle_workers(s, p);
            if (!list.empty()) clear_selection(sel);
            select_units(sel, list);
            r.what = std::to_string(list.size()) + " idle workers";
            break;
        }
        case kHpStrike:  // Entente Engine Plus: the app's target mode for the strike (the order comes from the map)
            if (arg < sim::kStrikeCount &&
                sim::Strike_Check(s, p, static_cast<std::uint8_t>(arg)) == sim::StrikeRefusal::None) {
                r.strike = static_cast<int>(arg);
                r.what = sim::Strike_Info(static_cast<std::uint8_t>(arg)).two_points
                             ? "strike: left click where it starts, then where it ends"
                             : "strike: left click on the target";
            }
            break;
        case kHpDiplomacy:
            clear_selection(sel);
            r.diplomacy = true;
            r.what = "diplomacy";
            break;
        case kHpSow: order(Command::Kind::Act_c0c20, {house_arg(*ref)}, "raise crops"); break;
        case kHpReleaseAircraft:
            if (const sim::UnitRef* u = inside(click.arg)) {
                const sim::Unit* x = sim::Unit_Find(s, *u);
                if (x != nullptr && x->work == 3) {
                    order(Command::Kind::ReleaseUnit, {house_arg(*ref), unit_arg(*u)}, "aircraft out of the hangar");
                }
            }
            break;
        case kHpUpgrade: {
            const auto t = sim::House_UpgradeTime(h.type, static_cast<std::uint16_t>(click.arg));
            order(Command::Kind::Act_a5290, {house_arg(*ref), raw(t), raw(arg + 3)}, "upgrade");
            break;
        }
        case kHpUpgradeCancel:
            if (const std::int32_t* row = sim::House_UpgradeCostRow(s, h.type, static_cast<std::uint16_t>(click.arg))) {
                order(Command::Kind::Act_a52c0, {house_arg(*ref), array_arg(row, 5)}, "cancel the upgrade");
            }
            break;
        case kHpResearch:
            order(Command::Kind::Act_a5290,
                  {house_arg(*ref), raw(sim::Research_Time(s, h.type, static_cast<std::uint16_t>(click.arg))),
                   raw(arg + 3)},
                  "research");
            break;
        case kHpResearchCancel:
            if (std::array<std::int32_t, 6> price{};
                sim::Research_Price(s, ref->player, h.type, static_cast<std::uint16_t>(click.arg), price)) {
                order(Command::Kind::Act_a52c0, {house_arg(*ref), array_arg(price.data(), 5)}, "cancel the research");
            }
            break;
        case kHpMint:
            order(Command::Kind::Act_a5290, {house_arg(*ref), raw(sim::House_UpgradeTime(14, 0)), raw(6)},
                  "start minting coins");
            break;
        default:
            if (market != nullptr && click.action >= kHpMarketSell && click.action <= kHpMarketTarget) {
                market_click(r, s, p, click, shift, ctrl, *market, strings);
            }
            break;
    }
    return r;
}

std::optional<std::array<float, 2>> rally_flag_point(const sim::GameState& s, const Selection& sel) {
    const auto ref = house_panel_house(s, sel);
    if (!ref) return std::nullopt;
    const sim::Player& pl = s.players[ref->player];
    const sim::House& h = pl.houses[ref->slot];
    if (h.type == 0x16) return std::nullopt;
    if (sim::House_Type(s, ref->player, h).produce[0] == 0xffff && h.capacity == 0) return std::nullopt;
    const auto ground = static_cast<float>(sim::GroundHeightAt(s, h.rally_x, h.rally_y));
    return std::array<float, 2>{h.rally_x, h.rally_y - ground};
}

void FlagPhase::frame() {
    value += 0.5f;
    if (value >= 8.0f) value = 0.001f;
}

void FlagPhase::advance(double elapsed_ms, double period_ms) {
    ms += elapsed_ms;
    while (period_ms > 0 && ms >= period_ms) {
        frame();
        ms -= period_ms;
    }
}

}  // namespace antanta::ui
