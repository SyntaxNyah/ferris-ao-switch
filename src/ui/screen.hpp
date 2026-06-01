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

    // True if this screen fully covers the framebuffer. App::render() draws
    // only from the topmost opaque screen upward, so an opaque screen hides
    // everything beneath it (e.g. the courtroom never shows the character
    // grid behind it). Transparent overlays should override to return false.
    virtual bool opaque() const { return true; }

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
