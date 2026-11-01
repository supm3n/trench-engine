#include "ui/sound.hpp"

#include <cmath>

namespace antanta::ui {

namespace {

constexpr double kMinFactor = 0.4, kMaxFactor = 1.2;
constexpr double kRolloff = 5.0;
constexpr double kPi = 3.14159265358979323846;



struct HouseName {
    std::int16_t key;
    const char* name;
};
constexpr HouseName kHouseNames[55] = {
    {250, "common\\Electro_Station\\Electro"},
    {270, "common\\air_hangar\\AHangar"},
    {265, "common\\barn\\barn"},
    {261, "common\\gold_mine\\gold_mine"},
    {271, "common\\guard_tower\\gtower"},
    {272, "common\\harbour\\harbour"},
    {267, "common\\hospital\\hospital"},
    {262, "common\\lesopilka\\lesopilka"},
    {258, "common\\market\\Market"},
    {263, "common\\oil_pump\\oil_pump"},
    {260, "common\\ore_mine\\ore_mine"},
    {257, "common\\pillbox\\pillbox"},
    {273, "common\\stables\\stables"},
    {271, "common\\Guard_Tower\\GTower"},
    {268, "common\\Medic_Spot\\Medic_Spot"},
    {259, "common\\House\\House"},
    {252, "common\\Sci_Acad\\Sci_Acad"},
    {6, "russia\\city_center\\city_center"},
    {4, "russia\\Quarters\\Quarters"},
    {16, "russia\\Artillery_Depot\\Arti_Depot"},
    {14, "russia\\Mint\\Mint"},
    {19, "russia\\Tank_Plant\\Tank_Plant"},
    {3, "russia\\Build_Acad\\Build_Acad"},
    {5, "russia\\War_Acad\\War_Acad"},
    {31, "england\\city_center\\city_center"},
    {29, "england\\Quarters\\Quarters"},
    {41, "england\\Artillery_Depot\\Arti_Depot"},
    {39, "england\\Mint\\Mint"},
    {44, "england\\Tank_Plant\\Tank_Plant"},
    {28, "england\\Build_Acad\\Build_Acad"},
    {30, "england\\War_Acad\\War_Acad"},
    {56, "france\\city_center\\city_center"},
    {54, "france\\Quarters\\Quarters"},
    {66, "france\\Artillery_Depot\\Arti_Depot"},
    {64, "france\\Mint\\Mint"},
    {69, "france\\Tank_Plant\\Tank_Plant"},
    {53, "france\\Build_Acad\\Build_Acad"},
    {55, "france\\War_Acad\\War_Acad"},
    {81, "germany\\city_center\\city_center"},
    {79, "germany\\Quarters\\Quarters"},
    {91, "germany\\Artillery_Depot\\Arti_Depot"},
    {89, "germany\\Mint\\Mint"},
    {94, "germany\\Tank_Plant\\Tank_Plant"},
    {78, "germany\\Build_Acad\\Build_Acad"},
    {80, "germany\\War_Acad\\War_Acad"},
    {106, "austria\\city_center\\city_center"},
    {104, "austria\\Quarters\\Quarters"},
    {116, "austria\\Artillery_Depot\\Arti_Depot"},
    {114, "austria\\Mint\\Mint"},
    {119, "austria\\Tank_Plant\\Tank_Plant"},
    {103, "austria\\Build_Acad\\Build_Acad"},
    {105, "austria\\War_Acad\\War_Acad"},
    {251, "common\\constructing_buildings\\const_buildings"},
    {252, "common\\dead_building\\dead_building"},
    {254, "common\\Anounces\\UpgComplete"},
};

constexpr std::uint8_t kCommonHouse[25] = {1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0};


constexpr const char* kMusic[kMusicTracks] = {
    "Theme1.ogg",
    "Theme3.ogg",
    "Theme2.ogg",
    "Theme1.ogg",
    "akt2.ogg",
    "akt4.ogg",
    "akt7.ogg",
    "France.ogg",
    "akt5.ogg",
    "akt1.ogg",
    "akt6.ogg",
    "akt8.ogg",
    "Marsh.ogg",
    "StartGame.ogg",
    "National\\Russia.ogg",
    "National\\England.ogg",
    "National\\France.ogg",
    "National\\Germany.ogg",
    "National\\Avstrya.ogg",
    "CampaignMenu\\Russia.ogg",
    "CampaignMenu\\Englich.ogg",
    "CampaignMenu\\France.ogg",
    "CampaignMenu\\Germany.ogg",
    "CampaignMenu\\Avstriya.ogg",
};

// "media\" + a name with '\' separators, as a path relative to the game folder with '/'.
std::string media(const std::string& name) {
    std::string p = "media/" + name;
    for (char& c : p) {
        if (c == '\\') c = '/';
    }
    return p;
}

double clamp_gain(double g) { return g < 0.0 ? 0.0 : g; }

}  // namespace

int sound_volume_mb(std::uint8_t volume) { return (static_cast<int>(volume) - 100) * 20; }

double gain_of_mb(double mb) { return std::pow(10.0, mb / 2000.0); }

Placement place_sound(double x, double y, int screen_h) {
    Placement p;
    p.distance = std::sqrt(x * x + y * y);
    const double lo = kMinFactor * screen_h, hi = kMaxFactor * screen_h;
    if (p.distance > hi) {
        p.audible = false;
        return p;
    }
    if (p.distance > lo) p.attenuation_db = -6.02 * kRolloff * std::log2(p.distance / lo);
    if (p.distance > 0.0) {
        const double pan = x / p.distance;  // -1 left .. 1 right
        p.left = static_cast<float>(pan > 0.0 ? std::cos(pan * kPi / 2.0) : 1.0);
        p.right = static_cast<float>(pan < 0.0 ? std::cos(-pan * kPi / 2.0) : 1.0);
    }
    return p;
}

Placement place_centred() { return Placement{}; }

SoundData sound_data_from_params(std::span<const BasicParams> params) {
    SoundData d;
    for (std::size_t n = 0; n < params.size() && n < 6; ++n) {
        for (const UnitParams& u : params[n].units) d.units[n].push_back(u.sounds);
        for (const HouseParams& h : params[n].houses) {
            d.houses[n].push_back(HouseGeometry{h.width, h.height, h.offset_x, h.offset_y, h.fire_y0});
        }
    }
    return d;
}

int sound_rand(SoundBoard& b) {
    b.holdrand = b.holdrand * 214013u + 2531011u;
    return static_cast<int>((b.holdrand >> 16) & 0x7fff);
}



SlotChoice choose_slot(const SoundBoard& b, std::uint8_t kind, std::uint8_t variant, std::uint16_t id,
                       std::int32_t distance, std::uint8_t priority, bool loop) {
    int n_kind = 0, n_kv = 0, same = -1;
    std::int32_t far = -1;
    for (int i = 0; i < kSoundSlots; ++i) {
        const SoundSlot& s = b.slots[static_cast<std::size_t>(i)];
        if (s.priority == 0 || s.kind != kind) continue;
        ++n_kind;
        if (s.variant != variant) continue;
        ++n_kv;
        if (s.distance > far) far = s.distance;
        if (s.id == id && same < 0) same = i;
    }
    SlotChoice c;
    if (id != 0) {
        if (same >= 0) {
            if (loop) c.slot = same, c.refresh = true;
            return c;  // a non-looping sound of this source and variant is still there: dropped
        }
        if (n_kind > 2 && n_kv > 2) return c;
    } else if (n_kind > 2 && n_kv > 2 && !(distance + 300 <= far)) {
        return c;
    }
    int low = -1;
    for (int i = 0; i < kSoundSlots; ++i) {
        const SoundSlot& s = b.slots[static_cast<std::size_t>(i)];
        if (s.priority == 0) {
            c.slot = i;
            return c;
        }
        if (low < 0 || s.priority < b.slots[static_cast<std::size_t>(low)].priority) low = i;
    }
    if (b.slots[static_cast<std::size_t>(low)].priority <= priority) c.slot = low;
    return c;
}



std::string house_sound_file(SoundBoard& b, std::uint8_t nation, std::uint16_t type, std::uint8_t variant) {
    if (variant == 6) return media("units\\weapons\\machinegun2\\machinegun_1.wav");
    if (variant == 4) {
        // Fix 1: the original looks key 252 up and finds Sci_Acad (index 16) before dead_building (index 53).
        return media(std::string("Buildings\\") + kHouseNames[53].name + "_" + std::to_string(sound_rand(b) % 3 + 1) +
                     ".wav");
    }
    int key = 0;
    if (variant == 3) {
        key = 251;
    } else if (variant == 5) {
        key = 254;
    } else {
        key = type < 25 && kCommonHouse[type] == 1 ? type + 250 : nation * 25 + type;
    }
    for (const HouseName& h : kHouseNames) {
        if (h.key == key)
            return media(std::string("Buildings\\") + h.name + "_" + std::to_string(sound_rand(b) % 3 + 1) + ".wav");
    }
    return media("Buildings\\nosound.wav");
}



std::string unit_sound_file(SoundBoard& b, const UnitSound& entry) {
    if (entry.count == 0 || entry.name.empty()) return {};
    std::string name = entry.name;
    if (entry.count > 1 && name.size() >= 6) {
        const int k = sound_rand(b) % entry.count + 1;
        name = name.substr(0, name.size() - 6) + "_" + std::to_string(k) + ".wav";
        const std::string typo = "RUSSIA\\Artillery\\Artil_Att_";
        if (k >= 4 && k <= 6 && name.compare(0, typo.size(), typo) == 0) {
            name = "RUSSIA\\Artillery\\Atril_Att_" + std::to_string(k) + ".wav";
        }
    }
    return media("Units\\" + name);
}

std::string sound_file(SoundBoard& b, std::uint8_t kind, std::uint8_t variant) {
    switch (kind) {
        case 1:  // Button_YES / Button_NO (variants 1, 2) are stereo and have no caller
            return media(variant == 1   ? "Clicks\\Button_YES.wav"
                         : variant == 2 ? "Clicks\\Button_NO.wav"
                                        : "Clicks\\Button.wav");
        case 4: {
            const int k = variant == 1 ? sound_rand(b) % 6 + 1 : sound_rand(b) % 3 + 7;
            return media("Misc\\LeadIns\\LeadIn_" + std::to_string(k) + ".wav");
        }
        case 6: {
            const char* names[6] = {"Small", "Medium", "Big", "Big", "Water_Medium", "Water_Small"};
            const char* name = variant < 6 ? names[variant] : "Big";
            const int k = variant >= 3 && variant <= 5 ? sound_rand(b) % 2 + 1 : sound_rand(b) % 3 + 1;
            return media(std::string("Misc\\Blasts\\") + name + "_" + std::to_string(k) + ".wav");
        }
        case 7: return media("Misc\\Fire\\Fire.wav");
        case 8: return media(variant == 3 ? "Misc\\Ambience\\Winter.wav" : "Misc\\Ambience\\Country.wav");
        case 9:
            if (variant == 1) return media("Misc\\Horns\\You_Win.wav");
            if (variant == 2) return media("Misc\\Horns\\You_Lose.wav");
            if (variant == 3) return media("Misc\\Anounces\\AirRaid.wav");
            if (variant == 4) return media("Misc\\Anounces\\Attack_" + std::to_string(sound_rand(b) % 3 + 1) + ".wav");
            return {};
        default: return {};
    }
}

std::optional<SoundCommand> play_sound(SoundBoard& b, const SoundData& d, const Listener& l, const sim::SimEvent& e) {
    if (!b.on || e.kind != sim::SimEvent::Kind::Sound) return std::nullopt;
    std::uint8_t priority = e.priority == 0 ? 1 : e.priority > 8 ? 8 : e.priority;
    std::uint8_t variant = e.variant;
    std::uint16_t id = 0;
    bool loop = false;
    Placement p = place_centred();
    const UnitSound* entry = nullptr;
    // The screen centre in world pixels, as the original's int (W / 2 + view_x).
    const double cx = static_cast<double>(l.w / 2) + l.view_x, cy = static_cast<double>(l.h / 2) + l.view_y;
    switch (e.sound) {
        case 1:
        case 8:
        case 9: break;
        case 2: {
            id = static_cast<std::uint16_t>(e.nation * 100 + e.type);
            if (variant == 1 || variant == 2 || variant == 5) {
                priority = 6;
                break;
            }
            const std::vector<HouseGeometry>& hs = d.houses[e.nation % 6];
            const HouseGeometry g = e.type < hs.size() ? hs[e.type] : HouseGeometry{};
            const double x = e.x - cx + static_cast<double>((g.w >> 1) - g.ox);
            const double y = (e.y - cy) - static_cast<double>((g.h >> 1) - g.oy);
            p = place_sound(x, y, l.h);
            if (!p.audible) return std::nullopt;
            if (variant != 6 && e.build_left != 0) variant = 3;
            break;
        }
        case 3: {
            if (variant > 9) return std::nullopt;
            const auto& units = d.units[e.nation % 6];
            if (e.type >= units.size()) return std::nullopt;
            entry = &units[e.type][variant];
            if (entry->count == 0 || entry->name.empty()) return std::nullopt;
            id = static_cast<std::uint16_t>(e.id + 1);
            loop = entry->loop != 0;
            if (entry->positioned != 0) {
                const double x = e.x - cx, y = e.y - cy;

                if (e.flag && (std::fabs(x) > l.w / 2.0 || std::fabs(y) > l.h / 2.0)) return std::nullopt;
                p = place_sound(x, y, l.h);
                if (!p.audible) return std::nullopt;
            }
            break;
        }
        case 4:
        case 6:
            p = place_sound(e.x - l.w / 2 - l.view_x, e.y - l.h / 2 - l.view_y, l.h);
            if (!p.audible) return std::nullopt;
            break;
        case 7: {
            id = e.id;
            loop = true;
            variant = 0;
            p = place_sound(e.x, e.y, l.h);  // the fire's offset is computed by fire_sounds
            if (!p.audible) return std::nullopt;
            break;
        }
        default: return std::nullopt;
    }
    const auto dist = static_cast<std::int32_t>(p.distance);
    const SlotChoice c = choose_slot(b, e.sound, variant, id, dist, priority, loop);
    if (c.slot < 0) return std::nullopt;
    SoundSlot& slot = b.slots[static_cast<std::size_t>(c.slot)];
    const double g = gain_of_mb(sound_volume_mb(b.volume) + p.attenuation_db * 100.0);
    SoundCommand cmd;
    cmd.slot = c.slot;
    cmd.left = static_cast<float>(clamp_gain(g * p.left)), cmd.right = static_cast<float>(clamp_gain(g * p.right));
    if (c.refresh) {
        slot.keep_alive = 1;
        cmd.op = SoundCommand::Op::Refresh;
        return cmd;
    }
    std::string file;
    if (e.sound == 2) {
        file = house_sound_file(b, e.nation, e.type, variant);
    } else if (e.sound == 3) {
        file = unit_sound_file(b, *entry);
    } else {
        file = sound_file(b, e.sound, variant);
    }
    if (file.empty()) return std::nullopt;
    slot = SoundSlot{priority, e.sound, variant, id, dist, loop, 0};
    cmd.op = SoundCommand::Op::Start, cmd.file = std::move(file), cmd.loop = loop;
    return cmd;
}

void sound_failed(SoundBoard& b, int slot) {
    if (slot >= 0 && slot < kSoundSlots) b.slots[static_cast<std::size_t>(slot)] = SoundSlot{};
}

sim::SimEvent unit_sound_event(const sim::GameState& s, std::uint8_t player, const sim::Unit& u, std::uint8_t variant,
                               std::uint8_t priority) {
    sim::SimEvent e;
    e.sound = 3, e.variant = variant, e.priority = priority;
    e.player = player, e.nation = player < s.players.size() ? sim::Unit_Nation(s, player, u) : 0;  // Basic's sounds
    e.type = u.type, e.id = u.id;
    e.x = u.x, e.y = static_cast<float>(static_cast<double>(u.y) - u.field_024 - u.z);
    return e;
}

sim::SimEvent house_sound_event(const sim::GameState& s, std::uint8_t player, const sim::House& h, std::uint8_t variant,
                                std::uint8_t priority) {
    sim::SimEvent e;
    e.sound = 2, e.variant = variant, e.priority = priority;
    e.player = player, e.nation = player < s.players.size() ? s.players[player].nation : 0;
    e.type = h.type, e.id = h.num, e.build_left = h.field_020;
    e.x = h.x, e.y = h.y;
    return e;
}

sim::SimEvent click_event() {
    sim::SimEvent e;
    e.sound = 1, e.variant = 0, e.priority = 5;
    return e;
}





std::vector<SoundCommand> fire_sounds(SoundBoard& b, const SoundData& d, const Listener& l, const sim::GameState& s) {
    std::vector<SoundCommand> out;
    const double cx = static_cast<double>(l.w / 2) + l.view_x, cy = static_cast<double>(l.h / 2) + l.view_y;
    for (std::size_t p = 0; p < s.players.size(); ++p) {
        const sim::Player& pl = s.players[p];
        if (pl.is_lost != 0) continue;
        for (const sim::House& h : pl.houses) {
            if (h.is_destroyed != 0 || h.field_020 != 0) continue;
            const std::vector<HouseGeometry>& hs = d.houses[sim::House_Nation(s, static_cast<std::uint8_t>(p), h) % 6];
            const std::int32_t half = h.max_hp >> 1;
            if (half - static_cast<std::int32_t>(h.hp) <= 0) continue;
            const HouseGeometry g = h.type < hs.size() ? hs[h.type] : HouseGeometry{};
            sim::SimEvent e;
            e.sound = 7, e.priority = 4, e.id = static_cast<std::uint16_t>(h.num + 1);
            e.x = static_cast<float>(h.x - cx);
            e.y = static_cast<float>(((h.y - g.oy) - cy) + g.fire_y0);
            if (auto c = play_sound(b, d, l, e)) out.push_back(std::move(*c));
        }
    }
    return out;
}

std::vector<int> sound_housekeeping(SoundBoard& b, const std::array<bool, kSoundSlots>& playing) {
    std::vector<int> stop;
    for (int i = 0; i < kSoundSlots; ++i) {
        SoundSlot& s = b.slots[static_cast<std::size_t>(i)];
        if (s.priority == 0) continue;
        if (!playing[static_cast<std::size_t>(i)]) {
            s = SoundSlot{};
            continue;
        }
        if (s.loop && ++s.keep_alive > 10) {
            s = SoundSlot{};
            stop.push_back(i);
        }
    }
    return stop;
}

bool ambience_due(SoundBoard& b, double now_s, bool game_running, bool music_on) {
    if (!game_running || music_on || !(now_s - b.ambience_last > 50.0)) return false;
    b.ambience_last = now_s;
    return true;
}

std::string music_file(std::uint8_t track) {
    if (track >= kMusicTracks) return {};
    return media(std::string("Music\\") + kMusic[track]);
}

std::uint8_t music_next(std::uint8_t track, bool has_game, std::uint8_t nation) {
    if (track >= 14) return 0;
    ++track;
    if (track < 14) return track;
    return has_game && nation < 5 ? static_cast<std::uint8_t>(14 + nation) : 0;
}

std::uint8_t campaign_track(int list_entry) {
    static constexpr std::uint8_t kTable[10] = {0, 23, 20, 21, 22, 19, 23, 20, 21, 22};
    return list_entry >= 0 && list_entry < 10 ? kTable[list_entry] : 0;
}

bool music_may_advance(double& last_advance, double now_s) {
    if (last_advance != 0.0 && now_s - last_advance < 3.0) return false;
    last_advance = now_s;
    return true;
}

}  // namespace antanta::ui
