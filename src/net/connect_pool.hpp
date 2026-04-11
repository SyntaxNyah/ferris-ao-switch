#pragma once
#include <cstdint>

// libnx has a small global thread table. Every time we spawned a per-call
// SDL_Thread for the blocking mbedtls_net_connect() wrapper, that slot was
// tied up until the detached thread returned. Under AssetStream load (8
// workers × two mounts × 2048 prefetches × reconnect churn when keep-alive
// is dropped), we'd eventually hit `CreateThread() = LimitReached` and all
// subsequent connects would fail — producing the "char icons stopped loading
// mid-way" symptom.
//
// ConnectPool is a single persistent SDL_Thread that processes connect jobs
// from a queue. Callers block on a per-job semaphore with a 10-second
// timeout, matching the old wrapper's behaviour exactly. On timeout the job
// is flagged "detached" and the worker cleans it up when the OS connect()
// eventually returns. Thread table usage is O(1) no matter how many
// concurrent callers hit it.

#ifdef AO_TLS
#include <mbedtls/net_sockets.h>
#endif

namespace ao {

class ConnectPool {
public:
    // Blocking connect with a timeout. On success, `out_net` is populated
    // with a valid mbedtls_net_context that the caller owns and must
    // eventually close via mbedtls_net_free. Returns 0 on success, a
    // negative mbedtls error code on connect failure, or -1 on timeout /
    // enqueue failure.
#ifdef AO_TLS
    static int connect(const char* host, uint16_t port,
                       mbedtls_net_context* out_net, int timeout_ms);
#endif
};

} // namespace ao
