# ferris-ao-switch

A full-featured [Attorney Online 2](https://aceattorneyonline.com/) client for Nintendo Switch, written in C++17 using SDL2 and [devkitPro](https://devkitpro.org/). Connects to any standard AO2 server over TCP or WebSocket.

Runs on **real modded Switch hardware** (Atmosphere CFW) and **Ryujinx emulator** — same `.nro` binary, no changes needed.

---

## Table of Contents

- [What is Attorney Online](#what-is-attorney-online)
- [Features](#features)
- [Architecture](#architecture)
- [Build Guide](#build-guide)
- [Running on Ryujinx](#running-on-ryujinx)
- [Running on Real Switch](#running-on-real-switch)
- [Asset Setup](#asset-setup)
- [Connecting to a Server](#connecting-to-a-server)
- [Controller Reference](#controller-reference)
- [Project Structure](#project-structure)
- [Module Reference](#module-reference)
- [AO2 Protocol Notes](#ao2-protocol-notes)
- [Contributing](#contributing)

---

## What is Attorney Online

Attorney Online is a courtroom roleplay game where players take on the roles of lawyers, witnesses, and judges to act out cases. Players communicate through in-character (IC) speech bubbles with character sprites and animations, out-of-character (OOC) chat, music playback, evidence presentation, and health bar management. The AO2 protocol is a lightweight text-based protocol over TCP or WebSocket.

ferris-ao-switch implements the full AO2 client protocol so Switch players can join existing AO2 servers alongside desktop and browser (WebAO) users in real time.

---

## Features

### Core
- **Full AO2 protocol** — IC messages with animations, OOC chat, music, evidence, health bars, rebuttal/realization, pairing, case alerts, mod calls
- **TCP + WebSocket + WSS** — connects to any AO2 server; auto-detects `ws://` (plain WebSocket) and `wss://` (TLS WebSocket via mbedtls) prefixes
- **Dual-platform** — same `.nro` runs on Ryujinx emulator and real Switch hardware (Atmosphere CFW)
- **Any AO2 server** — compatible with Ferris-AO, tsuserver3, Akasha, and any server implementing the standard AO2 protocol
- **Server browser** — fetches the public server list from the Attorney Online master server (`https://servers.aceattorneyonline.com/servers`); master server URL is configurable in-app

### Gameplay
- **Character select** — full grid of server character slots (grayed-out when taken), with **name search/filter**, mouse-wheel and **touch drag-scrolling** for big rosters
- **Quick talk** — a tap-to-talk IC bar (tap it or press Enter to type & send instantly) with inline `< >` emote arrows; the emote isn't reset between lines, so back-and-forth is fast
- **Room switching** — in-courtroom **Rooms** panel lists every area with live player counts, statuses and lock states (ARUP); join one to move rooms without reconnecting
- **IC messages** — typewriter effect, word wrap, per-message text colors (12 colors), shownames, objection/hold-it/take-that popups, realization flash, screenshake
- **Emote picker** — IC composer shows a grid of your character's emotes with sprite-button thumbnails and a live preview of the selected one
- **IC log** — always-on scrollback column showing recent IC lines (showname + colored, word-wrapped message), newest at the bottom; scroll back through history with the mouse wheel or a finger drag
- **OOC chat** — your own messages are highlighted so they're easy to find in the log
- **Pairing** — renders two characters side by side with individual offsets and flip states
- **Evidence panel** — view, present, add, edit, and delete evidence; grid view with thumbnails
- **Music panel** — full server music list; select and play any track; shows currently playing track
- **OOC chat** — scrollable log of OOC messages; send via system keyboard
- **HP bars** — defense and prosecution health bar display, 0–10 scale
- **Narrator mode** — send IC messages without a character sprite

### Assets
- **HTTP & HTTPS streaming** — loads assets on-demand from a server CDN (`ASS` packet); `https://` (TLS) and `http://` URLs both supported; no base pack download needed
- **Async, non-blocking loads** — sprites/backgrounds/music are prefetched on 8 worker threads and decoded from cache on the main thread, so the courtroom never stalls on the network
- **Smart WebP-first probing** — AO2 assets carry no extension on the wire, so the client probes candidates **WebP-first** (the modern AO default), then `.webp.static`/`.png`/`.gif`. It learns the format a server actually uses on the first sprite and then probes **only** that one — collapsing the cold-load 404 storm (was ~5 requests per asset, 4 of which 404) — with an automatic fall back to the full candidate list for any odd/missing asset
- **Decoded-animation cache** — an LRU of already-decoded frame-sets (≈96 MB VRAM budget) keeps recently-seen characters/backgrounds in memory, so when someone who just spoke talks again their sprite re-shows instantly (no re-decode, no re-fetch)
- **Persistent disk cache** — streamed assets are saved to `sdmc:/switch/ferris-ao/cache` (keyed by full URL) and served from SD on the next view/relaunch, so repeat fetches skip the network entirely — pairs with HTTPS keep-alive to make the most of Cloudflare/CDN edge caching
- **No-freeze loading** — char.ini, sprites, audio and music all load off the main thread (your own sprite is pre-warmed on join, audio plays only from cache); the render loop never blocks on the network, so there's no join freeze or IC stutter even on 3000+ character servers
- **Saved settings** — a custom showname, theme, master-server URL and volumes persist across servers and launches (`sdmc:/switch/ferris-ao/config.ini`)
- **Theme import** — drop AO2 theme folders on the SD card and pick them in Settings (applies `courtroom_design.ini` live)
- **Four-tier fallback** — server CDN → community CDN (`attorneyoffline.de/base/`) → `sdmc:/switch/ferris-ao/base/` local pack → `romfs:/` bundled fallback
- **Server background only** — the courtroom streams the server's real background and never substitutes a bundled default courtroom (black until it loads)
- **Sprite reuse** — a character talking line after line never re-downloads or re-decodes its sprite (loads are path-cached)
- **APNG + GIF animations** — character idle, talk, and pre-animations via `IMG_LoadAnimation_RW()`
- **LRU texture cache** — 256-slot cache; all lookups use relative paths as keys, regardless of source
- **1280×720 layout** — matches Switch native resolution in both docked and handheld modes; full-screen courtroom stage with an overlaid chat bar, corner HP bars, a now-playing strip, and an always-on IC log (authentic AO composition, themeable via `courtroom_design.ini`)

### Input
- **Touchscreen** — tap buttons, panels, server/character lists, emote grid; tap the chat box to type a line instantly. **Drag to scroll** long lists (servers, the character grid, music/rooms panels, the IC log) — the handheld equivalent of a mouse wheel. Works in handheld mode (and via mouse on Ryujinx)
- **Joy-Con + Pro Controller** — full D-pad/stick/button mapping
- **System keyboard** — uses libnx `swkbdShow()` for all text entry; works correctly on Ryujinx
- **Keyboard fallback** — arrow keys + Enter + letter shortcuts for desktop/emulator development

### Audio
- **BGM** — plays server music via SDL_mixer with crossfade between tracks
- **SFX** — per-message sound effects with an LRU chunk cache
- **OGG/Opus/WAV** — all formats supported by SDL_mixer portlib

### Networking
- **Background thread** — all socket I/O on a dedicated thread; main thread only reads from a lock-free SPSC queue
- **WebSocket** — custom RFC 6455 implementation (~300 lines, no external dependency); SHA-1 and Base64 inline
- **TLS WebSocket (`wss://`)** — mbedtls (`switch-mbedtls` portlib) for encrypted WebSocket connections; SNI sent; certificate verification disabled (no CA bundle on Switch)
- **Reconnect** — synthetic `__DISCONNECT` notification lets the UI handle drops gracefully

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     Main Thread (60 Hz)                  │
│                                                          │
│  App::run()                                              │
│    ├── SDL_PollEvent → InputManager::handle_event()      │
│    ├── AOClient::process(InQueue)   ← network thread     │
│    │     └── mutates GameState                           │
│    ├── Screen::update(dt_ms)                             │
│    └── Screen::render() → Renderer → SDL_RenderPresent  │
└─────────────────────────────────────────────────────────┘
              ↕ SPSCQueue (lock-free, no mutex)
┌─────────────────────────────────────────────────────────┐
│                   Network Thread                         │
│                                                          │
│  NetworkThread::run()                                    │
│    ├── SDLNet_TCP_Open / ws_upgrade()                    │
│    ├── SDLNet_CheckSockets (1 ms poll)                   │
│    ├── recv bytes → extract AO packets → InQueue.push()  │
│    └── OutQueue.pop() → send bytes (TCP or WS frame)     │
└─────────────────────────────────────────────────────────┘
```

**GameState** is exclusively owned by the main thread. The network thread writes only to `InQueue`; the main thread writes only to `OutQueue`. No mutexes on the hot path.

**Screen stack** (max depth 4) — overlays (OOC panel, music panel, evidence panel, IC input) are pushed on top of `CourtroomScreen` and rendered bottom-up so lower screens show through.

---

## Build Guide

### 1. Install devkitPro

Download and run the [devkitPro installer](https://github.com/devkitPro/installer/releases) for your platform. On Windows, use the MSYS2-based devkitPro pacman environment.

Ensure `DEVKITPRO` is set in your environment (the installer does this automatically):

```bash
echo $DEVKITPRO   # should print /opt/devkitpro (Linux/Mac) or C:/devkitPro (Windows)
```

### 2. Install the Switch portlibs

Open the devkitPro pacman shell (MSYS2 on Windows, or your terminal on Linux/Mac) and install the required packages:

```bash
dkp-pacman -S switch-dev \
              switch-sdl2 \
              switch-sdl2_image \
              switch-sdl2_ttf \
              switch-sdl2_mixer \
              switch-sdl2_net \
              switch-libwebp \
              switch-mbedtls
```

These install the Switch-cross-compiled SDL2 libraries and their dependencies (libpng, libvorbis, libopus, freetype, libwebp, etc.) into `$DEVKITPRO/portlibs/switch/`.

`switch-libwebp` provides both `libwebp` (static/animated WebP decode) and `libwebpdemux` (animated WebP frame extraction). Both are required for full WebP support.

### 3. Clone and build

```bash
git clone https://github.com/SyntaxNyah/ferris-ao-switch.git
cd ferris-ao-switch
make
```

A successful build produces `ferris-ao-switch.nro` in the project root. The devkitPro Makefile handles compilation, linking, and the `elf2nro` step automatically.

**Build outputs:**

| File | Description |
|---|---|
| `ferris-ao-switch.nro` | The Switch homebrew executable |
| `ferris-ao-switch.nacp` | Metadata (title, author, version) |
| `build/` | Intermediate object files |

**Clean:**

```bash
make clean
```

### 4. Compiler flags reference

The Makefile uses these flags for correctness on Switch:

```makefile
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CXXFLAGS := -std=c++17 -O2 -fno-exceptions -fno-rtti $(ARCH)
```

- `-fno-exceptions -fno-rtti` — standard devkitPro practice; reduces binary size
- `-march=armv8-a+crc+crypto` — targets the Switch's Cortex-A57 with hardware CRC and crypto extensions
- `-fPIE` — required for Switch's ASLR

---

## Running on Ryujinx

[Ryujinx](https://ryujinx.app/) emulates Nintendo Switch homebrew correctly, including SDL2, libnx system calls, and the `swkbdShow()` software keyboard applet.

1. Open Ryujinx
2. **File → Open Ryujinx Folder** → navigate to `portable/` (or wherever your Ryujinx data is)
3. Drag `ferris-ao-switch.nro` onto the Ryujinx game list, or use **File → Load Application from File**
4. Ryujinx will boot the `.nro` and present the Connect screen

**Network on Ryujinx:** Ryujinx uses your PC's network stack. Connect to any AO2 server by IP or hostname exactly as you would from a desktop client. `localhost` works if you're running Ferris-AO on the same machine.

**Keyboard on Ryujinx:** The system keyboard applet (`swkbdShow`) is emulated. A text input dialog will appear when you edit a field.

---

## Running on Real Switch

Your Switch must be running **Atmosphere** custom firmware. Homebrew does not work on stock firmware.

> **Important:** Running homebrew in applet mode (via the Album applet) gives limited RAM. Launch via **hbmenu** in title override mode (hold R while launching any game) for full memory access, which is recommended for a media-heavy app like this.

### Steps

1. Copy `ferris-ao-switch.nro` to your Switch's SD card at:
   ```
   sdmc:/switch/ferris-ao-switch/ferris-ao-switch.nro
   ```
   (Or any path under `sdmc:/switch/` — hbmenu scans subdirectories.)

2. Copy your AO base assets to:
   ```
   sdmc:/switch/ferris-ao/base/
   ```
   See [Asset Setup](#asset-setup) for the expected folder layout.

3. Boot into Atmosphere, open **hbmenu**, and launch **ferris-ao-switch**.

4. Use the Joy-Con or Pro Controller to navigate. Press **A** on any text field to open the system keyboard.

---

## Asset Setup

### HTTP Asset Streaming (recommended — no downloads required)

ferris-ao-switch supports **on-demand asset streaming** directly from a server's CDN. When a server advertises an asset URL (via the `ASS` packet during handshake), the client fetches every character sprite, background, music file, and sound effect from that URL as needed — no base pack download required.

**For players:** If the server you're connecting to has a CDN, you don't need to install anything. Just connect and play.

**For server operators:** Set the `asset_url` field in your server's config to point to your file server (HTTP **or** HTTPS). Clients that support it (including ferris-ao-switch) will stream from there automatically. Example:
```toml
[server]
asset_url = "https://cdn.myaoserver.com/base"
```

The client constructs requests as `<asset_url>/<relative_path>`, e.g.:
```
https://cdn.myaoserver.com/base/characters/phoenix/(a)normal.png
```

**HTTPS / TLS CDNs are fully supported.** `https://` asset URLs are fetched over
TLS via mbedtls (`switch-mbedtls`) — the same stack used for `wss://` servers and
the master server list. Plain `http://` works too. There's also a built-in
secondary community CDN (`https://attorneyoffline.de/base/`) that fills in the
classic base pack for servers that only host their own custom characters.

---

### Local Base Folder (optional fallback)

If the server has no CDN, or for offline use, assets can be installed locally. ferris-ao-switch looks for assets in three locations, in priority order:

| Priority | Source | When used |
|---|---|---|
| 1 | Server CDN — `<server asset_url>/<relative>` (HTTP or **HTTPS**) | Server sent `ASS` packet with a URL |
| 2 | Community CDN — `https://attorneyoffline.de/base/<relative>` | Classic base-pack fallback (built in) |
| 3 | `sdmc:/switch/ferris-ao/base/<relative>` | Local base pack on SD card (optional) |
| 4 | `romfs:/<relative>` | Bundled fallback (just the UI font today) |

If the server has no CDN and you have no local base, the client still runs and connects — you can read/send IC and OOC text, browse the music and area lists, and watch HP bars — but character sprites and backgrounds won't appear (nothing to load them from), so the stage stays black behind the chat bar. The courtroom draws its own chat bar, nameplate, HP bars and buttons as primitives, so the UI is fully usable without any art. Point the client at a server with a CDN (most public servers) or drop a base pack on the SD card to get the visuals.

The expected folder structure under `base/` mirrors the standard AO2 base pack:

```
sdmc:/switch/ferris-ao/base/
├── characters/
│   ├── phoenix/
│   │   ├── char.ini
│   │   ├── char_icon.png        ← char-select icon
│   │   ├── (a)normal.png        ← idle sprite  (prefix at char root)
│   │   ├── (b)normal.png        ← talk sprite
│   │   ├── normal.png           ← bare PNG (used for both if no (a)/(b))
│   │   └── emotions/
│   │       └── button1_off.png  ← emote-picker button icons
│   ├── edgeworth/
│   │   └── ...
│   └── ...
├── background/
│   ├── gs4/
│   │   ├── witnessempty.png     ← per-position background
│   │   ├── defensedesk.png      ← per-position desk overlay
│   │   └── ...
│   └── ...
└── sounds/
    ├── music/Turnabout_Sisters.opus
    ├── general/sfx-deskslam.opus
    └── blips/male.opus
```

### Where to get an AO2 base pack

The standard AO2 base pack is distributed with the [Attorney Online 2 desktop client](https://github.com/AttorneyOnline/AO2-Client/releases). After installing the desktop client, copy the `base/` folder from its installation directory to `sdmc:/switch/ferris-ao/base/`.

### Character sprite format

Character sprites follow the AO2 naming convention (matching AO2-Client, AO-SDL,
and webAO). The `(a)`/`(b)` marker is a **prefix** and the sprite lives at the
**character root** — not in an `emotions/` subfolder. Each is probed across the
extensions the server advertises (default order, **WebP-first**: `.webp` →
`.webp.static` → `.png` → `.gif` → `.apng`). The client also **learns** the
format a server actually ships from the first sprite that decodes and probes
only that one afterwards, so the usual ~5-candidate fan-out collapses to a single
request per asset (with an automatic fall back to the full list for an
odd/missing asset). A server's `extensions.json` overrides the default order.

| File | Purpose |
|---|---|
| `characters/<char>/(a)<emote>.<ext>` | Idle sprite (webp/apng/gif) |
| `characters/<char>/(b)<emote>.<ext>` | Talk sprite (webp/apng/gif) |
| `characters/<char>/<emote>.png` | Bare PNG — classic static emote, used for **both** idle and talk when no `(a)`/`(b)` exists |
| `characters/<char>/<preanim>.<ext>` | Pre-animation (no prefix) |
| `characters/<char>/char_icon.png` | Char-select grid icon |
| `characters/<char>/char.ini` | Character metadata (name, showname, blips, emotion list) |

The MS packet's `emote` field **is** the animation base name, so other players'
sprites render straight from the packet — no `char.ini` lookup needed. Folder
names are lowercased before the request (AO2 CDNs host lowercase-only trees).

**Supported image formats:** PNG, APNG, GIF, WebP (static), animated WebP. All are decoded via `SDL2_image`'s `IMG_LoadAnimation_RW` / `IMG_LoadTexture_RW` — format detection is by file content, not extension. WebP requires `switch-libwebp` to be installed (included in the build prerequisites above).

---

### AO2 Theme Compatibility

ferris-ao-switch reads standard **AO2 desktop-client themes** directly from the base pack — no porting or conversion required. On startup, the client loads `misc/default/courtroom_design.ini` (and `courtroom_sounds.ini`) from your base folder or the server CDN and applies the theme's layout to the courtroom UI.

**What the theme controls:**

| Element | INI section |
|---|---|
| Viewport (background + character sprite area) | `[Viewport]` |
| Chatbox position and size | `[Chatbox]` |
| IC message text area | `[IC text]` |
| Nameplate / showname bar | `[Showname]` / `[Nameplate]` |
| Defense HP bar | `[Defense HP bar]` |
| Prosecution HP bar | `[Prosecution HP bar]` |
| OOC log / side panel | `[Log]` |
| Music name strip | `[Music name]` |
| UI sound effects | `[Sounds]` |

**How it works:**

1. At startup, `ThemeManager::load("default")` searches for `courtroom_design.ini` in:
   - `misc/default/` (classic base-pack path)
   - `themes/default/` (newer AO2 theme path)
2. Coordinates are read at their authored resolution (default 960×540) and scaled linearly to 1280×720.
3. If no theme file is found, built-in defaults matching the standard AO2 layout are used.

**Using a non-default theme:**

Themes can be switched at runtime. Future versions will expose a settings screen to select the active theme by name.

**Supported sound mappings** (from `courtroom_sounds.ini`, `[Sounds]` section):

```ini
[Sounds]
realization = sfx-realization
testimony   = sfx-testimony
cross       = sfx-cross_examination
blink       = sfx-blink
objection   = sfx-objection
holdit      = sfx-holdit
takethat    = sfx-takethat
guilty      = sfx-guilty
notguilty   = sfx-notguilty
```

---

## Connecting to a Server

The Connect screen has two tabs — switch between them with **L** / **R**:

### Tab 1: Servers (server browser)

Displays the public server list fetched from the Attorney Online master server in the background. Each row shows:

- Server name
- Player count
- Address and port
- Short description

Press **A** on any row to connect immediately. Press **R** to refresh the list. The master server URL defaults to `https://servers.aceattorneyonline.com/servers` and can be changed in-app by pressing **ZL**.

### Tab 2: Direct Connect

Manually enter connection details for servers not on the public list:

| Field | Description | Default |
|---|---|---|
| **Host** | Server IP address, hostname, or WebSocket URL | `127.0.0.1` |
| **Port** | TCP port (AO2 default: 27017) or WebSocket port | `27017` |
| **Username** | Your OOC display name | `Switch` |

Press **A** on a field to open the system keyboard and edit it. Press **ZR** to connect.

### Tab 3: Settings

Everything here is saved to `sdmc:/switch/ferris-ao/config.ini` automatically and
persists across servers and launches. Up/Down to select a row, **A** / **←→** to change (or tap):

| Setting | What it does |
|---|---|
| **Showname** | Custom IC/OOC display name (blank = username). Persists on close. |
| **Theme** | Cycles through AO2 theme folders found on the SD card (see below) + the built-in `default`; applies instantly. |
| **SFX / Music Volume** | 0–128, applied live. |

**Importing AO2 themes:** drop a standard AO2 theme folder (one containing
`courtroom_design.ini`) into `sdmc:/switch/ferris-ao/base/themes/` (or `…/misc/`)
and it appears in the Theme setting to select.

### Tab 4: Credits

Project info and the repo link (also see [Credits](#credits)).

**WebSocket servers:** Prefix the host with `ws://` to connect in WebSocket mode, or `wss://` for TLS WebSocket (encrypted). Examples:
- `ws://game.example.com` on port `27018` — plain WebSocket
- `wss://game.example.com` on port `443` — TLS WebSocket (default port 443)

After connecting, the handshake sequence runs automatically:

```
Connect → Character Select → Area Select → Courtroom
```

If the server places you in an area automatically (single-area servers), the Area Select screen may be skipped.

---

## Controller Reference

### Global

| Button | Action |
|---|---|
| **+** | Disconnect / return to Connect screen |
| **D-pad** | Navigate menus and lists |
| **A** | Confirm / select |
| **B** | Back / close overlay |

### Connect Screen

| Button | Action |
|---|---|
| **L / R** | Switch between Servers and Direct Connect tabs |
| **D-pad Up/Down** | Move selection (server list or field) |
| **Mouse wheel / touch drag** | Scroll the server list |
| **A** | Connect to selected server (Servers tab) / Edit field (Direct Connect tab) |
| **R** | Refresh server list (Servers tab) |
| **ZL** | Edit master server URL |
| **ZR** | Connect (Direct Connect tab) |

### Character Select

| Button | Action |
|---|---|
| **D-pad** | Move cursor |
| **Mouse wheel / touch drag** | Scroll the grid a row at a time |
| **A** | Select character (if not taken) |
| **Y** / **F** | Search characters by name (system keyboard) |
| **B** | Clear the search |

Searching filters the grid to matching names — the fast way to find one on a
600+ character server.

### Area Select

| Button | Action |
|---|---|
| **D-pad Up/Down** | Move cursor |
| **Right stick** | Scroll list |
| **A** | Enter area |

### Courtroom

The stage fills the whole screen; a chat bar is overlaid across the bottom. The
chatbox has the **showname merged in as a corner tab** (not a floating plate),
with the incoming IC text inside and a **tap-to-talk input bar** below it that
carries inline **`<` `>` emote arrows**. A row of status buttons (IC / OOC /
Music / Evi / Rooms) sits to the right with key hints; HP bars are in the top
corners and the now-playing track runs along the top.

| Button | Action |
|---|---|
| **X** | Toggle the IC composer (full emote grid + preview) |
| **L** | Toggle the OOC chat panel |
| **R** | Toggle the music panel |
| **Y** | Toggle the evidence panel |
| **−** (Minus) | Toggle the **Rooms** panel (switch areas) |
| **← / →** | Cycle your emote (no composer needed) |
| **A / Enter** | Quick-talk: type & send with the current emote — or skip the typewriter / confirm in a panel |
| **D-pad Up/Down** | Navigate / scroll the open panel |
| **Mouse wheel / touch drag** | Scroll the open panel or the IC log |
| **B** | Close the open panel |
| **+** | Leave the courtroom (disconnect) |

Keyboard equivalents: `X` IC, `Z` OOC, `C` Music, `Y` Evidence, `R` Rooms,
`P` leave, arrows/Enter/Esc for navigate/confirm/back.

### Rooms panel (switching areas)

Open with **−** (Minus) / `R`. Lists every area the server advertised with its
live player count, status (`IDLE`/`CASING`/…) and a `[LOCKED]` marker; your
current room is highlighted. **Up/Down** to move, **A** to join (the client
sends the AO2 area-join and the server swaps your background, HP and roster),
**B** to close.

### IC Input (composer overlay)

Opened with **X**. Shows a grid of your character's emotes (with sprite-button
thumbnails when the server provides them), a larger preview of the selected
emote, the text colour with a live swatch, your position, and a preview of the
typed message. It closes itself after a line is sent so you can watch it play.
Thumbnails stream in the background, so opening it never stalls the courtroom.

| Button | Action |
|---|---|
| **D-pad Left/Right** | Move through the emote grid (from your `char.ini`) |
| **D-pad Up/Down** | Cycle the text colour |
| **A** | Open the system keyboard, then type and send the line |
| **B** / **X** | Close the composer |

### Touchscreen

Everything is tappable in handheld mode (and via mouse on Ryujinx):

| Screen | Tap |
|---|---|
| Connect | Tap a tab (Servers / Direct / Credits); tap a server to select, tap it again to connect; tap a Direct field to edit/connect |
| Character select | Tap the search bar to filter; tap a character to highlight, tap it again to pick |
| Courtroom | **Tap the IC input bar to type a line instantly** (current emote/colour/pos); tap the **`<` `>`** arrows on it to change emote; tap a HUD button to open its panel |
| Music / Rooms panel | Tap a row to play that track / join that room |
| IC composer | Tap an emote to select it; tap the message box to type & send |
| OOC panel | Tap to open the keyboard |
| Any panel | Tap outside it to close |

**Drag to scroll.** Since the handheld has no mouse wheel, a finger **drag**
scrolls whatever has focus — the server list, the character grid, the
music/rooms/evidence/OOC panels, the composer's emote grid, and the IC-log
scrollback. A quick press-and-release is still a tap; only movement past a small
threshold becomes a scroll, so taps and drags never collide. **Mouse wheel**
(Ryujinx/desktop) drives the exact same scrolling.

---

## Project Structure

```
ferris-ao-switch/
├── Makefile                        # devkitPro NX + SDL2 portlibs build system
├── icon.jpg                        # 256×256 NRO icon
├── romfs/                          # Bundled assets (romfsInit → romfs:/)
│   └── fonts/noto_sans.ttf         # UI font — the ONLY bundled asset; all art,
│                                   # characters, sounds and music stream over
│                                   # HTTP or come from the optional sdmc: base
│                                   # pack. The courtroom draws primitives when
│                                   # an image is missing, so none need bundling.
└── src/
    ├── main.cpp                    # Entry point — init App, push ConnectScreen, run
    ├── app.hpp / app.cpp           # App class: game loop, screen stack, SDL init
    ├── net/
    │   ├── packet_queue.hpp        # Lock-free SPSC ring buffer (template, N must be power-of-2)
    │   ├── http_fetch.hpp/cpp      # Synchronous HTTP/1.1 GET (HTTPS via TlsConn)
    │   ├── tls_conn.hpp/cpp        # mbedtls TLS client (WSS + HTTPS), #ifdef AO_TLS
    │   ├── connect_pool.hpp/cpp    # Single-thread TCP connect pool (libnx thread-table safe)
    │   ├── ws_handshake.hpp/cpp    # HTTP/1.1 WS upgrade, inline SHA-1 + Base64
    │   ├── ws_frame.hpp/cpp        # RFC 6455 frame encode (masked text) / decode
    │   └── network_thread.hpp/cpp  # Background thread: recv loop, packet extraction, send
    ├── protocol/
    │   ├── packet.hpp              # Packet struct, parse(), escape/unescape
    │   ├── ao_client.hpp/cpp       # Handshake state machine + all in-lobby packet handlers
    │   └── commands.hpp            # Outgoing packet builder free functions (stack buffers)
    ├── state/
    │   ├── game_state.hpp          # All mutable game state, main-thread only
    │   │                           # (CharacterInfo, AreaInfo, EvidenceEntry,
    │   │                           #  ChatLog ring buffer, ICAnimState — all here)
    │   └── settings.hpp/cpp        # Persisted prefs (showname/theme/URL/volumes) → config.ini
    ├── assets/
    │   ├── asset_manager.hpp/cpp   # 4-tier resolution: prefetch → CDN×2 → sdmc: → romfs:
    │   ├── asset_stream.hpp/cpp    # Background worker threads that pre-warm the cache
    │   ├── extensions_config.hpp/cpp # extensions.json (per-category file-ext probe order)
    │   ├── char_ini_parser.hpp/cpp # Windows INI parser for char.ini
    │   ├── theme_manager.hpp/cpp   # AO2 courtroom_design.ini → scaled ThemeLayout
    │   ├── texture_cache.hpp/cpp   # LRU SDL_Texture* cache (256 slots)
    │   └── apng_player.hpp/cpp     # APNG/GIF/animated-WebP via IMG_LoadAnimation_RW()
    ├── audio/
    │   ├── audio_manager.hpp/cpp   # SFX: Mix_Chunk LRU cache
    │   └── music_player.hpp/cpp    # BGM: Mix_Music with crossfade
    ├── render/
    │   ├── renderer.hpp/cpp        # SDL_Renderer wrapper + Layout:: constants, 1280×720
    │   └── text_renderer.hpp/cpp   # SDL_ttf wrapper, 32-slot LRU texture cache
    ├── ui/
    │   ├── screen.hpp              # Abstract Screen base class (opaque() compositing)
    │   └── screens/
    │       ├── connect_screen.hpp/cpp       # Server browser + Direct Connect tabs
    │       ├── char_select_screen.hpp/cpp   # 8×4 character grid
    │       ├── area_select_screen.hpp/cpp   # Scrollable area list with ARUP data
    │       └── courtroom_screen.hpp/cpp     # Main courtroom: stage, chat bar, HUD, panels
    └── input/
        ├── input_manager.hpp/cpp    # SDL_GameController → Action enum, keyboard fallback
        └── virtual_keyboard.hpp/cpp # libnx swkbdShow() wrapper (stdin fallback on desktop)
```

---

## Module Reference

### `net/packet_queue.hpp` — SPSCQueue

Lock-free single-producer / single-consumer ring buffer. Template parameter `N` must be a power of two. Uses `std::atomic<int>` head/tail with acquire/release ordering. No heap allocation after construction.

```cpp
SPSCQueue<InPacket, 256>  in_queue;   // network thread → main thread
SPSCQueue<OutPacket, 64>  out_queue;  // main thread → network thread
```

### `net/ws_handshake.cpp` — WebSocket upgrade

Inline SHA-1 (~80 lines) and Base64 encoder. Sends a standard HTTP/1.1 GET upgrade request, validates the `Sec-WebSocket-Accept` response header. No external cryptography library needed — SHA-1 is only used for the handshake key validation, not for security.

### `net/ws_frame.cpp` — WS framing

- `ws_encode_frame()` — client→server frames are always masked (RFC 6455 §5.3); uses `SDL_GetTicks()` XOR'd with the payload address as a mask key seed
- `ws_decode_frame()` — server→client frames are unmasked; handles 7-bit, 16-bit, and 64-bit payload lengths; returns `FrameResult` enum

### `protocol/packet.hpp` — AO2 packet parser

Packet format: `HEADER#field0#field1#...#%`

`parse_packet()` splits on `#`, stores header and fields in fixed char arrays (no heap). Returns bytes consumed (0 = incomplete). `Packet::unescape()` and `Packet::escape()` handle the four AO2 escape sequences in-place.

### `protocol/ao_client.cpp` — Handshake state machine

States: `Idle → WaitDecryptor → WaitId → WaitSi → WaitSc → WaitSm → WaitDone → InLobby`

Each state transition sends the appropriate outgoing packet and waits for the server's response. Once `DONE` is received, all subsequent packets are dispatched to in-lobby handlers which mutate `GameState` directly.

### `protocol/commands.hpp` — Outgoing packet builders

All builders write into caller-supplied stack buffers and return the byte count. No heap allocation. Example:

```cpp
char buf[256];
int n = ao::cmd::ct(buf, sizeof(buf), "MyName", "Hello world!");
out_queue.push({buf, n});  // push to network thread
```

### `assets/apng_player.cpp` — Animation

Uses `SDL2_image`'s `IMG_LoadAnimation()` (requires SDL2_image ≥ 2.6). Loads all frames into an array of `SDL_Texture*` (max 128 frames). `update(dt_ms)` advances the frame counter; `current()` returns the active texture. Falls back to `IMG_Load()` for static PNG if the file is not animated.

### `input/virtual_keyboard.cpp` — Text input

On Switch: calls `swkbdCreate()`, `swkbdConfigSetGuideText()`, `swkbdShow()`, `swkbdClose()`. The system keyboard applet blocks until the user confirms or cancels. Works identically on Ryujinx.

On desktop (non-`__SWITCH__` builds): reads from stdin, allowing dev/testing without a Switch.

---

## AO2 Protocol Notes

### Wire format

```
HEADER#field0#field1#...#%
```

All fields are UTF-8 text. Special characters are escaped:

| Wire | Meaning |
|---|---|
| `<num>` | `#` |
| `<percent>` | `%` |
| `<dollar>` | `$` |
| `<and>` | `&` |

### Handshake sequence

```
← decryptor#NOENCRYPT#%
→ HI#<hdid>#%
← ID#0#<servername>#<version>#%
← PN#<players>#<max>#<description>#%
← FL#<feature flags...>#%
[← ASS#<asset_url>#%]
→ ID#ferris-ao-switch#0.1#%
→ askchaa#%
← SI#<char_count>#<evi_count>#<music_count>#%
→ RC#%
← SC#<char0>#<char1>#...#%
→ RM#%
← SM#<area0>#<area1>#...#<song0>#<song1>#...#%
→ RD#%
← LE#...#%
← CharsCheck#<0|1>#...#%
← HP#1#<defense_val>#%
← HP#2#<prosecution_val>#%
← BN#<background>#%
← DONE#%
→ CC#<uid>#<char_id>#<hdid>#%   ← join with chosen character
```

### MS packet — IC message (30 server-broadcast fields)

The server broadcasts 30 fields; the client sends 26. Fields 17, 18, 20, 21 are inserted server-side from the pairing partner's state.

```
[0]  desk_mod       [1]  pre_anim       [2]  char_name     [3]  emote
[4]  message        [5]  pos            [6]  sfx           [7]  emote_mod
[8]  char_id        [9]  clip           [10] objection_mod [11] evidence_id
[12] flip           [13] realization    [14] text_color    [15] showname
[16] other_charid   [17] other_name*    [18] other_emote*
[19] self_offset    [20] other_offset*  [21] other_flip*
[22] immediate      [23] looping_sfx   [24] screenshake   [25] frame_screenshake
[26] frame_real     [27] frame_sfx     [28] additive      [29] effects
```
`*` = server-inserted pairing fields

### ARUP — Area update packets

Four types, sent as a broadcast whenever area data changes (delta-suppressed server-side):

| Type | Data |
|---|---|
| `ARUP#0#...` | Player counts per area |
| `ARUP#1#...` | Status strings per area (`IDLE`, `CASING`, `RECESS`, etc.) |
| `ARUP#2#...` | CM labels per area |
| `ARUP#3#...` | Lock states per area (`FREE`, `SPECTATABLE`, `LOCKED`) |

### SM packet — Music list

Areas come first (entries without `.`), then music files (entries containing `.`). The client splits on the first entry containing a `.` to determine where areas end and music begins.

---

## Contributing

Pull requests are welcome. Before contributing:

- Build successfully with `make` targeting Switch
- Test on Ryujinx with a real AO2 server connection
- Follow the no-heap-in-hot-path rule: all per-frame data uses fixed arrays
- No `std::string`, `std::vector`, or dynamic allocation in `Packet`, `GameState`, or the network path
- New screens must inherit from `Screen` and be pushed/popped via `App::push_screen()` / `App::pop_screen()`
- New outgoing packet types belong in `protocol/commands.hpp` as free functions

### Known limitations / TODO

- Evidence can be viewed but not yet attached to an outgoing IC message from the UI
- Switch rooms from inside the courtroom via the **Rooms** panel (`−`); the separate Area Select screen pushed before the courtroom is currently bypassed (Character Select enters the courtroom directly)

---

## Credits

Created by **SyntaxNyah** — <https://github.com/SyntaxNyah/ferris-ao-switch>

Also reachable in-app from the **Credits** tab on the connect screen.

---

*ferris-ao-switch is not affiliated with the official Attorney Online project.*
