// The game's own Windows Media videos (Media/*.wmv: WMV video, WMA sound) through Windows Media Foundation's Source
// Reader, desktop Windows only (ui/video_decoder.hpp). The reader converts the picture to RGB32 (its video processing
// switched on) and the sound to float; both are read on the calling thread, no thread of our own. Pictures are shown
// by their time stamps, so the sources' fixed rates and frozen stretches (no new picture for a while) keep the
// original's timing.
#include "ui/video_decoder.hpp"

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <windows.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <utility>

namespace antanta::ui {

namespace {

constexpr DWORD kVideo = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD kAudio = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
constexpr double kTick = 1e-7;  // Media Foundation counts time in 100 ns

// Owns one COM reference.
template <class T>
struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T** out() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
};

std::string hr_text(HRESULT hr) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "error 0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

// MFStartup once per run, on the first Windows Media video. COM is set up on this thread first (the reader needs it;
// if the thread already has COM in another mode that is kept). Media Foundation stays started until the process ends.
HRESULT media_foundation() {
    static const HRESULT hr = [] {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        (void)co;  // S_OK, S_FALSE (already) or RPC_E_CHANGED_MODE (already, the other mode): all usable
        return MFStartup(MF_VERSION, MFSTARTUP_LITE);
    }();
    return hr;
}

std::wstring widen(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n : 1, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    w.resize(w.size() - 1);  // the terminator
    return w;
}

class MfDecoder final : public VideoDecoder {
  public:
    static std::unique_ptr<VideoDecoder> open(const std::string& path, std::string* err) {
        auto fail = [&](const std::string& why) {
            if (err) *err = path + ": " + why;
            return std::unique_ptr<VideoDecoder>();
        };
        SDL_PathInfo info;
        if (!SDL_GetPathInfo(path.c_str(), &info) || info.type != SDL_PATHTYPE_FILE) return fail("file not found");
        if (const HRESULT hr = media_foundation(); FAILED(hr)) {
            return fail("Windows Media Foundation is not available (" + hr_text(hr) + ")");
        }
        std::unique_ptr<MfDecoder> d(new MfDecoder());
        Com<IMFAttributes> attrs;
        HRESULT hr = MFCreateAttributes(attrs.out(), 1);
        if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = MFCreateSourceReaderFromURL(widen(path).c_str(), attrs.p, d->reader_.out());
        if (FAILED(hr)) return fail("Media Foundation cannot open it (" + hr_text(hr) + ")");
        IMFSourceReader* r = d->reader_.p;
        r->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        if (FAILED(hr = r->SetStreamSelection(kVideo, TRUE))) return fail("no picture (" + hr_text(hr) + ")");

        // The picture: its own size and rate, delivered as RGB32.
        UINT32 w = 0, h = 0, num = 0, den = 0;
        {
            Com<IMFMediaType> native;
            if (FAILED(hr = r->GetNativeMediaType(kVideo, 0, native.out())))
                return fail("no picture (" + hr_text(hr) + ")");
            MFGetAttributeSize(native.p, MF_MT_FRAME_SIZE, &w, &h);
            MFGetAttributeRatio(native.p, MF_MT_FRAME_RATE, &num, &den);
        }
        Com<IMFMediaType> want;
        hr = MFCreateMediaType(want.out());
        if (SUCCEEDED(hr)) hr = want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = want->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = r->SetCurrentMediaType(kVideo, nullptr, want.p);
        if (FAILED(hr)) return fail("no decoder for its picture (" + hr_text(hr) + ")");
        d->read_picture_layout();
        d->width_ = static_cast<int>(w), d->height_ = static_cast<int>(h);
        d->fps_ = den > 0 ? static_cast<double>(num) / den : 0;
        if (d->width_ <= 0 || d->height_ <= 0 || d->width_ > 4096 || d->height_ > 4096) {
            return fail("unusable picture size");
        }
        d->rgba_.assign(static_cast<std::size_t>(d->width_) * d->height_ * 4, 0);

        // The sound, if any, as float; without it the video plays silent.
        if (SUCCEEDED(r->SetStreamSelection(kAudio, TRUE))) {
            Com<IMFMediaType> a;
            hr = MFCreateMediaType(a.out());
            if (SUCCEEDED(hr)) hr = a->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            if (SUCCEEDED(hr)) hr = a->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
            if (SUCCEEDED(hr)) hr = r->SetCurrentMediaType(kAudio, nullptr, a.p);
            Com<IMFMediaType> cur;
            if (SUCCEEDED(hr)) hr = r->GetCurrentMediaType(kAudio, cur.out());
            if (SUCCEEDED(hr)) {
                d->channels_ = static_cast<int>(MFGetAttributeUINT32(cur.p, MF_MT_AUDIO_NUM_CHANNELS, 0));
                d->rate_ = static_cast<int>(MFGetAttributeUINT32(cur.p, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0));
            }
            if (FAILED(hr) || d->channels_ < 1 || d->rate_ <= 0) {
                SDL_Log("video: no sound for %s (%s)", path.c_str(), hr_text(hr).c_str());
                d->rate_ = 0;
                r->SetStreamSelection(kAudio, FALSE);
            }
        }

        PROPVARIANT pv;
        PropVariantInit(&pv);
        if (SUCCEEDED(
                r->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &pv)) &&
            pv.vt == VT_UI8) {
            d->duration_ = static_cast<double>(pv.uhVal.QuadPart) * kTick;
        }
        PropVariantClear(&pv);

