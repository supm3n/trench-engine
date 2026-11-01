// Every number of the network game in one place (net/config.hpp).
#include "config.hpp"

#include <cstdlib>

namespace antanta::net {

namespace {

struct Field {
    const char* name;
    std::uint32_t NetConfig::* u32;
};

const Field kFields[] = {
    {"max_games", &NetConfig::max_games},
    {"hello_timeout_ms", &NetConfig::hello_timeout_ms},
    {"tick_ms", &NetConfig::tick_ms},
    {"min_tick_ms", &NetConfig::min_tick_ms},
    {"max_tick_ms", &NetConfig::max_tick_ms},
    {"input_delay", &NetConfig::input_delay},
    {"min_input_delay", &NetConfig::min_input_delay},
    {"max_input_delay", &NetConfig::max_input_delay},
    {"hash_every", &NetConfig::hash_every},
    {"report_ms", &NetConfig::report_ms},
    {"step_headroom_pct", &NetConfig::step_headroom_pct},
    {"max_lag_ticks", &NetConfig::max_lag_ticks},
    {"catch_up_ticks", &NetConfig::catch_up_ticks},
    {"seat_hold_s", &NetConfig::seat_hold_s},
    {"ai_level", &NetConfig::ai_level},
    {"autosave_minutes", &NetConfig::autosave_minutes},
    {"autosave_ticks", &NetConfig::autosave_ticks},
};

bool Number(const std::string& value, std::uint32_t* out) {
    if (value.empty() || value.size() > 9 || value.find_first_not_of("0123456789") != std::string::npos) return false;
    *out = static_cast<std::uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
    return true;
}

}  // namespace

bool NetConfig_Set(NetConfig& c, const std::string& key, const std::string& value) {
    std::uint32_t v = 0;
    if (!Number(value, &v)) return false;
    if (key == "max_humans") {
        if (v < 2 || v > 8) return false;
        c.max_humans = static_cast<std::uint8_t>(v);
        return true;
    }
    if ((key == "ai_level" && v > 3) || (key == "autosave_minutes" && v > 15)) return false;
    for (const Field& f : kFields) {
        if (key == f.name) {
            c.*f.u32 = v;
            return true;
        }
    }
    return false;
}

std::uint32_t NetConfig_AutosaveTicks(const NetConfig& c, std::uint32_t tick_ms) {
    if (c.autosave_ticks != 0) return c.autosave_ticks;
    if (c.autosave_minutes == 0) return 0;
    const std::uint32_t minutes = c.autosave_minutes > 15 ? 15 : c.autosave_minutes;
    return minutes * 60000 / (tick_ms == 0 ? 30 : tick_ms);
}

std::string NetConfig_Text(const NetConfig& c) {
    std::string out = "max_humans=" + std::to_string(c.max_humans) + "\n";
    for (const Field& f : kFields) out += std::string(f.name) + "=" + std::to_string(c.*f.u32) + "\n";
    return out;
}

}  // namespace antanta::net
