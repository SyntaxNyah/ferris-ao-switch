#pragma once
#include <cstdint>

// mbedtls headers are only available when switch-mbedtls portlib is installed.
// The Makefile sets -DAO_TLS and links -lmbedtls -lmbedx509 -lmbedcrypto when
// the portlib is present.  Without it, TlsConn is a stub whose connect()
// always returns false so the rest of the code compiles unchanged.
#ifdef AO_TLS
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#endif

namespace ao {

// Plain TCP connection using mbedtls_net (no TLS).
//
// Replaces SDLNet_TCP_Open for TCP and WS modes. SDL_net issues a blocking
// connect() syscall which Ryujinx rejects with "Blocking socket operations
// are not yet working properly". mbedtls_net_connect uses POSIX sockets
// which Ryujinx handles correctly. After connect, mbedtls_net_set_nonblock
// gives us the same non-blocking I/O model as TlsConn.
struct RawConn {
#ifdef AO_TLS
    mbedtls_net_context net {};
#endif
    bool active_ = false;   // guards close() against double-free / double-close
    bool connect(const char* host, uint16_t port);
    void close();
    int  send(const void* data, int len);
    int  recv(void* buf, int cap); // 0 = WANT_READ, <0 = error
    bool poll(int timeout_ms);
};

// Thin mbedtls TLS client connection wrapper.
//
// Usage:
//   TlsConn tls;
//   if (!tls.connect("server.example.com", 27443)) { /* error */ }
//   tls.send(data, len);
//   tls.recv(buf, cap);  // 0 = no data yet, <0 = closed/error
//   tls.close();
//
// Certificate validation: MBEDTLS_SSL_VERIFY_NONE.
// AO servers frequently use self-signed certs and Switch homebrew has no
// convenient CA bundle.  TLS still provides transport encryption; just no
// server-identity check.  SNI is still sent so virtual-hosted servers work.
struct TlsConn {
#ifdef AO_TLS
    mbedtls_net_context      net      {};
    mbedtls_ssl_context      ssl      {};
    mbedtls_ssl_config       conf     {};
    mbedtls_entropy_context  entropy  {};
    mbedtls_ctr_drbg_context ctr_drbg {};
#endif
    bool active_ = false;   // guards close() against double-free / double-close

    // DNS + TCP + TLS handshake (blocking).  Call from network thread only.
    bool connect(const char* host, uint16_t port);

    // Free all mbedtls resources and close the underlying socket.
    void close();

    // Send len bytes.  Returns bytes sent (>0) or error (<=0).
    int send(const void* data, int len);

    // Receive up to cap bytes.  Returns bytes received (>0),
    // 0 if no data is available right now (WANT_READ), or <0 on error/close.
    int recv(void* buf, int cap);

    // Returns true if data is ready to read within timeout_ms milliseconds.
    bool poll(int timeout_ms);
};

} // namespace ao
