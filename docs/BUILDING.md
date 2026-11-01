# Building Entente Engine from source

The release exe is built on Windows with a pinned toolchain. The same source and the same toolchain give the same
bytes, so you can check a release against your own build.

## Requirements

- Windows 10 or 11, 64-bit, with PowerShell (built in).
- An internet connection for the first step only. No administrator rights, no Visual Studio, no other compiler.

## The pinned toolchain

[`engine/toolchains.txt`](../engine/toolchains.txt) lists each tool with its exact download address and SHA-256 (taken
from the GitHub release digests):

| Tool | Version | File |
|---|---|---|
| CMake | 4.4.4 | `cmake-4.4.4-windows-x86_64.zip` |
| Ninja | 1.13.2 | `ninja-win.zip` |
| llvm-mingw (clang for Windows, x86_64, UCRT) | 20260922 | `llvm-mingw-20260922-ucrt-x86_64.zip` |

Change nothing in this list if you want to reproduce a release: a different compiler version gives a different exe.

## Build

From the top folder of this repository, in a Command Prompt or PowerShell window:

```
powershell -NoProfile -ExecutionPolicy Bypass -File engine\scripts\fetch-toolchains.ps1
cmd /c "call engine\scripts\env-clang.cmd && cmake -S engine -B engine\build\release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DANTANTA_RELEASE=ON && cmake --build engine\build\release --target antanta"
```

1. The first line downloads the three tools into `toolchains\dl\`, checks each SHA-256 against
   `engine/toolchains.txt` and unpacks them into `toolchains\<name>-<version>\`. It stops if a hash does not match.
2. The second line puts the tools on the PATH for this one command, configures and builds. The result is
   `engine\build\release\EntenteEngine.exe`: one file, linked statically, that needs only Windows' own DLLs.

Both `toolchains\` and `engine\build\` are ignored by git.

### Build options

| Option | Default in this repository | What it does |
|---|---|---|
| `ANTANTA_RELEASE` | `OFF` | `ON` gives the release exe: `EntenteEngine.exe`, statically linked, no console window, reproducible. `OFF` gives a developer build, `antanta.exe`, with a console window for its log. |
| `ANTANTA_PUBLIC` | `ON` | The public build. Keep it `ON`: this repository does not contain the parts it leaves out. |

A developer build run from its own build folder can use the Liberation fonts in `engine/third_party/` when Times New
Roman Bold or Arial is missing; the release exe uses the Windows fonts only.

To play your build you need your own copy of The Entente Gold, exactly as with a downloaded release
([INSTALL.md](INSTALL.md)).

## Reproducible build

The release build leaves out everything that would differ between two builds:

- no build date or time inside the program;
- source and build folders mapped to fixed names (`-ffile-prefix-map`), so the folder you build in does not matter;
- no time stamp in the exe's header (`-Wl,--no-insert-timestamp`);
- the exact toolchain above.

Two builds of the same source in different folders have given byte-identical exes.

## Check a release against your own build

1. Check out the tag of the release, for example `git checkout v2026.1`.
2. Build as above, with `-DANTANTA_RELEASE=ON`.
3. Compare the fingerprint of your exe with the one on the Releases page:

```powershell
Get-FileHash .\engine\build\release\EntenteEngine.exe -Algorithm SHA256
```

The two values must be the same. If they differ, first check that the toolchain matches `engine/toolchains.txt` and
that the working tree is clean (`git status`). If they still differ, please tell us in an issue.

## Code layout

| Folder | What is in it |
|---|---|
| `engine/apps` | the game program |
| `engine/sim` | the simulation: units, buildings, combat, the computer players, the rules |
| `engine/formats` | readers for the game's file formats |
| `engine/render` | drawing the world |
| `engine/ui` | menus, the in-game interface, the fonts |
| `engine/script` | the Lua mission scripts |
| `engine/net` | network games |
| `engine/platform` | Windows, files, sound, the session log |
| `engine/lang` | our own texts in English, German, Italian and Russian |
| `engine/third_party` | the libraries, unchanged, each with its licence ([THIRD_PARTY.md](../THIRD_PARTY.md)) |
| `engine/scripts` | the toolchain download and environment scripts |
