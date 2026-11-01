#include "ui/game_menu.hpp"

namespace antanta::ui {

GameMenuItem game_menu_item(std::uint16_t handler) {
    switch (handler) {
        case 9: return GameMenuItem::Restart;
        case 10: return GameMenuItem::Settings;
        case 11: return GameMenuItem::Continue;
        case 12: return GameMenuItem::Save;
        case 13: return GameMenuItem::Load;
        case 14: return GameMenuItem::BackToMenu;
        default: return GameMenuItem::None;
    }
}

std::string game_menu_unavailable(const Language& l, GameMenuItem item, bool can_restart) {
    switch (item) {
        case GameMenuItem::Restart: return can_restart ? std::string() : lang_text(l, "game_menu.restart_unavailable");
        default: return {};
    }
}

std::string game_menu_confirm_key(GameMenuItem item) {
    if (item == GameMenuItem::Restart) return "IDS_CONFIRM_RESTART";
    if (item == GameMenuItem::BackToMenu) return "IDS_CONFIRM_BACK_TO_MENU";
    if (item == GameMenuItem::QuickLoad) return "IDS_CONFIRM_QUICK_LOAD";
    return {};
}

sim::SkirmishSetup game_menu_restart_setup(const sim::SkirmishSetup& last, const sim::GameState& s,
                                           std::uint32_t seed) {
    sim::SkirmishSetup out = last;
    out.seed = seed;
    out.comp_names = {};  // the app names the computer players by their new positions
    out.slots = {};
    const std::size_t count = s.slot_count < s.slots.size() ? s.slot_count : s.slots.size();
    std::size_t n = 0;
    auto add = [&](const sim::GameState::SlotRecord& r) {
        out.slots[n++] = sim::SkirmishSlot{r.level, r.nation, r.colour, r.team};
    };
    if (s.my_player < count) add(s.slots[s.my_player]);
    for (std::size_t p = 0; p < count; ++p) {
        if (p != s.my_player && s.slots[p].level != 0) add(s.slots[p]);
    }
    out.players = static_cast<std::uint8_t>(n < 2 ? 2 : n);  // a game alone: the second row stays closed ("No")
    return out;
}

}  // namespace antanta::ui
