#include "ui/video_host.hpp"

#include <algorithm>
#include <utility>
#include <vector>

#include "ui/video_decoder.hpp"

namespace antanta::ui {

struct VideoHost::Player {
    std::unique_ptr<VideoDecoder> decoder;
    SDL_Texture* texture = nullptr;
    SDL_AudioStream* audio = nullptr;
    bool audio_started = false;
    bool flushed = false;
    double fed = 0;       // seconds of sound given to the audio stream
    Uint64 start_ns = 0;  // wall clock at the start, the clock until sound is playing

    ~Player() {
        if (audio) SDL_DestroyAudioStream(audio);
        if (texture) SDL_DestroyTexture(texture);
    }

    double queued_seconds() const {
        const int bytes = audio ? SDL_GetAudioStreamQueued(audio) : 0;
        return static_cast<double>(bytes) / (sizeof(float) * 2 * std::max(1, decoder->samplerate()));
    }

    // Moves the picture on to what is being heard (or the wall clock without sound) and hands new sound over.
    void advance() {
        VideoDecoder& d = *decoder;
        double target;
        if (audio && audio_started) {
            target = fed - queued_seconds();  // what has left the stream so far
        } else {
            target = static_cast<double>(SDL_GetTicksNS() - start_ns) / 1e9;
            if (audio) target = std::min(target, d.time() + 1.0 / d.framerate());  // wait for the first sound
        }
        const double dt = std::clamp(target - d.time(), 0.0, 0.5);
        d.decode(dt > 0 ? dt : (d.frames() == 0 ? 1.0 / d.framerate() : 0.0));
        std::vector<float>& s = d.audio();
        if (audio && !s.empty()) {
            SDL_PutAudioStreamData(audio, s.data(), static_cast<int>(s.size() * sizeof(float)));
            fed = d.audio_seconds();
            if (!audio_started) audio_started = SDL_ResumeAudioStreamDevice(audio);
        }
        s.clear();
        if (d.ended() && audio && !flushed) {
            SDL_FlushAudioStream(audio);  // the last few samples the converter holds
            flushed = true;
        }
    }

    // Done when everything is decoded and the sound has played out.
    bool finished() const { return decoder->ended() && (!audio || !audio_started || queued_seconds() <= 0.0); }
};

bool is_video_skip_event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) return true;
    if (e.type != SDL_EVENT_KEY_DOWN || e.key.repeat) return false;
    switch (e.key.key) {
        case SDLK_ESCAPE:
        case SDLK_SPACE: return true;
        case SDLK_RETURN:
        case SDLK_KP_ENTER: return (e.key.mod & SDL_KMOD_ALT) == 0;
        default: return false;
    }
}

VideoHost::VideoHost() = default;
VideoHost::~VideoHost() { close_current(); }

void VideoHost::configure(std::string dir, bool enabled, VideoHooks hooks) {
    dir_ = std::move(dir);
    if (!dir_.empty() && dir_.back() != '/' && dir_.back() != '\\') dir_ += '/';
    seq_.set_enabled(enabled);
    hooks_ = std::move(hooks);
}

bool VideoHost::play_startup(SDL_Renderer* renderer) {
    return start(seq_.start_startup({dir_ + "buka.mpg", dir_ + "intro-en.mpg"}), renderer);
}

bool VideoHost::play_credits(SDL_Renderer* renderer) {
    return start(seq_.start_credits(dir_ + "credits.mpg"), renderer);
}

bool VideoHost::start(bool ok, SDL_Renderer* renderer) {
    if (!ok) return false;
    if (hooks_.pause_music) hooks_.pause_music();
    SDL_HideCursor();
    open_current(renderer);
    return active();
}

void VideoHost::set_volume(float gain) {
    gain_ = gain;
    if (player_ && player_->audio) SDL_SetAudioStreamGain(player_->audio, gain_);
}

// Opens the file the sequence is at; one that cannot be opened is logged and skipped like a skipped video.
void VideoHost::open_current(SDL_Renderer* renderer) {
    close_current();
    while (seq_.active()) {
        std::string err;
        auto decoder = VideoDecoder::open(seq_.current(), &err);
        if (decoder) {
            auto p = std::make_unique<Player>();
            p->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING,
                                           decoder->width(), decoder->height());
            if (p->texture) {
                SDL_SetTextureBlendMode(p->texture, SDL_BLENDMODE_NONE);
                SDL_SetTextureScaleMode(p->texture, SDL_SCALEMODE_LINEAR);  // filtered when scaled to the window
                if (decoder->samplerate() > 0 && SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, decoder->samplerate()};
                    p->audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
                    if (!p->audio) SDL_Log("video: no sound for %s: %s", seq_.current().c_str(), SDL_GetError());
                }
                if (p->audio) SDL_SetAudioStreamGain(p->audio, gain_);
                decoder->set_audio_lead(0.2);
                p->decoder = std::move(decoder);
                p->start_ns = SDL_GetTicksNS();
                SDL_Log("video: playing %s (%d x %d%s)", seq_.current().c_str(), p->decoder->width(),
                        p->decoder->height(), p->audio ? ", with sound" : ", no sound");
                player_ = std::move(p);
                return;
            }
            err = std::string(seq_.current()) + ": " + SDL_GetError();
        }
        SDL_Log("video: skipped, %s", err.c_str());
        seq_.finish_current();
    }
    end();
}

void VideoHost::close_current() { player_.reset(); }

void VideoHost::end() {
    close_current();
    SDL_ShowCursor();
    if (hooks_.resume_music) hooks_.resume_music();
}

void VideoHost::skip(SDL_Renderer* renderer) {
    if (!active()) return;
    if (player_) {
        SDL_Log("video: over after %.1f s (%s)", static_cast<double>(SDL_GetTicksNS() - player_->start_ns) / 1e9,
                player_->finished() ? "played to the end" : "skipped");
    }
    if (seq_.finish_current()) {
        open_current(renderer);
    } else {
        end();
    }
}

void VideoHost::iterate(SDL_Renderer* renderer) {
    if (!active() || !player_) return;
    Player& p = *player_;
    p.advance();
    if (p.decoder->new_frame()) {
        SDL_UpdateTexture(p.texture, nullptr, p.decoder->rgba().data(), p.decoder->width() * 4);
    }
    // The bars black, the picture as large as the window allows at its own proportions, any window size.
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderLogicalPresentation(renderer, p.decoder->width(), p.decoder->height(),
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_RenderTexture(renderer, p.texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    if (p.finished()) skip(renderer);
}

}  // namespace antanta::ui
