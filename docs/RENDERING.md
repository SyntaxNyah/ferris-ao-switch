# Courtroom Rendering & Asset Streaming

How ferris-ao-switch turns AO2 packets into a live courtroom, and how every
asset is fetched on demand. This mirrors how **AO-SDL** and **webAO** work; the
path conventions below are the authoritative AO2 ones (cross-checked against
AO2-Client, AO-SDL, webAO, the LemmyAO viewport, and the aolib JSON schemas).

---

## 1. Asset streaming (where files come from)

Only the UI font is bundled. Every sprite/background/sound is resolved at
runtime by `AssetManager`, which searches these tiers in order:

1. **Prefetch cache** — in-memory, filled by the `AssetStream` worker threads.
2. **Primary HTTP CDN** — the URL from the server's `ASS` handshake packet.
3. **Secondary HTTP CDN** — the community base pack (`attorneyoffline.de/base/`).
4. **Local** — `sdmc:/switch/ferris-ao/base/<rel>` then `romfs:/<rel>`.

Each tier that 404s falls through to the next; per-URL failures are cached so a
dead server is never hammered. HTTP paths are **lowercased** and percent-encoded
(preserving `()` so `(a)normal.png` round-trips). See `asset_manager.cpp`.

**Persistent disk cache (HTTPS/Cloudflare).** Both HTTP tiers are wrapped by an
on-disk cache at `sdmc:/switch/ferris-ao/cache`, keyed by a hash of the full URL.
`try_http_mount` checks the cache file before hitting the network and writes the
bytes after a successful download (≤ 8 MB/file, best-effort). It runs only on the
worker threads, so it makes repeat fetches (and relaunches) instant without ever
touching the render loop. Combined with per-worker keep-alive `HttpClient`s, this
is how the client leans on Cloudflare/CDN edge caching. Delete the folder to
reclaim space; disabled on non-Switch builds.

`open_rwops(rel)` returns an owning `SDL_RWops` (closing it frees the buffer),
so it drops straight into `IMG_Load_RW`, `IMG_LoadAnimation_RW`,
`Mix_LoadWAV_RW`, etc. with `freesrc=1`.

### Extension probing

A path like `characters/phoenix/(b)normal` has no extension — the real file
could be `.webp`, `.apng`, `.gif`, or `.png`. The order to try comes from
`ExtensionsConfig` (parsed from `<cdn>/extensions.json`, webAO format, with
built-in defaults). The courtroom builds the exact candidate path for each
extension (with the `(a)`/`(b)`-prefix rules below) and decodes the first one
that is present.

### Non-blocking loading (no main-thread network — buttery smooth)

The render loop **never** does network I/O. The split is:

- **Worker threads (`AssetStream`, 8 of them)** do all blocking fetches. When a
  message arrives, `begin_message()` queues every candidate path
  (`prefetch_emote`/`prefetch_bgimg`/`prefetch_shout`) — each worker downloads
  one (HTTP keep-alive → sdmc → romfs) and stores successes in the prefetch
  cache. 404s simply never appear there.
- **Main thread** only ever calls `APNGPlayer::load()` for a path that
  `AssetManager::has_prefetch()` reports is already in the cache — so the load
  is a pure in-memory decode (~1 ms), not a network round-trip.
  `resolve_assets()` runs every frame and decodes whatever has become ready,
  setting the per-slot `*_ready_` flags (`bg_ready_`, `talk_ready_`, …).

A new **`Phase::Loading`** holds the IC timeline until the speaker's sprite is
decoded (`char_ready()`) or a short gate elapses (`LOAD_GATE_MS`, 1.2 s), so the
text and animation start together and crisp. Assets that never arrive are given
up on after `ASSET_GIVEUP_MS` (8 s) so a missing file can't stall anything.
Music is resolved the same way (`update_music()` prefetches the three AO2 music
layouts and plays whichever lands first). Small audio (SFX/blips) stays
synchronous but is warmed by the same prefetch, so it's normally a cache hit.

