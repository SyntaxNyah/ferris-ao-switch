#pragma once
#include <SDL2/SDL.h>
#include <cstdint>

namespace ao {

class HttpClient;

// Asset resolution with four-tier priority (mirrors AO-SDL's MountManager):
//
//   1. Primary   HTTP from <primary_url>   (set via set_asset_url from ASS packet)
//   2. Secondary HTTP from <secondary_url> (set via set_secondary_url, e.g.
//                                           https://attorneyoffline.de/base/)
//   3. sdmc:/switch/ferris-ao/base/<relative>   (user-installed local AO base pack)
//   4. romfs:/<relative>                         (bundled fallback assets in NRO)
//
// Tier 1 is only active when the server advertises an asset URL (ASS packet during handshake).
// Tier 2 is a community fallback CDN that hosts the classic AO2 base pack — it lets servers
// that only host their custom chars on their own CDN still resolve everything else.
// If a tier returns 404 the search falls through to the next tier. Network failures are
// cached per-URL so we never hammer a dead server.
//
// URL composition (AO-SDL MountHttp semantics):
//   - Relative paths are lowercased before being sent to either HTTP tier
//     (AO2 CDNs host lowercase-only trees by convention).
//   - Each path segment is percent-encoded per RFC 3986, preserving `/`, `.`,
//     `-`, `_`, `~`, and crucially `()` so emote filenames like `normal(a).png`
//     round-trip intact.
//   - Local tiers (sdmc:/romfs:) preserve case — the user controls that tree.
//
// Thread safety: set_asset_url / set_secondary_url / clear_* must only be called from
//                the main thread. fetch_bytes / open_rwops are safe to call from any
//                thread (e.g. AssetStream workers).

class AssetManager {
public:
    // ── URL management ─────────────────────────────────────────────────────────
    // Primary CDN — set from the server's ASS packet. Tried first.
    // url should be the bare base, e.g. "http://cdn.example.com/ao-base"
    // Trailing slashes are normalised — exactly one is always appended so that
    // paths can be joined with "%s%s" safely.
    static void set_asset_url(const char* url);
    static void clear_asset_url();
    static bool        has_asset_url();
    static const char* asset_url();          // "" if none set

    // Secondary CDN — community fallback base pack (AO-SDL ships with this set
    // to https://attorneyoffline.de/base/). Tried after the primary 404s and
    // before the local sdmc:/romfs: tiers. Lets servers that only host their
    // custom assets on their own CDN still resolve the classic base pack.
    static void set_secondary_url(const char* url);
    static void clear_secondary_url();
    static bool        has_secondary_url();
    static const char* secondary_url();      // "" if none set

    // ── Path resolution (local only) ───────────────────────────────────────────
    // Checks sdmc: then romfs:. Returns true and fills out_path if found locally.
    // Does NOT check HTTP — use fetch_bytes / open_rwops for the full search.
    static bool resolve(const char* relative, char* out_path, int out_cap);

    // ── Data access ────────────────────────────────────────────────────────────
    // Fetch asset bytes from the best available source (HTTP → sdmc: → romfs:).
    // On success: returns SDL_malloc'd buffer; caller must SDL_free() it.
    // On failure: returns nullptr.
    // This call may block (network I/O or disk read); call from a worker thread
    // when latency matters.
    static uint8_t* fetch_bytes(const char* relative, int* out_size);

    // Same as fetch_bytes, but reuses a caller-owned HttpClient for the HTTP
    // tier. Lets AssetStream worker threads keep their TCP/TLS connection alive
    // across prefetches, eliminating the per-request TLS handshake.
    // `client` must not be shared between threads.
    static uint8_t* fetch_bytes_with_client(const char* relative, int* out_size,
                                            HttpClient& client);

    // Two-client overload: the primary tier uses `primary_client` and the
    // secondary tier uses `secondary_client`. Required when both mounts live
    // on different hosts — a single client would otherwise thrash, closing
    // and reopening its connection on every alternation, exhausting libnx's
    // thread table on Switch.
    // Neither client may be shared between threads.
    static uint8_t* fetch_bytes_with_clients(const char* relative, int* out_size,
                                             HttpClient& primary_client,
                                             HttpClient& secondary_client);

    // Open an asset as an SDL_RWops from the best available source.
    // The returned RWops owns its underlying buffer: SDL_RWclose() frees it.
    // Returns nullptr if the asset cannot be found anywhere.
    // Pass the returned RWops to IMG_Load_RW, IMG_LoadAnimation_RW,
    // Mix_LoadMUS_RW, Mix_LoadWAV_RW etc. with freesrc=1.
    static SDL_RWops* open_rwops(const char* relative);

    // Non-blocking open: prefetch cache → sdmc: → romfs: ONLY (never HTTP).
    // Returns nullptr if the asset isn't already local/cached. For the audio hot
    // path and anything on the render thread that must not stall on the network.
    static SDL_RWops* open_rwops_cached(const char* relative);

    // ── Prefetch cache (used by AssetStream) ───────────────────────────────────
    // Store pre-fetched data so the next fetch_bytes call for this relative path
    // returns immediately without a network round-trip.
    // data must be SDL_malloc'd — AssetManager takes ownership.
    static void store_prefetch(const char* relative, uint8_t* data, int size);

    // Non-consuming peek: returns true if the prefetch cache contains this path
    // EITHER as raw bytes OR as off-thread-decoded frames (see below).
    // Does NOT consume the entry — subsequent fetch_bytes will still find it.
    static bool has_prefetch(const char* relative);

    // ── Decoded-frame cache (off-thread image decode) ──────────────────────────
    // AssetStream workers can decode image bytes into SDL_Surface frames so the
    // main thread only does the GPU upload (SDL_CreateTextureFromSurface, which
    // MUST run on the render thread). This moves the heavy PNG/WebP/APNG decode
    // off the render loop — the cost the courtroom load-gate used to wait on.
    // Surfaces are owned by exactly one place at a time: decoding worker → this
    // staging cache → the consuming player (which uploads then frees them).
    static constexpr int FRAMES_MAX = 128;   // matches APNG_MAX_FRAMES
    struct DecodedFrames {
        SDL_Surface* frames[FRAMES_MAX];
        int          delays[FRAMES_MAX];      // ms per frame (1 entry for a still)
        int          count;
        int          w, h;
    };
    // Worker side: take ownership of decoded frames. Best-effort — if the staging
    // cache can't fit them the surfaces are freed and the entry is dropped, so the
    // consumer simply falls back to the main-thread decode path (always safe).
    // `frames[i]` must be surfaces the caller no longer owns.
    static void store_frames(const char* relative, SDL_Surface** frames,
                             const int* delays, int count, int w, int h);
    // Main thread: hand staged frames to the caller (ownership transferred). The
    // caller uploads each frame to a texture, then SDL_FreeSurface()s it. Returns
    // false if no frames are staged for this path.
    static bool take_frames(const char* relative, DecodedFrames& out);
    // Non-consuming: are decoded frames staged for this path?
    static bool has_frames(const char* relative);
    // Free all staged frames (call on disconnect — next server differs).
    static void clear_frames();

    // Convenience helpers
    static const char* user_base();   // "sdmc:/switch/ferris-ao/base" (or "base" on desktop)
    static const char* romfs_base();  // "romfs:" (or "romfs" on desktop)

    // True if a local AO2 base pack is present (user_base has a characters/ dir).
    // When present, assets load locally with zero network — the AO-SDL-style
    // "instant" path. Surfaced in the connect-screen Settings tab.
    static bool has_local_base();
};

} // namespace ao
