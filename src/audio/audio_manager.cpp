#include "audio_manager.hpp"
#include "../assets/asset_manager.hpp"
#include <cstdio>
#include <cstring>
#include <SDL2/SDL.h>

namespace ao {

AudioManager::~AudioManager() {
    shutdown();
}

void AudioManager::shutdown() {
    stop_sfx();
    for (auto& s : sfx_cache_)
        if (s.chunk) { Mix_FreeChunk(s.chunk); s.chunk = nullptr; }
}

bool AudioManager::init() {
    Mix_AllocateChannels(SFX_CHANNELS);
    return true;
}

int AudioManager::find_sfx(const char* path) const {
    for (int i = 0; i < SFX_CACHE_SLOTS; ++i)
        if (sfx_cache_[i].chunk && std::strcmp(sfx_cache_[i].path, path) == 0)
            return i;
    return -1;
}

int AudioManager::evict_sfx() const {
    for (int i = 0; i < SFX_CACHE_SLOTS; ++i)
        if (!sfx_cache_[i].chunk) return i;
    int oldest = 0;
    for (int i = 1; i < SFX_CACHE_SLOTS; ++i)
        if (sfx_cache_[i].last_used < sfx_cache_[oldest].last_used)
            oldest = i;
    return oldest;
}

// `path` is a RELATIVE asset path (e.g. "sounds/sfx-guilty.ogg").
// Resolution order: HTTP streaming → sdmc: local base → romfs: bundled fallback.
bool AudioManager::play_sfx(const char* path) {
    int idx = find_sfx(path);
    if (idx < 0) {
        // Load via AssetManager (HTTP, local, or romfs)
        SDL_RWops* rw = AssetManager::open_rwops(path);
        if (!rw) {
            std::fprintf(stderr, "AudioManager: not found '%s'\n", path);
            return false;
        }
        // freesrc=1: Mix_LoadWAV_RW closes rw (which frees the owning buffer).
        // Mix_Chunk stores decoded PCM internally so we do not need to keep rw alive.
        Mix_Chunk* c = Mix_LoadWAV_RW(rw, 1);
        if (!c) {
            std::fprintf(stderr, "AudioManager: decode failed '%s': %s\n",
                path, Mix_GetError());
            return false;
        }
        idx = evict_sfx();
        if (sfx_cache_[idx].chunk) Mix_FreeChunk(sfx_cache_[idx].chunk);
        sfx_cache_[idx].chunk = c;
        std::strncpy(sfx_cache_[idx].path, path,
            sizeof(sfx_cache_[idx].path) - 1);
    }
    sfx_cache_[idx].last_used = SDL_GetTicks();
    return Mix_PlayChannel(-1, sfx_cache_[idx].chunk, 0) >= 0;
}

void AudioManager::stop_sfx() {
    Mix_HaltChannel(-1);
}

void AudioManager::set_sfx_volume(int vol) {
    Mix_Volume(-1, vol);
}

} // namespace ao
