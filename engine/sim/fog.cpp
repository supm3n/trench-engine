// The fog of war of the local player's side (sim/fog.hpp, docs/fog.md). Reads the state, writes GameState::fog only;
// no RNG and no CRT rand (docs/fog.md 4.3).
#include "fog.hpp"

#include <algorithm>

#include "movement.hpp"
#include "strike.hpp"

namespace antanta::sim {



void Fog_Rebuild(GameState& s) {
    Fog& f = s.fog;
    f.built = true;
    f.on = s.fog_553ae0 != 0;
    f.tick = s.tick;
    f.circles.clear();
    const std::uint8_t me = s.my_player;
    for (std::size_t q = 0; q < s.players.size(); ++q) {
        const auto p = static_cast<std::uint8_t>(q);
        if (IsEnemy(s, p, me)) continue;
        if (p != me && s.skirmish_flag == 0 && s.show_fog_allies == 0) continue;  // network: never in single player
        const Player& pl = s.players[q];
        for (std::uint32_t k = pl.first_unit; k != kNoSlot; k = pl.unit_slots[k].next) {
            const Unit& u = pl.unit_slots[k];
            const auto sight = static_cast<std::uint8_t>(Unit_Type(s, p, u).field_588);
            std::uint8_t sv = sight;
            if (u.is_deleted != 0) {

                if (u.field_0ca <= 0xee6a) continue;
                const std::int32_t red = (0xffff - static_cast<std::int32_t>(u.field_0ca)) * sight / 4500;
                sv = static_cast<std::uint8_t>(sight - static_cast<std::uint8_t>(red));
            }
            if (sv == 0 || sv >= 40) continue;
            f.circles.push_back(FogCircle{u.map_x, u.map_y, u.x, u.y, sv, true});
        }
        for (const House& h : pl.houses) {
            if (h.is_destroyed != 0) continue;
            const std::uint8_t sv = h.field_08c;
            if (sv == 0 || sv >= 40) continue;
            const float x = static_cast<float>((2 * h.map_x - (h.map_y % 2 == 0 ? 1 : 0)) * 48);
            const float y = static_cast<float>(h.map_y * 24);
            f.circles.push_back(FogCircle{h.map_x, h.map_y, x, y, sv, false});
        }
    }
    for (const GameState::Reveal& r : s.reveal) {
        if (r.radius == 0 || r.radius >= 40) continue;
        const float x = static_cast<float>((2 * r.x - (r.y % 2 == 0 ? 1 : 0)) * 48);
        const float y = static_cast<float>(r.y * 24);
        f.circles.push_back(FogCircle{r.x, r.y, x, y, r.radius, false});
    }
    // Entente Engine Plus: MyPlayer's own recon flight (sim/strike.hpp), a circle until its end tick
    if (s.plus_rules && me < s.players.size() && s.tick < s.players[me].plus_recon_end) {
        const Player& pl = s.players[me];
        const float x = static_cast<float>((2 * pl.plus_recon_x - (pl.plus_recon_y % 2 == 0 ? 1 : 0)) * 48);
        const float y = static_cast<float>(pl.plus_recon_y * 24);
        const auto r = static_cast<std::uint8_t>(Strike_Info(kStrikeRecon).radius);
        f.circles.push_back(FogCircle{pl.plus_recon_x, pl.plus_recon_y, x, y, r, false});
    }
}

std::vector<std::uint8_t> Fog_CellMask(const Fog& fog, std::int32_t x0, std::int32_t y0, std::int32_t x1,
                                       std::int32_t y1) {
    const std::int32_t w = std::max(0, x1 - x0), h = std::max(0, y1 - y0);
    std::vector<std::uint8_t> m(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), fog.on ? 0 : 1);
    if (!fog.on) return m;
    for (const FogCircle& c : fog.circles) {
        const std::int32_t sv = c.sight, lim = (2 * sv + 1) * (2 * sv + 1);
        const std::int32_t ya = std::max(y0, c.map_y - 2 * sv), yb = std::min(y1 - 1, c.map_y + 2 * sv);
        const std::int32_t xa = std::max(x0, c.map_x - sv), xb = std::min(x1 - 1, c.map_x + sv);
        for (std::int32_t y = ya; y <= yb; ++y) {
            const std::int32_t dy = y - c.map_y;
            std::uint8_t* row = m.data() + static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(w);
            for (std::int32_t x = xa; x <= xb; ++x) {
                const std::int32_t dx = x - c.map_x;
                if (4 * dx * dx + dy * dy < lim) row[x - x0] = 1;  // sqrt(dx^2 + (dy / 2)^2) < s + 0.5
            }
        }
    }
    return m;
}

std::vector<std::uint8_t> Fog_MinimapMask(const Fog& fog, std::int32_t map_w, std::int32_t map_h, std::int32_t n) {
    std::vector<std::uint8_t> m(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), fog.on ? 0 : 1);
    if (!fog.on || map_w <= 0 || map_h <= 0 || n <= 0) return m;

    const double kx = static_cast<float>(static_cast<double>(n) / (map_w * 96.0));
    const double ky = static_cast<float>(static_cast<double>(n) / (map_h * 24.0));
    const double f = map_w == 300 ? 2.0 / 3.0 : 1.0;
    const std::int32_t k = std::max(1, n / 200);
    for (const FogCircle& c : fog.circles) {
        const std::int32_t rm = c.unit ? Ftol((c.sight - 1) * f) : Ftol(c.sight * f);
        if (rm <= 0) continue;
        const std::int32_t cx = Ftol(static_cast<double>(c.x) * kx), cy = Ftol(static_cast<double>(c.y) * ky);
        // sqrt(dx^2 + dy^2) < (rm + 0.5) k
        const std::int64_t lim = static_cast<std::int64_t>(k * (2 * rm + 1)) * (k * (2 * rm + 1));
        const std::int32_t r = k * rm + k;
        for (std::int32_t y = std::max(0, cy - r); y <= std::min(n - 1, cy + r); ++y) {
            const std::int64_t dy = y - cy;
            for (std::int32_t x = std::max(0, cx - r); x <= std::min(n - 1, cx + r); ++x) {
                const std::int64_t dx = x - cx;
                if (4 * (dx * dx + dy * dy) < lim) m[static_cast<std::size_t>(y) * n + x] = 1;
            }
        }
    }
    return m;
}

}  // namespace antanta::sim
