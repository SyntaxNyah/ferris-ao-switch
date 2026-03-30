#include "courtroom_screen.hpp"
#include "../../app.hpp"
#include "../../render/renderer.hpp"
#include "../../state/game_state.hpp"
#include "../../assets/theme_manager.hpp"
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdlib>

namespace ao {

static const SDL_Color TEXT_COLORS[12] = {
    {255,255,255,255}, // 0 white
    {0,  255,0,  255}, // 1 green
    {255,0,  0,  255}, // 2 red
    {255,165,0,  255}, // 3 orange
    {0,  0,  255,255}, // 4 blue
    {255,255,0,  255}, // 5 yellow
    {255,105,180,255}, // 6 pink
    {0,  255,255,255}, // 7 cyan
    {128,0,  128,255}, // 8 purple
    {139,69, 19, 255}, // 9 brown
    {128,128,128,255}, // 10 grey
    {0,  128,0,  255}, // 11 dark green
};

CourtroomScreen::CourtroomScreen(App& app) : Screen(app) {}

void CourtroomScreen::on_enter() {
    active_panel_   = CourtroomPanel::None;
    typewriter_pos_ = 0;
    typewriter_max_ = 0;
}

void CourtroomScreen::handle_event(const SDL_Event& e) {
    GameState& gs = app_.state();

    auto handle_key = [&](int sym) {
        switch (sym) {
            case SDLK_z: // ZL — OOC
                active_panel_ = (active_panel_ == CourtroomPanel::OOC)
                    ? CourtroomPanel::None : CourtroomPanel::OOC;
                break;
            case SDLK_c: // ZR — Music
                active_panel_ = (active_panel_ == CourtroomPanel::Music)
                    ? CourtroomPanel::None : CourtroomPanel::Music;
                break;
            case SDLK_y: // Y — Evidence
                active_panel_ = (active_panel_ == CourtroomPanel::Evidence)
                    ? CourtroomPanel::None : CourtroomPanel::Evidence;
                break;
            case SDLK_x: // X — IC input
                active_panel_ = (active_panel_ == CourtroomPanel::ICInput)
                    ? CourtroomPanel::None : CourtroomPanel::ICInput;
                break;
            case SDLK_ESCAPE: // + — disconnect
                app_.pop_screen();
                break;
            case SDLK_RETURN: // A — skip typewriter
                typewriter_pos_ = typewriter_max_;
                break;
            case SDLK_PAGEUP:
                if (active_panel_ == CourtroomPanel::OOC) ooc_scroll_--;
                break;
            case SDLK_PAGEDOWN:
                if (active_panel_ == CourtroomPanel::OOC) ooc_scroll_++;
                break;
            default: break;
        }
        (void)gs;
    };

    if (e.type == SDL_KEYDOWN) handle_key(e.key.keysym.sym);

    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_LEFTSTICK:  // ZL
                active_panel_ = (active_panel_==CourtroomPanel::OOC)
                    ? CourtroomPanel::None : CourtroomPanel::OOC; break;
            case SDL_CONTROLLER_BUTTON_RIGHTSTICK: // ZR
                active_panel_ = (active_panel_==CourtroomPanel::Music)
                    ? CourtroomPanel::None : CourtroomPanel::Music; break;
            case SDL_CONTROLLER_BUTTON_Y:
                active_panel_ = (active_panel_==CourtroomPanel::Evidence)
                    ? CourtroomPanel::None : CourtroomPanel::Evidence; break;
            case SDL_CONTROLLER_BUTTON_X:
                active_panel_ = (active_panel_==CourtroomPanel::ICInput)
                    ? CourtroomPanel::None : CourtroomPanel::ICInput; break;
            case SDL_CONTROLLER_BUTTON_START:
                app_.pop_screen(); break;
            case SDL_CONTROLLER_BUTTON_A:
                typewriter_pos_ = typewriter_max_; break;
            default: break;
        }
    }
}

void CourtroomScreen::update(uint32_t dt_ms) {
    GameState& gs = app_.state();

    // Kick off typewriter when a new IC message arrives
    if (gs.ic_anim.pending) {
        gs.ic_anim.pending = false;
        typewriter_pos_    = 0;
        typewriter_max_    = (int)std::strlen(gs.ic_anim.message);
        typewriter_acc_    = 0;
        if (gs.ic_anim.screenshake) shake_frames_ = 12;
    }

    // Advance typewriter
    if (typewriter_pos_ < typewriter_max_) {
        typewriter_acc_ += dt_ms;
        while (typewriter_acc_ >= TYPEWRITER_MS && typewriter_pos_ < typewriter_max_) {
            typewriter_acc_ -= TYPEWRITER_MS;
            ++typewriter_pos_;
        }
    }

    // Screenshake
    if (shake_frames_ > 0) {
        --shake_frames_;
        shake_x_ = (rand() % 9) - 4;
        shake_y_ = (rand() % 9) - 4;
    } else {
        shake_x_ = shake_y_ = 0;
    }
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
    // Background fill (will be replaced by bg texture in Phase 9)
    SDL_Rect vp = {tl.viewport.x + shake_x_,
                   tl.viewport.y + shake_y_,
                   tl.viewport.w, tl.viewport.h};
    r.fill_rect(vp, {30, 30, 50, 255});

    // Character sprite placeholder rectangle
    SDL_Rect sprite = {vp.x + 200, vp.y + 80, 200, 360};
    r.fill_rect(sprite, {60, 80, 120, 255});
    r.draw_rect(sprite, {120, 150, 200, 255});
}

