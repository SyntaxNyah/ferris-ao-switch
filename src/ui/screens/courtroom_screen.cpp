#include "courtroom_screen.hpp"
#include "../touch.hpp"
#include "../../app.hpp"
#include "../../render/renderer.hpp"
#include "../../state/game_state.hpp"
#include "../../assets/theme_manager.hpp"
#include "../../assets/asset_manager.hpp"
#include "../../assets/asset_stream.hpp"
#include "../../assets/extensions_config.hpp"
#include "../../render/text_renderer.hpp"
#include "../../protocol/commands.hpp"
#include "../../input/virtual_keyboard.hpp"
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cctype>

namespace ao {

// AO2 canonical text colours (matches AO-SDL / webAO chat_config defaults).
static const SDL_Color TEXT_COLORS[10] = {
    {247, 247, 247, 255}, // 0 white
    {0,   247, 0,   255}, // 1 green
    {247, 0,   57,  255}, // 2 red
    {247, 115, 57,  255}, // 3 orange
    {107, 198, 247, 255}, // 4 blue
    {247, 247, 0,   255}, // 5 yellow
    {247, 115, 247, 255}, // 6 pink
    {128, 247, 247, 255}, // 7 cyan
    {160, 181, 205, 255}, // 8 grey
    {247, 247, 247, 255}, // 9 rainbow (rendered as white)
};

// ── Static asset helpers ────────────────────────────────────────────────────────

static void lc_copy(char* dst, int cap, const char* src) {
    int i = 0;
    for (; src[i] && i < cap - 1; ++i)
        dst[i] = (char)std::tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

// AO2 position → background image name (pre-2.8 "empty" naming).
static const char* bg_filename(const char* pos) {
    if (!std::strcmp(pos, "def")) return "defenseempty";
    if (!std::strcmp(pos, "pro")) return "prosecutorempty";
    if (!std::strcmp(pos, "wit")) return "witnessempty";
    if (!std::strcmp(pos, "jud")) return "judgestand";
    if (!std::strcmp(pos, "hld")) return "helperstand";
    if (!std::strcmp(pos, "hlp")) return "prohelperstand";
    if (!std::strcmp(pos, "jur")) return "jurystand";
    if (!std::strcmp(pos, "sea")) return "seancestand";
    return pos;
}

// AO2 position → desk/bench overlay image name.
static const char* desk_filename(const char* pos) {
    if (!std::strcmp(pos, "def")) return "defensedesk";
    if (!std::strcmp(pos, "pro")) return "prosecutiondesk";
    if (!std::strcmp(pos, "wit")) return "stand";
    if (!std::strcmp(pos, "jud")) return "judgedesk";
    if (!std::strcmp(pos, "hld")) return "helperdesk";
    if (!std::strcmp(pos, "hlp")) return "prohelperdesk";
    if (!std::strcmp(pos, "jur")) return "jurydesk";
    if (!std::strcmp(pos, "sea")) return "seancedesk";
    return pos;
}

// Build one character-sprite candidate path, replicating webAO/LemmyAO
// buildEmoteUrls(): .png and .webp.static use the BARE emote name (no
// (a)/(b) prefix), every other format uses the prefix.
static void emote_path(char* out, int cap, const char* char_lc,
                       const char* emote_lc, const char* prefix, const char* ext) {
    if (!std::strcmp(ext, ".png"))
        std::snprintf(out, cap, "characters/%s/%s.png", char_lc, emote_lc);
    else if (!std::strcmp(ext, ".webp.static"))
        std::snprintf(out, cap, "characters/%s/%s.webp", char_lc, emote_lc);
    else
        std::snprintf(out, cap, "characters/%s/%s%s%s", char_lc, prefix, emote_lc, ext);
}

// Queue every emote candidate for background prefetch (non-blocking).
static void prefetch_emote(AssetStream& s, const char* char_lc,
                           const char* emote_lc, const char* prefix) {
    if (!char_lc[0] || !emote_lc[0]) return;
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    char p[256];
    for (int i = 0; i < ec.emote_count; ++i) {
        emote_path(p, sizeof(p), char_lc, emote_lc, prefix, ec.emote[i]);
        if (!AssetManager::has_prefetch(p)) s.prefetch(p);
    }
}

// Decode the first emote candidate that is already in the prefetch cache.
// Never touches the network. Returns true once a frame is loaded.
static bool resolve_emote(APNGPlayer& pl, SDL_Renderer* r, const char* char_lc,
                          const char* emote_lc, const char* prefix) {
    if (!char_lc[0] || !emote_lc[0]) return false;
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    char p[256];
    for (int i = 0; i < ec.emote_count; ++i) {
        emote_path(p, sizeof(p), char_lc, emote_lc, prefix, ec.emote[i]);
        if (AssetManager::has_prefetch(p)) { pl.set_loop(true); return pl.load(r, p); }
    }
    return false;
}

static void prefetch_bgimg(AssetStream& s, const char* bg_lc, const char* file) {
    if (!bg_lc[0]) return;
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    char p[256];
    for (int i = 0; i < ec.background_count; ++i) {
        std::snprintf(p, sizeof(p), "background/%s/%s%s", bg_lc, file, ec.background[i]);
        if (!AssetManager::has_prefetch(p)) s.prefetch(p);
    }
}

static bool resolve_bgimg(APNGPlayer& pl, SDL_Renderer* r, const char* bg_lc, const char* file) {
    if (!bg_lc[0]) return false;
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    char p[256];
    for (int i = 0; i < ec.background_count; ++i) {
        std::snprintf(p, sizeof(p), "background/%s/%s%s", bg_lc, file, ec.background[i]);
        if (AssetManager::has_prefetch(p)) { pl.set_loop(true); return pl.load(r, p); }
    }
    return false;
}

static const char* SHOUT_NAMES[] = {nullptr, "holdit", "objection", "takethat", "custom"};
static const char* SHOUT_EXTS[]  = {".webp", ".gif", ".apng", ".png"};

static void prefetch_shout(AssetStream& s, const char* char_lc, int shout) {
    if (shout < 1 || shout > 4) return;
    bool custom = (shout == 4);
    char p[256];
    for (int i = 0; i < 4; ++i) {
        if (custom) std::snprintf(p, sizeof(p), "characters/%s/custom%s", char_lc, SHOUT_EXTS[i]);
        else        std::snprintf(p, sizeof(p), "characters/%s/%s_bubble%s", char_lc, SHOUT_NAMES[shout], SHOUT_EXTS[i]);
        if (!AssetManager::has_prefetch(p)) s.prefetch(p);
    }
    if (!custom) {
        std::snprintf(p, sizeof(p), "misc/default/%s_bubble.png", SHOUT_NAMES[shout]);
        if (!AssetManager::has_prefetch(p)) s.prefetch(p);
    }
}

static bool resolve_shout(APNGPlayer& pl, SDL_Renderer* r, const char* char_lc, int shout) {
    if (shout < 1 || shout > 4) return false;
    bool custom = (shout == 4);
    char p[256];
    for (int i = 0; i < 4; ++i) {
        if (custom) std::snprintf(p, sizeof(p), "characters/%s/custom%s", char_lc, SHOUT_EXTS[i]);
        else        std::snprintf(p, sizeof(p), "characters/%s/%s_bubble%s", char_lc, SHOUT_NAMES[shout], SHOUT_EXTS[i]);
        if (AssetManager::has_prefetch(p)) { pl.set_loop(false); return pl.load(r, p); }
    }
    if (!custom) {
        std::snprintf(p, sizeof(p), "misc/default/%s_bubble.png", SHOUT_NAMES[shout]);
        if (AssetManager::has_prefetch(p)) { pl.set_loop(false); return pl.load(r, p); }
    }
    return false;
}

// Audio (small files): kept synchronous, but warmed via prefetch so the load
// is normally a cache hit. "" first so names with an embedded extension work.
static bool try_play_audio(App& app, const char* base) {
    static const char* exts[] = {"", ".opus", ".ogg", ".wav", ".mp3"};
    char path[256];
    for (int i = 0; i < 5; ++i) {
        std::snprintf(path, sizeof(path), "%s%s", base, exts[i]);
        if (app.audio().play_sfx(path)) return true;
    }
    return false;
}

static void play_sfx_named(App& app, const char* name) {
    if (!name || !name[0] || !std::strcmp(name, "0") || !std::strcmp(name, "1")) return;
    char b[200];
    std::snprintf(b, sizeof(b), "sounds/general/%s", name); if (try_play_audio(app, b)) return;
    std::snprintf(b, sizeof(b), "sounds/%s", name);         try_play_audio(app, b);
}

static void play_blip(App& app, const char* blip) {
    const char* b = (blip && blip[0]) ? blip : "male";
    char p[160];
    std::snprintf(p, sizeof(p), "sounds/blips/%s", b); if (try_play_audio(app, p)) return;
    std::snprintf(p, sizeof(p), "blips/%s", b);        try_play_audio(app, p);
}

// ── CourtroomScreen ─────────────────────────────────────────────────────────────

CourtroomScreen::CourtroomScreen(App& app) : Screen(app) {}

void CourtroomScreen::on_enter() {
    active_panel_ = CourtroomPanel::None;
    phase_        = Phase::Idle;
    tw_pos_ = tw_max_ = 0;
    // Drop any character-select icon prefetches still queued so our background,
    // desk and sprite prefetches go to the workers first — otherwise the first
    // few IC lines on a 600-character server wait behind a wall of icon fetches.
    app_.asset_stream().clear_pending();
    load_own_character();
    GameState& gs = app_.state();
    std::strncpy(ic_pos_, (own_loaded_ && own_char_.side[0]) ? own_char_.side : "wit",
                 sizeof(ic_pos_) - 1);
    ic_pos_[sizeof(ic_pos_) - 1] = '\0';
    // Queue the opening background asynchronously — never block on entry.
    if (gs.background[0]) {
        lc_copy(cur_bg_, sizeof(cur_bg_), gs.background);
        std::strncpy(cur_pos_, ic_pos_, sizeof(cur_pos_) - 1);
        cur_pos_[sizeof(cur_pos_) - 1] = '\0';
        queue_scene();
    }
    // Warm our own emote thumbnails now (queued behind the scene), so the
    // composer's grid/preview is already cached by the time it's opened.
    prefetch_emote_buttons();
}

void CourtroomScreen::load_own_character() {
    GameState& gs = app_.state();
    int cid = gs.my_char_id;
    if (cid < 0 || cid >= gs.char_count || !gs.characters[cid].name[0]) return;
    own_loaded_ = load_char_ini(gs.characters[cid].name, own_char_);
}

// ── IC composer emote thumbnails (AO2 emotions/button<N>_{on,off}.png) ──────────
// Queue the player's emote-button thumbnails for background prefetch so the
// composer grid/preview can show real sprite art without the render loop ever
// blocking on a network fetch. Off-state for every emote, plus the selected
// emote's on-state. has_prefetch()/peek() guards keep this from re-queueing
// already-resolved buttons.
void CourtroomScreen::prefetch_emote_buttons() {
    if (!own_loaded_ || own_char_.emotion_count <= 0) return;
    GameState& gs = app_.state();
    int cid = gs.my_char_id;
    if (cid < 0 || cid >= gs.char_count || !gs.characters[cid].name[0]) return;
    char lc[64]; lc_copy(lc, sizeof(lc), gs.characters[cid].name);
    AssetStream& s = app_.asset_stream();
    char p[256];
    for (int i = 0; i < own_char_.emotion_count; ++i) {
        std::snprintf(p, sizeof(p), "characters/%s/emotions/button%d_off.png", lc, i + 1);
        if (!AssetManager::has_prefetch(p) && !app_.tex_cache().peek(p)) s.prefetch(p);
    }
    std::snprintf(p, sizeof(p), "characters/%s/emotions/button%d_on.png",
                  lc, ic_emote_sel_ + 1);
    if (!AssetManager::has_prefetch(p) && !app_.tex_cache().peek(p)) s.prefetch(p);
}

// Peek-only: returns the cached emote-button texture, or nullptr if it hasn't
// been decoded yet (never triggers a blocking load).
SDL_Texture* CourtroomScreen::emote_button_tex(int emote_idx, bool on) const {
    GameState& gs = app_.state();
    int cid = gs.my_char_id;
    if (cid < 0 || cid >= gs.char_count || !gs.characters[cid].name[0]) return nullptr;
    char lc[64]; lc_copy(lc, sizeof(lc), gs.characters[cid].name);
    char p[256];
    std::snprintf(p, sizeof(p), "characters/%s/emotions/button%d_%s.png",
                  lc, emote_idx + 1, on ? "on" : "off");
    return app_.tex_cache().peek(p);
}

// ── Async scene/sprite resolution ─────────────────────────────────────────────

void CourtroomScreen::queue_scene() {
    AssetStream& s = app_.asset_stream();
    scene_pending_ = true;
    bg_ready_ = desk_ready_ = false;
    msg_age_ms_ = 0;   // give this scene load its own give-up window (e.g. BN mid-idle)
    // Stream the SERVER's background only — never substitute the bundled
    // background/default courtroom (that's what made rooms look wrong while the
    // real background was still downloading). The viewport stays black until the
    // server's background streams in.
    prefetch_bgimg(s, cur_bg_, bg_filename(cur_pos_));
    prefetch_bgimg(s, cur_bg_, desk_filename(cur_pos_));
}

void CourtroomScreen::resolve_assets() {
    SDL_Renderer* r = app_.renderer().raw();

    // Fast path: once everything this message needs is decoded there is nothing
    // left to probe, so skip the per-frame has_prefetch() mutex scans entirely.
    bool pending = scene_pending_ || !idle_ready_ || !talk_ready_ ||
                   (m_use_pre_  && !preanim_ready_) ||
                   (m_has_pair_ && !pair_ready_) ||
                   (m_shout_ >= 1 && !shout_ready_);
    if (!pending) return;

    if (msg_age_ms_ > ASSET_GIVEUP_MS && phase_ != Phase::Loading) {
        // Past the give-up window — stop probing missing assets to save cycles.
        return;
    }

    if (scene_pending_) {
        // Server background only — keep trying until it streams in (no default).
        if (!bg_ready_)
            bg_ready_ = resolve_bgimg(bg_player_, r, cur_bg_, bg_filename(cur_pos_));
        if (!desk_ready_)
            desk_ready_ = resolve_bgimg(desk_player_, r, cur_bg_, desk_filename(cur_pos_));
        if (bg_ready_ && desk_ready_) scene_pending_ = false;
    }

    if (!idle_ready_) idle_ready_ = resolve_emote(idle_player_, r, m_char_, m_emote_, "(a)");
    if (!talk_ready_) talk_ready_ = resolve_emote(talk_player_, r, m_char_, m_emote_, "(b)");
    if (m_use_pre_ && !preanim_ready_)
        preanim_ready_ = resolve_emote(preanim_player_, r, m_char_, m_preanim_, "");
    if (m_has_pair_ && !pair_ready_)
        pair_ready_ = resolve_emote(pair_player_, r, m_pair_char_, m_pair_emote_, "(a)");
    if (m_shout_ >= 1 && !shout_ready_)
        shout_ready_ = resolve_shout(shout_player_, r, m_char_, m_shout_);
}

void CourtroomScreen::begin_message() {
    GameState& gs   = app_.state();
    ICAnimState& ic = gs.ic_anim;
    ic.pending      = false;
    AssetStream& s  = app_.asset_stream();

    ic_log_scroll_ = 0;   // snap the log back to the newest line

    // Remember the previous speaker sprite so we can avoid reloading it.
    char prev_char[64], prev_emote[64];
    std::strncpy(prev_char,  m_char_,  sizeof(prev_char));  prev_char[sizeof(prev_char) - 1]  = '\0';
    std::strncpy(prev_emote, m_emote_, sizeof(prev_emote)); prev_emote[sizeof(prev_emote) - 1] = '\0';

    lc_copy(m_char_,    sizeof(m_char_),    ic.char_name);
    lc_copy(m_emote_,   sizeof(m_emote_),   ic.emote);
    lc_copy(m_preanim_, sizeof(m_preanim_), ic.pre_anim);
    m_color_     = (ic.text_color >= 0 && ic.text_color < 10) ? ic.text_color : 0;
    m_emote_mod_ = ic.emote_mod;
    m_shout_     = ic.objection_mod;
    m_flip_      = ic.flip;
    m_self_off_  = ic.self_offset;
    m_realize_   = ic.realization;
    m_desk_visible_ = ic.desk_mod != 0;
    std::strncpy(m_blip_, "male", sizeof(m_blip_) - 1);
    m_use_pre_ = (m_emote_mod_ == 1 || m_emote_mod_ == 2 || m_emote_mod_ == 6) &&
                 m_preanim_[0] && std::strcmp(m_preanim_, "-") != 0;

    // Pairing — derive the partner folder from its char_id in the roster.
    m_has_pair_ = false;
    if (ic.other_charid >= 0 && ic.other_emote[0] &&
        ic.other_charid < gs.char_count && gs.characters[ic.other_charid].name[0]) {
        lc_copy(m_pair_char_,  sizeof(m_pair_char_),  gs.characters[ic.other_charid].name);
        lc_copy(m_pair_emote_, sizeof(m_pair_emote_), ic.other_emote);
        m_pair_off_  = ic.other_offset;
        m_pair_flip_ = ic.other_flip;
        m_has_pair_  = true;
    }

    // Background: only reload when position or room changed. An empty pos means
    // "stay where we are" — keep the current pos instead of snapping to the
    // witness stand (which made the background flip to a courtroom mid-scene).
    char pos[16];
    const char* want_pos = ic.pos[0] ? ic.pos : (cur_pos_[0] ? cur_pos_ : "wit");
    std::strncpy(pos, want_pos, sizeof(pos) - 1);
    pos[sizeof(pos) - 1] = '\0';
    char bg_lc[128];
    lc_copy(bg_lc, sizeof(bg_lc), gs.background);
    if (std::strcmp(pos, cur_pos_) != 0 || std::strcmp(bg_lc, cur_bg_) != 0) {
        std::strncpy(cur_pos_, pos,   sizeof(cur_pos_) - 1); cur_pos_[sizeof(cur_pos_) - 1] = '\0';
        std::strncpy(cur_bg_,  bg_lc, sizeof(cur_bg_)  - 1); cur_bg_[sizeof(cur_bg_)  - 1] = '\0';
        queue_scene();
    }

    // Reset per-message readiness and queue sprite prefetches. Skip the speaker
    // sprite entirely when it's the same char+emote as the last line — its
    // textures are still loaded, so there is nothing to re-fetch or re-decode.
    // This is the main fix for "characters reload every time someone talks", and
    // it also lets repeated lines skip the Loading gate (char_ready() stays true).
    bool same_sprite = (idle_ready_ || talk_ready_) &&
                       std::strcmp(prev_char,  m_char_)  == 0 &&
                       std::strcmp(prev_emote, m_emote_) == 0;
    preanim_ready_ = pair_ready_ = shout_ready_ = false;
    msg_age_ms_ = 0;
    if (!same_sprite) {
        idle_ready_ = talk_ready_ = false;
        prefetch_emote(s, m_char_, m_emote_, "(a)");
        prefetch_emote(s, m_char_, m_emote_, "(b)");
    }
    if (m_use_pre_)  prefetch_emote(s, m_char_, m_preanim_, "");
    if (m_has_pair_) prefetch_emote(s, m_pair_char_, m_pair_emote_, "(a)");
    if (m_shout_ >= 1) prefetch_shout(s, m_char_, m_shout_);

    // Decide which phase to enter once the sprite is ready.
    if (m_shout_ >= 1)      pending_phase_ = Phase::Shout;
    else if (m_use_pre_)    pending_phase_ = Phase::Preanim;
    else                    pending_phase_ = Phase::Talking;

    phase_ = Phase::Loading;
}

void CourtroomScreen::start_pending_phase() {
    if (pending_phase_ == Phase::Shout) {
        phase_     = Phase::Shout;
        shout_acc_ = 0;
        const ThemeLayout& tl = app_.theme().layout();
        if      (m_shout_ == 1) play_sfx_named(app_, tl.sfx_holdit);
        else if (m_shout_ == 2) play_sfx_named(app_, tl.sfx_objection);
        else if (m_shout_ == 3) play_sfx_named(app_, tl.sfx_takethat);
        else if (m_shout_ == 4) { char p[160]; std::snprintf(p, sizeof(p), "characters/%s/custom", m_char_); try_play_audio(app_, p); }
    } else if (pending_phase_ == Phase::Preanim) {
        phase_ = Phase::Preanim;
        preanim_player_.set_loop(false);
        preanim_player_.reset();
    } else {
        enter_talking();
    }
}

void CourtroomScreen::enter_talking() {
    GameState& gs = app_.state();
    phase_  = Phase::Talking;
    tw_pos_ = 0;
    tw_acc_ = 0;
    tw_since_blip_ = 0;
    tw_max_ = (int)std::strlen(gs.ic_anim.message);
    talk_player_.reset();
    idle_player_.reset();
    play_sfx_named(app_, gs.ic_anim.sfx);
    if (m_realize_) {
        realize_ms_ = REALIZE_MS;
        play_sfx_named(app_, app_.theme().layout().sfx_realization);
    }
    if (gs.ic_anim.screenshake) shake_frames_ = 14;
}

// ── Async music ────────────────────────────────────────────────────────────────

void CourtroomScreen::update_music() {
    GameState& gs = app_.state();
    if (std::strcmp(gs.current_music, cur_music_) != 0) {
        std::strncpy(cur_music_, gs.current_music, sizeof(cur_music_) - 1);
        cur_music_[sizeof(cur_music_) - 1] = '\0';
        if (!cur_music_[0] || std::strstr(cur_music_, "~stop") || !std::strcmp(cur_music_, "~~")) {
            app_.music().stop();
            music_pending_ = false;
        } else {
            // Queue the three AO2 music layouts; play whichever lands first.
            std::strncpy(want_music_, cur_music_, sizeof(want_music_) - 1);
            want_music_[sizeof(want_music_) - 1] = '\0';
            music_pending_ = true;
            music_age_ms_  = 0;
            AssetStream& s = app_.asset_stream();
            char p[200];
            std::snprintf(p, sizeof(p), "sounds/music/%s", want_music_); if (!AssetManager::has_prefetch(p)) s.prefetch(p);
            if (!AssetManager::has_prefetch(want_music_)) s.prefetch(want_music_);
            std::snprintf(p, sizeof(p), "music/%s", want_music_);        if (!AssetManager::has_prefetch(p)) s.prefetch(p);
        }
    }

    if (!music_pending_) return;
    char p[200];
    std::snprintf(p, sizeof(p), "sounds/music/%s", want_music_);
    if (AssetManager::has_prefetch(p))            { app_.music().play(p);          music_pending_ = false; return; }
    if (AssetManager::has_prefetch(want_music_))  { app_.music().play(want_music_); music_pending_ = false; return; }
    std::snprintf(p, sizeof(p), "music/%s", want_music_);
    if (AssetManager::has_prefetch(p))            { app_.music().play(p);          music_pending_ = false; return; }
    // Give up waiting after a few seconds: let MusicPlayer block-resolve once.
    if (music_age_ms_ > 5000) { app_.music().play(want_music_); music_pending_ = false; }
}

// ── Update ──────────────────────────────────────────────────────────────────────

void CourtroomScreen::update(uint32_t dt_ms) {
    GameState& gs = app_.state();

    update_music();

    // Background can change via a BN packet without a new IC line.
    if (gs.background[0]) {
        char bg_lc[128];
        lc_copy(bg_lc, sizeof(bg_lc), gs.background);
        if (std::strcmp(bg_lc, cur_bg_) != 0) {
            std::strncpy(cur_bg_, bg_lc, sizeof(cur_bg_) - 1);
            cur_bg_[sizeof(cur_bg_) - 1] = '\0';
            if (!cur_pos_[0]) { std::strncpy(cur_pos_, "wit", sizeof(cur_pos_) - 1); cur_pos_[sizeof(cur_pos_) - 1] = '\0'; }
            queue_scene();
        }
    }

    if (gs.ic_anim.pending) begin_message();

    msg_age_ms_  += dt_ms;
    music_age_ms_ += dt_ms;
    resolve_assets();   // decode anything that's now in the prefetch cache

    // IC composer: warm emote-button thumbnails without blocking — prefetch on
    // open/move, then decode a few prefetched buttons per frame (char-select style).
    if (active_panel_ == CourtroomPanel::ICInput && own_loaded_ &&
        own_char_.emotion_count > 0) {
        if (ic_buttons_dirty_) { prefetch_emote_buttons(); ic_buttons_dirty_ = false; }
        int cid = gs.my_char_id;
        if (cid >= 0 && cid < gs.char_count && gs.characters[cid].name[0]) {
            char lc[64]; lc_copy(lc, sizeof(lc), gs.characters[cid].name);
            char p[256];
            int budget = 10;   // small 40x40 PNGs; warmed at courtroom entry
            for (int i = 0; i < own_char_.emotion_count && budget > 0; ++i) {
                std::snprintf(p, sizeof(p), "characters/%s/emotions/button%d_off.png", lc, i + 1);
                if (!app_.tex_cache().peek(p) && AssetManager::has_prefetch(p)) {
                    app_.tex_cache().get(app_.renderer().raw(), p);
                    --budget;
                }
            }
            std::snprintf(p, sizeof(p), "characters/%s/emotions/button%d_on.png",
                          lc, ic_emote_sel_ + 1);
            if (!app_.tex_cache().peek(p) && AssetManager::has_prefetch(p))
                app_.tex_cache().get(app_.renderer().raw(), p);
        }
    }

    // Advance animations.
    bg_player_.update(dt_ms);
    if (desk_ready_) desk_player_.update(dt_ms);
    if (pair_ready_) pair_player_.update(dt_ms);

    switch (phase_) {
        case Phase::Loading:
            // Hold the line until the speaker sprite is decoded (or the gate
            // elapses), so text + animation start together and crisp.
            if (char_ready() || msg_age_ms_ >= LOAD_GATE_MS)
                start_pending_phase();
            break;

        case Phase::Shout:
            if (shout_ready_) shout_player_.update(dt_ms);
            shout_acc_ += dt_ms;
            if (shout_acc_ >= SHOUT_MS) {
                if (m_use_pre_) { phase_ = Phase::Preanim; preanim_player_.set_loop(false); preanim_player_.reset(); }
                else            enter_talking();
            }
            break;

        case Phase::Preanim:
            if (preanim_ready_) {
                preanim_player_.update(dt_ms);
                if (!preanim_player_.animated() || preanim_player_.finished())
                    enter_talking();
            } else if (msg_age_ms_ >= ASSET_GIVEUP_MS) {
                enter_talking();   // pre-anim never arrived — skip it
            }
            break;

        case Phase::Talking: {
            talk_player_.update(dt_ms);
            idle_player_.update(dt_ms);
            if (tw_pos_ < tw_max_) {
                tw_acc_ += dt_ms;
                while (tw_acc_ >= TYPEWRITER_MS && tw_pos_ < tw_max_) {
                    tw_acc_ -= TYPEWRITER_MS;
                    char c = gs.ic_anim.message[tw_pos_];
                    ++tw_pos_;
                    if (c != ' ' && ++tw_since_blip_ >= BLIP_EVERY) {
                        tw_since_blip_ = 0;
                        play_blip(app_, m_blip_);
                    }
                }
            }
            break;
        }

        case Phase::Idle:
        default:
            break;
    }

    if (realize_ms_ > 0)
        realize_ms_ = (realize_ms_ > dt_ms) ? realize_ms_ - dt_ms : 0;

    if (shake_frames_ > 0) {
        --shake_frames_;
        shake_x_ = (std::rand() % 13) - 6;
        shake_y_ = (std::rand() % 13) - 6;
    } else {
        shake_x_ = shake_y_ = 0;
    }
}

// ── Rendering ─────────────────────────────────────────────────────────────────

void CourtroomScreen::draw_sprite_fill(APNGPlayer& p, int off_x_pct, bool flip) {
    SDL_Texture* t = p.current();
    if (!t) return;
    const ThemeLayout& tl = app_.theme().layout();
    SDL_Rect vp = {tl.viewport.x + shake_x_, tl.viewport.y + shake_y_,
                   tl.viewport.w, tl.viewport.h};
    int dx = vp.w * off_x_pct / 100;
    SDL_Rect dst = {vp.x + dx, vp.y, vp.w, vp.h};
    app_.renderer().draw(t, nullptr, &dst, flip);
}

void CourtroomScreen::render() {
    render_viewport();
    render_ic_log();        // history, under the chat bar and any open panel
    render_chat_area();
    render_side_panel();
    render_active_panel();
}

// Always-on IC log. Each entry is a showname line plus its word-wrapped message
// (so nothing is cut off), stacked from the BOTTOM with the newest line last.
// Drawn strings are stable, so after their first frame every draw is a
// TextRenderer cache hit (a blit, no rasterisation); a clip rect trims the
// top-most partially-visible entry. ic_log_scroll_ (mouse wheel) skips that many
// newest entries to read back through history.
void CourtroomScreen::render_ic_log() {
    GameState& gs = app_.state();
    const ChatLog& log = gs.ic_log;
    if (log.count == 0) return;

    Renderer&     r   = app_.renderer();
    TextRenderer& txt = app_.text();
    const SDL_Rect box = Layout::IC_LOG;
    const int lh = txt.line_h() > 0 ? txt.line_h() : 20;

    r.fill_rect(box, {6, 8, 16, 170});
    r.draw_rect(box, {50, 60, 95, 180});

    SDL_Rect clip = {box.x + 8, box.y + 6, box.w - 16, box.h - 12};
    SDL_RenderSetClipRect(r.raw(), &clip);

    if (ic_log_scroll_ < 0) ic_log_scroll_ = 0;
    if (ic_log_scroll_ > log.count - 1) ic_log_scroll_ = log.count - 1;

    const int gap = 8;
    int newest = log.count - 1 - ic_log_scroll_;   // bottom-most entry to draw
    int y = clip.y + clip.h;                        // fill upward from the bottom
    for (int i = newest; i >= 0 && y > clip.y; --i) {
        const ChatEntry& e = log.at(i);
        int mh = txt.wrapped_height(e.message, clip.w);
        if (mh < lh) mh = lh;
        int eh  = lh + mh;                          // name line + wrapped message
        int top = y - eh;

        char nm[80];
        std::snprintf(nm, sizeof(nm), "%s:", e.name);
        txt.draw(nm, clip.x, top, {170, 200, 240, 255});
        SDL_Color col = TEXT_COLORS[(e.color < 10) ? e.color : 0];
        txt.draw_wrapped(e.message, clip.x, top + lh, clip.w, col);

        y = top - gap;
    }

    SDL_RenderSetClipRect(r.raw(), nullptr);
}

void CourtroomScreen::render_viewport() {
    Renderer& r = app_.renderer();
    const ThemeLayout& tl = app_.theme().layout();
    SDL_Rect vp = {tl.viewport.x + shake_x_, tl.viewport.y + shake_y_,
                   tl.viewport.w, tl.viewport.h};

    // Background (or black until the first frame streams in).
    if (bg_player_.current()) r.draw(bg_player_.current(), nullptr, &vp);
    else                      r.fill_rect(vp, {0, 0, 0, 255});

    // Pair partner first (behind), then the speaker.
    if (m_has_pair_ && pair_ready_)
        draw_sprite_fill(pair_player_, m_pair_off_, m_pair_flip_);

    // Choose the speaker frame: pre-anim → talk (while typing) → idle.
    APNGPlayer* who = nullptr;
    if (phase_ == Phase::Preanim && preanim_ready_) {
        who = &preanim_player_;
    } else if (phase_ == Phase::Talking && tw_pos_ < tw_max_ && talk_player_.current()) {
        who = &talk_player_;
    } else if (idle_player_.current()) {
        who = &idle_player_;
    } else if (talk_player_.current()) {
        who = &talk_player_;   // char only shipped a (b) sprite
    }
    if (who) draw_sprite_fill(*who, m_self_off_, m_flip_);

    // Desk / bench overlay sits on top of the characters.
    if (m_desk_visible_ && desk_ready_ && desk_player_.current())
        r.draw(desk_player_.current(), nullptr, &vp);

    // Realization white flash.
    if (realize_ms_ > 0) {
        Uint8 a = (Uint8)(200u * realize_ms_ / REALIZE_MS);
        r.fill_rect(vp, {255, 255, 255, a});
    }

    // Shout bubble fills the viewport during the shout phase.
    if (phase_ == Phase::Shout && shout_ready_ && shout_player_.current())
        r.draw(shout_player_.current(), nullptr, &vp);
}

void CourtroomScreen::render_chat_area() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();
    const ThemeLayout& tl = app_.theme().layout();
    TextRenderer& txt = app_.text();
    const ICAnimState& ic = gs.ic_anim;
    const int lh = txt.line_h() > 0 ? txt.line_h() : 20;

    // Subtle full-width base so the chat controls feel grounded on the stage.
    r.fill_rect(tl.chatbox, {8, 10, 18, 216});
    r.fill_rect({tl.chatbox.x, tl.chatbox.y, tl.chatbox.w, 2}, {70, 100, 165, 255});

    // ── Chatbox (incoming IC text) with the showname MERGED in as a corner tab ──
    const SDL_Rect box = tl.ic_text;
    r.fill_rect(box, {14, 16, 30, 245});
    r.draw_rect(box, {70, 95, 150, 255});

    const char* display_name = ic.showname[0] ? ic.showname : ic.char_name;
    if (display_name[0]) {
        int pw = txt.measure_w(display_name) + 24;
        if (pw > tl.nameplate.w) pw = tl.nameplate.w;
        if (pw < 60) pw = 60;
        SDL_Rect tab = {tl.nameplate.x, tl.nameplate.y, pw, tl.nameplate.h};
        r.fill_rect(tab, {40, 60, 120, 255});
        r.draw_rect(tab, {110, 150, 220, 255});
        // Paint over the box's top border under the tab so they read as one shape.
        r.fill_rect({tab.x + 1, box.y - 1, tab.w - 2, 3}, {40, 60, 120, 255});
        int nyt = tab.y + (tab.h - lh) / 2;
        txt.draw(display_name, tab.x + 12, nyt, {255, 255, 255, 255});
    }

    // IC text (typewriter) inside the box, clipped so a long line can't spill out.
    SDL_Rect tbox = {box.x + 14, box.y + 10, box.w - 28, box.h - 18};
    SDL_RenderSetClipRect(r.raw(), &tbox);
    if (tw_pos_ > 0) {
        SDL_Color col = TEXT_COLORS[(m_color_ >= 0 && m_color_ < 10) ? m_color_ : 0];
        txt.draw_wrapped_upto(ic.message, tbox.x, tbox.y, tbox.w, col, tw_pos_);
    } else {
        txt.draw("(IC messages appear here)", tbox.x, tbox.y, {90, 100, 125, 255});
    }
    SDL_RenderSetClipRect(r.raw(), nullptr);

    // ── Tap-to-talk input bar with inline emote arrows (easy emote access) ──────
    // Tap the bar (anywhere but the arrows) or press Enter to type & send with
    // the current emote/colour/pos; < > change the emote without the composer.
    const SDL_Rect bar = Layout::IC_INPUT_BAR;
    r.fill_rect(bar, {16, 22, 42, 245});
    r.draw_rect(bar, {80, 120, 190, 255});
    int by = bar.y + (bar.h - lh) / 2;

    SDL_Rect aL = {bar.x + 4,   bar.y + 4, 30, bar.h - 8};   // keep in sync w/ handle_tap
    SDL_Rect aR = {bar.x + 214, bar.y + 4, 30, bar.h - 8};
    r.fill_rect(aL, {30, 44, 78, 255}); r.draw_rect(aL, {90, 130, 200, 255});
    r.fill_rect(aR, {30, 44, 78, 255}); r.draw_rect(aR, {90, 130, 200, 255});
    txt.draw("<", aL.x + (aL.w - txt.measure_w("<")) / 2, by, {185, 215, 255, 255});
    txt.draw(">", aR.x + (aR.w - txt.measure_w(">")) / 2, by, {185, 215, 255, 255});
    const char* em = (own_loaded_ && own_char_.emotion_count > 0 &&
                      ic_emote_sel_ >= 0 && ic_emote_sel_ < own_char_.emotion_count)
                     ? own_char_.emotions[ic_emote_sel_].name : "normal";
    char emn[24];
    std::snprintf(emn, sizeof(emn), "%.14s", em);
    txt.draw(emn, aL.x + aL.w + 8, by, {150, 200, 255, 255});

    const char* shown = ic_text_[0] ? ic_text_ : "Tap here or Enter to talk...";
    txt.draw(shown, aR.x + aR.w + 12, by,
             ic_text_[0] ? SDL_Color{225, 225, 235, 255} : SDL_Color{120, 128, 150, 255});
}

// Top/bottom HUD overlaid on the full-screen stage: HP bars in the top
// corners, the now-playing strip top-centre, and the action-button row at the
// chat bar's right edge. Everything sits on its own dark backing so it stays
// readable over any background — there is no longer a big opaque side panel.
void CourtroomScreen::render_side_panel() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();
    const ThemeLayout& tl = app_.theme().layout();
    TextRenderer& txt = app_.text();
    const int lh = txt.line_h() > 0 ? txt.line_h() : 20;

