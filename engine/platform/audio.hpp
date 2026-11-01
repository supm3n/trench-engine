// Sound effects and music on the audio device (milestone 4 slice J, docs/sound.md) with SDL3_mixer. The rules are in
// ui/sound.hpp; this file owns the device, the 16 effect tracks, the music track and the decoded sounds.
//
// Every function takes a nullable Audio*: null is silence, so batch runs, antanta-headless and tests (which never
// open a device) and a PC without a sound device run the same code paths without sound. Nothing here reads or writes
// the simulation beyond a const GameState&: sound never changes a trace (docs/sound.md 6).
//
// For the video player (docs/sound.md 5): audio_video_pause before a video starts, audio_video_resume after the last
// one ends or is skipped. The music keeps its place and goes on from there.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>

#include "formats/bytes.hpp"
#include "formats/params.hpp"
#include "sim/state.hpp"
#include "ui/settings.hpp"
#include "ui/sound.hpp"

namespace antanta::audio {

// Reads a file of the game folder by its path relative to it ("media/Clicks/Button.wav").
using ReadFile = std::function<std::optional<Bytes>(const std::string& path, std::string* error)>;

struct Audio;
struct AudioFree {
    void operator()(Audio* a) const;
};
using AudioPtr = std::unique_ptr<Audio, AudioFree>;

// Opens the default playback device. null (and the reason) when there is none: the game then runs silent. The music
// starts with track 0 (the original starts it after the intro videos: the video player pauses it first).
AudioPtr audio_open(ReadFile files, const ui::Settings& settings, std::string* error);

// The Options values: sound on / off and volume (applied to sounds that start from now on, as in the original),
// music on / off and volume (at once).
void audio_settings(Audio* a, const ui::Settings& settings);

void audio_focus(Audio* a, bool focused);
// The video player's pause and resume of the music.
void audio_video_pause(Audio* a);
void audio_video_resume(Audio* a);
// Music_Play: switches to a track at once (back to the main menu: 0; the campaign list: ui::campaign_track).
void audio_music_play(Audio* a, std::uint8_t track);
// The track playing now (tests and the log).
std::uint8_t audio_music_track(const Audio* a);
// Whether the file of the track that Music_Play ran for was opened (false: it counts as ended).
bool audio_music_loaded(const Audio* a);

// The unit and house sound data of a game (BasicParams0..5); a game's events need it.
void audio_game_data(Audio* a, std::span<const BasicParams> params);
// The sounds of the events a step raised (GameState::events), placed for the camera `l`.
void audio_events(Audio* a, std::span<const sim::SimEvent> events, const ui::Listener& l);
// One sound the view raises itself (ui::click_event, ui::unit_sound_event, ui::house_sound_event).
void audio_event(Audio* a, const sim::SimEvent& e, const ui::Listener& l);
void audio_click(Audio* a);



void audio_frame(Audio* a, const sim::GameState* game, const ui::Listener& l, double now_s);

}  // namespace antanta::audio
