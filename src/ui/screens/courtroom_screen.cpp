#include "courtroom_screen.hpp"
#include "../../app.hpp"
#include "../../render/renderer.hpp"
#include "../../state/game_state.hpp"
#include "../../assets/theme_manager.hpp"
#include "../../render/text_renderer.hpp"
#include <SDL2/SDL.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>

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
    TextRenderer& txt = app_.text();

    // Chat background strip (from chatbox top to screen bottom)
    SDL_Rect chat_bg = {0, tl.chatbox.y, Renderer::WIDTH, Renderer::HEIGHT - tl.chatbox.y};
    r.fill_rect(chat_bg,    {10, 10, 20, 230});
    r.fill_rect(tl.chatbox, {15, 15, 30, 200});
    r.draw_rect(tl.chatbox, {60, 80, 120, 255});

    // Nameplate background + showname text
    const ICAnimState& ic = gs.ic_anim;
    r.fill_rect(tl.nameplate, {40, 60, 120, 255});
    const char* display_name = (ic.showname[0] != '\0') ? ic.showname : ic.char_name;
    if (display_name[0] != '\0') {
        int ty = tl.nameplate.y + (tl.nameplate.h - txt.line_h()) / 2;
        txt.draw(display_name, tl.nameplate.x + 6, ty, {255, 255, 255, 255});
    }

    // IC message text — typewriter-clipped, word-wrapped inside ic_text area
    if (typewriter_pos_ > 0) {
        char msg_buf[512];
        int len = typewriter_pos_ < 511 ? typewriter_pos_ : 511;
        std::memcpy(msg_buf, ic.message, len);
        msg_buf[len] = '\0';

        SDL_Color text_col = (ic.text_color >= 0 && ic.text_color < 12)
            ? TEXT_COLORS[ic.text_color] : TEXT_COLORS[0];
        txt.draw_wrapped(msg_buf,
                         tl.ic_text.x, tl.ic_text.y,
                         tl.ic_text.w, text_col);
    }

    // Panel toggle buttons
    auto draw_btn = [&](const SDL_Rect& rect, SDL_Color c, const char* label) {
        r.fill_rect(rect, c);
        r.draw_rect(rect, {100, 120, 180, 255});
        int tx = rect.x + (rect.w - txt.measure_w(label)) / 2;
        int ty = rect.y + (rect.h - txt.line_h()) / 2;
        txt.draw(label, tx, ty, {220, 230, 255, 255});
    };
    draw_btn(tl.btn_ooc,
        active_panel_==CourtroomPanel::OOC ? SDL_Color{80,120,200,255} : SDL_Color{30,30,60,255},
        "OOC");
    draw_btn(tl.btn_music,
        active_panel_==CourtroomPanel::Music ? SDL_Color{80,120,200,255} : SDL_Color{30,30,60,255},
        "Music");
    draw_btn(tl.btn_evidence,
        active_panel_==CourtroomPanel::Evidence ? SDL_Color{80,120,200,255} : SDL_Color{30,30,60,255},
        "Evi");
}

void CourtroomScreen::render_side_panel() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();
    const ThemeLayout& tl = app_.theme().layout();
    TextRenderer& txt = app_.text();

    r.fill_rect(tl.log, {18, 18, 35, 255});

    // Current music name
    if (gs.current_music[0] != '\0') {
        r.fill_rect(tl.music_name, {22, 22, 45, 220});
        int ty = tl.music_name.y + (tl.music_name.h - txt.line_h()) / 2;
        txt.draw(gs.current_music, tl.music_name.x + 4, ty, {160, 190, 255, 255});
    }

    // HP bars with DEF / PRO labels
    auto draw_hp = [&](SDL_Rect bar, int val, SDL_Color fill,
                       const char* label, SDL_Color label_col) {
        r.fill_rect(bar, {20, 20, 20, 255});
        if (val > 0) {
            SDL_Rect filled = {bar.x, bar.y, bar.w * val / 10, bar.h};
            r.fill_rect(filled, fill);
        }
        r.draw_rect(bar, {60, 60, 80, 255});
        txt.draw(label, bar.x, bar.y - txt.line_h(), label_col);
    };
    draw_hp(tl.hp_def, gs.hp_defense,     {60, 140, 220, 255}, "DEF", {140, 180, 255, 255});
    draw_hp(tl.hp_pro, gs.hp_prosecution, {220, 60,  60, 255}, "PRO", {255, 120, 120, 255});
}

