#include "ui/fonts.hpp"

#include <SDL3/SDL_log.h>

namespace antanta::ui {

TTF_Font* open_gdi_font(const std::string& path, int height) {
    TTF_Font* font = TTF_OpenFont(path.c_str(), 1000.0f);
    if (!font) return nullptr;
    no_kerning(font);
    const long ascent = TTF_GetFontAscent(font), descent = -TTF_GetFontDescent(font);
    int em = height;
    while (em > 1 && (ascent * em + 999) / 1000 + (descent * em + 999) / 1000 > height) --em;
    TTF_SetFontSize(font, static_cast<float>(em));
    return font;
}

void check_drawn(Language* lang, std::string_view text, const std::source_location& where) {
    if (!lang || !lang->pseudo) return;
    if (const std::optional<std::string> miss = lang_check(*lang, text, where)) SDL_Log("%s", miss->c_str());
}

Coverage text_coverage(TTF_Font* font, std::string_view text) {
    Coverage c;
    c.ascent = TTF_GetFontAscent(font);
    int minx = 0, maxx = 0, miny = 0, maxy = 0, advance = 0;
    if (TTF_GetGlyphMetrics(font, 'H', &minx, &maxx, &miny, &maxy, &advance)) c.cap = maxy;
    SDL_Surface* drawn =
        text.empty() ? nullptr : TTF_RenderText_Blended(font, text.data(), text.size(), SDL_Color{255, 255, 255, 255});
    SDL_Surface* s = drawn ? SDL_ConvertSurface(drawn, SDL_PIXELFORMAT_RGBA32) : nullptr;
    if (s) {
        c.w = s->w, c.h = s->h;
        c.a.resize(static_cast<std::size_t>(c.w) * c.h);
        for (int y = 0; y < c.h; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(s->pixels) + static_cast<std::ptrdiff_t>(y) * s->pitch;
            for (int x = 0; x < c.w; ++x) c.a[static_cast<std::size_t>(y) * c.w + x] = row[x * 4 + 3];
        }
    }
    SDL_DestroySurface(s);
    SDL_DestroySurface(drawn);
    return c;
}

}  // namespace antanta::ui
