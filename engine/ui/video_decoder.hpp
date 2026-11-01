// One video file decoded for VideoHost. No drawing and no audio device here: the picture comes out as RGBA, the sound
// as interleaved stereo floats. Two kinds of file:
// - MPEG-1 with MP2 sound (the files tools/convert_videos.cmd makes), decoded with pl_mpeg on every platform. The file
//   is read in chunks through SDL_IOStream, so a path with any characters works and the 80 MB credits file is never
//   held whole.
// - The game's own Windows Media files (Media/*.wmv), decoded with Windows Media Foundation (video_decoder_mf.cpp),
//   desktop Windows only. Elsewhere, or on a Windows without Media Foundation (N editions without the Media Feature
//   Pack), opening one fails with the reason and the host tries the converted file.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace antanta::ui {

class VideoDecoder {
  public:
    virtual ~VideoDecoder() = default;
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Null, with the reason in *err, if the file cannot be opened or holds no usable picture. A name ending in .wmv
    // goes to Media Foundation, any other to pl_mpeg.
    static std::unique_ptr<VideoDecoder> open(const std::string& path, std::string* err);

    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual double framerate() const = 0;
    virtual int samplerate() const = 0;            // 0 without sound
    virtual double duration() const { return 0; }  // seconds, 0 when the decoder does not know it beforehand

    // Plays `seconds` further: decodes the pictures that are due and the sound up to a little ahead of them.
    virtual void decode(double seconds) = 0;
    virtual double time() const = 0;  // seconds played so far
    virtual bool ended() const = 0;   // everything decoded
    // How far ahead of the clock the sound is decoded.
    virtual void set_audio_lead(double seconds) = 0;

    // The newest picture, width * height * 4 bytes. new_frame() is true once per picture decoded since its last call.
    const std::vector<std::uint8_t>& rgba() const { return rgba_; }
    bool new_frame() {
        const bool f = fresh_;
        fresh_ = false;
        return f;
    }
    std::uint64_t frames() const { return frames_; }
    // Sound decoded and not taken yet. The caller empties it.
    std::vector<float>& audio() { return audio_; }
    double audio_seconds() const { return audio_secs_; }  // all the sound decoded so far

  protected:
    VideoDecoder() = default;
    std::vector<std::uint8_t> rgba_;
    std::vector<float> audio_;
    double audio_secs_ = 0;
    bool fresh_ = false;
    std::uint64_t frames_ = 0;
};

// The Media Foundation decoder (video_decoder_mf.cpp); on other platforms it only fails with the reason.
std::unique_ptr<VideoDecoder> open_windows_media(const std::string& path, std::string* err);

}  // namespace antanta::ui
