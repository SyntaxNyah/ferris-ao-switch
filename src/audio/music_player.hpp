#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>

namespace ao {

// BGM player with crossfade.
// Handles the "~~" stop-music sentinel from AO2 MC packets.
class MusicPlayer {
public:
    MusicPlayer()  = default;
    ~MusicPlayer();

    // Play a music file (path must be a valid SDL_mixer-supported format).
    // Fades out the current track and fades in the new one.
    // If path is "~~", stops all music.
    void play(const char* path, int fade_ms = 300);

    // Stop immediately.
    void stop();

    bool is_playing() const;

    void set_volume(int vol); // 0-128

    const char* current() const { return current_path_; }

private:
    Mix_Music* music_      = nullptr;
    SDL_RWops* music_rw_   = nullptr;  // kept alive while SDL_mixer streams from it
    char       current_path_[256] = {};
};

} // namespace ao
