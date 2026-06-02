#include "apng_player.hpp"
#include "asset_manager.hpp"
#include <cstdio>
#include <cstring>
#include <cstdint>

namespace ao {

// ── Shared decoded-animation cache (main thread only) ───────────────────────────
//
// An LRU of already-decoded frame-sets keyed by relative path, bounded by an
// approximate VRAM budget. APNGPlayer "parks" its current frames here when it
// switches to another sprite and "takes" them back if asked for the same path
// again — so re-showing a recently-seen character/background is instant (no
// re-decode, no SD/network read). Ownership is transferred, never shared: a
// frame-set is owned by exactly one APNGPlayer OR one cache slot at a time, so
// there are no dangling pointers or double-frees. All access is on the render
// thread, so no locking is needed.
namespace {

struct AnimSlot {
    char         path[256];
    SDL_Texture* frames[APNG_MAX_FRAMES];
    int          delays[APNG_MAX_FRAMES];
    int          count;
    int          w, h;
    uint32_t     bytes;       // approx VRAM (w*h*4*count)
    uint32_t     last_used;
    bool         occupied;
};

constexpr int      ANIM_SLOTS  = 32;
constexpr uint32_t ANIM_BUDGET = 96u * 1024 * 1024;   // ~96 MB of decoded frames

AnimSlot s_anim[ANIM_SLOTS] = {};
uint32_t s_anim_total = 0;
uint32_t s_anim_clock = 0;

uint32_t anim_bytes(int w, int h, int count) {
    return (uint32_t)(w > 0 ? w : 1) * (uint32_t)(h > 0 ? h : 1) * 4u *
           (uint32_t)(count > 0 ? count : 1);
}

void anim_drop(AnimSlot& s) {   // destroy a cached slot's textures
    for (int i = 0; i < s.count; ++i)
        if (s.frames[i]) { SDL_DestroyTexture(s.frames[i]); s.frames[i] = nullptr; }
    if (s_anim_total >= s.bytes) s_anim_total -= s.bytes; else s_anim_total = 0;
    s.count = 0; s.bytes = 0; s.occupied = false; s.path[0] = '\0';
}

// Take ownership of a decoded frame-set into the cache. If it can't fit even
// after evicting LRU entries, the frames are destroyed (discarded).
void anim_store(const char* path, SDL_Texture** frames, const int* delays,
                int count, int w, int h) {
    if (count <= 0 || !path || !path[0]) {
        for (int i = 0; i < count; ++i) if (frames[i]) SDL_DestroyTexture(frames[i]);
        return;
    }
    uint32_t bytes = anim_bytes(w, h, count);
    auto first_free = []() {
        for (int i = 0; i < ANIM_SLOTS; ++i) if (!s_anim[i].occupied) return i;
        return -1;
    };
    while (bytes <= ANIM_BUDGET && (s_anim_total + bytes > ANIM_BUDGET || first_free() < 0)) {
        int lru = -1;
        for (int i = 0; i < ANIM_SLOTS; ++i)
            if (s_anim[i].occupied &&
                (lru < 0 || s_anim[i].last_used < s_anim[lru].last_used)) lru = i;
        if (lru < 0) break;
        anim_drop(s_anim[lru]);
    }
    int slot = first_free();
    if (bytes > ANIM_BUDGET || slot < 0) {   // too big / no room — discard
        for (int i = 0; i < count; ++i) if (frames[i]) SDL_DestroyTexture(frames[i]);
        return;
    }
    AnimSlot& s = s_anim[slot];
    std::strncpy(s.path, path, sizeof(s.path) - 1); s.path[sizeof(s.path) - 1] = '\0';
    for (int i = 0; i < count; ++i) { s.frames[i] = frames[i]; s.delays[i] = delays[i]; }
    s.count = count; s.w = w; s.h = h; s.bytes = bytes;
    s.last_used = ++s_anim_clock; s.occupied = true;
    s_anim_total += bytes;
}

// Hand a cached frame-set back to the caller (cache relinquishes ownership).
bool anim_take(const char* path, SDL_Texture** frames, int* delays,
               int* count, int* w, int* h) {
    if (!path || !path[0]) return false;
    for (int i = 0; i < ANIM_SLOTS; ++i) {
        AnimSlot& s = s_anim[i];
        if (!s.occupied || std::strcmp(s.path, path) != 0) continue;
        for (int j = 0; j < s.count; ++j) { frames[j] = s.frames[j]; delays[j] = s.delays[j]; }
        *count = s.count; *w = s.w; *h = s.h;
        if (s_anim_total >= s.bytes) s_anim_total -= s.bytes; else s_anim_total = 0;
        s.count = 0; s.bytes = 0; s.occupied = false; s.path[0] = '\0';
        return true;
    }
    return false;
}

} // anonymous namespace

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
    path_[0]       = '\0';
}

