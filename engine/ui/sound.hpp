// The view side of the original's sound effects and music (docs/sound.md, milestone 4 slice J). No SDL here: the



//
// Fixes of the original's sound bugs (owner rule 2026-10-15, docs/bugs.md B12):
//   1. a destroyed building plays Dead_Building (the original's table lookup finds Sci_Acad first);
//   2. the Russian artillery's command voices 4 to 6 play (the files are named Atril_Att, the data says Artil_Att);
//   3. the British anthem plays (the track table names National\Englich.ogg, the file is England.ogg);
//   4. a sound is never cut because a menu window is open (the original frees every slot then, so a menu click was
//      cut at the next frame);
//   5. the music never stays paused because a video did not play (the original pauses on the Medal button with
//      videos off and never resumes): pause is a state of its reasons (music off, focus, video), see MusicPause.
// Variants are drawn from the board's own generator (the CRT formula on its own state), never the simulation's.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "formats/params.hpp"
#include "sim/state.hpp"

namespace antanta::ui {

constexpr int kSoundSlots = 16;


int sound_volume_mb(std::uint8_t volume);
// A level in hundredths of a decibel as a linear gain (10^(mb / 2000)).
double gain_of_mb(double mb);


struct Listener {
    double view_x = 0, view_y = 0;
    int w = 1024, h = 768;
};

// 3.3: a sound that starts at (x, y) pixels from the screen centre (right, down). DirectSound's model with the
// original's settings: no attenuation up to min = 0.4 H, then -6.02 dB * rolloff 5 * log2(d / min); beyond max =
// 1.2 H the sound does not start. Pan by x / d (left for negative x), centre gives both channels 1. The pan law (each
// ear keeps full level until the sound moves to the other side, then cos falls to 0 at the far side) is ours; the
// original's is DirectSound's and is not in the exe (to be tuned by ear).
struct Placement {
    bool audible = true;
    double distance = 0;        // sqrt(x^2 + y^2)
    double attenuation_db = 0;  // 0 or negative
    float left = 1, right = 1;  // pan gains, before the attenuation
};
Placement place_sound(double x, double y, int screen_h);
Placement place_centred();

// The data a sound needs from BasicParams0..5 (indexed by nation): the unit sound slots and the house sprite sizes.
struct HouseGeometry {
    std::uint16_t w = 0, h = 0;
    std::int16_t ox = 0, oy = 0;
    std::uint16_t fire_y0 = 0;
};
struct SoundData {
    std::array<std::vector<std::array<UnitSound, 10>>, 6> units;
    std::array<std::vector<HouseGeometry>, 6> houses;
};
SoundData sound_data_from_params(std::span<const BasicParams> params);



struct SoundSlot {
    std::uint8_t priority = 0;  // 0: free
    std::uint8_t kind = 0, variant = 0;
    std::uint16_t id = 0;
    std::int32_t distance = 0;
    bool loop = false;
    std::uint8_t keep_alive = 0;
};

struct SoundBoard {
    std::array<SoundSlot, kSoundSlots> slots{};
    bool on = true;
    std::uint8_t volume = 96;
    std::uint32_t holdrand = 1;    // the variant generator (the CRT's formula, its own state)
    double ambience_last = -30.0;
};

// The CRT rand() formula on the board's own state.
int sound_rand(SoundBoard& b);

// 3.4 steps 1 to 3: where a new sound goes. slot -1: dropped. refresh: a looping sound of the same source is there
// (slot names it), its position is rewritten and its keep alive counter set to 1, nothing new starts.
struct SlotChoice {
    int slot = -1;
    bool refresh = false;
};
SlotChoice choose_slot(const SoundBoard& b, std::uint8_t kind, std::uint8_t variant, std::uint16_t id,
                       std::int32_t distance, std::uint8_t priority, bool loop);



std::string house_sound_file(SoundBoard& b, std::uint8_t nation, std::uint16_t type, std::uint8_t variant);
std::string unit_sound_file(SoundBoard& b, const UnitSound& entry);
std::string sound_file(SoundBoard& b, std::uint8_t kind, std::uint8_t variant);  // kinds 1, 4, 6, 7, 8, 9

// What the device does with a slot.
struct SoundCommand {
    enum class Op : std::uint8_t { Start, Refresh, Stop };
    Op op = Op::Start;
    int slot = 0;
    std::string file;
    float left = 1, right = 1;  // the final gains: volume, attenuation and pan
    bool loop = false;
};



std::optional<SoundCommand> play_sound(SoundBoard& b, const SoundData& d, const Listener& l, const sim::SimEvent& e);
void sound_failed(SoundBoard& b, int slot);



sim::SimEvent unit_sound_event(const sim::GameState& s, std::uint8_t player, const sim::Unit& u, std::uint8_t variant,
                               std::uint8_t priority);
sim::SimEvent house_sound_event(const sim::GameState& s, std::uint8_t player, const sim::House& h, std::uint8_t variant,
                                std::uint8_t priority);
sim::SimEvent click_event();



std::vector<SoundCommand> fire_sounds(SoundBoard& b, const SoundData& d, const Listener& l, const sim::GameState& s);




std::vector<int> sound_housekeeping(SoundBoard& b, const std::array<bool, kSoundSlots>& playing);



bool ambience_due(SoundBoard& b, double now_s, bool game_running, bool music_on);



constexpr std::uint8_t kMusicTracks = 24;
// The file of a track, relative to the game folder ("media/Music/Theme1.ogg"); track 15 is National/England.ogg (fix
// 3).
std::string music_file(std::uint8_t track);
// Music_Update steps 2 and 3: the track after `track` ended. 0..12 go on, 13 goes to the anthem of the human player's
// nation (14 + nation) when a game exists, else to 0; 14 and above go back to 0.
std::uint8_t music_next(std::uint8_t track, bool has_game, std::uint8_t nation);

std::uint8_t campaign_track(int list_entry);


struct MusicPause {
    bool off = false, unfocused = false, video = false;
    bool any() const { return off || unfocused || video; }
};



bool music_may_advance(double& last_advance, double now_s);

}  // namespace antanta::ui
