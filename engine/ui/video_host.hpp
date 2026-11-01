// Plays the game's videos in the window (docs/sound.md section 5): the order and the skipping come from
// VideoSequence, the picture from VideoDecoder, drawn letterboxed into the whole window at any size, the sound pushed
// through an SDL3 audio stream of its own (not SDL3_mixer). Nothing here touches the simulation.
#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <memory>
#include <string>

#include "ui/video_sequence.hpp"

namespace antanta::ui {



struct VideoHooks {
    std::function<void()> pause_music;
    std::function<void()> resume_music;
};

// What skips the video playing: a click of any mouse button, Esc, Space, Enter or the keypad's Enter (ours; the
// original skips with Esc only). Not a key held down (a repeat), and not Alt+Enter, which switches full screen.
bool is_video_skip_event(const SDL_Event& e);

class VideoHost {
  public:
    VideoHost();
    ~VideoHost();
    VideoHost(const VideoHost&) = delete;
    VideoHost& operator=(const VideoHost&) = delete;

    // dir holds buka.mpg, intro-en.mpg and credits.mpg (tools/convert_videos.cmd). enabled is the `+video` switch.
    void configure(std::string dir, bool enabled, VideoHooks hooks);

    // Buka, then the intro. False when nothing plays (videos off, or no file could be opened): the menu is already
    // there. Never fails the app: a file that cannot be opened is logged and skipped.
    bool play_startup(SDL_Renderer* renderer);
    // The Medal button. Same rules.
    bool play_credits(SDL_Renderer* renderer);

    bool active() const { return seq_.active(); }
    // A skip event (is_video_skip_event): the video playing is over, the next one starts, or the menu is back.
    void skip(SDL_Renderer* renderer);
    // One frame while active(): decodes what is due, draws and presents it.
    void iterate(SDL_Renderer* renderer);

    // Frees the picture and the sound device; call before the renderer is destroyed.
    void shutdown() { close_current(); }

    // The level of the videos' sound, 0 (silent) to 1; the app passes the music setting (slice J).
    void set_volume(float gain);

    const VideoSequence& sequence() const { return seq_; }

  private:
    struct Player;
    bool start(bool ok, SDL_Renderer* renderer);
    void open_current(SDL_Renderer* renderer);
    void close_current();
    void end();

    VideoSequence seq_;
    std::string dir_;
    VideoHooks hooks_;
    float gain_ = 1.0f;
    std::unique_ptr<Player> player_;
};

}  // namespace antanta::ui