    // ── HP bars, each with an inline label chip. ──────────────────────────────
    auto draw_hp = [&](const SDL_Rect& bar, int val, SDL_Color fill,
                       const char* label) {
        const int lw = 52;
        SDL_Rect chip  = {bar.x, bar.y, lw, bar.h};
        SDL_Rect gauge = {bar.x + lw + 3, bar.y, bar.w - lw - 3, bar.h};
        r.fill_rect(chip, {0, 0, 0, 205});
        txt.draw(label, chip.x + 7, chip.y + (chip.h - lh) / 2, {235, 238, 248, 255});
        r.fill_rect(gauge, {0, 0, 0, 205});
        if (val > 0) {
            int v = val > 10 ? 10 : val;
            r.fill_rect({gauge.x, gauge.y, gauge.w * v / 10, gauge.h}, fill);
        }
        r.draw_rect(gauge, {90, 95, 125, 255});
    };
    draw_hp(tl.hp_def, gs.hp_defense,     {60, 140, 220, 255}, "DEF");
    draw_hp(tl.hp_pro, gs.hp_prosecution, {220, 60,  60, 255}, "PRO");

    // ── Now-playing strip. ────────────────────────────────────────────────────
    if (gs.current_music[0] != '\0') {
        r.fill_rect(tl.music_name, {0, 0, 0, 185});
        char np[180];
        std::snprintf(np, sizeof(np), "Music: %s", gs.current_music);
        int ty = tl.music_name.y + (tl.music_name.h - lh) / 2;
        txt.draw(np, tl.music_name.x + 10, ty, {170, 200, 255, 255});
    }

