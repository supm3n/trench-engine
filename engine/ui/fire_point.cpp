#include "ui/fire_point.hpp"

#include <bit>

namespace antanta::ui {

std::vector<sim::Command> fire_point_orders(const sim::GameState& s, const Selection& sel, std::int32_t x,
                                            std::int32_t y, std::uint8_t gas) {
    std::vector<sim::Command> out;
    if (sel.units.empty() || sel.player >= s.players.size()) return out;

    std::int32_t h = 50, cx = -1, cy = -1;
    const std::int32_t w = s.map.width, hh = s.map.height;
    if (pick_cell(s, x, y, &cx, &cy) && cx > 1 && cy > 1 && cx < w - 2 && cy < hh - 2) {
        h = s.map.heights[static_cast<std::size_t>(cy) * static_cast<std::size_t>(w) + static_cast<std::size_t>(cx)];
    }
    const auto fx = static_cast<float>(x);
    const auto fy = static_cast<float>(h) + static_cast<float>(y);
    for (const sim::UnitRef& u : sel.units) {
        sim::Command c;
        c.kind = sim::Command::Kind::OrderPoint;
        c.player = sel.player;
        sim::OrderArg a;
        a.tag = sim::OrderArg::Tag::Unit, a.unit = u;
        c.args.push_back(a);
        for (const std::uint32_t v :
             {std::bit_cast<std::uint32_t>(fx), std::bit_cast<std::uint32_t>(fy), static_cast<std::uint32_t>(gas)}) {
            sim::OrderArg b;
            b.raw = v;
            c.args.push_back(b);
        }
        c.units = {u};
        out.push_back(std::move(c));
    }
    return out;
}

}  // namespace antanta::ui
