# What Entente Engine fixes and improves

Entente Engine plays The Entente Gold by the original's rules, with the original's maps, pictures, sounds and
missions. Where the original has a bug, crashes or loses something, Entente Engine fixes it instead of copying it. Changes to
gameplay or balance are kept small and are listed here; bigger changes live only in the optional
[Entente Engine Plus](PLUS.md) rule set.

To be fair to the original: it is a game made in 2003 for the PCs of 2003, and most of the problems below are small
or show only in rare cases.

- [Runs on today's Windows](#runs-on-todays-windows)
- [Crashes and freezes](#crashes-and-freezes)
- [Saves](#saves)
- [Gameplay quirks fixed](#gameplay-quirks-fixed)
- [Missions and campaigns](#missions-and-campaigns)
- [Interface and comfort](#interface-and-comfort)
- [Pictures](#pictures)
- [Sound and video](#sound-and-video)
- [Multiplayer](#multiplayer)
- [Languages](#languages)
- [Performance](#performance)
- [Kept as the original on purpose](#kept-as-the-original-on-purpose)

## Runs on today's Windows

| The original | Entente Engine |
|---|---|
| Needs a DirectDraw wrapper on Windows 10 and 11 | One exe that uses only Windows' own libraries: no wrapper, no installer, no admin rights |
| Five screen sizes, all 4:3, up to 1600 x 1200 | Any window size, widescreen and full screen; the game view uses the whole window |
| Saves and settings in the game's folder (under Program Files they end up in a hidden Windows folder) | Everything in your own user folder, `%APPDATA%\EntenteEngine`; the game's folder is never written |
| The error log is wiped at every start | A log per session (the last 10 are kept) and a short crash report you can send us |

## Crashes and freezes

Crashes and hangs of the original that cannot happen in Entente Engine:

- **The frozen mouse.** In the original the pointer and clicks can stop working while the music plays on, until you
  switch to another window and back. Entente Engine reads the mouse the ordinary Windows way, so this cannot happen.
- **The intro crash.** The original now and then dies during the intro. Here the videos play inside the game's own
  window, with nothing shut down and restarted in between.
- **The 8-player start crash.** Starting 8 players on the map "newhouses" crashed the original at random. Now a map
  gets only as many players as it has starting places, and every player gets a real one.
- **A broken map in the list.** A map whose file does not load is left out of the list, and Start never opens a game
  whose map did not load.
- **A damaged save.** Our own saves carry a format number and checks for every part, so a broken file gives a message
  and the game goes on.
- **Mission scripts.** A mission script naming an unknown group, player or unit is noted in the log and the mission
  goes on.
- **A missing ship picture.** When the Russian cruiser's picture file is missing, the cruiser uses the picture of the
  Russian ship Rurik.
- **A mission that never ends.** When your last unit is gone in a scripted mission, you are beaten and see the
  statistics, instead of the game running on with no message.

## Saves

The original's saves lose or change several things on loading. Entente Engine's own save format keeps everything:

- Research results (accuracy, shell spread, gas and medical research) are kept for every player.
- A game saved and loaded plays on exactly as if it had never been saved: the unit limit, unit numbers, orders in
  progress, weapon targets, routes, gas clouds, fog of war, control groups, the selection, the camera, mission
  progress and mission scripts are all stored.
- Each save says which format and version it is; a newer or broken file gives a message.
- A save name with a character Windows does not allow is refused with a message instead of silently not saving.
- Campaign progress has its own file with full mission names (in the original, winning "russia_1_1" could also mark
  "russia_1_10" as won).
- "Time played" counts only the time the game runs, not pauses or open menus.
- Saves of the original still load, the original's way.

## Gameplay quirks fixed

**The computer and your advisors**

- A Difficult computer's second barracks trains its mortar men instead of queueing them into the first.
- A computer's half-full transport no longer waits forever for soldiers who cannot reach it, and ships no longer
  lose their way near their target.
- A Difficult computer that lacks a required building builds it first, instead of asking for the wrong one.
- A destroyed first Academy of Sciences no longer ends research for good: the first Academy that stands is used.
- The computer's units no longer give up their orders when more than 100 wait for a route in one game turn.
- The computer rebuilds a map's own starting mines when they are lost.

**Rules and orders**

- **Big orders start at once.** In the original, a right click on 16,000 units let them search their routes a small
  batch at a time: the last one started walking 2 min 38 s after the click. In Entente Engine every unit starts
  within about a second. (Both measured in our engine, which can run the original's route rule for comparison.)
- **"Form group" on many units** splits them all into groups of up to 126 (300 units make 126 + 126 + 48) instead of
  leaving most of them standing. Riflemen go in front, guns behind, workers last. A formed group marches at its
  slowest member's speed.
- **Mixed unit types** sent to one place spread on one shared spacing instead of three overlapping grids.
- **Where you look no longer changes the battle.** In the original, whether a shell's hit on a building counted
  could depend on whether that building was on screen. Now every building counts the same, wherever you look.
- **A right click into the fog** on a hidden enemy no longer attacks it: soldiers move there ready to fight whatever
  they meet, workers simply walk. The pointer shows what the click will really do.
- **Clicking dark ground** no longer shows the name and health of a hidden enemy.
- **Pontoon bridges:** a bridge across a ship channel is refused (the preview shows the refused spots in red and a
  message says why) instead of being laid and paid for with a hole; a bridge is laid whole or not at all, with a
  message naming the wood needed; workers build the nearest section they can reach, so no middle section is left
  unbuilt; the preview shows exactly the sections that will be laid.
- **Cancelling** the unit in training keeps the power use right; a cancelled ship gives its place in the unit limit
  back.
- **The market** sends a gift to the player you picked (with a nameless player in the game, the original could send
  it to someone else).
- **Cavalry research:** the special horseman gets its own strength boost, and speed research counts only for the
  player who did it (in the original it sped up the cavalry of every player of that nation).
- **Aircraft:** new planes fly to the hangar's rally point, as every other building's units do. Planes that scramble
  because their hangar is attacked stay to defend it. A fighter on a move order attacks enemy planes it meets on the
  way; a fighter sent at a ground unit turns on a plane that shoots at it, then goes back to its target.
- **Clicking an enemy plane** drawn over your own units shows the plane's information instead of selecting your unit under it.
- **Mines near the map's edge:** an iron or gold deposit near the edge can now get its mine (the original's edge rule
  refused every mine there).
- **The unit limit** cannot wrap round any more: each dwelling upgrade adds exactly the places it later takes away,
  and the limit stops at 0 and at 65,535.

## Missions and campaigns

- An enemy's win in a mission counts as your loss and its loss as your win (the original had it the wrong way round;
  none of the game's own missions were affected).
- Mission timers, area checks near the map's edge, handing units and buildings to another player, and mission groups
  work as the missions mean them to.
- **Champagne:** "losses too big" now ends in your loss, not the victory horn.
- **Marne:** the timer marks the first task done only while it is still open, so no stray second attack wave comes.
- **Nivel:** a leftover test line ("We have : N units.") is no longer shown on screen.
- When a scripted mission is won, the winner's allies stay alive and your ally's win is yours.
- No task message after the end message.
- A barracks handed over in a mission trains the unit its button shows.
- The campaign lists select the next mission to play, not one you already won.

## Interface and comfort

- **Any window size,** widescreen and full screen. The menus keep their shape with bars at the sides. The window
  remembers its size, place and whether it was maximised; Full screen is in the Screen Resolution box, and Alt+Enter
  switches at any time.
- **Interface scale** Auto, 1x to 4x, for big screens (Auto gives double size on 2560 x 1440 and 4K). Text is always
  drawn sharp at full size.
- **Mouse wheel zoom** around the pointer, the main-row minus and equals keys, and Backspace back to your start zoom
  (see [INSTALL.md](INSTALL.md#options-worth-knowing)).
- **Smooth scrolling** instead of hopping one square at a time, units drawn smoothly between game steps, and dragging
  the map with the middle mouse button. The scrolling sliders work over their whole range.
- **One fixed game clock:** in the original the game ran faster while you held the minimap. The number pad's plus and
  minus set the game speed: normal, double, four times.
- **Clicking buildings:** any visible part of a building selects it (in the original a click on a roof or tower often
  did nothing).
- **Keys:** Shift with a panel letter works; pressing a group key twice centres the view; the main menu's letter keys
  work in either case; Esc cancels fire at a point; a click, Esc, Space or Enter skips a video.
- **The building preview** shows fields only where they will grow, the building where it will stand, and no stream of
  refusal sounds while you drag.
- **Mission messages** come one at a time, oldest first, and can all be read again in the mission window (F1).
- **Settings sliders** come back exactly as you left them (the original lost a step each time you pressed Ok).
- **Player profiles:** typing a name that exists picks that profile instead of renaming the chosen one; empty names
  are not added.
- **Windows you drag** follow the pointer, also over their own buttons and at the screen's edge.
- **Order queues:** Shift + right click plans several orders in a row for the selected units.
- Pausing keeps the selection panel open. Holding Shift and clicking one of your units adds it to the selection or
  takes it out, once (the original kept switching it while the button was held).

## Pictures

- No flicker or half-drawn units while scrolling: the whole picture is drawn fresh every frame.
- The fog of war lines up with the ground you see (in the original it sat about 116 pixels too high).
- The minimap shows each kind of ground in its own colour (the original's colours were shifted by one).
- Shell craters follow the ground, resource deposits are drawn right, explosions and smoke stand still while paused,
  falling leaves no longer sink into hills, fields and wells animate at one steady pace, and Austria's depot smoke
  shows.
- Water and coast foam stay put on the map while you scroll, and a ship's reflection no longer lies over its
  neighbours.

## Sound and video

- A collapsing building plays its collapse sound (the original played the science academy's).
- Half of the Russian artillery's voices are heard again, and the British hear their anthem (both were misspelt file
  names in the original).
- Menu clicks are no longer cut short.
- The music comes back after the Medal button with videos switched off.
- Videos keep their shape at any window size.

## Multiplayer

| The original | Entente Engine |
|---|---|
| DirectPlay, an optional part of Windows 10 and 11 that is off by default | Its own network play: games on your network are listed, or join by address, with an optional password |
| A network turn of 50 or 100 ms, so 60 % or 30 % of normal speed at most (from the original's design, not timed in a real game) | The host picks the speed; 8 players held full normal speed (33.3 game steps a second) in our test of 8 game windows on one PC |
| A dropped player is out, the whole army removed | The seat is kept for 5 minutes (a setting) while the computer plays it; the player rejoins and takes it back |
| A player who leaves is out | The computer plays that side to the end |
| No autosave, F5 refused | Autosave every 1 to 15 minutes (default 5, last 3 kept); F5 works too |
| A saved network game cannot be continued | It can, and everyone gets their own seat back |

Every player's PC runs the same game and only the orders travel over the network. The host can be reached over IPv4
and IPv6.

## Generated maps

The first entry of the skirmish map list makes a new map for 2 to 8 players: land, Sea or Islands, with Mountains,
Dense forest or Winter, in Normal or Large size. Every map is checked to be fair before you see it: the same
resources for every player at about the same distances, about as much land each and, on water maps, about as far
to the sea. A map's short code makes the same map again, also in a network game.

## Languages

Entente Engine's own menus, windows and messages are in English, German, Italian and Russian, and so are the few
words painted on the game's buttons. Mission texts and briefings come from your copy of the game (English on Steam).

## Performance

All numbers below were measured on one PC: an AMD Ryzen 9 9950X3D with 16 cores, at 1280 x 1024 unless noted,
often with other work running, so single runs vary. Please read them as what they are, not as a promise for your PC.

**The original's own program against Entente Engine, the same four saves** (a 4 against 4 skirmish with Difficult
computers, saved at four points; medians of 3 rounds):

| Units on the map | Speed, original | Speed, ours | Turn time, original | Turn time, ours |
|---|---|---|---|---|
| 1,383 | 32.0 | 33.4 | 0.42 ms | 0.25 ms |
| 11,768 | 32.1 | 33.4 | 2.11 ms | 1.14 ms |
| 52,160 | 31.8 | 33.4 | 8.75 ms | 4.39 ms |
| 86,415 | 29.8 | 33.4 | 13.48 ms | 7.33 ms |

Speed is in game steps a second at normal speed; full speed is 33.3. Turn time is how long the program needs to work
out one game turn.

**Pictures a second on the same saves** (normal game speed, medians of 3 rounds):

| Units on the map | Still camera, original | Still camera, ours | Scrolling, original | Scrolling, ours |
|---|---|---|---|---|
| 1,383 | 32 | 234 | 63 | 224 |
| 11,768 | 32 | 223 | 53 | 208 |
| 52,160 | 32 | 120 | 35 | 132 |
| 86,415 | 30 | 110 | 33 | 82 |

Ours was measured in a window without vsync, so it drew as many pictures as it could; when you play, it follows your
screen's refresh rate. The original's higher numbers while scrolling come from one of its bugs: edge scrolling skips
its frame limiter, so the whole game runs faster while you scroll (Entente Engine keeps one steady game clock).

What this means, fairly:

- The original is **not slow at its own job** on a current PC. It keeps nearly full speed up to about 52,000 units
  and loses about 11 % at 86,000. Entente Engine keeps full speed on all four saves.
- Entente Engine needs **about half the time** to work out a game turn.
- The original draws one picture per game step by design, so it shows 30 to 32 pictures a second. Entente Engine draws
  between steps with moving things blended: 110 to 234 pictures a second on these saves with a still camera. Per
  picture, the original's drawing is cheaper with a still camera; ours is cheaper while scrolling.
- **Stutter** on the 86,415-unit save, full screen at 3840 x 2160, counted as pictures that came more than 50 ms
  late: 22 times a minute in the original and 2.7 in Entente Engine; after selecting everything and ordering a move
  across the map, 92 times a minute in the original and 8.3 in Entente Engine.
- Entente Engine uses more memory with its pictures (514 to 1,042 MB against the original's 444 to 801 MB).

**Other speed points:**

- **Big orders:** 16,000 units start walking within about a second instead of over 2 min 38 s (see
  [Rules and orders](#gameplay-quirks-fixed)).
- **Several cores:** Entente Engine draws on several cores and, on PCs with 4 or more logical processors, works out
  the next game turn on a core of its own while the picture is drawn (Game options can switch this). On a save with
  67,365 units this took the average frame from 7.7 to 5.7 ms and the slowest 1 % of frames from 22.5 to 12.9 ms. The
  game itself is the same either way.
- **Smaller PCs:** the same huge save, with the PC held to fewer cores, ran at about 13 pictures a second on 1 core,
  35 on 2 and 80 on 4. A normal-sized game took 20.9 ms a frame on 1 core and 10.9 ms on 2.

## Kept as the original on purpose

- The building outlines' thin right edge, as the original draws it.
- The original's skirmish list (maps that are unfinished stay hidden).
- Under the standard rules the computer sends one group per attack, and leftovers join the next wave.
- No extra unit limit beyond the original's (the houses you build set it).
- In network games, orders that several players give at the same moment are carried out one game step after another,
  a fraction of a second apart, as the original's order numbering needs.
- In missions, an ally's defeat still ends your game where the mission says so, because some missions rely on it.
