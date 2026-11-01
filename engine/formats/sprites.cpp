#include "sprites.hpp"

#include <algorithm>
#include <cstring>

namespace antanta {
namespace {

constexpr std::size_t kFrameRecord = 20;
constexpr std::uint32_t kInlineMarker = 0xCCCCCCCC;
constexpr int kDirections = 5;
constexpr int kAtlasSide = 256;

std::uint64_t pixel_bytes(std::uint64_t w, std::uint64_t h, std::uint64_t bpp) { return w * h * bpp; }

struct FrameRecord {
    int off_x, off_y;
    std::uint16_t cols, rows;
    std::uint32_t size, offset;
};

FrameRecord read_record(ByteSpan file, std::size_t p) {
    return {static_cast<std::int16_t>(read_u16(file, p)),
            static_cast<std::int16_t>(read_u16(file, p + 2)),
            read_u16(file, p + 4),
            read_u16(file, p + 6),
            read_u32(file, p + 12),
            read_u32(file, p + 16)};
}

// n frame records at pos, pixels at their absolute offsets. A frame whose data would run past the end of
// the file is an empty frame (formats.md .egr), as in tools/sprite_dump.py read_frames.
bool read_frames(ByteSpan file, std::size_t pos, std::size_t n, std::vector<SpriteFrame>* out, std::uint64_t* data_end,
                 std::string* error) {
    if (pos > file.size() || (file.size() - pos) / kFrameRecord < n) {
        *error = "frame table runs past the end of the file";
        return false;
    }
    *data_end = 0;
    out->reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const FrameRecord r = read_record(file, pos + i * kFrameRecord);
        const std::uint64_t end = std::uint64_t{r.offset} + r.size;
        *data_end = std::max(*data_end, end);
        SpriteFrame f;
        f.off_x = r.off_x;
        f.off_y = r.off_y;
        if (end <= file.size()) {
            if (r.size < pixel_bytes(r.cols, r.rows, 2)) {
                *error = "frame " + std::to_string(i) + ": pixel data smaller than width * height * 2";
                return false;
            }
            f.has_pixels = true;
            f.image = decode_argb4444(file.subspan(r.offset, r.size), r.cols, r.rows);
        }
        out->push_back(std::move(f));
    }
    return true;
}

// Row span table shared by .img and .hzb: per row u16 n, then n * (u16 x0, u16 x1, u32 offset).
struct Span {
    int y, x0, x1;
    std::uint32_t offset;
};

bool read_spans(Cursor& c, int w, int h, std::vector<Span>* spans, std::string* error) {
    for (int y = 0; y < h; ++y) {
        const int n = c.u16();
        for (int i = 0; i < n && c.ok; ++i) {
            Span s{y, c.u16(), c.u16(), c.u32()};
            if (c.ok && (s.x0 > s.x1 || s.x1 >= w)) {
                *error = "span outside the image in row " + std::to_string(y);
                return false;
            }
            spans->push_back(s);
        }
        if (!c.ok) {
            *error = "span table runs past the end of the file";
            return false;
        }
    }
    return true;
}

std::string lower_suffix(std::string_view name) {
    const std::size_t dot = name.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size()) return {};
    std::string s(name.substr(dot));
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
    }
    return s;
}

}  // namespace

SpriteKind sprite_kind(std::string_view name, ByteSpan data) {
    const std::string ext = lower_suffix(name);
    if (ext == ".444") return SpriteKind::Tex444;
    if (ext == ".ugr") return SpriteKind::Ugr;
    if (ext == ".egr") return SpriteKind::Egr;
    if (ext == ".img") return SpriteKind::Img;
    if (ext == ".ogr") return SpriteKind::Ogr;
    if (ext == ".g4m" || ext == ".g4") return SpriteKind::G4m;
    if (ext == ".hzb") return SpriteKind::Hzb;
    if (ext == ".shp" || ext == ".twr") return SpriteKind::Shp;
    if (ext.empty() && data.size() >= 4 && data.size() == 4 + pixel_bytes(read_u16(data, 0), read_u16(data, 2), 2)) {
        return SpriteKind::Tex444;
    }
    return SpriteKind::None;
}

Image decode_argb4444(ByteSpan pixels, int width, int height) {
    Image img;
    img.width = width;
    img.height = height;
    const std::size_t n = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    img.rgba.resize(n * 4);
    for (std::size_t i = 0; i < n; ++i) {
        const unsigned v = read_u16(pixels, i * 2);
        img.rgba[i * 4 + 0] = static_cast<std::uint8_t>((v >> 8 & 15) * 17);
        img.rgba[i * 4 + 1] = static_cast<std::uint8_t>((v >> 4 & 15) * 17);
        img.rgba[i * 4 + 2] = static_cast<std::uint8_t>((v & 15) * 17);
        img.rgba[i * 4 + 3] = static_cast<std::uint8_t>((v >> 12) * 17);
    }
    return img;
}

