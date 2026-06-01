#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstdint>
#include <cstring>

namespace ao {

// LRU texture cache with a fixed number of slots.
// Evicts the least-recently-used entry when full.
// All textures are owned by the cache and freed on eviction or destruction.

// Enlarged from 64 to hold most of a full AO character roster at once
// (typical servers have 100-600 chars). Sized as a balance between VRAM
// footprint (~16 MB at 64 KB per 128x128 RGBA icon) and the cost of the
// linear slot-search that peek() runs every frame in CharSelectScreen.
static constexpr int TEX_CACHE_SLOTS = 512;

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

    // Cache-only lookup — does NOT load the asset. Returns nullptr if not cached.
    SDL_Texture* peek(const char* path) const;

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
