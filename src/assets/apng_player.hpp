#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <cstdint>

namespace ao {

// Plays an APNG or GIF animation.
// Falls back to a static PNG if the image is not animated.
// All frame textures are owned by this object.

static constexpr int APNG_MAX_FRAMES = 128;

class APNGPlayer {
public:
    APNGPlayer() = default;
    ~APNGPlayer();

    // Load an animation from path. Returns false on failure.
    bool load(SDL_Renderer* r, const char* path);

    // Unload frames and reset state.
    void unload();

    // Advance the animation by dt_ms milliseconds.
    void update(uint32_t dt_ms);

    // Get the texture for the current frame.
    SDL_Texture* current() const;

    // Get source rect for the current frame (always {0,0,w,h} for full frame).
    SDL_Rect current_rect() const;

    bool loaded()   const { return frame_count_ > 0; }
    bool animated() const { return frame_count_ > 1; }
    bool finished() const { return !loop_ && done_; }

    // If false (default), loop forever. If true, play once then stop.
    void set_loop(bool loop) { loop_ = loop; }

    // Reset to first frame.
    void reset();

private:
    SDL_Texture* frames_[APNG_MAX_FRAMES] = {};
    int          delays_[APNG_MAX_FRAMES] = {};  // ms per frame
    int          frame_count_ = 0;
    int          current_frame_ = 0;
    uint32_t     accum_ms_  = 0;
    bool         loop_      = true;
    bool         done_      = false;
    int          width_     = 0;
    int          height_    = 0;
};

} // namespace ao
