#include "tls_conn.hpp"
#include <cstdio>
#include <cstring>
#include <SDL2/SDL.h>

#ifdef AO_TLS
// ── Full mbedtls implementation ───────────────────────────────────────────────
#include <mbedtls/error.h>
#include <sys/socket.h>
#include <sys/time.h>

namespace ao {

static void tls_log_err(const char* tag, int ret) {
    char msg[128];
    mbedtls_strerror(ret, msg, sizeof(msg));
    std::fprintf(stderr, "TLS [%s]: %s (−0x%04X)\n", tag, msg, (unsigned)(-ret));
}

// ── RawConn — plain non-blocking TCP via mbedtls_net ─────────────────────────

bool RawConn::connect(const char* host, uint16_t port) {
    mbedtls_net_init(&net);
    char port_str[8];
    std::snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
    int ret = mbedtls_net_connect(&net, host, port_str, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) { tls_log_err("raw_connect", ret); return false; }
    // Use SO_RCVTIMEO (5 ms) instead of a non-blocking socket.
    // On Ryujinx, mbedtls_net_recv on a non-blocking socket returns WANT_READ
    // even when data is buffered (Ryujinx select() is unreliable).
    // With SO_RCVTIMEO the recv call blocks up to 5 ms then returns EAGAIN,
    // which mbedtls maps to WANT_READ → 0 in RawConn::recv.
    // This lets ws_loop check the outgoing queue every 5 ms without select().
    struct timeval tv;
    tv.tv_sec  = 0;
    tv.tv_usec = 5000; // 5 ms
    setsockopt(net.fd, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tv), sizeof(tv));
    return true;
}

void RawConn::close() {
    mbedtls_net_free(&net);
}

int RawConn::send(const void* data, int len) {
    int ret;
    do {
        ret = (int)mbedtls_net_send(&net,
            reinterpret_cast<const unsigned char*>(data), (size_t)len);
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            mbedtls_net_poll(&net, MBEDTLS_NET_POLL_WRITE, 1);
    } while (ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    return ret;
}

int RawConn::recv(void* buf, int cap) {
    int ret = (int)mbedtls_net_recv(&net,
        reinterpret_cast<unsigned char*>(buf), (size_t)cap);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ) return 0;
    return ret;
}

bool RawConn::poll(int timeout_ms) {
    return mbedtls_net_poll(&net, MBEDTLS_NET_POLL_READ, (uint32_t)timeout_ms) > 0;
}

// ── TlsConn ───────────────────────────────────────────────────────────────────

bool TlsConn::connect(const char* host, uint16_t port) {
    mbedtls_net_init(&net);
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    // Seed the CSPRNG
    const char* pers = "ferris-ao-tls";
    int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                     (const unsigned char*)pers, std::strlen(pers));
    if (ret != 0) { tls_log_err("ctr_drbg_seed", ret); return false; }

    // DNS resolve + TCP connect via mbedtls (uses POSIX sockets — works on Switch)
    char port_str[8];
    std::snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
    ret = mbedtls_net_connect(&net, host, port_str, MBEDTLS_NET_PROTO_TCP);
    if (ret != 0) { tls_log_err("net_connect", ret); return false; }

    // Non-blocking so poll() + WANT_READ work correctly
    mbedtls_net_set_nonblock(&net);

    // TLS defaults: client, stream (TLS not DTLS), default ciphersuites
    ret = mbedtls_ssl_config_defaults(&conf,
        MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM,
        MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) { tls_log_err("config_defaults", ret); return false; }

    // No certificate verification — see header comment for rationale
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    ret = mbedtls_ssl_setup(&ssl, &conf);
    if (ret != 0) { tls_log_err("ssl_setup", ret); return false; }

    // SNI — required by many modern TLS servers
    mbedtls_ssl_set_hostname(&ssl, host);
    mbedtls_ssl_set_bio(&ssl, &net,
        mbedtls_net_send, mbedtls_net_recv, nullptr);

    // Handshake (poll then retry on WANT_READ/WANT_WRITE — non-blocking socket)
    uint32_t hs_deadline = SDL_GetTicks() + 10000; // 10-second timeout
    do {
        ret = mbedtls_ssl_handshake(&ssl);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ)
            mbedtls_net_poll(&net, MBEDTLS_NET_POLL_READ,  10);
        else if (ret == MBEDTLS_ERR_SSL_WANT_WRITE)
            mbedtls_net_poll(&net, MBEDTLS_NET_POLL_WRITE, 10);
        if ((int32_t)(hs_deadline - SDL_GetTicks()) <= 0) {
            std::fprintf(stderr, "TLS [handshake]: timed out\n");
            return false;
        }
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ ||
             ret == MBEDTLS_ERR_SSL_WANT_WRITE);

    if (ret != 0) { tls_log_err("handshake", ret); return false; }
    return true;
}

void TlsConn::close() {
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_net_free(&net);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);
}

int TlsConn::send(const void* data, int len) {
    int ret;
    // Retry on WANT_WRITE (non-blocking write buffer full)
    do {
        ret = mbedtls_ssl_write(&ssl,
            reinterpret_cast<const unsigned char*>(data), (size_t)len);
    } while (ret == MBEDTLS_ERR_SSL_WANT_WRITE);
    return ret; // >0 = bytes sent, <=0 = error
}

int TlsConn::recv(void* buf, int cap) {
    int ret = mbedtls_ssl_read(&ssl,
        reinterpret_cast<unsigned char*>(buf), (size_t)cap);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ)          return 0;  // no data yet
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)  return -1; // clean close
    return ret; // >0 = bytes received, <0 = fatal error
}

bool TlsConn::poll(int timeout_ms) {
    return mbedtls_net_poll(&net, MBEDTLS_NET_POLL_READ, (uint32_t)timeout_ms) > 0;
}

} // namespace ao

#else // ── Stubs when AO_TLS is not defined ─────────────────────────────────────

namespace ao {

bool RawConn::connect(const char*, uint16_t) {
    std::fprintf(stderr, "RawConn: built without AO_TLS\n");
    return false;
}
void RawConn::close() {}
int  RawConn::send(const void*, int)  { return -1; }
int  RawConn::recv(void*, int)        { return 0;  }
bool RawConn::poll(int)               { return false; }

bool TlsConn::connect(const char* host, uint16_t port) {
    (void)host; (void)port;
    std::fprintf(stderr, "TLS: this build was compiled without AO_TLS support\n");
    return false;
}
void TlsConn::close() {}
int  TlsConn::send(const void*, int)  { return -1; }
int  TlsConn::recv(void*, int)        { return 0;  }
bool TlsConn::poll(int)               { return false; }

} // namespace ao

#endif // AO_TLS
