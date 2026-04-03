#include "app.hpp"
#include "render/renderer.hpp"
#include "state/game_state.hpp"
#include "ui/screen.hpp"
#include "ui/screens/char_select_screen.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <cstdio>
#include <cstring>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace ao {

static constexpr uint32_t TARGET_FPS    = 60;
static constexpr uint32_t FRAME_MS      = 1000 / TARGET_FPS;

App::App()  = default;
App::~App() {
    // Pop and delete all screens
    while (screen_count_ > 0) {
        screen_stack_[--screen_count_]->on_exit();
        delete screen_stack_[screen_count_];
        screen_stack_[screen_count_] = nullptr;
    }

    // disconnect() sets both to nullptr after deleting
    disconnect();

    delete game_state_;
    delete renderer_;

    Mix_CloseAudio();
    Mix_Quit();
    TTF_Quit();
    IMG_Quit();

    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();

#ifdef __SWITCH__
    socketExit();
    romfsExit();
#endif
}

bool App::init() {
#ifdef __SWITCH__
    // Mount the RomFS bundle
    if (R_FAILED(romfsInit())) {
        std::fprintf(stderr, "romfsInit failed\n");
        return false;
    }
    // Initialize BSD socket layer (required before any network use, incl. mbedtls)
    if (R_FAILED(socketInitializeDefault())) {
        std::fprintf(stderr, "socketInitializeDefault failed\n");
        return false;
    }
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    input_manager_.open_controllers();

    if (TTF_Init() != 0) {
        std::fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        return false;
    }

    if (!(IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & (IMG_INIT_PNG | IMG_INIT_JPG))) {
        std::fprintf(stderr, "IMG_Init: %s\n", IMG_GetError());
        return false;
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
        std::fprintf(stderr, "Mix_OpenAudio: %s\n", Mix_GetError());
        return false;
    }
    Mix_Init(MIX_INIT_OGG | MIX_INIT_OPUS);
    Mix_AllocateChannels(16);

    window_ = SDL_CreateWindow(
        "Ferris-AO",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        Renderer::WIDTH, Renderer::HEIGHT,
        SDL_WINDOW_FULLSCREEN
    );
    if (!window_) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }

    renderer_ = new Renderer();
    if (!renderer_->init(window_)) return false;

    game_state_ = new GameState();

    // Load theme layout from base pack (misc/default/courtroom_design.ini) if present.
    // Falls back to built-in Layout:: constants silently when files are absent.
    theme_manager_.load("default");

    // Init text renderer — non-fatal if font is missing (text renders as nothing).
    text_renderer_.init(renderer_->raw(), "fonts/noto_sans.ttf", 18);

    running_ = true;
    return true;
}

void App::run() {
    uint32_t last_ticks = SDL_GetTicks();

    while (running_) {
        const uint32_t now = SDL_GetTicks();
        const uint32_t dt  = now - last_ticks;
        last_ticks = now;

        process_events();
        if (!running_) break;

        update(dt);
        render();

        // Cap to ~60 fps if vsync is off
        const uint32_t elapsed = SDL_GetTicks() - now;
        if (elapsed < FRAME_MS) SDL_Delay(FRAME_MS - elapsed);
    }
}

void App::push_screen(Screen* s) {
    if (screen_count_ >= SCREEN_STACK_MAX) {
        std::fprintf(stderr, "Screen stack overflow\n");
        delete s;
        return;
    }
    screen_stack_[screen_count_++] = s;
    s->on_enter();
}

void App::pop_screen() {
    if (screen_count_ <= 0) return;
    Screen* s = screen_stack_[--screen_count_];
    s->on_exit();
    delete s;
    screen_stack_[screen_count_] = nullptr;

    // Resume the screen below
    if (screen_count_ > 0)
        screen_stack_[screen_count_ - 1]->on_enter();
}

void App::process_events() {
    input_manager_.begin_frame();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            running_ = false;
            return;
        }
        // Switch: appletMainLoop equivalent — check HOME button
