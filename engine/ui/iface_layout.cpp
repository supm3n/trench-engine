#include "ui/iface_layout.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace antanta::ui {

int auto_ui_scale(int w, int h) {
    const double fit = std::min(static_cast<double>(w) / 1440.0, static_cast<double>(h) / 900.0);
    return std::max(1, static_cast<int>(std::floor(fit + 0.5)));
}

int ui_scale_for(std::uint8_t setting, int w, int h) {
    return setting >= 1 && setting <= 4 ? setting : auto_ui_scale(w, h);
}

int world_zoom_for(std::uint8_t setting, std::uint8_t ui_scale_setting, int w, int h) {
    return setting >= 1 && setting <= 4 ? setting : ui_scale_for(ui_scale_setting, w, h);
}

namespace {

// One axis of the scaling: output pixel o of n * k takes source pixel a with weight 1 - t and pixel b with weight t.
struct Tap {
    int a = 0, b = 0;
    float t = 0;
};

Tap tap(int o, int n, int k, PictureFilter f) {
    const double c = o + 0.5;  // the output pixel's centre in output pixels
    Tap r;
    if (k == 1 || f == PictureFilter::Crisp) {
        r.a = r.b = std::min(o / k, n - 1);
        return r;
    }
    if (f == PictureFilter::Smooth) {  // bilinear between the source pixel centres
        const double u = c / k - 0.5;
        const int i = static_cast<int>(std::floor(u));
        r.a = std::clamp(i, 0, n - 1), r.b = std::clamp(i + 1, 0, n - 1);
        r.t = static_cast<float>(u - i);
        return r;
    }
    // In between: the nearest source pixel, mixed with its neighbour by 0.5 - d / 2 where d is the distance in
    // output pixels to the edge between them (a quarter for the pixel next to the edge, nothing from 1 pixel on).
    const int i = o / k;
    const double dl = c - i * k, dr = (i + 1) * k - c;
    const int nb = dl < dr ? i - 1 : i + 1;
    const double d = std::min(dl, dr);
    r.a = i;
    r.b = std::clamp(nb, 0, n - 1);
    r.t = static_cast<float>(std::max(0.0, 0.5 - d / 2));
    return r;
}

}  // namespace

