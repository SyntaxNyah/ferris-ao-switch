#include "music_player.hpp"
#include "../assets/asset_manager.hpp"
#include <cstring>
#include <cstdio>
#include <strings.h>   // strcasecmp

namespace {
// SDL_mixer's Mix_LoadMUS_RW auto-detection frequently misreads .opus (it shares
// the "OggS" container with Vorbis, and peeking far enough to find "OpusHead"
// fails on a streamed RWops) — which is exactly the AO case, where ~all server
// music is Opus. Force the right decoder from the filename extension instead.
Mix_MusicType music_type_for(const char* path) {
    const char* dot = std::strrchr(path, '.');
    if (!dot) return MUS_NONE;
    if (!strcasecmp(dot, ".opus")) return MUS_OPUS;
    if (!strcasecmp(dot, ".ogg"))  return MUS_OGG;
    if (!strcasecmp(dot, ".mp3"))  return MUS_MP3;
    if (!strcasecmp(dot, ".flac")) return MUS_FLAC;
    if (!strcasecmp(dot, ".wav"))  return MUS_WAV;
    if (!strcasecmp(dot, ".mid") || !strcasecmp(dot, ".midi")) return MUS_MID;
    if (!strcasecmp(dot, ".mod") || !strcasecmp(dot, ".xm") ||
        !strcasecmp(dot, ".it")  || !strcasecmp(dot, ".s3m")) return MUS_MOD;
    return MUS_NONE;
}

// Identify the decoder from the file's MAGIC bytes — more reliable than the
// extension (some servers send a track name with no extension, or the wrong
// one). Critically, this distinguishes Opus from Vorbis: both use the "OggS"
// container, but SDL2_mixer 2.0.4's own auto-detection only checks "OggS" and so
// hands every Opus track to the Vorbis decoder, which fails silently. We peek
// for "OpusHead" inside the first Ogg page and force MUS_OPUS ourselves.
// Reads from the head of `rw` and restores the position. Returns MUS_NONE when
// nothing matches (caller falls back to the extension, then to auto-detect).
Mix_MusicType sniff_music_type(SDL_RWops* rw) {
    unsigned char h[64];
    const Sint64 pos = SDL_RWtell(rw);
    const size_t n   = SDL_RWread(rw, h, 1, sizeof(h));
    SDL_RWseek(rw, pos, RW_SEEK_SET);   // rewind so the decoder sees byte 0
    if (n >= 36 && std::memcmp(h, "OggS", 4) == 0 &&
                   std::memcmp(h + 28, "OpusHead", 8) == 0) return MUS_OPUS;
    if (n >= 4  && std::memcmp(h, "OggS", 4) == 0) return MUS_OGG;   // Vorbis/FLAC-in-Ogg
    if (n >= 4  && std::memcmp(h, "fLaC", 4) == 0) return MUS_FLAC;
    if (n >= 4  && std::memcmp(h, "RIFF", 4) == 0) return MUS_WAV;
    if (n >= 4  && std::memcmp(h, "MThd", 4) == 0) return MUS_MID;
    if (n >= 3  && std::memcmp(h, "ID3",  3) == 0) return MUS_MP3;
    if (n >= 2  && h[0] == 0xFF && (h[1] & 0xE0) == 0xE0) return MUS_MP3;  // MPEG frame sync
    return MUS_NONE;
}
} // namespace

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
    // Pick the decoder by magic first (distinguishes Opus from Vorbis, which the
    // mixer's own detector cannot), then by extension, then let the mixer detect.
    Mix_MusicType mt = sniff_music_type(rw);
    if (mt == MUS_NONE) mt = music_type_for(path);
    music_ = (mt != MUS_NONE) ? Mix_LoadMUSType_RW(rw, mt, 0)   // forced (e.g. Opus)
                              : Mix_LoadMUS_RW(rw, 0);          // unknown → mixer detect
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
