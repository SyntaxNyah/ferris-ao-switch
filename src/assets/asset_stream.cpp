#include "asset_stream.hpp"
#include "asset_manager.hpp"
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
    thread_     = SDL_CreateThread(thread_func, "AssetStream", this);
}

void AssetStream::stop() {
    if (!running_) return;
    running_ = false;

    // Wake the worker so it can see running_ == false
    SDL_LockMutex(req_mutex_);
    SDL_CondSignal(req_cond_);
    SDL_UnlockMutex(req_mutex_);

    if (thread_) {
        SDL_WaitThread(thread_, nullptr);
        thread_ = nullptr;
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

    SDL_CondSignal(req_cond_);
    SDL_UnlockMutex(req_mutex_);
    return true;
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

        // Fetch bytes (HTTP → sdmc: → romfs:)
        int      size = 0;
        uint8_t* data = AssetManager::fetch_bytes(rel, &size);
        if (data) {
            // Store in prefetch cache — AssetManager takes ownership
            AssetManager::store_prefetch(rel, data, size);
            std::fprintf(stdout, "[stream] prefetched '%s' (%d bytes)\n", rel, size);
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
