#include "music_player.hpp"
#include "../assets/asset_manager.hpp"
#include <cstring>
#include <cstdio>

namespace ao {

MusicPlayer::~MusicPlayer() { stop(); }

void MusicPlayer::play(const char* path, int fade_ms) {
    if (std::strcmp(path, "~~") == 0) { stop(); return; }

    // Resolve path via asset manager
    char resolved[512];
    if (!AssetManager::resolve(path, resolved, sizeof(resolved))) {
        // Try prepending "music/"
        char rel[300];
        std::snprintf(rel, sizeof(rel), "music/%s", path);
        if (!AssetManager::resolve(rel, resolved, sizeof(resolved))) {
            std::fprintf(stderr, "MusicPlayer: can't find '%s'\n", path);
            return;
        }
    }

    if (music_) {
        Mix_FadeOutMusic(fade_ms / 2);
        // SDL_mixer will free and close the old music when the fade completes;
        // we need to free after fade.  Simplest: halt then free.
        Mix_HaltMusic();
        Mix_FreeMusic(music_);
        music_ = nullptr;
    }

    music_ = Mix_LoadMUS(resolved);
    if (!music_) {
        std::fprintf(stderr, "MusicPlayer: Mix_LoadMUS('%s'): %s\n",
            resolved, Mix_GetError());
        return;
    }

    Mix_FadeInMusic(music_, -1, fade_ms);
    std::strncpy(current_path_, path, sizeof(current_path_) - 1);
}

void MusicPlayer::stop() {
    Mix_HaltMusic();
    if (music_) { Mix_FreeMusic(music_); music_ = nullptr; }
    current_path_[0] = '\0';
}

bool MusicPlayer::is_playing() const {
    return Mix_PlayingMusic() != 0;
}

void MusicPlayer::set_volume(int vol) {
    Mix_VolumeMusic(vol);
}

} // namespace ao
