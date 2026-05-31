#pragma once
#include <SDL2/SDL_mixer.h>

namespace ao {

static constexpr int SFX_CACHE_SLOTS  = 16;
static constexpr int SFX_CHANNELS     = 8;

struct SfxEntry {
    char       path[256] = {};
    Mix_Chunk* chunk     = nullptr;
    uint32_t   last_used = 0;
};

class AudioManager {
public:
    AudioManager()  = default;
    ~AudioManager();

    bool init();  // called once after Mix_OpenAudio

    // Free all cached chunks and halt channels. Idempotent. MUST be called
    // while the mixer is still open (i.e. before Mix_CloseAudio), so App calls
    // it explicitly during teardown rather than relying on the destructor —
    // member destructors run after App::~App has already closed the mixer.
    void shutdown();

    // Play a sound effect by filesystem path (cached).
    // Returns false if the file can't be loaded.
    bool play_sfx(const char* path);

    // Stop all SFX channels.
    void stop_sfx();

    // Volume 0-128 (MIX_MAX_VOLUME)
    void set_sfx_volume(int vol);

private:
    int  find_sfx(const char* path) const;
    int  evict_sfx() const;

    SfxEntry sfx_cache_[SFX_CACHE_SLOTS];
};

} // namespace ao
