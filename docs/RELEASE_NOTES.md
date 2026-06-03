# Ferris-AO Switch — v1.05

**First stable release.** A Nintendo Switch homebrew client for
[Attorney Online 2](https://attorneyonline.de/). Connect to any standard AO2
server, pick a character, and play — characters, backgrounds, music and sound all
stream from the server, so there is nothing to pre-download.

Runs as a single `.nro` on real modded hardware (Atmosphère) and on the Ryujinx
emulator — the same binary.

## Fixed in this release
- **Music plays.** BGM is now decoded up front and played through the mixer's
  working channel path. The streaming-music path is silent on the Switch's
  SDL2_mixer build, so Opus tracks (the AO norm) came out silent before.
- **Blips & SFX play.** Opus sound effects are decoded directly with libopusfile
  (the bundled mixer mis-detects Opus as Vorbis and dropped them).
- **Audio opens at the Switch's native 48 kHz** (opening at 44.1 kHz played as
  silence, worst on Ryujinx).
- **No more crash on disconnect/kick.** A packet-buffer bug crashed the client
  whenever a server dropped it mid-message (e.g. an Akashi "you are sending
  messages too quickly" kick). Fixed.

## Install — modded Switch (Atmosphère)
Copy `ferris-ao-switch.nro` to `/switch/` on your SD card and launch it from the
Homebrew Menu. No firmware keys, title install, or base pack required.

## Running on Ryujinx
Check these three settings — otherwise you get no network, no typing, or no sound:

- **Internet access ON** — *Options → Settings → System → Enable Internet Access.*
  Required to reach servers and stream assets.
- **Keyboard input ON** — *Options → Settings → Input → Enable Keyboard.* Lets you
  type into IC/OOC with your computer keyboard.
- **Audio Backend not `Dummy`** — *Options → Settings → Audio → Audio Backend* →
  **SDL3** (or SoundIO / OpenAL). `Dummy` produces no sound for any game.

Then load the `.nro` via *File → Load Application from File…*

---
*Unofficial homebrew — not affiliated with the official Attorney Online project or
Nintendo.*
