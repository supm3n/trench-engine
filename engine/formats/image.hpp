// Plain picture files used by the renderer: Data/LandTextures/Filter.tga and m44.bmp. Only the variants the game
// ships are supported; anything else is an error.
#pragma once

#include <optional>
#include <string>

#include "bytes.hpp"
#include "sprites.hpp"

namespace antanta {

// Uncompressed true-colour TGA (image type 2), 24 or 32 bits, either row order. Output RGBA8, top row first;
// alpha 255 for 24-bit files.
std::optional<Image> decode_tga(ByteSpan file, std::string* error);

// Uncompressed 24-bit BMP (BITMAPINFOHEADER or later, BI_RGB), bottom-up or top-down. Output RGBA8, alpha 255.
std::optional<Image> decode_bmp(ByteSpan file, std::string* error);

}  // namespace antanta
