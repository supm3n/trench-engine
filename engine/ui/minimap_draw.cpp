#include "ui/minimap_draw.hpp"

#include <algorithm>

#include "formats/image.hpp"
#include "formats/les.hpp"
#include "formats/sprites.hpp"

namespace antanta::ui {

namespace {

// One picture: the decoded RGBA (magenta transparent) and its texture at the current scale and filter.
struct Pic {
    int w = 0, h = 0;
    std::vector<std::uint8_t> rgba;
    SDL_Texture* tex = nullptr;
};

// The button pictures of slice I3 are cut into their states when loaded (a strip scaled whole would soften the edge
// between two states): the objective's 33 x 29 piece, the advisors' off and on, Help and Play menu's three states (each
// 26 x 23).
enum PicId {
    kPlate,
    kLeft,
    kArrow,
    kObjective,
    kEconomic,  // + 0 off, + 1 on
    kMilitary = kEconomic + 2,
    kHelp = kMilitary + 2,  // + 0 normal, + 1 pressed, + 2 hover
    kOption = kHelp + 3,
    kBattle0 = kOption + 3,
    kPicCount = kBattle0 + kBattleFrames
};

}  // namespace

struct MinimapGraphics {
    SDL_Renderer* renderer = nullptr;
    std::array<Pic, kPicCount> pics;
    int pic_scale = 0;
    PictureFilter pic_filter = PictureFilter::Between;
    SDL_Texture* static_tex = nullptr;
    std::uint64_t static_version = 0;
    SDL_Texture* fog_tex = nullptr;
    std::uint64_t fog_version = 0;
    int tex_size = 0;
    // The unit dots as a layer (ours): drawn into it when the app's list changes (once a tick), so a frame draws one
    // picture instead of tens of thousands of squares on a huge battle. size + k - 1 square: a dot at the last pixel
    // reaches k - 1 pixels beyond the minimap, as the squares did.
    SDL_Texture* dots_tex = nullptr;
    std::uint64_t dots_version = 0;
    int dots_size = 0;
    ~MinimapGraphics() {
        for (Pic& p : pics) SDL_DestroyTexture(p.tex);
        SDL_DestroyTexture(static_tex);
        SDL_DestroyTexture(fog_tex);
        SDL_DestroyTexture(dots_tex);
    }
};

void MinimapGraphicsFree::operator()(MinimapGraphics* g) const { std::unique_ptr<MinimapGraphics> owned(g); }

namespace {

std::optional<Bytes> entry(const GameFiles& files, const std::string& archive, const std::string& name,
                           std::string* error) {
    auto data = files(archive, error);
    if (!data) return std::nullopt;
    auto table = parse_les(*data, error);
    if (!table) {
        *error = archive + ": " + *error;
        return std::nullopt;
    }
    const LesEntry* e = find_entry(*table, name);
    if (!e) {
        *error = archive + ": no entry " + name;
        return std::nullopt;
    }
    return read_entry(*data, *e, error);
}

bool load_bmp(const GameFiles& files, const std::string& archive, const std::string& name, Pic* out,
              std::string* error) {
    auto bytes = entry(files, archive, name, error);
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

bool load_444(const GameFiles& files, const std::string& archive, const std::string& name, Pic* out,
              std::string* error) {
    auto bytes = entry(files, archive, name, error);
    if (!bytes) return false;
    auto img = decode_444(*bytes, error);
    if (!img) {
        *error = archive + "/" + name + ": " + *error;
        return false;
    }

    for (std::size_t i = 0; i + 3 < img->rgba.size(); i += 4) {
        std::uint32_t rgb = 0;
        for (std::size_t c = 0; c < 3; ++c) rgb = rgb << 8 | (img->rgba[i + c] * 229u + 127u) / 255u;
        rgb = rgb565(rgb);
        img->rgba[i] = static_cast<std::uint8_t>(rgb >> 16), img->rgba[i + 1] = static_cast<std::uint8_t>(rgb >> 8);
        img->rgba[i + 2] = static_cast<std::uint8_t>(rgb);
    }
    out->w = img->width, out->h = img->height;
    out->rgba = std::move(img->rgba);
    return true;
}

// The part x, y, w, h of a picture as a picture of its own.
Pic crop(const Pic& p, int x, int y, int w, int h) {
    Pic out;
    out.w = w, out.h = h;
    out.rgba.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            for (int c = 0; c < 4; ++c) {
                out.rgba[(static_cast<std::size_t>(j) * w + i) * 4 + c] =
                    p.rgba[(static_cast<std::size_t>(j + y) * p.w + (i + x)) * 4 + c];
            }
        }
    }
    return out;
}

void prepare_pictures(MinimapGraphics& g, int k, PictureFilter f) {
    if (g.pic_scale == k && g.pic_filter == f) return;
    for (Pic& p : g.pics) {
        SDL_DestroyTexture(p.tex);
        p.tex = nullptr;
        std::vector<std::uint8_t> px = scale_picture(p.rgba, p.w, p.h, k, f);
        SDL_Surface* s = SDL_CreateSurfaceFrom(p.w * k, p.h * k, SDL_PIXELFORMAT_RGBA32, px.data(), p.w * k * 4);
        if (s) p.tex = SDL_CreateTextureFromSurface(g.renderer, s);
        SDL_DestroySurface(s);
        if (!p.tex) continue;
        SDL_SetTextureScaleMode(p.tex, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(p.tex, SDL_BLENDMODE_BLEND);
    }
    g.pic_scale = k, g.pic_filter = f;
}

// The whole picture, or its source rect (in the picture's own pixels) at k times its size, at (x, y).
void blit(SDL_Renderer* r, const Pic& p, int k, int x, int y, const SDL_FRect* src = nullptr) {
    if (!p.tex) return;
    SDL_FRect s{0, 0, static_cast<float>(p.w * k), static_cast<float>(p.h * k)};
    if (src) s = {src->x * k, src->y * k, src->w * k, src->h * k};
    const SDL_FRect d{static_cast<float>(x), static_cast<float>(y), s.w, s.h};
    SDL_RenderTexture(r, p.tex, &s, &d);
}

// The static layer and the fog layer as textures of size x size (made again when the size changes).
void prepare_layers(MinimapGraphics& g, const MinimapFrame& f, int size) {
    SDL_Renderer* r = g.renderer;
    if (g.tex_size != size) {
        SDL_DestroyTexture(g.static_tex);
        SDL_DestroyTexture(g.fog_tex);
        g.static_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, size, size);
        g.fog_tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, size, size);
        if (g.static_tex) SDL_SetTextureScaleMode(g.static_tex, SDL_SCALEMODE_NEAREST);
        if (g.fog_tex) {
            SDL_SetTextureScaleMode(g.fog_tex, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(g.fog_tex, SDL_BLENDMODE_BLEND);
        }
        g.tex_size = size, g.static_version = g.fog_version = ~std::uint64_t{0};
    }
    const std::size_t n = static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
    if (g.static_tex && f.static_rgb && f.static_rgb->size() == n && g.static_version != f.static_version) {
        SDL_UpdateTexture(g.static_tex, nullptr, f.static_rgb->data(), size * 4);
        g.static_version = f.static_version;
    }
    if (g.fog_tex && f.fog && f.fog->size() == n && g.fog_version != f.fog_version) {

        std::vector<std::uint32_t> px(n);
        for (std::size_t i = 0; i < n; ++i) px[i] = (*f.fog)[i] ? 0x00000000u : 0xff000000u;
        SDL_UpdateTexture(g.fog_tex, nullptr, px.data(), size * 4);
        g.fog_version = f.fog_version;
    }
}

// In list order, as one call per run of dots of one colour (tens of thousands of units: one call each cost several
// ms a frame, docs/bugs.md B59); a dot equal to the one before it adds nothing and is left out.
void dots(SDL_Renderer* r, const MinimapLayout& l, const std::vector<MinimapDot>& list) {
    std::vector<SDL_FRect> run;
    std::uint32_t rgb = 0;
    auto flush = [&] {
        if (run.empty()) return;
        SDL_SetRenderDrawColor(r, static_cast<Uint8>(rgb >> 16), static_cast<Uint8>(rgb >> 8), static_cast<Uint8>(rgb),
                               255);
        SDL_RenderFillRects(r, run.data(), static_cast<int>(run.size()));
        run.clear();
    };
    const MinimapDot* last = nullptr;
    for (const MinimapDot& d : list) {
        if (last && last->x == d.x && last->y == d.y && last->size == d.size && last->rgb == d.rgb) continue;
        last = &d;
        if (d.rgb != rgb) flush(), rgb = d.rgb;
        run.push_back({static_cast<float>(l.x + d.x), static_cast<float>(l.y + d.y), static_cast<float>(d.size),
                       static_cast<float>(d.size)});
    }
    flush();
}

// The dots of `list` into the layer (list order, a later dot over an earlier one, as the squares are drawn), the rest
// transparent; made again when the version or the size changes. False when there is no layer (then the squares).
bool prepare_dots(MinimapGraphics& g, const std::vector<MinimapDot>& list, std::uint64_t version, int size, int k) {
    const int n = size + std::max(1, k) - 1;
    if (g.dots_size != n) {
        SDL_DestroyTexture(g.dots_tex);
        g.dots_tex = SDL_CreateTexture(g.renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, n, n);
        if (g.dots_tex) {
            SDL_SetTextureScaleMode(g.dots_tex, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(g.dots_tex, SDL_BLENDMODE_BLEND);
        }
        g.dots_size = n, g.dots_version = ~std::uint64_t{0};
    }
    if (!g.dots_tex) return false;
    if (g.dots_version == version) return true;
    std::vector<std::uint32_t> px(static_cast<std::size_t>(n) * static_cast<std::size_t>(n), 0);
    for (const MinimapDot& d : list) {
        const std::uint32_t c = 0xff000000u | (d.rgb & 0xffffffu);
        for (int y = std::max(0, d.y); y < std::min(n, d.y + d.size); ++y) {
            for (int x = std::max(0, d.x); x < std::min(n, d.x + d.size); ++x) {
                px[static_cast<std::size_t>(y) * static_cast<std::size_t>(n) + static_cast<std::size_t>(x)] = c;
            }
        }
    }
    SDL_UpdateTexture(g.dots_tex, nullptr, px.data(), n * 4);
    g.dots_version = version;
    return true;
}

}  // namespace

MinimapGraphicsPtr minimap_graphics_open(SDL_Renderer* renderer, GameFiles files, std::string* error) {
    MinimapGraphicsPtr g(std::make_unique<MinimapGraphics>().release());
    g->renderer = renderer;
    const std::string iface = "Data/Interface/interface.les", group = "Data/Interface/InterfaceGroup.les";
    if (!load_bmp(files, iface, "MiniMapBevelUp.bmp", &g->pics[kPlate], error) ||
        !load_bmp(files, iface, "MiniMapBevelleft.bmp", &g->pics[kLeft], error) ||
        !load_bmp(files, group, "minimaparrow.bmp", &g->pics[kArrow], error)) {
        return nullptr;
    }
    {
       // of three 26 x 23 states)
        Pic whole;
        if (!load_bmp(files, group, "showzadanie.bmp", &whole, error)) return nullptr;
        g->pics[kObjective] = crop(whole, 0, 0, 33, 29);
        const struct {
            const char* name;
            int first, count;
        } strips[] = {{"Econom+.bmp", kEconomic, 2},
                      {"War+.bmp", kMilitary, 2},
                      {"Help.bmp", kHelp, 3},
                      {"Option.bmp", kOption, 3}};
        for (const auto& st : strips) {
            if (!load_bmp(files, iface, st.name, &whole, error)) return nullptr;
            if (whole.w < 26 * st.count || whole.h < 23) {
                *error = std::string(st.name) + ": unexpected size";
                return nullptr;
            }
            for (int i = 0; i < st.count; ++i)
                g->pics[static_cast<std::size_t>(st.first + i)] = crop(whole, 26 * i, 0, 26, 23);
        }
    }
    for (int i = 0; i < kBattleFrames; ++i) {
        if (!load_444(files, "Data/Misc/MiniMapIndicators/Battle.les", std::to_string(i) + ".444",
                      &g->pics[kBattle0 + i], error)) {
            return nullptr;
        }
    }
    return g;
}

void minimap_draw(MinimapGraphics& g, const MinimapLayout& l, PictureFilter filter, const MinimapFrame& f) {
    SDL_Renderer* r = g.renderer;
    const int k = l.scale;
    prepare_pictures(g, k, filter);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
    if (!l.shown) {  // 1. hidden: only the plate
        blit(r, g.pics[kPlate], k, l.plate.x, l.plate.y);
        return;
    }
    // 2. The static layer, the left bevel, the top bevel.
    prepare_layers(g, f, l.size);
    const SDL_FRect mm{static_cast<float>(l.x), static_cast<float>(l.y), static_cast<float>(l.size),
                       static_cast<float>(l.size)};
    if (g.static_tex) SDL_RenderTexture(r, g.static_tex, nullptr, &mm);
    blit(r, g.pics[kLeft], k, l.left.x, l.left.y);
    blit(r, g.pics[kPlate], k, l.plate.x, l.plate.y);
    // 3. The units: one pixel each (k x k here) in the owner's colour, selected ones in the selection colour.
    if (f.dots_version != 0 && prepare_dots(g, f.dots, f.dots_version, l.size, k)) {
        const SDL_FRect at{static_cast<float>(l.x), static_cast<float>(l.y), static_cast<float>(g.dots_size),
                           static_cast<float>(g.dots_size)};
        SDL_RenderTexture(r, g.dots_tex, nullptr, &at);
    } else {
        dots(r, l, f.dots);
    }
    // 4. The fog (fog slice).
    if (f.fog && g.fog_tex) SDL_RenderTexture(r, g.fog_tex, nullptr, &mm);
    // 5. The selected house of yours: its footprint and the blinking rally cross.
    dots(r, l, f.house_dots);
    // 6. The view rectangle: a GDI pen of width 0 (one pixel; k here), pure red.
    SDL_SetRenderDrawColor(r, 255, 0, 0, 255);
    const float x0 = static_cast<float>(f.view[0]), y0 = static_cast<float>(f.view[1]);
    const float x1 = static_cast<float>(f.view[2]), y1 = static_cast<float>(f.view[3]);
    const float t = static_cast<float>(k);
    // MoveTo / LineTo around (x0, y0), (x1, y1): both ends drawn, so x0 .. x1 and y0 .. y1 inclusive.
    const float w = x1 - x0 + t, hh = y1 - y0 + t;
    const SDL_FRect sides[4] = {{x0, y0, w, t}, {x0, y1, w, t}, {x0, y0, t, hh}, {x1, y0, t, hh}};
    if (x1 >= x0 && y1 >= y0) SDL_RenderFillRects(r, sides, 4);
    // 7. The battle marks over everything (the fog too).
    for (const MinimapMarkDraw& m : f.marks) {
        if (m.frame < 0 || m.frame >= kBattleFrames) continue;
        blit(r, g.pics[kBattle0 + m.frame], k, l.x + m.x, l.y + m.y);
    }
}

void minimap_draw_buttons(MinimapGraphics& g, const MinimapLayout& l, PictureFilter filter, const HudButtonsState& st) {
    prepare_pictures(g, l.scale, filter);
    for (const HudButtonDraw& d : hud_buttons_draw(l, st)) {
        const int k = l.scale;
        switch (d.button) {
            case HudButton::Arrow: {
                const SDL_FRect src{d.frame == 0 ? 0.0f : 33.0f, 0, 33, 30};  // minimaparrow.bmp: down arrow, up arrow
                blit(g.renderer, g.pics[kArrow], k, d.dst.x, d.dst.y, &src);
                break;
            }
            case HudButton::Objective: blit(g.renderer, g.pics[kObjective], k, d.dst.x, d.dst.y); break;
            default: {
                const int first = d.button == HudButton::Economic   ? kEconomic
                                  : d.button == HudButton::Military ? kMilitary
                                  : d.button == HudButton::Help     ? kHelp
                                                                    : kOption;
                blit(g.renderer, g.pics[static_cast<std::size_t>(first + d.frame)], k, d.dst.x, d.dst.y);
            }
        }
    }
}

}  // namespace antanta::ui
