#include "tls_conn.hpp"
#include <cstdio>
#include <cstring>
#include <SDL2/SDL.h>

#ifdef AO_TLS
// ── Full mbedtls implementation ───────────────────────────────────────────────
#include <mbedtls/error.h>

namespace ao {

static void tls_log_err(const char* tag, int ret) {
    char msg[128];
    mbedtls_strerror(ret, msg, sizeof(msg));
    std::fprintf(stderr, "TLS [%s]: %s (−0x%04X)\n", tag, msg, (unsigned)(-ret));
}

// ── RawConn — plain non-blocking TCP via mbedtls_net ─────────────────────────

// mbedtls_net_connect does a blocking TCP connect.  On Ryujinx the blocking
// connect() syscall never times out, freezing the app forever.  Run it on a
// detached thread and wait with a 10-second timeout via SDL semaphore.

struct RawConnectTask {
    char              host[256];
    char              port_str[8];
    mbedtls_net_context net;
    int               result;
    SDL_sem*          sem;
};

static int raw_connect_thread(void* ud) {
    auto* t = static_cast<RawConnectTask*>(ud);
    mbedtls_net_init(&t->net);
    t->result = mbedtls_net_connect(&t->net, t->host, t->port_str,
                                    MBEDTLS_NET_PROTO_TCP);
    SDL_SemPost(t->sem);
    return 0;
}

bool RawConn::connect(const char* host, uint16_t port) {
    mbedtls_net_init(&net);

    auto* task = new RawConnectTask{};
    std::strncpy(task->host, host, sizeof(task->host) - 1);
    std::snprintf(task->port_str, sizeof(task->port_str), "%u", (unsigned)port);
    task->result = -1;
    task->sem    = SDL_CreateSemaphore(0);

    SDL_Thread* th = SDL_CreateThread(raw_connect_thread, "raw_tcp_connect", task);
    if (!th) {
        SDL_DestroySemaphore(task->sem);
        delete task;
        std::fprintf(stderr, "RawConn: failed to create connect thread\n");
        return false;
    }
    SDL_DetachThread(th);

    // Wait up to 10 s for the connect to complete
    if (SDL_SemWaitTimeout(task->sem, 10000) != 0) {
        // Timed out — task/thread leaked intentionally (thread may still be
        // blocked in the OS connect() call; it will eventually post and exit)
        std::fprintf(stderr, "RawConn: connect to %s:%u timed out\n", host, port);
        return false;
    }

    int ret = task->result;
    net = task->net;              // take ownership of the fd
    mbedtls_net_init(&task->net); // zero task copy so its destructor won't close our fd
    SDL_DestroySemaphore(task->sem);
    delete task;

    if (ret != 0) { tls_log_err("raw_connect", ret); return false; }
    mbedtls_net_set_nonblock(&net);
    std::fprintf(stderr, "RawConn: connected to %s:%u\n", host, port);
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

    // DNS resolve + TCP connect — run in a thread with timeout (Ryujinx hangs
    // indefinitely on blocking connect() when the remote doesn't respond fast)
    char port_str[8];
    std::snprintf(port_str, sizeof(port_str), "%u", (unsigned)port);
    {
        auto* task = new RawConnectTask{};
        std::strncpy(task->host, host, sizeof(task->host) - 1);
        std::strncpy(task->port_str, port_str, sizeof(task->port_str) - 1);
        task->result = -1;
        task->sem    = SDL_CreateSemaphore(0);
        SDL_Thread* th = SDL_CreateThread(raw_connect_thread, "tls_tcp_connect", task);
        if (!th) {
            SDL_DestroySemaphore(task->sem);
            delete task;
            std::fprintf(stderr, "TlsConn: failed to create connect thread\n");
            return false;
        }
        SDL_DetachThread(th);
        if (SDL_SemWaitTimeout(task->sem, 10000) != 0) {
            std::fprintf(stderr, "TlsConn: TCP connect to %s:%u timed out\n", host, port);
            return false;
        }
        ret = task->result;
        net = task->net;
        mbedtls_net_init(&task->net);
        SDL_DestroySemaphore(task->sem);
        delete task;
        if (ret != 0) { tls_log_err("net_connect", ret); return false; }
        std::fprintf(stderr, "TlsConn: TCP connected to %s:%u\n", host, port);
    }

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
