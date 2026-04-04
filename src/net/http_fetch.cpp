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

// ── Helper ────────────────────────────────────────────────────────────────────

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

// ── Buffered reader — plain TCP ───────────────────────────────────────────────
struct RecvBuf {
    TCPsocket        sock;
    SDLNet_SocketSet set;
    int              timeout_ms;
    char             buf[8192];
    int              pos    = 0;
    int              filled = 0;

    bool refill() {
        pos = filled = 0;
        if (SDLNet_CheckSockets(set, timeout_ms) <= 0) return false;
        int n = SDLNet_TCP_Recv(sock, buf, (int)sizeof(buf));
        if (n <= 0) return false;
        filled = n;
        return true;
    }
    int read_line(char* out, int cap) {
        int len = 0;
        while (len < cap - 1) {
            if (pos >= filled && !refill()) return -1;
            char c = buf[pos++];
            if (c == '\n') {
                if (len > 0 && out[len - 1] == '\r') --len;
                out[len] = '\0';
                return len;
            }
            out[len++] = c;
        }
        return -1;
    }
    int read_exact(char* dst, int want) {
        int got = 0;
        while (got < want) {
            if (pos >= filled && !refill()) break;
            int avail = filled - pos;
            int take  = (avail < want - got) ? avail : (want - got);
            std::memcpy(dst + got, buf + pos, (size_t)take);
            pos += take;
            got += take;
        }
        return got;
    }
    // Read until server closes connection. Returns SDL_malloc'd buf or nullptr.
    uint8_t* read_until_close(int* out_size) {
        int   cap  = 65536;
        auto* body = (uint8_t*)SDL_malloc(cap);
        if (!body) return nullptr;
        int total = 0;
        // drain already-buffered bytes
        if (pos < filled) {
            int avail = filled - pos;
            std::memcpy(body, buf + pos, (size_t)avail);
            total = avail;
            pos = filled = 0;
        }
        while (total < HTTP_MAX_BODY) {
            if (SDLNet_CheckSockets(set, timeout_ms) <= 0) break;
            if (total + 65536 > cap) {
                cap *= 2;
                auto* nb = (uint8_t*)SDL_realloc(body, cap);
                if (!nb) break;
                body = nb;
            }
            int n = SDLNet_TCP_Recv(sock, body + total, 65536);
            if (n <= 0) break;
            total += n;
        }
        if (total > 0) { *out_size = total; return body; }
        SDL_free(body); return nullptr;
    }
};

// ── Chunked transfer decoder (plain TCP) ─────────────────────────────────────

static bool read_chunked(RecvBuf& rb, uint8_t** out, int* out_size) {
    int  cap  = 65536;
    auto* body = (uint8_t*)SDL_malloc(cap);
    if (!body) return false;
    int total = 0;
    char line[64];

    while (true) {
        if (rb.read_line(line, sizeof(line)) < 0) { SDL_free(body); return false; }
        int chunk_size = (int)std::strtol(line, nullptr, 16);
        if (chunk_size == 0) break;
        if (chunk_size < 0 || total + chunk_size > HTTP_MAX_BODY) { SDL_free(body); return false; }
        while (total + chunk_size > cap) {
            cap *= 2;
            auto* nb = (uint8_t*)SDL_realloc(body, cap);
            if (!nb) { SDL_free(body); return false; }
            body = nb;
        }
        int got = rb.read_exact((char*)(body + total), chunk_size);
        if (got != chunk_size) { SDL_free(body); return false; }
        total += chunk_size;
        rb.read_line(line, sizeof(line)); // trailing CRLF
    }
    *out = body; *out_size = total;
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
        std::snprintf(res.error, sizeof(res.error), "DNS failed: %s", SDLNet_GetError());
        std::fprintf(stderr, "http_get: %s\n", res.error);
        return res;
    }

    TCPsocket sock = SDLNet_TCP_Open(&ip);
    if (!sock) {
        std::snprintf(res.error, sizeof(res.error), "TCP connect failed: %s", SDLNet_GetError());
        std::fprintf(stderr, "http_get: %s\n", res.error);
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
        std::snprintf(res.error, sizeof(res.error), "Send failed");
        SDLNet_FreeSocketSet(set);
        SDLNet_TCP_Close(sock);
        return res;
    }

    RecvBuf rb;
    rb.sock       = sock;
    rb.set        = set;
    rb.timeout_ms = HTTP_TIMEOUT_MS;
    rb.pos        = 0;
    rb.filled     = 0;

    // Read status line: "HTTP/1.x NNN reason"
    char line[512];
    if (rb.read_line(line, sizeof(line)) < 0) {
        std::snprintf(res.error, sizeof(res.error), "Timeout reading response");
        SDLNet_FreeSocketSet(set);
        SDLNet_TCP_Close(sock);
        return res;
    }

    int status_code = 0;
    if (std::sscanf(line, "HTTP/%*s %d", &status_code) != 1 || status_code != 200) {
        std::snprintf(res.error, sizeof(res.error), "HTTP %d", status_code);
        SDLNet_FreeSocketSet(set);
        SDLNet_TCP_Close(sock);
        return res;
    }

    // Read headers until blank line
    int  content_length = -1;
    bool chunked        = false;
    while (true) {
        int n = rb.read_line(line, sizeof(line));
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
        if (read_chunked(rb, &body, &body_size)) {
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
                int got = rb.read_exact((char*)body, content_length);
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
        int body_size = 0;
        uint8_t* body = rb.read_until_close(&body_size);
        if (body) {
            res.data = body;
            res.size = body_size;
            res.ok   = true;
        }
    }

    SDLNet_FreeSocketSet(set);
    SDLNet_TCP_Close(sock);
    return res;
}

