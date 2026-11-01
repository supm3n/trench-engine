#include "ui/video_decoder.hpp"

#include <SDL3/SDL.h>

#include <utility>

#include "ui/plm.hpp"

namespace antanta::ui {

struct VideoDecoder::Impl {
    SDL_IOStream* io = nullptr;
    plm_t* plm = nullptr;
    int width = 0, height = 0, rate = 0;
    double fps = 0;
    std::vector<std::uint8_t> rgba;
    std::vector<float> audio;
    double audio_secs = 0;
    bool fresh = false;
    std::uint64_t frames = 0;

    ~Impl() {
        if (plm) plm_destroy(plm);  // frees its buffer
        if (io) SDL_CloseIO(io);
    }
};

namespace {

// pl_mpeg asks for more of the file whenever its buffer runs dry. Ring-buffer mode: write what was read, and mark the
// end when the file gives nothing more.
void load_more(plm_buffer_t* buffer, void* user) {
    auto* io = static_cast<SDL_IOStream*>(user);
    std::uint8_t chunk[64 * 1024];
    const std::size_t n = SDL_ReadIO(io, chunk, sizeof chunk);
    if (n > 0) {
        plm_buffer_write(buffer, chunk, n);
    } else {
        plm_buffer_signal_end(buffer);
    }
}

void on_picture(plm_t*, plm_frame_t* frame, void* user) {
    auto* d = static_cast<VideoDecoder::Impl*>(user);
    plm_frame_to_rgba(frame, d->rgba.data(), d->width * 4);
    d->fresh = true;
    ++d->frames;
}

void on_sound(plm_t*, plm_samples_t* samples, void* user) {
    auto* d = static_cast<VideoDecoder::Impl*>(user);
    d->audio.insert(d->audio.end(), samples->interleaved, samples->interleaved + samples->count * 2);
    d->audio_secs += static_cast<double>(samples->count) / plm_get_samplerate(d->plm);
}

}  // namespace

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() = default;

std::unique_ptr<VideoDecoder> VideoDecoder::open(const std::string& path, std::string* err) {
    auto fail = [&](const std::string& why) {
        if (err) *err = path + ": " + why;
        return std::unique_ptr<VideoDecoder>();
    };
    std::unique_ptr<VideoDecoder> d(new VideoDecoder());
    Impl& m = *d->impl_;
    m.io = SDL_IOFromFile(path.c_str(), "rb");
    if (!m.io) return fail(SDL_GetError());
    plm_buffer_t* buffer = plm_buffer_create_with_capacity(PLM_BUFFER_DEFAULT_SIZE);
    plm_buffer_set_load_callback(buffer, load_more, m.io);
    m.plm = plm_create_with_buffer(buffer, 1);
    if (!plm_has_headers(m.plm) || plm_get_num_video_streams(m.plm) < 1) {
        return fail("not an MPEG-1 video with a picture");
    }
    m.width = plm_get_width(m.plm), m.height = plm_get_height(m.plm);
    m.fps = plm_get_framerate(m.plm);
    if (m.width <= 0 || m.height <= 0 || m.width > 4096 || m.height > 4096 || !(m.fps > 0)) {
        return fail("unusable picture size or rate");
    }
    m.rate = plm_get_num_audio_streams(m.plm) > 0 ? plm_get_samplerate(m.plm) : 0;
    m.rgba.assign(static_cast<std::size_t>(m.width) * m.height * 4, 0);
    for (std::size_t i = 3; i < m.rgba.size(); i += 4) m.rgba[i] = 255;  // pl_mpeg writes red, green, blue only
    plm_set_video_decode_callback(m.plm, on_picture, &m);
    if (m.rate > 0) plm_set_audio_decode_callback(m.plm, on_sound, &m);
    plm_set_loop(m.plm, 0);
    return d;
}

int VideoDecoder::width() const { return impl_->width; }
int VideoDecoder::height() const { return impl_->height; }
double VideoDecoder::framerate() const { return impl_->fps; }
int VideoDecoder::samplerate() const { return impl_->rate; }
void VideoDecoder::decode(double seconds) { plm_decode(impl_->plm, seconds); }
double VideoDecoder::time() const { return plm_get_time(impl_->plm); }
bool VideoDecoder::ended() const { return plm_has_ended(impl_->plm) != 0; }
void VideoDecoder::set_audio_lead(double seconds) { plm_set_audio_lead_time(impl_->plm, seconds); }
const std::vector<std::uint8_t>& VideoDecoder::rgba() const { return impl_->rgba; }
bool VideoDecoder::new_frame() { return std::exchange(impl_->fresh, false); }
std::uint64_t VideoDecoder::frames() const { return impl_->frames; }
std::vector<float>& VideoDecoder::audio() { return impl_->audio; }
double VideoDecoder::audio_seconds() const { return impl_->audio_secs; }

}  // namespace antanta::ui