void CourtroomScreen::render_active_panel() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();
    const ThemeLayout& tl = app_.theme().layout();
    TextRenderer& txt = app_.text();

    if (active_panel_ == CourtroomPanel::None) return;

    const int lh = txt.line_h() > 0 ? txt.line_h() : 20; // fallback if font missing

    if (active_panel_ == CourtroomPanel::OOC) {
        r.fill_rect(tl.panel_ooc, {10, 10, 20, 230});
        r.draw_rect(tl.panel_ooc, {60, 80, 140, 255});

        // Two text lines per entry: name header + message body
        const int row_h   = lh * 2 + 6;
        const int visible = (tl.panel_ooc.h - 8) / row_h;
        int start = gs.ooc_log.count > visible ? gs.ooc_log.count - visible : 0;
        start -= ooc_scroll_;
        if (start < 0) start = 0;

        for (int i = 0; i < visible && (start + i) < gs.ooc_log.count; ++i) {
            const ChatEntry& ce = gs.ooc_log.at(start + i);
            int ry = tl.panel_ooc.y + 4 + i * row_h;
            SDL_Rect row_bg = {tl.panel_ooc.x + 4, ry, tl.panel_ooc.w - 8, row_h - 2};
            r.fill_rect(row_bg, ce.server
                ? SDL_Color{30, 30, 50, 180} : SDL_Color{20, 20, 40, 180});

            // "[name]" in accent color, message below in white
            char header[80];
            std::snprintf(header, sizeof(header), "[%s]", ce.name);
            SDL_Color name_col = ce.server
                ? SDL_Color{180, 200, 255, 255} : SDL_Color{140, 200, 140, 255};
            txt.draw(header, tl.panel_ooc.x + 8, ry + 2, name_col);
            txt.draw_wrapped(ce.message, tl.panel_ooc.x + 8, ry + 2 + lh,
                             tl.panel_ooc.w - 16, {220, 220, 220, 255});
        }

    } else if (active_panel_ == CourtroomPanel::Music) {
        r.fill_rect(tl.panel_music, {10, 10, 20, 230});
        r.draw_rect(tl.panel_music, {60, 80, 140, 255});

        const int row_h   = lh + 10;
        const int visible = (tl.panel_music.h - 8) / row_h;
        int start = music_scroll_;
        if (start < 0) start = 0;

        for (int i = 0; i < visible && (start + i) < gs.music_count; ++i) {
            const char* track = gs.music_list[start + i];
            SDL_Rect row = {tl.panel_music.x + 4, tl.panel_music.y + 4 + i * row_h,
                            tl.panel_music.w - 8, row_h - 2};
            bool cur = std::strcmp(track, gs.current_music) == 0;
            r.fill_rect(row, cur ? SDL_Color{50,100,180,200} : SDL_Color{20,20,40,180});
            r.draw_rect(row, {50, 50, 80, 255});
            SDL_Color tc = cur ? SDL_Color{255,255,100,255} : SDL_Color{200,210,230,255};
            txt.draw(track, row.x + 6, row.y + (row.h - lh) / 2, tc);
        }

    } else if (active_panel_ == CourtroomPanel::Evidence) {
        r.fill_rect(tl.panel_evidence, {10, 10, 20, 230});
        r.draw_rect(tl.panel_evidence, {60, 80, 140, 255});

        static constexpr int COLS = 4;
        static constexpr int CELL = 100;
        const int cell_stride = CELL + 8;
        const int name_h = lh + 2;

        for (int i = 0; i < gs.evidence_count && i < 20; ++i) {
            int col  = i % COLS;
            int rowi = (i / COLS) - evi_scroll_;
            if (rowi < 0) continue;
            int cx = tl.panel_evidence.x + 8 + col * cell_stride;
            int cy = tl.panel_evidence.y + 8 + rowi * (cell_stride + name_h);
            SDL_Rect cell = {cx, cy, CELL, CELL};
            r.fill_rect(cell, {30, 40, 60, 220});
            r.draw_rect(cell, {70, 90, 130, 255});
            // Evidence name below cell image
            txt.draw(gs.evidence[i].name, cx, cy + CELL + 2, {200, 215, 235, 255});
        }
    }
}

} // namespace ao
