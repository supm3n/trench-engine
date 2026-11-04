<p align="center">
  <img src=".github/banner.svg" alt="Entente Engine: an unofficial engine for The Entente Gold" width="760">
</p>

<p align="center">
  <a href="https://github.com/supm3n/trench-engine/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/supm3n/trench-engine?label=release"></a>
  <img alt="Windows 10 and 11, 64-bit" src="https://img.shields.io/badge/Windows-10%20%7C%2011%20(64--bit)-0078d4">
  <a href="LICENSE"><img alt="Licence: PolyForm Strict 1.0.0" src="https://img.shields.io/badge/licence-PolyForm%20Strict%201.0.0-555"></a>
</p>

**Entente Engine** is an unofficial, home-made engine for the strategy game **The Entente Gold** (Lesta Studio,
2003). It plays the same game, with the same maps, pictures, sounds and missions, on today's Windows 10 and 11,
without a compatibility wrapper. Along the way it fixes many of the old game's crashes and bugs, adds comforts such
as any window size and mouse wheel zoom, and runs big battles more smoothly. For players who want something new,
there is an optional rule set, **Entente Engine Plus**; the standard rules stay as they were.

It is a free hobby project. Its own menus are in English, German, Italian and Russian; mission texts and
briefings come from your copy of the game (English on Steam). Versions are numbered by
year: 2026.1 is the first release of 2026.

<p align="center">
  <img src="https://github.com/supm3n/trench-engine/releases/download/v2026.1/screenshot-battle.jpg" alt="A big battle with the standard rules" width="900">
</p>

> [!IMPORTANT]
> **You need your own copy of The Entente Gold**, for example from Steam. Entente Engine contains none of the game's files: no pictures, sounds, maps or mission texts. It reads them from your own copy of The Entente Gold. To play the same game, the code does include some numbers from the original program (such as research prices and shell values) and the few words painted on the game's buttons, which it translates for the menus. It never changes the game's own files, and the original game keeps working as before.

## Download and install

**Prefer watching?** The video shows the download, the install and the tutorial, step by step:

<a href="https://youtu.be/mgHVgllk7jc"><img src="https://i.ytimg.com/vi/mgHVgllk7jc/maxresdefault.jpg" alt="Video: The Entente Gold: the game rebuilt for Windows 10 and 11" width="560"></a>

1. Download the zip file of the newest version from the
   **[Releases page](https://github.com/supm3n/trench-engine/releases)** and unzip it.
2. Put `EntenteEngine.exe` into the game's folder, next to the original `Antanta.exe` (in Steam: right-click the game,
   **Manage**, **Browse local files**).
3. Double-click it. On the first start Windows may warn about a new, unsigned program: click **More info**, then
   **Run anyway**.

Entente Engine finds your copy of the game by itself (Steam included) or asks you once for its folder. The full
guide, with network play, saves, updating and a troubleshooting list, is in **[docs/INSTALL.md](docs/INSTALL.md)**.

## Highlights