// ── https_get ─────────────────────────────────────────────────────────────────

#ifdef AO_TLS

// ── Buffered reader — TLS ─────────────────────────────────────────────────────
struct TlsBuf {
    TlsConn& tls;
    int      timeout_ms;
    char     buf[8192];
    int      pos    = 0;
    int      filled = 0;

    bool refill() {
        pos = filled = 0;
        uint32_t deadline = SDL_GetTicks() + (uint32_t)timeout_ms;
        while (true) {
            if ((int32_t)(deadline - SDL_GetTicks()) <= 0) return false;
            int n = tls.recv(buf, (int)sizeof(buf));
            if (n > 0) { filled = n; return true; }
            if (n < 0) return false;
            tls.poll(1);
        }
    }
    int read_line(char* out, int cap) {
        int len = 0;
        while (len < cap - 1) {
            if (pos >= filled && !refill()) return -1;
            char c = buf[pos++];
            if (c == '\n') {
                if (len > 0 && out[len - 1] == '\r') --len;
                out[len] = '\0';
                return len;
            }
            out[len++] = c;
        }
        return -1;
    }
    int read_exact(char* dst, int want) {
        int got = 0;
        while (got < want) {
            if (pos >= filled && !refill()) break;
            int avail = filled - pos;
            int take  = (avail < want - got) ? avail : (want - got);
            std::memcpy(dst + got, buf + pos, (size_t)take);
            pos += take;
            got += take;
        }
        return got;
    }
    uint8_t* read_until_close(int* out_size) {
        int   cap  = 65536;
        auto* body = (uint8_t*)SDL_malloc(cap);
        if (!body) return nullptr;
        int total = 0;
        if (pos < filled) {
            int avail = filled - pos;
            std::memcpy(body, buf + pos, (size_t)avail);
            total = avail;
            pos = filled = 0;
        }
        uint32_t deadline = SDL_GetTicks() + (uint32_t)timeout_ms;
        while (total < HTTP_MAX_BODY) {
            if ((int32_t)(deadline - SDL_GetTicks()) <= 0) break;
            if (total + 65536 > cap) {
                cap *= 2;
                auto* nb = (uint8_t*)SDL_realloc(body, cap);
                if (!nb) break;
                body = nb;
            }
            int n = tls.recv(body + total, 65536);
            if (n < 0) break;
            if (n > 0) total += n;
            else tls.poll(1);
        }
        if (total > 0) { *out_size = total; return body; }
        SDL_free(body); return nullptr;
    }
};

static bool tls_read_chunked(TlsBuf& tb, uint8_t** out, int* out_size) {
    int  cap  = 65536;
    auto* buf = (uint8_t*)SDL_malloc(cap);
    if (!buf) return false;
    int total = 0;
    char line[64];

    while (true) {
        if (tb.read_line(line, sizeof(line)) < 0) { SDL_free(buf); return false; }
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
        int got = tb.read_exact((char*)(buf + total), chunk_size);
        if (got != chunk_size) { SDL_free(buf); return false; }
        total += chunk_size;
        tb.read_line(line, sizeof(line)); // trailing CRLF
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
        std::snprintf(res.error, sizeof(res.error), "TLS connect to %s failed", pu.host);
        std::fprintf(stderr, "https_get: %s\n", res.error);
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
        std::snprintf(res.error, sizeof(res.error), "TLS send failed");
        tls.close();
        return res;
    }

    TlsBuf tb = {tls, HTTP_TIMEOUT_MS};
    tb.pos    = 0;
    tb.filled = 0;

    // Read status line
    char line[512];
    if (tb.read_line(line, sizeof(line)) < 0) {
        std::snprintf(res.error, sizeof(res.error), "TLS timeout reading response");
        tls.close();
        return res;
    }

    int status_code = 0;
    if (std::sscanf(line, "HTTP/%*s %d", &status_code) != 1 || status_code != 200) {
        std::snprintf(res.error, sizeof(res.error), "HTTP %d", status_code);
        tls.close();
        return res;
    }

    // Read headers until blank line
    int  content_length = -1;
    bool chunked        = false;
    while (true) {
        int n = tb.read_line(line, sizeof(line));
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
        if (tls_read_chunked(tb, &body, &body_size)) {
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
                int got = tb.read_exact((char*)body, content_length);
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
        int body_size = 0;
        uint8_t* body = tb.read_until_close(&body_size);
        if (body) {
            res.data = body;
            res.size = body_size;
            res.ok   = true;
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