void CourtroomScreen::render_chat_area() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();
    const ThemeLayout& tl = app_.theme().layout();

    // Chat background strip (from chatbox top to screen bottom)
    SDL_Rect chat_bg = {0, tl.chatbox.y, Renderer::WIDTH, Renderer::HEIGHT - tl.chatbox.y};
    r.fill_rect(chat_bg,  {10, 10, 20, 230});
    r.fill_rect(tl.chatbox, {15, 15, 30, 200});
    r.draw_rect(tl.chatbox, {60, 80, 120, 255});

    // Nameplate
    const ICAnimState& ic = gs.ic_anim;
    r.fill_rect(tl.nameplate, {40, 60, 120, 255});

    // Message text (clipped to typewriter progress)
    // Full text rendering requires TextRenderer (Phase 5+).
    // For now we just show the chatbox is wired up by drawing a progress bar.
    if (typewriter_max_ > 0) {
        int bar_w = tl.chatbox.w * typewriter_pos_ / typewriter_max_;
        SDL_Color text_col = (ic.text_color >= 0 && ic.text_color < 12)
            ? TEXT_COLORS[ic.text_color] : TEXT_COLORS[0];
        text_col.a = 80;
        r.fill_rect({tl.chatbox.x, tl.chatbox.y + tl.chatbox.h - 8, bar_w, 8}, text_col);
    }

    // Panel toggle buttons
    auto draw_btn = [&](const SDL_Rect& rect, SDL_Color c) {
        r.fill_rect(rect, c);
        r.draw_rect(rect, {100, 120, 180, 255});
    };
    draw_btn(tl.btn_ooc,
        active_panel_==CourtroomPanel::OOC ? SDL_Color{80,120,200,255} : SDL_Color{30,30,60,255});
    draw_btn(tl.btn_music,
        active_panel_==CourtroomPanel::Music ? SDL_Color{80,120,200,255} : SDL_Color{30,30,60,255});
    draw_btn(tl.btn_evidence,
        active_panel_==CourtroomPanel::Evidence ? SDL_Color{80,120,200,255} : SDL_Color{30,30,60,255});
}

void CourtroomScreen::render_side_panel() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();
    const ThemeLayout& tl = app_.theme().layout();

    r.fill_rect(tl.log, {18, 18, 35, 255});

    // HP bars
    auto draw_hp = [&](SDL_Rect bar, int val, SDL_Color fill) {
        r.fill_rect(bar, {20, 20, 20, 255});
        if (val > 0) {
            SDL_Rect filled = {bar.x, bar.y, bar.w * val / 10, bar.h};
            r.fill_rect(filled, fill);
        }
        r.draw_rect(bar, {60, 60, 80, 255});
    };
    draw_hp(tl.hp_def, gs.hp_defense,     {60, 140, 220, 255});
    draw_hp(tl.hp_pro, gs.hp_prosecution, {220, 60, 60,  255});
}

void CourtroomScreen::render_active_panel() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();
    const ThemeLayout& tl = app_.theme().layout();

    if (active_panel_ == CourtroomPanel::None) return;

    if (active_panel_ == CourtroomPanel::OOC) {
        r.fill_rect(tl.panel_ooc, {10, 10, 20, 230});
        r.draw_rect(tl.panel_ooc, {60, 80, 140, 255});
        static constexpr int ROWS = 18;
        int start = gs.ooc_log.count > ROWS ? gs.ooc_log.count - ROWS : 0;
        start = start - ooc_scroll_;
        if (start < 0) start = 0;
        for (int i = 0; i < ROWS && (start + i) < gs.ooc_log.count; ++i) {
            const ChatEntry& ce = gs.ooc_log.at(start + i);
            SDL_Color row_bg = ce.server
                ? SDL_Color{30, 30, 50, 180}
                : SDL_Color{20, 20, 40, 180};
            r.fill_rect({tl.panel_ooc.x + 4, tl.panel_ooc.y + 4 + i * 38,
                         tl.panel_ooc.w - 8, 36}, row_bg);
            (void)ce;
        }
    } else if (active_panel_ == CourtroomPanel::Music) {
        r.fill_rect(tl.panel_music, {10, 10, 20, 230});
        r.draw_rect(tl.panel_music, {60, 80, 140, 255});
        static constexpr int ROWS = 16;
        int start = music_scroll_;
        if (start < 0) start = 0;
        for (int i = 0; i < ROWS && (start + i) < gs.music_count; ++i) {
            SDL_Rect row = {tl.panel_music.x + 4, tl.panel_music.y + 4 + i * 44,
                            tl.panel_music.w - 8, 40};
            bool cur = std::strcmp(gs.music_list[start+i], gs.current_music) == 0;
            r.fill_rect(row, cur ? SDL_Color{50,100,180,200} : SDL_Color{20,20,40,180});
            r.draw_rect(row, {50,50,80,255});
        }
    } else if (active_panel_ == CourtroomPanel::Evidence) {
        r.fill_rect(tl.panel_evidence, {10, 10, 20, 230});
        r.draw_rect(tl.panel_evidence, {60, 80, 140, 255});
        static constexpr int COLS = 4;
        static constexpr int CELL = 120;
        for (int i = 0; i < gs.evidence_count && i < 20; ++i) {
            int col = i % COLS;
            int row_i = i / COLS;
            SDL_Rect cell = {tl.panel_evidence.x + 10 + col * (CELL+6),
                             tl.panel_evidence.y + 10 + row_i * (CELL+6), CELL, CELL};
            r.fill_rect(cell, {30, 40, 60, 220});
            r.draw_rect(cell, {70, 90, 130, 255});
        }
    }
}

} // namespace ao
