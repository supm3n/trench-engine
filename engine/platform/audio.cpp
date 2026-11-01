#include "platform/audio.hpp"

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include <array>
#include <map>
#include <set>
#include <vector>

namespace antanta::audio {

struct Audio {
    ReadFile files;
    MIX_Mixer* mixer = nullptr;
    std::array<MIX_Track*, ui::kSoundSlots> tracks{};
    std::map<std::string, MIX_Audio*> sounds;  // decoded on first use; nullptr: the file did not load (logged once)
    ui::SoundBoard board;
    ui::SoundData data;
    bool music_on = true;

    MIX_Track* music = nullptr;
    MIX_Audio* music_audio = nullptr;
    std::uint8_t track = 0;
    bool stream = false;  // Music_Play ran for `track` (also when its file did not load: then it counts as ended)
    std::set<std::string> music_failed;  // files that did not load: logged once, not read and decoded again
    double last_advance = 0;
    float music_gain = 0.93f;
    ui::MusicPause pause;
    ~Audio() {
        if (mixer) MIX_DestroyMixer(mixer);  // also destroys the tracks
        for (auto& [name, s] : sounds) {
            if (s) MIX_DestroyAudio(s);
        }
        if (music_audio) MIX_DestroyAudio(music_audio);
        MIX_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};

void AudioFree::operator()(Audio* a) const { delete a; }

namespace {

MIX_Audio* load(Audio& a, const std::string& file, bool predecode) {
    std::string err;
    const std::optional<Bytes> bytes = a.files(file, &err);
    if (!bytes) {
        SDL_Log("sound: %s: %s", file.c_str(), err.c_str());
        return nullptr;
    }
    SDL_IOStream* io = SDL_IOFromConstMem(bytes->data(), bytes->size());
    MIX_Audio* s = io ? MIX_LoadAudio_IO(a.mixer, io, predecode, true) : nullptr;  // copies or decodes the bytes
    if (!s) SDL_Log("sound: %s: %s", file.c_str(), SDL_GetError());
    return s;
}

MIX_Audio* sound(Audio& a, const std::string& file) {
    const auto it = a.sounds.find(file);
    if (it != a.sounds.end()) return it->second;
    MIX_Audio* s = load(a, file, true);
    a.sounds.emplace(file, s);
    return s;
}

void run(Audio& a, const ui::SoundCommand& c) {
    MIX_Track* t = a.tracks[static_cast<std::size_t>(c.slot)];
    const MIX_StereoGains g{c.left, c.right};
    switch (c.op) {
        case ui::SoundCommand::Op::Refresh: MIX_SetTrackStereo(t, &g); return;
        case ui::SoundCommand::Op::Stop: MIX_StopTrack(t, 0); return;
        case ui::SoundCommand::Op::Start: break;
    }
    MIX_StopTrack(t, 0);  // a stolen slot is stopped first
    MIX_Audio* s = sound(a, c.file);
    if (!s || !MIX_SetTrackAudio(t, s) || !MIX_SetTrackStereo(t, &g)) {
        ui::sound_failed(a.board, c.slot);
        return;
    }
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetNumberProperty(props, MIX_PROP_PLAY_LOOPS_NUMBER, c.loop ? -1 : 0);
    if (!MIX_PlayTrack(t, props)) ui::sound_failed(a.board, c.slot);
    SDL_DestroyProperties(props);
}

void play(Audio& a, const sim::SimEvent& e, const ui::Listener& l) {
    if (auto c = ui::play_sound(a.board, a.data, l, e)) run(a, *c);
}




void music_play(Audio& a, std::uint8_t track) {
    a.track = track;
    a.stream = true;
    MIX_StopTrack(a.music, 0);
    if (a.music_audio) MIX_DestroyAudio(a.music_audio);
    a.music_audio = nullptr;
    const std::string file = ui::music_file(track);
    if (!a.music_failed.contains(file)) {
        a.music_audio = load(a, file, false);
        if (!a.music_audio) a.music_failed.insert(file);
    }
    if (!a.music_audio || !MIX_SetTrackAudio(a.music, a.music_audio)) return;
    MIX_SetTrackGain(a.music, a.music_gain);
    MIX_PlayTrack(a.music, 0);
    if (a.pause.any()) MIX_PauseTrack(a.music);
}

void music_pause_state(Audio& a) {
    if (a.pause.any()) {
        if (MIX_TrackPlaying(a.music)) MIX_PauseTrack(a.music);
    } else if (MIX_TrackPaused(a.music)) {
        MIX_ResumeTrack(a.music);
    }
}

}  // namespace

AudioPtr audio_open(ReadFile files, const ui::Settings& settings, std::string* error) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        *error = std::string("no audio: ") + SDL_GetError();
        return nullptr;
    }
    if (!MIX_Init()) {
        *error = std::string("SDL_mixer: ") + SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return nullptr;
    }
    AudioPtr a(new Audio);  // NOLINT: owned by the unique_ptr at once (Audio has a private-free destructor path)
    a->files = std::move(files);
    a->mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!a->mixer) {
        *error = std::string("no audio device: ") + SDL_GetError();
        return nullptr;
    }
    for (MIX_Track*& t : a->tracks) t = MIX_CreateTrack(a->mixer);
    a->music = MIX_CreateTrack(a->mixer);
    for (MIX_Track* t : a->tracks) {
        if (!t) {
            *error = std::string("SDL_mixer tracks: ") + SDL_GetError();
            return nullptr;
        }
    }
    if (!a->music) {
        *error = std::string("SDL_mixer music track: ") + SDL_GetError();
        return nullptr;
    }
    audio_settings(a.get(), settings);
    return a;
}

