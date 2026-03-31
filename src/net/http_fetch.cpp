#include "http_fetch.hpp"
#include "tls_conn.hpp"
#include <SDL2/SDL_net.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace ao {

// ── URL parser ────────────────────────────────────────────────────────────────

struct ParsedUrl {
    char host[256];
    char path[512];
    int  port;
    bool valid;
    bool tls;   // true if https://
};

static ParsedUrl parse_url(const char* url) {
    ParsedUrl r = {};
    const char* p;
    if (std::strncmp(url, "https://", 8) == 0) {
        r.tls = true;
        p = url + 8;
    } else if (std::strncmp(url, "http://", 7) == 0) {
        r.tls = false;
        p = url + 7;
    } else {
        return r;
    }

    const char* slash = std::strchr(p, '/');
    const char* colon = std::strchr(p, ':');
    // Only treat the colon as a port separator if it comes before the first slash
    if (colon && slash && colon > slash) colon = nullptr;

    int host_end = slash ? (int)(slash - p) : (int)std::strlen(p);

    if (colon) {
        int hlen = (int)(colon - p);
        if (hlen <= 0 || hlen >= (int)sizeof(r.host)) return r;
        std::memcpy(r.host, p, hlen);
        r.host[hlen] = '\0';
        r.port = std::atoi(colon + 1);
    } else {
        if (host_end <= 0 || host_end >= (int)sizeof(r.host)) return r;
        std::memcpy(r.host, p, host_end);
        r.host[host_end] = '\0';
        r.port = r.tls ? 443 : 80;
    }

    if (slash) {
        std::strncpy(r.path, slash, sizeof(r.path) - 1);
    } else {
        r.path[0] = '/';
        r.path[1] = '\0';
    }

    r.valid = (r.host[0] != '\0' && r.port > 0 && r.port < 65536);
    return r;
}

// ── Socket helpers ────────────────────────────────────────────────────────────

// Read exactly `want` bytes. Returns bytes actually read (< want on timeout/close).
static int recv_exact(TCPsocket sock, SDLNet_SocketSet set,
                      char* buf, int want, int timeout_ms) {
    int got = 0;
    while (got < want) {
        if (SDLNet_CheckSockets(set, timeout_ms) <= 0) break;
        int n = SDLNet_TCP_Recv(sock, buf + got, want - got);
        if (n <= 0) break;
        got += n;
    }
    return got;
}

// Read one line (up to CRLF). Strips the CRLF. Returns length or -1 on error.
static int recv_line(TCPsocket sock, SDLNet_SocketSet set,
                     char* buf, int cap, int timeout_ms) {
    int len = 0;
    while (len < cap - 1) {
        if (SDLNet_CheckSockets(set, timeout_ms) <= 0) return -1;
        char c;
        if (SDLNet_TCP_Recv(sock, &c, 1) != 1) return -1;
        if (c == '\n') {
            if (len > 0 && buf[len - 1] == '\r') --len;
            buf[len] = '\0';
            return len;
        }
        buf[len++] = c;
    }
    return -1;
}

// Case-insensitive prefix check; sets *val to the trimmed value after the colon.
static bool header_match(const char* line, const char* name, const char** val) {
    size_t nlen = std::strlen(name);
    for (size_t i = 0; i < nlen; ++i) {
        if (std::tolower((unsigned char)line[i]) !=
            std::tolower((unsigned char)name[i])) return false;
    }
    const char* p = line + nlen;
    if (*p != ':') return false;
    ++p;
    while (*p == ' ') ++p;
    *val = p;
    return true;
}

// ── Chunked transfer decoder ──────────────────────────────────────────────────

