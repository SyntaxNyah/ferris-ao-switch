# CLAUDE.md — ferris-ao-switch Codebase Guide

Complete architecture reference for working on this codebase. Read before touching any file.

---

## What This Project Is

**ferris-ao-switch** is a Nintendo Switch homebrew client for [Attorney Online 2 (AO2)](https://attorneyonline.de/), written in C++17 using devkitPro + SDL2 portlibs. Produces a single `.nro` file that runs on real modded Switch hardware (Atmosphere CFW) and Ryujinx emulator — same binary, no conditional compilation needed at the game-logic level.

Connects to any standard AO2 server (Ferris-AO, tsuserver, Akasha, etc.) over TCP or WebSocket. Full IC message pipeline with character animation, music/SFX, evidence, pairing, OOC chat.

---

## Build System

**Toolchain:** devkitPro (dkp-pacman) — ARM64 cross-compiler. `make` invokes the standard NX SDL2 Makefile template, producing `ferris-ao-switch.nro` via `elf2nro`.

```makefile
TARGET   := ferris-ao-switch
SOURCES  := src src/net src/protocol src/state src/assets \
            src/audio src/render src/ui src/ui/screens src/ui/courtroom src/input
ROMFS    := romfs
ARCH     := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CXXFLAGS := -std=c++17 -O2 -fno-exceptions -fno-rtti $(ARCH)
LIBS     := -lSDL2_mixer -lSDL2_ttf -lSDL2_image -lSDL2_net -lSDL2 \
            -lopusfile -lopus -lvorbisidec -logg -lfreetype -lpng -lz -lnx -lm
```

**Required portlibs** (install via `dkp-pacman -S`):
- `switch-sdl2` `switch-sdl2_image` `switch-sdl2_ttf` `switch-sdl2_mixer` `switch-sdl2_net`

**Key flags:**
- `-fno-exceptions -fno-rtti` — standard Switch practice; no `try`/`catch` anywhere in codebase
- `-fPIE` — position-independent executable for NRO ASLR
- `ROMFS := romfs` — bundles `romfs/` into the NRO; accessible at runtime as `romfs:/`

**Desktop build:** Not supported by Makefile, but all non-libnx code compiles with a standard g++/clang++ if you stub `<switch.h>` and swkbd. `AssetManager` falls back to relative `base/` and `romfs/` paths on non-Switch.

---

## Repository Layout

```
ferris-ao-switch/
├── Makefile
├── icon.jpg                    # 256×256 NRO icon (shown in hbmenu)
├── romfs/                      # Bundled read-only assets
│   ├── fonts/noto_sans.ttf
│   ├── ui/                     # Chatbox, nameplate, HP bar, objection sprites
│   ├── characters/phoenix/     # Minimal fallback character
│   ├── sounds/sfx-blink.ogg
│   └── music/silence.ogg
└── src/
    ├── main.cpp                # Entry point
    ├── app.hpp / app.cpp       # Game loop, screen stack
    ├── net/                    # Networking layer
    ├── protocol/               # AO2 packet parsing and handlers
    ├── state/                  # Game state (main-thread only)
    ├── assets/                 # Asset loading and caching
    ├── audio/                  # SDL_mixer BGM + SFX
    ├── render/                 # SDL_Renderer wrapper, layout constants
    ├── ui/                     # Screen interface + all screens
    │   ├── screen.hpp
    │   ├── screens/            # Connect, CharSelect, AreaSelect, Courtroom
    │   └── courtroom/          # Courtroom sub-panels
    └── input/                  # Controller + system keyboard
```

---

## Module Reference

### `src/main.cpp`

Entry point. Constructs `App`, pushes `ConnectScreen` as the initial screen, calls `app.run()`. Does not hold any state itself — everything is owned by `App`.

---

### `src/app.hpp` / `src/app.cpp`

**Class:** `ao::App`

Owns the entire application lifecycle:
- SDL2 init: `SDL_Init`, `Mix_OpenAudio` (44100 Hz, stereo, 4096 chunk), `TTF_Init`, `IMG_Init`, `SDLNet_Init`, `romfsInit`
- Creates `SDL_Window` (1280×720 fullscreen) and `SDL_Renderer` (hardware accelerated, vsync)
- Instantiates `Renderer`, `InputManager`, `AudioManager`, `MusicPlayer`, `NetworkThread`, `AOClient`, `GameState`
- Holds the screen stack (`Screen* stack_[4]`, `int top_ = -1`)

**Screen stack:**
- `push_screen(Screen*)` — calls `on_exit()` on current, `on_enter()` on new
- `pop_screen()` — calls `on_exit()` on current, `on_enter()` on previous; never pops below 0
- Max depth 4 (Switch stack size concern — avoid deep nesting)

**Game loop (60Hz target):**
```
SDL_PollEvent → InputManager::feed_event()
NetworkThread: incoming_queue.pop() → AOClient::process()
AOClient::process() → mutates GameState
screen->handle_event(input_state)
screen->update(dt_ms)
screen->render()
Renderer::present()
```

All `GameState` mutation happens exclusively on the main thread via `AOClient::process()`. `NetworkThread` only touches its two `SPSCQueue` instances.

---

### `src/render/renderer.hpp` / `src/render/renderer.cpp`

**Class:** `ao::Renderer`

Thin wrapper around `SDL_Renderer*`. Provides:
- `clear()`, `present()`
- `draw_rect()`, `fill_rect()`, `draw_texture()`, `draw_texture_ex()` (rotation/flip)
- `set_draw_color(r,g,b,a)`, `set_blend_mode()`
- `get_sdl()` — exposes raw `SDL_Renderer*` for SDL_ttf / SDL_image calls

**`ao::Layout` namespace** — all coordinate constants as `constexpr SDL_Rect` / `constexpr int`:

| Constant | Value | Purpose |
|----------|-------|---------|
| `VIEWPORT` | 853×480 | Logical courtroom render target (letterboxed to 1280×720) |
| `CHAT_AREA` | bottom strip | Chatbox region |
| `SIDE_PANEL` | right strip | HP bars / button strip |
| `HP_DEF` | top of side panel | Defense HP bar |
| `HP_PRO` | below HP_DEF | Prosecution HP bar |
| `BTN_OOC` | — | OOC toggle button |
| `BTN_MUSIC` | — | Music toggle button |
| `BTN_EVIDENCE` | — | Evidence toggle button |
| `PANEL_OOC` | overlay | OOC chat panel area |
| `PANEL_MUSIC` | overlay | Music list panel area |
| `PANEL_EVIDENCE` | overlay | Evidence panel area |
| `PANEL_ICINPUT` | overlay | IC input composer area |

**Do not hard-code pixel values anywhere else.** Add new layout constants here.

---

### `src/ui/screen.hpp`

**Abstract class:** `ao::Screen`

All screens implement:
```cpp
virtual void on_enter() {}   // Called when pushed onto stack
virtual void on_exit()  {}   // Called when popped or another screen pushed above
virtual void handle_event(const InputState&) = 0;
virtual void update(uint32_t dt_ms) = 0;
virtual void render() = 0;
```

Screens receive a reference to `App` (or relevant subsystems) via constructor. They must not own network state — that lives in `App`.

---

### `src/state/game_state.hpp`

**Struct:** `ao::GameState` — single source of truth, **main-thread only**.

Never accessed from `NetworkThread`. `AOClient::process()` mutates it; screens read it.

**Key members:**

```cpp
static constexpr int MAX_CHARS    = 256;
static constexpr int MAX_AREAS    = 64;
static constexpr int MAX_MUSIC    = 512;
static constexpr int MAX_EVIDENCE = 48;

CharacterInfo chars[MAX_CHARS];   // Populated from SC packet
bool char_taken[MAX_CHARS];       // From CharsCheck packet
AreaInfo areas[MAX_AREAS];        // From SM + ARUP packets
char music[MAX_MUSIC][128];       // From SM packet
EvidenceEntry evidence[MAX_EVIDENCE]; // From LE packet
int char_count, area_count, music_count, evidence_count;

int my_char_id;
int my_uid;
int def_hp, pro_hp;               // 0–10 scale
char bg[256];
ICAnimState pending_ic;           // Latest MS packet, ready for courtroom to consume
bool has_pending_ic;
ChatLog ic_log;                   // Fixed 128-entry ring buffer
ChatLog ooc_log;
```

**`ICAnimState`** — parsed from all 30 fields of the MS server broadcast:
```cpp
struct ICAnimState {
    char char_name[64];
    char emote[64];
    char pre_anim[64];
    char message[1024];
    char sfx[128];
    char pos[16];
    char showname[64];
    int  emote_mod;       // 0=normal, 1=no pre-anim, 5/6=looping
    int  objection_mod;   // 0=none, 1=objection, 2=hold it!, 3=take that!
    int  desk_mod;
    int  text_color;
    int  flip;
    int  realization;
    int  immediate;
    int  looping_sfx;
    int  screenshake;
    bool additive;
    char effects[128];
    // pairing fields:
    int  other_char_id;
    char other_emote[64];
    int  self_offset;
    int  other_offset;
    int  other_flip;
};
```

**`ChatLog`** — fixed 128-entry ring buffer. `push(entry)` overwrites oldest. `get(i)` is 0=oldest, count-1=newest. Never allocates.

---

### `src/net/packet_queue.hpp`

**Template:** `ao::SPSCQueue<T, N>` — single-producer single-consumer lock-free ring buffer.

- `N` must be a power of 2 (asserted at compile time via `static_assert`)
- Uses `std::atomic<int>` head and tail — only one reader, one writer
- No heap allocation after construction
- `push(const T&)` → `bool` (false if full)
- `pop(T&)` → `bool` (false if empty)

**Instantiated as:**
```cpp
using InQueue  = SPSCQueue<InPacket,  256>;  // NetworkThread → main thread
using OutQueue = SPSCQueue<OutPacket, 64>;   // main thread → NetworkThread
```

**`InPacket`** / **`OutPacket`**: fixed `char buf[2048]` — no heap.

**Rule:** Never access these queues except from their designated producer/consumer thread. No exceptions.

---

### `src/net/network_thread.hpp` / `src/net/network_thread.cpp`

**Class:** `ao::NetworkThread`

Runs on a dedicated SDL thread (`SDL_CreateThread`). Owns raw socket(s). Communicates with main thread **only** via `InQueue` (outbound to main) and `OutQueue` (outbound from main).

**Public API:**
```cpp
bool connect(const char* host, int port, ConnMode mode); // TCP or WS
void disconnect();
bool is_connected() const;          // atomic
void stop();                        // sets stop_flag_, joins thread
InQueue&  incoming();               // main thread pops from this
OutQueue& outgoing();               // main thread pushes to this
```

**`ConnMode` enum:** `TCP`, `WS` (determined by URL prefix in ConnectScreen)

**Internal loops:**

`tcp_loop()`:
1. `SDLNet_CheckSockets(set, 1ms)`
2. If readable: `SDLNet_TCP_Recv` into rolling buffer
3. `extract_packets()` — splits buffer on `%`, pushes complete packets to `incoming_`
4. `outgoing_.pop()` → `SDLNet_TCP_Send`

`ws_loop()`:
1. Same recv into buffer
2. `ws_decode_frame()` on buffer bytes
3. If `FrameResult::Ping` → `send_pong()`
4. If `FrameResult::Close` → disconnect
5. If `FrameResult::Complete` → `extract_packets()` on frame payload → push to `incoming_`
6. `outgoing_.pop()` → `ws_encode_frame()` → send

**`extract_packets()`:** Scans recv buffer for `%` delimiter. Each `%`-terminated segment is a complete AO2 packet. Pushes each as one `InPacket`. Leftover bytes (partial packet) are moved to front of buffer.

**Disconnect sentinel:** On any error or `disconnect()`, pushes `InPacket` containing `"__DISCONNECT#%"` before stopping. `AOClient::process()` handles this to return to `ConnectScreen`.

---

### `src/net/ws_handshake.hpp` / `src/net/ws_handshake.cpp`

**Function:** `bool ao::ws_upgrade(TCPsocket sock, const char* host, const char* path)`

Performs the RFC 6455 HTTP upgrade:
1. Generates 16 random bytes → Base64 → `Sec-WebSocket-Key`
2. Sends `GET <path> HTTP/1.1` request with required headers
3. Reads response line by line until blank line (header end)
4. Computes expected `Sec-WebSocket-Accept`: `Base64(SHA1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))`
5. Compares against server's value — returns false if mismatch

**SHA-1:** Implemented inline (~80 lines). No external dependency.
**Base64:** Implemented inline (~20 lines). No external dependency.

**Blocking call** — must only be called from `NetworkThread`, not from the main thread. Never call from a screen's `handle_event` or `update`.

---

### `src/net/ws_frame.hpp` / `src/net/ws_frame.cpp`

**`ws_encode_frame(const char* payload, int len, char* out, int out_cap)`**
- Writes a binary (opcode `0x02`) WebSocket frame to `out`
- Client→server frames **must** be masked per RFC 6455 — uses `SDL_GetTicks()` XOR for 4-byte mask seed
- Returns bytes written, or -1 if `out_cap` insufficient

**`ws_decode_frame(const char* buf, int len, char* out, int out_cap, int& consumed)`**
- Returns `FrameResult` enum: `Complete`, `Incomplete`, `Ping`, `Close`, `Error`
- On `Complete`: payload is written to `out`, `consumed` is set to bytes used from `buf`
- On `Incomplete`: need more data — do not consume yet
- Handles FIN bit, masks server→client frames (RFC says server must not mask, but tolerates it)

**`FrameResult` enum:**
```cpp
enum class FrameResult { Complete, Incomplete, Ping, Close, Error };
```

---

### `src/protocol/packet.hpp`

**Struct:** `ao::Packet`

```cpp
struct Packet {
    static constexpr int MAX_FIELDS    = 35;
    static constexpr int MAX_FIELD_LEN = 2048;

    char header[32];
    char fields[MAX_FIELDS][MAX_FIELD_LEN];
    int  field_count;

    static bool parse(const char* raw, Packet& out); // returns false if malformed
    static void unescape(const char* in, char* out, int out_len);
    static void escape(const char* in, char* out, int out_len);
};
```

**Wire format:** `HEADER#field0#field1#...#%`

**Escaping:**
| Wire | Decoded |
|------|---------|
| `<percent>` | `%` |
| `<num>` | `#` |
| `<dollar>` | `$` |
| `<and>` | `&` |

`parse()` splits on `#`, sets `header` to tokens[0], `fields[i]` to tokens[1..n], strips trailing `%` entry.
`unescape()` must be called on individual field values before display or comparison.
`escape()` must be called on user-supplied strings before embedding in outgoing packets.

**No heap.** All storage is fixed char arrays. `parse()` truncates silently at `MAX_FIELD_LEN`.

---

### `src/protocol/commands.hpp`

Free functions in `ao::cmd::` namespace. All write to caller-provided stack buffers.

```cpp
namespace ao::cmd {
    int hi     (char* buf, int sz, const char* hdid);
    int id     (char* buf, int sz, const char* name, const char* ver);
    int askchaa(char* buf, int sz);
    int rc     (char* buf, int sz);
    int rm     (char* buf, int sz);
    int rd     (char* buf, int sz);
    int ch     (char* buf, int sz, int char_id);
    int cc     (char* buf, int sz, int uid, int char_id, const char* hdid);
    int ct     (char* buf, int sz, const char* name, const char* msg);
    int mc     (char* buf, int sz, const char* song, int char_id, const char* name);
    int hp     (char* buf, int sz, int bar, int value);
    int pe     (char* buf, int sz, const char* name, const char* desc, const char* image);
    int de     (char* buf, int sz, int idx);
    int ee     (char* buf, int sz, int idx, const char* name, const char* desc, const char* img);
    int zz     (char* buf, int sz, const char* reason);
    int rt     (char* buf, int sz, const char* anim);
    int setcase(char* buf, int sz, const char* desc, bool def, bool pro, bool jud, bool jur, bool steno);
    int ms     (char* buf, int sz, const MSParams& p);
}
```

**Usage pattern:**
```cpp
char buf[512];
int len = ao::cmd::cc(buf, sizeof(buf), uid, char_id, hdid);
outgoing_.push({buf, len});
```

All functions return the number of bytes written (not including null terminator). They call `Packet::escape()` internally on user-supplied strings.

**`MSParams`** — IC message parameters (client→server, 26 fields):
```cpp
struct MSParams {
    int  desk_mod;
    char pre_anim[64];
    char char_name[64];
    char emote[64];
    char message[1024];
    char pos[16];
    char sfx[128];
    int  emote_mod;
    int  char_id;
    int  evidence;
    int  objection_mod;
    int  evi_id;
    int  flip;
    int  realization;
    int  text_color;
    char showname[64];
    int  other_char_id;
    int  self_offset;
    int  immediate;
    int  looping_sfx;
    int  screenshake;
    // frame_screenshake, frame_realization, frame_sfx
    int  additive;
    char effects[128];
};
```

---

### `src/protocol/ao_client.hpp` / `src/protocol/ao_client.cpp`

**Class:** `ao::AOClient`

State machine for the AO2 handshake and ongoing packet dispatch. Called from the main thread each frame via `process(InQueue& q, OutQueue& out, GameState& gs)`.

**`HandshakeState` enum:**
```cpp
enum class HandshakeState {
    Idle,
    WaitDecryptor,   // ← decryptor#NOENCRYPT#%
    WaitId,          // ← ID#...#%
    WaitSi,          // ← SI#...#%
    WaitSc,          // ← SC#...#%
    WaitSm,          // ← SM#...#%
    WaitDone,        // ← DONE#%
    Connected,       // Full handshake complete
};
```

**Handshake sequence sent:**
```
→ HI#<hdid>#%
← ID + PN + FL
→ ID#ferris-ao-switch#0.1#%
→ askchaa#%
← SI#<chars>#<evi>#<music>#%
→ RC#%
← SC#char0#...#%
→ RM#%
← SM#areas+music#%
→ RD#%
← LE + CharsCheck + HP×2 + BN + DONE
→ CC#<uid>#<char_id>#<hdid>#%  (after user picks char)
```

**`process()` per-frame logic:**
1. `q.pop()` in a loop until empty
2. If `__DISCONNECT` → fire `on_disconnect` callback → return
3. Parse `Packet`
4. If in handshake state: call appropriate `on_<header>_hs()`
5. If `Connected`: dispatch to `on_<header>()`

**Key dispatch handlers:**

| Packet | Handler | What it does |
|--------|---------|--------------|
| `decryptor` | `on_decryptor` | Send `HI` |
| `ID` | `on_id` | Send `ID`, send `askchaa` |
| `SI` | `on_si` | Send `RC` |
| `SC` | `on_sc` | Populate `gs.chars[]`, send `RM` |
| `SM` | `on_sm` | Populate `gs.areas[]` + `gs.music[]`, send `RD` |
| `LE` | `on_le` | Populate `gs.evidence[]` |
| `CharsCheck` | `on_chars_check` | Set `gs.char_taken[]` |
| `DONE` | `on_done` | Transition to `Connected`, fire `on_ready` callback |
| `MS` | `on_ms` | Parse all 30 fields → `ICAnimState` → push to `gs.ic_log` |
| `CT` | `on_ct` | Push to `gs.ooc_log` |
| `MC` | `on_mc` | Update `gs.current_music`, call `MusicPlayer::play()` |
| `HP` | `on_hp` | Update `gs.def_hp` or `gs.pro_hp` |
| `BN` | `on_bn` | Update `gs.bg` |
| `ARUP` | `on_arup` | Update `gs.areas[i].players/status/cm/lock` by type 0–3 |
| `AUTH` | `on_auth` | Update `gs.authenticated` + username |
| `BD` | `on_bd` | Push disconnect with reason |

**Callbacks (set by `App` before connecting):**
```cpp
std::function<void()> on_ready;         // Handshake complete → push CharSelectScreen
std::function<void()> on_disconnect;    // Connection lost → pop to ConnectScreen
```

---

### `src/assets/asset_manager.hpp` / `src/assets/asset_manager.cpp`

**Static class:** `ao::AssetManager`

Three-tier asset resolution with HTTP streaming support.

**Priority order:**

| Tier | Source | When active |
|------|--------|-------------|
| 1 | HTTP — `<asset_url>/<relative>` | Server sent an `ASS` packet with a CDN URL |
| 2 | `sdmc:/switch/ferris-ao/base/<relative>` | SD card base folder present |
| 3 | `romfs:/<relative>` | Always (bundled fallback) |

Tier 1 is activated by `set_asset_url(url)` (called from `AOClient::on_ass`). The local base folder (tier 2) is entirely **optional** — servers without a CDN fall through to romfs for bundled fallbacks, and servers with a CDN stream everything on demand without requiring any SD card content.

**API:**

```cpp
// URL management — call from main thread only
static void set_asset_url(const char* url);  // set from ASS packet
static void clear_asset_url();               // called on disconnect
static bool        has_asset_url();
static const char* asset_url();              // "" if unset

// Local path resolution (no HTTP) — fast existence check
static bool resolve(const char* relative, char* out_path, int out_cap);

// Full three-tier data fetch — may block (network I/O or disk read)
// Returns SDL_malloc'd buffer; caller must SDL_free() it.
static uint8_t* fetch_bytes(const char* relative, int* out_size);

// Full three-tier SDL_RWops — owns its buffer; SDL_RWclose() frees it.
// Pass to IMG_Load_RW, IMG_LoadAnimation_RW, Mix_LoadMUS_RW, etc. with freesrc=1.
static SDL_RWops* open_rwops(const char* relative);

// Called by AssetStream to pre-populate the prefetch cache.
// AssetManager takes ownership of `data` (SDL_malloc'd).
static void store_prefetch(const char* relative, uint8_t* data, int size);
```

**`open_rwops` returns a custom owning `SDL_RWops`** (`SDL_AllocRW()` + custom function pointers). When the returned ops is closed (by SDL_image, SDL_mixer, or manually), the underlying buffer is freed automatically. There is no need to track the buffer separately.

**`fetch_bytes` consumption order:**
1. Check `AssetStream`'s prefetch cache — if found, remove and return immediately (no I/O)
2. HTTP GET from `<asset_url>/<relative>`
3. Read from `sdmc:/switch/ferris-ao/base/<relative>`
4. Read from `romfs:/<relative>`

**Prefetch cache:** Fixed array of 32 `PrefetchEntry` slots guarded by an `SDL_mutex`. FIFO eviction when full. `store_prefetch` is called by `AssetStream`; `fetch_bytes` calls `consume_prefetch` (removes the entry, single-use).

**All callers use relative paths** — never resolved absolute paths. The relative path is also the cache key in `TextureCache`.

**`resolve()` is local-only** (for quick existence checks, char.ini parsing, etc.). For any actual data loading, use `fetch_bytes` or `open_rwops`.

---

### `src/net/http_fetch.hpp` / `src/net/http_fetch.cpp`

**Function:** `ao::HttpResult http_get(const char* url)`

Synchronous HTTP/1.1 GET over SDL_net TCP. Plain HTTP only — no TLS.

**`HttpResult`:**
```cpp
struct HttpResult {
    uint8_t* data;  // SDL_malloc'd; call result.free() when done
    int      size;
    bool     ok;    // false on non-200, DNS failure, timeout, or body error
    void     free();
};
```

**Limits:**
- `HTTP_MAX_BODY = 32 MB` — hard cap; fails if server sends more
- `HTTP_TIMEOUT_MS = 8000` — per-read socket timeout

**Supported transfer modes:**
- `Content-Length` — exact byte count read
- `Transfer-Encoding: chunked` — chunk sizes decoded, body assembled
- Neither — reads until server closes connection (fallback)

**Blocking call.** Call only from worker threads (`AssetStream`, `NetworkThread`). Never call from the main thread or from inside `App::run()`.

**Thread safety:** safe to call concurrently from multiple threads (each call opens its own socket).

---

### `src/assets/asset_stream.hpp` / `src/assets/asset_stream.cpp`

**Class:** `ao::AssetStream`

Background SDL thread that pre-warms the `AssetManager` prefetch cache. Prevents hitches when assets are first needed by downloading them before the render loop requests them.

```cpp
void start();
void stop();

// Queue a relative path for background prefetch.
// Returns false if request queue is full (retry next frame).
// Silently deduplicates requests already in the queue.
bool prefetch(const char* relative);

// Drain completed prefetches (for logging/debugging — optional).
bool poll_done(char* out_path, int out_cap);
```

**`STREAM_QUEUE_SIZE = 64`** — max concurrent pending requests.

**Worker loop:**
1. Block on `SDL_CondWait` until `prefetch()` signals work
2. Pop relative path from request queue
3. Call `AssetManager::fetch_bytes(rel, &size)` (HTTP → sdmc: → romfs:)
4. Call `AssetManager::store_prefetch(rel, data, size)`
5. Push path to done queue; repeat

**Integration:** `AssetStream` is owned by `App`. `CourtroomScreen` calls `app_.stream().prefetch(rel)` when it knows an IC message is incoming, pre-loading the character sprites and SFX before they are needed.

**Effect on `open_rwops` / `fetch_bytes`:** The next call for a pre-fetched path returns immediately from the in-memory prefetch cache — no HTTP round-trip, no disk read, no frame hitch.

**Without `AssetStream`:** Everything still works. `open_rwops` falls back to synchronous HTTP/local fetch on first use. Use `AssetStream` when low-latency frame rendering matters.

---

### `src/assets/texture_cache.hpp` / `src/assets/texture_cache.cpp`

**Class:** `ao::TextureCache`

LRU cache for `SDL_Texture*`. 64 slots. Evicts least-recently-used on overflow.

```cpp
SDL_Texture* get(SDL_Renderer* r, const char* rel_path);
void release(const char* rel_path);
void clear();
```

- `get()` checks cache first (strcmp on relative path); on miss, calls `AssetManager::open_rwops(rel)` → `IMG_LoadTexture_RW(r, rw, 1)`
- The cache key and stored path are **relative paths** — never resolved absolute paths
- On HTTP streaming: first request for an uncached path fetches from server; subsequent calls return cached texture instantly
- Sets `last_used = SDL_GetTicks()` on every access (hit or miss)
- Returns `nullptr` if asset not found anywhere (log to stderr, caller must handle gracefully)

**Slot struct:**
```cpp
struct TexEntry {
    char         path[256];
    SDL_Texture* tex;
    uint32_t     last_used;
};
```

---

### `src/assets/apng_player.hpp` / `src/assets/apng_player.cpp`

**Class:** `ao::APNGPlayer`

Plays APNG/GIF/animated-WebP animations frame-by-frame using SDL2_image ≥ 2.6's `IMG_LoadAnimation_RW()`.

```cpp
bool load(const char* path, SDL_Renderer* r);
void update(uint32_t dt_ms);           // Advance frame timer
SDL_Texture* current_frame() const;   // Currently visible frame texture
bool finished() const;                 // True if non-looping and last frame done
void set_loop(bool loop);
void reset();
void free_frames();
```

**Internals:**
- `IMG_LoadAnimation_RW()` → `IMG_Animation*` (array of `SDL_Surface*` + delays in ms); handles GIF, APNG, animated WebP
- Converts each surface to `SDL_Texture*` (up to `MAX_FRAMES = 128`)
- `update(dt_ms)` accumulates `elapsed_ms_`; when ≥ frame delay, advances `frame_idx_`
- If non-looping: stays on last frame, sets `finished_ = true`

**Fallback:** If `IMG_LoadAnimation_RW()` returns null (static image), opens a second `AssetManager::open_rwops()` call and uses `IMG_Load_RW()` as a single-frame "animation".

**HTTP streaming:** `load(path)` takes a relative path and resolves it via `AssetManager::open_rwops()` — HTTP CDN first, then local base, then romfs. No caller path-resolution needed.

**Resource management:** `unload()` destroys all `SDL_Texture*` and zeros arrays. Called automatically in `load()` before loading a new animation.

---

### `src/assets/char_ini_parser.hpp` / `src/assets/char_ini_parser.cpp`

**Function:** `bool ao::parse_char_ini(const char* path, CharDef& out)`

Parses `characters/<name>/char.ini` — standard AO2 Windows INI format.

**`CharDef` struct:**
```cpp
struct EmotionEntry {
    char name[64];
    char pre_anim[64];
    char anim_base[64]; // Without (a)/(b) suffix
    int  desk_mod;
};

struct CharDef {
    char          display_name[64];
    char          showname[64];
    EmotionEntry  emotions[64];
    int           emotion_count;
};
```

**Parsing behavior:**
- `[Options]` section: reads `name =` and `showname =`
- `[Emotions]` section: reads entries `1 = Name#PreAnim#AnimBase#DeskMod#`, `2 = ...`
- Lines starting with `;` or `#` are comments
- Silently skips malformed lines
- Returns false if file cannot be opened

**Used by `CharSelectScreen`** to load display names for the character grid, and by **`CourtroomScreen`** to resolve emotion sprite paths.

---

### `src/audio/audio_manager.hpp` / `src/audio/audio_manager.cpp`

**Class:** `ao::AudioManager`

SFX pool using SDL_mixer.

```cpp
bool init();                          // Call after Mix_OpenAudio; allocates SFX_CHANNELS=8 channels
bool play_sfx(const char* path);     // Cached; resolves via AssetManager
void stop_sfx();                     // Halt all SFX channels
void set_sfx_volume(int vol);        // 0–128 (MIX_MAX_VOLUME)
```

**Cache:** `SfxEntry sfx_cache_[16]` — LRU by `last_used = SDL_GetTicks()`. `evict_sfx()` finds oldest occupied slot when all 16 are full.

**`play_sfx(path)`:** takes a relative path.
1. `find_sfx(path)` — linear search on relative path strings
2. Miss: `AssetManager::open_rwops(path)` → `Mix_LoadWAV_RW(rw, 1)`, evict LRU if needed
   - `freesrc=1`: SDL_mixer decodes WAV/OGG entirely upfront, so the RWops is not needed after load
3. Hit or loaded: `Mix_PlayChannel(-1, chunk, 0)`

---

### `src/audio/music_player.hpp` / `src/audio/music_player.cpp`

**Class:** `ao::MusicPlayer`

BGM with crossfade.

```cpp
void play(const char* path, int fade_ms = 300);
void stop();
bool is_playing() const;
void set_volume(int vol);          // 0–128
const char* current() const;      // Path of currently playing track
```

**`play()` behavior:**
- If `path == "~~"` → `stop()` (AO2 stop-music sentinel)
- Calls `AssetManager::open_rwops(path)`, then tries `music/<path>` prefix fallback (HTTP → sdmc: → romfs:)
- If music already playing: `Mix_HaltMusic()` + `Mix_FreeMusic()` + `SDL_RWclose(music_rw_)`
- `Mix_LoadMUS_RW(rw, 0)` with `freesrc=0` — we keep `rw` alive in `music_rw_` because SDL_mixer streams from it
- `Mix_FadeInMusic(music_, -1, fade_ms)` (loop forever)
- Updates `current_path_`

**`music_rw_` lifetime:** SDL_mixer streams OGG/MP3 from the RWops rather than loading everything upfront. `music_rw_` must stay alive until `stop()` is called. `stop()` calls `Mix_FreeMusic` first (stops streaming), then `SDL_RWclose(music_rw_)` (frees the buffer from `open_rwops`). Never close `music_rw_` while music is playing.

---

### `src/input/input_manager.hpp` / `src/input/input_manager.cpp`

**Class:** `ao::InputManager`

Maps SDL controller/keyboard events to `Action` enum. Screens only ever read `InputState`, never raw SDL events.

**`Action` enum:**
```cpp
enum class Action {
    Up, Down, Left, Right,
    Confirm,      // A / Enter
    Back,         // B / Escape
    Menu,         // X
    Secondary,    // Y
    TabL,         // L
    TabR,         // R
    TriggerL,     // ZL
    TriggerR,     // ZR
    Plus,         // + / P
    Minus,        // - / M
    ScrollUp,     // Right stick up / mouse wheel up
    ScrollDown,   // Right stick down / mouse wheel down
    COUNT
};
```

**`InputState` struct:**
```cpp
struct InputState {
    bool pressed [static_cast<int>(Action::COUNT)];   // True only on the frame it went down
    bool held    [static_cast<int>(Action::COUNT)];   // True while held
    bool released[static_cast<int>(Action::COUNT)];   // True only on the frame it went up
};
```

**`begin_frame()`** — clears `pressed` and `released` arrays (must be called at start of each game loop iteration before `SDL_PollEvent`).

**`feed_event(SDL_Event&)`** — updates all three arrays for the event's action.

**Controller mapping:**

| SDL Button | Action |
|-----------|--------|
| `SDL_CONTROLLER_BUTTON_DPAD_UP` | Up |
| `SDL_CONTROLLER_BUTTON_DPAD_DOWN` | Down |
| `SDL_CONTROLLER_BUTTON_DPAD_LEFT` | Left |
| `SDL_CONTROLLER_BUTTON_DPAD_RIGHT` | Right |
| `SDL_CONTROLLER_BUTTON_A` | Confirm |
| `SDL_CONTROLLER_BUTTON_B` | Back |
| `SDL_CONTROLLER_BUTTON_X` | Menu |
| `SDL_CONTROLLER_BUTTON_Y` | Secondary |
| `SDL_CONTROLLER_BUTTON_LEFTSHOULDER` | TabL |
| `SDL_CONTROLLER_BUTTON_RIGHTSHOULDER` | TabR |
| `SDL_CONTROLLER_BUTTON_LEFTSTICK` axis ZL | TriggerL |
| `SDL_CONTROLLER_BUTTON_RIGHTSTICK` axis ZR | TriggerR |
| `SDL_CONTROLLER_BUTTON_START` | Plus |
| `SDL_CONTROLLER_BUTTON_BACK` | Minus |

Keyboard fallback: WASD→direction, Enter→Confirm, Escape→Back, Tab→TabR, Q→TabL, etc.

---

### `src/input/virtual_keyboard.hpp` / `src/input/virtual_keyboard.cpp`

**Function:** `bool ao::show_keyboard(const char* hint, char* out, int out_len, int max_len)`

**On Switch (`__SWITCH__` defined):**
```cpp
SwkbdConfig kbd;
swkbdCreate(&kbd, 0);
swkbdConfigMakePresetDefault(&kbd);
swkbdConfigSetHeaderText(&kbd, hint);
swkbdConfigSetStringLenMax(&kbd, max_len);
Result rc = swkbdShow(&kbd, out, out_len);
swkbdClose(&kbd);
return R_SUCCEEDED(rc);
```

**On desktop:** Prints hint to stdout, reads a line from stdin into `out`.

**Blocking call.** On real hardware, this suspends the homebrew and shows the system keyboard. On Ryujinx, it opens the emulated keyboard. **Never call this from a render loop frame without tracking that you're waiting for input.** Typical usage:

```cpp
// In handle_event, when user presses Confirm on a text field:
if (waiting_for_keyboard_) return;
waiting_for_keyboard_ = true;
// Keyboard is blocking — on Switch this blocks until dismissed
show_keyboard("Server address", host_buf_, sizeof(host_buf_), 63);
waiting_for_keyboard_ = false;
```

---

### `src/ui/screens/connect_screen.hpp` / `connect_screen.cpp`

First screen the user sees. Three fields: Host, Port, Username.

- D-pad Up/Down → select field
- A (Confirm) → open system keyboard for selected field
- ZR (TriggerR) → attempt connection
- Renders colored rows with selection highlight (yellow border on active field)
- Calls `App::connect(host, port, mode)` which starts `NetworkThread` and enters handshake
- `ConnMode` determined by URL prefix: `ws://` → WS, `wss://` → WS (TLS not implemented), else → TCP

**After successful `DONE` from server:** `AOClient::on_ready` callback fires → `app.push_screen(new CharSelectScreen(...))`

---

### `src/ui/screens/char_select_screen.hpp` / `char_select_screen.cpp`

8×4 grid of character slots (32 per page). Infinite pages (scroll wraps at `gs.char_count`).

- D-pad → navigate grid
- L/R (TabL/TabR) → prev/next page
- A → select character if not taken → sends `CC` packet → `app.push_screen(new AreaSelectScreen(...))`
- B → back to ConnectScreen (sends disconnect)
- Taken slots rendered dimmed; available slots rendered with character name and colored square placeholder
- Character names read from `gs.chars[i].name`

**Does not load textures.** Character portrait loading is deferred to CourtroomScreen for memory reasons.

---

### `src/ui/screens/area_select_screen.hpp` / `area_select_screen.cpp`

Scrollable list showing up to 10 areas at a time. Reads `gs.areas[]` for player counts, status, CM, lock state.

- D-pad Up/Down → move selection
- A → join area (no separate packet needed — already joined via `CC`)
- B → back to CharSelectScreen (re-enters char select without reconnecting)
- Renders: area name, player count, status string, lock indicator
- Pushes `CourtroomScreen` on A

---

### `src/ui/screens/courtroom_screen.hpp` / `courtroom_screen.cpp`

Main gameplay screen. Most complex screen. Owns animation state.

**`CourtroomPanel` enum:**
```cpp
enum class CourtroomPanel { None, OOC, Music, Evidence, ICInput };
```

**Layout regions (from `Layout` namespace):**
- Viewport (853×480): character sprite + background
- Chat area: chatbox typewriter text
- Side panel: HP bars, button strip
- Overlays: OOC, Music, Evidence, ICInput panels (semi-transparent, slide in/out)

**Core update loop per frame:**
1. If `gs.has_pending_ic`: consume `gs.pending_ic` → start IC animation sequence
2. IC animation state machine:
   - `PRE_ANIM`: play `pre_anim` GIF once (skip if `emote_mod == 1`)
   - `OBJECTION`: show objection popup (0.5s), play SFX
   - `REALIZATION`: white flash overlay (0.3s)
   - `TALKING`: show `<emote>(b).png`, run chatbox typewriter (35ms/char)
   - `IDLE`: switch to `<emote>(a).png`, wait for next IC
3. Screenshake: accumulate offset from `screenshake` flag, decay over 500ms
4. HP bar renders: 10-segment bar (green = def, red = pro)

**Controller mapping in courtroom:**

| Button | Action |
|--------|--------|
| ZL | Toggle OOC panel |
| ZR | Toggle Music panel |
| Y | Toggle Evidence panel |
| X | Open IC Input overlay |
| A | Skip typewriter (jump to end of current message) |
| B | Close active panel |
| + | Disconnect → pop to ConnectScreen |
| Right stick Up/Down | Scroll OOC/Music/Evidence list |

**IC Input overlay (`ic_input.hpp`):**
- Shows current showname, character, emote selector (D-pad L/R to cycle)
- A → open system keyboard for message text
- ZR → send `MS` packet (via `OutQueue`)
- Reads `CharDef` to populate emote list

---

### `src/ui/courtroom/chatbox.hpp` / `chatbox.cpp`

Typewriter chatbox. Called from `CourtroomScreen`.

```cpp
void start(const char* text, int text_color, bool additive);
void update(uint32_t dt_ms);
bool is_done() const;
void skip();                      // Jump to end immediately
void render(Renderer& r, TTF_Font* font, const SDL_Rect& bounds);
```

- 35ms per character default speed
- `additive = true` → new message appended to previous (don't clear)
- `text_color` → maps to SDL_Color (AO2 color codes 0–7)
- Renders only visible characters up to current position

**Color map:**

| Code | Color |
|------|-------|
| 0 | White |
| 1 | Green |
| 2 | Red |
| 3 | Orange |
| 4 | Blue |
| 5 | Yellow |
| 6 | Rainbow (cycles) |
| 7 | Pink |

---

## Threading Model

```
Main Thread (60Hz)                  NetworkThread (SDL thread)
  InputManager::begin_frame()         SDLNet_CheckSockets(1ms timeout)
  SDL_PollEvent loop                  Recv bytes into rolling buffer
  incoming_queue.pop() loop           extract_packets() → split on '%'
    AOClient::process(packet)         push InPacket to incoming_queue
      → mutate GameState
  screen->handle_event()              outgoing_queue.pop()
  screen->update(dt_ms)               ws_encode_frame() / raw send
  screen->render()
  Renderer::present()
```

**Synchronization:** Only the two `SPSCQueue` instances cross thread boundaries. `GameState` is exclusively main-thread. No mutexes on the hot path. No condition variables. No `std::thread`.

**SDL thread:** `NetworkThread` uses `SDL_CreateThread` / `SDL_WaitThread` (not `std::thread`) because libnx's POSIX thread support is limited and SDL abstracts the difference correctly.

---

## AO2 Protocol Quick Reference

### Wire format

```
HEADER#field0#field1#...#%
```

Fields split on `#`. Packet terminated by `%`. Escaping required:

| In message | On wire |
|-----------|---------|
| `%` | `<percent>` |
| `#` | `<num>` |
| `$` | `<dollar>` |
| `&` | `<and>` |

Always call `Packet::escape()` before building outgoing packets and `Packet::unescape()` before displaying received fields.

### MS packet — server broadcast (30 fields, 0-indexed)

```
[0]  desk_mod       [1]  pre_anim       [2]  char_name      [3]  emote
[4]  message        [5]  pos            [6]  sfx            [7]  emote_mod
[8]  char_id        [9]  evidence       [10] objection_mod  [11] evi_id
[12] flip           [13] realization    [14] text_color     [15] showname
[16] other_charid   [17] other_name     [18] other_emote    [19] self_offset
[20] other_offset   [21] other_flip     [22] immediate      [23] looping_sfx
[24] screenshake    [25] frame_screenshake [26] frame_realization [27] frame_sfx
[28] additive       [29] effects
```

`[17..21]` are server-inserted pairing fields. `[22..29]` map from client fields `[18..25]`.

### Handshake sequence

```
← decryptor#NOENCRYPT#%
→ HI#<hdid>#%
← ID#0#<name>#<ver>#%  ← PN#<n>#<max>#<desc>#%  ← FL#flags...#%
→ ID#ferris-ao-switch#0.1#%
→ askchaa#%
← SI#<chars>#<evi>#<music>#%
→ RC#%
← SC#char0#char1#...#%
→ RM#%
← SM#area0#area1#...#song0#song1#...#%
→ RD#%
← LE#...#%  ← CharsCheck#...#%  ← HP#1#v#%  ← HP#2#v#%  ← BN#bg#%  ← DONE#%
→ CC#<uid>#<char_id>#<hdid>#%
```

---

## Asset Streaming System

### Overview

The asset streaming system allows the client to load assets directly from a server's HTTP CDN without requiring users to pre-download a base pack to their SD card. The local base folder and romfs are used as fallbacks.

```
Server sends ASS#http://cdn.example.com/ao-base#%
         │
         ▼
AOClient::on_ass() → AssetManager::set_asset_url("http://cdn.example.com/ao-base")
         │
         ▼ (on any open_rwops / fetch_bytes call)
AssetManager checks:
  [1] Prefetch cache (populated by AssetStream background thread)
       └─ hit: return immediately, no I/O
  [2] HTTP GET http://cdn.example.com/ao-base/<rel>
       └─ hit: return SDL_malloc'd buffer
  [3] sdmc:/switch/ferris-ao/base/<rel>
       └─ hit: read file, return buffer
  [4] romfs:/<rel>
       └─ hit: read file, return buffer
       └─ miss: return nullptr → asset not found
```

### Without a CDN (no ASS packet)

Tier 1 (HTTP) is skipped entirely. The client uses the local base folder (tier 2) and romfs fallbacks (tier 3). This is the experience for servers that don't host a CDN.

Users who want the full character roster on such servers should drop an AO2 base pack to `sdmc:/switch/ferris-ao/base/` on their SD card.

### With a CDN (ASS packet received)

All asset loading goes through HTTP first. Users need no local files at all — everything streams on demand. The romfs fallbacks still apply for UI chrome (chatbox, HP bars, fonts) which is always bundled.

### Owning SDL_RWops

`open_rwops()` returns a custom `SDL_RWops` (allocated via `SDL_AllocRW()`) with function pointers that own the underlying `SDL_malloc`'d buffer. Calling `SDL_RWclose()` on it frees the buffer. This means:

- `IMG_Load_RW(rw, 1)` — SDL_image decodes and closes rw → buffer freed ✓
- `IMG_LoadAnimation_RW(rw, 1)` — SDL_image decodes and closes rw → buffer freed ✓
- `IMG_LoadTexture_RW(r, rw, 1)` — SDL_image decodes and closes rw → buffer freed ✓
- `Mix_LoadWAV_RW(rw, 1)` — SDL_mixer decodes fully, closes rw → buffer freed ✓
- `Mix_LoadMUS_RW(rw, 0)` — SDL_mixer **streams** from rw → keep rw alive in `music_rw_`; close after `Mix_FreeMusic`

### ASS packet timing

The `ASS` packet arrives during the handshake, between `FL` and `SI`. `AssetManager::set_asset_url()` is called immediately in `on_ass()`. All subsequent asset loads (character list portraits, backgrounds) use the URL.

### URL cleared on disconnect

`AOClient::on_disconnected()` calls `AssetManager::clear_asset_url()`. The next server connection may have a different CDN URL (or none at all).

---

## AO2 Theme System

### Overview

`ThemeManager` (`src/assets/theme_manager.hpp` / `theme_manager.cpp`) parses standard AO2 desktop-client themes and drives the courtroom UI layout at runtime. Any theme that ships with the AO2 desktop base pack works without modification.

`App` owns a `ThemeManager theme_manager_` and exposes it via `App::theme()`. `App::init()` calls `theme_manager_.load("default")` after romfs is mounted.

`CourtroomScreen` reads `app_.theme().layout()` at render time — every rect is live from the theme.

### File search order

`ThemeManager::load(name)` tries these paths via `AssetManager::fetch_bytes`:

1. `misc/<name>/courtroom_design.ini` — classic AO2 base-pack location
2. `themes/<name>/courtroom_design.ini` — newer AO2 theme location
3. Built-in defaults (`Layout::` constants from `renderer.hpp`) — used when neither file is found

Sound names are loaded from `misc/<name>/courtroom_sounds.ini` (or `themes/<name>/`) alongside the design file. If absent, sound name fields are left empty (callers must guard with `sfx[0] != '\0'`).

### ThemeLayout struct

```cpp
struct ThemeLayout {
    SDL_Rect viewport;       // Background + character sprite area
    SDL_Rect chatbox;        // Chatbox background image bounds
    SDL_Rect ic_text;        // IC message text display area (inside chatbox)
    SDL_Rect nameplate;      // Showname / character nameplate
    SDL_Rect hp_def;         // Defense HP bar
    SDL_Rect hp_pro;         // Prosecution HP bar
    SDL_Rect log;            // OOC log scrollback area (side panel)
    SDL_Rect music_name;     // Currently playing music name strip
    SDL_Rect btn_ooc;        // OOC toggle button
    SDL_Rect btn_music;      // Music toggle button
    SDL_Rect btn_evidence;   // Evidence toggle button
    SDL_Rect panel_ooc;      // OOC overlay panel
    SDL_Rect panel_music;    // Music overlay panel
    SDL_Rect panel_evidence; // Evidence overlay panel
    char sfx_realization[64];
    char sfx_testimony[64];
    char sfx_cross[64];
    char sfx_blink[64];      // IC typewriter tick
    char sfx_objection[64];
    char sfx_holdit[64];
    char sfx_takethat[64];
    char sfx_guilty[64];
    char sfx_notguilty[64];
};
```

### Coordinate scaling

AO2 themes are authored at a base resolution (default 960×540; overridden by `[version] width`/`height` keys). `ThemeManager` reads this resolution from `[version]` and scales all coordinates linearly to 1280×720 on load:

```
sx = 1280.0f / base_w_
sy = 720.0f  / base_h_
```

No per-frame scaling is done — all rects in `ThemeLayout` are already in 1280×720 screen space.

### INI parsing

`parse_ini_bytes(data, size, cb, ud)` is an allocation-free tokenizer that handles:
- `[Section]` headers
- `key = value` pairs (whitespace-trimmed)
- `;` and `#` comment lines

The callback signature is `void cb(const char* section, const char* key, const char* val, void* ud)`.
`design_cb` and `sounds_cb` are the two callbacks — they write to `ThemeManager::raw_` and `ThemeLayout::sfx_*` respectively.

### Resolve helpers

```cpp
// "sfx-blink" → "misc/default/sounds/sfx-blink.ogg"
//               or "sounds/general/sfx-blink.ogg" if theme_dir unknown
bool resolve_sfx(const char* sfx_name, char* out_path, int out_cap) const;

// "chatbox" → "misc/default/chatbox.png"
bool resolve_image(const char* image_name, char* out_path, int out_cap) const;
```

Pass the result to `AssetManager::open_rwops()`.

### Changing theme at runtime

Call `theme_manager_.load("newtheme")` — parses and rescales immediately. `CourtroomScreen` picks up the new layout on the next render frame (it reads `layout()` each frame). No screen reload needed.

### Derived panels

`ThemeManager` derives `panel_ooc`, `panel_music`, and `panel_evidence` from the `log` rect during `scale_layout` — they are positioned to cover the log area and extend full-height. Themes that define explicit panel rects override this. If a theme has no explicit panel section, all three panels share the same position (the log rect extended to screen height).

---

## Common Gotchas

### 1. No exceptions, no RTTI

`-fno-exceptions -fno-rtti` is mandatory. Never use `try`/`catch`, `dynamic_cast`, or `typeid`. Error handling must use return values or output parameters.

### 2. No heap in hot path

`Packet`, `InPacket`, `OutPacket`, `ICAnimState`, `ChatLog`, `SPSCQueue` all use fixed-size arrays. `std::string` and `std::vector` are fine in initialization code (e.g., char.ini parsing) but must not appear in per-frame paths.

### 3. `ws_upgrade` and `show_keyboard` block

Both functions are **synchronous blocking calls**. They must not be called from `App::run()`'s frame loop directly. `ws_upgrade` is called inside `NetworkThread::connect()` (on the network thread). `show_keyboard` is called from screen event handlers — it blocks the entire main thread (and game loop) until the user dismisses the keyboard, which is acceptable on Switch.

### 4. `__DISCONNECT` sentinel

When `NetworkThread` drops the connection (error, server close, or `disconnect()` called), it pushes an `InPacket` containing `"__DISCONNECT#%"` to `incoming_queue` before the thread exits. `AOClient::process()` detects this and fires `on_disconnect`. Do not look for `"DISCONNECT"` without the `__` prefix — that's not an AO2 packet.

### 5. `IMG_LoadAnimation_RW` requires SDL2_image ≥ 2.6; animated WebP needs `switch-libwebp`

SDL2_image 2.6+ is required for `IMG_LoadAnimation_RW`. Never use `IMG_Load` for animations. Supported animated formats: GIF, APNG, animated WebP. Static formats (PNG, WebP) fall back to `IMG_Load_RW` automatically in `APNGPlayer::load`.

Animated WebP requires both `libwebpdemux` and `libwebp` linked in that order (`-lwebpdemux -lwebp`). If `switch-libwebp` is missing, animated WebP silently falls back to single-frame — `IMG_LoadAnimation_RW` returns null and `APNGPlayer` retries with `IMG_Load_RW`.

### 6. `romfsInit()` must be called before any `romfs:/` access

`App::App()` calls `romfsInit()`. If it fails (not running from NRO context on Switch, or wrong romfs embed), `romfs:/` paths will not resolve. `AssetManager::resolve()` will still try the `sdmc:` path first.

### 7. SPSC queue direction

- `incoming_queue`: NetworkThread writes (push), main thread reads (pop)
- `outgoing_queue`: main thread writes (push), NetworkThread reads (pop)

Never invert this. No other thread should ever touch these queues.

### 8. Screen pointer ownership

`App::push_screen(Screen* s)` takes raw pointer ownership. `pop_screen()` calls `delete` on the popped screen. Never `delete` a screen manually. Never push a stack-allocated screen.

### 9. `SDL_GetTicks()` overflow

`SDL_GetTicks()` returns `uint32_t` milliseconds, which wraps at ~49 days. LRU comparisons (`last_used`) use subtraction, which handles wrapping correctly for differences < 2^31 ms. Do not use `>` / `<` directly for LRU timestamps — always use subtraction.

### 10. `http_get` is synchronous and blocking

`http_get()` blocks until the response is complete or times out (8 seconds). Never call it from the main thread or from `App::run()`. It is called from:
- `AssetStream` worker thread (via `fetch_bytes`)
- `NetworkThread` indirectly (via `open_rwops` in music/sfx loading, but those happen in AOClient handlers on the main thread — acceptable one-time stall on MC packet)

If latency-sensitive loading is needed, call `asset_stream.prefetch()` before the asset is required.

### 11. `music_rw_` must outlive Mix_Music

`MusicPlayer` holds `music_rw_` alongside `music_`. `music_rw_` is the `open_rwops()` result; SDL_mixer streams from it. Always call `Mix_FreeMusic(music_)` before `SDL_RWclose(music_rw_)`. The `stop()` method does this correctly. Do not reorder these calls.

### 12. Character sprite path convention

```
Idle:     characters/<name>/emotions/<emote>(a).png
Talk:     characters/<name>/emotions/<emote>(b).png
Pre-anim: characters/<name>/<pre_anim>.gif
```

The `emote` field from the MS packet is the base name (no suffix). Append `(a)` or `(b)` before calling `AssetManager::open_rwops()` or `TextureCache::get()`. Pre-anims use the full name from `[Emotions]` ini section field 2.

### 11. `SDLNet_TCP_Send` is not frame-safe on partial sends

`SDLNet_TCP_Send` may return fewer bytes than requested (partial send). The network thread must retry with the remaining bytes. The current implementation loops on partial sends in `tcp_loop()`.

### 12. WebSocket mask seed

`ws_encode_frame()` uses `SDL_GetTicks() ^ (uintptr_t)payload` as the 32-bit mask key. This is sufficient for protocol compliance (RFC 6455 requires masking but does not require cryptographic randomness for the mask) and avoids calling into a CSPRNG on every packet.

### 13. `AOClient` does not own `GameState`

`AOClient::process()` takes `GameState&` by reference. `GameState` is owned by `App`. This means `AOClient` can read and write `GameState` freely but must not outlive `App`.

### 14. ARUP type mapping

```
ARUP#0#... → player counts   (gs.areas[i].players)
ARUP#1#... → status strings  (gs.areas[i].status)
ARUP#2#... → CM names        (gs.areas[i].cm_name)
ARUP#3#... → lock state      (gs.areas[i].lock)
```

Each `ARUP` packet has one field per area in server order. `on_arup()` iterates up to `gs.area_count` fields.

---

## IC Animation Flow

When `AOClient::on_ms()` populates `gs.pending_ic`:

```
CourtroomScreen::update()
  1. PRE_ANIM phase:
     - Load characters/<name>/<pre_anim>.gif via APNGPlayer
     - Play once (APNGPlayer::set_loop(false))
     - Wait for APNGPlayer::finished()
     - Skip if emote_mod == 1 (preanim disabled)

  2. OBJECTION phase (if objection_mod > 0):
     - Load ui/objection.png (or holdit.png / takethat.png)
     - Display for 500ms
     - Play objection SFX

  3. REALIZATION phase (if realization == 1):
     - White rectangle fill over viewport
     - Fade out over 300ms

  4. TALKING phase:
     - Load characters/<name>/emotions/<emote>(b).png
     - Start Chatbox::start(message, text_color, additive)
     - Play sfx via AudioManager::play_sfx()
     - Wait for Chatbox::is_done() or A pressed

  5. IDLE phase:
     - Switch to characters/<name>/emotions/<emote>(a).png
     - Wait for next pending_ic
```

---

## Adding a New Screen

1. Create `src/ui/screens/my_screen.hpp` and `my_screen.cpp`
2. Inherit from `ao::Screen`
3. Implement all five virtual methods
4. Add `src/ui/screens` to `SOURCES` in Makefile (already present)
5. In the parent screen's `handle_event`, call `app_.push_screen(new MyScreen(app_))`
6. No registration needed — screen stack is dynamic

---

## Adding a New AO2 Packet Handler

1. In `src/protocol/ao_client.cpp`, add a `void AOClient::on_<header>(const Packet& p, GameState& gs)` method
2. In `AOClient::process()`, add a `else if (strcmp(p.header, "<HEADER>") == 0)` branch (when `state_ == HandshakeState::Connected`)
3. Declare the method in `ao_client.hpp`
4. If the handler needs to send a response, call `outgoing_.push({buf, len})` with a packet built via `ao::cmd::`

---

## Memory Budget (approximate)

| Resource | Count | Size each | Total |
|----------|-------|-----------|-------|
| Texture cache slots | 64 | ~128 KB avg | ~8 MB |
| SFX chunks | 16 | ~64 KB avg | ~1 MB |
| APNG frames | 128 | ~32 KB avg | ~4 MB |
| GameState (stack) | 1 | ~1.5 MB | ~1.5 MB |
| SPSCQueues | 2 | ~2 MB each | ~4 MB |

Switch has 4 GB RAM; homebrew can typically use ~2 GB. Budget is not a concern for current scope, but avoid adding unbounded allocations.

---

## Dependencies

| Library | Source | Notes |
|---------|--------|-------|
| SDL2 | `switch-sdl2` portlib | Window, renderer, events, threads |
| SDL2_image | `switch-sdl2_image` | PNG, APNG, GIF, WebP, animated WebP; needs ≥ 2.6 for `IMG_LoadAnimation_RW` |
| SDL2_ttf | `switch-sdl2_ttf` | Font rendering |
| SDL2_mixer | `switch-sdl2_mixer` | BGM (Mix_Music) + SFX (Mix_Chunk) |
| SDL2_net | `switch-sdl2_net` | TCP sockets (blocking, SDLNet_CheckSockets for non-blocking poll) |
| libnx | built into devkitPro | `romfsInit`, `swkbdCreate/Show`, Switch system APIs |
| libopusfile, libvorbisidec, libogg | bundled with SDL2_mixer portlib | Audio codec support |
| libfreetype, libpng, libz | bundled with SDL2_ttf/SDL2_image | Font + image decode |
| libwebp, libwebpdemux | `switch-libwebp` portlib | Static WebP decode (`libwebp`) + animated WebP frame extraction (`libwebpdemux`); both required for animated WebP |

No external networking libraries. WebSocket is implemented inline (~300 lines total across ws_handshake + ws_frame).
