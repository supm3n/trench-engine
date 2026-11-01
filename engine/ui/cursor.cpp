#include "ui/cursor.hpp"

#include "sim/fog.hpp"
#include "sim/movement.hpp"

namespace antanta::ui {

const char* cursor_file(Cursor c) {
    switch (c) {
        case Cursor::Default: return "Cursor_Default.bmp";
        case Cursor::Patrol: return "Cursor_Patrol.bmp";
        case Cursor::Build: return "Cursor_Build.bmp";
        case Cursor::SetExit: return "Way_point2.bmp";
        case Cursor::Attack: return "Cursor_Attack.bmp";
        case Cursor::AttackPoint: return "Cursor_AttackPoint.bmp";
        case Cursor::Food: return "Cursor_Food.bmp";
        case Cursor::GoIntoHouse: return "Cursor_GoIntoHouse.bmp";
    }
    return "Cursor_Default.bmp";
}

Cursor cursor_choice(const CursorState& st, const CursorFacts& f) {
    using T = CursorFacts::Target;
    if (st.paused) return Cursor::Default;
    if (st.kept == Cursor::SetExit || st.kept == Cursor::AttackPoint || st.kept == Cursor::Patrol) return st.kept;
    if (st.over_hud || st.building || !f.units) return Cursor::Default;
    if ((f.target == T::EnemyUnit || (f.target == T::EnemyHouse && !f.destroyed)) && !f.fogged) return Cursor::Attack;
    if (f.fogged) return Cursor::Default;
    if (f.target == T::Field) return f.workers ? Cursor::Food : Cursor::Default;
    if (f.target != T::OwnHouse || f.destroyed) return Cursor::Default;
    if (f.workers) {
        if (f.unfinished || f.damaged) return Cursor::Build;
        return f.room ? Cursor::GoIntoHouse : Cursor::Default;
    }
    return !f.unfinished && f.room && f.can_enter ? Cursor::GoIntoHouse : Cursor::Default;
}

CursorFacts cursor_facts(const sim::GameState& s, const WorldGraphics* g, const Selection& sel, ViewPos pos, int sx,
                         int sy) {
    using T = CursorFacts::Target;
    CursorFacts f;
    const std::uint8_t p = sel.player;
    if (p >= s.players.size()) return f;
    for (const sim::UnitRef& r : sel.units) {
        const sim::Unit* u = sim::Unit_Find(s, r);
        if (u == nullptr) continue;
        const sim::UnitType& t = sim::Unit_Type(s, p, *u);
        f.units = true;
        f.workers = f.workers || t.move_class == 6;
        f.can_enter = f.can_enter || t.field_006 != 0 || t.move_class == 6;
    }
    if (!f.units) return f;

    const WorldPoint w = screen_to_world(pos, sx, sy);
    std::int32_t cx = 0, cy = 0;
    if (s.fog.built && s.fog.on && pick_cell(s, w.x, w.y, &cx, &cy)) {
        f.fogged = sim::Fog_CellMask(s.fog, cx, cy, cx + 1, cy + 1)[0] == 0;
    }
    const RightClickTarget t = right_click_target(s, g, p, pos, sx, sy);
    if (!t.on_map) return f;
    if (t.house) {
        const sim::House& h = s.players[t.house->player].houses[t.house->slot];
        if (t.house->player == p) {
            f.target = T::OwnHouse;
        } else if (sim::IsEnemy(s, p, t.house->player)) {
            f.target = T::EnemyHouse;
        } else {
            return f;  // an allied house: a move (click kind 0)
        }
        f.destroyed = h.is_destroyed != 0;
        f.unfinished = h.field_020 != 0;
        f.damaged = h.hp < static_cast<float>(h.max_hp);
        f.room = h.units_inside < h.capacity;
    } else if (t.target) {
        f.target = T::EnemyUnit;
    } else if (t.obj >= 0x25de && t.obj <= 0x25e3) {
        f.target = T::Field;
    }
    return f;
}

}  // namespace antanta::ui
