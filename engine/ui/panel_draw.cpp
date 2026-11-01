#include "ui/panel_draw.hpp"

#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <tuple>

#include "formats/image.hpp"
#include "formats/les.hpp"
#include "ui/fonts.hpp"
#include "ui/text_fit.hpp"

namespace antanta::ui {

namespace {

// A decoded picture (magenta transparent).
struct Source {
    bool ok = false;
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
};

struct Archive {
    bool ok = false;
    Bytes data;
    LesTable table;
};

// One frame of a picture stretched to a size and scaled: the texture key.
using TexKey = std::tuple<std::string, int, int, int, int>;  // picture key, frames, frame, w, h

std::string pic_key(const PanelPic& p) {
    std::string n = p.name;
    for (char& c : n) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return p.archive + "|" + (p.name.empty() ? "#" + std::to_string(p.id) : n);
}

// "BankGothic RUSS" 14 as Windows draws it: Arial at an em of 11 pixels with the baseline 11 pixels below the y TextOut
// gets (measured in hud.png; the same rule as ui/iface_draw.cpp).
constexpr int kSansEm = 11, kSansBaseline = 11;
// "BankGothic RUSS" 10 the same way: Arial at an em of 8 with the baseline 8 below (a "0" 4 x 6 pixels from y + 2,
// measured in panel_market.png).
constexpr int kSans10Em = 8, kSans10Baseline = 8;

}  // namespace

struct PanelGraphics {
    SDL_Renderer* renderer = nullptr;
    Language* lang = nullptr;  // the marker check (docs/languages.md 0)
    GameFiles files;
    TTF_TextEngine* engine = nullptr;
    std::string serif_path, sans_path;
    int font_scale = 0;
    TTF_Font* serif16 = nullptr;
    TTF_Font* serif18 = nullptr;
    TTF_Font* sans14 = nullptr;
    TTF_Font* sans10 = nullptr;
    int sans_dy = 0, sans10_dy = 0;
    std::map<int, TTF_Font*> fit_fonts;  // Times at the smaller ems of a fitted label (docs/languages.md 2.5)
    void close_fit_fonts() {
        for (auto& [em, font] : fit_fonts) TTF_CloseFont(font);
        fit_fonts.clear();
    }
    std::map<std::string, Archive> archives;
    std::map<std::string, Source> sources;
    std::map<TexKey, SDL_Texture*> textures;
    int tex_scale = 0;
    PictureFilter tex_filter = PictureFilter::Between;
    std::set<std::string> logged;
    bool ttf = false;
    void clear_textures() {
        for (auto& [k, t] : textures) SDL_DestroyTexture(t);
        textures.clear();
    }
    ~PanelGraphics() {
        clear_textures();
        if (engine) TTF_DestroyRendererTextEngine(engine);
        if (serif16) TTF_CloseFont(serif16);
        if (serif18) TTF_CloseFont(serif18);
        if (sans14) TTF_CloseFont(sans14);
        if (sans10) TTF_CloseFont(sans10);
        close_fit_fonts();
        if (ttf) TTF_Quit();
    }
};

void PanelGraphicsFree::operator()(PanelGraphics* g) const { std::unique_ptr<PanelGraphics> owned(g); }

namespace {

void log_once(PanelGraphics& g, const std::string& what) {
    if (g.logged.insert(what).second) SDL_Log("panel: %s", what.c_str());
}

const Source& source(PanelGraphics& g, const PanelPic& pic) {
    const std::string key = pic_key(pic);
    auto found = g.sources.find(key);
    if (found != g.sources.end()) return found->second;
    Source& out = g.sources[key];
    auto ait = g.archives.find(pic.archive);
    if (ait == g.archives.end()) {
        Archive a;
        std::string err;
        if (auto data = g.files(pic.archive, &err)) {
            if (auto table = parse_les(*data, &err)) {
                a.ok = true, a.data = std::move(*data), a.table = std::move(*table);
            }
        }
        if (!a.ok) log_once(g, pic.archive + ": " + err);
        ait = g.archives.emplace(pic.archive, std::move(a)).first;
    }
    const Archive& a = ait->second;
    if (!a.ok) return out;
    const LesEntry* e = pic.name.empty() ? find_entry_id(a.table, pic.id) : find_entry(a.table, pic.name);
    std::string err;
    std::optional<Bytes> bytes;
    if (e) bytes = read_entry(a.data, *e, &err);
    std::optional<Image> img;
    if (bytes) img = decode_bmp(*bytes, &err);
    if (!img) {
        log_once(g, key + ": " + (e ? err : std::string("no such picture")));
        return out;
    }
    for (std::size_t i = 0; i + 3 < img->rgba.size(); i += 4) {  // the colour key of every interface BMP
        if (img->rgba[i] == 255 && img->rgba[i + 1] == 0 && img->rgba[i + 2] == 255) img->rgba[i + 3] = 0;
    }
    out.ok = true, out.w = img->width, out.h = img->height, out.rgba = std::move(img->rgba);
    return out;
}





SDL_Texture* texture(PanelGraphics& g, const PanelPic& pic, int frames, int frame, int w, int h, int k,
                     PictureFilter f) {
    if (w <= 0 || h <= 0) return nullptr;
    if (g.tex_scale != k || g.tex_filter != f) {
        g.clear_textures();
        g.tex_scale = k, g.tex_filter = f;
    }
    const TexKey key{pic_key(pic), frames, frame, w, h};
    auto it = g.textures.find(key);
    if (it != g.textures.end()) return it->second;
    SDL_Texture*& tex = g.textures[key];
    const Source& s = source(g, pic);
    if (!s.ok) return nullptr;
    const int fw = std::max(1, s.w / std::max(1, frames)), x0 = std::min(frame, std::max(1, frames) - 1) * fw;
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    const std::int64_t step_x = (std::int64_t{fw} << 16) / w, step_y = (std::int64_t{s.h} << 16) / h;
    for (int y = 0; y < h; ++y) {
        const int sy = std::min(s.h - 1, static_cast<int>((step_y / 2 + y * step_y) >> 16));
        for (int x = 0; x < w; ++x) {
            const int sx = x0 + std::min(fw - 1, static_cast<int>((step_x / 2 + x * step_x) >> 16));
            const std::size_t si = (static_cast<std::size_t>(sy) * s.w + sx) * 4;
            const std::size_t di = (static_cast<std::size_t>(y) * w + x) * 4;
            for (int c = 0; c < 4; ++c) px[di + c] = s.rgba[si + c];
        }
    }
    std::vector<std::uint8_t> scaled = scale_picture(px, w, h, k, f);
    SDL_Surface* surf = SDL_CreateSurfaceFrom(w * k, h * k, SDL_PIXELFORMAT_RGBA32, scaled.data(), w * k * 4);
    if (surf) tex = SDL_CreateTextureFromSurface(g.renderer, surf);
    SDL_DestroySurface(surf);
    if (tex) {
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    return tex;
}

void blit(SDL_Renderer* r, SDL_Texture* t, const IRect& d) {
    if (!t) return;
    const SDL_FRect f{static_cast<float>(d.x), static_cast<float>(d.y), static_cast<float>(d.w),
                      static_cast<float>(d.h)};
    SDL_RenderTexture(r, t, nullptr, &f);
}

void prepare_fonts(PanelGraphics& g, int k) {
    if (g.font_scale == k) return;
    for (TTF_Font** f : {&g.serif16, &g.serif18, &g.sans14, &g.sans10}) {
        if (*f) TTF_CloseFont(*f);
        *f = nullptr;
    }
    g.close_fit_fonts();
    g.serif16 = open_gdi_font(g.serif_path, 16 * k);
    g.serif18 = open_gdi_font(g.serif_path, 18 * k);
    g.sans14 = no_kerning(TTF_OpenFont(g.sans_path.c_str(), static_cast<float>(kSansEm * k)));
    g.sans_dy = g.sans14 ? kSansBaseline * k - TTF_GetFontAscent(g.sans14) : 0;
    g.sans10 = no_kerning(TTF_OpenFont(g.sans_path.c_str(), static_cast<float>(kSans10Em * k)));
    g.sans10_dy = g.sans10 ? kSans10Baseline * k - TTF_GetFontAscent(g.sans10) : 0;
    g.font_scale = k;
}

// Ours (docs/languages.md 2.5, the place panel.info): a Times 16 label wider than `fit_w` (interface pixels at scale
// 1) drawn smaller, down to em 11 of 13, else cut with an ellipsis; its baseline stays. English fits as it is.
std::string fit_label(PanelGraphics& g, const std::string& s, int fit_w, int k, TTF_Font** f, int* y) {
    int w = 0;
    if (fit_w <= 0 || !TTF_GetStringSize(*f, s.c_str(), 0, &w, nullptr) || w <= fit_w * k) return s;
    TextPlace p = text_place("panel.info");
    p.width = fit_w * k;
    p.em = static_cast<int>(TTF_GetFontSize(*f));
    p.min_em = p.em * 11 / 13;
    auto font_at = [&g, &p, f](int em) {
        if (em == p.em) return *f;
        auto [it, added] = g.fit_fonts.try_emplace(em, nullptr);
        if (added) it->second = no_kerning(TTF_OpenFont(g.serif_path.c_str(), static_cast<float>(em)));
        return it->second ? it->second : *f;
    };
    const TextFit r = text_fit(p, s, lang_short(g.lang ? *g.lang : lang_english(), s), [&](int em, std::string_view t) {
        int tw = 0;
        TTF_GetStringSize(font_at(em), t.data(), t.size(), &tw, nullptr);
        return tw;
    });
    TTF_Font* smaller = font_at(r.em);
    *y += TTF_GetFontAscent(*f) - TTF_GetFontAscent(smaller);
    *f = smaller;
    return r.lines[0];
}


void text(PanelGraphics& g, PanelFont font, const std::string& s, int x, int y, std::uint32_t rgb, bool shadow, int k,
          int fit_w = 0, const std::source_location& where = std::source_location::current()) {
    check_drawn(g.lang, s, where);
    TTF_Font* f = font == PanelFont::Serif18  ? g.serif18
                  : font == PanelFont::Sans14 ? g.sans14
                  : font == PanelFont::Sans10 ? g.sans10
                                              : g.serif16;
    if (!f || s.empty()) return;
    if (font == PanelFont::Sans14) y += g.sans_dy;
    if (font == PanelFont::Sans10) y += g.sans10_dy;
    const std::string shown = font == PanelFont::Serif16 ? fit_label(g, s, fit_w, k, &f, &y) : s;
    TTF_Text* t = TTF_CreateText(g.engine, f, shown.c_str(), 0);
    if (!t) return;
    if (shadow) {
        TTF_SetTextColor(t, 0, 0, 0, 255);
        TTF_DrawRendererText(t, static_cast<float>(x + 2 * k), static_cast<float>(y + k));
    }
    TTF_SetTextColor(t, static_cast<Uint8>(rgb >> 16), static_cast<Uint8>(rgb >> 8), static_cast<Uint8>(rgb), 255);
    TTF_DrawRendererText(t, static_cast<float>(x), static_cast<float>(y));
    TTF_DestroyText(t);
}

}  // namespace

PanelGraphicsPtr panel_graphics_open(SDL_Renderer* renderer, GameFiles files,
                                     const std::vector<std::string>& serif_fonts,
                                     const std::vector<std::string>& sans_fonts, std::string* error, Language* lang) {
    PanelGraphicsPtr g(std::make_unique<PanelGraphics>().release());
    g->renderer = renderer;
    g->lang = lang;
    g->files = std::move(files);
    if (!TTF_Init()) {
        *error = std::string("SDL_ttf: ") + SDL_GetError();
        return nullptr;
    }
    g->ttf = true;
    auto first = [](const std::vector<std::string>& paths, int cell, std::string* chosen) {
        for (const std::string& p : paths) {
            if (TTF_Font* f = open_gdi_font(p, cell)) {
                TTF_CloseFont(f);
                *chosen = p;
                return true;
            }
        }
        return false;
    };
    if (!first(serif_fonts, 16, &g->serif_path) || !first(sans_fonts, 14, &g->sans_path)) {
        *error =
            "no panel font could be opened (Times New Roman Bold or Liberation Serif Bold, Arial or Liberation Sans)";
        return nullptr;
    }
    g->engine = TTF_CreateRendererTextEngine(renderer);
    if (!g->engine) {
        *error = std::string("SDL_ttf text engine: ") + SDL_GetError();
        return nullptr;
    }
    return g;
}

void panel_draw(PanelGraphics& g, const Panel& p, const PanelInput& in, int w, int h, int k, PictureFilter filter) {
    SDL_Renderer* r = g.renderer;
    prepare_fonts(g, k);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    for (std::size_t i = 0; i < p.widgets.size(); ++i) {
        const PanelWidget& wd = p.widgets[i];
        const IRect d = widget_rect(p, wd, w, h, k);
        if (wd.kind == PanelWidget::Kind::Label) {
            text(g, wd.font, wd.text, d.x, d.y, wd.rgb, wd.shadow, k, wd.rect.w);
            continue;
        }
        const int state = wd.kind == PanelWidget::Kind::Button ? panel_state(p, in, static_cast<int>(i)) : wd.frame;
        if (!wd.pic.archive.empty()) blit(r, texture(g, wd.pic, wd.frames, state, wd.rect.w, wd.rect.h, k, filter), d);
        if (wd.kind != PanelWidget::Kind::Button) continue;
        if (wd.disabled) {  // shadowforbutton.bmp (40 x 40) over it
            blit(r, texture(g, pic_named(kInterfaceLes, "shadowforbutton.bmp"), 1, 0, wd.rect.w, wd.rect.h, k, filter),
                 d);
        }
        if (wd.active) {  // the 2 px pen of Rectangle(x - 1, y - 1, x + w + 1, y + h + 1): the ring just outside
            SDL_SetRenderDrawColor(r, static_cast<Uint8>(kPanelRed >> 16), static_cast<Uint8>(kPanelRed >> 8),
                                   static_cast<Uint8>(kPanelRed), 255);
            const float t = static_cast<float>(2 * k);
            const float x0 = static_cast<float>(d.x) - t, y0 = static_cast<float>(d.y) - t;
            const float ww = static_cast<float>(d.w) + 2 * t, hh = static_cast<float>(d.h) + 2 * t;
            const SDL_FRect ring[4] = {
                {x0, y0, ww, t}, {x0, y0 + hh - t, ww, t}, {x0, y0, t, hh}, {x0 + ww - t, y0, t, hh}};
            SDL_RenderFillRects(r, ring, 4);
        }

        const int push = state == 1 ? k : 0;
        text(g, wd.font, wd.text, d.x + push, d.y + push, wd.rgb, wd.shadow, k);
    }
}

}  // namespace antanta::ui