static bool read_chunked(TCPsocket sock, SDLNet_SocketSet set,
                         uint8_t** out, int* out_size, int timeout_ms) {
    int  cap  = 65536;
    auto* buf = (uint8_t*)SDL_malloc(cap);
    if (!buf) return false;
    int total = 0;
    char line[64];

    while (true) {
        if (recv_line(sock, set, line, sizeof(line), timeout_ms) < 0) {
            SDL_free(buf); return false;
        }
        int chunk_size = (int)std::strtol(line, nullptr, 16);
        if (chunk_size == 0) break;
        if (chunk_size < 0 || total + chunk_size > HTTP_MAX_BODY) {
            SDL_free(buf); return false;
        }
        while (total + chunk_size > cap) {
            cap *= 2;
            auto* nb = (uint8_t*)SDL_realloc(buf, cap);
            if (!nb) { SDL_free(buf); return false; }
            buf = nb;
        }
        int got = recv_exact(sock, set, (char*)(buf + total), chunk_size, timeout_ms);
        if (got != chunk_size) { SDL_free(buf); return false; }
        total += chunk_size;
        recv_line(sock, set, line, sizeof(line), timeout_ms); // trailing CRLF
    }
    *out      = buf;
    *out_size = total;
    return true;
}

// ── http_get ──────────────────────────────────────────────────────────────────

HttpResult http_get(const char* url) {
    // Dispatch to https_get for https:// URLs
    if (std::strncmp(url, "https://", 8) == 0)
        return https_get(url);

    HttpResult res = {};

    ParsedUrl pu = parse_url(url);
    if (!pu.valid) {
        std::fprintf(stderr, "http_get: invalid or non-HTTP URL '%s'\n", url);
        return res;
    }

    IPaddress ip;
    if (SDLNet_ResolveHost(&ip, pu.host, (Uint16)pu.port) != 0) {
        std::fprintf(stderr, "http_get: DNS failed for '%s': %s\n",
            pu.host, SDLNet_GetError());
        return res;
    }

    TCPsocket sock = SDLNet_TCP_Open(&ip);
    if (!sock) {
        std::fprintf(stderr, "http_get: connect %s:%d failed: %s\n",
            pu.host, pu.port, SDLNet_GetError());
        return res;
    }

    SDLNet_SocketSet set = SDLNet_AllocSocketSet(1);
    SDLNet_TCP_AddSocket(set, sock);

    // Send request
    char req[1024];
    int req_len = std::snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: ferris-ao-switch/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        pu.path, pu.host);

    if (SDLNet_TCP_Send(sock, req, req_len) != req_len) {
        std::fprintf(stderr, "http_get: send failed for '%s'\n", url);
        SDLNet_FreeSocketSet(set);
        SDLNet_TCP_Close(sock);
        return res;
    }

    // Read status line: "HTTP/1.x NNN reason"
    char line[512];
    if (recv_line(sock, set, line, sizeof(line), HTTP_TIMEOUT_MS) < 0) {
        std::fprintf(stderr, "http_get: timeout reading status for '%s'\n", url);
        SDLNet_FreeSocketSet(set);
        SDLNet_TCP_Close(sock);
        return res;
    }

    int status_code = 0;
    if (std::sscanf(line, "HTTP/%*s %d", &status_code) != 1 || status_code != 200) {
        std::fprintf(stderr, "http_get: HTTP %d for '%s'\n", status_code, url);
        SDLNet_FreeSocketSet(set);
        SDLNet_TCP_Close(sock);
        return res;
    }

    // Read headers until blank line
    int  content_length = -1;
    bool chunked        = false;
    while (true) {
        int n = recv_line(sock, set, line, sizeof(line), HTTP_TIMEOUT_MS);
        if (n < 0) { SDLNet_FreeSocketSet(set); SDLNet_TCP_Close(sock); return res; }
        if (n == 0) break; // end of headers

        const char* val;
        if (header_match(line, "content-length", &val))
            content_length = std::atoi(val);
        if (header_match(line, "transfer-encoding", &val))
            if (std::strstr(val, "chunked")) chunked = true;
    }

    // Read body
    if (chunked) {
        uint8_t* body = nullptr;
        int      body_size = 0;
        if (read_chunked(sock, set, &body, &body_size, HTTP_TIMEOUT_MS)) {
            res.data = body;
            res.size = body_size;
            res.ok   = true;
        } else {
            std::fprintf(stderr, "http_get: chunked decode failed for '%s'\n", url);
        }
    } else if (content_length > 0) {
        if (content_length > HTTP_MAX_BODY) {
            std::fprintf(stderr, "http_get: body too large (%d B) for '%s'\n",
                content_length, url);
        } else {
            auto* body = (uint8_t*)SDL_malloc(content_length);
            if (body) {
                int got = recv_exact(sock, set, (char*)body, content_length, HTTP_TIMEOUT_MS);
                if (got == content_length) {
                    res.data = body;
                    res.size = content_length;
                    res.ok   = true;
                } else {
                    std::fprintf(stderr, "http_get: short body for '%s' (%d/%d)\n",
                        url, got, content_length);
                    SDL_free(body);
                }
            }
        }
    } else {
        // No framing — read until server closes connection
        int      cap  = 65536;
        auto*    body = (uint8_t*)SDL_malloc(cap);
        int      total = 0;
        if (body) {
            while (total < HTTP_MAX_BODY) {
                if (SDLNet_CheckSockets(set, HTTP_TIMEOUT_MS) <= 0) break;
                if (total + 4096 > cap) {
                    cap *= 2;
                    auto* nb = (uint8_t*)SDL_realloc(body, cap);
                    if (!nb) break;
                    body = nb;
                }
                int n = SDLNet_TCP_Recv(sock, body + total, 4096);
                if (n <= 0) break;
                total += n;
            }
            if (total > 0) {
                res.data = body;
                res.size = total;
                res.ok   = true;
            } else {
                SDL_free(body);
            }
        }
    }

    SDLNet_FreeSocketSet(set);
    SDLNet_TCP_Close(sock);
    return res;
}