The net effect: the first time a character/background is seen, the bytes come
off the worker threads while the room keeps rendering at 60 fps; every
subsequent view is an instant cache hit.

---

## 2. AO2 asset path conventions

### Character sprites (`load_emote`)

`buildEmoteUrls` semantics — the `(a)`/`(b)` marker is a **prefix at the
character root**, except `.png`/`.webp.static` which use the **bare** name:

| Candidate ext | Idle path | Talk path |
|---------------|-----------|-----------|
| `.webp/.apng/.gif` | `characters/<c>/(a)<emote><ext>` | `characters/<c>/(b)<emote><ext>` |
| `.png` | `characters/<c>/<emote>.png` | `characters/<c>/<emote>.png` |
| `.webp.static` | `characters/<c>/<emote>.webp` | `characters/<c>/<emote>.webp` |

- **The MS packet's `emote` field is the animation base** (e.g. `normal`), so
  other players' sprites render straight from the packet — **no char.ini needed**.
- Pre-anim: same builder with an empty prefix (`characters/<c>/<preanim><ext>`).
- Char folder and background folder are lowercased before the URL is built.
- `char.ini` is only needed for the local player's **emote picker** and is
  fetched over HTTP by `load_char_ini()` (`char_ini_parser.cpp`).

### Backgrounds & desks (`load_bg_image`, `bg_filename`/`desk_filename`)

`background/<bg>/<file><ext>`, where the position maps to a file name:

| side | background file | desk/bench file |
|------|-----------------|-----------------|
| `def` | `defenseempty` | `defensedesk` |
| `pro` | `prosecutorempty` | `prosecutiondesk` |
| `wit` | `witnessempty` | `stand` |
| `jud` | `judgestand` | `judgedesk` |
| `hld` | `helperstand` | `helperdesk` |
| `hlp` | `prohelperstand` | `prohelperdesk` |
| `jur` | `jurystand` | `jurydesk` |
| `sea` | `seancestand` | `seancedesk` |

The courtroom streams the **server's** background only — it does **not**
substitute `background/default/...`, and an empty MS `pos` keeps the current
position instead of snapping to `wit`. The viewport stays black until the
server's background streams in, so a slow or no-pos line never flips the room to
a bundled courtroom.

### Shout bubbles (`load_shout_bubble`)

`shout_modifier` → name: `1=holdit, 2=objection, 3=takethat, 4=custom`. Tries
`characters/<c>/<name>_bubble.<ext>` (webp/gif/apng/png), then
`misc/default/<name>_bubble.png`. Custom (`4`) uses `characters/<c>/custom.<ext>`.

### Audio

- SFX: `sounds/general/<name>` then `sounds/<name>` (`.opus/.ogg/.wav/.mp3`).
- Blips: `sounds/blips/<blip>` then `blips/<blip>` (default blip `male`).
- Music: `sounds/music/<song>`, then `<song>`, then `music/<song>`.
  `~stop.mp3` / `~~` stop playback.

---

## 3. The IC animation timeline

When `on_ms()` parses an `MS` broadcast it fills `gs.ic_anim` and sets
`pending`. `CourtroomScreen::update()` notices and calls `begin_message()`,
which snapshots the fields, queues all asset prefetches, and enters
`Phase::Loading`. Loading waits for the speaker sprite to decode (or the 1.2 s
gate) and then runs this state machine (`enum class Phase`):

```
            objection_mod ≥ 1 ?
                  │ yes                 │ no
                  ▼                     │
          ┌──────────────┐             │
          │   Shout       │  1.5s      │
          │  bubble + SFX │            │
          └──────┬────────┘            │
                 ▼                     ▼
            emote_mod ∈ {1,2,6} && preanim valid ?
                  │ yes                 │ no
                  ▼                     │
          ┌──────────────┐             │
          │   Preanim     │ play once  │
          │  (a-less gif) │            │
          └──────┬────────┘            │
                 ▼                     ▼
          ┌────────────────────────────────┐
          │            Talking              │
          │  (b) talk sprite + typewriter   │
          │  + blips + message SFX          │
          │  + optional realization flash   │
          └──────┬──────────────────────────┘
                 ▼  (typewriter done)
          ┌──────────────┐
          │     Idle      │  (a) idle sprite
          └──────────────┘
```

