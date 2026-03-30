#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <cstdint>

namespace ao {

class Screen;
class Renderer;
struct GameState;

// Maximum screens on the stack (courtroom + up to 3 overlays)
static constexpr int SCREEN_STACK_MAX = 4;

class App {
public:
    App();
    ~App();

    // Returns false if initialization failed
    bool init();

    // Runs the 60 Hz game loop until quit
    void run();

    // Push/pop screens; App owns the pointers
    void push_screen(Screen* s);
    void pop_screen();

    // Access shared systems
    Renderer&   renderer()   { return *renderer_; }
    GameState&  state()      { return *game_state_; }
    bool        running()    const { return running_; }
    void        quit()       { running_ = false; }

private:
    void process_events();
    void update(uint32_t dt_ms);
    void render();

    bool        running_  = false;
    SDL_Window* window_   = nullptr;

    Renderer*   renderer_    = nullptr;
    GameState*  game_state_  = nullptr;

    Screen* screen_stack_[SCREEN_STACK_MAX] = {};
    int     screen_count_ = 0;
};

} // namespace ao