// ── https_get ─────────────────────────────────────────────────────────────────

#ifdef AO_TLS

// TLS recv helper: read from TlsConn using a spin-wait on WANT_READ.
// Returns bytes read, or <0 on error/close.
static int tls_recv_exact(TlsConn& tls, char* buf, int want, int timeout_ms) {
    int got = 0;
    uint32_t deadline = SDL_GetTicks() + (uint32_t)timeout_ms;
    while (got < want) {
        uint32_t now = SDL_GetTicks();
        if ((int32_t)(deadline - now) <= 0) break;
        if (!tls.poll(1)) continue;
        int n = tls.recv(buf + got, want - got);
        if (n < 0) return -1;
        got += n;
    }
    return got;
}

// Read one line (terminated by LF) from TlsConn.
static int tls_recv_line(TlsConn& tls, char* buf, int cap, int timeout_ms) {
    int len = 0;
    uint32_t deadline = SDL_GetTicks() + (uint32_t)timeout_ms;
    while (len < cap - 1) {
        uint32_t now = SDL_GetTicks();
        if ((int32_t)(deadline - now) <= 0) return -1;
        char c;
        int n = tls.recv(&c, 1);
        if (n == 0) { tls.poll(1); continue; }
        if (n < 0) return -1;
        if (c == '\n') {
            if (len > 0 && buf[len - 1] == '\r') --len;
            buf[len] = '\0';
            return len;
        }
        buf[len++] = c;
    }
    return -1;
}

static bool tls_read_chunked(TlsConn& tls, uint8_t** out, int* out_size, int timeout_ms) {
    int  cap  = 65536;
    auto* buf = (uint8_t*)SDL_malloc(cap);
    if (!buf) return false;
    int total = 0;
    char line[64];

    while (true) {
        if (tls_recv_line(tls, line, sizeof(line), timeout_ms) < 0) {
            SDL_free(buf); return false;
        }
        int chunk_size = (int)std::strtol(line, nullptr, 16);
        if (chunk_size == 0) break;
        if (chunk_size < 0 || total + chunk_size > HTTP_MAX_BODY) {
            SDL_free(buf); return false;
        }
        while (total + chunk_size > cap) {
            cap *= 2;
            auto* nb = (uint8_t*)SDL_realloc(buf, cap);
            if (!nb) { SDL_free(buf); return false; }
            buf = nb;
        }
        int got = tls_recv_exact(tls, (char*)(buf + total), chunk_size, timeout_ms);
        if (got != chunk_size) { SDL_free(buf); return false; }
        total += chunk_size;
        tls_recv_line(tls, line, sizeof(line), timeout_ms); // trailing CRLF
    }
    *out      = buf;
    *out_size = total;
    return true;
}

