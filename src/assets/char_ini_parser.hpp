#pragma once
#include <cstring>

namespace ao {

// ── Emotion entry ──────────────────────────────────────────────────────────────
struct EmotionEntry {
    char name[32]      = {};   // display name
    char pre_anim[64]  = {};   // pre-animation filename (empty = none)
    char idle_anim[64] = {};   // (a).png base name
    char talk_anim[64] = {};   // (b).png base name (usually same as idle)
    int  desk_mod      = 0;
    bool has_pre       = false;
};

// ── Character definition ───────────────────────────────────────────────────────
static constexpr int MAX_EMOTIONS = 64;

struct CharDef {
    char         name[64]     = {};
    char         showname[64] = {};
    int          emotion_count = 0;
    EmotionEntry emotions[MAX_EMOTIONS];
};

// Parse a char.ini file into a CharDef.
// Returns true on success.
bool parse_char_ini(const char* path, CharDef& out);

} // namespace ao
