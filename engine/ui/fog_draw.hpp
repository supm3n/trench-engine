// The fog of war over the game view drawn with SDL_Renderer (docs/fog.md 7.2 to 7.4; the rules in ui/fog_view.hpp):
// the sample grid as a small black texture with the fog's alpha, stretched with a linear filter over the world, after
// the selection marks and before the interface.
#pragma once

#include <SDL3/SDL.h>

#include <optional>
#include <string>

#include "ui/fog_view.hpp"
#include "ui/frontend.hpp"

namespace antanta::ui {

// The ramp from Data/Misc/Fog/SuperFog.tga (a game file read at run time).
std::optional<FogRamp> fog_ramp_load(const GameFiles& files, std::string* error);

class FogTexture {
  public:
    FogTexture() = default;
    FogTexture(const FogTexture&) = delete;
    FogTexture& operator=(const FogTexture&) = delete;
    ~FogTexture();
    // Uploads `f` when `upload`, then draws it: window pixel = (world pixel - view) * zoom.
    void draw(SDL_Renderer* r, const FogView& f, bool upload, double view_x, double view_y, double zoom);

  private:
    SDL_Texture* tex_ = nullptr;
    int w_ = 0, h_ = 0;
};

}  // namespace antanta::ui
