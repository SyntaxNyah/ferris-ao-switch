#include "music_player.hpp"
#include "../assets/asset_manager.hpp"
#include <cstring>
#include <cstdio>

namespace ao {

MusicPlayer::~MusicPlayer() { stop(); }

void MusicPlayer::play(const char* path, int fade_ms) {
    if (std::strcmp(path, "~~") == 0) { stop(); return; }

    // Resolve via AssetManager (HTTP streaming → sdmc: → romfs:)
    // Try the path as given first, then with "music/" prepended.
    SDL_RWops* rw = AssetManager::open_rwops(path);
    if (!rw) {
        char rel[300];
        std::snprintf(rel, sizeof(rel), "music/%s", path);
        rw = AssetManager::open_rwops(rel);
    }
    if (!rw) {
        std::fprintf(stderr, "MusicPlayer: can't find '%s'\n", path);
        return;
    }

    // Stop and free the current track before loading the new one
    if (music_) {
        Mix_HaltMusic();
        // music_rw_ is freed by Mix_FreeMusic (freesrc was 1)
        Mix_FreeMusic(music_);
        music_    = nullptr;
        music_rw_ = nullptr;
    }

    // freesrc=0: we keep rw alive ourselves so SDL_mixer can stream from it.
    // music_rw_ is closed and freed when we next call _free_music() / stop().
    music_rw_ = rw;
    music_    = Mix_LoadMUS_RW(rw, 0);
    if (!music_) {
        std::fprintf(stderr, "MusicPlayer: Mix_LoadMUS_RW failed for '%s': %s\n",
            path, Mix_GetError());
        SDL_RWclose(rw);
        music_rw_ = nullptr;
        return;
    }

    Mix_FadeInMusic(music_, -1, fade_ms);
    std::strncpy(current_path_, path, sizeof(current_path_) - 1);
}

void MusicPlayer::stop() {
    Mix_HaltMusic();
    if (music_) {
        Mix_FreeMusic(music_);
        music_ = nullptr;
    }
    // Close the RWops after freeing the music (SDL_mixer may access it until then)
    if (music_rw_) {
        SDL_RWclose(music_rw_);
        music_rw_ = nullptr;
    }
    current_path_[0] = '\0';
}

bool MusicPlayer::is_playing() const {
    return Mix_PlayingMusic() != 0;
}

void MusicPlayer::set_volume(int vol) {
    Mix_VolumeMusic(vol);
}

} // namespace ao
