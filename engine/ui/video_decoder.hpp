// One MPEG-1 video with MP2 sound (the files tools/convert_videos.cmd makes), decoded with pl_mpeg. No drawing and no
// audio device here: the picture comes out as RGBA, the sound as interleaved stereo floats. The file is read in chunks
// through SDL_IOStream, so a path with any characters works and the 80 MB credits file is never held whole.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace antanta::ui {

class VideoDecoder {
  public:
    ~VideoDecoder();
    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    // Null, with the reason in *err, if the file cannot be opened or is not an MPEG program stream with a picture.
    static std::unique_ptr<VideoDecoder> open(const std::string& path, std::string* err);

    int width() const;
    int height() const;
    double framerate() const;
    int samplerate() const;  // 0 without sound

    // Plays `seconds` further: decodes the pictures that are due and the sound up to a little ahead of them.
    void decode(double seconds);
    double time() const;  // seconds played so far
    bool ended() const;   // everything decoded
    // How far ahead of the clock the sound is decoded.
    void set_audio_lead(double seconds);

    // The newest picture, width * height * 4 bytes. new_frame() is true once per picture decoded since its last call.
    const std::vector<std::uint8_t>& rgba() const;
    bool new_frame();
    std::uint64_t frames() const;
    // Sound decoded and not taken yet. The caller empties it.
    std::vector<float>& audio();
    double audio_seconds() const;  // all the sound decoded so far

    struct Impl;  // public only so the decoder's callbacks can name it

  private:
    VideoDecoder();
    std::unique_ptr<Impl> impl_;
};

}  // namespace antanta::ui
