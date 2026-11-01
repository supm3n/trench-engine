// Map viewer: the original's isometric view of one map (render/iso), or the flat top-down overview (render/topdown).
// Desktop: mapviewer <map name> [--game <game folder>] [--at <col> <row>] [--screenshot out.png] [--topdown]
//                    [--bench <frames>]
//          The game folder defaults to the ANTANTA_GAME_DIR environment variable. --at is the scroll position
//          (1 col = 96 px, 1 row = 24 px). --screenshot writes the first frame and quits. --bench scrolls through
//          <frames> frames (a static redraw every frame), logs the timings and quits.
// Web:     the page writes the picked game folder's files into /game/ (lower-case paths) and the name of the map
//          to show into /picked/ready; the viewer polls for it each frame.
// Controls: arrow keys/WASD scroll in the game's steps (1 column, 2 rows); T toggles the top-down view
//           (drag, wheel or +/- to zoom, Home to fit there); Esc quits.
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

#include "formats/map.hpp"
#include "platform/files.hpp"
#include "render/iso.hpp"
#include "render/topdown.hpp"

using namespace antanta;

namespace {

struct App {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::string game_dir;
    bool lower_case_paths = false;  // web: the page stores every path in lower case
    // isometric view
    std::optional<WorldGraphics> graphics;
    std::optional<MapData> map;
    WorldView view;
    ViewPos pos;
    bool static_dirty = true;
    Target static_layer, work;
    std::vector<std::uint8_t> frame;
    SDL_Texture* iso = nullptr;
    // top-down view
    bool top_down = false;
    SDL_Texture* overview = nullptr;
    float map_w = 0, map_h = 0;
    float zoom = 1, cam_x = 0, cam_y = 0;  // cam: map pixel at the window's top-left corner
    bool dragging = false;
    // batch modes
    std::string screenshot_path;
    int bench_frames = 0;
    int frames = 0;
    bool report_frame = true;  // log the crc of the next isometric frame (after a map load)
    Uint64 bench_static_ns = 0, bench_frame_ns = 0, bench_start = 0;
};

constexpr const char* kPickDir = "/picked/";

GameFileLoader loader(const App& app) {
    return [&app](const std::string& path, std::string* error) {
        std::string p = path;
        if (app.lower_case_paths) {
            for (char& c : p) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
        }
        return load_file(app.game_dir + "/" + p, error);
    };
}

void fit(App& app) {
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(app.renderer, &w, &h);
    if (app.map_w <= 0 || app.map_h <= 0) return;
    app.zoom = std::min(float(w) / app.map_w, float(h) / app.map_h);
    app.cam_x = (app.map_w - float(w) / app.zoom) / 2;
    app.cam_y = (app.map_h - float(h) / app.zoom) / 2;
}

void zoom_at(App& app, float factor, float sx, float sy) {
    const float mx = app.cam_x + sx / app.zoom, my = app.cam_y + sy / app.zoom;
    app.zoom = std::clamp(app.zoom * factor, 0.1f, 32.0f);
    app.cam_x = mx - sx / app.zoom;
    app.cam_y = my - sy / app.zoom;
}

bool load_map(App& app, const std::string& map_file, std::string* error) {
    const GameFileLoader load = loader(app);
    auto file = load("Maps/" + map_file, error);
    if (!file) return false;
    auto map = parse_map(*file, error);
    if (!map) {
        *error = map_file + ": " + *error;
        return false;
    }
    if (!app.graphics) {
        const Uint64 t = SDL_GetTicksNS();
        app.graphics = load_world_graphics(load, error);
        if (!app.graphics) return false;
        SDL_Log("game graphics loaded in %.0f ms", double(SDL_GetTicksNS() - t) / 1e6);
    }
    app.map = std::move(*map);
    app.view = world_view_from_map(*app.map);
    std::vector<std::string> warnings;
    load_view_graphics(*app.graphics, app.view, load, &warnings);
    for (const std::string& w : warnings) SDL_Log("warning: %s", w.c_str());
    app.pos = clamp_view(*app.map, app.pos);
    app.static_dirty = true;

    std::vector<Rgb> colours;
    std::string land_error;
    if (auto land_file = load("Data/LandTextures/LandTextures.dat", &land_error)) {
        if (auto land = parse_land_textures(*land_file, &land_error)) colours = tile_colours(*land);
    }
    const Image img = render_top_down(*app.map, colours);
    SDL_Texture* tex =
        SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.width, img.height);
    if (!tex || !SDL_UpdateTexture(tex, nullptr, img.rgba.data(), img.width * 4)) {
        *error = std::string("cannot create the map texture: ") + SDL_GetError();
        SDL_DestroyTexture(tex);
        return false;
    }
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
    SDL_DestroyTexture(app.overview);
    app.overview = tex;
    app.map_w = float(img.width);
    app.map_h = float(img.height);
    fit(app);
    SDL_Log("%s: %dx%d cells, %zu houses, %zu units, view at %d %d", map_file.c_str(), app.map->width, app.map->height,
            app.map->houses.size(), app.map->units.size(), app.pos.col, app.pos.row);
    return true;
}

