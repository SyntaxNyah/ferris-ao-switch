#include "connect_pool.hpp"
#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>

#ifdef AO_TLS
#include <mbedtls/error.h>

namespace ao {

// ── Queue model ──────────────────────────────────────────────────────────────
//
// A small fixed pool of persistent worker threads picks Jobs off a
// mutex-guarded FIFO linked list and runs mbedtls_net_connect on each. The
// caller waits on the Job's done semaphore with a timeout. If the timeout
// fires, the caller marks the Job as "detached" and returns failure — the
// worker then cleans up the Job when mbedtls_net_connect eventually
// unblocks (the socket state is moot at that point because the caller
// abandoned it). Using multiple workers means one slow/hung connect
// doesn't serialise the entire pool behind it.

struct Job {
    char                host[256];
    char                port_str[8];
    mbedtls_net_context out_net;
    int                 result   = 0;
    SDL_sem*            done     = nullptr;
    bool                detached = false; // set true by caller on timeout
    Job*                next     = nullptr;
};

// Small fixed pool size. Bounded enough to never blow up libnx's thread
// table (which caps at ~48 concurrent threads on Switch), large enough that
// one slow/hung connect doesn't serialise the entire pool behind it. Four
// workers match AssetStream's concurrent-request shape well — the prefetch
// queue feeds at most two mounts × N=8 workers = 16 outstanding fetches,
// and the sticky HttpClient pattern means each fetch reuses its connection,
// so in steady state we only hit the pool for the initial handshake + the
// occasional reconnect when a mount swaps hosts.
static constexpr int POOL_WORKERS = 4;

static SDL_Thread*   g_workers[POOL_WORKERS] = { nullptr };
static SDL_mutex*    g_mutex       = nullptr;
static SDL_cond*     g_cond        = nullptr;
static Job*          g_head        = nullptr;
static Job*          g_tail        = nullptr;
// 0 = uninitialised, 1 = initialising, 2 = ready. Used with SDL_AtomicCAS
// so that concurrent first-callers from multiple AssetStream worker threads
// race to exactly one initialiser — the losers spin until the winner
// finishes publishing g_mutex / g_cond / g_workers.
static SDL_atomic_t  g_start_state = { 0 };

static int worker_fn(void* /*ud*/) {
    for (;;) {
        SDL_LockMutex(g_mutex);
        while (!g_head) SDL_CondWait(g_cond, g_mutex);
        Job* j = g_head;
        g_head = j->next;
        if (!g_head) g_tail = nullptr;
        j->next = nullptr;
        SDL_UnlockMutex(g_mutex);

        mbedtls_net_init(&j->out_net);
        j->result = mbedtls_net_connect(&j->out_net, j->host, j->port_str,
                                        MBEDTLS_NET_PROTO_TCP);

        // Race-safe handoff. We must check `detached` and do the sem post
        // atomically — if we dropped the lock between the two, the caller's
        // timeout path could set `detached = true` after we read it as false
        // but before we posted, and then neither side would own the Job. By
        // posting while still holding the lock, the caller's timeout path
        // (which also locks) is guaranteed to either (a) observe a pending
        // sem post and claim the Job via SemTryWait, or (b) write detached
        // while we've already committed to the detached branch.
        SDL_LockMutex(g_mutex);
        bool detached = j->detached;
        if (!detached) SDL_SemPost(j->done);
        SDL_UnlockMutex(g_mutex);

        if (detached) {
            // Caller gave up; free everything here.
            if (j->result == 0) mbedtls_net_free(&j->out_net);
            if (j->done) SDL_DestroySemaphore(j->done);
            delete j;
        }
        // else: caller owns `j` from here.
    }
    return 0;
}

static void lazy_start() {
    int s = SDL_AtomicGet(&g_start_state);
    if (s == 2) return;
    if (SDL_AtomicCAS(&g_start_state, 0, 1)) {
        // We won the init race. Publish in dependency order, then flip
        // to state 2 to release any spinning waiters.
        g_mutex = SDL_CreateMutex();
        g_cond  = SDL_CreateCond();
        for (int i = 0; i < POOL_WORKERS; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), "ConnectPool%d", i);
            g_workers[i] = SDL_CreateThread(worker_fn, name, nullptr);
            if (g_workers[i]) SDL_DetachThread(g_workers[i]);
        }
        SDL_AtomicSet(&g_start_state, 2);
    } else {
        // Another thread is mid-init. Spin briefly — init is ~milliseconds.
        while (SDL_AtomicGet(&g_start_state) != 2) SDL_Delay(1);
    }
}

int ConnectPool::connect(const char* host, uint16_t port,
                         mbedtls_net_context* out_net, int timeout_ms) {
    // One-time init (thread-safe via SDL_AtomicCAS in lazy_start). All
    // callers — NetworkThread and every AssetStream HTTP worker — share the
    // same singleton pool of POOL_WORKERS threads.
    lazy_start();
    // Require at least one worker thread. If the first SDL_CreateThread
    // succeeded but later ones failed we still function — just with less
    // parallelism. Only fail hard if every slot is null.
    bool any_worker = false;
    for (int i = 0; i < POOL_WORKERS; ++i)
        if (g_workers[i]) { any_worker = true; break; }
    if (!any_worker) {
        std::fprintf(stderr, "ConnectPool: no worker threads could start\n");
        return -1;
    }

    Job* j = new Job();
    std::strncpy(j->host, host, sizeof(j->host) - 1);
    std::snprintf(j->port_str, sizeof(j->port_str), "%u", (unsigned)port);
    j->done = SDL_CreateSemaphore(0);

    SDL_LockMutex(g_mutex);
    if (g_tail) g_tail->next = j;
    else        g_head       = j;
    g_tail = j;
    SDL_CondSignal(g_cond);
    SDL_UnlockMutex(g_mutex);

    if (SDL_SemWaitTimeout(j->done, (Uint32)timeout_ms) != 0) {
        // Timeout path. Grab the lock and try the semaphore one more time
        // under it. This closes the last race with the worker: the worker
        // posts + checks `detached` while holding the lock, so either the
        // post is visible to our SemTryWait here (we claim the Job) or the
        // worker will see our `detached = true` below and free everything
        // itself. There is no interleaving where both sides skip the Job.
        SDL_LockMutex(g_mutex);
        if (SDL_SemTryWait(j->done) == 0) {
            // The worker completed between our timeout and our lock — we
            // own `j` after all. Fall through to the success path.
            SDL_UnlockMutex(g_mutex);
        } else {
            j->detached = true;
            SDL_UnlockMutex(g_mutex);
            std::fprintf(stderr, "ConnectPool: connect to %s:%u timed out\n",
                         host, port);
            return -1;
        }
    }

    int ret = j->result;
    if (ret == 0) {
        *out_net = j->out_net;
        mbedtls_net_init(&j->out_net); // prevent double-free from delete
    }
    SDL_DestroySemaphore(j->done);
    delete j;
    return ret;
}

} // namespace ao

#endif // AO_TLS
