#include "ui/fog_draw.hpp"

#include <vector>

#include "formats/image.hpp"

namespace antanta::ui {

std::optional<FogRamp> fog_ramp_load(const GameFiles& files, std::string* error) {
    const std::string path = "Data/Misc/Fog/SuperFog.tga";
    auto file = files(path, error);
    if (!file) return std::nullopt;
    auto img = decode_tga(*file, error);
    if (!img) {
        *error = path + ": " + *error;
        return std::nullopt;
    }
    return fog_ramp(img->rgba, img->width, img->height);
}

FogTexture::~FogTexture() { SDL_DestroyTexture(tex_); }

void FogTexture::draw(SDL_Renderer* r, const FogView& f, bool upload, double view_x, double view_y, double zoom) {
    if (f.cols <= 0 || f.rows <= 0) return;
    if (!tex_ || w_ != f.cols || h_ != f.rows) {
        SDL_DestroyTexture(tex_);
        tex_ = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, f.cols, f.rows);
        if (!tex_) return;
        SDL_SetTextureBlendMode(tex_, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex_, SDL_SCALEMODE_LINEAR);
        w_ = f.cols, h_ = f.rows, upload = true;
    }
    if (upload) {
        std::vector<std::uint32_t> px(f.alpha.size());
        for (std::size_t i = 0; i < px.size(); ++i) px[i] = static_cast<std::uint32_t>(f.alpha[i]) << 24;  // black
        SDL_UpdateTexture(tex_, nullptr, px.data(), f.cols * 4);
    }
    // Sample (i, j) sits at world (x0 + i step, y0 + j step): the texel centres go there.
    const double z = zoom;
    const SDL_FRect src{0.5f, 0.5f, static_cast<float>(f.cols - 1), static_cast<float>(f.rows - 1)};
    const SDL_FRect dst{static_cast<float>((f.x0 - view_x) * z), static_cast<float>((f.y0 - view_y) * z),
                        static_cast<float>((f.cols - 1) * f.step * z), static_cast<float>((f.rows - 1) * f.step * z)};
    SDL_RenderTexture(r, tex_, &src, &dst);
}

}  // namespace antanta::ui
