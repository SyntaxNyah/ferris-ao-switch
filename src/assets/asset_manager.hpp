#pragma once
#include <SDL2/SDL.h>
#include <cstdint>

namespace ao {

// Asset resolution with three-tier priority:
//
//   1. HTTP streaming from server's asset base URL (set via set_asset_url from ASS packet)
//   2. sdmc:/switch/ferris-ao/base/<relative>   (user-installed local AO base pack)
//   3. romfs:/<relative>                         (bundled fallback assets in NRO)
//
// Tier 1 is only active when the server advertises an asset URL (ASS packet during handshake).
// If HTTP fetch fails or no URL is set, the search falls through to tiers 2 and 3.
// This means the local base folder is fully optional — servers without a web server simply
// rely on romfs fallbacks, while servers with a CDN stream everything on demand.
//
// Thread safety: set_asset_url / clear_asset_url must only be called from the main thread.
//                fetch_bytes / open_rwops are safe to call from any thread (e.g. AssetStream).

class AssetManager {
public:
    // ── URL management ─────────────────────────────────────────────────────────
    // Called by AOClient when the server sends an ASS packet.
    // url should be the bare base, e.g. "http://cdn.example.com/ao-base"
    // A trailing slash, if present, is stripped.
    static void set_asset_url(const char* url);
    static void clear_asset_url();
    static bool        has_asset_url();
    static const char* asset_url();          // "" if none set

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

    // Open an asset as an SDL_RWops from the best available source.
    // The returned RWops owns its underlying buffer: SDL_RWclose() frees it.
    // Returns nullptr if the asset cannot be found anywhere.
    // Pass the returned RWops to IMG_Load_RW, IMG_LoadAnimation_RW,
    // Mix_LoadMUS_RW, Mix_LoadWAV_RW etc. with freesrc=1.
    static SDL_RWops* open_rwops(const char* relative);

    // ── Prefetch cache (used by AssetStream) ───────────────────────────────────
    // Store pre-fetched data so the next fetch_bytes call for this relative path
    // returns immediately without a network round-trip.
    // data must be SDL_malloc'd — AssetManager takes ownership.
    static void store_prefetch(const char* relative, uint8_t* data, int size);

    // Non-consuming peek: returns true if the prefetch cache contains this path.
    // Does NOT consume the entry — subsequent fetch_bytes will still find it.
    static bool has_prefetch(const char* relative);

    // Convenience helpers
    static const char* user_base();   // "sdmc:/switch/ferris-ao/base" (or "base" on desktop)
    static const char* romfs_base();  // "romfs:" (or "romfs" on desktop)
};

} // namespace ao
