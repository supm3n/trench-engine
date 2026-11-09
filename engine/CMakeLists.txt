cmake_minimum_required(VERSION 3.25)
project(antanta LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The one setting for the game folder: the ANTANTA_GAME_DIR environment variable at configure time, else the
# project's working copy. Tests read it from the environment ctest sets; they skip if the folder is missing.
if(DEFINED ENV{ANTANTA_GAME_DIR})
  set(_antanta_game "$ENV{ANTANTA_GAME_DIR}")
else()
  set(_antanta_game "${CMAKE_CURRENT_SOURCE_DIR}/../game")
endif()
set(ANTANTA_GAME_DIR "${_antanta_game}" CACHE PATH "game folder (read only), used by tests")
option(ANTANTA_SANITIZE "build with ASan and UBSan (clang)" OFF)
# The version of the product "Entente Engine" (docs/release.md, docs/naming.md): the Windows version information, the
# main menu's corner and the session and crash logs show it. By year: <year>.<n>, 2026.1 the first release, 2026.2
# the next, 2027.1 the first of the next year. Raise it for every package that goes to friends.
set(ANTANTA_VERSION 2026.2)
# The release exe for friends (docs/release.md, scripts/release.ps1): one file, the C++ runtime of llvm-mingw linked
# in (only Windows' own DLLs are loaded), no console window, the icon and version information, no developer defaults.
# Off in every build of the gate. It has to be set before the add_subdirectory calls below.
option(ANTANTA_RELEASE "the release exe for friends (docs/release.md)" OFF)
if(ANTANTA_RELEASE AND MINGW)
  add_link_options(-static)
endif()
# A reproducible release exe (docs/public-release.md): the same sources and toolchain give the same bytes, whatever
# the folders. The source and build paths are mapped away (__FILE__ and debug info) and the linker writes no time.
if(ANTANTA_RELEASE AND NOT MSVC)
  add_compile_options("-ffile-prefix-map=${CMAKE_SOURCE_DIR}=src" "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
  if(MINGW)
    add_link_options(-Wl,--no-insert-timestamp)
  endif()
endif()
# The public code-only build (docs/public-release.md): without the updater and the language download, which talk to
# the owner's own servers. tools/public_export.py turns it on in the public copy, which leaves those files out.
option(ANTANTA_PUBLIC "the public build: no updater, no language download" ON)
if(ANTANTA_SANITIZE)
  # -O1 keeps sanitized Debug builds about 8x faster (zlib inflate dominates the golden tests); asserts stay on.
  string(APPEND CMAKE_C_FLAGS_DEBUG " -O1")
  string(APPEND CMAKE_CXX_FLAGS_DEBUG " -O1")
endif()

# The web build with threads (docs/web.md): every library compiled for shared memory, so render/ draws in bands and
# tiles on several threads as on the desktop; the page then needs the COOP and COEP headers. Off (the gate's web
# build): one thread.
option(ANTANTA_WEB_THREADS "web build with threads (Emscripten pthreads)" OFF)
if(EMSCRIPTEN AND ANTANTA_WEB_THREADS)
  add_compile_options(-pthread)
  add_link_options(-pthread)
endif()

# Link-time optimisation for every web target, third-party code included (docs/web.md): the simulation steps 15 to
# 17 percent faster, the same results. It has to be set before the add_subdirectory calls below.
if(EMSCRIPTEN)
  add_compile_options(-flto)
  add_link_options(-flto)
endif()

# ---- third party (vendored release sources, see THIRD_PARTY.md) ----
set(TP "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${TP}/zlib-1.3.2" zlib EXCLUDE_FROM_ALL)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${TP}/SDL3-3.4.18" SDL3 EXCLUDE_FROM_ALL)

# Text for the front-end (PLAN.md 2026-10-15): SDL3_ttf over FreeType, no HarfBuzz, no plutosvg, no FreeType extras.
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory("${TP}/freetype-2.14.3" freetype EXCLUDE_FROM_ALL)
add_library(Freetype::Freetype ALIAS freetype)
# SDL3_ttf asks find_package(Freetype) when it does not build its own copy: these answer it with the target above.
set(FREETYPE_INCLUDE_DIR_ft2build "${TP}/freetype-2.14.3/include" CACHE PATH "" FORCE)
set(FREETYPE_INCLUDE_DIR_freetype2 "${TP}/freetype-2.14.3/include" CACHE PATH "" FORCE)
set(FREETYPE_LIBRARY freetype CACHE STRING "" FORCE)
set(SDLTTF_VENDORED OFF CACHE BOOL "" FORCE)
set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)
set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${TP}/SDL3_ttf-3.2.2" SDL3_ttf EXCLUDE_FROM_ALL)

# Sound and music (docs/sound.md, PLAN.md decision 5): SDL3_mixer with only its own WAV decoder and libvorbis (the
# game's effects are WAV, its music Ogg Vorbis), nothing loaded at run time, no tests, examples or install. stb_vorbis
# does not do: 21 of the game's 24 Ogg files come from an old encoder that uses floor type 0, which it rejects.
# libogg and libvorbis are the vendored copies in SDL3_mixer's external/ (libsdl-org v1.3.5-SDL and v1.3.7-SDL).
foreach(_fmt AIFF VOC AU FLAC GME MOD MP3 MIDI OPUS VORBIS_STB VORBIS_TREMOR WAVPACK TESTS EXAMPLES INSTALL)
  set(SDLMIXER_${_fmt} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDLMIXER_WAVE ON CACHE BOOL "" FORCE)
set(SDLMIXER_VORBIS_VORBISFILE ON CACHE BOOL "" FORCE)
set(SDLMIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDLMIXER_DEPS_SHARED OFF CACHE BOOL "" FORCE)
add_subdirectory("${TP}/SDL3_mixer-3.2.4" SDL3_mixer EXCLUDE_FROM_ALL)

# ---- engine compile rules (CONVENTIONS.md) ----
add_library(antanta_options INTERFACE)
if(MSVC)
  target_compile_options(antanta_options INTERFACE /W4 /WX /permissive- /utf-8 /fp:precise)
  target_compile_definitions(antanta_options INTERFACE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(antanta_options INTERFACE -Wall -Wextra -Wpedantic -Werror -ffp-contract=off)
endif()
if(ANTANTA_SANITIZE)
  set(_san -fsanitize=address,undefined -fno-sanitize=vptr -fno-sanitize-recover=all -fno-omit-frame-pointer)
  target_compile_options(antanta_options INTERFACE ${_san})
  target_link_options(antanta_options INTERFACE ${_san})
endif()

# Engine code: no exceptions, no RTTI. clang enforces both; MSVC enforces no RTTI (its STL headers need /EHsc).
add_library(antanta_engine_rules INTERFACE)
target_link_libraries(antanta_engine_rules INTERFACE antanta_options)
if(MSVC)
  target_compile_options(antanta_engine_rules INTERFACE /GR-)
else()
  target_compile_options(antanta_engine_rules INTERFACE -fno-exceptions -fno-rtti)
endif()

add_library(antanta_formats STATIC formats/bytes.cpp formats/les.cpp formats/sprites.cpp formats/map.cpp
  formats/params.cpp formats/image.cpp formats/svg.cpp formats/gameinf.cpp formats/menu.cpp formats/aitemplate.cpp)
target_include_directories(antanta_formats PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(antanta_formats PUBLIC zlibstatic PRIVATE antanta_engine_rules)
target_sources(antanta_formats PRIVATE formats/museum.cpp)  # Museum.mus and the text table (docs/formats.md)
target_sources(antanta_formats PRIVATE formats/mis.cpp formats/campaign.cpp)  # mission and campaign files (docs/formats.md)

add_library(antanta_render STATIC render/topdown.cpp render/raster.cpp render/iso.cpp render/ground_fx.cpp
            render/house_fx.cpp)
target_link_libraries(antanta_render PUBLIC antanta_formats PRIVATE antanta_engine_rules)
target_sources(antanta_render PRIVATE render/effects.cpp)
target_sources(antanta_render PRIVATE render/vehicle_dust.cpp)
target_sources(antanta_render PRIVATE render/bridges.cpp)

add_library(antanta_sim STATIC sim/rng.cpp sim/savegame.cpp sim/tick.cpp sim/trace.cpp sim/unittype.cpp sim/map.cpp
  sim/pathfind.cpp sim/unitmove.cpp sim/order.cpp sim/pathgrid.cpp sim/command.cpp sim/hash.cpp
  sim/house.cpp sim/research.cpp sim/group.cpp sim/worker.cpp sim/combat.cpp sim/shell.cpp sim/ai.cpp
  sim/vehicle.cpp sim/vehicle_path.cpp sim/air.cpp sim/ship.cpp sim/skirmish.cpp sim/endgame.cpp sim/events.cpp
  sim/market.cpp sim/flow.cpp)
target_sources(antanta_sim PRIVATE sim/fog.cpp)  # the fog of war of the local side (docs/fog.md)
target_sources(antanta_sim PRIVATE sim/plus_brain.cpp)  # Entente Engine Plus's smart research rule (docs/plus-balance.md 6)
target_sources(antanta_sim PRIVATE sim/plus_army.cpp)  # Entente Engine Plus's army brain (docs/ai.md 11)
target_sources(antanta_sim PRIVATE sim/strike.cpp)  # Entente Engine Plus's call-in strikes (docs/decisions.md 34)
target_sources(antanta_sim PRIVATE sim/control_group.cpp)  # the number-key control groups (docs/interface.md D.3)
target_sources(antanta_sim PRIVATE sim/ownsave.cpp)  # our own save files (milestone 4 slice H, docs/save-own.md)
target_sources(antanta_sim PRIVATE sim/firepoint.cpp)  # fire at a point and the gas clouds (docs/combat.md 21)
target_sources(antanta_sim PRIVATE sim/transport.cpp)  # transports and the sea (milestone 4 slice L, docs/transports.md)
target_sources(antanta_sim PRIVATE sim/special.cpp)  # the special map objects: the pontoon bridge (docs/transports.md 19)
target_sources(antanta_sim PRIVATE sim/mission.cpp)  # the mission start (docs/missions.md 2)
target_sources(antanta_sim PRIVATE sim/mission_events.cpp)  # the mission event blocks (docs/missions.md 3)
target_sources(antanta_sim PRIVATE sim/mapgen.cpp sim/mapgen_check.cpp)  # the random map generator (docs/map-generator-plan.md)
target_sources(antanta_sim PRIVATE sim/mapgen_ground.cpp)  # the generator's ground painting (docs/map-generator-plan.md 5.8)
target_sources(antanta_sim PRIVATE sim/mapgen_hills.cpp)  # the generator's hills and mountains (docs/map-generator-plan.md 5.5)
target_sources(antanta_sim PRIVATE sim/mapgen_forest.cpp)  # the generator's free deposits and forests (docs/map-generator-plan.md 5.6, 5.7)
target_sources(antanta_sim PRIVATE sim/mapgen_water.cpp)  # the generator's lakes, sea, coasts and port sites (docs/map-generator-plan.md 5.4)
target_sources(antanta_sim PRIVATE sim/mapgen_sea.cpp)  # the generator's Sea maps: the water drawn first, the starts fitted to it (docs/map-generator-plan.md 5.1, 5.4)
target_sources(antanta_sim PRIVATE sim/mapgen_land.cpp)  # the generator's land maps: a kind of country drawn first, the starts fitted to it (docs/map-generator-plan.md 5.1)
target_link_libraries(antanta_sim PUBLIC antanta_formats PRIVATE antanta_engine_rules)
# The simulation's sin, cos, tan, atan2 and acos (sim/dmath.hpp, docs/determinism.md): fdlibm 5.3 from netlib, the
# files unmodified, as C with no warnings. Only + - * / and sqrt, so the results are the same on every PC and in the
# browser (never the C library's, whose results depend on the CPU). Every name gets an antanta_fdlibm_ prefix here so
# nothing clashes with the C library. No FMA contraction, no fast maths, no type-based aliasing (fdlibm reads the
# halves of a double through an int pointer); x86-64 and wasm are little endian.
add_library(fdlibm STATIC "${TP}/fdlibm-5.3/s_sin.c" "${TP}/fdlibm-5.3/s_cos.c" "${TP}/fdlibm-5.3/s_tan.c"
  "${TP}/fdlibm-5.3/k_sin.c" "${TP}/fdlibm-5.3/k_cos.c" "${TP}/fdlibm-5.3/k_tan.c" "${TP}/fdlibm-5.3/e_rem_pio2.c"
  "${TP}/fdlibm-5.3/k_rem_pio2.c" "${TP}/fdlibm-5.3/s_atan.c" "${TP}/fdlibm-5.3/e_atan2.c" "${TP}/fdlibm-5.3/e_acos.c"
  "${TP}/fdlibm-5.3/s_fabs.c" "${TP}/fdlibm-5.3/s_floor.c" "${TP}/fdlibm-5.3/s_scalbn.c" "${TP}/fdlibm-5.3/s_copysign.c")
set(_fdlibm_names sin cos tan atan fabs floor scalbn copysign __ieee754_atan2 __ieee754_acos __ieee754_rem_pio2
  __kernel_sin __kernel_cos __kernel_tan __kernel_rem_pio2)
foreach(_n IN LISTS _fdlibm_names)
  string(REGEX REPLACE "^_+" "" _short "${_n}")
  target_compile_definitions(fdlibm PRIVATE "${_n}=antanta_fdlibm_${_short}")
endforeach()
target_compile_definitions(fdlibm PRIVATE __LITTLE_ENDIAN)
if(MSVC)
  target_compile_options(fdlibm PRIVATE /w /fp:precise)
else()
  target_compile_options(fdlibm PRIVATE -w -ffp-contract=off -fno-fast-math -fno-strict-aliasing)
endif()
target_link_libraries(antanta_sim PUBLIC fdlibm)
# The Lua missions (docs/missions.md 5, slice M5): Lua 5.0.3's core and base library only (no io, os or loadlib), its
# number conversions replaced by ours (script/lua_user.h, locale free), as C with no warnings (vendored, unmodified).
file(GLOB _lua_core CONFIGURE_DEPENDS "${TP}/lua-5.0.3/src/*.c")
add_library(lua503 STATIC ${_lua_core} "${TP}/lua-5.0.3/src/lib/lauxlib.c" "${TP}/lua-5.0.3/src/lib/lbaselib.c"
            script/lua_user.cpp)
target_include_directories(lua503 PUBLIC "${TP}/lua-5.0.3/include" "${CMAKE_CURRENT_SOURCE_DIR}"
                           PRIVATE "${TP}/lua-5.0.3/src")
target_compile_definitions(lua503 PUBLIC "LUA_USER_H=\"script/lua_user.h\"")
if(MSVC)
  target_compile_options(lua503 PRIVATE /w)
else()
  target_compile_options(lua503 PRIVATE -w -ffp-contract=off)
endif()
# The script manager and its 22 functions, its saves (slice M6) and the owner's fixes of the shipped scripts.
target_sources(antanta_sim PRIVATE script/lua_manager.cpp script/lua_save.cpp script/fixes.cpp)
target_link_libraries(antanta_sim PRIVATE lua503)

add_library(antanta_platform STATIC platform/files.cpp)
target_sources(antanta_platform PRIVATE platform/session_log.cpp)  # the log file of one play session (docs/bugs.md B5)
target_sources(antanta_platform PRIVATE platform/game_folder.cpp)  # where the game's files are (docs/release.md)
target_link_libraries(antanta_platform PUBLIC antanta_formats SDL3::SDL3-static PRIVATE antanta_engine_rules)

add_executable(mapviewer apps/mapviewer/main.cpp)
target_link_libraries(mapviewer PRIVATE antanta_platform antanta_render antanta_engine_rules)
target_link_libraries(mapviewer PRIVATE antanta_sim)  # generated maps from a map code (sim/mapgen.hpp)

# The live renderer (the GameState drawn by the compositor), the debug HUD's logic and the view inputs both the game
# app and the headless runner make from a camera.
add_library(antanta_render_live STATIC render/live.cpp)
target_link_libraries(antanta_render_live PUBLIC antanta_render antanta_sim PRIVATE antanta_engine_rules)
target_sources(antanta_render_live PRIVATE render/effects_emit.cpp)  # the live game's effects (docs/render.md 14)
target_sources(antanta_render_live PRIVATE render/shells.cpp)  # the shells in flight and their trails (render.md 15)
target_sources(antanta_render_live PRIVATE render/unit_fx.cpp)  # muzzle and rifle flashes, burnt stains (render.md 18)
target_sources(antanta_render_live PRIVATE render/sel_ring.cpp)  # the dots of the selection rings (render.md 17)
target_sources(antanta_render_live PRIVATE render/water.cpp)  # the water, the waves, the bubbles (render.md 19)
add_library(antanta_ui STATIC ui/hud.cpp ui/settings.cpp ui/profiles.cpp ui/skirmish_screen.cpp ui/iface_layout.cpp
            ui/camera.cpp ui/video_sequence.cpp)
target_sources(antanta_ui PRIVATE ui/panel.cpp ui/unit_panel.cpp)  # the panel toolkit and the unit panel (slice I4)
target_sources(antanta_ui PRIVATE ui/build_menu.cpp)  # the worker build menu and the build ghost (slice I5)
target_sources(antanta_ui PRIVATE ui/sound.cpp)  # the rules of the sound effects and music (slice J)
target_sources(antanta_ui PRIVATE ui/select_keys.cpp)  # the original's game keys (slice I9, docs/interface.md D.3)
target_sources(antanta_ui PRIVATE ui/minimap.cpp)  # the minimap (slice I2)
target_sources(antanta_ui PRIVATE ui/fog_view.cpp)  # the fog of war over the game view (docs/fog.md 7)
target_sources(antanta_ui PRIVATE ui/world_marks.cpp)  # the marks in the world (slice I9)
target_sources(antanta_ui PRIVATE ui/selection_rings.cpp)  # the selection rings (docs/render.md 17)
target_sources(antanta_ui PRIVATE ui/facing.cpp)  # the right-drag facing arrow and order (slice I9)
target_sources(antanta_ui PRIVATE ui/house_panel.cpp)  # the house panel and production (slice I6)
target_sources(antanta_ui PRIVATE ui/fire_point.cpp)  # fire at a point, cursor mode 5 (docs/interface.md D.1)
target_sources(antanta_ui PRIVATE ui/game_menu.cpp)  # the in-game menu's rules and Restart (docs/frontend.md 4.12)
target_sources(antanta_ui PRIVATE ui/game_windows.cpp)  # the in-game windows' rules (slice I8)
target_sources(antanta_ui PRIVATE ui/cursor.cpp)  # the cursor's per-frame choice (slice I9)
target_sources(antanta_ui PRIVATE ui/museum_screen.cpp)  # the museum's rules (docs/frontend.md 4.16)
target_sources(antanta_ui PRIVATE ui/mission_list.cpp ui/progress.cpp)  # mission lists and progress (missions.md 4)
# The display language (docs/languages.md, slice L1). Every engine/lang/<code>/ with a language.txt is built in: its
# language.txt, ui.txt and overrides.txt (slice L3, the override layer of docs/languages.md 6.1) become byte arrays of
# lang_builtin.cpp (ui/lang.hpp lang_builtin).
file(GLOB _lang_infos CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/lang/*/language.txt")
set(ANTANTA_LANG_ARRAYS "")
set(ANTANTA_LANG_LIST "")
foreach(_info IN LISTS _lang_infos)
  get_filename_component(_dir "${_info}" DIRECTORY)
  get_filename_component(_code "${_dir}" NAME)
  string(MAKE_C_IDENTIFIER "${_code}" _id)
  foreach(_part language ui overrides)
    set(_bytes "")
    if(EXISTS "${_dir}/${_part}.txt")
      file(READ "${_dir}/${_part}.txt" _hex HEX)
      string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_dir}/${_part}.txt")
    endif()
    string(APPEND ANTANTA_LANG_ARRAYS "const unsigned char k_${_id}_${_part}[] = {${_bytes}0};\n")
  endforeach()
  string(APPEND ANTANTA_LANG_LIST "{\"${_code}\", k_${_id}_language, k_${_id}_ui, k_${_id}_overrides}, ")
endforeach()
configure_file(lang/builtin.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/lang_builtin.cpp" @ONLY)
target_sources(antanta_ui PRIVATE ui/lang.cpp "${CMAKE_CURRENT_BINARY_DIR}/lang_builtin.cpp")
target_sources(antanta_ui PRIVATE ui/text_fit.cpp)  # the place table and the fitting of docs/languages.md 2.5 (L2b)
target_sources(antanta_ui PRIVATE ui/painted.cpp)  # the painted words of the pictures (docs/languages.md 3, L4)
target_link_libraries(antanta_ui PUBLIC antanta_render_live PRIVATE antanta_engine_rules)
# The front-end screens (docs/frontend.md) and the in-game interface (docs/interface.md): SDL_Renderer and SDL3_ttf.
add_library(antanta_frontend STATIC ui/frontend.cpp ui/fonts.cpp ui/iface_draw.cpp)
# The display font of the painted headings (docs/languages.md 9 item 2, slice L4b): an OFL font built into the engine
# as bytes (display_font.cpp, made from ui/display_font.cpp.in), so the desktop and the browser draw the same.
set(_display_font "${TP}/black-ops-one/BlackOpsOne-Regular.ttf")
file(READ "${_display_font}" _hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," ANTANTA_DISPLAY_FONT_BYTES "${_hex}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_display_font}")
configure_file(ui/display_font.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/display_font.cpp" @ONLY)
target_sources(antanta_frontend PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/display_font.cpp")
target_sources(antanta_frontend PRIVATE ui/panel_draw.cpp)
target_sources(antanta_frontend PRIVATE ui/minimap_draw.cpp)  # the minimap (slice I2)
target_sources(antanta_frontend PRIVATE ui/fog_draw.cpp)  # the fog of war over the game view (docs/fog.md 7)
target_sources(antanta_frontend PRIVATE ui/build_ghost_draw.cpp)  # the build ghost (slice I5)
target_sources(antanta_frontend PRIVATE ui/world_marks_draw.cpp)  # the marks in the world (slice I9)
target_sources(antanta_frontend PRIVATE ui/rally_flag_draw.cpp)  # the rally flag (slice I6)
target_sources(antanta_frontend PRIVATE ui/cursor_draw.cpp)  # the cursor pictures (slice I9)
target_link_libraries(antanta_frontend PUBLIC antanta_ui SDL3::SDL3-static SDL3_ttf::SDL3_ttf PRIVATE antanta_engine_rules)
# The network game (docs/multiplayer.md): the protocol, the transports, the lockstep rules, the server and the
# player's end. WebSocket: IXWebSocket 12.0.1 on the desktop (plain ws://, no TLS and no zlib yet; vendored
# unmodified, compiled without the engine's rules as the other vendored code), Emscripten's websocket.h in the browser.
add_library(antanta_net STATIC net/config.cpp net/protocol.cpp net/transport.cpp net/lockstep.cpp net/server.cpp net/client.cpp)
target_link_libraries(antanta_net PUBLIC antanta_sim PRIVATE antanta_engine_rules)
# The build id carries a hash of engine/sim and engine/formats (scripts/sim-hash.cmake): the header is rewritten only when
# one of those files changed, and only net/lockstep.cpp includes it.
file(GLOB_RECURSE _sim_hash_deps CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/sim/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/sim/*.hpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/formats/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/formats/*.hpp")
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/gen/sim_hash.hpp"
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/gen"
  COMMAND ${CMAKE_COMMAND} "-DENGINE_DIR=${CMAKE_CURRENT_SOURCE_DIR}" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/gen/sim_hash.hpp"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sim-hash.cmake"
  DEPENDS ${_sim_hash_deps} "${CMAKE_CURRENT_SOURCE_DIR}/scripts/sim-hash.cmake"
  COMMENT "Hashing the simulation's code for the network build id" VERBATIM)
add_custom_target(antanta_sim_hash DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/gen/sim_hash.hpp")
add_dependencies(antanta_net antanta_sim_hash)
target_include_directories(antanta_net PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gen")
if(EMSCRIPTEN)
  target_sources(antanta_net PRIVATE net/ws_web.cpp)
  target_link_options(antanta_net PUBLIC -lwebsocket.js)
else()
  file(GLOB _ix_sources CONFIGURE_DEPENDS "${TP}/IXWebSocket-12.0.1/ixwebsocket/*.cpp")
  list(FILTER _ix_sources EXCLUDE REGEX "IXSocket(AppleSSL|MbedTLS|OpenSSL)\.cpp$")
  add_library(ixwebsocket STATIC ${_ix_sources})
  target_include_directories(ixwebsocket SYSTEM PUBLIC "${TP}/IXWebSocket-12.0.1")
  find_package(Threads REQUIRED)
  target_link_libraries(ixwebsocket PUBLIC Threads::Threads)
  if(WIN32)
    target_link_libraries(ixwebsocket PUBLIC ws2_32 wsock32 shlwapi)
    target_compile_definitions(ixwebsocket PRIVATE _CRT_SECURE_NO_WARNINGS WIN32_LEAN_AND_MEAN NOMINMAX)
  endif()
  if(MSVC)
    target_compile_options(ixwebsocket PRIVATE /w /EHsc)
  else()
    target_compile_options(ixwebsocket PRIVATE -w)
  endif()
  target_sources(antanta_net PRIVATE net/ws_native.cpp)
  target_sources(antanta_net PRIVATE net/lan.cpp)  # games on the same network (UDP, slice MP2)
  target_link_libraries(antanta_net PRIVATE ixwebsocket)
  if(WIN32)
    target_link_libraries(antanta_net PRIVATE ws2_32 iphlpapi)
  endif()
endif()

add_library(antanta_appcommon STATIC apps/common/view_input.cpp)
target_sources(antanta_appcommon PRIVATE apps/common/net_game.cpp)  # the network game's lockstep driver
target_link_libraries(antanta_appcommon PUBLIC antanta_sim antanta_net PRIVATE antanta_engine_rules)
# Slice MP9: the seats a player keeps and the autosaves' ring, written on a thread of their own (not on the web).
target_sources(antanta_appcommon PRIVATE apps/common/net_store.cpp)
if(NOT EMSCRIPTEN)
  target_link_libraries(antanta_appcommon PUBLIC Threads::Threads)
endif()

# The network game's server (docs/multiplayer.md): several games in one process, a pure relay (no game files).
if(NOT EMSCRIPTEN)
  add_executable(antanta-server apps/antanta-server/main.cpp)
  target_link_libraries(antanta-server PRIVATE antanta_net antanta_engine_rules)
endif()

# The headless runner reads and writes plain files. On the web target it is built for node (NODERAWFS), so the gate
# runs the fidelity scenarios on wasm too.
add_executable(antanta-headless apps/antanta-headless/main.cpp)
target_link_libraries(antanta-headless PRIVATE antanta_platform antanta_sim antanta_appcommon antanta_engine_rules)

# The game app: an original save played live with the debug HUD (docs/play.md).
add_executable(antanta apps/antanta/main.cpp apps/antanta/sim_host.cpp)
target_link_libraries(antanta PRIVATE antanta_platform antanta_frontend antanta_appcommon antanta_engine_rules)
target_compile_definitions(antanta PRIVATE "ANTANTA_VERSION=\"${ANTANTA_VERSION}\"")
target_compile_definitions(antanta_platform PRIVATE "ANTANTA_VERSION=\"${ANTANTA_VERSION}\"")  # the session log
if(ANTANTA_RELEASE)
  target_compile_definitions(antanta PRIVATE ANTANTA_RELEASE)
  target_compile_definitions(antanta_platform PRIVATE ANTANTA_RELEASE)
  if(WIN32)
    # Its own fixed name (no version in it, so shortcuts and a later updater keep working): the original's exe is
    # Antanta.exe, and Windows does not tell antanta.exe apart from it, so a copy of the dev build put into the game
    # folder would replace it. The icon, the version information and the manifest come from antanta.rc.in.
    string(REPLACE "." ";" _v "${ANTANTA_VERSION}")
    list(GET _v 0 ANTANTA_VERSION_MAJOR)
    list(GET _v 1 ANTANTA_VERSION_MINOR)
    set(ANTANTA_VERSION_PATCH 0)  # the version is <year>.<n>; the Windows version information has four numbers
    list(LENGTH _v _v_count)
    if(_v_count GREATER 2)
      list(GET _v 2 ANTANTA_VERSION_PATCH)
    endif()
    set(ANTANTA_RES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/apps/antanta")
    set(ANTANTA_RELEASE_NAME "EntenteEngine")  # EntenteEngine.exe
    configure_file(apps/antanta/antanta.rc.in "${CMAKE_CURRENT_BINARY_DIR}/antanta.rc" @ONLY)
    target_sources(antanta PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/antanta.rc")
    set_property(SOURCE "${CMAKE_CURRENT_BINARY_DIR}/antanta.rc" APPEND PROPERTY OBJECT_DEPENDS
                 "${ANTANTA_RES_DIR}/antanta.ico" "${ANTANTA_RES_DIR}/antanta.manifest")
    set_target_properties(antanta PROPERTIES WIN32_EXECUTABLE ON OUTPUT_NAME "${ANTANTA_RELEASE_NAME}")
  endif()
endif()
# The year versions and GitHub's answer for the newest release (platform/release_check.hpp, pure), and on Windows
# desktop the HTTPS requests with WinHTTP (platform/http_win.hpp) and the public build's Releases notice on a thread
# (platform/release_check_win.hpp, docs/public-release.md). The updater below uses the versions and the requests.
add_library(antanta_release_check STATIC platform/release_check.cpp)
target_include_directories(antanta_release_check PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(antanta_release_check PRIVATE antanta_engine_rules)
if(WIN32 AND NOT EMSCRIPTEN)
  find_package(Threads REQUIRED)
  target_sources(antanta_release_check PRIVATE platform/http_win.cpp platform/release_check_win.cpp)
  target_link_libraries(antanta_release_check PUBLIC Threads::Threads PRIVATE winhttp)
endif()
# The updater (docs/download.md "The updater"), Windows desktop only: Ed25519 from Monocypher 4.0.3 (vendored
# unmodified, compiled without the engine's rules as the other vendored code), WinHTTP and BCrypt of Windows. The app
# checks only as the release exe or with --update-url; entente-sign is the owner's key and signing tool. The language
# packs and the Access login (docs/languages.md, slice L3b) use the same WinHTTP code, X25519 and Poly1305 of
# Monocypher and DPAPI (crypt32) for the stored login.
if(WIN32 AND NOT EMSCRIPTEN AND NOT ANTANTA_PUBLIC)
  add_library(monocypher STATIC "${TP}/monocypher-4.0.3/src/monocypher.c"
              "${TP}/monocypher-4.0.3/src/optional/monocypher-ed25519.c")
  target_include_directories(monocypher SYSTEM PUBLIC "${TP}/monocypher-4.0.3/src")
  if(MSVC)
    target_compile_options(monocypher PRIVATE /w)
  else()
    target_compile_options(monocypher PRIVATE -w)
  endif()
  find_package(Threads REQUIRED)
  add_library(antanta_update STATIC platform/update.cpp platform/update_win.cpp platform/access.cpp
              platform/lang_pack.cpp platform/lang_pack_win.cpp)
  target_include_directories(antanta_update PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(antanta_update PUBLIC Threads::Threads antanta_release_check
                        PRIVATE monocypher winhttp bcrypt crypt32 shell32 antanta_engine_rules)
  add_executable(entente-sign apps/entente-sign/main.cpp)
  target_link_libraries(entente-sign PRIVATE antanta_update antanta_engine_rules)
  target_link_libraries(antanta PRIVATE antanta_update)
  target_compile_definitions(antanta PRIVATE ANTANTA_UPDATER)
elseif(WIN32 AND NOT EMSCRIPTEN)
  # Without the updater (the public build): the menu only tells of a newer release on GitHub (decisions.md 40).
  target_link_libraries(antanta PRIVATE antanta_release_check)
  target_compile_definitions(antanta PRIVATE ANTANTA_RELEASE_CHECK)
endif()
# Sound and music on the audio device (platform/audio.hpp, slice J).
add_library(antanta_audio STATIC platform/audio.cpp)
target_link_libraries(antanta_audio PUBLIC antanta_ui SDL3::SDL3-static SDL3_mixer::SDL3_mixer PRIVATE antanta_engine_rules)
target_link_libraries(antanta PRIVATE antanta_audio)
# The videos (docs/sound.md section 5): pl_mpeg decodes the MPEG-1 files of tools/convert_videos.cmd. On the web the
# page unpacks them into /game/videos/ (tools/web_game_files.py, docs/web.md).
add_library(plmpeg STATIC ui/pl_mpeg_impl.c)
target_include_directories(plmpeg SYSTEM PUBLIC "${TP}/pl_mpeg-c871f2b")
if(MSVC)
  target_compile_options(plmpeg PRIVATE /w)
else()
  target_compile_options(plmpeg PRIVATE -w)
endif()
# On desktop Windows the game's own .wmv files play first, through Windows Media Foundation (ui/video_decoder_mf.cpp;
# mfplat and mfreadwrite are Windows' own DLLs, missing only on N editions without the Media Feature Pack).
add_library(antanta_video STATIC ui/video_decoder.cpp ui/video_decoder_mf.cpp ui/video_host.cpp)
target_link_libraries(antanta_video PUBLIC antanta_ui plmpeg SDL3::SDL3-static PRIVATE antanta_engine_rules)
if(WIN32 AND NOT EMSCRIPTEN)
  target_link_libraries(antanta_video PRIVATE mfplat mfreadwrite mfuuid ole32)
endif()
target_link_libraries(antanta PRIVATE antanta_video)
if(EMSCRIPTEN)
  target_link_options(antanta-headless PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=4MB
    -sENVIRONMENT=node -sEXIT_RUNTIME=1)
endif()

if(EMSCRIPTEN)
  set_target_properties(mapviewer PROPERTIES SUFFIX .html)
  target_link_options(mapviewer PRIVATE --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/apps/mapviewer/shell.html"
    -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -sEXPORTED_RUNTIME_METHODS=FS)
  set_property(TARGET mapviewer APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/apps/mapviewer/shell.html")
  # The game in the browser (docs/web.md): the page starts it (callMain) once the game's files are in /game; our
  # folder /libsdl is IndexedDB (IDBFS); the free menu fonts are inside the build (the page cannot read the system's).
  # Memory grows up to wasm32's 4 GB (a 65,000-unit game).
  set(_lib "${TP}/liberation-fonts-ttf-2.1.5")
  set_target_properties(antanta PROPERTIES SUFFIX .html)
  target_link_options(antanta PRIVATE --shell-file "${CMAKE_CURRENT_SOURCE_DIR}/apps/antanta/shell.html"
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=4GB -sSTACK_SIZE=4MB -sFORCE_FILESYSTEM=1
    -sINVOKE_RUN=0 -sEXPORTED_RUNTIME_METHODS=FS,callMain -lidbfs.js
    "SHELL:--embed-file ${_lib}/LiberationSerif-Bold.ttf@/third_party/liberation-fonts-ttf-2.1.5/LiberationSerif-Bold.ttf"
    "SHELL:--embed-file ${_lib}/LiberationSans-Regular.ttf@/third_party/liberation-fonts-ttf-2.1.5/LiberationSans-Regular.ttf")
  # Workers made at the start: render/raster.cpp's band pool (up to 15), iso.cpp's tiles (8) and the sim thread
  # (apps/antanta/sim_host.cpp, docs/sim-thread.md).
  if(ANTANTA_WEB_THREADS)
    target_link_options(antanta PRIVATE "-sPTHREAD_POOL_SIZE=Math.min(navigator.hardwareConcurrency,16)+9"
      -sPTHREAD_POOL_SIZE_STRICT=2)
  endif()
  set_property(TARGET antanta APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/apps/antanta/shell.html")
endif()
enable_testing()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")  # the public copy has no tests (docs/public-release.md)
  add_subdirectory(tests)
endif()
