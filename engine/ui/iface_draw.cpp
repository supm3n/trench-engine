#include "ui/iface_draw.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <optional>

#include "formats/image.hpp"
#include "formats/les.hpp"
#include "ui/fonts.hpp"

namespace antanta::ui {

namespace {

// One picture of the interface: the decoded BMP (magenta transparent) and its texture at the current scale and filter.
struct Pic {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
    SDL_Texture* tex = nullptr;
};

enum PicId { kUp, kDown, kLeft, kRight, kThermometer, kPicCount };

}  // namespace

struct GameInterface {
    SDL_Renderer* renderer = nullptr;
    Language* lang = nullptr;  // the marker check (docs/languages.md 0)
    TTF_TextEngine* engine = nullptr;
    std::string serif_path, sans_path;
    TTF_Font* measure = nullptr;  // Times New Roman Bold 16 at scale 1

    int font_scale = 0;
    TTF_Font* serif16 = nullptr;
    TTF_Font* sans14 = nullptr;
    int sans_dy = 0;  // added to y so the baseline lands where GDI puts it
    std::array<Pic, kPicCount> pics;
    int pic_scale = 0;
    PictureFilter pic_filter = PictureFilter::Between;
    ~GameInterface() {
        for (Pic& p : pics) SDL_DestroyTexture(p.tex);
        if (engine) TTF_DestroyRendererTextEngine(engine);
        if (measure) TTF_CloseFont(measure);
        if (serif16) TTF_CloseFont(serif16);
        if (sans14) TTF_CloseFont(sans14);
        TTF_Quit();
    }
};

void GameInterfaceFree::operator()(GameInterface* g) const { std::unique_ptr<GameInterface> owned(g); }

namespace {

bool load_pic(const GameFiles& files, const std::string& archive, const std::string& name, Pic* out,
              std::string* error) {
    auto data = files(archive, error);
    if (!data) return false;
    auto table = parse_les(*data, error);
    if (!table) {
        *error = archive + ": " + *error;
        return false;
    }
    const LesEntry* e = find_entry(*table, name);
    if (!e) {
        *error = archive + ": no picture " + name;
        return false;
    }
    auto bytes = read_entry(*data, *e, error);
    if (!bytes) return false;
    auto img = decode_bmp(*bytes, error);
    if (!img) {
        *error = archive + "/" + name + ": " + *error;
        return false;
    }
    for (std::size_t i = 0; i + 3 < img->rgba.size(); i += 4) {  // the colour key of every interface BMP
        if (img->rgba[i] == 255 && img->rgba[i + 1] == 0 && img->rgba[i + 2] == 255) img->rgba[i + 3] = 0;
    }
    out->w = img->width, out->h = img->height;
    out->rgba = std::move(img->rgba);
    return true;
}

// The pictures as textures k times their size with the filter (rebuilt when either changes).
void prepare_pictures(GameInterface& g, int k, PictureFilter f) {
    if (g.pic_scale == k && g.pic_filter == f) return;
    for (Pic& p : g.pics) {
        SDL_DestroyTexture(p.tex);
        p.tex = nullptr;
        std::vector<std::uint8_t> px = scale_picture(p.rgba, p.w, p.h, k, f);
        SDL_Surface* s = SDL_CreateSurfaceFrom(p.w * k, p.h * k, SDL_PIXELFORMAT_RGBA32, px.data(), p.w * k * 4);
        if (s) p.tex = SDL_CreateTextureFromSurface(g.renderer, s);
        SDL_DestroySurface(s);
        if (!p.tex) continue;
        SDL_SetTextureScaleMode(p.tex, SDL_SCALEMODE_NEAREST);  // DirectDraw's stretching Blt
        SDL_SetTextureBlendMode(p.tex, SDL_BLENDMODE_BLEND);
    }
    g.pic_scale = k, g.pic_filter = f;
}

// "BankGothic RUSS" 14 as Windows draws it (Arial, frontend.md 7): measured in hud.png, the digits are 8 pixels high
// with a pitch of 6 and their baseline is 11 pixels below the y TextOut gets, which is Arial at an em of 11 pixels.
// The em and the baseline both grow with the scale.
constexpr int kSansEm = 11, kSansBaseline = 11;

void prepare_fonts(GameInterface& g, int k) {
    if (g.font_scale == k) return;
    if (g.serif16) TTF_CloseFont(g.serif16);
    if (g.sans14) TTF_CloseFont(g.sans14);
    g.serif16 = open_gdi_font(g.serif_path, 16 * k);
    g.sans14 = no_kerning(TTF_OpenFont(g.sans_path.c_str(), static_cast<float>(kSansEm * k)));
    g.sans_dy = g.sans14 ? kSansBaseline * k - TTF_GetFontAscent(g.sans14) : 0;
    g.font_scale = k;
}

void draw_pic(SDL_Renderer* r, const Pic& p, const IRect& dst) {
    if (!p.tex) return;
    const SDL_FRect d{static_cast<float>(dst.x), static_cast<float>(dst.y), static_cast<float>(dst.w),
                      static_cast<float>(dst.h)};
    SDL_RenderTexture(r, p.tex, nullptr, &d);
}


void draw_text(GameInterface& g, TTF_Font* font, const std::string& s, int x, int y, std::uint32_t rgb,
               const std::uint32_t* shadow, int dx, int dy,
               const std::source_location& where = std::source_location::current()) {
    if (!font || s.empty()) return;
    check_drawn(g.lang, s, where);
    TTF_Text* t = TTF_CreateText(g.engine, font, s.c_str(), 0);
    if (!t) return;
    if (shadow) {
        TTF_SetTextColor(t, static_cast<Uint8>(*shadow >> 16), static_cast<Uint8>(*shadow >> 8),
                         static_cast<Uint8>(*shadow), 255);
        TTF_DrawRendererText(t, static_cast<float>(x + dx), static_cast<float>(y + dy));
    }
    TTF_SetTextColor(t, static_cast<Uint8>(rgb >> 16), static_cast<Uint8>(rgb >> 8), static_cast<Uint8>(rgb), 255);
    TTF_DrawRendererText(t, static_cast<float>(x), static_cast<float>(y));
    TTF_DestroyText(t);
}

constexpr std::uint32_t kPaleYellow = 0xfff799;
constexpr std::uint32_t kWhite = 0xffffff;

}  // namespace

GameInterfacePtr interface_open(SDL_Renderer* renderer, GameFiles files, const std::vector<std::string>& serif_fonts,
                                const std::vector<std::string>& sans_fonts, std::string* error, Language* lang) {
    GameInterfacePtr g(std::make_unique<GameInterface>().release());
    g->renderer = renderer;
    g->lang = lang;
    if (!TTF_Init()) {
        *error = std::string("SDL_ttf: ") + SDL_GetError();
        return nullptr;
    }

    const struct {
        const char* archive;
        const char* name;
    } kPics[kPicCount] = {{"Data/Interface/MapBevels.les", "MapBevelUp.bmp"},
                          {"Data/Interface/MapBevels.les", "MapBevelDown.bmp"},
                          {"Data/Interface/MapBevels.les", "MapBevelLeft.bmp"},
                          {"Data/Interface/MapBevels.les", "MapBevelRight.bmp"},
                          {"Data/Interface/interface.les", "Thermometer.bmp"}};
    for (int i = 0; i < kPicCount; ++i) {
        if (!load_pic(files, kPics[i].archive, kPics[i].name, &g->pics[i], error)) return nullptr;
    }
    auto first = [](const std::vector<std::string>& paths, int cell, std::string* chosen) -> TTF_Font* {
        for (const std::string& p : paths) {
            if (TTF_Font* f = open_gdi_font(p, cell)) {
                *chosen = p;
                return f;
            }
        }
        return nullptr;
    };
    g->measure = first(serif_fonts, 16, &g->serif_path);
    TTF_Font* probe = first(sans_fonts, 14, &g->sans_path);
    if (probe) TTF_CloseFont(probe);
    if (!g->measure || !probe) {
        *error =
            "no interface font could be opened (Times New Roman Bold or Liberation Serif Bold, Arial or "
            "Liberation Sans)";
        return nullptr;
    }
    g->engine = TTF_CreateRendererTextEngine(renderer);
    if (!g->engine) {
        *error = std::string("SDL_ttf text engine: ") + SDL_GetError();
        return nullptr;
    }
    SDL_Log("interface fonts: %s and %s", g->serif_path.c_str(), g->sans_path.c_str());
    return g;
}

int interface_text_width(GameInterface& g, const std::string& text) {
    int w = 0;
    if (g.measure && !text.empty()) TTF_GetStringSize(g.measure, text.c_str(), 0, &w, nullptr);
    return w;
}

void interface_draw(GameInterface& g, const HudLayout& l, PictureFilter filter, const InterfaceFrame& frame,
                    const ScreenTexts& texts, const std::function<void()>& after_texts) {
    SDL_Renderer* r = g.renderer;
    const int k = l.scale;
    prepare_pictures(g, k, filter);
    prepare_fonts(g, k);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);



