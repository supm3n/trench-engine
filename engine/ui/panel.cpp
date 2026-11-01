#include "ui/panel.hpp"

namespace antanta::ui {

PanelPic pic_named(const char* archive, const std::string& name) { return PanelPic{archive, name, 0}; }
PanelPic pic_id(const char* archive, std::uint16_t id) { return PanelPic{archive, {}, id}; }

PanelPic house_icon(std::uint16_t type) {
    return type == 3 ? pic_named(kInterfaceLes, "germany_stroy_acad.bmp") : pic_id(kHouseIconsLes, type);
}

IRect panel_rect(const Panel& p, int /*w*/, int h, int k) { return IRect{p.x * k, h - p.bottom * k, p.w * k, p.h * k}; }

IRect widget_rect(const Panel& p, const PanelWidget& wd, int w, int h, int k) {
    const IRect r = panel_rect(p, w, h, k);
    return IRect{r.x + wd.rect.x * k, r.y + wd.rect.y * k, wd.rect.w * k, wd.rect.h * k};
}

namespace {

bool inside(const IRect& r, int x, int y) { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

}  // namespace

int panel_hit(const Panel& p, int w, int h, int k, int x, int y) {
    // The window hands the event to the topmost child under the pointer: the last one added wins where they overlap
    // (the type icons of a crowded selection overlap, B.4).
    for (int i = static_cast<int>(p.widgets.size()) - 1; i >= 0; --i) {
        const PanelWidget& wd = p.widgets[static_cast<std::size_t>(i)];
        if (wd.kind == PanelWidget::Kind::Button && inside(widget_rect(p, wd, w, h, k), x, y)) return i;
    }
    return -1;
}

bool panel_contains(const Panel& p, int w, int h, int k, int x, int y) { return inside(panel_rect(p, w, h, k), x, y); }

bool panel_press(const Panel& p, PanelInput& in, int w, int h, int k, int x, int y, bool right) {
    if (!panel_contains(p, w, h, k, x, y)) return false;
    in.pressed = panel_hit(p, w, h, k, x, y);
    in.pressed_right = right;
    return true;
}

std::optional<PanelClick> panel_release(const Panel& p, PanelInput& in, int w, int h, int k, int x, int y, bool right) {
    const int down = in.pressed;
    const bool was_right = in.pressed_right;
    in.pressed = -1;
    if (down < 0 || was_right != right || panel_hit(p, w, h, k, x, y) != down) return std::nullopt;
    const PanelWidget& wd = p.widgets[static_cast<std::size_t>(down)];


    if (wd.disabled && !right) return std::nullopt;
    const int action = right ? wd.right_action : wd.action;
    if (action == 0) return std::nullopt;
    return PanelClick{action, right ? wd.right_arg : wd.arg};
}

std::optional<PanelClick> panel_key(const Panel& p, char key) {
    if (key == 0) return std::nullopt;
    for (const PanelWidget& wd : p.widgets) {
        if (wd.kind == PanelWidget::Kind::Button && !wd.disabled && wd.key == key && wd.action != 0) {
            return PanelClick{wd.action, wd.arg};
        }
    }
    return std::nullopt;
}

int panel_state(const Panel& p, const PanelInput& in, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= p.widgets.size()) return 0;
    if (p.widgets[static_cast<std::size_t>(index)].disabled) return 0;
    if (in.pressed == index && in.hover == index) return 1;
    return in.hover == index && in.pressed < 0 ? 2 : 0;
}

std::vector<TooltipLine> panel_tooltip(const Panel& p, const PanelInput& in, int h, int k) {
    if (in.hover < 0 || static_cast<std::size_t>(in.hover) >= p.widgets.size()) return {};
    const std::string& t = p.widgets[static_cast<std::size_t>(in.hover)].tooltip;
    if (t.empty()) return {};
    const int top = (h - p.bottom * k) / k;  // the panel's top in interface pixels
    const int y = top - p.tooltip_above;
    std::size_t letters = 0, at_128 = t.size();  // the byte where letter 128 starts
    for (std::size_t i = 0; i < t.size(); ++i) {
        if ((static_cast<unsigned char>(t[i]) & 0xc0) == 0x80) continue;
        if (letters++ == 128) at_128 = i;
    }
    if (letters > 128) {
        const std::size_t cut = t.rfind(' ', at_128);
        if (cut != std::string::npos && cut > 0) {
            return {{t.substr(0, cut), p.tooltip_x, y - 13}, {t.substr(cut + 1), p.tooltip_x, y}};
        }
    }
    return {{t, p.tooltip_x, y}};
}

}  // namespace antanta::ui