    // ── Action buttons with control-key hints. ────────────────────────────────
    auto draw_btn = [&](const SDL_Rect& rect, bool on, const char* label,
                        const char* key) {
        r.fill_rect(rect, on ? SDL_Color{70, 120, 200, 255}
                             : SDL_Color{22, 26, 44, 235});
        r.draw_rect(rect, on ? SDL_Color{150, 190, 255, 255}
                             : SDL_Color{80, 95, 140, 255});
        // Center the two-line block (function label over control-key hint).
        int pad = (rect.h - 2 * lh) / 2;
        if (pad < 2) pad = 2;
        int tx = rect.x + (rect.w - txt.measure_w(label)) / 2;
        txt.draw(label, tx, rect.y + pad, {225, 232, 255, 255});
        int kx = rect.x + (rect.w - txt.measure_w(key)) / 2;
        txt.draw(key, kx, rect.y + pad + lh, {150, 168, 205, 255});
    };
    draw_btn(tl.btn_ic,       active_panel_ == CourtroomPanel::ICInput,  "IC",    "X");
    draw_btn(tl.btn_ooc,      active_panel_ == CourtroomPanel::OOC,      "OOC",   "L");
    draw_btn(tl.btn_music,    active_panel_ == CourtroomPanel::Music,    "Music", "R");
    draw_btn(tl.btn_evidence, active_panel_ == CourtroomPanel::Evidence, "Evi",   "Y");
    draw_btn(tl.btn_area,     active_panel_ == CourtroomPanel::Area,     "Rooms", "-");
}

