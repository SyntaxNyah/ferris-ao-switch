# 🦀 Ferris-AO Switch — v0.5 (First Public Release!)

**Attorney Online 2, running natively on your Nintendo Switch.**

Ferris-AO Switch is a homebrew client for [Attorney Online 2](https://attorneyonline.de/) — the same courtroom roleplay you know from the desktop and web clients, now as a single `.nro` you can launch right from the homebrew menu. Hold court in handheld mode on the couch, or play docked with a Pro Controller. No PC required, and **no base pack to download** — characters, backgrounds, music and sounds all stream straight from the server.

> Written from scratch in C++17 on devkitPro + SDL2. Same binary runs on real Switch hardware (Atmosphère) and the Ryujinx emulator.

---

## ✨ Highlights

**Full courtroom gameplay**
- Animated character sprites — idle, talking, and pre-animations (GIF / APNG / animated WebP)
- Typewriter IC messages with word-wrap and the full AO2 text-colour palette
- **OBJECTION!**, Hold It!, Take That! shout bubbles, realization flash, and screenshake
- Character pairing (two characters side-by-side with offsets + flip)
- Always-on IC scrollback log so you never miss a line
- HP bars, music now-playing strip, and narrator mode

**Everything you need to play**
- **Server browser** — pulls the live public server list; pick one and connect
- **Direct connect** for private/unlisted servers (TCP, `ws://`, and secure `wss://`)
- **Character search** — filter a 600-, 3000-, even 3800-character roster by name in seconds
- **Music**, **OOC chat**, **evidence**, and **room/area switching** panels
- **Themes** — drop any AO2 `courtroom_design.ini` theme folder on your SD card and pick it in Settings
- Custom showname, master-server URL, and volume — all saved across launches

**Built to feel instant**
- Assets stream on **8 background threads** and decode **off the render thread**, so the courtroom never stalls on the network
- **WebP-first** smart format probing that learns what each server uses (and remembers it next time)
- An in-memory decode cache plus an on-disk cache, so anyone who just spoke re-appears instantly and revisits load from the SD card with zero network
- Keep-alive HTTPS connection reuse — comfortably handles big Cloudflare-hosted servers with **3000+ files**

**Plays your way**
- 📱 **Touchscreen** in handheld — tap buttons/lists, **drag to scroll**, tap-to-talk
- 🎮 **Joy-Con & Pro Controller** — full button mapping
- ⌨️ **System keyboard** for all text entry

---

## 📋 Complete feature list

<details>
<summary><b>Click to expand the full list</b></summary>

**Connecting**
- Public **server browser** (live master list; configurable master-server URL)
- **Direct connect** by host / port / username
- **TCP**, **WebSocket** (`ws://`), and **secure WebSocket** (`wss://`, TLS) — auto-detected from the address
- Compatible with akashi, tsuserver, Ferris-AO and other standard AO2 servers
- Full AO2 handshake; reconnects cleanly on disconnect

**Characters**
- 8×4 character grid, paged, supporting **huge rosters (thousands of characters)**
- **Name search / filter** (instant, case-insensitive)
- Taken characters dimmed; streamed character icons; per-character `char.ini` (name, showname, emotes, blips)
- Narrator mode (talk with no character)

**In-Character messaging**
- **Typewriter** text with word-wrap
- **10 AO2 text colours** (white, green, red, orange, blue, yellow, pink, cyan, grey, rainbow)
- Custom or character **shownames**
- Idle / talking sprite animation, **pre-animations**
- **Shout bubbles** — Objection!, Hold It!, Take That!, and custom
- **Realization** flash, **screenshake**, horizontal **flip**, desk / no-desk
- **Pairing** — partner character drawn alongside with independent offset + flip
- Position support (`wit`, `def`, `pro`, `jud`, `hld`, `hlp`, `jur`, `sea`)
- **Always-on IC log** — word-wrapped, colour-coded scrollback you can scroll through
- **Tap-to-talk bar** with inline `< >` emote arrows (fire lines back-to-back)
- **IC composer** — emote grid with thumbnails, live sprite preview, colour swatch, position

**Other panels**
- **OOC chat** — send + scrollable log, with your own messages highlighted
- **Music** — full server track list, play any track, now-playing strip, crossfade (OGG/MP3/Opus)
- **Evidence** — viewer with thumbnails
- **Rooms / Areas** — live player counts, status, CM and lock state (ARUP); hop rooms without reconnecting
- **HP bars** — defense & prosecution (0–10)

**Audio**
- Per-character typewriter **blips**, message **SFX**, and **shout SFX**
- Background **music with crossfade**
- Separate **SFX / music volume** controls

**Assets & streaming**
- **HTTP & HTTPS** streaming from the server's CDN (`ASS`) — **no base-pack download needed**
- Four-tier resolution: prefetch cache → server CDN → community base CDN → local `sdmc:` base → bundled `romfs:`
- Optional **local base pack** (`sdmc:/switch/ferris-ao/base/`) with on-screen "detected" indicator
- **Persistent on-disk cache** (per-URL) — revisits load from SD with no network
- **WebP-first smart probing** + **learned format per server** (remembered across visits)
- **Off-thread image decode** (workers decode; render thread only uploads)
- **Decoded-animation cache** (recently-seen sprites re-show instantly)
- Keep-alive **connection reuse**; 8-thread async prefetch; **content-based format detection** (handles mislabeled files like GIF-as-`.webp`)
- Formats: **PNG, APNG, GIF, WebP (static & animated)**

**Themes & settings**
- Standard AO2 **`courtroom_design.ini` theme** support — drop a theme folder on the SD card and pick it; live switching; auto-scaled to 1280×720; reads `courtroom_sounds.ini`
- Persistent settings (`config.ini`): custom **showname**, **theme**, **master-server URL**, **SFX/music volumes**

**Input & display**
- **Touchscreen** (tap + finger **drag-scroll**), **Joy-Con / Pro Controller**, **system keyboard**, plus mouse/keyboard on Ryujinx
- **1280×720** native, docked & handheld; hardware-accelerated, vsync; full-screen stage with overlaid HUD

**Under the hood**
- The render loop **never blocks on the network** — no join freeze or IC stutter, even on **3000+ file** servers
- Crash-resilient networking; streams the **server's real background** (no placeholder courtroom flashing)
- One `.nro`, runs on **Atmosphère** hardware and **Ryujinx** — no keys, no title install

</details>

---

## ⚡ Why it's so fast

AO content can be *gigabytes* — thousands of characters, backgrounds and tracks — and the Switch is not a powerful machine. Ferris-AO Switch stays smooth anyway because **the render loop never waits on anything**:

- **Nothing downloads or decodes on the main thread.** Eight background worker threads fetch files *and* decode the images (PNG/WebP/GIF/APNG) into ready-to-upload pixels; the game thread only does the final, cheap GPU upload. No hitches, no "loading…" freezes.
- **It asks for far less.** AO files carry no extension on the wire, so most clients fire ~5 guesses per asset and let 4 of them 404. Ferris-AO probes **WebP-first**, *learns* the format a server actually uses from the first file, and then asks for **only that one** — and remembers it for next time.
- **It reuses connections.** A single warm HTTPS connection per worker streams file after file instead of paying a fresh DNS + TLS handshake every time — the difference between sluggish and snappy on big Cloudflare-hosted servers.
- **It remembers what it's seen.** A decoded-sprite cache means anyone who *just* spoke pops back instantly, and an on-disk cache means the **second** visit to a server loads from your SD card with **zero network**.

The payoff: it stays responsive even on **3000+ file servers**, and on a repeat visit it's nearly instant.

---

## 📥 Installing

**On a modded Switch (Atmosphère CFW):**
1. Download `ferris-ao-switch.nro` from the Assets below.
2. Copy it to the `/switch/` folder on your SD card.
3. Open the **Homebrew Menu** (hold the Album button, or launch your hbmenu entry point) and pick **Ferris-AO**.

**On Ryujinx (PC):**
1. Download `ferris-ao-switch.nro`.
2. Drag it onto the Ryujinx window, or **File → Load Application from File…**.
3. Make sure internet access is enabled in Ryujinx settings.

That's it — no firmware keys, no title installation, no base pack.

---

## 🎮 Controls (quick reference)

| Where | Button | Action |
|---|---|---|
| Connect | **L / R** | Switch tabs · **A** connect/edit · **R** refresh list |
| Character select | **D-pad** move · **A** pick · **Y / tap** search · wheel/drag scroll |
| Courtroom | **X** IC composer · **L** OOC · **R** Music · **Y** Evidence · **−** Rooms |
| Courtroom | **← / →** cycle emote · **A / Enter** quick-talk or skip typewriter |
| Anywhere | **+** disconnect · **B** close panel · touch-drag to scroll long lists |

The chat bar has a **tap-to-talk** input with inline `< >` emote arrows — fire line after line without re-opening the composer.

---

## ✅ Compatibility

- Connects to standard AO2 servers — Ferris-AO, tsuserver, **akashi**, and others — over TCP, WebSocket, or secure WebSocket (TLS).
- Reads stock AO2 content: characters, backgrounds, evidence, music, themes. Mislabeled assets in the wild (e.g. GIF sprites saved with a `.webp` name) are detected by content and handled.
- Servers without a CDN: drop an AO2 base pack at `sdmc:/switch/ferris-ao/base/` and it loads locally; otherwise the community base CDN fills in the classics.

## ⚠️ Known limitations

- Evidence can be **viewed** but not yet attached to an outgoing message from the UI.
- The IC composer exposes emote + colour + position; some advanced toggles (flip, immediate, etc.) are rendered when received but not yet selectable when sending.
- This is a **0.x first public release** — expect rough edges, and please report bugs!

---

## 🐛 Found a bug?

Open an issue on the [GitHub repo](https://github.com/SyntaxNyah/ferris-ao-switch) with the server you were on and what happened. Ryujinx logs are gold for diagnosing asset/network issues.

## 🙏 Credits

Created by **SyntaxNyah**. Built on the shoulders of the Attorney Online community, devkitPro, and SDL2.

*Ferris-AO Switch is not affiliated with the official Attorney Online project. "Nintendo Switch" is a trademark of Nintendo; this is unofficial homebrew.*

**Now get out there and OBJECT! 🦜⚖️**