- **Typewriter:** `TYPEWRITER_MS` (18 ms) per character; A/Enter (or a tap on the chat box) skips to the
  end. A blip SFX fires every `BLIP_EVERY` visible characters.
- **Realization:** a white viewport flash fading over `REALIZE_MS` (350 ms),
  plus the theme `realization` SFX.
- **Screenshake:** ~14 frames of viewport jitter when `screenshake` is set.
- **Static pre-anim guard:** a single-frame "pre-anim" never reports
  `finished()`, so the Preanim phase also exits when `!animated()` — otherwise
  it would hang forever.

### Layer order (`render_viewport`)

```
background  →  pair sprite  →  speaker sprite  →  desk overlay
            →  realization flash  →  shout bubble
```

`flip` mirrors a sprite horizontally; `self_offset`/`other_offset` shift it by a
percentage of viewport width (used for pairing — the partner is drawn behind).
Every sprite layer is stretched to the themed viewport rect, matching AO2 where
each frame is authored at viewport size.

### Scene/music caching

Backgrounds and desks reload only when `pos` or `gs.background` changes
(`cur_pos_`/`cur_bg_`); music (re)plays only when `gs.current_music` changes
(`cur_music_`). This keeps the per-message cost to just the character sprites.

---

## 4. Screen layout & theme support

### Screen compositing (opaque culling)

`App::render()` clears once, then draws **only from the topmost opaque screen
upward**. Every full-screen screen (`Screen::opaque()` defaults to `true`)
hides whatever is beneath it, so picking a character no longer leaves the
character grid showing through the courtroom. A transparent overlay screen would
override `opaque()` to return `false` and let the screen below render first; all
current screens are opaque, and the courtroom's own OOC/Music/Evidence/IC panels
are drawn inline (not as separate screens), so they layer correctly on top.

### Default layout (no theme file)

`romfs` ships only the font, so unless a server CDN serves
`misc/default/courtroom_design.ini` the built-in `Layout::` constants
(`render/renderer.hpp`) are what you see. They describe an **authentic
full-screen AO composition**, not the old boxed-in look:

```
┌───────────────────────────────────────────────────────────┐
│ [DEF ▓▓▓▓░░]        Music: <track>          [PRO ▓▓░░░░]   │  ← HUD over the stage
│                                                             │
│            full-screen stage: background + sprites          │
│                  (sprites fill the whole viewport,          │
│                   their lower body behind the bar)          │
│                                                             │
├─────────────────────────────────────────────────────────── │
│ [ Showname ]                                                │  ← chat bar (overlay)
│ IC message text, word-wrapped …      [IC][OOC][Music][Evi]  │
└───────────────────────────────────────────────────────────┘
```

- `VIEWPORT` is the entire 1280×720 framebuffer — the stage is no longer a small
  853×480 box beside a giant side panel.
- `CHAT_AREA` is a 176 px bar overlaid across the bottom; `NAMEPLATE` and the
  `CHATBOX` (IC text) sit inside it, the button row hugs its right edge.
- HP bars get inline dark **label chips** (`DEF`/`PRO`) and the now-playing
  strip its own dark backing, so the HUD stays legible over any background.
- The button row has five toggles — **IC** (`X`), **OOC** (`L`), **Music**
  (`R`), **Evi** (`Y`), **Rooms** (`−`) — each printing its control-key hint and
  highlighting when its panel is open. (The courtroom reads raw controller
  buttons, so OOC/Music are the shoulder buttons `L`/`R` and Rooms is `−`/BACK.)

### Theme files

`ThemeManager::load("default")` fetches `misc/<theme>/courtroom_design.ini`
(or `themes/<theme>/...`) through `AssetManager`, parses the rects, and scales
them from the theme's authored resolution (default 960×540) to 1280×720. The
courtroom reads `app.theme().layout()` every frame, so any standard AO2
base-pack theme drives the viewport, chatbox, nameplate, HP bars, and panels.
`courtroom_sounds.ini` supplies the shout/realization SFX names. The chatbox
image (`<theme>/chatbox`) is drawn over the chat bar when present.