void CourtroomScreen::render_active_panel() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();
    const ThemeLayout& tl = app_.theme().layout();
    TextRenderer& txt = app_.text();

    if (active_panel_ == CourtroomPanel::None) return;
    const int lh = txt.line_h() > 0 ? txt.line_h() : 20;

    if (active_panel_ == CourtroomPanel::OOC) {
        r.fill_rect(tl.panel_ooc, {10, 10, 20, 235});
        r.draw_rect(tl.panel_ooc, {60, 80, 140, 255});
        const int row_h   = lh * 2 + 6;
        const int visible = (tl.panel_ooc.h - 8) / row_h;
        int start = gs.ooc_log.count > visible ? gs.ooc_log.count - visible : 0;
        start -= ooc_scroll_;
        if (start < 0) start = 0;
        const char* me = app_.username();
        for (int i = 0; i < visible && (start + i) < gs.ooc_log.count; ++i) {
            const ChatEntry& ce = gs.ooc_log.at(start + i);
            int ry = tl.panel_ooc.y + 4 + i * row_h;
            bool mine = !ce.server && me[0] && std::strcmp(ce.name, me) == 0;
            if (mine) {
                // Highlight + left accent so your own lines are easy to spot.
                r.fill_rect({tl.panel_ooc.x + 2, ry - 2, tl.panel_ooc.w - 4, row_h},
                            {30, 46, 74, 190});
                r.fill_rect({tl.panel_ooc.x + 2, ry - 2, 3, row_h}, {120, 200, 255, 255});
            }
            char header[96];
            std::snprintf(header, sizeof(header), "[%s]", ce.name);
            SDL_Color name_col = ce.server ? SDL_Color{255, 200, 120, 255}
                               : mine      ? SDL_Color{130, 210, 255, 255}
                                           : SDL_Color{140, 200, 140, 255};
            txt.draw(header, tl.panel_ooc.x + 8, ry, name_col);
            txt.draw_wrapped(ce.message, tl.panel_ooc.x + 8, ry + lh,
                             tl.panel_ooc.w - 16,
                             mine ? SDL_Color{255, 255, 255, 255}
                                  : SDL_Color{215, 215, 222, 255});
        }
        txt.draw("A: OOC msg   B: close", tl.panel_ooc.x + 8,
                 tl.panel_ooc.y + tl.panel_ooc.h - lh - 4, {150, 150, 170, 255});

    } else if (active_panel_ == CourtroomPanel::Music) {
        r.fill_rect(tl.panel_music, {10, 10, 20, 235});
        r.draw_rect(tl.panel_music, {60, 80, 140, 255});
        const int row_h   = lh + 10;
        const int visible = (tl.panel_music.h - 8) / row_h;
        if (music_sel_ < music_scroll_) music_scroll_ = music_sel_;
        if (music_sel_ >= music_scroll_ + visible) music_scroll_ = music_sel_ - visible + 1;
        for (int i = 0; i < visible && (music_scroll_ + i) < gs.music_count; ++i) {
            int idx = music_scroll_ + i;
            const char* track = gs.music_list[idx];
            SDL_Rect row = {tl.panel_music.x + 4, tl.panel_music.y + 4 + i * row_h,
                            tl.panel_music.w - 8, row_h - 2};
            bool cur = std::strcmp(track, gs.current_music) == 0;
            bool sel = idx == music_sel_;
            r.fill_rect(row, sel ? SDL_Color{50,100,180,220}
                                 : (cur ? SDL_Color{40,70,110,200} : SDL_Color{20,20,40,180}));
            r.draw_rect(row, {50, 50, 80, 255});
            SDL_Color tc = sel ? SDL_Color{255,255,255,255}
                               : (cur ? SDL_Color{255,255,100,255} : SDL_Color{200,210,230,255});
            txt.draw(track, row.x + 6, row.y + (row.h - lh) / 2, tc);
        }

    } else if (active_panel_ == CourtroomPanel::Evidence) {
        r.fill_rect(tl.panel_evidence, {10, 10, 20, 235});
        r.draw_rect(tl.panel_evidence, {60, 80, 140, 255});
        for (int i = 0; i < gs.evidence_count && i < 24; ++i) {
            int ry = tl.panel_evidence.y + 6 + i * (lh + 4);
            txt.draw(gs.evidence[i].name, tl.panel_evidence.x + 8, ry, {210, 220, 240, 255});
        }

    } else if (active_panel_ == CourtroomPanel::Area) {
        const SDL_Rect panel = tl.panel_music;   // panels share the right-side rect
        r.fill_rect(panel, {10, 10, 20, 235});
        r.draw_rect(panel, {60, 80, 140, 255});
        txt.draw("Rooms  (A: join   B: close)", panel.x + 10, panel.y + 6,
                 {200, 220, 255, 255});

        const int row_h   = lh + 14;
        const int top     = panel.y + 10 + lh + 6;
        const int avail   = panel.y + panel.h - top - 6;
        const int visible = row_h > 0 ? avail / row_h : 1;
        if (area_sel_ >= gs.area_count) area_sel_ = gs.area_count - 1;
        if (area_sel_ < 0) area_sel_ = 0;
        if (area_sel_ < area_scroll_) area_scroll_ = area_sel_;
        if (visible > 0 && area_sel_ >= area_scroll_ + visible)
            area_scroll_ = area_sel_ - visible + 1;
        if (area_scroll_ < 0) area_scroll_ = 0;

        for (int i = 0; i < visible && (area_scroll_ + i) < gs.area_count; ++i) {
            int idx = area_scroll_ + i;
            const AreaInfo& a = gs.areas[idx];
            SDL_Rect row = {panel.x + 4, top + i * row_h, panel.w - 8, row_h - 2};
            bool here = idx == gs.my_area_idx;
            bool sel  = idx == area_sel_;
            r.fill_rect(row, sel ? SDL_Color{50,100,180,225}
                                 : (here ? SDL_Color{40,70,110,200} : SDL_Color{20,20,40,180}));
            r.draw_rect(row, {50, 50, 80, 255});
            SDL_Color tc = sel ? SDL_Color{255,255,255,255}
                               : (here ? SDL_Color{255,255,150,255} : SDL_Color{205,212,230,255});
            txt.draw(a.name, row.x + 8, row.y + (row.h - lh) / 2, tc);
            // Right-aligned player count + status (+ lock marker).
            char meta[80];
            bool locked = std::strcmp(a.lock_state, "LOCKED") == 0;
            std::snprintf(meta, sizeof(meta), "%d  %s%s", a.players, a.status,
                          locked ? "  [LOCKED]" : "");
            int mw = txt.measure_w(meta);
            txt.draw(meta, row.x + row.w - mw - 8, row.y + (row.h - lh) / 2,
                     {150, 175, 205, 255});
        }
        if (gs.area_count == 0)
            txt.draw("(no area list from this server)", panel.x + 10, top,
                     {150, 150, 160, 255});

    } else if (active_panel_ == CourtroomPanel::ICInput) {
        const SDL_Rect box = Layout::IC_COMPOSER;
        r.fill_rect(box, {12, 14, 26, 246});
        r.draw_rect(box, {90, 130, 210, 255});
        r.fill_rect({box.x, box.y, box.w, lh + 16}, {30, 45, 90, 255});
        const char* cn = (gs.my_char_id >= 0 && gs.my_char_id < gs.char_count)
                         ? gs.characters[gs.my_char_id].name : "";
        char title[96];
        std::snprintf(title, sizeof(title), "Compose IC Message%s%s",
                      cn[0] ? "   -   " : "", cn);
        txt.draw(title, box.x + 16, box.y + 8, {225, 235, 255, 255});

        const int content_y = box.y + lh + 28;
        const int total = own_loaded_ ? own_char_.emotion_count : 0;

        // ── Emote grid (left): names + sprite thumbnails, selected highlighted ──
        const int cols = 4, cell_w = 144, cell_h = 56, gap = 6, rows_vis = 5;
        const int grid_x = box.x + 20, grid_y = content_y;
        if (ic_emote_sel_ < 0) ic_emote_sel_ = 0;
        if (total > 0 && ic_emote_sel_ >= total) ic_emote_sel_ = total - 1;
        int sel_row = total > 0 ? ic_emote_sel_ / cols : 0;
        if (sel_row < ic_emote_scroll_) ic_emote_scroll_ = sel_row;
        if (sel_row >= ic_emote_scroll_ + rows_vis) ic_emote_scroll_ = sel_row - rows_vis + 1;
        if (ic_emote_scroll_ < 0) ic_emote_scroll_ = 0;

        if (total == 0)
            txt.draw("(no char.ini emotes — your line will use 'normal')",
                     grid_x, grid_y, {165, 165, 175, 255});

        for (int vr = 0; vr < rows_vis; ++vr) {
            for (int col = 0; col < cols; ++col) {
                int idx = (ic_emote_scroll_ + vr) * cols + col;
                if (idx >= total) continue;
                SDL_Rect cell = {grid_x + col * (cell_w + gap),
                                 grid_y + vr * (cell_h + gap), cell_w, cell_h};
                bool sel = idx == ic_emote_sel_;
                r.fill_rect(cell, sel ? SDL_Color{50, 90, 170, 235}
                                      : SDL_Color{22, 26, 46, 220});
                r.draw_rect(cell, sel ? SDL_Color{150, 190, 255, 255}
                                      : SDL_Color{70, 80, 120, 255});
                SDL_Texture* th = emote_button_tex(idx, sel);
                if (!th) th = emote_button_tex(idx, false);
                int tx_off = 6;
                if (th) {
                    SDL_Rect td = {cell.x + 5, cell.y + (cell_h - 44) / 2, 44, 44};
                    r.draw(th, nullptr, &td);
                    tx_off = 56;
                }
                char nm[20];
                std::snprintf(nm, sizeof(nm), "%.13s", own_char_.emotions[idx].name);
                txt.draw(nm, cell.x + tx_off, cell.y + (cell_h - lh) / 2,
                         sel ? SDL_Color{255, 255, 255, 255}
                             : SDL_Color{210, 215, 232, 255});
            }
        }

        // ── Preview + options (right) ──────────────────────────────────────────
        const int rx = grid_x + cols * (cell_w + gap) + 14;
        SDL_Rect prev = {rx, content_y, box.x + box.w - rx - 20, 200};
        r.fill_rect(prev, {6, 8, 16, 255});
        r.draw_rect(prev, {70, 90, 140, 255});
        SDL_Texture* pv = nullptr;
        if (total > 0) {
            pv = emote_button_tex(ic_emote_sel_, true);
            if (!pv) pv = emote_button_tex(ic_emote_sel_, false);
        }
        if (pv) {
            int side = (prev.w < prev.h ? prev.w : prev.h) - 24;
            SDL_Rect pd = {prev.x + (prev.w - side) / 2, prev.y + (prev.h - side) / 2,
                           side, side};
            r.draw(pv, nullptr, &pd);
        } else {
            const char* msg = total > 0 ? "loading sprite..." : "no sprite art";
            txt.draw(msg, prev.x + 12, prev.y + prev.h / 2 - lh / 2, {120, 128, 150, 255});
        }

        int oy = prev.y + prev.h + 12;
        char line[160];
        const char* en = total > 0 ? own_char_.emotions[ic_emote_sel_].name : "normal";
        std::snprintf(line, sizeof(line), "Emote: %.20s", en);
        txt.draw(line, rx, oy, {220, 224, 235, 255}); oy += lh + 6;
        std::snprintf(line, sizeof(line), "Colour: %d", ic_color_);
        int cw = txt.draw(line, rx, oy, {220, 224, 235, 255});
        SDL_Rect sw = {rx + cw + 12, oy + 2, 26, lh - 4};
        r.fill_rect(sw, TEXT_COLORS[(ic_color_ >= 0 && ic_color_ < 10) ? ic_color_ : 0]);
        r.draw_rect(sw, {200, 200, 210, 255}); oy += lh + 6;
        std::snprintf(line, sizeof(line), "Position: %s", ic_pos_);
        txt.draw(line, rx, oy, {200, 205, 220, 255});

        // ── Message preview + controls (bottom) ────────────────────────────────
        int by2 = grid_y + rows_vis * (cell_h + gap) + 14;
        SDL_Rect mp = {box.x + 20, by2, box.w - 40, lh * 2 + 12};
        r.fill_rect(mp, {6, 8, 16, 255});
        r.draw_rect(mp, {60, 70, 110, 255});
        bool has = ic_text_[0] != '\0';
        txt.draw_wrapped(has ? ic_text_ : "(press A to type your message)",
                         mp.x + 8, mp.y + 6, mp.w - 16,
                         has ? SDL_Color{235, 235, 245, 255}
                             : SDL_Color{120, 128, 150, 255});
        txt.draw("< >  emote     up/dn  colour     A  type & send     B  close",
                 box.x + 20, box.y + box.h - lh - 12, {160, 175, 205, 255});
    }
}

