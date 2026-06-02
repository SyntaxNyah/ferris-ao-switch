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

static constexpr int STREAM_QUEUE_SIZE = 2048;

class AssetStream {
public:
    AssetStream()  = default;
    ~AssetStream() { stop(); }

    void start();
    void stop();

    // Queue a relative path for background prefetch (raw bytes only).
    // Returns false if the request queue is full (caller may retry next frame).
    // Silently ignores duplicate requests already in the queue.
    bool prefetch(const char* relative);

    // Like prefetch(), but the worker also DECODES the image off the main thread
    // into SDL_Surface frames (AssetManager::store_frames), so the render thread
    // only does the GPU upload. Use for sprites/backgrounds whose exact path is
    // already known (the courtroom emote/scene prefetch). Non-images fall back to
    // raw bytes automatically.
    bool prefetch_decode(const char* relative);

    // Queue a worker-side sequential probe: the worker tries `base`+ext for the
    // category's extensions (learned format first), STOPS at the first that
    // exists, decodes it off-thread, and records the winning format. Replaces the
    // old fire-all-candidates fan-out for char icons / backgrounds — one request,
    // ~1 fetch on a format-uniform server. `category` is an ExtensionsConfig::Category.
    bool prefetch_probe(const char* base, int category);

    // Drop every still-queued request (in-flight downloads finish normally).
    // The courtroom calls this on entry so IC sprite prefetches are not stuck
    // behind a flood of character-select icon prefetches on big servers.
    void clear_pending();

    // Multi-extension prefetch helpers — queue every variant that the server
    // might ship for a given asset kind. Mirrors AO-SDL's AssetLibrary::probe()
    // behaviour where all candidate extensions are fetched concurrently and
    // whichever decodes first wins. Extension lists come from ExtensionsConfig
    // (loaded from <asset_url>/extensions.json or built-in defaults).
    //
    // `rel_without_ext` is the path WITHOUT the trailing ".xxx" — e.g.
    //   prefetch_charicon("characters/phoenix/char_icon")
    //   prefetch_image   ("characters/phoenix/(a)normal")
    //   prefetch_background("background/gs4/defenseempty")
    //
    // Returns the number of variants actually queued (0 if the queue was
    // saturated or the extension list was empty).
    int prefetch_charicon  (const char* rel_without_ext);
    int prefetch_image     (const char* rel_without_ext); // emote/preanim sprites
    int prefetch_emoticon  (const char* rel_without_ext); // emote-button still frames
    int prefetch_background(const char* rel_without_ext);

    // Returns true and sets out_path (max 256 chars) when a prefetch completes.
    // Drain this each frame to stay informed (optional — for logging/debugging).
    bool poll_done(char* out_path, int out_cap);

private:
    static int  thread_func(void* userdata);
    void        run();

    static constexpr int N_WORKERS = 8;
    SDL_Thread* threads_[N_WORKERS] = {};
    bool        running_   = false;

    // Enqueue a request with a kind tag (shared by prefetch/prefetch_decode/
    // prefetch_probe). Dedupes on path AND kind.
    bool enqueue(const char* relative, int kind);

    // Request queue (main → worker). req_kind_ is parallel to req_buf_:
    //   -1 = raw bytes, -2 = fetch+decode, 0..N = probe that ExtensionsConfig category.
    SDL_mutex* req_mutex_  = nullptr;
    SDL_cond*  req_cond_   = nullptr;
    char       req_buf_[STREAM_QUEUE_SIZE][256] = {};
    int        req_kind_[STREAM_QUEUE_SIZE]     = {};
    int        req_head_   = 0;
    int        req_tail_   = 0;  // [head, tail)

    // Done queue (worker → main) — paths of completed prefetches
    SDL_mutex* done_mutex_ = nullptr;
    char       done_buf_[STREAM_QUEUE_SIZE][256] = {};
    int        done_head_  = 0;
    int        done_tail_  = 0;
};

} // namespace ao