    const std::uint32_t near_black = 0x010000;
    for (const ScreenText& e : texts.entries) {
        draw_text(g, g.serif16, e.text, e.x * k, e.y * k, e.rgb, &near_black, 2 * k, k);
    }
    if (frame.build_more_houses) {
        draw_text(g, g.serif16, lang_text(g.lang ? *g.lang : lang_english(), "game.build_more_houses"), l.build_x,
                  l.build_y, kWhite, &near_black, 2 * k, k);
    }
    if (after_texts) {
        after_texts();
        SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    }


    draw_pic(r, g.pics[kUp], l.top);
    draw_pic(r, g.pics[kDown], l.bottom);
    draw_pic(r, g.pics[kLeft], l.left);
    draw_pic(r, g.pics[kRight], l.right);
    draw_pic(r, g.pics[kThermometer], l.thermometer);
    if (frame.fill.bottom > frame.fill.top) {
        SDL_SetRenderDrawColor(r, frame.fill.r, frame.fill.g, frame.fill.b, 255);
        const SDL_FRect col{static_cast<float>(l.fill_x), static_cast<float>(frame.fill.top * k),
                            static_cast<float>(l.fill_w), static_cast<float>((frame.fill.bottom - frame.fill.top) * k)};
        SDL_RenderFillRect(r, &col);
    }


    const std::uint32_t black = 0;
    for (int i = 0; i < 6; ++i) {
        draw_text(g, g.sans14, frame.numbers[i], l.number_x[i], l.number_y + g.sans_dy, kPaleYellow, &black,
                  l.shadow_dx, l.shadow_dy);
    }


    if (frame.version) {
        draw_text(g, g.serif16, lang_text(g.lang ? *g.lang : lang_english(), "version.game"), l.version_x, l.version_y,
                  kPaleYellow, nullptr, 0, 0);
    }
}

}  // namespace antanta::ui
