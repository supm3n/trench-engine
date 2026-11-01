#include "ui/unit_panel.hpp"

#include <algorithm>
#include <cmath>

#include "sim/movement.hpp"
#include "ui/build_menu.hpp"

namespace antanta::ui {

namespace {

using sim::Command;

// The string table's nation keys in nation order (formats.md: file index = nation; the table spells Austria-Hungary
// "AUSTR0HUNGARY" in the unit names and "AUSTRIA" in the MPS names).
const char* const kUnitNationKeys[] = {"RUSSIA", "ENGLAND", "FRANCE", "GERMANY", "AUSTR0HUNGARY"};
const char* const kMpsKeys[] = {"MPS_RUSSIA_0", "MPS_ENGLAND_1", "MPS_FRANCE_2", "MPS_GERMANY_3", "MPS_AUSTRIA_4"};

std::string text_of(const StringTable& t, const std::string& key) {
    const auto it = t.find(key);
    return it != t.end() ? it->second : key;
}

sim::OrderArg raw(std::uint32_t v) {
    sim::OrderArg a;
    a.raw = v;
    return a;
}

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

// Upstar.bmp (8 x 8 frames) k times from (x, y), 10 pixels apart.
void stars(Panel& p, int n, int x, int y) {
    for (int k = 0; k < n; ++k)
        p.widgets.push_back(picture(pic_named(kInterfaceLes, "Upstar.bmp"), {x + 10 * k, y, 8, 8}, 3));
}

int ftol(double v) { return static_cast<int>(v); }  // truncation toward zero, as ftol

}  // namespace

int unit_stars(float experience) { return experience >= 250.0f ? 5 : ftol(static_cast<double>(experience) * 0.02); }

std::uint16_t unit_icon_id(std::uint8_t nation, std::uint16_t type) {
    if (type == 14 || type == 16 || type == 23) return static_cast<std::uint16_t>(nation * 100 + type);
    return type;
}

IRect type_icon_rect(int n, int k) {
    const int cols = n <= 5 ? n : n <= 14 ? 5 : n / 3 + 1;
    const int step = 150 / std::max(1, cols);
    return IRect{5 + (k % cols) * step, 7 + (k / cols) * 43, 40, 40};
}

std::string unit_type_name(const StringTable& strings, std::uint8_t nation, std::uint16_t type) {
    if (nation >= 5) return text_of(strings, "IDS_UNIT_NAME_UNIVERSAL_0");
    return text_of(strings, std::string("IDS_UNIT_NAME_") + kUnitNationKeys[nation] + "_" + std::to_string(type));
}

std::string nation_label(const StringTable& strings, std::uint8_t nation) {
    return nation < 5 ? text_of(strings, kMpsKeys[nation]) : std::string();
}

namespace {

// The type pays iron, gold or oil a tick (upkeep: Food, Wood, Iron, Gold, Oil, Power): what E1 reads.
bool plus_supplied(const sim::GameState& s, std::uint8_t nation, std::uint16_t type) {
    if (!s.plus_rules) return false;
    const sim::UnitType& t = s.unit_types[nation % 6][type % 40];
    return t.upkeep[2] > 0.0f || t.upkeep[3] > 0.0f || t.upkeep[4] > 0.0f;
}

// " Gold " -> "Gold": the game's resource names carry spaces for the cost text.
std::string trimmed(std::string v) {
    const auto a = v.find_first_not_of(' ');
    if (a == std::string::npos) return {};
    return v.substr(a, v.find_last_not_of(' ') - a + 1);
}

}  // namespace

std::vector<std::pair<std::string, int>> plus_upkeep(const sim::GameState& s, std::uint8_t nation, std::uint16_t type,
                                                     const StringTable& strings) {
    std::vector<std::pair<std::string, int>> out;
    if (!plus_supplied(s, nation, type) || s.speed_ms == 0) return out;
    const sim::UnitType& t = s.unit_types[nation % 6][type % 40];
    const double per_minute = 60000.0 / s.speed_ms;
    const struct {
        int index;
        const char* key;
    } kRes[] = {{3, "IDS_GOLD"}, {0, "IDS_FOOD"}, {2, "IDS_IRON"}, {4, "IDS_OIL"}, {1, "IDS_WOOD"}};
    for (const auto& c : kRes) {
        const long n = std::lround(static_cast<double>(t.upkeep[static_cast<std::size_t>(c.index)]) * per_minute);
        if (n > 0) out.emplace_back(text_of(strings, c.key), static_cast<int>(n));
    }
    return out;
}

std::string plus_upkeep_text(const sim::GameState& s, std::uint8_t nation, std::uint16_t type,
                             const StringTable& strings) {
    const auto list = plus_upkeep(s, nation, type, strings);
    if (list.empty()) return {};
    std::string text = ". " + text_of(strings, "plus.upkeep") + ":";
    for (const auto& [name, n] : list) text += name + " (" + std::to_string(n) + "), ";  // as train_tooltip's costs
    text.resize(text.size() - 2);
    return text;
}

bool plus_reload_slow(const sim::GameState& s, std::uint8_t player, std::uint8_t nation, std::uint16_t type) {
    if (player >= s.players.size() || !plus_supplied(s, nation, type)) return false;
    const sim::UnitType& t = s.unit_types[nation % 6][type % 40];
    const sim::Player& pl = s.players[player];
    return (t.upkeep[2] > 0.0f && pl.iron < 1.0) || (t.upkeep[3] > 0.0f && pl.gold < 1.0) ||
           (t.upkeep[4] > 0.0f && pl.oil < 1.0);
}

std::string plus_reload_hint(const sim::GameState& s, std::uint8_t player, std::uint8_t nation, std::uint16_t type,
                             const StringTable& strings) {
    if (!plus_reload_slow(s, player, nation, type)) return {};
    return text_of(strings, "plus.reload_slow") + ": " + text_of(strings, "plus.reload_slow_why");
}

std::string unit_info_line(const sim::GameState& s, const StringTable& strings, std::uint8_t me,
                           const sim::UnitRef& r) {
    const sim::Unit* u = sim::Unit_Find(s, r);
    if (u == nullptr || r.player >= s.players.size()) return {};
    const std::uint8_t nation = s.players[r.player].nation;
    const sim::UnitType& t = s.unit_types[sim::Unit_Nation(s, r.player, *u) % 6][u->type % 40];
    return unit_type_name(strings, nation, u->type) + ", " + nation_label(strings, nation) + "," +
           text_of(strings, sim::IsEnemy(s, me, r.player) ? "IDS_ENEMY" : "IDS_ALLY") + "(" +
           std::to_string(std::max(1, ftol(static_cast<double>(u->hp)))) + "/" +
           std::to_string(t.max_hp[r.player % 8]) + ")";
}

std::string house_info_line(const sim::GameState& s, const StringTable& strings, std::uint8_t me,
                            const sim::HouseRef& r) {
    if (r.player >= s.players.size() || r.slot >= s.players[r.player].houses.size()) return {};
    const sim::House& h = s.players[r.player].houses[r.slot];
    const std::uint8_t nation = s.players[r.player].nation;

    std::string line = house_type_name(strings, sim::House_Nation(s, r.player, h), h.type) + ", " +
                       nation_label(strings, nation) + "," +
                       text_of(strings, sim::IsEnemy(s, me, r.player) ? "IDS_ENEMY" : "IDS_ALLY");
    if (h.is_destroyed != 0) return line + text_of(strings, "IDS_HOUSE_DESTROYED");


    const int work = sim::House_Type(s, r.player, h).work_rate;
    double done = 1.0;
    if (h.field_020 != 0) done = work != 0 ? 1.0 - static_cast<double>(h.field_020) / work : 0.0;
    return line + "(" + std::to_string(std::max(1, ftol(done * static_cast<double>(h.hp)))) + "/" +
           std::to_string(h.max_hp) + ")";
}

UnitPanelSummary unit_panel_summary(const sim::GameState& s, const Selection& sel) {
    UnitPanelSummary u;
    if (sel.player >= s.players.size()) return u;
    const sim::Player& pl = s.players[sel.player];
    u.nation = pl.nation;
    bool first = true;
    for (const sim::UnitRef& ref : sel.units) {
        const sim::Unit* x = sim::Unit_Find(s, ref);
        if (x == nullptr) continue;
        const sim::UnitType& t = s.unit_types[sim::Unit_Nation(s, sel.player, *x) % 6][x->type % 40];
        ++u.count;
        u.hp += static_cast<double>(x->hp);
        u.max_hp += static_cast<double>(t.max_hp[sel.player % 8]);
        u.stars += unit_stars(x->field_08c);
        u.grenades += ftol(static_cast<double>(x->field_118));
        if (x->type == 5 || x->type == 21 || x->type == 22) u.gas_unit = true;
        if (x->in_group) u.in_group = true;
        if (t.field_009 != 0 && !x->in_group) u.can_group = true;
        if (x->field_006 == 0) u.fire_held = true;
        if (x->field_006 != 0) u.may_fire = true;
        u.passengers += static_cast<int>(x->passengers.size());
        if (first) {
            u.one_mode = true, u.mode = x->field_116;
            u.move_class = t.move_class, u.ship = t.is_ship != 0;
            first = false;
        } else if (x->field_116 != u.mode) {
            u.one_mode = false;
        }
        auto it = std::find_if(u.types.begin(), u.types.end(),
                               [&](const UnitPanelSummary::TypeCount& c) { return c.type == x->type; });
        if (it != u.types.end()) {
            ++it->count;
        } else if (u.types.size() < 20) {
            u.types.push_back({x->type, 1});
        }
    }
    if (u.count > 0) u.stars /= u.count;
    return u;
}

bool can_fire_at_point(const UnitPanelSummary& u) {
    if (u.types.size() != 1) return false;
    const std::uint16_t t = u.types[0].type;
    const std::uint8_t mc = u.move_class;
    const bool kind = mc == 5 || mc == 8 || mc == 3 || mc == 9 || mc == 10 || u.ship;
    return kind && t != 24 && t != 17 && !(u.nation == 1 && t == 11);
}

Panel unit_panel(const sim::GameState& s, const Selection& sel, const StringTable& strings) {
    Panel p;
    p.x = 10, p.bottom = 208, p.w = 610, p.h = 200;
    const UnitPanelSummary u = unit_panel_summary(s, sel);
    if (u.count == 0) return p;
    const sim::Player& pl = s.players[sel.player];

    // The frame's bevels (interface.les, 4 px): left, right, top, bottom, then the divider at x 188, 196 tall (it
    // stretches over 196 rows and stops at the bottom bevel; measured pixel for pixel in iface_one_soldier).
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "LBevel.bmp"), {0, 0, 4, 200}, 1));
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "RBevel.bmp"), {606, 0, 4, 200}, 1));
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "TBevel.bmp"), {0, 0, 610, 4}, 1));
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "BBevel.bmp"), {0, 196, 610, 4}, 1));
    p.widgets.push_back(picture(pic_named(kInterfaceLes, "RBevel.bmp"), {188, 0, 4, 196}, 1));

    // The type icons (Icons/UnitsIcons.les by entry id) with the count of each type above 1 (BankGothic 14).
    const int n = static_cast<int>(u.types.size());
    int y0 = 0;
    for (int k = 0; k < n; ++k) {
        const UnitPanelSummary::TypeCount& c = u.types[static_cast<std::size_t>(k)];
        const IRect r = type_icon_rect(n, k);
        PanelWidget w =
            button(pic_id(kUnitsIconsLes, unit_icon_id(u.nation, c.type)), r.x, r.y,
                   text_of(strings, "IDS_UNIT_SELECT_THIS_GROUP") + unit_type_name(strings, u.nation, c.type), 0,
                   kUpSelectType, c.type);
        w.right_action = kUpDropType, w.right_arg = c.type;
        if (c.count > 1) w.text = std::to_string(c.count);
        w.font = PanelFont::Sans14;
        p.widgets.push_back(std::move(w));
        y0 = std::max(y0, r.y + 40);
    }


    if (u.count == 1) {
        const sim::Unit* x = nullptr;
        for (const sim::UnitRef& r : sel.units) {  // the one that exists
            if ((x = sim::Unit_Find(s, r)) != nullptr) break;
        }
        const sim::UnitType& t = s.unit_types[sim::Unit_Nation(s, sel.player, *x) % 6][x->type % 40];
        const double max_hp = std::max<double>(1, t.max_hp[sel.player % 8]);
        p.widgets.push_back(bar("IconsLife.bmp", 5, y0, ftol(static_cast<double>(x->hp) * 40 / max_hp), 3));
        p.widgets.push_back(label(5, y0 + 2, unit_type_name(strings, u.nation, x->type)));
        p.widgets.push_back(label(5, y0 + 14, nation_label(strings, u.nation)));
        int y = y0 + 28;
        if (u.grenades > 0) {
            p.widgets.push_back(label(5, y, text_of(strings, "IDS_GRENADE") + std::to_string(u.grenades)));
            y += 14;
        }
        // Entente Engine Plus: the upkeep, one resource a line, and the slow reload when the stock is out (ours)
        const std::uint8_t nation = sim::Unit_Nation(s, sel.player, *x);
        const auto upkeep = plus_upkeep(s, nation, x->type, strings);
        if (!upkeep.empty()) {
            p.widgets.push_back(label(5, y, text_of(strings, "plus.upkeep") + ":"));
            for (const auto& [name, per_min] : upkeep) {
                p.widgets.push_back(label(5, y += 14, trimmed(name) + " (" + std::to_string(per_min) + ")"));
            }
            y += 14;
        }
        if (plus_reload_slow(s, sel.player, nation, x->type)) {
            p.widgets.push_back(label(5, y, text_of(strings, "plus.reload_slow") + ":"));
            p.widgets.push_back(label(5, y + 14, text_of(strings, "plus.reload_slow_why")));
        }
        stars(p, unit_stars(x->field_08c), 5, 7);
    } else {
        p.widgets.push_back(label(5, y0 + 5, std::to_string(u.count) + text_of(strings, "IDS_UNIT_SELECTED")));
        p.widgets.push_back(bar("IconsUpgradeTimer.bmp", 5, y0 + 2, 40, 3));
        const double max_hp = std::max<double>(1, u.max_hp);
        p.widgets.push_back(bar("IconsLife.bmp", 5, y0 + 2, ftol(u.hp * 40 / max_hp), 3));
        if (u.stars > 0) {
            p.widgets.push_back(label(5, y0 + 32, text_of(strings, "IDS_UNIT_EXPERIENCE")));
            stars(p, u.stars, 55, y0 + 36);
        }
        if (u.grenades > 0) {
            p.widgets.push_back(label(5, y0 + 18, text_of(strings, "IDS_GRENADE") + std::to_string(u.grenades)));
        }
    }



    if (shows_build_menu(u)) {
        build_menu_add(p, s, sel.player, strings);
        return p;
    }
    PanelWidget stop =
        button(pic_named(kInterfaceLes, "stop.bmp"), 195, 6, text_of(strings, "IDS_UNIT_STOP"), 's', kUpStop);
    PanelWidget hold =
        button(pic_named(kInterfaceLes, "nofire.bmp"), 236, 6, text_of(strings, "IDS_UNIT_NO_FIRE"), 'n', kUpNoFire);
    hold.active = u.fire_held;
    PanelWidget may =
        button(pic_named(kInterfaceLes, "mayfire.bmp"), 277, 6, text_of(strings, "IDS_UNIT_CAN_FIRE"), 'c', kUpMayFire);
    may.active = u.may_fire;
    p.widgets.push_back(std::move(stop));
    p.widgets.push_back(std::move(hold));
    p.widgets.push_back(std::move(may));
    if (can_fire_at_point(u)) {
        p.widgets.push_back(button(pic_named(kInterfaceLes, "firetopoint.bmp"), 195, 47,
                                   text_of(strings, "IDS_UNIT_FIRE_TO_POINT"), 'p', kUpFirePoint));
    }
    if (u.gas_unit && pl.research_9b3950[12] != 0 && pl.special_shells > 0) {
        PanelWidget gas = button(pic_named(kInterfaceGroupLes, "firetopointgas.bmp"), 238, 47,
                                 text_of(strings, "IDS_FIRE_TO_POINT_GAS"), 0, kUpFireGas);
        gas.text = std::to_string(pl.special_shells);
        p.widgets.push_back(std::move(gas));
    }
    if (u.can_group) {
        p.widgets.push_back(button(pic_named(kInterfaceGroupLes, "creategroup.bmp"), 515, 150,
                                   text_of(strings, "IDS_UNIT_OFFICER_RECT"), 0, kUpGroupCreate));
    }
    if (u.in_group) {
        p.widgets.push_back(button(pic_named(kInterfaceGroupLes, "dismissgroup.bmp"), 560, 150,
                                   text_of(strings, "IDS_UNIT_OFFICER_DISBAND"), 0, kUpGroupDismiss));
    }
    if (u.passengers > 0) {
        PanelWidget un = button(pic_named(kInterfaceGroupLes, "freefromtransport.bmp"), 470, 150,
                                text_of(strings, "IDS_FREE_FROM_TRANSPORT"), 't', kUpUnload);
        un.text = std::to_string(u.passengers);
        p.widgets.push_back(std::move(un));
    }
    // The attack types; the units' common mode is marked (mixed modes mark none).
    const struct {
        const char* pic;
        const char* tip;
        char key;
        int mode;
    } kModes[] = {{"attypefire.bmp", "IF_ONLY_FIRE", 'f', 1},
                  {"attypefight.bmp", "IF_ONLY_FIGHT", 'h', 2},
                  {"attypegrenade.bmp", "IF_ONLY_GRFIGHT", 'g', 3},
                  {"attypeundefined.bmp", "IF_ONLY_AUTOATTACK", 'j', 0xff},
                  {"defend.bmp", "IDS_DEFEND_ONLY", 'd', 4}};
    int x = 195;
    for (const auto& m : kModes) {
        PanelWidget b =
            button(pic_named(kInterfaceGroupLes, m.pic), x, 150, text_of(strings, m.tip), m.key, kUpSetMode, m.mode);
        b.active = u.one_mode && u.mode == m.mode;
        p.widgets.push_back(std::move(b));
        x += 45;
    }
    return p;
}

