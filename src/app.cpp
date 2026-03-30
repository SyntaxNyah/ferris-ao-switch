#include "app.hpp"
#include "render/renderer.hpp"
#include "state/game_state.hpp"
#include "ui/screen.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <cstdio>

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
    delete game_state_;
    delete renderer_;

    Mix_CloseAudio();
    Mix_Quit();
    TTF_Quit();
    IMG_Quit();

    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();

#ifdef __SWITCH__
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
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }

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
        if (screen_count_ > 0)
            screen_stack_[screen_count_ - 1]->handle_event(e);
    }
}

void App::update(uint32_t dt_ms) {
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

} // namespace ao
