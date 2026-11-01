# Security

## Reporting a security problem

If you find a security problem in Entente Engine, please report it privately: on this repository's **Security** tab,
click **Report a vulnerability**. Please do not open a public issue for it.

<!-- TODO for the owner: switch on "Private vulnerability reporting" in the repository settings
     (Settings, Code security) when the repository is made public. -->

Please include what you found, which version you used (the bottom right corner of the main menu), and the steps to
see it. We are a small hobby project and will answer as soon as we can.

## Checking your download

- **The fingerprint.** Each release on the [Releases page](https://github.com/supm3n/trench-engine/releases) lists the
  SHA-256 of `EntenteEngine.exe`. Compare it with yours in PowerShell:

  ```powershell
  Get-FileHash .\EntenteEngine.exe -Algorithm SHA256
  ```

- **Build it yourself.** The release build is reproducible: the same source and toolchain give the same bytes, so a
  build of the release's tag must have the same SHA-256
  ([docs/BUILDING.md](docs/BUILDING.md#check-a-release-against-your-own-build)).
- Download Entente Engine only from this repository's Releases page.

The exe is not signed with a paid certificate, so Windows SmartScreen warns on the first start
([docs/INSTALL.md](docs/INSTALL.md#the-windows-warning-on-the-first-start)).

## What the program does on your PC

- It reads the files of your own copy of The Entente Gold and never writes into its folder.
- It writes only into `%APPDATA%\EntenteEngine`: saves, settings, profiles, progress, session logs and crash reports.
- It never updates itself and needs no administrator rights.
- **The network:** apart from the check for a new version below, the game only connects for network games you
  start. While you look for games, it asks on your home network (UDP port 47624) which games are there; a game you
  host listens on TCP port 47625. Network games between players are not encrypted, and the game password travels in
  plain text, so do not reuse a password from anywhere else.
- **The check for a new version:** at each start, and when you click **Check for updates** in the main menu, it asks
  GitHub (api.github.com) which version is the newest one on the Releases page. It sends nothing about you or your
  games; as with any website, GitHub sees your internet address and which version of Entente Engine asks. It never
  downloads or installs anything: if a newer version is out, it only tells you and can open the page in your browser.
  To turn the check at the start off, set **Check for updates** to Off in Game options.

Crash reports and logs stay on your PC. They are sent to nobody unless you attach them to an issue yourself; please
look through them first if you like.
