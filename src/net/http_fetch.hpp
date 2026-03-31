#pragma once
#include <SDL2/SDL.h>
#include <cstdint>

namespace ao {

// Simple synchronous HTTP/1.1 GET over plain TCP (SDL_net).
// Also supports HTTPS via TlsConn when compiled with AO_TLS.
//
// Supports:
//   - Content-Length body framing
//   - Transfer-Encoding: chunked
//   - Connection: close read-until-EOF fallback
//
// Limits:
//   - HTTP_MAX_BODY (32 MB) — hard cap; returns ok=false if exceeded
//   - HTTP_TIMEOUT_MS (8 s) — per-operation socket timeout
//
// Thread safety: safe to call from any thread concurrently.
// SDL_net must already be initialised before calling http_get().

static constexpr int HTTP_MAX_BODY   = 32 * 1024 * 1024;
static constexpr int HTTP_TIMEOUT_MS = 8000;

struct HttpResult {
    uint8_t* data = nullptr;  // SDL_malloc'd; caller must call free()
    int      size = 0;
    bool     ok   = false;

    void free() {
        if (data) { SDL_free(data); data = nullptr; }
        size = 0;
        ok   = false;
    }
};

// Blocking HTTP GET. Returns an HttpResult; call result.free() when done.
// url must begin with "http://". Returns ok=false on non-200, timeout, or error.
HttpResult http_get(const char* url);

// Blocking HTTPS GET via TlsConn (mbedtls). Guarded by AO_TLS.
// url must begin with "https://". Returns ok=false when AO_TLS not defined.
HttpResult https_get(const char* url);

} // namespace ao
