#pragma once
#include <SDL2/SDL.h>

namespace ao {

// Background asset pre-fetcher.
//
// Screens submit relative asset paths via prefetch(). The worker thread
// downloads them via AssetManager::fetch_bytes() (HTTP or local) and stores
// the result in AssetManager's prefetch cache. The next time fetch_bytes() or
// open_rwops() is called for that path on the main thread, it returns
// immediately from cache — no network round-trip.
//
// This is purely an optimisation. Everything works without it; prefetch() just
// removes the hitch when an asset is needed for the first time.
//
// Usage:
//   app.asset_stream().prefetch("characters/phoenix/emotions/normal(a).png");
//   // ... later in the same or next frame:
//   SDL_RWops* rw = AssetManager::open_rwops("characters/phoenix/emotions/normal(a).png");
//   // rw is backed by the pre-fetched buffer — no blocking network call
//
// Thread safety: prefetch() is safe to call from the main thread at any time.
//                start() / stop() must be called from the main thread.

static constexpr int STREAM_QUEUE_SIZE = 64;

class AssetStream {
public:
    AssetStream()  = default;
    ~AssetStream() { stop(); }

    void start();
    void stop();

    // Queue a relative path for background prefetch.
    // Returns false if the request queue is full (caller may retry next frame).
    // Silently ignores duplicate requests already in the queue.
    bool prefetch(const char* relative);

    // Returns true and sets out_path (max 256 chars) when a prefetch completes.
    // Drain this each frame to stay informed (optional — for logging/debugging).
    bool poll_done(char* out_path, int out_cap);

private:
    static int  thread_func(void* userdata);
    void        run();

    static constexpr int N_WORKERS = 4;
    SDL_Thread* threads_[N_WORKERS] = {};
    bool        running_   = false;

    // Request queue (main → worker)
    SDL_mutex* req_mutex_  = nullptr;
    SDL_cond*  req_cond_   = nullptr;
    char       req_buf_[STREAM_QUEUE_SIZE][256] = {};
    int        req_head_   = 0;
    int        req_tail_   = 0;  // [head, tail)

    // Done queue (worker → main) — paths of completed prefetches
    SDL_mutex* done_mutex_ = nullptr;
    char       done_buf_[STREAM_QUEUE_SIZE][256] = {};
    int        done_head_  = 0;
    int        done_tail_  = 0;
};

} // namespace ao