#ifdef __SWITCH__
        if (!appletMainLoop()) {
            running_ = false;
            return;
        }
#endif
        input_manager_.handle_event(e);
        if (screen_count_ > 0)
            screen_stack_[screen_count_ - 1]->handle_event(e);
    }
}

void App::update(uint32_t dt_ms) {
    // Process incoming network packets on the main thread
    if (ao_client_) {
        ao_client_->process(in_queue_);
    }

    // Detect transition to in_lobby (DONE packet received) → push CharSelectScreen
    if (game_state_ && game_state_->in_lobby && !was_in_lobby_) {
        was_in_lobby_ = true;
        push_screen(new CharSelectScreen(*this));
    }

    // Detect unexpected disconnection: network thread stopped while we were
    // connected (net_thread_ exists, not connected, but was_in_lobby_ is true)
    // and the AOClient went back to Idle.  Pop back to the connect screen.
    if (net_thread_ && !net_thread_->is_connected() && was_in_lobby_) {
        if (ao_client_ &&
            ao_client_->handshake_state() == HandshakeState::Idle) {
            was_in_lobby_ = false;
            game_state_->in_lobby  = false;
            game_state_->connected = false;
            // Pop screens until only ConnectScreen remains (screen_count_ == 1)
            while (screen_count_ > 1)
                pop_screen();
        }
    }

    // Detect pre-lobby connection failure: connection attempt started but the
    // network thread died before the handshake completed.
    // (was_in_lobby_ is false here because DONE was never received.)
    if (net_thread_ && !net_thread_->is_connected() && !was_in_lobby_) {
        if (ao_client_ &&
            ao_client_->handshake_state() == HandshakeState::Idle) {
            const char* err = net_thread_->error();
            std::snprintf(pending_error_, sizeof(pending_error_),
                "Connection failed: %s",
                err[0] ? err : "server closed connection");
            std::fprintf(stderr, "App: %s\n", pending_error_);
            disconnect();
        }
    }

    if (screen_count_ > 0)
        screen_stack_[screen_count_ - 1]->update(dt_ms);
}

void App::render() {
    renderer_->clear({15, 15, 20, 255});

    // Render all screens bottom-up (lower screens may show through overlays)
    for (int i = 0; i < screen_count_; ++i)
        screen_stack_[i]->render();

    renderer_->present();
}

// ── Networking ─────────────────────────────────────────────────────────────────

bool App::connect(const char* host, uint16_t port, ConnMode mode) {
    // Tear down any existing connection
    disconnect();

    // Reset game state for a fresh connection
    *game_state_  = GameState();
    was_in_lobby_ = false;

    // Create fresh network objects
    net_thread_ = new NetworkThread(in_queue_, out_queue_);
    ao_client_  = new AOClient(out_queue_, *game_state_, username_);

    if (!net_thread_->connect(host, port, mode)) {
        std::fprintf(stderr, "App::connect: NetworkThread::connect failed\n");
        delete net_thread_; net_thread_ = nullptr;
        delete ao_client_;  ao_client_  = nullptr;
        return false;
    }

    ao_client_->on_connected();
    return true;
}

void App::disconnect() {
    if (net_thread_) {
        net_thread_->disconnect();
        delete net_thread_;
        net_thread_ = nullptr;
    }
    if (ao_client_) {
        ao_client_->on_disconnected();
        delete ao_client_;
        ao_client_ = nullptr;
    }
    was_in_lobby_ = false;
    if (game_state_) {
        game_state_->connected = false;
        game_state_->in_lobby  = false;
    }
}

void App::send_packet(const char* buf, int len) {
    OutPacket pkt;
    int copy = len < (int)sizeof(pkt.data) - 1 ? len : (int)sizeof(pkt.data) - 1;
    std::memcpy(pkt.data, buf, copy);
    pkt.data[copy] = '\0';
    pkt.len = copy;
    out_queue_.push(pkt);
}

void App::set_username(const char* u) {
    std::strncpy(username_, u, sizeof(username_) - 1);
    username_[sizeof(username_) - 1] = '\0';
}

} // namespace ao
