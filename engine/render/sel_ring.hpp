



#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "render/iso.hpp"
#include "render/raster.hpp"

namespace antanta {

struct RingDot {
    float x = 0, y = 0;  // the quad's top left in view pixels
    float key = 0;       // the depth key of all four corners
};



struct RingTexture {
    int width = 0, height = 0;
    std::vector<std::array<float, 3>> rgb;  // 0..1
    std::vector<std::uint8_t> opaque;
};
// nullopt when the file is missing or broken (error says why).
std::optional<RingTexture> ring_texture(const GameFileLoader& load, std::string* error);




void ring_dots_draw(const RingTexture& tex, std::span<const RingDot> dots, Target& rt);
// The same pixels straight into the finished 8-bit picture (the house ring, after the landscape filter), tested
// against rt's depth keys.
void ring_dots_draw_rgba(const RingTexture& tex, std::span<const RingDot> dots, const Target& rt,
                         std::vector<std::uint8_t>& rgba);

}  // namespace antanta
