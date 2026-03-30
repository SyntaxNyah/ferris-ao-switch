#include "connect_screen.hpp"
#include "../../app.hpp"
#include "../../render/renderer.hpp"
#include <SDL2/SDL.h>
#include <cstdio>

namespace ao {

ConnectScreen::ConnectScreen(App& app) : Screen(app) {}

void ConnectScreen::on_enter() {
    std::snprintf(status_msg_, sizeof(status_msg_),
        "D-pad to select field  |  A to edit  |  ZR to connect");
}

void ConnectScreen::handle_event(const SDL_Event& e) {
    if (e.type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e.cbutton.button) {
            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                selected_field_ = (selected_field_ + 1) % 4;
                break;
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                selected_field_ = (selected_field_ + 3) % 4;
                break;
            case SDL_CONTROLLER_BUTTON_A:
                // TODO Phase 5: open system keyboard for the selected field
                break;
            case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                // TODO Phase 2: kick off network connection
                std::snprintf(status_msg_, sizeof(status_msg_),
                    "Connecting to %s:%s ...", host_, port_str_);
                connecting_ = true;
                break;
            default:
                break;
        }
    }
    // Keyboard fallback (for emulator / desktop testing)
    if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
            case SDLK_DOWN:
                selected_field_ = (selected_field_ + 1) % 4; break;
            case SDLK_UP:
                selected_field_ = (selected_field_ + 3) % 4; break;
            case SDLK_RETURN:
                std::snprintf(status_msg_, sizeof(status_msg_),
                    "Connecting to %s:%s ...", host_, port_str_);
                connecting_ = true;
                break;
            default: break;
        }
    }
}

void ConnectScreen::update(uint32_t /*dt_ms*/) {
    // TODO Phase 2: poll network thread for connection result
}

void ConnectScreen::render() {
    Renderer& r = app_.renderer();

    // Background fill
    SDL_Rect bg = {0, 0, Renderer::WIDTH, Renderer::HEIGHT};
    r.fill_rect(bg, {15, 15, 30, 255});

    // Title
    // TODO Phase 5: draw actual text via TextRenderer
    // For now just draw placeholder rectangles so the screen is non-blank

    // Title bar area
    r.fill_rect({0, 0, Renderer::WIDTH, 80}, {30, 30, 60, 255});

    // Field rows
    const char* labels[] = {"Host:", "Port:", "Username:", "[ Connect ]"};
    const char* values[] = {host_, port_str_, username_, ""};

    for (int i = 0; i < 4; ++i) {
        SDL_Rect row = {200, 130 + i * 90, 880, 70};
        SDL_Color bg_col = (i == selected_field_)
            ? SDL_Color{60, 80, 140, 255}
            : SDL_Color{30, 30, 50, 255};
        r.fill_rect(row, bg_col);
        r.draw_rect(row, {100, 120, 200, 255});

        // Label indicator (small rect on the left)
        r.fill_rect({200, 130 + i * 90, 140, 70}, {50, 50, 80, 255});
        (void)labels[i];
        (void)values[i];
    }

    // Status message area
    r.fill_rect({0, 660, Renderer::WIDTH, 60}, {20, 20, 40, 255});
}

} // namespace ao
