# Installing and playing Entente Engine

This guide takes you from the download to your first game, and helps when something does not work.

- [What you need](#what-you-need)
- [Getting the game](#getting-the-game)
- [Installing](#installing)
- [The Windows warning on the first start](#the-windows-warning-on-the-first-start)
- [Finding your copy of the game](#finding-your-copy-of-the-game)
- [The first start](#the-first-start)
- [Where your saves and settings live](#where-your-saves-and-settings-live)
- [Options worth knowing](#options-worth-knowing)
- [Playing over the network](#playing-over-the-network)
- [Updating](#updating)
- [Uninstalling](#uninstalling)
- [Troubleshooting](#troubleshooting)

## What you need

- **Windows 10 or 11, 64-bit.** Windows 7 and 8 were not tried. Linux through Wine or Proton and the Steam Deck were
  not tried either.
- **Your own copy of The Entente Gold.** Entente Engine contains none of the game's files: no pictures, sounds, maps or mission texts. It reads them from your own copy of The Entente Gold. To play the same game, the code does include some numbers from the original program (such as research prices and shell values) and the few words painted on the game's buttons, which it translates for the menus.
- **The fonts Times New Roman Bold and Arial.** They come with Windows.

There is nothing else to install: no runtime, no DirectX package, no compatibility wrapper, and no administrator
rights are needed.

## Getting the game

The Entente Gold is sold on Steam. Install it as usual through Steam. Entente Engine does not need Steam to be running;
it only reads the installed files.

You do not need to start the original game first, and you do not need to change anything in its folder.

## Installing

1. Open the **[Releases page](https://github.com/supm3n/trench-engine/releases)** and download the newest version.
2. Check the download's fingerprint if you like (see [Verifying your download](../README.md#verifying-your-download)).
3. Make a folder of your own, for example `Documents\Entente Engine`, and put `EntenteEngine.exe` into it.
4. Double-click `EntenteEngine.exe`. You may want to pin it to the Start menu or make a shortcut on the Desktop.

Please keep in mind:

- **Do not put it under Program Files** or another protected Windows folder (Steam installs games under Program Files
  by default). A folder of your own is simpler and avoids Windows' write protection.
- **Keep the name `EntenteEngine.exe`.** Never rename it to `Antanta.exe` and never copy it into the game's folder
  under that name: `Antanta.exe` is the original game's own program, and the two would replace each other.

## The Windows warning on the first start

Entente Engine is new and not signed with a paid certificate, so on the first start Windows may show a blue box
saying **"Windows protected your PC"**.

1. Click **More info**.
2. Click **Run anyway**.

Some antivirus programs may ask as well. This normally happens only the first time. If you want to be sure the file
is the real one before you click, compare its SHA-256 with the one on the Releases page first.

## Finding your copy of the game

Entente Engine looks for the game by itself, in this order:

1. The folder `EntenteEngine.exe` sits in.
2. Your Steam installation and every Steam library folder, on any drive.
3. The folder you chose the last time.
4. Windows' list of installed programs, so a copy installed from a disc or another shop is found too.

If none of these holds the game, a message explains what is needed and a folder picker opens. Choose the game's
folder, the one that contains the `Data` folder. For a Steam copy that is usually:

```
C:\Program Files (x86)\Steam\steamapps\common\The Entente Gold
```

In Steam you can also right-click the game, then **Manage**, **Browse local files** to open it. If you pick a wrong
folder, the message names the file that is missing and the picker opens again. Entente Engine remembers the folder
you chose.

## The first start

The main menu opens. Your version number is in its bottom right corner, for example "Entente Engine 2026.1".

- **Single Player** has the skirmish (Random Maps), the missions and the campaigns, as in the original.
- **Network game** hosts or joins a game with other players (see below).
- **Options** has the settings, and **Game options** inside it has the new ones described below.

The menus follow your Windows language where Entente Engine has a translation (English, German, Italian, Russian);
you can change it in Game options. Mission texts and briefings come from your copy of the game (English on Steam).

## Where your saves and settings live

Everything Entente Engine writes goes into your own user folder:

```
%APPDATA%\EntenteEngine
```

Paste that into the address bar of a File Explorer window to open it. It holds your saves, profiles, settings,
campaign progress, the remembered game folder, the session logs (in `logs`, the last 10) and any crash reports.
Nothing is ever written into the game's folder.

To back up your games, copy that folder.

## Options worth knowing

In the main menu: **Options**, then **Game options**.

| Option | What it does |
|---|---|
| **Interface scale** | Auto, 1x, 2x, 3x or 4x. Auto shows normal size on a 1920 x 1080 screen and double size on 2560 x 1440 and 4K screens. |
| **Interface pictures** | How the frame and buttons look when scaled up: In between (sharp with soft edges, the default), Crisp or Smooth. Text is always drawn sharp. |
| **World zoom** | How big the game world is drawn when a game starts. Auto follows the interface scale. |
| **Rules** (on the skirmish setup screen) | Entente Engine (the standard rules) or [Entente Engine Plus](PLUS.md). Your choice is remembered. |

**Zoom during a game:**

- **Mouse wheel** over the map zooms in and out in small, smooth steps. The spot under the pointer stays under it.
- **Minus** and **equals** (or plus) on the main row of the keyboard zoom out and in around the middle of the screen.
- **Backspace** goes back to the zoom from Game options.
- You can zoom in up to 4x. Zooming out stops when the screen shows about as much of the map as a 2560 x 1440 screen
  at 1x. In a network game that limit is the same for every player.
- The wheel zoom lasts for the game you are playing and is not saved.

**Other things to know:**

- The plus and minus keys on the **number pad** set the game speed (normal, double, four times).
- **Drag the map** with the middle mouse button (press the wheel and move the mouse).
- The scrolling speed sliders in Game options now work over their whole range.

## Playing over the network

Up to 8 players can play together. Everybody needs **the same version** of Entente Engine and their own copy of the
game.

- **On the same home network:** in the main menu click **Network game**. Games on the same network show up in the list
  by themselves. To host, click **Create** and set up the game; the others pick it from the list and click **Join**.
  A password is optional.
- **Over the internet:** the host gives the others their internet address, and the others type it on the first page.
  The host's router must forward **TCP port 47625** to the host's PC.
- **The Windows Firewall:** the first time you host or look for games, Windows asks whether Entente Engine may use the
  network. Allow it for private networks; without it nobody can join and the list stays empty. Entente Engine uses
  **UDP port 47624** to find games on your network and **TCP port 47625** for the game itself.
- If a player's connection drops, their seat is kept for 5 minutes (a setting) and the computer plays it meanwhile;
  they can rejoin and take it back. A player who leaves is replaced by the computer. Network games autosave, and a
  saved network game can be continued from the network page.

Entente Engine connects to nothing else: only the network games you start, and while you look for games it asks on
your home network (a broadcast) which games are there.

Please note: the game password is sent without encryption, so do not use a password you use anywhere else.

## Updating

Entente Engine never updates itself. When it starts, it asks GitHub which version is the newest one on the Releases
page. If there is a newer one than yours, a small window tells you so, and its button opens that page in your
browser. Nothing is downloaded or installed for you. You can also click **Check for updates** in the bottom right
corner of the main menu. To stop the question at the start, set **Check for updates** to Off in Game options.

To update:

1. Download the new version from the [Releases page](https://github.com/supm3n/trench-engine/releases).
2. Replace your old `EntenteEngine.exe` with the new one (same name, same folder).

Your saves, settings and progress in `%APPDATA%\EntenteEngine` stay as they are. In network games, everybody needs to
update to the same version.

## Uninstalling

1. Delete `EntenteEngine.exe` (and the folder you made for it).
2. If you also want to remove your saves and settings, delete `%APPDATA%\EntenteEngine`.

Your copy of The Entente Gold is not touched either way.

## Troubleshooting

**"The game could not be found" and a folder picker opens.**
Choose the folder that contains the game's `Data` folder (see [Finding your copy of the game](#finding-your-copy-of-the-game)).
If you chose a wrong folder, the message names the missing file.

**"Entente Engine could not start" with a message about fonts.**
Entente Engine needs Times New Roman Bold (`timesbd.ttf`) and Arial (`arial.ttf`) in `C:\Windows\Fonts`. They come
with Windows, but a trimmed-down Windows installation may lack them. Putting the two fonts back fixes it.

**Windows says "Windows protected your PC" or the antivirus blocks the file.**
See [The Windows warning on the first start](#the-windows-warning-on-the-first-start). Check the SHA-256 first if you
want to be sure the file is the real one.

**The intro and other videos do not play.**
The game's videos are skipped in this release. Everything else plays as usual.
<!-- TODO for the owner: confirm the video situation of the public build; docs/release.md says friends have no
     converted videos, so the intro and credits are skipped. Remove this item if that changes. -->

**Nobody can join my network game, or the list of games stays empty.**
Allow Entente Engine in the Windows Firewall for private networks (Windows Security, Firewall and network
protection, Allow an app through the firewall). Over the internet, forward TCP port 47625 on the host's router. A PC
with several networks (a VPN or virtual network adapters) may not find games on the other network; typing the host's
address always works. Check that everybody has the same version.

**Entente Engine crashed.**
It saves a short report named `crash_<date>_<time>.txt` in `%APPDATA%\EntenteEngine` and tells you where. Please open
a [bug report](https://github.com/supm3n/trench-engine/issues/new/choose), say in a few words what you were doing, and
attach that file and the newest file from `%APPDATA%\EntenteEngine\logs`.

**A save made with a newer version does not load.**
Each save says which version of the format it is. An older Entente Engine refuses a newer save with a message
instead of crashing. Update to the newest version.

**Something else looks wrong.**
Please tell us on the [Issues page](https://github.com/supm3n/trench-engine/issues). The newest file in
`%APPDATA%\EntenteEngine\logs` helps us find the cause.
