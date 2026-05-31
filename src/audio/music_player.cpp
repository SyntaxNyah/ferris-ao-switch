#include "music_player.hpp"
#include "../assets/asset_manager.hpp"
#include <cstring>
#include <cstdio>

namespace ao {

MusicPlayer::~MusicPlayer() { stop(); }

void MusicPlayer::play(const char* path, int fade_ms) {
    // AO2 stop-music sentinels: "~stop.mp3" (modern) and "~~" (legacy).
    if (std::strcmp(path, "~~") == 0 || std::strstr(path, "~stop")) { stop(); return; }

    // Resolve via AssetManager (prefetch cache → HTTP → sdmc: → romfs:). Try
    // the path exactly as given FIRST — the courtroom hands us the precise
    // relative path it already warmed in the prefetch cache, so this hits the
    // cache with no network. Fall back to the AO2 layouts otherwise.
    SDL_RWops* rw = AssetManager::open_rwops(path);
    char rel[320];
    if (!rw) {
        std::snprintf(rel, sizeof(rel), "sounds/music/%s", path);
        rw = AssetManager::open_rwops(rel);
    }
    if (!rw) {
        std::snprintf(rel, sizeof(rel), "music/%s", path);
        rw = AssetManager::open_rwops(rel);
    }
    if (!rw) {
        std::fprintf(stderr, "MusicPlayer: can't find '%s'\n", path);
        return;
    }

    // Stop and free the current track before loading the new one. The old
    // track was loaded with freesrc=0, so Mix_FreeMusic does NOT close its
    // RWops — we must SDL_RWclose(music_rw_) ourselves or it leaks.
    stop();

    // freesrc=0: we keep rw alive ourselves so SDL_mixer can stream from it.
    // music_rw_ is closed and freed by the next play()/stop() call.
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