        // The rate from the first pictures' time stamps: the files' headers do not always say it right (the logo's
        // claims 25, its pictures come every 1/30 s, a few left out). The shortest step, a whole rate if within 2 %
        // (the stamps are rounded to milliseconds).
        for (int i = 0; i < 8 && d->read_picture(); ++i) {
        }
        double step = 0;
        for (std::size_t i = 1; i < d->queue_.size(); ++i) {
            const double s = d->queue_[i].first - d->queue_[i - 1].first;
            if (s > 1e-4 && (step == 0 || s < step)) step = s;
        }
        if (step > 0) {
            d->fps_ = 1.0 / step;
            const double whole = std::round(d->fps_);
            if (std::abs(d->fps_ - whole) <= whole * 0.02) d->fps_ = whole;
        }
        if (d->queue_.empty() || !(d->fps_ > 0) || d->fps_ > 1000) return fail("no usable pictures");
        return d;
    }

    ~MfDecoder() override {
        for (auto& q : queue_) q.second->Release();
    }

    int width() const override { return width_; }
    int height() const override { return height_; }
    double framerate() const override { return fps_; }
    int samplerate() const override { return rate_; }
    double duration() const override { return duration_; }
    double time() const override { return clock_; }
    void set_audio_lead(double seconds) override { lead_ = seconds; }

    // Without sound the last picture stays for its own length; with sound the end is where the sound ends (the clock
    // is the sound, so it cannot run past it).
    bool ended() const override {
        const bool pictures_done = video_end_ && queue_.empty();
        if (rate_ > 0) return audio_end_ && (pictures_done || clock_ >= audio_secs_);
        return pictures_done && clock_ >= video_stop_;
    }

    void decode(double seconds) override {
        clock_ += seconds;
        while ((!queue_.empty() || read_picture()) && queue_.front().first <= clock_ + 1e-6) {
            show(queue_.front().second);
            queue_.front().second->Release();
            queue_.pop_front();
        }
        while (rate_ > 0 && !audio_end_ && audio_secs_ < clock_ + lead_) read_sound();
    }

  private:
    MfDecoder() = default;

    // The size of the RGB32 rows the reader delivers (the picture may arrive in a padded buffer).
    void read_picture_layout() {
        Com<IMFMediaType> cur;
        if (FAILED(reader_->GetCurrentMediaType(kVideo, cur.out()))) return;
        UINT32 w = 0, h = 0;
        MFGetAttributeSize(cur.p, MF_MT_FRAME_SIZE, &w, &h);
        out_w_ = static_cast<int>(w), out_h_ = static_cast<int>(h);
        stride_ = static_cast<INT32>(MFGetAttributeUINT32(cur.p, MF_MT_DEFAULT_STRIDE, w * 4));
    }