// ── Input ───────────────────────────────────────────────────────────────────────

void CourtroomScreen::compose_and_send() {
    GameState& gs = app_.state();
    int cid = gs.my_char_id;
    if (cid < 0 || cid >= gs.char_count) return;
    if (!own_loaded_) load_own_character();

    char text[256] = {};
    kb_active_ = true;
    bool ok = show_keyboard("IC message", "", text, sizeof(text));  // fresh field each time
    kb_active_ = false;
    if (!ok || !text[0]) return;
    std::strncpy(ic_text_, text, sizeof(ic_text_) - 1);
    ic_text_[sizeof(ic_text_) - 1] = '\0';

    cmd::MSParams p;
    std::strncpy(p.char_name, gs.characters[cid].name, sizeof(p.char_name) - 1);
    if (own_loaded_ && own_char_.emotion_count > 0 &&
        ic_emote_sel_ >= 0 && ic_emote_sel_ < own_char_.emotion_count) {
        const EmotionEntry& em = own_char_.emotions[ic_emote_sel_];
        std::strncpy(p.emote, em.idle_anim, sizeof(p.emote) - 1);
        if (em.has_pre) {
            std::strncpy(p.pre_anim, em.pre_anim, sizeof(p.pre_anim) - 1);
            p.emote_mod = 1;
        }
        p.desk_mod = em.desk_mod;
    } else {
        std::strncpy(p.emote, "normal", sizeof(p.emote) - 1);
    }
    std::strncpy(p.message,  text,             sizeof(p.message)  - 1);
    std::strncpy(p.pos,      ic_pos_,          sizeof(p.pos)      - 1);
    std::strncpy(p.showname, app_.username(),  sizeof(p.showname) - 1);
    p.char_id    = cid;
    p.text_color = ic_color_;

    char buf[2048];
    int n = cmd::ms(buf, sizeof(buf), p);
    if (n > 0) {
        app_.send_packet(buf, n);
        // Close the composer so the courtroom (and the line we just sent) is
        // visible; reopen with X to send another.
        active_panel_ = CourtroomPanel::None;
    }
}

