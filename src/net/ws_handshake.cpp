#include "ws_handshake.hpp"
#include <SDL2/SDL_net.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace ao {

// ── SHA-1 (needed only for the WS handshake) ──────────────────────────────────
// Minimal standalone implementation — no external dependency.

static void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
    uint32_t h[5] = {
        0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u
    };

    // Process message in 64-byte blocks
    auto rot = [](uint32_t v, int n) { return (v << n) | (v >> (32 - n)); };

    // Append padding and length
    size_t msg_len = len;
    // We allocate a local buffer large enough for the padded message.
    // Max useful AO handshake key is ~128 bytes; total padded <= 256 bytes.
    uint8_t buf[256] = {};
    if (len > 200) { std::memset(out, 0, 20); return; } // sanity guard
    std::memcpy(buf, data, len);
    buf[len] = 0x80;
    // Zero-pad to 56 bytes mod 64, then append 64-bit big-endian bit-length
    size_t padded = ((len + 9 + 63) / 64) * 64;
    uint64_t bit_len = (uint64_t)msg_len * 8;
    for (int i = 0; i < 8; ++i)
        buf[padded - 8 + i] = (uint8_t)(bit_len >> (56 - 8 * i));

    for (size_t off = 0; off < padded; off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = ((uint32_t)buf[off + i*4    ] << 24) |
                   ((uint32_t)buf[off + i*4 + 1] << 16) |
                   ((uint32_t)buf[off + i*4 + 2] <<  8) |
                   ((uint32_t)buf[off + i*4 + 3]);
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rot(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if      (i < 20) { f=(b&c)|((~b)&d); k=0x5A827999; }
            else if (i < 40) { f=b^c^d;           k=0x6ED9EBA1; }
            else if (i < 60) { f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
            else              { f=b^c^d;           k=0xCA62C1D6; }
            uint32_t temp = rot(a,5)+f+e+k+w[i];
            e=d; d=c; c=rot(b,30); b=a; a=temp;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d; h[4]+=e;
    }

    for (int i = 0; i < 5; ++i) {
        out[i*4+0] = (uint8_t)(h[i] >> 24);
        out[i*4+1] = (uint8_t)(h[i] >> 16);
        out[i*4+2] = (uint8_t)(h[i] >>  8);
        out[i*4+3] = (uint8_t)(h[i]       );
    }
}

// ── Base64 encode ──────────────────────────────────────────────────────────────
static const char B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const uint8_t* in, size_t len, char* out) {
    size_t i = 0, j = 0;
    for (; i + 2 < len; i += 3) {
        out[j++] = B64[ in[i]         >> 2];
        out[j++] = B64[(in[i]   & 3)  << 4 | in[i+1] >> 4];
        out[j++] = B64[(in[i+1] & 15) << 2 | in[i+2] >> 6];
        out[j++] = B64[ in[i+2] & 63];
    }
    if (i < len) {
        out[j++] = B64[in[i] >> 2];
        if (i + 1 < len) {
            out[j++] = B64[(in[i] & 3) << 4 | in[i+1] >> 4];
            out[j++] = B64[(in[i+1] & 15) << 2];
        } else {
            out[j++] = B64[(in[i] & 3) << 4];
            out[j++] = '=';
        }
        out[j++] = '=';
    }
    out[j] = '\0';
}

// ── WebSocket upgrade ─────────────────────────────────────────────────────────
bool ws_upgrade(void* sdl_tcp_socket, const char* host, const char* path) {
    TCPsocket sock = reinterpret_cast<TCPsocket>(sdl_tcp_socket);

    // Generate a random 16-byte key and base64-encode it
    uint8_t raw_key[16];
    // Use SDL_GetTicks + rand as a simple non-crypto source (fine for WS)
    srand((unsigned)SDL_GetTicks());
    for (int i = 0; i < 16; ++i) raw_key[i] = (uint8_t)(rand() & 0xFF);
    char key_b64[32] = {};
    base64_encode(raw_key, 16, key_b64);

    // Build HTTP upgrade request
    char request[512];
    int req_len = std::snprintf(request, sizeof(request),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        path, host, key_b64);

    if (SDLNet_TCP_Send(sock, request, req_len) < req_len) {
        std::fprintf(stderr, "ws_upgrade: send failed\n");
        return false;
    }

    // Read HTTP response (look for \r\n\r\n)
    char resp[1024] = {};
    int  resp_len   = 0;
    while (resp_len < (int)sizeof(resp) - 1) {
        int n = SDLNet_TCP_Recv(sock, resp + resp_len, 1);
        if (n <= 0) {
            std::fprintf(stderr, "ws_upgrade: recv failed\n");
            return false;
        }
        resp_len += n;
        if (resp_len >= 4 &&
            resp[resp_len-4] == '\r' && resp[resp_len-3] == '\n' &&
            resp[resp_len-2] == '\r' && resp[resp_len-1] == '\n')
            break;
    }
    resp[resp_len] = '\0';

    // Check for 101
    if (!std::strstr(resp, "101")) {
        std::fprintf(stderr, "ws_upgrade: server did not return 101\n%.256s\n", resp);
        return false;
    }

    // Validate Sec-WebSocket-Accept
    // Expected = base64(sha1(key + magic))
    const char* magic = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char concat[128];
    std::snprintf(concat, sizeof(concat), "%s%s", key_b64, magic);
    uint8_t digest[20];
    sha1(reinterpret_cast<const uint8_t*>(concat), std::strlen(concat), digest);
    char expected[32] = {};
    base64_encode(digest, 20, expected);

    if (!std::strstr(resp, expected)) {
        std::fprintf(stderr, "ws_upgrade: Sec-WebSocket-Accept mismatch\n");
        return false;
    }

    return true;
}

} // namespace ao
