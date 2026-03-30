#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstdint>
#include <cstring>

namespace ao {

// LRU texture cache with a fixed number of slots.
// Evicts the least-recently-used entry when full.
// All textures are owned by the cache and freed on eviction or destruction.

static constexpr int TEX_CACHE_SLOTS = 64;

struct TexEntry {
    char          path[256] = {};
    SDL_Texture*  tex       = nullptr;
    uint32_t      last_used = 0;  // SDL_GetTicks() value
};

class TextureCache {
public:
    TextureCache() = default;
    ~TextureCache();

    // Load (or return cached) texture for the given filesystem path.
    // Returns nullptr on failure.
    SDL_Texture* get(SDL_Renderer* r, const char* path);

    // Explicitly release a texture by path.
    void release(const char* path);

    // Free all textures.
    void clear();

private:
    int find_slot(const char* path) const;
    int evict_slot() const;  // returns index of LRU slot

    TexEntry slots_[TEX_CACHE_SLOTS];
};

} // namespace ao
