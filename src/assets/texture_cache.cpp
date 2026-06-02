#include "texture_cache.hpp"
#include "asset_manager.hpp"
#include <cstdio>
#include <SDL2/SDL.h>

namespace ao {

TextureCache::~TextureCache() { clear(); }

void TextureCache::clear() {
    for (auto& s : slots_) {
        if (s.tex) { SDL_DestroyTexture(s.tex); s.tex = nullptr; }
        s.path[0]   = '\0';
        s.last_used = 0;
    }
}

int TextureCache::find_slot(const char* path) const {
    for (int i = 0; i < TEX_CACHE_SLOTS; ++i)
        if (slots_[i].tex && std::strcmp(slots_[i].path, path) == 0)
            return i;
    return -1;
}

int TextureCache::evict_slot() const {
    for (int i = 0; i < TEX_CACHE_SLOTS; ++i)
        if (!slots_[i].tex) return i;

    int oldest = 0;
    for (int i = 1; i < TEX_CACHE_SLOTS; ++i)
        if (slots_[i].last_used < slots_[oldest].last_used)
            oldest = i;
    return oldest;
}

// `path` is a RELATIVE asset path (e.g. "characters/phoenix/emotions/normal(a).png").
// Resolution order: HTTP streaming → sdmc: local base → romfs: bundled fallback.
SDL_Texture* TextureCache::get(SDL_Renderer* r, const char* path) {
    int idx = find_slot(path);
    if (idx >= 0) {
        slots_[idx].last_used = SDL_GetTicks();
        return slots_[idx].tex;
    }

    SDL_Texture* tex = nullptr;

    // Off-thread decode fast path: a worker may have already decoded this icon to
    // an SDL_Surface — if so we only do the GPU upload here (no decode, no I/O).
    AssetManager::DecodedFrames df;
    if (AssetManager::take_frames(path, df)) {
        if (df.count > 0 && df.frames[0])
            tex = SDL_CreateTextureFromSurface(r, df.frames[0]);
        for (int i = 0; i < df.count; ++i) if (df.frames[i]) SDL_FreeSurface(df.frames[i]);
    }

    // Otherwise decode here from bytes (HTTP/prefetch cache → sdmc: → romfs:).
    if (!tex) {
        SDL_RWops* rw = AssetManager::open_rwops(path);
        if (!rw) {
            std::fprintf(stderr, "TextureCache: not found '%s'\n", path);
            return nullptr;
        }
        // freesrc=1: IMG_LoadTexture_RW closes rw (which frees the owning buffer)
        tex = IMG_LoadTexture_RW(r, rw, 1);
        if (!tex) {
            std::fprintf(stderr, "TextureCache: decode failed '%s': %s\n",
                path, IMG_GetError());
            return nullptr;
        }
    }

    idx = evict_slot();
    if (slots_[idx].tex) SDL_DestroyTexture(slots_[idx].tex);

    slots_[idx].tex = tex;
    std::strncpy(slots_[idx].path, path, sizeof(slots_[idx].path) - 1);
    slots_[idx].path[sizeof(slots_[idx].path) - 1] = '\0';
    slots_[idx].last_used = SDL_GetTicks();
    return tex;
}

SDL_Texture* TextureCache::peek(const char* path) const {
    int idx = find_slot(path);
    return (idx >= 0) ? slots_[idx].tex : nullptr;
}

void TextureCache::release(const char* path) {
    int idx = find_slot(path);
    if (idx < 0) return;
    SDL_DestroyTexture(slots_[idx].tex);
    slots_[idx].tex       = nullptr;
    slots_[idx].path[0]   = '\0';
    slots_[idx].last_used = 0;
}

} // namespace ao