void scroll(App& app, int dc, int dr) {
    if (!app.map) return;
    const ViewPos p = clamp_view(*app.map, {app.pos.col + dc, app.pos.row + dr});
    if (p.col != app.pos.col || p.row != app.pos.row) {
        app.pos = p;
        app.static_dirty = true;
    }
}

// Composites one isometric frame on the CPU and uploads it.
void draw_iso(App& app) {
    const Uint64 t0 = SDL_GetTicksNS();
    if (app.static_dirty) {
        render_static_layer(*app.graphics, app.view, app.pos, &app.static_layer);
        app.static_dirty = false;
    }
    const Uint64 t1 = SDL_GetTicksNS();
    render_frame(*app.graphics, app.view, app.pos, app.static_layer, &app.work, &app.frame);
    SDL_UpdateTexture(app.iso, nullptr, app.frame.data(), kViewWidth * 4);
    const Uint64 t2 = SDL_GetTicksNS();
    app.bench_static_ns += t1 - t0;
    app.bench_frame_ns += t2 - t1;
    SDL_RenderTexture(app.renderer, app.iso, nullptr, nullptr);
}

bool save_png(SDL_Surface* s, const std::string& path) {
    const bool ok = s && SDL_SavePNG(s, path.c_str());
    if (!ok) {
        SDL_Log("screenshot failed: %s", SDL_GetError());
    } else {
        SDL_Log("wrote %s", path.c_str());
    }
    return ok;
}

void save_screenshot(App& app) {
    if (app.top_down) {
        SDL_Surface* shot = SDL_RenderReadPixels(app.renderer, nullptr);
        save_png(shot, app.screenshot_path);
        SDL_DestroySurface(shot);
        return;
    }
    // The CPU frame itself, so the file is byte-identical across platforms.
    SDL_Surface* shot =
        SDL_CreateSurfaceFrom(kViewWidth, kViewHeight, SDL_PIXELFORMAT_RGBA32, app.frame.data(), kViewWidth * 4);
    save_png(shot, app.screenshot_path);
    SDL_DestroySurface(shot);
}

}  // namespace

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv) {
    auto app = std::make_unique<App>();
    std::string map_name;
    if (const char* env = std::getenv("ANTANTA_GAME_DIR")) app->game_dir = env;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--game" && i + 1 < argc) {
            app->game_dir = argv[++i];
        } else if (a == "--screenshot" && i + 1 < argc) {
            app->screenshot_path = argv[++i];
        } else if (a == "--at" && i + 2 < argc) {
            app->pos.col = std::atoi(argv[++i]);
            app->pos.row = std::atoi(argv[++i]);
        } else if (a == "--bench" && i + 1 < argc) {
            app->bench_frames = std::max(1, std::atoi(argv[++i]));
        } else if (a == "--topdown") {
            app->top_down = true;
        } else {
            map_name = a;
        }
    }
#ifdef __EMSCRIPTEN__
    app->game_dir = "/game";
    app->lower_case_paths = true;
#else
    if (map_name.empty() || app->game_dir.empty()) {
        SDL_Log(
            "usage: mapviewer <map name> [--game <game folder>] [--at <col> <row>] [--screenshot out.png] "
            "[--topdown] [--bench <frames>]\n"
            "the game folder defaults to the ANTANTA_GAME_DIR environment variable");
        return SDL_APP_FAILURE;
    }
