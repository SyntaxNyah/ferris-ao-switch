# 🦀 Ferris-AO Switch — v1.00

**Attorney Online 2, running natively on your Nintendo Switch.** First **1.0** release — the client is now fully playable end to end: connect, pick a character, and actually *hold court* with a keyboard that doesn't freeze and chat that flows live.

Runs as a single `.nro` on real modded hardware (Atmosphère) and on the Ryujinx emulator — same binary. Characters, backgrounds, music and sounds all **stream from the server**, so there's nothing to pre-download.

---

## 🎉 What's new since v0.5

- ⌨️ **In-app keyboard that doesn't freeze the game.** Typing used to open the blocking system keyboard, which froze the whole client — incoming messages piled up and only appeared after you sent one. The IC/OOC keyboard is now an **on-screen keyboard the app draws itself**, so chat keeps flowing live while you type. Works with **touch** (handheld), **mouse** (Ryujinx), **controller** (D-pad cursor + A/B — so it works **docked**), and a **USB keyboard**.
- ⭐ **Send shouts & effects.** The IC composer can now SEND **Objection! / Hold It! / Take That! / Custom** shouts, plus **Flip**, **Realization**, and **Screenshake** — not just receive them.
- 🔍 **Navigate huge rosters.** The character grid **zooms** (32 → 220 per page via on-screen `− / +`) and has a **right-edge scrollbar you drag to jump** — built for 3000+ character servers.
- 🧹 **OOC chat fixed.** The OOC log no longer overlaps itself on long lines — entries stack cleanly by their real height.
- ⚡ **Much faster on big HTTPS/Cloudflare servers.** Fixed a connection bug that did a fresh TLS handshake on every file; now the workers keep warm connections and stream back-to-back. Plus off-thread image decode and a smarter, WebP-first, self-learning asset probe.
- 🐦 **Mislabeled assets handled.** Old GIF sprites shipped with a `.webp` name (e.g. Polly) no longer render as a "giant pink screen," and WebP emote buttons load correctly.

---

## ✨ Feature highlights

- **Full IC gameplay** — animated idle/talk sprites, pre-animations, typewriter text with the AO2 colour palette, shouts, realization, screenshake, **pairing** (rendered), an always-on IC log.
- **Server browser** + **direct connect** (TCP / `ws://` / secure `wss://`).
- **Character search/filter**, **music** & **rooms/areas** panels, **OOC chat**, **evidence** viewer, **HP bars**, narrator mode.
- **Themes** — drop any AO2 `courtroom_design.ini` theme on the SD card and pick it.
- **Streams everything** from the server's CDN — no base pack required (optional local pack supported).

<details>
<summary><b>📋 Full feature list</b></summary>

**Connecting** — public server browser (configurable master URL); direct host/port/username; TCP, WebSocket, secure WebSocket (TLS); akashi/tsuserver/Ferris-AO compatible; clean reconnect.
**Characters** — zoomable grid for thousands of chars; instant name search; streamed icons; taken-dimming; narrator mode.
**IC** — typewriter + word-wrap; 10 AO2 colours; custom/character shownames; idle/talk + pre-anims; **Objection!/Hold It!/Take That!/Custom shouts**; **Flip / Realization / Screenshake**; pairing; positions; always-on word-wrapped IC log.
**Text entry** — non-blocking on-screen keyboard (touch / mouse / controller / USB keyboard).
**Panels** — OOC chat (own-message highlight, clean wrap); music list (play any track, now-playing, crossfade); evidence viewer; rooms/areas with live counts/status/lock (ARUP); HP bars.
**Audio** — per-character blips, message & shout SFX, crossfading BGM, separate SFX/music volumes.
**Assets** — HTTP/HTTPS streaming (no base pack); four-tier resolution (CDN → community CDN → local → bundled); persistent disk cache; WebP-first learned probing; **off-thread decode**; decoded-animation cache; keep-alive connection reuse; content-based format detection.
**Themes/settings** — `courtroom_design.ini` themes (live switch, auto-scaled); persistent showname/theme/master-URL/volumes.
**Input/display** — touchscreen (tap + drag); Joy-Con/Pro Controller; on-screen + USB keyboard; 1280×720 docked & handheld, hardware-accelerated.

</details>

---

## ⚡ Why it's so fast

The render loop **never waits on anything**: 8 worker threads fetch *and decode* assets off the main thread, the client probes **WebP-first** and learns each server's format (so it asks for one file, not five), reuses warm HTTPS connections, and caches decoded sprites in memory + raw files on the SD card. The payoff — it stays responsive even on **3000+ file servers**, and a repeat visit is nearly instant.

---

## 📥 Installing

**Modded Switch (Atmosphère):** download `ferris-ao-switch.nro`, copy it to `/switch/` on your SD card, open the **Homebrew Menu**, and launch **Ferris-AO**.

**Ryujinx:** download `ferris-ao-switch.nro`, drag it onto Ryujinx (or **File → Load Application from File…**), and make sure internet access is enabled.

No firmware keys, no title install, no base pack.

## 🎮 Controls

| Where | Controls |
|---|---|
| Connect | **L/R** tabs · **A** connect/edit · **R** refresh |
| Character select | **D-pad** move · **A** pick · **Y/F/tap** search · on-screen **− / +** zoom · drag the **scrollbar** to jump |
| Courtroom | **X** IC composer · **L** OOC · **R** Music · **Y** Evidence · **−** Rooms · **←/→** emote · **A** quick-talk · **+** leave |
| Keyboard | tap keys, or **D-pad** cursor + **A** press + **B** cancel (docked) |

## ⚠️ Known limitations

- Evidence is **viewable** but not yet attachable to a message; pair-up partner picker, judge/CM controls and mod-calls aren't wired into the UI yet.
- Build-tested; please report anything odd on the [repo](https://github.com/SyntaxNyah/ferris-ao-switch).

## 🙏 Credits

Created by **SyntaxNyah**, on the shoulders of the Attorney Online community, devkitPro and SDL2.

*Unofficial homebrew — not affiliated with the official Attorney Online project or Nintendo.*

**Court is in session. 🦜⚖️**
