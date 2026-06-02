#pragma once
#include <cstring>

namespace ao {

// Parsed from <asset_url>/extensions.json (webAO format).
// Provides ordered extension lists for each asset category.
// Falls back to AO2 classic defaults when the file is absent.
//
// Usage:
//   const ExtensionsConfig& ec = ExtensionsConfig::get();
//   for (int i = 0; i < ec.charicon_count; ++i)
//       try path "characters/<name>/char_icon" + ec.charicon[i]
//
// Call fetch_and_apply() once after the asset URL is known (main thread, one-shot).
// Call reset() on disconnect.

struct ExtensionsConfig {
    static constexpr int MAX_EXTS = 6;
    static constexpr int EXT_LEN  = 20; // ".webp.static" = 13 chars

    char charicon  [MAX_EXTS][EXT_LEN];
    char emote     [MAX_EXTS][EXT_LEN]; // pre-anims and talking sprites
    char emotions  [MAX_EXTS][EXT_LEN]; // idle/talk still frames
    char background[MAX_EXTS][EXT_LEN];

    int charicon_count;
    int emote_count;
    int emotions_count;
    int background_count;

    // Fetch extensions.json from the current asset URL and apply.
    // Silently keeps defaults if the file is absent or unparseable.
    // Blocking — call from main thread at lobby-enter time (one-shot).
    static void fetch_and_apply();

    // Reset to built-in defaults AND forget learned formats (call on disconnect).
    static void reset();

    // Access the current config.
    static const ExtensionsConfig& get();

    // ── Learned winning format (per category, per server) ──────────────────────
    // AO2 servers are almost always format-uniform: every character/background
    // ships the same image type (these days usually WebP). So once a candidate
    // extension actually decodes for a category, we remember its INDEX and probe
    // it FIRST next time — the courtroom probes ONLY it, and the worker-side icon
    // probe stops at it — falling back to the full candidate list if it turns out
    // missing. That is what collapses the cold-load 404 storm (firing all ~5
    // candidates when 4 always 404). Thread-safe: both the AssetStream workers
    // (icon/background probe) and the main-thread decode path record winners, and
    // workers read the hint to order their probe. Backed by an SDL_atomic_t — it
    // only biases probe ORDER, never correctness, so relaxed atomicity is enough.
    enum Category { CAT_CHARICON, CAT_EMOTE, CAT_EMOTIONS, CAT_BACKGROUND, CAT_COUNT };

    static int  learned(Category c);            // ext index into this category, or -1
    static void note(Category c, int ext_index); // record a decoded format's index

    // Generic probe support (for the worker-side icon/background probe). Fills
    // `out_idx` with this category's extension indices in probe order — the
    // learned format first, then the rest — and returns the count. `ext_at`
    // resolves an index back to its extension string (".webp", …).
    static int         probe_order(Category c, int out_idx[], int max_out);
    static const char* ext_at(Category c, int idx);

    // Persist / restore the learned formats for the CURRENT server (keyed by its
    // asset URL) so a revisit probes the right format from the very first asset —
    // the disk cache then serves it with no network at all. persist() is called
    // by App on disconnect (asset URL still set); restore() by fetch_and_apply()
    // once the asset URL + extension lists are known. Switch-only (no-op on
    // desktop). A stale hint only mis-orders one probe, so this is best-effort.
    static void persist();
    static void restore();
};

} // namespace ao
