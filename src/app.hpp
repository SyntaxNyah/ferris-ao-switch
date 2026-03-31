#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <cstdint>
#include "assets/theme_manager.hpp"
#include "render/text_renderer.hpp"
#include "net/network_thread.hpp"
#include "net/packet_queue.hpp"
#include "protocol/ao_client.hpp"

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
    Renderer&     renderer()     { return *renderer_; }
    GameState&    state()        { return *game_state_; }
    ThemeManager& theme()        { return theme_manager_; }
    TextRenderer& text()         { return text_renderer_; }
    bool          running()      const { return running_; }
    void          quit()         { running_ = false; }

    // Networking
    bool connect(const char* host, uint16_t port, ConnMode mode);
    void disconnect();

    const char* username() const { return username_; }
    void        set_username(const char* u);

    NetworkThread* net_thread() { return net_thread_; }
    AOClient*      ao_client()  { return ao_client_; }

private:
    void process_events();
    void update(uint32_t dt_ms);
    void render();

    bool        running_  = false;
    SDL_Window* window_   = nullptr;

    Renderer*     renderer_      = nullptr;
    GameState*    game_state_    = nullptr;
    ThemeManager  theme_manager_;
    TextRenderer  text_renderer_;

    // Networking
    InQueue        in_queue_;
    OutQueue       out_queue_;
    NetworkThread* net_thread_    = nullptr;
    AOClient*      ao_client_     = nullptr;
    char           username_[64]  = "Switch";
    bool           was_in_lobby_  = false;  // edge detection for CharSelectScreen push

    Screen* screen_stack_[SCREEN_STACK_MAX] = {};
    int     screen_count_ = 0;
};

} // namespace ao