std::optional<Image> decode_444(ByteSpan file, std::string* error) {
    if (file.size() < 4) {
        *error = ".444 shorter than its 4-byte header";
        return std::nullopt;
    }
    const int w = read_u16(file, 0), h = read_u16(file, 2);
    if (file.size() - 4 < pixel_bytes(w, h, 2)) {
        *error = ".444 pixel data shorter than width * height * 2";
        return std::nullopt;
    }
    return decode_argb4444(file.subspan(4), w, h);
}

std::optional<Ugr> decode_ugr(ByteSpan file, std::string* error) {
    if (file.size() < 2) {
        *error = ".ugr shorter than its 2-byte header";
        return std::nullopt;
    }
    Ugr ugr;
    ugr.frames_per_dir = file[0];
    ugr.flags = file[1];
    if (ugr.frames_per_dir == 0) {
        *error = ".ugr has 0 frames per direction";
        return std::nullopt;
    }
    const int layers = 1 + (ugr.flags >> 1 & 1) + (ugr.flags & 1);
    const std::size_t count = static_cast<std::size_t>(ugr.frames_per_dir) * kDirections;
    std::uint64_t pos = 2;
    for (int l = 0; l < layers; ++l) {
        std::vector<SpriteFrame> frames;
        std::uint64_t end = 0;
        const std::size_t at = static_cast<std::size_t>(std::min<std::uint64_t>(pos, file.size() + 1));
        if (!read_frames(file, at, count, &frames, &end, error)) {
            *error = "layer " + std::to_string(l) + ": " + *error;
            return std::nullopt;
        }
        ugr.layers.push_back(std::move(frames));
        pos = end;  // the next layer starts where the last frame's data ends
    }
    return ugr;
}

std::optional<Egr> decode_egr(ByteSpan file, std::string* error) {
    Cursor c{file};
    const int n = c.u8();
    Egr egr;
    egr.canvas_w = c.u32();
    egr.canvas_h = c.u32();
    egr.anchor_x = c.u16();
    egr.anchor_y = c.u16();
    if (!c.ok) {
        *error = ".egr shorter than its 13-byte header";
        return std::nullopt;
    }
    std::uint64_t end = 0;
    if (!read_frames(file, c.pos, static_cast<std::size_t>(n), &egr.frames, &end, error)) return std::nullopt;
    return egr;
}

std::optional<Image> decode_img(ByteSpan file, std::string* error) {
    Cursor c{file};
    const ByteSpan magic = c.take(5);
    if (!c.ok || std::memcmp(magic.data(), "~cIMg", 5) != 0) {
        *error = ".img magic ~cIMg missing";
        return std::nullopt;
    }
    const int w = c.u16(), h = c.u16();
    const std::uint32_t pixel_size = c.u32();
    std::vector<Span> spans;
    if (!c.ok) {
        *error = ".img header truncated";
        return std::nullopt;
    }
    if (!read_spans(c, w, h, &spans, error)) return std::nullopt;
    const ByteSpan pix = file.subspan(c.pos, std::min<std::size_t>(pixel_size, file.size() - c.pos));
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    for (const Span& s : spans) {
        const std::size_t len = static_cast<std::size_t>(s.x1 - s.x0 + 1);
        if (s.offset > pix.size() || (pix.size() - s.offset) / 3 < len) {
            *error = "span pixels run past the pixel data in row " + std::to_string(s.y);
            return std::nullopt;
        }
        std::uint8_t* dst = &img.rgba[(static_cast<std::size_t>(s.y) * w + s.x0) * 4];
        for (std::size_t i = 0; i < len; ++i) {
            dst[i * 4 + 0] = pix[s.offset + i * 3 + 0];
            dst[i * 4 + 1] = pix[s.offset + i * 3 + 1];
            dst[i * 4 + 2] = pix[s.offset + i * 3 + 2];
            dst[i * 4 + 3] = 255;
        }
    }
    return img;
}

std::optional<Ogr> decode_ogr(ByteSpan file, std::string* error) {
    Cursor c{file};
    Ogr ogr;
    ogr.entry_count = c.u16();
    const ByteSpan entries = c.take(std::size_t{ogr.entry_count} * 56);
    const int pages = c.u8();
    if (!c.ok) {
        *error = ".ogr entry table truncated";
        return std::nullopt;
    }
    ogr.entries.assign(entries.begin(), entries.end());
    for (int p = 0; p < pages; ++p) {
        const ByteSpan page = c.take(pixel_bytes(kAtlasSide, kAtlasSide, 2));
        if (!c.ok) {
            *error = ".ogr atlas page " + std::to_string(p) + " truncated";
            return std::nullopt;
        }
        ogr.pages.push_back(decode_argb4444(page, kAtlasSide, kAtlasSide));
    }
    return ogr;
}

