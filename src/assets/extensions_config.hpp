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

    // Reset to built-in defaults (call on disconnect).
    static void reset();

    // Access the current config.
    static const ExtensionsConfig& get();
};

} // namespace ao
