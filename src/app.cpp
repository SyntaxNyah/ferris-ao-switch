#include "app.hpp"
#include "render/renderer.hpp"
#include "state/game_state.hpp"
#include "ui/screen.hpp"
#include "ui/screens/char_select_screen.hpp"
#include "assets/asset_manager.hpp"
#include "assets/extensions_config.hpp"
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

    asset_stream_.start();

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
        // Apply the fallback asset URL only if the server never sent ASS
        if (!AssetManager::has_asset_url() && fallback_asset_url_[0] != '\0') {
            AssetManager::set_asset_url(fallback_asset_url_);
            std::fprintf(stderr, "[app] No ASS received — using fallback URL: %s\n",
                fallback_asset_url_);
        }
        ExtensionsConfig::fetch_and_apply();
        // If SC never populated the character list (Akashi servers), fetch
        // characters.json from the asset base — a JSON array of name strings,
        // e.g. ["Phoenix","Maya",...].  Same format webAO uses.
        if (game_state_->char_count == 0 && AssetManager::has_asset_url()) {
            int size = 0;
            uint8_t* data = AssetManager::fetch_bytes("characters.json", &size);
            if (data) {
                std::fprintf(stderr, "[app] Loading character list from characters.json\n");
                int count = 0;
                // Parse JSON array of strings: scan for each "value" between [ and ]
                const char* p = reinterpret_cast<const char*>(data);
                const char* end = p + size;
                // skip to '['
                while (p < end && *p != '[') ++p;
                while (p < end && count < GameState::MAX_CHARS) {
                    // find next '"'
                    while (p < end && *p != '"' && *p != ']') ++p;
                    if (p >= end || *p == ']') break;
                    ++p; // skip opening '"'
                    const char* name_start = p;
                    while (p < end && *p != '"') ++p;
                    int len = (int)(p - name_start);
                    if (len > 0 && len < (int)sizeof(game_state_->characters[0].name)) {
                        std::memcpy(game_state_->characters[count].name, name_start, len);
                        game_state_->characters[count].name[len] = '\0';
                        ++count;
                    }
                    if (p < end) ++p; // skip closing '"'
                }
                game_state_->char_count = count;
                SDL_free(data);
                std::fprintf(stderr, "[app] characters.json: loaded %d characters\n", count);
            } else {
                std::fprintf(stderr, "[app] characters.json not found\n");
            }
        }
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
    *game_state_       = GameState();
    was_in_lobby_      = false;
    fallback_asset_url_[0] = '\0';

    // For WS/WSS servers, prepare a fallback asset URL derived from the host.
    // We do NOT set it immediately — instead we wait until in_lobby is true
    // (handshake complete) and only apply it if the server never sent an ASS
    // packet with the real URL.
    //
    // Akashi servers follow the pattern: WebSocket on ao.<domain>, webAO (and
    // assets) served from <domain>.  Strip known AO subdomain prefixes so the
    // fallback points at the parent domain, matching what webAO's same-origin
    // behaviour produces in the browser.
    fallback_asset_url_[0] = '\0';
    if (mode == ConnMode::WS || mode == ConnMode::WSS) {
        const char* scheme = (mode == ConnMode::WSS) ? "https" : "http";
        // Check for common AO subdomain prefixes: "ao.", "ws.", "wss."
        const char* asset_host = host;
        const char* dot = std::strchr(host, '.');
        if (dot && std::strchr(dot + 1, '.')) {  // at least 3 components
            int prefix_len = (int)(dot - host);
            bool is_ao_prefix =
                (prefix_len == 2 && std::strncmp(host, "ao", 2) == 0) ||
                (prefix_len == 2 && std::strncmp(host, "ws", 2) == 0) ||
                (prefix_len == 3 && std::strncmp(host, "wss", 3) == 0);
            if (is_ao_prefix)
                asset_host = dot + 1;  // e.g. "ao.umineko.online" → "umineko.online"
        }
        std::snprintf(fallback_asset_url_, sizeof(fallback_asset_url_),
            "%s://%s/base", scheme, asset_host);
        std::fprintf(stderr, "[app] WS asset fallback URL prepared: %s\n",
            fallback_asset_url_);
    }

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
    fallback_asset_url_[0] = '\0';
    AssetManager::clear_asset_url();
    ExtensionsConfig::reset();
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