// ── Shared actions (controller / keyboard / touch all funnel here) ──────────────

void CourtroomScreen::play_music(int idx) {
    GameState& gs = app_.state();
    if (idx < 0 || idx >= gs.music_count) return;
    char buf[256];
    int n = cmd::mc(buf, sizeof(buf), gs.music_list[idx],
                    gs.my_char_id < 0 ? 0 : gs.my_char_id, app_.username());
    if (n > 0) app_.send_packet(buf, n);
}

void CourtroomScreen::join_area(int idx) {
    GameState& gs = app_.state();
    if (idx < 0 || idx >= gs.area_count) return;
    // AO2 joins an area by sending a music-change to the area's name; the server
    // moves us and follows up with BN / HP / chars, etc.
    char buf[256];
    int n = cmd::mc(buf, sizeof(buf), gs.areas[idx].name,
                    gs.my_char_id < 0 ? 0 : gs.my_char_id, app_.username());
    if (n > 0) {
        app_.send_packet(buf, n);
        gs.my_area_idx = idx;                  // optimistic
        active_panel_ = CourtroomPanel::None;
    }
}

void CourtroomScreen::compose_ooc() {
    char text[256] = {};
    kb_active_ = true;
    bool ok = show_keyboard("OOC message", "", text, sizeof(text));
    kb_active_ = false;
    if (ok && text[0]) {
        char buf[512];
        int n = cmd::ct(buf, sizeof(buf), app_.username(), text);
        if (n > 0) app_.send_packet(buf, n);
    }
}

