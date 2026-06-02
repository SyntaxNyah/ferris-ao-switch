#pragma once
#include "../screen.hpp"
#include "../touch.hpp"
#include "../../assets/apng_player.hpp"
#include "../../assets/char_ini_parser.hpp"
#include "../../input/soft_keyboard.hpp"
#include <cstdint>

namespace ao {

enum class CourtroomPanel { None, OOC, Music, Evidence, Area, ICInput };

// Main gameplay screen. Renders the AO2 courtroom in real time: streamed
// backgrounds, desks, character idle/talk sprites, pre-animations, shout
// bubbles, the typewriter chatbox, HP bars, music/SFX/blips, and the IC
// message composer.
//
// Loading is fully asynchronous and never blocks the render loop: when a
// message arrives, every candidate asset (all extensions) is queued on the
// AssetStream worker threads, and the main thread only ever decodes an asset
// once it is already sitting in the in-memory prefetch cache. A short Loading
// phase holds the timeline until the speaker's sprite is ready (or a small
// timeout elapses) so text and animation start crisp and in sync.
class CourtroomScreen : public Screen {
public:
    explicit CourtroomScreen(App& app);
    ~CourtroomScreen() override = default;

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void update(uint32_t dt_ms) override;
    void render() override;

private:
    // IC animation phases. Loading gates the start of the timeline on the
    // speaker sprite being decoded; Idle is the resting state between lines.
    enum class Phase { Loading, Shout, Preanim, Talking, Idle };

    // ── Sequence control (all non-blocking) ───────────────────────────────
    void begin_message();          // snapshot gs.ic_anim, queue prefetches
    void resolve_assets();         // per-frame: decode any asset that's ready
    void start_pending_phase();    // leave Loading → Shout/Preanim/Talking
    void enter_talking();          // start typewriter + message SFX
    void queue_scene();            // prefetch background + desk for cur_*
    void update_music();           // async track changes

    // ── Rendering ─────────────────────────────────────────────────────────
    void render_viewport();
    void render_ic_log();        // always-on IC scrollback (left column)
    void render_chat_area();
    void render_side_panel();
    void render_active_panel();
    void draw_sprite_fill(APNGPlayer& p, int off_x_pct, bool flip);

    // ── Touch ───────────────────────────────────────────────────────────────
    void handle_tap(int x, int y);        // route a tap to a button / chatbox / panel
    void handle_panel_tap(int x, int y);  // tap inside the open panel
    void ic_toggle_rects(SDL_Rect out[4]) const;  // composer shout/flip/realize/shake buttons
    SDL_Rect change_char_rect() const;            // "Char" HUD button (back to char select)
    void scroll_focused(int rows);        // scroll the focused panel / IC log (wheel + drag)
    int  focused_row_px() const;          // row height of the focused list (drag px → rows)

    // ── Actions (shared by controller, keyboard and touch) ──────────────────
    void join_area(int idx);   // send AO2 area-join (MC) for areas[idx]
    void play_music(int idx);  // send MC for music_list[idx]
    void compose_ooc();        // open the on-screen keyboard for an OOC (CT) line
    void send_ic(const char* text);   // build + send an MS from the typed text
    void send_ooc(const char* text);  // build + send a CT from the typed text
    void dispatch_kb_result(SoftKeyboard::Result rs);  // route SUBMIT/CANCEL to composer
    void cycle_emote(int dir);  // change the selected emote by ±1 (main view + composer)

    // ── IC composer ───────────────────────────────────────────────────────
    void load_own_character();
    void compose_and_send();

    bool char_ready() const { return idle_ready_ || talk_ready_; }

    CourtroomPanel active_panel_ = CourtroomPanel::None;

    // ── Snapshot of the message currently animating ───────────────────────
    char m_char_[64]      = {};   // lowercased speaker folder
    char m_emote_[64]     = {};   // emote anim base (lowercased)
    char m_preanim_[64]   = {};
    char m_pair_char_[64] = {};
    char m_pair_emote_[64]= {};
    char m_blip_[32]      = {};
    int  m_color_     = 0;
    int  m_emote_mod_ = 0;
    int  m_shout_     = 0;
    bool m_flip_      = false;
    bool m_pair_flip_ = false;
    int  m_self_off_  = 0;         // % of viewport width
    int  m_pair_off_  = 0;
    bool m_has_pair_  = false;
    bool m_desk_visible_ = true;
    bool m_realize_   = false;
    bool m_use_pre_   = false;     // this line plays a pre-anim

    // ── Scene caches (so we don't reload every message) ───────────────────
    char cur_bg_[128]   = {};
    char cur_pos_[16]   = {};
    char cur_music_[128]= {};
    bool         chatbox_tried_ = false;
    SDL_Texture* chatbox_tex_   = nullptr;  // owned by TextureCache, not freed here