std::vector<std::uint8_t> scale_picture(const std::vector<std::uint8_t>& rgba, int w, int h, int k, PictureFilter f) {
    if (k <= 1) return rgba;
    const int ow = w * k, oh = h * k;
    // Premultiplied, so a keyed (transparent) pixel gives no colour to its neighbours.
    std::vector<float> pre(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < pre.size(); i += 4) {
        const float a = rgba[i + 3] / 255.0f;
        for (int ch = 0; ch < 3; ++ch) pre[i + ch] = rgba[i + ch] / 255.0f * a;
        pre[i + 3] = a;
    }
    std::vector<Tap> xs(static_cast<std::size_t>(ow)), ys(static_cast<std::size_t>(oh));
    for (int x = 0; x < ow; ++x) xs[x] = tap(x, w, k, f);
    for (int y = 0; y < oh; ++y) ys[y] = tap(y, h, k, f);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(ow) * oh * 4);
    for (int y = 0; y < oh; ++y) {
        const Tap& ty = ys[y];
        for (int x = 0; x < ow; ++x) {
            const Tap& tx = xs[x];
            float v[4];
            for (int ch = 0; ch < 4; ++ch) {
                auto at = [&](int sx, int sy) { return pre[(static_cast<std::size_t>(sy) * w + sx) * 4 + ch]; };
                const float top = at(tx.a, ty.a) * (1 - tx.t) + at(tx.b, ty.a) * tx.t;
                const float bot = at(tx.a, ty.b) * (1 - tx.t) + at(tx.b, ty.b) * tx.t;
                v[ch] = top * (1 - ty.t) + bot * ty.t;
            }
            std::uint8_t* o = &out[(static_cast<std::size_t>(y) * ow + x) * 4];
            const float a = v[3];
            for (int ch = 0; ch < 3; ++ch) {
                const float c = a > 0 ? v[ch] / a : 0.0f;
                o[ch] = static_cast<std::uint8_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            o[3] = static_cast<std::uint8_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
    return out;
}

HudLayout hud_layout(int w, int h, int k) {
    HudLayout l;
    l.width = w, l.height = h, l.scale = k;
    l.top = {0, 0, w, 23 * k};
    l.bottom = {0, h - 15 * k, w, 15 * k};
    l.left = {0, 0, 34 * k, h};
    l.right = {w - 34 * k, 0, 34 * k, h};
    l.thermometer = {w - 32 * k, 23 * k, 19 * k, 214 * k};
    l.fill_x = w - 24 * k, l.fill_w = 3 * k;

    constexpr int kBase[6] = {109, 242, 384, 675, 807, 945};
    for (int i = 0; i < 6; ++i) {
        l.number_x[i] = static_cast<int>(static_cast<double>(kBase[i]) * w / 1024.0);
    }
    l.number_y = 5 * k;
    l.shadow_dx = 2 * k, l.shadow_dy = k;
    l.build_x = 30 * k, l.build_y = 22 * k;
    l.version_x = 885 * k, l.version_y = 20 * k;
    return l;
}

ThermometerFill thermometer_fill(float power_made, float power_used, float factor_6c4, float factor_6e4) {

    double v = 2.0 * (static_cast<double>(factor_6e4) * power_made - static_cast<double>(power_used) / factor_6c4);
    if (std::isnan(v)) v = 0;  // a factor of 0 (never seen) would divide 0 by 0
    v = std::clamp(v, -78.0, 78.0);
    ThermometerFill f;
    f.top = static_cast<int>(191.0 - v - 78.0 - 1.0);
    f.bottom = 191;

    const int d = static_cast<int>(v * 3.2692);
    f.r = static_cast<std::uint8_t>(v >= 0 ? 255 - d : 255);
    f.g = static_cast<std::uint8_t>(v >= 0 ? 255 : 255 + d);
    f.b = 0;
    return f;
}

std::string resource_text(double value) {
    if (std::isnan(value)) return "0";
    const double t = std::clamp(std::trunc(value), -9.0e18, 9.0e18);
    return std::to_string(static_cast<long long>(t));
}

bool version_visible(bool units_selected, bool one_house_selected) { return !units_selected && !one_house_selected; }

namespace {

bool is_break(char c) { return c == ' ' || c == ',' || c == '.' || c == ':'; }

}  // namespace

void screen_text_add(ScreenTexts& list, int x, int y, const std::string& text, int frames, std::uint32_t rgb, bool up,
                     int w, int h, bool minimap_shown, const TextWidth& width) {
    if (text.empty() || list.entries.size() >= kMaxScreenTexts || y >= h) return;
    // Too wide for the screen less the minimap: broken at the last space, comma, full stop or colon that fits, the
    // rest goes 15 pixels lower as an entry of its own.
    const int limit = w - (minimap_shown ? 200 : 0) - 70;
    if (width(text) > limit) {
        std::size_t fit = 0;
        while (fit < text.size() && width(text.substr(0, fit + 1)) <= limit) ++fit;
        std::size_t cut = std::string::npos;
        for (std::size_t i = fit; i-- > 0;) {
            if (is_break(text[i])) {
                cut = i;
                break;
            }
        }
        if (cut != std::string::npos && cut > 0) {
            const std::string first = text.substr(0, text[cut] == ' ' ? cut : cut + 1);
            std::size_t rest_at = cut + 1;
            while (rest_at < text.size() && text[rest_at] == ' ') ++rest_at;
            screen_text_add(list, x, y, first, frames, rgb, up, w, h, minimap_shown, width);
            if (rest_at < text.size()) {
                screen_text_add(list, x, y + 15, text.substr(rest_at), frames, rgb, up, w, h, minimap_shown, width);
            }
            return;
        }
    }
    for (const ScreenText& e : list.entries) {
        if (e.text == text) return;  // the same text is on the screen already
    }
    // A live entry within 10 pixels: move 15 pixels (down, or up with `up`) while inside 0 < y < H - 220, else the
    // old entry makes room.
    for (;;) {
        const auto near = std::find_if(list.entries.begin(), list.entries.end(),
                                       [&](const ScreenText& e) { return std::abs(e.y - y) < 10; });
        if (near == list.entries.end()) break;
        const int moved = y + (up ? -15 : 15);
        if (moved > 0 && moved < h - 220) {
            y = moved;
            continue;
        }
        list.entries.erase(near);
    }
    list.entries.push_back(ScreenText{text, x, y, rgb, frames});
}

void screen_text_event(ScreenTexts& list, const std::string& text, std::uint32_t rgb, int w, int h, bool minimap_shown,
                       const TextWidth& width) {
    screen_text_add(list, 30, h - 280, text, 60, rgb, false, w, h, minimap_shown, width);
}

void screen_texts_frame(ScreenTexts& list) {
    for (ScreenText& e : list.entries) --e.frames;
    std::erase_if(list.entries, [](const ScreenText& e) { return e.frames <= 0; });
}

ViewCells view_cells(int w, int h) { return ViewCells{(w + 95) / 96, (h + 23) / 24 + 2}; }

ViewPos clamp_camera(const MapData& map, int w, int h, ViewPos p) {
    const ViewCells c = view_cells(w, h);
    p.col = std::clamp(p.col, 0, std::max(0, map.width - c.cols - 2));
    p.row = std::clamp(p.row, 0, std::max(0, map.height - c.rows - 10));
    return p;
}

}  // namespace antanta::ui