void CourtroomScreen::cycle_emote(int dir) {
    if (!own_loaded_) load_own_character();
    int n = own_char_.emotion_count;
    if (n <= 0) return;
    ic_emote_sel_ = (ic_emote_sel_ + dir + n) % n;
    ic_buttons_dirty_ = true;   // warm the new emote's preview button
}

// ── Touch ───────────────────────────────────────────────────────────────────────

void CourtroomScreen::handle_tap(int x, int y) {
    GameState& gs = app_.state();
    const ThemeLayout& tl = app_.theme().layout();

    // While a panel is open it covers the HUD buttons, so route taps to it.
    if (active_panel_ != CourtroomPanel::None) { handle_panel_tap(x, y); return; }

    // HUD buttons open their panel.
    if (pt_in(x, y, tl.btn_ic)) { active_panel_ = CourtroomPanel::ICInput; ic_buttons_dirty_ = true; return; }
    if (pt_in(x, y, tl.btn_ooc))      { active_panel_ = CourtroomPanel::OOC;      return; }
    if (pt_in(x, y, tl.btn_music))    { active_panel_ = CourtroomPanel::Music;    return; }
    if (pt_in(x, y, tl.btn_evidence)) { active_panel_ = CourtroomPanel::Evidence; return; }
    if (pt_in(x, y, tl.btn_area)) {
        active_panel_ = CourtroomPanel::Area;
        if (gs.my_area_idx >= 0 && gs.my_area_idx < gs.area_count) area_sel_ = gs.my_area_idx;
        return;
    }

    // IC input bar: < > change emote; anywhere else opens the keyboard to talk.
    {
        const SDL_Rect bar = Layout::IC_INPUT_BAR;   // arrow rects in sync w/ render
        SDL_Rect aL = {bar.x + 4,   bar.y + 4, 30, bar.h - 8};
        SDL_Rect aR = {bar.x + 214, bar.y + 4, 30, bar.h - 8};
        if (pt_in(x, y, aL)) { cycle_emote(-1); return; }
        if (pt_in(x, y, aR)) { cycle_emote(+1); return; }
        if (pt_in(x, y, bar)) { compose_and_send(); return; }
    }

    // Tapping the rest of the chat bar skips the typewriter, or (when idle)
    // opens the keyboard to send a line with the current emote/colour/pos.
    if (pt_in(x, y, tl.chatbox) || pt_in(x, y, tl.nameplate)) {
        if (phase_ == Phase::Talking && tw_pos_ < tw_max_) tw_pos_ = tw_max_;
        else                                               compose_and_send();
    }
}

