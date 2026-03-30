#include "char_select_screen.hpp"
#include "courtroom_screen.hpp"
#include "../../app.hpp"
#include "../../render/renderer.hpp"
#include "../../state/game_state.hpp"
#include "../../input/input_manager.hpp"
#include "../../net/packet_queue.hpp"
#include "../../protocol/commands.hpp"
#include <SDL2/SDL.h>

namespace ao {

CharSelectScreen::CharSelectScreen(App& app) : Screen(app) {}

void CharSelectScreen::on_enter() { selected_ = 0; scroll_ = 0; }

void CharSelectScreen::handle_event(const SDL_Event& e) {
    GameState& gs = app_.state();
    if (gs.char_count == 0) return;

    // Use keyboard for emulator convenience
    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_RIGHT: selected_ = (selected_ + 1) % gs.char_count; break;
            case SDLK_LEFT:  selected_ = (selected_ - 1 + gs.char_count) % gs.char_count; break;
            case SDLK_DOWN:  selected_ = (selected_ + COLS) % gs.char_count; break;
            case SDLK_UP:    selected_ = (selected_ - COLS + gs.char_count) % gs.char_count; break;
            case SDLK_RETURN:
                if (!gs.char_taken[selected_]) {
                    // Send CC packet
                    // TODO Phase 3: access OutQueue via App
                    app_.push_screen(new CourtroomScreen(app_));
                }
                break;
            default: break;
        }
    }
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: selected_=(selected_+1)%gs.char_count; break;
            case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  selected_=(selected_-1+gs.char_count)%gs.char_count; break;
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  selected_=(selected_+COLS)%gs.char_count; break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:    selected_=(selected_-COLS+gs.char_count)%gs.char_count; break;
            case SDL_CONTROLLER_BUTTON_A:
                if (!gs.char_taken[selected_])
                    app_.push_screen(new CourtroomScreen(app_));
                break;
            default: break;
        }
    }
}

void CharSelectScreen::update(uint32_t /*dt*/) {
    GameState& gs = app_.state();
    if (gs.char_count == 0) return;
    // Keep scroll in sync with selected
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + PAGE) scroll_ = selected_ - PAGE + 1;
    if (scroll_ < 0) scroll_ = 0;
}

void CharSelectScreen::render() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();

    r.fill_rect({0, 0, Renderer::WIDTH, Renderer::HEIGHT}, {10, 10, 25, 255});
    // Title bar
    r.fill_rect({0, 0, Renderer::WIDTH, 60}, {25, 40, 80, 255});

    // Character grid (placeholder coloured squares until Phase 4 textures)
    static constexpr int CELL_W = 140;
    static constexpr int CELL_H = 140;
    static constexpr int START_X = 40;
    static constexpr int START_Y = 80;

    for (int row = 0; row < ROWS; ++row) {
        for (int col = 0; col < COLS; ++col) {
            int idx = scroll_ + row * COLS + col;
            if (idx >= gs.char_count) break;

            int x = START_X + col * (CELL_W + 8);
            int y = START_Y + row * (CELL_H + 8);
            SDL_Rect cell = {x, y, CELL_W, CELL_H};

            SDL_Color bg = gs.char_taken[idx]
                ? SDL_Color{40, 20, 20, 255}
                : (idx == selected_
                    ? SDL_Color{60, 100, 180, 255}
                    : SDL_Color{30, 30, 55, 255});
            r.fill_rect(cell, bg);
            r.draw_rect(cell, {80, 80, 120, 255});
        }
    }
}

} // namespace ao