#endif
    if (!SDL_CreateWindowAndRenderer("Antanta map viewer", kViewWidth, kViewHeight, SDL_WINDOW_RESIZABLE, &app->window,
                                     &app->renderer)) {
        SDL_Log("cannot create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (app->bench_frames) SDL_SetRenderVSync(app->renderer, 0);
    app->iso =
        SDL_CreateTexture(app->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, kViewWidth, kViewHeight);
    if (!app->iso) {
        SDL_Log("cannot create the view texture: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetTextureScaleMode(app->iso, SDL_SCALEMODE_NEAREST);
    if (!map_name.empty()) {
        if (map_name.size() < 4 || SDL_strcasecmp(map_name.c_str() + map_name.size() - 4, ".dat") != 0) {
            map_name += ".dat";
        }
        std::string error;
        if (!load_map(*app, map_name, &error)) {
            SDL_Log("cannot load map: %s", error.c_str());
            return SDL_APP_FAILURE;
        }
    }
    app->bench_start = SDL_GetTicksNS();
    *appstate = app.release();  // owned by SDL until SDL_AppQuit
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* e) {
    App& app = *static_cast<App*>(appstate);
    constexpr float kStep = 64;
    switch (e->type) {
        case SDL_EVENT_QUIT: return SDL_APP_SUCCESS;
        case SDL_EVENT_MOUSE_WHEEL:
            if (app.top_down) zoom_at(app, e->wheel.y > 0 ? 1.25f : 0.8f, e->wheel.mouse_x, e->wheel.mouse_y);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN: app.dragging = true; break;
        case SDL_EVENT_MOUSE_BUTTON_UP: app.dragging = false; break;
        case SDL_EVENT_MOUSE_MOTION:
            if (app.dragging && app.top_down) {
                app.cam_x -= e->motion.xrel / app.zoom;
                app.cam_y -= e->motion.yrel / app.zoom;
            }
            break;
        case SDL_EVENT_KEY_DOWN: {
            const float pan = kStep / app.zoom;
            switch (e->key.key) {
                case SDLK_ESCAPE: return SDL_APP_SUCCESS;
                case SDLK_T: app.top_down = !app.top_down; break;
                case SDLK_LEFT:
                case SDLK_A: app.top_down ? void(app.cam_x -= pan) : scroll(app, -1, 0); break;
                case SDLK_RIGHT:
                case SDLK_D: app.top_down ? void(app.cam_x += pan) : scroll(app, 1, 0); break;
                case SDLK_UP:
                case SDLK_W: app.top_down ? void(app.cam_y -= pan) : scroll(app, 0, -2); break;
                case SDLK_DOWN:
                case SDLK_S: app.top_down ? void(app.cam_y += pan) : scroll(app, 0, 2); break;
                case SDLK_EQUALS:
                case SDLK_KP_PLUS: zoom_at(app, 1.25f, 0, 0); break;
                case SDLK_MINUS:
                case SDLK_KP_MINUS: zoom_at(app, 0.8f, 0, 0); break;
                case SDLK_HOME: fit(app); break;
                default: break;
            }
            break;
        }
        default: break;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App& app = *static_cast<App*>(appstate);
    const std::string ready = std::string(kPickDir) + "ready";
    if (SDL_GetPathInfo(ready.c_str(), nullptr)) {  // web: the page has picked a map
        std::string error;
        auto text = load_file(ready, &error);
        SDL_RemovePath(ready.c_str());
        if (text) {  // "<map file>[ <col> <row>]"
            const std::string s(text->begin(), text->end());
            const std::size_t sp = s.find(' ');
            if (sp != std::string::npos) {
                app.pos.col = std::atoi(s.c_str() + sp + 1);
                const std::size_t sp2 = s.find(' ', sp + 1);
                app.pos.row = sp2 == std::string::npos ? 0 : std::atoi(s.c_str() + sp2 + 1);
            }
            if (!load_map(app, s.substr(0, sp), &error)) SDL_Log("cannot load map: %s", error.c_str());
            app.report_frame = true;
        }
    }
    SDL_SetRenderDrawColor(app.renderer, 24, 24, 32, 255);
    SDL_RenderClear(app.renderer);
    if (app.top_down && app.overview) {
        const SDL_FRect dst{-app.cam_x * app.zoom, -app.cam_y * app.zoom, app.map_w * app.zoom, app.map_h * app.zoom};
        SDL_RenderTexture(app.renderer, app.overview, nullptr, &dst);
    } else if (!app.top_down && app.map) {
        draw_iso(app);
        if (app.report_frame) {  // for scripts (web-smoke.ps1): the frame's crc32, as in tests/golden/render.tsv
            app.report_frame = false;
            char line[64];
            std::snprintf(line, sizeof line, "frame %d %d crc %08lx", app.pos.col, app.pos.row,
                          crc32(0, app.frame.data(), static_cast<uInt>(app.frame.size())));
            SDL_Log("%s", line);
#ifdef __EMSCRIPTEN__
            emscripten_run_script((std::string("document.title = '") + line + "'").c_str());
#endif
        }
    }
    ++app.frames;
    if (!app.screenshot_path.empty() && app.frames == 2) {
        save_screenshot(app);
        return SDL_APP_SUCCESS;
    }
    if (app.bench_frames && app.map) {
        // Scroll right, then down, then back, so every frame redraws the static layer.
        const int phase = (app.frames / 20) % 4;
        scroll(app, phase == 0 ? 1 : phase == 2 ? -1 : 0, phase == 1 ? 2 : phase == 3 ? -2 : 0);
        if (app.frames >= app.bench_frames) {
            const double n = app.frames, total = double(SDL_GetTicksNS() - app.bench_start) / 1e6;
            SDL_Log("bench: %d frames, static layer %.2f ms/frame, frame pass + upload %.2f ms/frame, %.1f fps overall",
                    app.frames, double(app.bench_static_ns) / 1e6 / n, double(app.bench_frame_ns) / 1e6 / n,
                    n * 1000 / total);
            return SDL_APP_SUCCESS;
        }
    }
    SDL_RenderPresent(app.renderer);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    std::unique_ptr<App> app(static_cast<App*>(appstate));
    if (!app) return;
    SDL_DestroyTexture(app->iso);
    SDL_DestroyTexture(app->overview);
    SDL_DestroyRenderer(app->renderer);
    SDL_DestroyWindow(app->window);
}