void audio_settings(Audio* a, const ui::Settings& settings) {
    if (!a) return;
    a->board.on = settings.sound_on;
    a->board.volume = settings.sound_volume > 100 ? 100 : settings.sound_volume;
    a->music_on = settings.music_on;
    a->pause.off = !settings.music_on;
    a->music_gain = static_cast<float>(settings.music_volume > 100 ? 100 : settings.music_volume) * 0.01f;
    MIX_SetTrackGain(a->music, a->music_gain);
    music_pause_state(*a);
}

void audio_focus(Audio* a, bool focused) {
    if (!a) return;
    a->pause.unfocused = !focused;
    music_pause_state(*a);
}

void audio_video_pause(Audio* a) {
    if (!a) return;
    a->pause.video = true;
    music_pause_state(*a);
}

void audio_video_resume(Audio* a) {
    if (!a) return;
    a->pause.video = false;
    music_pause_state(*a);
}

void audio_music_play(Audio* a, std::uint8_t track) {
    if (!a || track >= ui::kMusicTracks) return;
    music_play(*a, track);
}

std::uint8_t audio_music_track(const Audio* a) { return a ? a->track : 0; }

bool audio_music_loaded(const Audio* a) { return a && a->music_audio; }

void audio_game_data(Audio* a, std::span<const BasicParams> params) {
    if (a) a->data = ui::sound_data_from_params(params);
}

void audio_events(Audio* a, std::span<const sim::SimEvent> events, const ui::Listener& l) {
    if (!a) return;
    for (const sim::SimEvent& e : events) {
        if (e.kind == sim::SimEvent::Kind::Sound) play(*a, e, l);
    }
}

void audio_event(Audio* a, const sim::SimEvent& e, const ui::Listener& l) {
    if (a) play(*a, e, l);
}

void audio_click(Audio* a) {
    if (a) play(*a, ui::click_event(), ui::Listener{});
}

void audio_frame(Audio* a, const sim::GameState* game, const ui::Listener& l, double now_s) {
    if (!a) return;
    // Music_Update: a stream that ended moves the playlist on (not while paused: state 2 returns at once).
    if (!a->pause.any()) {
        if (!a->stream) {
            music_play(*a, a->track);
        } else if (!MIX_TrackPlaying(a->music) && ui::music_may_advance(a->last_advance, now_s)) {
            const bool has_game = game != nullptr && game->my_player < game->players.size();
            music_play(*a, ui::music_next(a->track, has_game, has_game ? game->players[game->my_player].nation : 0));
        }
    }
    std::array<bool, ui::kSoundSlots> playing{};
    for (std::size_t i = 0; i < playing.size(); ++i) playing[i] = MIX_TrackPlaying(a->tracks[i]);
    for (int i : ui::sound_housekeeping(a->board, playing)) MIX_StopTrack(a->tracks[static_cast<std::size_t>(i)], 0);
    if (!game) return;
    for (const ui::SoundCommand& c : ui::fire_sounds(a->board, a->data, l, *game)) run(*a, c);
    if (ui::ambience_due(a->board, now_s, true, a->music_on)) {
        sim::SimEvent e;
        e.sound = 8, e.variant = static_cast<std::uint8_t>(game->climate), e.priority = 0;
        play(*a, e, l);
    }
}

}  // namespace antanta::audio