UnitPanelResult unit_panel_click(const sim::GameState& s, Selection& sel, PanelClick click, bool ctrl) {
    UnitPanelResult r;
    auto keep = [&](std::uint16_t type, bool drop) {
        std::vector<sim::UnitRef> out;
        for (const sim::UnitRef& ref : sel.units) {
            const sim::Unit* u = sim::Unit_Find(s, ref);
            if (u != nullptr && (u->type == type) != drop) out.push_back(ref);
        }
        sel.units = std::move(out);
    };
    auto order = [&](Command::Kind kind, std::vector<sim::OrderArg> args, const char* what) {
        if (sel.units.empty()) return;
        Command c;
        c.kind = kind;
        c.player = sel.player;
        c.args = std::move(args);
        c.units = sel.units;
        r.order.push_back(std::move(c));
        r.what = what;
    };
    const std::uint32_t p = sel.player;
    switch (click.action) {
        case kUpSelectType:
            keep(static_cast<std::uint16_t>(click.arg), ctrl);
            r.what = ctrl ? "drop a type from the selection" : "keep one type in the selection";
            break;
        case kUpDropType:
            keep(static_cast<std::uint16_t>(click.arg), true);
            r.what = "drop a type from the selection";
            break;
        case kUpStop: order(Command::Kind::Stop, {raw(p)}, "stop"); break;
        case kUpNoFire: order(Command::Kind::NoFire, {raw(p)}, "hold fire"); break;
        case kUpMayFire: order(Command::Kind::MayFire, {raw(p)}, "allow fire"); break;
        case kUpSetMode:
            order(Command::Kind::SetMode, {raw(p), raw(static_cast<std::uint32_t>(click.arg))}, "attack type");
            break;
        case kUpGroupCreate: order(Command::Kind::GroupCreate, {raw(p)}, "group units"); break;
        case kUpGroupDismiss: order(Command::Kind::GroupDismiss, {raw(p)}, "disband"); break;
        case kUpFirePoint:
        case kUpFireGas:
            r.fire_point = click.action == kUpFireGas ? 1 : 0;
            r.what = "fire at a point: right click on the map, a left click cancels";
            break;
        case kUpUnload: order(Command::Kind::Unload, {raw(p)}, "unload"); break;
        case kUpBuild:
            if (sim::House_CanBuild(s, static_cast<std::uint8_t>(p), static_cast<std::uint16_t>(click.arg))) {
                r.build = click.arg;
                r.what =
                    click.arg == kPontoonType
                        ? "lay a pontoon bridge: left click where it starts, then where it ends; right click cancels"
                        : "place the house: left click on the map, right click cancels";
            }
            break;
        default: break;
    }
    return r;
}

}  // namespace antanta::ui
