#include "image.hpp"

#include <cstdlib>

namespace antanta {

std::optional<Image> decode_tga(ByteSpan file, std::string* error) {
    if (file.size() < 18) {
        *error = ".tga shorter than its 18-byte header";
        return std::nullopt;
    }
    const int id_len = file[0], cmap_type = file[1], type = file[2];
    const int w = read_u16(file, 12), h = read_u16(file, 14), bpp = file[16], desc = file[17];
    if (cmap_type != 0 || type != 2 || (bpp != 24 && bpp != 32)) {
        *error = ".tga is not uncompressed 24/32-bit true colour";
        return std::nullopt;
    }
    const std::size_t px = static_cast<std::size_t>(bpp / 8);
    const std::size_t start = 18 + static_cast<std::size_t>(id_len);
    const std::size_t need = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * px;
    if (file.size() < start || file.size() - start < need) {
        *error = ".tga pixel data truncated";
        return std::nullopt;
    }
    const bool top_down = (desc & 0x20) != 0;
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (int y = 0; y < h; ++y) {
        const std::size_t src_row = static_cast<std::size_t>(top_down ? y : h - 1 - y);
        for (int x = 0; x < w; ++x) {
            const std::size_t s = start + (src_row * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * px;
            std::uint8_t* d = &img.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + x) * 4];
            d[0] = file[s + 2];
            d[1] = file[s + 1];
            d[2] = file[s + 0];
            d[3] = px == 4 ? file[s + 3] : 255;
        }
    }
    return img;
}

std::optional<Image> decode_bmp(ByteSpan file, std::string* error) {
    if (file.size() < 54 || file[0] != 'B' || file[1] != 'M') {
        *error = ".bmp header missing";
        return std::nullopt;
    }
    const std::uint32_t data_off = read_u32(file, 10);
    const std::uint32_t header = read_u32(file, 14);
    const auto w = static_cast<std::int32_t>(read_u32(file, 18));
    const auto h_raw = static_cast<std::int32_t>(read_u32(file, 22));
    const int bpp = read_u16(file, 28);
    const std::uint32_t compression = read_u32(file, 30);
    if (header < 40 || bpp != 24 || compression != 0 || w <= 0 || h_raw == 0 || w > 65535 || h_raw < -65535 ||
        h_raw > 65535) {
        *error = ".bmp is not an uncompressed 24-bit bitmap";
        return std::nullopt;
    }
    const int h = std::abs(h_raw);
    const std::size_t stride = (static_cast<std::size_t>(w) * 3 + 3) / 4 * 4;
    if (data_off > file.size() || (file.size() - data_off) / stride < static_cast<std::size_t>(h)) {
        *error = ".bmp pixel data truncated";
        return std::nullopt;
    }
    Image img;
    img.width = w;
    img.height = h;
    img.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (int y = 0; y < h; ++y) {
        const std::size_t src_row = static_cast<std::size_t>(h_raw < 0 ? y : h - 1 - y);
        for (int x = 0; x < w; ++x) {
            const std::size_t s = data_off + src_row * stride + static_cast<std::size_t>(x) * 3;
            std::uint8_t* d = &img.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + x) * 4];
            d[0] = file[s + 2];
            d[1] = file[s + 1];
            d[2] = file[s + 0];
            d[3] = 255;
        }
    }
    return img;
}

}  // namespace antanta
