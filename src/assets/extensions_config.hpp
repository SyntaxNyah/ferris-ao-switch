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
    // extension actually decodes for a category, we remember it and probe it
    // FIRST next time — and the courtroom probes ONLY it, falling back to the
    // full candidate list if it turns out missing. That is what collapses the
    // cold-load 404 storm (firing all ~5 candidates when 4 always 404). All
    // access is on the main thread: the decode path notes the winner, the
    // prefetch path reads it. No locking needed.
    enum Category { CAT_CHARICON, CAT_EMOTE, CAT_EMOTIONS, CAT_BACKGROUND, CAT_COUNT };

    static const char* learned(Category c);                // "" until one decodes
    static void        note(Category c, const char* ext);  // record a decoded format
};

} // namespace ao
