#include "apng_player.hpp"
#include "asset_manager.hpp"
#include <cstdio>
#include <cstring>

namespace ao {

APNGPlayer::~APNGPlayer() { unload(); }

void APNGPlayer::unload() {
    for (int i = 0; i < frame_count_; ++i) {
        if (frames_[i]) { SDL_DestroyTexture(frames_[i]); frames_[i] = nullptr; }
        delays_[i] = 0;
    }
    frame_count_   = 0;
    current_frame_ = 0;
    accum_ms_      = 0;
    done_          = false;
    width_ = height_ = 0;
}

// `path` is a RELATIVE asset path.
// Resolution order: HTTP streaming → sdmc: local base → romfs: bundled fallback.
bool APNGPlayer::load(SDL_Renderer* r, const char* path) {
    unload();

    SDL_RWops* rw = AssetManager::open_rwops(path);
    if (!rw) {
        std::fprintf(stderr, "APNGPlayer: not found '%s'\n", path);
        return false;
    }

#if SDL_IMAGE_VERSION_ATLEAST(2, 6, 0)
    // Try animated (APNG / GIF) first — freesrc=1 closes rw when done
    IMG_Animation* anim = IMG_LoadAnimation_RW(rw, 1);
    if (anim && anim->count > 0) {
        int n = anim->count < APNG_MAX_FRAMES ? anim->count : APNG_MAX_FRAMES;
        for (int i = 0; i < n; ++i) {
            frames_[i] = SDL_CreateTextureFromSurface(r, anim->frames[i]);
            if (!frames_[i])
                std::fprintf(stderr, "APNGPlayer: frame %d texture fail: %s\n",
                    i, SDL_GetError());
            delays_[i] = anim->delays[i] > 0 ? anim->delays[i] : 100;
        }
        frame_count_ = n;
        width_  = anim->w;
        height_ = anim->h;
        IMG_FreeAnimation(anim);
        return frame_count_ > 0;
    }
    if (anim) IMG_FreeAnimation(anim);
    // rw was already closed by IMG_LoadAnimation_RW (freesrc=1)

    // Fall back to static image — need a fresh RWops
    rw = AssetManager::open_rwops(path);
#endif // SDL_IMAGE_VERSION_ATLEAST(2, 6, 0)
    if (!rw) return false;

    SDL_Surface* surf = IMG_Load_RW(rw, 1); // freesrc=1
    if (!surf) {
        std::fprintf(stderr, "APNGPlayer: decode failed '%s': %s\n",
            path, IMG_GetError());
        return false;
    }
    frames_[0] = SDL_CreateTextureFromSurface(r, surf);
    delays_[0] = 100;
    width_     = surf->w;
    height_    = surf->h;
    SDL_FreeSurface(surf);
    frame_count_ = frames_[0] ? 1 : 0;
    return frame_count_ > 0;
}

void APNGPlayer::reset() {
    current_frame_ = 0;
    accum_ms_      = 0;
    done_          = false;
}

void APNGPlayer::update(uint32_t dt_ms) {
    if (frame_count_ <= 1 || done_) return;

    accum_ms_ += dt_ms;
    while (accum_ms_ >= (uint32_t)delays_[current_frame_]) {
        accum_ms_ -= delays_[current_frame_];
        int next = current_frame_ + 1;
        if (next >= frame_count_) {
            if (loop_) {
                current_frame_ = 0;
            } else {
                current_frame_ = frame_count_ - 1;
                done_ = true;
                return;
            }
        } else {
            current_frame_ = next;
        }
    }
}

SDL_Texture* APNGPlayer::current() const {
    if (frame_count_ == 0) return nullptr;
    return frames_[current_frame_];
}

SDL_Rect APNGPlayer::current_rect() const {
    return {0, 0, width_, height_};
}

} // namespace ao
