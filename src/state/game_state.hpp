#pragma once
#include <cstdint>
#include <cstring>

namespace ao {

// ── Character info ─────────────────────────────────────────────────────────────
struct CharacterInfo {
    char name[64];      // internal folder name
    char showname[64];  // display name (from char.ini, may be empty)
};

// ── Evidence ───────────────────────────────────────────────────────────────────
struct EvidenceEntry {
    char name[128];
    char description[512];
    char image[128];     // filename inside backgrounds/ or misc/
};

// ── Area info (populated from SM + ARUP packets) ───────────────────────────────
struct AreaInfo {
    char   name[128];
    int    players       = 0;
    char   status[32]    = "IDLE";  // IDLE, CASING, RECESS, RP, GAMING, LFP
    char   cm_label[64]  = "";
    char   lock_state[16]= "FREE";  // FREE, SPECTATABLE, LOCKED
};

// ── Chat entries ───────────────────────────────────────────────────────────────
struct ChatEntry {
    char    name[64];
    char    message[512];
    uint8_t color   = 0;   // AO2 text color index 0-11
    bool    server  = false;
};

static constexpr int CHAT_LOG_CAP = 128;

struct ChatLog {
    ChatEntry entries[CHAT_LOG_CAP];
    int head  = 0;
    int count = 0;

    void push(const ChatEntry& e) {
        entries[head % CHAT_LOG_CAP] = e;
        head = (head + 1) % CHAT_LOG_CAP;
        if (count < CHAT_LOG_CAP) ++count;
    }

    // Index 0 = oldest visible entry
    const ChatEntry& at(int i) const {
        int oldest = (head - count + CHAT_LOG_CAP * 2) % CHAT_LOG_CAP;
        return entries[(oldest + i) % CHAT_LOG_CAP];
    }
};

// ── IC animation state ─────────────────────────────────────────────────────────
struct ICAnimState {
    char    char_name[64]   = {};
    char    pre_anim[64]    = {};
    char    emote[64]       = {};
    char    message[512]    = {};
    char    showname[64]    = {};
    char    pos[16]         = {};       // def, pro, wit, jud, hlp, hld
    char    sfx[64]         = {};
    char    bg[128]         = {};
    int     char_id         = -1;
    int     emote_mod       = 0;        // 0-6
    int     text_color      = 0;        // 0-11
    int     objection_mod   = 0;        // 0=none,1=objection,2=hold_it,3=take_that
    bool    flip            = false;
    bool    realization     = false;
    bool    additive        = false;
    bool    immediate       = false;
    int     self_offset     = 0;        // -100..100 (% of viewport width)
    // pairing
    int     other_charid    = -1;
    char    other_emote[64] = {};
    int     other_offset    = 0;
    bool    other_flip      = false;
    // evidence shown with message
    int     evidence_id     = 0;
    // sfx options
    bool    looping_sfx     = false;
    bool    screenshake     = false;
    // set when a new message arrives and is ready to play
    bool    pending         = false;
};

// ── Master game state ──────────────────────────────────────────────────────────
// Lives on the main thread only. Network thread writes to the incoming
// SPSCQueue; the main thread processes packets and mutates this struct.
struct GameState {
    // ── Connection ──────────────────────────────────────────────────────────────
    char server_name[128]   = {};
    char server_version[32] = {};
    int  my_uid             = -1;
    bool connected          = false;
    bool in_lobby           = false;    // DONE packet received

    // ── Characters ──────────────────────────────────────────────────────────────
    // Large rosters: Akashi-based servers (e.g. ao.umineko.online) routinely
    // ship 600+ characters in a single SC packet. Sized at 1024 to fit the
    // biggest public servers with room to spare. At 128 B per CharacterInfo
    // this is 128 KB — trivial on the 4 GB Switch.
    static constexpr int MAX_CHARS = 1024;
    CharacterInfo characters[MAX_CHARS];
    bool          char_taken[MAX_CHARS] = {};
    int           char_count            = 0;
    int           my_char_id            = -1;

    // ── Areas ────────────────────────────────────────────────────────────────────
    static constexpr int MAX_AREAS = 64;
    AreaInfo areas[MAX_AREAS];
    int      area_count  = 0;
    int      my_area_idx = -1;

    // ── Music ────────────────────────────────────────────────────────────────────
    // Big rosters again: music-heavy servers send several hundred tracks in
    // the single SM packet that also carries the area list. 2048 × 128 B
    // = 256 KB.
    static constexpr int MAX_MUSIC = 2048;
    char music_list[MAX_MUSIC][128];
    int  music_count = 0;
    char current_music[128] = {};

    // ── Area runtime state ────────────────────────────────────────────────────────
    char          background[128]  = {};
    int           hp_defense       = 5;   // 0-10
    int           hp_prosecution   = 5;

    static constexpr int MAX_EVIDENCE = 48;
    EvidenceEntry evidence[MAX_EVIDENCE];
    int           evidence_count = 0;

    // ── Chat ─────────────────────────────────────────────────────────────────────
    ChatLog ic_log;
    ChatLog ooc_log;

    // ── IC animation ─────────────────────────────────────────────────────────────
    ICAnimState ic_anim;

    // ── Auth ─────────────────────────────────────────────────────────────────────
    bool authenticated = false;
    char mod_name[64]  = {};

    GameState() {
        std::memset(characters, 0, sizeof(characters));
        std::memset(music_list, 0, sizeof(music_list));
        std::memset(evidence,   0, sizeof(evidence));
    }
};

} // namespace ao
