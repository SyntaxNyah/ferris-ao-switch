#include "asset_stream.hpp"
#include "asset_manager.hpp"
#include "extensions_config.hpp"
#include "../net/http_fetch.hpp"
#include <SDL2/SDL_image.h>
#include <cstring>
#include <cstdio>

namespace ao {

// Request-kind tags stored in req_kind_ (>= 0 means "probe this ExtensionsConfig
// category", which is why these are negative).
static constexpr int KIND_RAW    = -1;   // fetch bytes only
static constexpr int KIND_DECODE = -2;   // fetch the exact path + decode off-thread

// Decode fetched image bytes into ARGB8888 frames on the worker and stage them
// for the main thread to upload (AssetManager::store_frames). Converting to a
// single canonical format here makes SDL_CreateTextureFromSurface a near-memcpy
// on the render thread. Returns true if frames were staged (caller then frees
// `data`); false means "not an image / decode failed" — caller keeps the bytes
// so the main thread can decode them the old way. `data` is never freed here.
static bool decode_and_store(const char* path, uint8_t* data, int size) {
    SDL_RWops* rw = SDL_RWFromConstMem(data, size);
    if (!rw) return false;

    SDL_Surface* fr[AssetManager::FRAMES_MAX] = {};
    int delays[AssetManager::FRAMES_MAX] = {};
    int count = 0, w = 0, h = 0;
    bool ok = false;

    auto take_static = [&]() {
        SDL_RWseek(rw, 0, RW_SEEK_SET);
        SDL_Surface* s = IMG_Load_RW(rw, 0);
        if (!s) return;
        fr[0] = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(s);
        if (fr[0]) { delays[0] = 0; count = 1; w = fr[0]->w; h = fr[0]->h; ok = true; }
    };

#if SDL_IMAGE_VERSION_ATLEAST(2, 6, 0)
    IMG_Animation* anim = IMG_LoadAnimation_RW(rw, 0);
    if (anim && anim->count > 0 && anim->frames) {
        count = anim->count > AssetManager::FRAMES_MAX ? AssetManager::FRAMES_MAX : anim->count;
        w = anim->w; h = anim->h; ok = true;
        for (int i = 0; i < count; ++i) {
            fr[i] = anim->frames[i]
                ? SDL_ConvertSurfaceFormat(anim->frames[i], SDL_PIXELFORMAT_ARGB8888, 0)
                : nullptr;
            delays[i] = (anim->delays && anim->delays[i] > 0) ? anim->delays[i] : 100;
            if (!fr[i]) { ok = false; break; }     // bail on any failed frame
        }
        IMG_FreeAnimation(anim);
        if (!ok) { for (int i = 0; i < count; ++i) if (fr[i]) { SDL_FreeSurface(fr[i]); fr[i] = nullptr; } count = 0; }
    } else {
        if (anim) IMG_FreeAnimation(anim);
        take_static();
    }
#else
    take_static();
#endif

    SDL_RWclose(rw);   // does not free `data` (const-mem RW)

    if (!ok || count <= 0) {
        for (int i = 0; i < AssetManager::FRAMES_MAX; ++i) if (fr[i]) SDL_FreeSurface(fr[i]);
        return false;
    }
    AssetManager::store_frames(path, fr, delays, count, w, h);   // takes ownership of fr[]
    return true;
}

void AssetStream::start() {
    if (running_) return;

    req_mutex_  = SDL_CreateMutex();
    req_cond_   = SDL_CreateCond();
    done_mutex_ = SDL_CreateMutex();
    req_head_   = req_tail_  = 0;
    done_head_  = done_tail_ = 0;
    running_    = true;
    for (int i = 0; i < N_WORKERS; ++i)
        threads_[i] = SDL_CreateThread(thread_func, "AssetStream", this);
}

void AssetStream::stop() {
    if (!running_) return;
    running_ = false;

    // Wake all workers so they can see running_ == false
    SDL_LockMutex(req_mutex_);
    SDL_CondBroadcast(req_cond_);
    SDL_UnlockMutex(req_mutex_);

    for (int i = 0; i < N_WORKERS; ++i) {
        if (threads_[i]) {
            SDL_WaitThread(threads_[i], nullptr);
            threads_[i] = nullptr;
        }
    }

    if (req_mutex_)  { SDL_DestroyMutex(req_mutex_);  req_mutex_  = nullptr; }
    if (req_cond_)   { SDL_DestroyCond(req_cond_);     req_cond_   = nullptr; }
    if (done_mutex_) { SDL_DestroyMutex(done_mutex_);  done_mutex_ = nullptr; }
}

bool AssetStream::enqueue(const char* relative, int kind) {
    if (!running_ || !req_mutex_) return false;

    SDL_LockMutex(req_mutex_);

    // Dedupe on path AND kind (a raw and a decode request for the same path are
    // distinct, but two identical requests collapse to one).
    for (int i = req_head_; i != req_tail_; i = (i + 1) % STREAM_QUEUE_SIZE) {
        if (req_kind_[i] == kind && std::strcmp(req_buf_[i], relative) == 0) {
            SDL_UnlockMutex(req_mutex_);
            return true; // already pending
        }
    }

    int next = (req_tail_ + 1) % STREAM_QUEUE_SIZE;
    if (next == req_head_) {
        SDL_UnlockMutex(req_mutex_);
        return false; // queue full
    }

    std::strncpy(req_buf_[req_tail_], relative, 255);
    req_buf_[req_tail_][255] = '\0';
    req_kind_[req_tail_] = kind;
    req_tail_ = next;

    SDL_CondBroadcast(req_cond_);
    SDL_UnlockMutex(req_mutex_);
    return true;
}

bool AssetStream::prefetch(const char* relative)        { return enqueue(relative, KIND_RAW); }
bool AssetStream::prefetch_decode(const char* relative) { return enqueue(relative, KIND_DECODE); }
bool AssetStream::prefetch_probe(const char* base, int category) {
    return enqueue(base, category);   // category >= 0 is the probe kind
}

void AssetStream::clear_pending() {
    if (!req_mutex_) return;
    SDL_LockMutex(req_mutex_);
    req_head_ = req_tail_;   // discard everything still queued
    SDL_UnlockMutex(req_mutex_);
}

// ── Multi-extension prefetch helpers ─────────────────────────────────────────
// Queue one prefetch per configured extension. AO-SDL fires all of these in
// parallel through its HttpPool; we lean on the N_WORKERS worker threads for
// the same effect. Extensions come from ExtensionsConfig which was loaded
// from <asset_url>/extensions.json (or the built-in defaults).

// Simple base+ext categories (char icons, backgrounds, emote-button icons) go
// through the worker-side sequential probe: one request, ~1 fetch on a uniform
// server (vs. firing every candidate and 404-ing all but one), and the winner is
// decoded off-thread.
int AssetStream::prefetch_charicon(const char* rel_without_ext) {
    return prefetch_probe(rel_without_ext, ExtensionsConfig::CAT_CHARICON) ? 1 : 0;
}

int AssetStream::prefetch_emoticon(const char* rel_without_ext) {
    return prefetch_probe(rel_without_ext, ExtensionsConfig::CAT_EMOTIONS) ? 1 : 0;
}

int AssetStream::prefetch_background(const char* rel_without_ext) {
    return prefetch_probe(rel_without_ext, ExtensionsConfig::CAT_BACKGROUND) ? 1 : 0;
}

int AssetStream::prefetch_image(const char* rel_without_ext) {
    // Emote/preanim sprites have special (a)/(b)-vs-bare path rules, so they
    // can't be a generic base+ext probe — the courtroom builds each candidate and
    // calls prefetch_decode() directly. This generic helper fans the candidates
    // out for any caller with a plain base, decoding each off-thread.
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    char path[256]; int queued = 0;
    for (int i = 0; i < ec.emote_count; ++i) {
        int n = std::snprintf(path, sizeof(path), "%s%s", rel_without_ext, ec.emote[i]);
        if (n > 0 && n < (int)sizeof(path) && prefetch_decode(path)) ++queued;
    }
    return queued;
}

bool AssetStream::poll_done(char* out_path, int out_cap) {
    if (!done_mutex_) return false;
    SDL_LockMutex(done_mutex_);
    if (done_head_ == done_tail_) {
        SDL_UnlockMutex(done_mutex_);
        return false;
    }
    std::strncpy(out_path, done_buf_[done_head_], (size_t)(out_cap - 1));
    out_path[out_cap - 1] = '\0';
    done_head_ = (done_head_ + 1) % STREAM_QUEUE_SIZE;
    SDL_UnlockMutex(done_mutex_);
    return true;
}

// ── Worker ────────────────────────────────────────────────────────────────────

int AssetStream::thread_func(void* userdata) {
    ((AssetStream*)userdata)->run();
    return 0;
}

void AssetStream::run() {
    char rel[256];

    // Per-worker persistent HTTP clients — one per mount tier. A single client
    // would close+reopen its connection every time the primary 404s and we
    // fall back to the secondary CDN (and vice versa), which thrashes the
    // connect-helper-thread pool so hard that libnx runs out of thread
    // handles. Holding one sticky client per host keeps both TLS sessions
    // warm and brings connect-thread churn to essentially zero.
    HttpClient primary_client;
    HttpClient secondary_client;

    while (running_) {
        // Wait for a request
        SDL_LockMutex(req_mutex_);
        while (running_ && req_head_ == req_tail_)
            SDL_CondWait(req_cond_, req_mutex_);

        if (!running_) { SDL_UnlockMutex(req_mutex_); break; }

        std::strncpy(rel, req_buf_[req_head_], 255);
        rel[255] = '\0';
        int kind = req_kind_[req_head_];
        req_head_ = (req_head_ + 1) % STREAM_QUEUE_SIZE;
        SDL_UnlockMutex(req_mutex_);

        int size = 0;
        if (kind >= 0) {
            // Sequential probe: try base+ext (learned format first), stop at the
            // first that exists, decode it off-thread, and record the winner. A
            // 404 falls through to the next ext (try_http_mount remembers it, so a
            // later probe of a sibling asset skips the dead candidate's network).
            auto cat = (ExtensionsConfig::Category)kind;
            int order[ExtensionsConfig::MAX_EXTS];
            int n = ExtensionsConfig::probe_order(cat, order, ExtensionsConfig::MAX_EXTS);
            char path[256];
            for (int k = 0; k < n; ++k) {
                int pn = std::snprintf(path, sizeof(path), "%s%s",
                                       rel, ExtensionsConfig::ext_at(cat, order[k]));
                if (pn <= 0 || pn >= (int)sizeof(path)) continue;
                if (AssetManager::has_prefetch(path)) break;   // already staged by a sibling
                uint8_t* d = AssetManager::fetch_bytes_with_clients(
                    path, &size, primary_client, secondary_client);
                if (d) {
                    if (decode_and_store(path, d, size)) SDL_free(d);
                    else AssetManager::store_prefetch(path, d, size);   // keep bytes
                    ExtensionsConfig::note(cat, order[k]);
                    break;
                }
            }
        } else {
            // Raw bytes, or fetch-the-exact-path-and-decode (kind == KIND_DECODE).
            uint8_t* data = AssetManager::fetch_bytes_with_clients(
                rel, &size, primary_client, secondary_client);
            if (data) {
                if (kind == KIND_DECODE && decode_and_store(rel, data, size)) SDL_free(data);
                else AssetManager::store_prefetch(rel, data, size);  // AssetManager owns it
            }
        }
        // No else-log: most prefetch candidates 404 by design, and stderr is
        // unbuffered to SD — logging each one serialised the workers on slow SD
        // writes and dominated cold-load time.

        // Signal done
        SDL_LockMutex(done_mutex_);
        int next = (done_tail_ + 1) % STREAM_QUEUE_SIZE;
        if (next != done_head_) {
            std::strncpy(done_buf_[done_tail_], rel, 255);
            done_buf_[done_tail_][255] = '\0';
            done_tail_ = next;
        }
        SDL_UnlockMutex(done_mutex_);
    }
}

} // namespace ao
