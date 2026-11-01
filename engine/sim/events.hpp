// Events the simulation raises for the view (milestone 4 slice J): sounds (docs/sound.md 3.7), event messages and


// GameState::events, a diagnostic field: step() empties it at its start, the app reads it after the step. The sim
// never reads it, it is not saved, not hashed and in no trace, and raising an event writes nothing else (no RNG, no
// CRT rand: the variant of a sound is picked by the view from its own generator), so the game runs the same with or
// without sound (docs/sound.md 6).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace antanta::sim {

struct GameState;
struct Unit;
struct House;
struct Shell;
struct Group;
struct UnitRef;

struct SimEvent {
    enum class Kind : std::uint8_t {
        Sound,
        Message,
        BattleMark,
        GroupAlert,
        Crater,
        Effect,
        Leaves,
        Dust,
        Flash,
        RifleFlash,
        ShipWake,
        ShipSink,
        // The mission event blocks (sim/mission_events.hpp, docs/missions.md 3.4 and 3.7):
        MissionMessage,
        MissionTimer,
        MissionCamera,
        // The Lua missions (script/lua_manager.hpp, docs/missions.md 5): MessageBox raises MissionMessage (`key` the
        // text table key, `pause`), SetCamera MissionCamera (`centred`), LuaManager_StopCurrentScript a horn (Sound).
        MissionLine,
    };
    // The mission events. MissionMessage: `player` the block's owner, `text` the block's text (UTF-8, the text
    // table's), `key` its text table key; `end`: closing the window opens the statistics (a win or lose block; the
    // sim has set paused, game_result, end_message 3 and end_text); `pause`: the window pauses the game until it is
    // closed (single player: always), which is the view's to do for a window without `end`. MissionTimer: the
    // countdown and the timer length in checks. MissionCamera: `centred` (action 8): (cell_x, cell_y) is the new
    // view centre, else (action 20) the new top-left cell of the view; both odd as the original makes them.
    // `log_only` (slice M6): the text goes to the F1 log without a window (a Lua mission's load gives back every
    // MessageBox text, script/lua_save.hpp ScriptVm_ReShow; the last one with a window).
    struct MissionArgs {
        std::uint8_t block = 0;  // the block's index in the owner's list
        std::uint8_t which = 0;  // 0 the "B" text (objective), 1 "C" (condition met), 2 "UC" (timer ran out)
        bool end = false, pause = false, centred = false, log_only = false;
        std::int32_t countdown = 0, timer = 0;
        std::string key;
    };
    MissionArgs mission;


    struct EffectArgs {
        std::uint8_t kind = 3;
        std::uint8_t type = 0;
        std::int32_t x = 0, y = 0, z = 0;
        std::uint16_t delay = 0;
        std::int32_t rise = 0;
        std::int32_t rise_step = 1;
        float frame = 0;
        float speed = 1;
        std::uint8_t ground = 0;


        bool on_screen = false;
        float view_x = 0, view_y = 0;



        std::uint8_t view_rand = 0;
    };
    EffectArgs effect;
    // Dust: what the view needs to make the puffs with CRT rand() (render/effects_emit), `player`, `nation` and `type`
    // the unit's.
    struct DustArgs {
        std::uint8_t what = 0;     // 0 a vehicle's exhaust, 1 a vehicle's turn ending, 2 cavalry riding
        std::uint8_t orient = 0;
        std::int16_t size = 0;
        std::uint32_t serial = 0;
        float z = 0;
    };
    DustArgs dust;



    struct FlashArgs {
        std::uint8_t weapon = 0;
        std::uint8_t barrel = 0;
        std::uint8_t ship = 0;     // Basic+4
        std::uint32_t serial = 0;
        float z = 0, alt = 0;
        float angle = 0;
        float gun = 0;
    };
    FlashArgs flash;




    struct SinkArgs {
        std::uint8_t orient = 0;
        struct Bubble {
            std::int16_t delay = 0, a = 0, b = 0;
        };
        std::vector<Bubble> bubbles;
    };
    SinkArgs sink;
    std::uint32_t serial = 0;
    Kind kind = Kind::Sound;
    // Sound: the arguments as the original passes them, and the fields of the object the call reads.
    std::uint8_t sound = 0, variant = 0, priority = 0;
    std::uint8_t player = 0xff;    // the unit's or house's owner; GroupAlert: the group's player
    std::uint8_t nation = 0;       // a unit's record's nation (Unit_Nation; Dust and houses: the owner's Nation)
    std::uint16_t type = 0;
    std::uint16_t id = 0;
    std::uint16_t build_left = 0;
    // The world point: kind 3 (X, Y - f24 - f28) of the unit; kind 2 the house's (X, Y); kinds 4 and 6 whole pixels;
    // Crater: the screen point (x, y - z) in whole pixels.
    float x = 0, y = 0;
    bool flag = false;  // GroupAlert: a member answered (bVar4); Sound kind 3 variant 5: the aircraft engine loop
    std::string text;
    std::uint32_t colour = 0xffffff;
    std::int32_t cell_x = 0, cell_y = 0;
};



void Event_UnitSound(GameState& s, std::uint8_t player, const Unit& u, std::uint8_t variant, std::uint8_t priority);

void Event_HouseSound(GameState& s, std::uint8_t player, const House& h, std::uint8_t variant, std::uint8_t priority);
// A sound with no object (kind 1, the click, and kind 9, the horns).
void Event_Sound(GameState& s, std::uint8_t sound, std::uint8_t variant, std::uint8_t priority);


void Event_WeaponSound(GameState& s, std::uint8_t player, const Unit& u, std::uint8_t weapon_48, std::uint8_t kind);

void Event_CommandVoice(GameState& s, std::uint8_t player, const UnitRef& head);

void Event_Horn(GameState& s, std::uint8_t player, const Unit& u);


void Event_ShellLeadIn(GameState& s, const Shell& v, double x, double y);


void Event_ShellExplosion(GameState& s, const Shell& v);




SimEvent::EffectArgs* Event_Effect(GameState& s, std::uint8_t kind, bool unit, std::int32_t x, std::int32_t y,
                                   std::int32_t z, std::uint8_t type);



void Event_TreeLeaves(GameState& s, std::int32_t x, std::int32_t y, std::uint16_t record);



void Event_UnitDust(GameState& s, std::uint8_t player, const Unit& u, std::uint8_t what);


void Event_ShipWake(GameState& s, std::uint8_t player, const Unit& u, bool moving);

void Event_ShipSink(GameState& s, std::uint8_t player, const Unit& u, std::vector<SimEvent::SinkArgs::Bubble> bubbles);


void Event_WeaponFlash(GameState& s, std::uint8_t player, const Unit& u, std::uint8_t weapon, std::uint8_t barrel,
                       float gun);

void Event_RifleFlash(GameState& s, std::uint8_t player, const Unit& u);


void Event_HouseAttacked(GameState& s, std::uint8_t player, const House& h);

void Event_GroupAlert(GameState& s, const Group& g, bool any);

void Event_TrainRefused(GameState& s, std::uint8_t player);


void Event_TransportRange(GameState& s, std::uint8_t player);

}  // namespace antanta::sim