// Hit-test the open panel. Geometry MUST match render_active_panel().
void CourtroomScreen::handle_panel_tap(int x, int y) {
    GameState& gs = app_.state();
    const ThemeLayout& tl = app_.theme().layout();
    const int lh = app_.text().line_h() > 0 ? app_.text().line_h() : 20;

    if (active_panel_ == CourtroomPanel::Music) {
        const SDL_Rect& p = tl.panel_music;
        if (!pt_in(x, y, p)) { active_panel_ = CourtroomPanel::None; return; }
        const int row_h = lh + 10;
        int i = (y - (p.y + 4)) / row_h;
        int idx = music_scroll_ + i;
        if (i >= 0 && idx >= 0 && idx < gs.music_count) { music_sel_ = idx; play_music(idx); }
        return;
    }
    if (active_panel_ == CourtroomPanel::Area) {
        const SDL_Rect& p = tl.panel_music;   // area panel shares this rect
        if (!pt_in(x, y, p)) { active_panel_ = CourtroomPanel::None; return; }
        const int row_h = lh + 14;
        const int top   = p.y + 10 + lh + 6;
        if (y >= top) {
            int idx = area_scroll_ + (y - top) / row_h;
            if (idx >= 0 && idx < gs.area_count) { area_sel_ = idx; join_area(idx); }
        }
        return;
    }
    if (active_panel_ == CourtroomPanel::OOC) {
        if (!pt_in(x, y, tl.panel_ooc)) { active_panel_ = CourtroomPanel::None; return; }
        compose_ooc();   // tap the OOC panel to type a line
        return;
    }
    if (active_panel_ == CourtroomPanel::Evidence) {
        if (!pt_in(x, y, tl.panel_evidence)) active_panel_ = CourtroomPanel::None;
        return;
    }
    if (active_panel_ == CourtroomPanel::ICInput) {
        const SDL_Rect box = Layout::IC_COMPOSER;
        if (!pt_in(x, y, box)) { active_panel_ = CourtroomPanel::None; return; }
        const int content_y = box.y + lh + 28;
        const int cols = 4, cell_w = 144, cell_h = 56, gap = 6, rows_vis = 5;
        const int grid_x = box.x + 20, grid_y = content_y;
        const int total = own_loaded_ ? own_char_.emotion_count : 0;
        for (int vr = 0; vr < rows_vis; ++vr)
            for (int col = 0; col < cols; ++col) {
                int idx = (ic_emote_scroll_ + vr) * cols + col;
                if (idx >= total) continue;
                SDL_Rect cell = {grid_x + col * (cell_w + gap),
                                 grid_y + vr * (cell_h + gap), cell_w, cell_h};
                if (pt_in(x, y, cell)) { ic_emote_sel_ = idx; ic_buttons_dirty_ = true; return; }
            }
        SDL_Rect mp = {box.x + 20, grid_y + rows_vis * (cell_h + gap) + 14,
                       box.w - 40, lh * 2 + 12};
        if (pt_in(x, y, mp)) compose_and_send();
        return;
    }
}

void CourtroomScreen::handle_event(const SDL_Event& e) {
    if (kb_active_) return;
    GameState& gs = app_.state();

    // Touch / mouse tap → route to the HUD buttons, chat bar, or open panel.
    int tx, ty;
    if (tap_point(e, app_.renderer().raw(), tx, ty)) { handle_tap(tx, ty); return; }

    // Mouse wheel scrolls whatever is in focus (wheel up = back/older/up the list).
    if (e.type == SDL_MOUSEWHEEL && e.wheel.y != 0) {
        int d = e.wheel.y;
        auto clamp = [](int v, int hi) { return v < 0 ? 0 : (v > hi ? hi : v); };
        switch (active_panel_) {
            case CourtroomPanel::Music:
                music_sel_ = clamp(music_sel_ - d, gs.music_count - 1); break;
            case CourtroomPanel::Area:
                area_sel_  = clamp(area_sel_ - d,  gs.area_count - 1);  break;
            case CourtroomPanel::Evidence:
                evi_scroll_ = clamp(evi_scroll_ - d, 1 << 20);          break;
            case CourtroomPanel::OOC:
                ooc_scroll_ = clamp(ooc_scroll_ + d, 1 << 20);          break;
            case CourtroomPanel::ICInput:
                if (own_loaded_ && own_char_.emotion_count > 0) {
                    ic_emote_sel_ = clamp(ic_emote_sel_ - d * 4, own_char_.emotion_count - 1);
                    ic_buttons_dirty_ = true;
                }
                break;
            case CourtroomPanel::None:    // scroll the always-on IC log
                if (gs.ic_log.count > 0)
                    ic_log_scroll_ = clamp(ic_log_scroll_ + d, gs.ic_log.count - 1);
                break;
        }
        return;
    }

    enum Act { None, Up, Down, Left, Right, Confirm, Back,
               TogOOC, TogMusic, TogEvi, TogIC, TogArea, Disconnect } act = None;

    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_UP:    act = Up;    break;
            case SDLK_DOWN:  act = Down;  break;
            case SDLK_LEFT:  act = Left;  break;
            case SDLK_RIGHT: act = Right; break;
            case SDLK_RETURN: act = Confirm; break;
            case SDLK_ESCAPE: act = Back; break;
            case SDLK_z: act = TogOOC;   break;
            case SDLK_c: act = TogMusic; break;
            case SDLK_y: act = TogEvi;   break;
            case SDLK_x: act = TogIC;    break;
            case SDLK_r: act = TogArea;  break;
            case SDLK_p: act = Disconnect; break;
            default: break;
        }
    } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    act = Up;    break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  act = Down;  break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  act = Left;  break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: act = Right; break;
            case SDL_CONTROLLER_BUTTON_A:             act = Confirm; break;
            case SDL_CONTROLLER_BUTTON_B:             act = Back;    break;
            case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  act = TogOOC;   break;  // L
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: act = TogMusic; break;  // R
            case SDL_CONTROLLER_BUTTON_Y:             act = TogEvi;   break;
            case SDL_CONTROLLER_BUTTON_X:             act = TogIC;    break;
            case SDL_CONTROLLER_BUTTON_BACK:          act = TogArea;  break;  // -
            case SDL_CONTROLLER_BUTTON_START:         act = Disconnect; break; // +
            default: break;
        }
    }
    if (act == None) return;

    auto toggle = [&](CourtroomPanel p) {
        active_panel_ = (active_panel_ == p) ? CourtroomPanel::None : p;
    };

    switch (act) {
        case TogOOC:     toggle(CourtroomPanel::OOC);      return;
        case TogMusic:   toggle(CourtroomPanel::Music);    return;
        case TogEvi:     toggle(CourtroomPanel::Evidence); return;
        case TogIC:
            toggle(CourtroomPanel::ICInput);
            if (active_panel_ == CourtroomPanel::ICInput) ic_buttons_dirty_ = true;
            return;
        case TogArea:
            toggle(CourtroomPanel::Area);
            // Start the cursor on the current room when opening the panel.
            if (active_panel_ == CourtroomPanel::Area &&
                gs.my_area_idx >= 0 && gs.my_area_idx < gs.area_count)
                area_sel_ = gs.my_area_idx;
            return;
        case Disconnect: app_.pop_screen();                return;
        default: break;
    }

    switch (active_panel_) {
        case CourtroomPanel::None:
            if (act == Left)  cycle_emote(-1);   // change emote without the composer
            if (act == Right) cycle_emote(+1);
            if (act == Confirm) {
                // Skip the typewriter if one is playing; otherwise quick-talk.
                if (phase_ == Phase::Talking && tw_pos_ < tw_max_) tw_pos_ = tw_max_;
                else compose_and_send();
            }
            if (act == Back) app_.pop_screen();
            break;

        case CourtroomPanel::Music:
            if (act == Up)   { if (music_sel_ > 0) --music_sel_; }
            if (act == Down) { if (music_sel_ < gs.music_count - 1) ++music_sel_; }
            if (act == Back) active_panel_ = CourtroomPanel::None;
            if (act == Confirm) play_music(music_sel_);
            break;

        case CourtroomPanel::OOC:
            if (act == Up)   --ooc_scroll_;
            if (act == Down) ++ooc_scroll_;
            if (ooc_scroll_ < 0) ooc_scroll_ = 0;
            if (act == Back) active_panel_ = CourtroomPanel::None;
            if (act == Confirm) compose_ooc();
            break;

        case CourtroomPanel::Evidence:
            if (act == Up)   { if (evi_scroll_ > 0) --evi_scroll_; }
            if (act == Down) ++evi_scroll_;
            if (act == Back) active_panel_ = CourtroomPanel::None;
            break;

        case CourtroomPanel::Area:
            if (act == Up)   { if (area_sel_ > 0) --area_sel_; }
            if (act == Down) { if (area_sel_ < gs.area_count - 1) ++area_sel_; }
            if (act == Back) active_panel_ = CourtroomPanel::None;
            if (act == Confirm) join_area(area_sel_);
            break;

        case CourtroomPanel::ICInput:
            if (act == Left)  cycle_emote(-1);
            if (act == Right) cycle_emote(+1);
            if (act == Up)   ic_color_ = (ic_color_ + 1) % 10;
            if (act == Down) ic_color_ = (ic_color_ + 9) % 10;
            if (act == Back) active_panel_ = CourtroomPanel::None;
            if (act == Confirm) compose_and_send();
            break;
    }
}

} // namespace ao