std::optional<G4m> decode_g4m(ByteSpan file, std::string* error) {
    Cursor c{file};
    G4m g;
    g.canvas_w = c.u16();
    g.canvas_h = c.u16();
    g.off_x = c.u16();
    g.off_y = c.u16();
    g.tex_w = c.u16();
    g.tex_h = c.u16();
    const int cols = c.u16(), rows = c.u16();
    const ByteSpan pix = c.take(pixel_bytes(cols, rows, 2));
    if (!c.ok) {
        *error = ".g4m truncated";
        return std::nullopt;
    }
    g.image = decode_argb4444(pix, cols, rows);
    return g;
}

std::uint16_t house_les_id(int phase, int stage, int part) {
    const int base = phase == 0 ? 0 : phase == 1 ? (stage + 1) * 10 : (stage + 5) * 10;
    return static_cast<std::uint16_t>(base + part);
}

std::optional<DepthMap> decode_hzb(ByteSpan file, std::string* error, std::uint16_t gap) {
    DepthMap d;
    if (file.size() >= 6 && std::memcmp(file.data(), "~cHMap", 6) == 0) {
        Cursor c{file, 6};
        d.spans = true;
        d.width = c.u16();
        d.height = c.u16();
        const std::uint32_t pixel_size = c.u32();
        std::vector<Span> spans;
        if (!c.ok) {
            *error = ".hzb header truncated";
            return std::nullopt;
        }
        if (!read_spans(c, d.width, d.height, &spans, error)) return std::nullopt;
        // pixelBytes overshoots the stored data (formats.md); use what the file holds.
        const std::size_t values = std::min<std::size_t>(pixel_size - pixel_size % 2, file.size() - c.pos) / 2;
        const std::size_t n = static_cast<std::size_t>(d.width) * static_cast<std::size_t>(d.height);
        d.depth.assign(n, gap);
        std::vector<bool> done(n, false);
        for (const Span& s : spans) {
            const std::size_t len = static_cast<std::size_t>(s.x1 - s.x0 + 1);
            if (s.offset > values || values - s.offset < len) {
                *error = "span depth values run past the data in row " + std::to_string(s.y);
                return std::nullopt;
            }
            for (std::size_t i = 0; i < len; ++i) {
                const std::size_t k = static_cast<std::size_t>(s.y) * d.width + s.x0 + i;
                if (done[k]) continue;
                done[k] = true;
                d.depth[k] = read_u16(file, c.pos + (s.offset + i) * 2);
            }
        }
        d.blit.assign(n, static_cast<std::int16_t>(kNoDepth));
        for (const Span& s : spans) {
            for (int x = s.x0; x < s.x1; ++x) {
                const std::uint16_t v = read_u16(file, c.pos + (s.offset + static_cast<std::size_t>(x - s.x0)) * 2);
                d.blit[static_cast<std::size_t>(s.y) * d.width + x] = static_cast<std::int16_t>(2 * v + 1);
            }
        }
        return d;
    }
    // Raw: no header, square or 2:1 (formats.md); the size gives the dimensions.
    if (file.empty() || file.size() % 2 != 0) {
        *error = "raw .hzb size is not a whole number of u16 values";
        return std::nullopt;
    }
    const std::size_t n = file.size() / 2;
    auto isqrt = [](std::size_t v) {
        std::size_t r = 0;
        while ((r + 1) * (r + 1) <= v) ++r;
        return r;
    };
    const std::size_t root = isqrt(n);
    const std::size_t w = root * root == n ? root : isqrt(n * 2);
    if (w == 0 || n % w != 0) {
        *error = "raw .hzb size is neither square nor 2:1";
        return std::nullopt;
    }
    d.width = static_cast<int>(w);
    d.height = static_cast<int>(n / w);
    d.depth.resize(n);
    for (std::size_t i = 0; i < n; ++i) d.depth[i] = read_u16(file, i * 2);
    return d;
}

std::optional<std::vector<SpriteFrame>> decode_shp(ByteSpan file, std::string* error) {


    std::vector<SpriteFrame> frames;
    std::size_t pos = 0;
    while (pos < file.size()) {
        if (file.size() - pos < kFrameRecord) {
            *error = "record cut off at byte " + std::to_string(pos);
            return std::nullopt;
        }
        const FrameRecord r = read_record(file, pos);
        if (r.offset == kInlineMarker && r.size == pixel_bytes(r.cols, r.rows, 2)) {
            if (file.size() - pos - kFrameRecord < r.size) {
                *error = "frame pixels run past the end of the file";
                return std::nullopt;
            }
            SpriteFrame f;
            f.off_x = r.off_x;
            f.off_y = r.off_y;
            f.has_pixels = true;
            f.image = decode_argb4444(file.subspan(pos + kFrameRecord, r.size), r.cols, r.rows);
            frames.push_back(std::move(f));
            pos += kFrameRecord + r.size;
        } else {
            ++pos;  // group count byte
        }
    }
    if (frames.empty()) {
        *error = "no frames";
        return std::nullopt;
    }
    return frames;
}

}  // namespace antanta
