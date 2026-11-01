// CPU rasteriser for the isometric view (docs/render.md section 8): pre-transformed triangles with affine
// attributes, point sampling and a "key" depth buffer where bigger means nearer (section 1).
//
// The arithmetic follows tools/render_proto.py Target.tri step by step (pixel centres at integer coordinates,
// barycentric weights in double, an edge tolerance of 1e-7, the 1e-3 depth slack in float, float colour), so the
// C++ and Python images agree closely. Everything is plain IEEE float/double without contraction, which keeps
// screenshots byte-stable across compilers.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "formats/sprites.hpp"

namespace antanta {

// A 4-bit-per-channel texture (the game's ARGB4444): values 0..15, sampled as value / 15.0f like the prototype.
struct Texture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // width * height * 4 nibbles
};
// From decoded ARGB4444 pixels (each channel nibble * 17); other images are rounded down to nibbles.
Texture texture_from_image(const Image& img);

constexpr float kFarKey = -1e9f;

// Colour, depth key and the "depth still belongs to the water pass" bit for every screen pixel.
struct Target {
    int width = 0;
    int height = 0;
    std::vector<float> rgb;  // 3 per pixel, 0..1
    std::vector<float> key;
    std::vector<std::uint8_t> water;
    void reset(int w, int h);  // black, kFarKey, no water
};

struct Vertex {
    double x = 0, y = 0;  // screen pixels
    double key = 0;
    double u = 0, v = 0;
    double shade = 1;  // grey vertex diffuse, used when DrawState::gouraud
};

struct DrawState {
    const Texture* texture = nullptr;    // nullptr: the colour is `tint`
    bool colour_key = false;             // texels 0x0000 are not drawn (COLORKEY on)
    bool gouraud = false;                // MODULATE with the interpolated vertex shade (terrain, sub-tiles)
    std::array<float, 3> tint{1, 1, 1};  // MODULATE with a constant colour (unit masks, water depth pass)
    bool z_write = true;
    bool blend = false;  // SRCALPHA / INVSRCALPHA
    float alpha = 1;     // times the texel's alpha (a TFACTOR alpha with ALPHAOP MODULATE)
    bool water = false;  // value of the water bit where the key is written
    int clip_top = 0;    // only rows clip_top <= y < clip_bottom are touched (one band per worker thread)
    int clip_bottom = 1 << 30;
};

// Runs fn(top, bottom) for row bands that cover 0 <= y < height, one per thread (up to 16, the threads kept between
// calls; one band on the web build). For passes in which every pixel gets the same operations whatever the band split
// (DrawState::clip_top).
void parallel_bands(int height, const std::function<void(int top, int bottom)>& fn);
// Leaves `cores` logical processors to other work: parallel_bands then makes at most that many bands fewer (the game
// app's sim thread, docs/sim-thread.md 4: one band less where every processor would draw).
void parallel_bands_spare(int cores);

// Depth test is always on: a pixel is drawn when its key >= the stored key - 0.001.
void draw_triangle(Target& t, const Vertex& a, const Vertex& b, const Vertex& c, const DrawState& s);
// Vertices top-left, top-right, bottom-left, bottom-right (strip order); triangles 0-1-2 and 1-2-3.
void draw_quad(Target& t, const std::array<Vertex, 4>& v, const DrawState& s);

}  // namespace antanta
