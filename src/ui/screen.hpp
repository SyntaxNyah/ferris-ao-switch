#pragma once
#include <SDL2/SDL.h>
#include <cstdint>

namespace ao {

class App;

class Screen {
public:
    explicit Screen(App& app) : app_(app) {}
    virtual ~Screen() = default;

    // Called when this screen becomes the top of the stack
    virtual void on_enter() {}
    // Called when it's about to be popped
    virtual void on_exit() {}

    // SDL event (keyboard, gamepad, etc.)
    virtual void handle_event(const SDL_Event& e) { (void)e; }

    // dt_ms = milliseconds since last frame
    virtual void update(uint32_t dt_ms) { (void)dt_ms; }

    // Draw everything for this screen
    virtual void render() {}

protected:
    App& app_;
};

} // namespace ao
