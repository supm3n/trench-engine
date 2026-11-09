# Changelog

Versions are numbered by year: 2026.1 is the first release of 2026, 2026.2 the next one.

## 2026.2

- **Generated maps:** the first entry of the skirmish map list makes a new map for 2 to 8 players. Choose land, Sea
  or Islands, Mountains, Dense forest, Winter and Normal or Large size; **New map** draws another one. Every map is
  checked to be fair: every player gets the same resources at about the same distances, about as much land and, on
  water maps, about as far to the sea. Each map has a short code: **Copy** it and give it to friends, and the same
  code always makes the same map. Network games can use generated maps too.
- **Order queues:** Shift + right click plans several orders in a row for the selected units.
- **Computers across water:** a computer's half-full transport no longer waits forever for soldiers who cannot reach
  it, and ships no longer lose their way near their target. With Plus the computer also builds harbours more often
  and lands troops on other islands earlier.
- **Network games:**
  - **Joining fixed:** a player whose game options file differed from the host's could not join; now they can.
  - **IPv6:** the host can now be reached over IPv6, which many cable and mobile lines need.
  - **Clearer messages:** when joining fails, the message says why and what to check (firewall, port forwarding).
  - **Steadier rejoin:** rejoining a long game is less likely to drop on a slow line.

## 2026.1 (the first public release)

The first public release of Entente Engine, for Windows 10 and 11 (64-bit). You need your own copy of
The Entente Gold.

- **The whole game:** skirmish, the missions and the campaigns, with the original's maps, pictures, sounds and rules,
  on today's Windows without a compatibility wrapper.
- **Fixes:** crashes and freezes of the original that cannot happen here (among them the frozen mouse and the
  8-player start crash), saves that keep everything, and many gameplay, mission, drawing and sound bugs.
  See [FEATURES.md](docs/FEATURES.md).
- **Comfort:** any window size and full screen, interface scale for big screens, smooth scrolling, mouse wheel zoom,
  menus in English, German, Italian and Russian.
- **Extra maps:** maps from other versions of the game, put into an `Extras` folder next to the exe, appear at the end
  of the skirmish list. See [INSTALL.md](docs/INSTALL.md#extra-maps).
- **Network games** for up to 8 players on your network or by address, with rejoin after a drop, the computer taking
  over, autosaves and continuing a saved game.
- **Performance:** full game speed on the biggest save we measured (86,415 units), about half the original's time per
  game turn, and much less stutter.
- **Entente Engine Plus**, an optional skirmish rule set: economy and research rework, the Building Academy,
  counter-battery fire, the computer's army brain and call-in strikes. See [PLUS.md](docs/PLUS.md).
- **Safety:** never updates itself; at each start it only asks GitHub whether a newer version is out (switch this off
  with **Check for updates** in Game options). Reproducible release build, with its SHA-256 on the Releases page.