    // Reads the next picture onto the queue. False at the end of the picture stream.
    bool read_picture() {
        while (!video_end_) {
            DWORD flags = 0;
            LONGLONG stamp = 0;
            IMFSample* sample = nullptr;
            const HRESULT hr = reader_->ReadSample(kVideo, 0, nullptr, &flags, &stamp, &sample);
            if (FAILED(hr) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM))) {
                if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
                    SDL_Log("video: picture stream stopped (%s)", hr_text(hr).c_str());
                }
                if (sample) sample->Release();
                video_end_ = true;
                break;
            }
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) read_picture_layout();
            if (!sample) continue;  // a gap in the stream (a stream tick): read on
            const double t = static_cast<double>(stamp) * kTick;
            LONGLONG length = 0;
            video_stop_ =
                t + (SUCCEEDED(sample->GetSampleDuration(&length)) && length > 0 ? static_cast<double>(length) * kTick
                                                                                 : (fps_ > 0 ? 1.0 / fps_ : 0.04));
            queue_.emplace_back(t, sample);
            return true;
        }
        return false;
    }

    // RGB32 (blue, green, red, unused) to RGBA.
    void show(IMFSample* sample) {
        Com<IMFMediaBuffer> buf;
        if (FAILED(sample->ConvertToContiguousBuffer(buf.out()))) return;
        Com<IMF2DBuffer> buf2d;
        BYTE* row0 = nullptr;
        LONG pitch = 0;
        BYTE* data = nullptr;
        DWORD size = 0;
        if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(buf2d.out()))) && SUCCEEDED(buf2d->Lock2D(&row0, &pitch))) {
            copy_rows(row0, pitch);
            buf2d->Unlock2D();
        } else if (SUCCEEDED(buf->Lock(&data, nullptr, &size))) {
            const LONG rows = out_h_ > 0 ? out_h_ : height_;
            if (static_cast<long long>(std::abs(stride_)) * rows <= size) {
                copy_rows(stride_ < 0 ? data + static_cast<std::ptrdiff_t>(-stride_) * (rows - 1) : data, stride_);
            }
            buf->Unlock();
        }
    }

    void copy_rows(const BYTE* row0, LONG pitch) {
        const int w = out_w_ > 0 ? std::min(width_, out_w_) : width_;
        const int h = out_h_ > 0 ? std::min(height_, out_h_) : height_;
        for (int y = 0; y < h; ++y) {
            const BYTE* s = row0 + static_cast<std::ptrdiff_t>(pitch) * y;
            std::uint8_t* o = rgba_.data() + static_cast<std::size_t>(y) * width_ * 4;
            for (int x = 0; x < w; ++x, s += 4, o += 4) o[0] = s[2], o[1] = s[1], o[2] = s[0], o[3] = 255;
        }
        fresh_ = true;
        ++frames_;
    }

    // The next piece of sound, as stereo (mono doubled, more channels: the first two).
    void read_sound() {
        DWORD flags = 0;
        LONGLONG stamp = 0;
        Com<IMFSample> sample;
        const HRESULT hr = reader_->ReadSample(kAudio, 0, nullptr, &flags, &stamp, sample.out());
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
            SDL_Log("video: sound stream stopped (%s)", hr_text(hr).c_str());
            audio_end_ = true;
            return;
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) audio_end_ = true;
        if (!sample.p) return;
        Com<IMFMediaBuffer> buf;
        BYTE* data = nullptr;
        DWORD size = 0;
        if (FAILED(sample->ConvertToContiguousBuffer(buf.out())) || FAILED(buf->Lock(&data, nullptr, &size))) return;
        const float* f = reinterpret_cast<const float*>(data);
        const std::size_t n = size / sizeof(float) / static_cast<std::size_t>(channels_);
        audio_.reserve(audio_.size() + n * 2);
        for (std::size_t i = 0; i < n; ++i, f += channels_) {
            audio_.push_back(f[0]);
            audio_.push_back(channels_ > 1 ? f[1] : f[0]);
        }
        buf->Unlock();
        audio_secs_ += static_cast<double>(n) / rate_;
    }

    Com<IMFSourceReader> reader_;
    std::deque<std::pair<double, IMFSample*>> queue_;  // pictures read and not shown yet, by time (owned)
    double video_stop_ = 0;                            // where the last picture read ends
    bool video_end_ = false, audio_end_ = false;
    int width_ = 0, height_ = 0, out_w_ = 0, out_h_ = 0, rate_ = 0, channels_ = 0;
    LONG stride_ = 0;
    double fps_ = 0, duration_ = 0, clock_ = 0, lead_ = 0.2;
};

}  // namespace

std::unique_ptr<VideoDecoder> open_windows_media(const std::string& path, std::string* err) {
    return MfDecoder::open(path, err);
}

}  // namespace antanta::ui

#endif
