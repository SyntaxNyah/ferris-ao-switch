#pragma once
#include <cstdint>
#include <cstring>

namespace ao {

// ── Emotion entry ──────────────────────────────────────────────────────────────
struct EmotionEntry {
    char name[32]      = {};   // display name
    char pre_anim[64]  = {};   // pre-animation filename (empty = none)
    char idle_anim[64] = {};   // (a) sprite base name
    char talk_anim[64] = {};   // (b) sprite base name (usually same as idle)
    int  desk_mod      = 1;
    bool has_pre       = false;
};

// ── Character definition ───────────────────────────────────────────────────────
static constexpr int MAX_EMOTIONS = 64;

struct CharDef {
    char         name[64]     = {};   // [Options] name (folder / internal name)
    char         showname[64] = {};   // [Options] showname (display name)
    char         blips[32]    = {};   // [Options] blips (talk-sound, e.g. "male")
    char         side[16]     = {};   // [Options] side (default position)
    int          emotion_count = 0;
    EmotionEntry emotions[MAX_EMOTIONS];
};

// Parse a char.ini file from a local filesystem path. Returns true on success.
bool parse_char_ini(const char* path, CharDef& out);

// Parse char.ini directly from an in-memory byte buffer (not necessarily
// null-terminated). Used by the asset-streaming path.
bool parse_char_ini_bytes(const uint8_t* data, int size, CharDef& out);

// Fetch and parse characters/<folder>/char.ini through AssetManager, so it
// resolves over the HTTP CDN, the local sdmc: base, or romfs: transparently.
// `folder` is the character's internal name (case-insensitive on the wire).
// Returns true on success.
bool load_char_ini(const char* folder, CharDef& out);

} // namespace ao
