#include "ui/video_decoder.hpp"

#include <SDL3/SDL.h>

#include <cctype>

#include "ui/plm.hpp"

namespace antanta::ui {

namespace {

// MPEG-1 through pl_mpeg.
class MpegDecoder final : public VideoDecoder {
  public:
    ~MpegDecoder() override {
        if (plm_) plm_destroy(plm_);  // frees its buffer
        if (io_) SDL_CloseIO(io_);
    }

    static std::unique_ptr<VideoDecoder> open(const std::string& path, std::string* err) {
        auto fail = [&](const std::string& why) {
            if (err) *err = path + ": " + why;
            return std::unique_ptr<VideoDecoder>();
        };
        std::unique_ptr<MpegDecoder> d(new MpegDecoder());
        d->io_ = SDL_IOFromFile(path.c_str(), "rb");
        if (!d->io_) return fail(SDL_GetError());
        plm_buffer_t* buffer = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
        plm_buffer_set_load_callback(buffer, load_more, d->io_);
        d->plm_ = plm_create_with_buffer(buffer, 1);
        if (!plm_has_headers(d->plm_) || plm_get_num_video_streams(d->plm_) < 1) {
            return fail("not an MPEG-1 video with a picture");
        }
        d->width_ = plm_get_width(d->plm_), d->height_ = plm_get_height(d->plm_);
        d->fps_ = plm_get_framerate(d->plm_);
        if (d->width_ <= 0 || d->height_ <= 0 || d->width_ > 4096 || d->height_ > 4096 || !(d->fps_ > 0)) {
            return fail("unusable picture size or rate");
        }
        d->rate_ = plm_get_num_audio_streams(d->plm_) > 0 ? plm_get_samplerate(d->plm_) : 0;
        d->rgba_.assign(static_cast<std::size_t>(d->width_) * d->height_ * 4, 0);
        for (std::size_t i = 3; i < d->rgba_.size(); i += 4) d->rgba_[i] = 255;  // pl_mpeg writes red, green, blue
        plm_set_video_decode_callback(d->plm_, on_picture, d.get());
        if (d->rate_ > 0) plm_set_audio_decode_callback(d->plm_, on_sound, d.get());
        plm_set_loop(d->plm_, 0);
        return d;
    }

    int width() const override { return width_; }
    int height() const override { return height_; }
    double framerate() const override { return fps_; }
    int samplerate() const override { return rate_; }
    void decode(double seconds) override { plm_decode(plm_, seconds); }
    double time() const override { return plm_get_time(plm_); }
    bool ended() const override { return plm_has_ended(plm_) != 0; }
    void set_audio_lead(double seconds) override { plm_set_audio_lead_time(plm_, seconds); }

  private:
    MpegDecoder() = default;

    // pl_mpeg asks for more of the file whenever its buffer runs dry. Ring-buffer mode: write what was read, and mark
    // the end when the file gives nothing more.
    static void load_more(plm_buffer_t* buffer, void* user) {
        auto* io = static_cast<SDL_IOStream*>(user);
        std::uint8_t chunk[64 * 1024];
        const std::size_t n = SDL_ReadIO(io, chunk, sizeof chunk);
        if (n > 0) {
            plm_buffer_write(buffer, chunk, n);
        } else {
            plm_buffer_signal_end(buffer);
        }
    }

    static void on_picture(plm_t*, plm_frame_t* frame, void* user) {
        auto* d = static_cast<MpegDecoder*>(user);
        plm_frame_to_rgba(frame, d->rgba_.data(), d->width_ * 4);
        d->fresh_ = true;
        ++d->frames_;
    }

    static void on_sound(plm_t*, plm_samples_t* samples, void* user) {
        auto* d = static_cast<MpegDecoder*>(user);
        d->audio_.insert(d->audio_.end(), samples->interleaved, samples->interleaved + samples->count * 2);
        d->audio_secs_ += static_cast<double>(samples->count) / d->rate_;
    }

    SDL_IOStream* io_ = nullptr;
    plm_t* plm_ = nullptr;
    int width_ = 0, height_ = 0, rate_ = 0;
    double fps_ = 0;
};

bool ends_with_wmv(const std::string& path) {
    if (path.size() < 4) return false;
    std::string ext = path.substr(path.size() - 4);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".wmv";
}

}  // namespace

std::unique_ptr<VideoDecoder> VideoDecoder::open(const std::string& path, std::string* err) {
    return ends_with_wmv(path) ? open_windows_media(path, err) : MpegDecoder::open(path, err);
}

#if !defined(_WIN32) || defined(__EMSCRIPTEN__)
std::unique_ptr<VideoDecoder> open_windows_media(const std::string& path, std::string* err) {
    if (err) *err = path + ": Windows Media files play only in the Windows version";
    return nullptr;
}
#endif

}  // namespace antanta::ui
