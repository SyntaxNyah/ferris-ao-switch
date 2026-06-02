#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstdint>
#include <cstring>

namespace ao {

// LRU texture cache with a fixed number of slots.
// Evicts the least-recently-used entry when full.
// All textures are owned by the cache and freed on eviction or destruction.

// Sized to hold a FULL large AO character roster at once (servers run 100-600,
// occasionally more), so scrolling back over the grid never re-decodes an icon
// that was already shown. ~48 MB at 64 KB per 128x128 RGBA icon — trivial on the
// Switch's ~2 GB budget. The trade-off is the linear slot-search that peek()
// runs every frame in CharSelectScreen, but 768 strcmp per visible icon is cheap.
static constexpr int TEX_CACHE_SLOTS = 768;

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
