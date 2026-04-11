#include "asset_stream.hpp"
#include "asset_manager.hpp"
#include "extensions_config.hpp"
#include "../net/http_fetch.hpp"
#include <cstring>
#include <cstdio>

namespace ao {

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

bool AssetStream::prefetch(const char* relative) {
    if (!running_ || !req_mutex_) return false;

    SDL_LockMutex(req_mutex_);

    // Check if already queued
    for (int i = req_head_; i != req_tail_; i = (i + 1) % STREAM_QUEUE_SIZE) {
        if (std::strcmp(req_buf_[i], relative) == 0) {
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
    req_tail_ = next;

    SDL_CondBroadcast(req_cond_);
    SDL_UnlockMutex(req_mutex_);
    return true;
}

// ── Multi-extension prefetch helpers ─────────────────────────────────────────
// Queue one prefetch per configured extension. AO-SDL fires all of these in
// parallel through its HttpPool; we lean on the N_WORKERS worker threads for
// the same effect. Extensions come from ExtensionsConfig which was loaded
// from <asset_url>/extensions.json (or the built-in defaults).

static int queue_variants(AssetStream& s, const char* rel_without_ext,
                          const char exts[][ExtensionsConfig::EXT_LEN],
                          int ext_count) {
    int queued = 0;
    char path[256];
    for (int i = 0; i < ext_count; ++i) {
        int n = std::snprintf(path, sizeof(path), "%s%s",
                              rel_without_ext, exts[i]);
        if (n > 0 && n < (int)sizeof(path) && s.prefetch(path)) ++queued;
    }
    return queued;
}

int AssetStream::prefetch_charicon(const char* rel_without_ext) {
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    return queue_variants(*this, rel_without_ext, ec.charicon, ec.charicon_count);
}

int AssetStream::prefetch_image(const char* rel_without_ext) {
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    return queue_variants(*this, rel_without_ext, ec.emote, ec.emote_count);
}

int AssetStream::prefetch_emoticon(const char* rel_without_ext) {
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    return queue_variants(*this, rel_without_ext, ec.emotions, ec.emotions_count);
}

int AssetStream::prefetch_background(const char* rel_without_ext) {
    const ExtensionsConfig& ec = ExtensionsConfig::get();
    return queue_variants(*this, rel_without_ext, ec.background, ec.background_count);
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
        req_head_ = (req_head_ + 1) % STREAM_QUEUE_SIZE;
        SDL_UnlockMutex(req_mutex_);

        // Fetch bytes (keep-alive HTTP → sdmc: → romfs:)
        int      size = 0;
        uint8_t* data = AssetManager::fetch_bytes_with_clients(
            rel, &size, primary_client, secondary_client);
        if (data) {
            // Store in prefetch cache — AssetManager takes ownership
            AssetManager::store_prefetch(rel, data, size);
        } else {
            std::fprintf(stderr, "[stream] prefetch failed for '%s'\n", rel);
        }

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
