# Third-party software

Entente Engine is built on free software made by other people. Thank you to everyone behind these projects.

Each library is vendored unchanged in its own folder under [`engine/third_party/`](engine/third_party/) and keeps its
own licence, which you find in that folder.

| Library | What it does here | Licence (file) |
|---|---|---|
| SDL 3.4.18 | window, input, sound output, drawing | zlib ([`SDL3-3.4.18/LICENSE.txt`](engine/third_party/SDL3-3.4.18/LICENSE.txt)) |
| yuv2rgb, part of SDL | video colour conversion | BSD 3-clause ([`SDL3-3.4.18/src/video/yuv2rgb/LICENSE`](engine/third_party/SDL3-3.4.18/src/video/yuv2rgb/LICENSE)) |
| HIDAPI, part of SDL | game controllers | its own HIDAPI licence ([`SDL3-3.4.18/src/hidapi/LICENSE.txt`](engine/third_party/SDL3-3.4.18/src/hidapi/LICENSE.txt)) |
| SDL_ttf 3.2.2 | text | zlib ([`SDL3_ttf-3.2.2/LICENSE.txt`](engine/third_party/SDL3_ttf-3.2.2/LICENSE.txt)) |
| SDL_mixer 3.2.4 | sound effects and music | zlib ([`SDL3_mixer-3.2.4/LICENSE.txt`](engine/third_party/SDL3_mixer-3.2.4/LICENSE.txt)) |
| libogg 1.3.5 and libvorbis 1.3.7, inside SDL_mixer | the game's Ogg Vorbis music | BSD 3-clause ([`ogg/COPYING`](engine/third_party/SDL3_mixer-3.2.4/external/ogg/COPYING), [`vorbis/COPYING`](engine/third_party/SDL3_mixer-3.2.4/external/vorbis/COPYING)) |
| FreeType 2.14.3 | font drawing | FreeType License ([`freetype-2.14.3/docs/FTL.TXT`](engine/third_party/freetype-2.14.3/docs/FTL.TXT)) |
| BDF and PCF drivers, part of FreeType | font formats | their own notices ([`src/bdf/README`](engine/third_party/freetype-2.14.3/src/bdf/README), [`src/pcf/README`](engine/third_party/freetype-2.14.3/src/pcf/README)) |
| zlib 1.3.2 | unpacking the game's archives | zlib ([`zlib-1.3.2/LICENSE`](engine/third_party/zlib-1.3.2/LICENSE)) |
| Lua 5.0.3 | the game's mission scripts | MIT ([`lua-5.0.3/COPYRIGHT`](engine/third_party/lua-5.0.3/COPYRIGHT)) |
| fdlibm 5.3 | the same sine, cosine and angle maths on every PC, so network games stay in step | Sun's notice, "freely granted, provided that this notice is preserved" ([`fdlibm-5.3/NOTICE.txt`](engine/third_party/fdlibm-5.3/NOTICE.txt)) |
| pl_mpeg (commit c871f2b) | the videos | MIT (in [`pl_mpeg-c871f2b/pl_mpeg.h`](engine/third_party/pl_mpeg-c871f2b/pl_mpeg.h)) |
| IXWebSocket 12.0.1 | network games | BSD 3-clause ([`IXWebSocket-12.0.1/LICENSE.txt`](engine/third_party/IXWebSocket-12.0.1/LICENSE.txt)) |
| Black Ops One 1.004 | the display font of the headings | SIL Open Font License 1.1 ([`black-ops-one/OFL.txt`](engine/third_party/black-ops-one/OFL.txt)) |
| Liberation fonts 2.1.5 | stand-ins for Times New Roman Bold and Arial, used only by a build run from its own build folder | SIL Open Font License 1.1 ([`liberation-fonts-ttf-2.1.5/LICENSE`](engine/third_party/liberation-fonts-ttf-2.1.5/LICENSE)) |

Portions of this software are copyright (c) 2026 The FreeType Project (https://freetype.org). All rights reserved.

## The toolchain's runtime

The release exe is linked statically with the llvm-mingw toolchain, so it also carries parts of that toolchain's
runtime:

- LLVM's libc++ and libunwind: Apache License 2.0 with LLVM Exceptions.
- The MinGW-w64 runtime and winpthreads: their own permissive licences.

Their licence files come with llvm-mingw (the pinned version is in [`engine/toolchains.txt`](engine/toolchains.txt)).
Each release carries them together with the libraries' licences.

## Fonts from Windows

Entente Engine draws its menus with Times New Roman Bold and Arial. It reads them from your own Windows font folder
at run time; they are never copied into this repository or into the release.