---

## 5. Sending messages, switching rooms

**IC composer** (`CourtroomPanel::ICInput`, opened with **X**) — a centred modal
that reads the local player's `char.ini` (`load_char_ini`) and shows a **grid of
their emotes**: each cell is the emote name plus its `emotions/button<N>_off.png`
thumbnail, and the selected cell uses `button<N>_on.png` with a larger preview
beside the grid. Those thumbnails are warmed exactly like the char-select icons
— `prefetch_emote_buttons()` queues them on the `AssetStream` workers, `update()`
decodes a few per frame, and the render path only ever `peek()`s the texture
cache, so opening the composer never blocks. D-pad ←/→ move through the grid,
↑/↓ cycle text colour (live swatch), and **A** opens the system keyboard and on
confirm builds the 26-field client `MS` packet via `cmd::ms` (`emote_mod=1` when
the chosen emote has a pre-anim), then closes the composer so the line can play.

**Music panel** sends `MC` for the highlighted track; **OOC panel** sends `CT`.

**Rooms panel** (`CourtroomPanel::Area`, opened with **−**) lists `gs.areas[]`
with the live player count / status / lock state that `ARUP` keeps current.
Joining an area reuses the **`MC`** packet — `cmd::mc(buf, sz,
gs.areas[sel].name, char_id, username)` — because AO2 servers treat a
music-change whose name matches an area as an area-join (this is exactly what
webAO does); the server then re-sends `BN`/`HP`/charscheck for the new room and
the courtroom updates itself. `gs.my_area_idx` is set optimistically so the
panel highlights the new room immediately.

---

## 6. Performance notes (real-time chat)

- **Typewriter renders one texture per message, not per character.**
  `TextRenderer::draw_wrapped_upto(full_message, …, reveal)` rasterises the whole
  IC line into a single cached texture once and blits a growing prefix of it. The
  earlier code rendered the growing substring every typewriter step, creating and
  destroying a texture per character *and* evicting the rest of the UI text from
  the text LRU (so shownames, buttons, music names, etc. re-rasterised every
  frame). A small `wrap_*` cache holds the line offsets and is recomputed only
  when the message or wrap width changes, so each frame's reveal is O(1).
- **Asset probing stops once a message is fully loaded.** `resolve_assets()`
  early-outs when every sprite/scene/shout this line needs is decoded, so the
  per-frame `has_prefetch()` mutex scans don't run during steady-state chat. A
  background change (`BN`) re-arms the scene's load window.
- **Emote thumbnails are budgeted.** The composer decodes at most a handful of
  prefetched button images per frame, the same pattern the char-select grid uses
  to stay inside the 16 ms frame.
- **Sprites don't reload between lines.** `APNGPlayer::load` is a no-op when asked
  for the path it already holds, and `begin_message` skips re-prefetching the
  speaker sprite when the char+emote is unchanged — so a character talking line
  after line does zero asset work and skips the Loading gate. This was the main
  cause of "characters reload every time someone talks".
- **Big-server icon flood is gone.** Character icons are prefetched on-demand for
  the visible window in `CharSelectScreen` (re-queued only when the scroll moves),
  not bulk-queued for all 600 at lobby-enter, and `CourtroomScreen::on_enter`
  calls `AssetStream::clear_pending()`. IC sprites no longer sit behind a wall of
  icon fetches, so the first lines on a huge server are fast.
- **Persistent disk cache.** Repeat views/relaunches read assets from SD instead
  of the network (see §1), so steady-state chat on a server you've used before is
  almost entirely cache hits.
- **The IC log re-blits cached textures.** Each entry is a showname line plus a
  word-wrapped message block; the strings are stable so they stay in the
  (96-slot) text cache and re-blit with no per-frame rasterisation or allocation
  (only the wrap-height math runs each frame, which is metrics-only). The wheel
  scrolls back through history.