HttpResult https_get(const char* url) {
    HttpResult res = {};

    ParsedUrl pu = parse_url(url);
    if (!pu.valid || !pu.tls) {
        std::fprintf(stderr, "https_get: invalid or non-HTTPS URL '%s'\n", url);
        return res;
    }

    TlsConn tls;
    if (!tls.connect(pu.host, (uint16_t)pu.port)) {
        std::fprintf(stderr, "https_get: TLS connect to %s:%d failed\n", pu.host, pu.port);
        return res;
    }

    // Send HTTP request
    char req[1024];
    int req_len = std::snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: ferris-ao-switch/1.0\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n"
        "\r\n",
        pu.path, pu.host);

    int sent = tls.send(req, req_len);
    if (sent != req_len) {
        std::fprintf(stderr, "https_get: send failed for '%s'\n", url);
        tls.close();
        return res;
    }

    // Read status line
    char line[512];
    if (tls_recv_line(tls, line, sizeof(line), HTTP_TIMEOUT_MS) < 0) {
        std::fprintf(stderr, "https_get: timeout reading status for '%s'\n", url);
        tls.close();
        return res;
    }

    int status_code = 0;
    if (std::sscanf(line, "HTTP/%*s %d", &status_code) != 1 || status_code != 200) {
        std::fprintf(stderr, "https_get: HTTP %d for '%s'\n", status_code, url);
        tls.close();
        return res;
    }

    // Read headers until blank line
    int  content_length = -1;
    bool chunked        = false;
    while (true) {
        int n = tls_recv_line(tls, line, sizeof(line), HTTP_TIMEOUT_MS);
        if (n < 0) { tls.close(); return res; }
        if (n == 0) break;

        const char* val;
        if (header_match(line, "content-length", &val))
            content_length = std::atoi(val);
        if (header_match(line, "transfer-encoding", &val))
            if (std::strstr(val, "chunked")) chunked = true;
    }

    // Read body
    if (chunked) {
        uint8_t* body = nullptr;
        int      body_size = 0;
        if (tls_read_chunked(tls, &body, &body_size, HTTP_TIMEOUT_MS)) {
            res.data = body;
            res.size = body_size;
            res.ok   = true;
        } else {
            std::fprintf(stderr, "https_get: chunked decode failed for '%s'\n", url);
        }
    } else if (content_length > 0) {
        if (content_length > HTTP_MAX_BODY) {
            std::fprintf(stderr, "https_get: body too large (%d B) for '%s'\n",
                content_length, url);
        } else {
            auto* body = (uint8_t*)SDL_malloc(content_length);
            if (body) {
                int got = tls_recv_exact(tls, (char*)body, content_length, HTTP_TIMEOUT_MS);
                if (got == content_length) {
                    res.data = body;
                    res.size = content_length;
                    res.ok   = true;
                } else {
                    std::fprintf(stderr, "https_get: short body for '%s' (%d/%d)\n",
                        url, got, content_length);
                    SDL_free(body);
                }
            }
        }
    } else {
        // No framing — read until server closes
        int   cap  = 65536;
        auto* body = (uint8_t*)SDL_malloc(cap);
        int   total = 0;
        if (body) {
            uint32_t deadline = SDL_GetTicks() + HTTP_TIMEOUT_MS;
            while (total < HTTP_MAX_BODY) {
                uint32_t now = SDL_GetTicks();
                if ((int32_t)(deadline - now) <= 0) break;
                if (!tls.poll(1)) continue;
                if (total + 4096 > cap) {
                    cap *= 2;
                    auto* nb = (uint8_t*)SDL_realloc(body, cap);
                    if (!nb) break;
                    body = nb;
                }
                int n = tls.recv(body + total, 4096);
                if (n < 0) break;
                if (n > 0) total += n;
            }
            if (total > 0) {
                res.data = body;
                res.size = total;
                res.ok   = true;
            } else {
                SDL_free(body);
            }
        }
    }

    tls.close();
    return res;
}

#else // AO_TLS not defined — stub

HttpResult https_get(const char* url) {
    (void)url;
    std::fprintf(stderr, "https_get: this build was compiled without AO_TLS\n");
    HttpResult res = {};
    return res;
}

#endif // AO_TLS

} // namespace ao