| | What you get | Details |
|---|---|---|
| **Fixed** | Crashes and freezes of the original that cannot happen here, among them the well-known frozen mouse and the 8-player start crash. Saves that keep everything. Many gameplay, mission, sound and drawing bugs. | [Features](docs/FEATURES.md#crashes-and-freezes) |
| **Comfort** | Any window size, widescreen and full screen; interface scale for big screens; smooth scrolling; mouse wheel zoom; network games for up to 8 players with rejoin and autosave. | [Features](docs/FEATURES.md#interface-and-comfort) |
| **Faster** | About half the original's time per game turn on the same saves and PC, 110 to 234 pictures a second instead of about 30, full game speed up to the biggest save measured (86,415 units), and far less stutter. | [Performance](#performance-at-a-glance) |
| **New: Plus** | An optional skirmish rule set: a reworked economy and research list, the Building Academy, counter-battery fire, a smarter computer army and call-in strikes. | [Entente Engine Plus](docs/PLUS.md) |

<p align="center">
  <img src="https://github.com/supm3n/trench-engine/releases/download/v2026.1/screenshot-town.jpg" alt="A town" width="32%">
  <img src="https://github.com/supm3n/trench-engine/releases/download/v2026.1/screenshot-airfield.jpg" alt="An airfield" width="32%">
  <img src="https://github.com/supm3n/trench-engine/releases/download/v2026.1/screenshot-coast.jpg" alt="A coast" width="32%">
</p>

## Performance at a glance

The original program and Entente Engine on the same saves of a 4 against 4 skirmish, on the same PC.

**Pictures a second** at normal game speed, still camera (longer is better):

```
Small game,    1,383 units   original        ████                            32
                             Entente Engine  ██████████████████████████████  234
Huge battle,  86,415 units   original        ████                            30
                             Entente Engine  ██████████████                  110
```

**Stutter** on the huge battle, full screen at 3840 x 2160: pictures more than 50 ms late, per minute (shorter is
better):

```
Watching                     original        ███████                         22
                             Entente Engine  █                               2.7
After "select all, move"     original        ██████████████████████████████  92
                             Entente Engine  ███                             8.3
```

**A big order:** 16,000 units told to move start walking within about a second, instead of over 2 min 38 s.

**Game speed:** full speed is 33 game steps a second. Entente Engine holds it on every save; the original runs at
about 32, and 30 on the huge battle. Entente Engine also needs about half the original's time to work out each step.

Fair to know: the original shows one picture per game step by design, so about 30 a second. Entente Engine draws
between the steps with the movement blended, so motion looks smoother, most of all on high refresh screens. The
pictures-a-second numbers were measured without vsync; when you play, the picture follows your screen's refresh rate.
One PC, an AMD Ryzen 9 9950X3D, 1280 x 1024 unless noted. All numbers and how they were measured:
[docs/FEATURES.md](docs/FEATURES.md#performance).

## Verifying your download

Each release lists the SHA-256 of `EntenteEngine.exe`. In PowerShell:

```powershell
Get-FileHash .\EntenteEngine.exe -Algorithm SHA256
```

The release build is reproducible: the same source and the same toolchain give the same bytes, whatever folder you
build in. So you can also build the release yourself and compare the two fingerprints
([how](docs/BUILDING.md#check-a-release-against-your-own-build)).

Entente Engine never updates itself; new versions are downloaded by hand from the Releases page. At each start it
asks GitHub (api.github.com) whether a newer version is out and, if so, only tells you. To turn that off, set
**Check for updates** to Off in Game options. More in [SECURITY.md](SECURITY.md).

## Building from source

The exe is built with a pinned toolchain (CMake, Ninja and llvm-mingw, each checked by SHA-256) and two commands on
Windows 10 or 11. See **[docs/BUILDING.md](docs/BUILDING.md)**.

## Licence

Entente Engine's own code is under the **PolyForm Strict License 1.0.0** ([LICENSE](LICENSE)). In plain words: you
may use it for any non-commercial purpose, such as playing the game and reading and checking the code. You may not
share copies of it (please share a link to this page instead) and you may not make changed versions. The licence
covers our code only. The game's files belong to their owners, and the libraries we use keep their own licences
([THIRD_PARTY.md](THIRD_PARTY.md)).

## Bugs and ideas

Bug reports and ideas are welcome on the [Issues page](https://github.com/supm3n/trench-engine/issues). Because of
the licence, we cannot accept pull requests with code for now; see [CONTRIBUTING.md](CONTRIBUTING.md).

## Not affiliated

Entente Engine is an independent fan project. It is **not affiliated with or endorsed by Lesta or ESDigital**. All
rights to The Entente Gold belong to their owners.

"Antanta" (the original program's own name) appears in this code only as an internal name for folders, build targets and
namespaces. The product is Entente Engine.

## Credits

Made by supm3n with Claude (Anthropic).

The Entente Gold was made by Lesta Studio. This project exists because it is a game worth playing again.

Entente Engine is built on free software made by other people: SDL, SDL_ttf, SDL_mixer, libogg and libvorbis,
FreeType, zlib, Lua, fdlibm, pl_mpeg, IXWebSocket and the Black Ops One and Liberation fonts. Thank you. The list with
versions and licences is in [THIRD_PARTY.md](THIRD_PARTY.md).
