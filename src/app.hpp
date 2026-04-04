#pragma once
#include <SDL2/SDL.h>
#include <array>
#include <cstdint>
#include "assets/theme_manager.hpp"
#include "assets/texture_cache.hpp"
#include "assets/asset_stream.hpp"
#include "render/text_renderer.hpp"
#include "input/input_manager.hpp"
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
    TextureCache& tex_cache()    { return tex_cache_; }
    AssetStream&  asset_stream() { return asset_stream_; }
    bool          running()      const { return running_; }
    void          quit()         { running_ = false; }

    // Networking
    bool connect(const char* host, uint16_t port, ConnMode mode);
    void disconnect();
    void send_packet(const char* buf, int len);  // push raw packet to outgoing queue

    const char* username() const { return username_; }
    void        set_username(const char* u);

    NetworkThread* net_thread() { return net_thread_; }
    AOClient*      ao_client()  { return ao_client_; }

    // Set by App::update() when a connection attempt fails before reaching the lobby.
    // ConnectScreen reads and clears this to show an error message.
    const char* pending_error() const { return pending_error_[0] ? pending_error_ : nullptr; }
    void        clear_pending_error() { pending_error_[0] = '\0'; }

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
    InputManager  input_manager_;
    TextureCache  tex_cache_;
    AssetStream   asset_stream_;

    // Networking
    InQueue        in_queue_;
    OutQueue       out_queue_;
    NetworkThread* net_thread_    = nullptr;
    AOClient*      ao_client_     = nullptr;
    char           username_[64]  = "Switch";
    bool           was_in_lobby_  = false;  // edge detection for CharSelectScreen push
    char           pending_error_[256] = {};

    Screen* screen_stack_[SCREEN_STACK_MAX] = {};
    int     screen_count_ = 0;
};

} // namespace ao
