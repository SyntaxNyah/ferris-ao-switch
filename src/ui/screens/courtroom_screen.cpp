#include "courtroom_screen.hpp"
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
}

void CourtroomScreen::load_own_character() {
    GameState& gs = app_.state();
    int cid = gs.my_char_id;
    if (cid < 0 || cid >= gs.char_count || !gs.characters[cid].name[0]) return;
    own_loaded_ = load_char_ini(gs.characters[cid].name, own_char_);
}

// ── Async scene/sprite resolution ─────────────────────────────────────────────

void CourtroomScreen::queue_scene() {
    AssetStream& s = app_.asset_stream();
    scene_pending_ = true;
    bg_ready_ = desk_ready_ = false;
    prefetch_bgimg(s, cur_bg_,   bg_filename(cur_pos_));
    prefetch_bgimg(s, "default", bg_filename(cur_pos_));
    prefetch_bgimg(s, cur_bg_,   desk_filename(cur_pos_));
    prefetch_bgimg(s, "default", desk_filename(cur_pos_));
}

void CourtroomScreen::resolve_assets() {
    SDL_Renderer* r = app_.renderer().raw();
    if (msg_age_ms_ > ASSET_GIVEUP_MS && phase_ != Phase::Loading) {
        // Past the give-up window — stop probing missing assets to save cycles.
        return;
    }

    if (scene_pending_) {
        if (!bg_ready_) {
            bg_ready_ = resolve_bgimg(bg_player_, r, cur_bg_, bg_filename(cur_pos_));
            if (!bg_ready_) bg_ready_ = resolve_bgimg(bg_player_, r, "default", bg_filename(cur_pos_));
        }
        if (!desk_ready_) {
            desk_ready_ = resolve_bgimg(desk_player_, r, cur_bg_, desk_filename(cur_pos_));
            if (!desk_ready_) desk_ready_ = resolve_bgimg(desk_player_, r, "default", desk_filename(cur_pos_));
        }
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

    // Background: only reload when position or room changed.
    char pos[16];
    std::strncpy(pos, ic.pos[0] ? ic.pos : "wit", sizeof(pos) - 1);
    pos[sizeof(pos) - 1] = '\0';
    char bg_lc[128];
    lc_copy(bg_lc, sizeof(bg_lc), gs.background);
    if (std::strcmp(pos, cur_pos_) != 0 || std::strcmp(bg_lc, cur_bg_) != 0) {
        std::strncpy(cur_pos_, pos,   sizeof(cur_pos_) - 1); cur_pos_[sizeof(cur_pos_) - 1] = '\0';
        std::strncpy(cur_bg_,  bg_lc, sizeof(cur_bg_)  - 1); cur_bg_[sizeof(cur_bg_)  - 1] = '\0';
        queue_scene();
    }

    // Reset per-message readiness and queue all sprite prefetches up front.
    idle_ready_ = talk_ready_ = preanim_ready_ = pair_ready_ = shout_ready_ = false;
    msg_age_ms_ = 0;
    prefetch_emote(s, m_char_, m_emote_, "(a)");
    prefetch_emote(s, m_char_, m_emote_, "(b)");
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
    render_chat_area();
    render_side_panel();
    render_active_panel();
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

    // Chat bar: themed chatbox image if the server provides one, otherwise a
    // clean dark bar with a bright top edge. (romfs ships no chatbox image, so
    // the fallback is the usual path unless a CDN serves misc/default/chatbox.)
    if (!chatbox_tried_) {
        chatbox_tried_ = true;
        char p[256];
        if (app_.theme().resolve_image("chatbox", p, sizeof(p)))
            chatbox_tex_ = app_.tex_cache().get(r.raw(), p);
    }
    if (chatbox_tex_) {
        r.draw(chatbox_tex_, nullptr, &tl.chatbox);
    } else {
        r.fill_rect(tl.chatbox, {8, 10, 18, 232});
        r.fill_rect({tl.chatbox.x, tl.chatbox.y, tl.chatbox.w, 2}, {70, 100, 165, 255});
    }

    // Showname plate.
    const ICAnimState& ic = gs.ic_anim;
    const char* display_name = (ic.showname[0] != '\0') ? ic.showname : ic.char_name;
    if (display_name[0] != '\0') {
        r.fill_rect(tl.nameplate, {40, 70, 140, 235});
        r.draw_rect(tl.nameplate, {120, 160, 230, 255});
        int ty = tl.nameplate.y + (tl.nameplate.h - txt.line_h()) / 2;
        txt.draw(display_name, tl.nameplate.x + 10, ty, {255, 255, 255, 255});
    }

    // IC text, typewriter-clipped, word-wrapped inside the ic_text box.
    if (tw_pos_ > 0) {
        char msg_buf[512];
        int len = tw_pos_ < (int)sizeof(msg_buf) - 1 ? tw_pos_ : (int)sizeof(msg_buf) - 1;
        std::memcpy(msg_buf, ic.message, len);
        msg_buf[len] = '\0';
        SDL_Color col = TEXT_COLORS[(m_color_ >= 0 && m_color_ < 10) ? m_color_ : 0];
        txt.draw_wrapped(msg_buf, tl.ic_text.x, tl.ic_text.y, tl.ic_text.w, col);
    } else if (active_panel_ == CourtroomPanel::None) {
        // Idle: show the controls so the screen is never blank or confusing.
        txt.draw("X: type IC     ZL: OOC     ZR: Music     Y: Evidence     +: leave",
                 tl.ic_text.x, tl.ic_text.y, {120, 132, 158, 255});
    }
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
    draw_btn(tl.btn_ooc,      active_panel_ == CourtroomPanel::OOC,      "OOC",   "ZL");
    draw_btn(tl.btn_music,    active_panel_ == CourtroomPanel::Music,    "Music", "ZR");
    draw_btn(tl.btn_evidence, active_panel_ == CourtroomPanel::Evidence, "Evi",   "Y");
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
        for (int i = 0; i < visible && (start + i) < gs.ooc_log.count; ++i) {
            const ChatEntry& ce = gs.ooc_log.at(start + i);
            int ry = tl.panel_ooc.y + 4 + i * row_h;
            char header[96];
            std::snprintf(header, sizeof(header), "[%s]", ce.name);
            SDL_Color name_col = ce.server
                ? SDL_Color{255, 200, 120, 255} : SDL_Color{140, 200, 140, 255};
            txt.draw(header, tl.panel_ooc.x + 8, ry, name_col);
            txt.draw_wrapped(ce.message, tl.panel_ooc.x + 8, ry + lh,
                             tl.panel_ooc.w - 16, {220, 220, 220, 255});
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

    } else if (active_panel_ == CourtroomPanel::ICInput) {
        const SDL_Rect box = Layout::IC_COMPOSER;
        r.fill_rect(box, {12, 14, 26, 245});
        r.draw_rect(box, {90, 130, 210, 255});
        r.fill_rect({box.x, box.y, box.w, lh + 16}, {30, 45, 90, 255});
        txt.draw("Compose IC Message", box.x + 16, box.y + 8, {225, 235, 255, 255});

        const int x    = box.x + 24;
        const int step = lh + 16;
        int y = box.y + lh + 30;

        const char* emote_name = "normal (default)";
        if (own_loaded_ && own_char_.emotion_count > 0 &&
            ic_emote_sel_ >= 0 && ic_emote_sel_ < own_char_.emotion_count)
            emote_name = own_char_.emotions[ic_emote_sel_].name;
        char line[200];
        std::snprintf(line, sizeof(line), "Emote   ( < > ):  %s", emote_name);
        txt.draw(line, x, y, {220, 224, 235, 255}); y += step;

        std::snprintf(line, sizeof(line), "Colour  (up/dn):  %d", ic_color_);
        int cw = txt.draw(line, x, y, {220, 224, 235, 255});
        SDL_Rect swatch = {x + cw + 18, y + 2, 30, lh - 4};
        r.fill_rect(swatch, TEXT_COLORS[(ic_color_ >= 0 && ic_color_ < 10) ? ic_color_ : 0]);
        r.draw_rect(swatch, {200, 200, 210, 255});
        y += step;

        std::snprintf(line, sizeof(line), "Position:  %s", ic_pos_);
        txt.draw(line, x, y, {200, 205, 220, 255}); y += step + 4;

        // Typed-message preview box.
        SDL_Rect prev = {x, y, box.w - 48, lh * 2 + 12};
        r.fill_rect(prev, {6, 8, 16, 255});
        r.draw_rect(prev, {60, 70, 110, 255});
        bool has = ic_text_[0] != '\0';
        txt.draw_wrapped(has ? ic_text_ : "(press A to type your message)",
                         prev.x + 8, prev.y + 6, prev.w - 16,
                         has ? SDL_Color{235, 235, 245, 255}
                             : SDL_Color{120, 128, 150, 255});

        txt.draw("A: type message & send          B: cancel",
                 x, box.y + box.h - lh - 14, {160, 175, 205, 255});
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
    bool ok = show_keyboard("IC message", ic_text_, text, sizeof(text));
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

void CourtroomScreen::handle_event(const SDL_Event& e) {
    if (kb_active_) return;
    GameState& gs = app_.state();

    enum Act { None, Up, Down, Left, Right, Confirm, Back,
               TogOOC, TogMusic, TogEvi, TogIC, Disconnect } act = None;

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
            case SDLK_p: act = Disconnect; break;
            default: break;
        }
    } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    act = Up;    break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  act = Down;  break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  act = Left;  break;
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: act = Right; break;
            case SDL_CONTROLLER_BUTTON_A:          act = Confirm; break;
            case SDL_CONTROLLER_BUTTON_B:          act = Back;    break;
            case SDL_CONTROLLER_BUTTON_LEFTSTICK:  act = TogOOC;   break;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK: act = TogMusic; break;
            case SDL_CONTROLLER_BUTTON_Y:          act = TogEvi;   break;
            case SDL_CONTROLLER_BUTTON_X:          act = TogIC;    break;
            case SDL_CONTROLLER_BUTTON_START:      act = Disconnect; break;
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
        case TogIC:      toggle(CourtroomPanel::ICInput);  return;
        case Disconnect: app_.pop_screen();                return;
        default: break;
    }

    switch (active_panel_) {
        case CourtroomPanel::None:
            if (act == Confirm) tw_pos_ = tw_max_; // skip typewriter
            if (act == Back)    app_.pop_screen();
            break;

        case CourtroomPanel::Music:
            if (act == Up)   { if (music_sel_ > 0) --music_sel_; }
            if (act == Down) { if (music_sel_ < gs.music_count - 1) ++music_sel_; }
            if (act == Back) active_panel_ = CourtroomPanel::None;
            if (act == Confirm && music_sel_ >= 0 && music_sel_ < gs.music_count) {
                char buf[256];
                int n = cmd::mc(buf, sizeof(buf), gs.music_list[music_sel_],
                                gs.my_char_id < 0 ? 0 : gs.my_char_id, app_.username());
                if (n > 0) app_.send_packet(buf, n);
            }
            break;

        case CourtroomPanel::OOC:
            if (act == Up)   --ooc_scroll_;
            if (act == Down) ++ooc_scroll_;
            if (ooc_scroll_ < 0) ooc_scroll_ = 0;
            if (act == Back) active_panel_ = CourtroomPanel::None;
            if (act == Confirm) {
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
            break;

        case CourtroomPanel::Evidence:
            if (act == Up)   { if (evi_scroll_ > 0) --evi_scroll_; }
            if (act == Down) ++evi_scroll_;
            if (act == Back) active_panel_ = CourtroomPanel::None;
            break;

        case CourtroomPanel::ICInput:
            if (!own_loaded_) load_own_character();
            if (act == Left  && own_char_.emotion_count > 0)
                ic_emote_sel_ = (ic_emote_sel_ - 1 + own_char_.emotion_count) % own_char_.emotion_count;
            if (act == Right && own_char_.emotion_count > 0)
                ic_emote_sel_ = (ic_emote_sel_ + 1) % own_char_.emotion_count;
            if (act == Up)   ic_color_ = (ic_color_ + 1) % 10;
            if (act == Down) ic_color_ = (ic_color_ + 9) % 10;
            if (act == Back) active_panel_ = CourtroomPanel::None;
            if (act == Confirm) compose_and_send();
            break;
    }
}

} // namespace ao
