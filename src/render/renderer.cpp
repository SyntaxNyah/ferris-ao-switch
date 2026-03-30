#include "renderer.hpp"
#include <cstdio>

namespace ao {

Renderer::~Renderer() {
    if (renderer_) SDL_DestroyRenderer(renderer_);
}

bool Renderer::init(SDL_Window* window) {
    renderer_ = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return false;
    }
    SDL_RenderSetLogicalSize(renderer_, WIDTH, HEIGHT);
    return true;
}

void Renderer::clear(SDL_Color bg) {
    SDL_SetRenderDrawColor(renderer_, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderClear(renderer_);
}

void Renderer::present() {
    SDL_RenderPresent(renderer_);
}

void Renderer::draw(SDL_Texture* tex,
                    const SDL_Rect* src,
                    const SDL_Rect* dst,
                    bool flip_h) {
    SDL_RenderCopyEx(renderer_, tex, src, dst,
                     0.0, nullptr,
                     flip_h ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE);
}

void Renderer::fill_rect(const SDL_Rect& r, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_SetRenderDrawBlendMode(renderer_,
        c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
    SDL_RenderFillRect(renderer_, &r);
}

void Renderer::draw_rect(const SDL_Rect& r, SDL_Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_RenderDrawRect(renderer_, &r);
}

} // namespace ao
