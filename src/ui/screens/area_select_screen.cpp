#include "area_select_screen.hpp"
#include "courtroom_screen.hpp"
#include "../../app.hpp"
#include "../../render/renderer.hpp"
#include "../../state/game_state.hpp"
#include <SDL2/SDL.h>

namespace ao {

AreaSelectScreen::AreaSelectScreen(App& app) : Screen(app) {}
void AreaSelectScreen::on_enter() { selected_ = 0; scroll_ = 0; }

void AreaSelectScreen::handle_event(const SDL_Event& e) {
    GameState& gs = app_.state();
    if (gs.area_count == 0) return;

    auto move = [&](int delta) {
        selected_ = (selected_ + delta + gs.area_count) % gs.area_count;
        if (selected_ < scroll_) scroll_ = selected_;
        if (selected_ >= scroll_ + VISIBLE) scroll_ = selected_ - VISIBLE + 1;
    };

    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_DOWN:   move(+1); break;
            case SDLK_UP:     move(-1); break;
            case SDLK_RETURN: app_.push_screen(new CourtroomScreen(app_)); break;
            default: break;
        }
    }
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN: move(+1); break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:   move(-1); break;
            case SDL_CONTROLLER_BUTTON_A: app_.push_screen(new CourtroomScreen(app_)); break;
            default: break;
        }
    }
}

void AreaSelectScreen::render() {
    Renderer& r = app_.renderer();
    GameState& gs = app_.state();

    r.fill_rect({0, 0, Renderer::WIDTH, Renderer::HEIGHT}, {10, 10, 25, 255});
    r.fill_rect({0, 0, Renderer::WIDTH, 60}, {25, 40, 80, 255});

    for (int i = 0; i < VISIBLE; ++i) {
        int idx = scroll_ + i;
        if (idx >= gs.area_count) break;
        SDL_Rect row = {40, 70 + i * 60, Renderer::WIDTH - 80, 52};
        SDL_Color bg = (idx == selected_)
            ? SDL_Color{60, 100, 180, 255}
            : SDL_Color{25, 25, 45, 255};
        r.fill_rect(row, bg);
        r.draw_rect(row, {60, 60, 100, 255});

        // Player count badge
        r.fill_rect({Renderer::WIDTH - 120, 70 + i * 60, 80, 52},
                    {40, 60, 100, 255});
    }
}

} // namespace ao