    // ── Animation players ─────────────────────────────────────────────────
    APNGPlayer bg_player_;
    APNGPlayer desk_player_;
    APNGPlayer idle_player_;     // (a)
    APNGPlayer talk_player_;     // (b)
    APNGPlayer preanim_player_;
    APNGPlayer shout_player_;
    APNGPlayer pair_player_;

    // ── Async load readiness flags ─────────────────────────────────────────
    bool scene_pending_  = false; // bg/desk still need decoding
    bool bg_ready_       = false;
    bool desk_ready_     = false;
    bool idle_ready_     = false;
    bool talk_ready_     = false;
    bool preanim_ready_  = false;
    bool pair_ready_     = false;
    bool shout_ready_    = false;
    uint32_t msg_age_ms_ = 0;     // ms since begin_message (load gate / give-up)
    bool assets_fallback_done_ = false;  // learned-format fallback fired for this line
    static constexpr uint32_t LOAD_GATE_MS   = 400;   // start the line quickly; sprite pops in when ready
    static constexpr uint32_t PROBE_FALLBACK_MS = 180; // re-fan-out all exts if learned-only didn't land (< gate)
    static constexpr uint32_t ASSET_GIVEUP_MS = 8000; // stop probing missing assets

    Phase phase_ = Phase::Idle;
    Phase pending_phase_ = Phase::Talking;

    // music async
    char want_music_[128] = {};
    bool     music_pending_ = false;
    uint32_t music_age_ms_  = 0;

    // typewriter
    int      tw_pos_ = 0;
    int      tw_max_ = 0;
    uint32_t tw_acc_ = 0;
    int      tw_since_blip_ = 0;
    static constexpr uint32_t TYPEWRITER_MS = 18;  // ms per char (snappier than AO default)
    static constexpr int      BLIP_EVERY    = 3;   // chars per blip (fewer SFX triggers)

    // phase timers
    uint32_t shout_acc_  = 0;
    static constexpr uint32_t SHOUT_MS = 1500;
    uint32_t realize_ms_ = 0;
    static constexpr uint32_t REALIZE_MS = 350;

    // screenshake
    int shake_frames_ = 0;
    int shake_x_ = 0, shake_y_ = 0;

    // panel scroll/selection
    int ooc_scroll_   = 0;
    int music_scroll_ = 0;
    int music_sel_    = 0;
    int evi_scroll_   = 0;
    int area_scroll_  = 0;
    int area_sel_     = 0;
    int ic_log_scroll_ = 0;   // entries skipped from the newest (wheel / drag scrollback)

    TouchDrag drag_;          // tap vs finger drag-scroll classifier (panels + IC log)

    // ── IC composer state (own character) ─────────────────────────────────
    CharDef own_char_;
    bool    own_loaded_  = false;
    int     ic_emote_sel_= 0;
    int     ic_emote_scroll_ = 0;  // top row of the emote grid
    bool    ic_buttons_dirty_ = true; // re-queue emote-button prefetch on open/move
    int     ic_color_    = 0;
    char    ic_pos_[16]  = "wit";
    // Sendable IC modifiers (the client already renders these when received; the
    // composer now lets you SEND them). Wired into MSParams in send_ic().
    int     ic_shout_    = 0;     // 0 none, 1 hold it, 2 objection, 3 take that, 4 custom
    bool    ic_flip_     = false; // mirror the sprite horizontally
    bool    ic_realize_  = false; // realization flash
    bool    ic_shake_    = false; // screenshake
    char    ic_text_[256]= {};

    // In-app on-screen keyboard (non-blocking) for IC/OOC text — replaces the
    // blocking swkbd so chat keeps flowing while you type. compose_mode_ records
    // what the open keyboard is composing so SUBMIT routes to the right sender.
    SoftKeyboard kb_;
    enum ComposeMode { CM_NONE, CM_IC, CM_OOC };
    ComposeMode compose_mode_ = CM_NONE;

    // IC composer emote preview: warm the selected emote's button thumbnail into
    // the texture cache without blocking the render loop (prefetch + peek, like
    // the character-select grid). Returns the cached texture or nullptr.
    void         prefetch_emote_buttons();          // queue visible/selected buttons
    SDL_Texture* emote_button_tex(int emote_idx, bool on) const; // peek-only (any ext)
    SDL_Texture* warm_button_tex(const char* char_lc, int idx, bool on); // peek, else upload a staged one
    void         prefetch_own_emote();              // warm own (a)/(b) sprite for the selected emote

    // Own char.ini is loaded ASYNCHRONOUSLY so joining never freezes the render
    // loop on a blocking HTTP fetch: on_enter queues the char.ini prefetch,
    // update() consumes it from cache once ready (or blocks once after a timeout).
    bool     own_pending_  = false;
    uint32_t own_load_age_ = 0;
};

} // namespace ao