void APNGPlayer::detach() {
    // Move the current frames into the shared cache (it takes ownership) so a
    // later load of the same path is instant. Then clear our state WITHOUT
    // destroying (the cache — or anim_store's discard — owns them now).
    if (frame_count_ > 0)
        anim_store(path_, frames_, delays_, frame_count_, width_, height_);
    for (int i = 0; i < APNG_MAX_FRAMES; ++i) frames_[i] = nullptr;
    frame_count_   = 0;
    current_frame_ = 0;
    accum_ms_      = 0;
    done_          = false;
    width_ = height_ = 0;
    path_[0]       = '\0';
}

// `path` is a RELATIVE asset path.
// Resolution order: decode cache → HTTP streaming → sdmc: local base → romfs:.
bool APNGPlayer::load(SDL_Renderer* r, const char* path) {
    // Already showing this exact asset — keep the decoded frames.
    if (frame_count_ > 0 && path && std::strcmp(path, path_) == 0)
        return true;

    // Park the current frames in the cache, then re-show this path instantly if
    // we've decoded it before.
    detach();
    if (path && anim_take(path, frames_, delays_, &frame_count_, &width_, &height_)) {
        std::strncpy(path_, path, sizeof(path_) - 1);
        path_[sizeof(path_) - 1] = '\0';
        current_frame_ = 0; accum_ms_ = 0; done_ = false;
        return true;
    }

    // A worker may have already DECODED this image off the main thread into
    // SDL_Surface frames — if so we only do the GPU upload here (no decode, no
    // disk/network read). This is the off-thread-decode fast path.
    AssetManager::DecodedFrames df;
    if (path && AssetManager::take_frames(path, df)) {
        int n = df.count < APNG_MAX_FRAMES ? df.count : APNG_MAX_FRAMES;
        for (int i = 0; i < n; ++i) {
            frames_[i] = df.frames[i] ? SDL_CreateTextureFromSurface(r, df.frames[i]) : nullptr;
            if (frames_[i]) SDL_SetTextureBlendMode(frames_[i], SDL_BLENDMODE_BLEND);
            if (df.frames[i]) SDL_FreeSurface(df.frames[i]);   // ours now; uploaded
            delays_[i] = df.delays[i] > 0 ? df.delays[i] : 100;
        }
        for (int i = n; i < df.count; ++i) if (df.frames[i]) SDL_FreeSurface(df.frames[i]);
        frame_count_ = n;
        width_  = df.w;
        height_ = df.h;
        std::strncpy(path_, path, sizeof(path_) - 1);
        path_[sizeof(path_) - 1] = '\0';
        current_frame_ = 0; accum_ms_ = 0; done_ = false;
        return frame_count_ > 0;
    }

    SDL_RWops* rw = AssetManager::open_rwops(path);
    if (!rw) return false;

#if SDL_IMAGE_VERSION_ATLEAST(2, 6, 0)
    // Try animated (APNG / GIF) first — freesrc=1 closes rw when done
    IMG_Animation* anim = IMG_LoadAnimation_RW(rw, 1);
    if (anim && anim->count > 0) {
        int n = anim->count < APNG_MAX_FRAMES ? anim->count : APNG_MAX_FRAMES;
        for (int i = 0; i < n; ++i) {
            frames_[i] = SDL_CreateTextureFromSurface(r, anim->frames[i]);
            if (frames_[i]) SDL_SetTextureBlendMode(frames_[i], SDL_BLENDMODE_BLEND);
            else
                std::fprintf(stderr, "APNGPlayer: frame %d texture fail: %s\n",
                    i, SDL_GetError());
            delays_[i] = anim->delays[i] > 0 ? anim->delays[i] : 100;
        }
        frame_count_ = n;
        width_  = anim->w;
        height_ = anim->h;
        IMG_FreeAnimation(anim);
        if (frame_count_ > 0) {
            std::strncpy(path_, path, sizeof(path_) - 1);
            path_[sizeof(path_) - 1] = '\0';
        }
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
    if (frames_[0]) SDL_SetTextureBlendMode(frames_[0], SDL_BLENDMODE_BLEND);
    delays_[0] = 100;
    width_     = surf->w;
    height_    = surf->h;
    SDL_FreeSurface(surf);
    frame_count_ = frames_[0] ? 1 : 0;
    if (frame_count_ > 0) {
        std::strncpy(path_, path, sizeof(path_) - 1);
        path_[sizeof(path_) - 1] = '\0';
    }
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
