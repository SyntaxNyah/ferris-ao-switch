#pragma once
#include "packet_queue.hpp"
#include "tls_conn.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_net.h>
#include <atomic>

namespace ao {

// Connection mode — derived by the caller from the URL prefix:
//   "ws://<host>"  → WS   (plain WebSocket over SDL_net TCP)
//   "wss://<host>" → WSS  (WebSocket over mbedtls TLS)
//   anything else  → TCP  (raw AO2 over SDL_net TCP)
enum class ConnMode { TCP, WS, WSS };

class NetworkThread {
public:
    NetworkThread(InQueue& in, OutQueue& out);
    ~NetworkThread();

    // Non-blocking — starts the background thread and returns immediately.
    // host must already have any URL prefix (ws://, wss://) stripped.
    bool connect(const char* host, uint16_t port, ConnMode mode = ConnMode::TCP);

    // Thread-safe — sets the stop flag and waits for the thread to finish.
    void disconnect();

    bool is_connected() const { return connected_.load(std::memory_order_acquire); }

    // Last error message (valid after is_connected() becomes false)
    const char* error() const { return error_msg_; }

private:
    static int SDLCALL thread_func(void* userdata);
    void run();

    void tcp_loop();
    void ws_loop();

    // Extract complete AO2 packets (terminated with '%') from recv_buf_
    // and push them onto in_queue_.
    void extract_packets();

    // ── Abstract IO helpers ────────────────────────────────────────────────────
    // These route to either SDL_net (TCP / WS) or TLS (WSS) based on mode_.
    // All loops and ws_upgrade go through these — never call SDL_net or
    // TlsConn directly from tcp_loop / ws_loop.

    // Returns true if incoming data is ready within timeout_ms.
    bool net_poll(int timeout_ms);
    // Read up to cap bytes. Returns >0 bytes, 0 if would-block, <0 on error.
    int  net_recv(void* buf, int cap);
    // Send len bytes. Returns bytes sent (>0) or error (<=0).
    int  net_send(const void* data, int len);

    // Send a WS pong frame in response to a ping.
    void send_pong(const char* payload, int len);

    // ── Static callbacks passed to ws_upgrade ─────────────────────────────────
    static int cb_raw_send (void* ctx, const void* data, int len);
    static int cb_raw_recv (void* ctx, void*       buf,  int cap);
    static int cb_tls_send (void* ctx, const void* data, int len);
    static int cb_tls_recv (void* ctx, void*       buf,  int cap);
#ifndef AO_TLS
    // SDL_net fallback for desktop builds compiled without mbedtls.
    static int cb_sdlnet_send(void* ctx, const void* data, int len);
    static int cb_sdlnet_recv(void* ctx, void*       buf,  int cap);
#endif

    InQueue&  in_queue_;
    OutQueue& out_queue_;

    SDL_Thread* thread_ = nullptr;
    RawConn     raw_conn_;                    // TCP and WS modes (AO_TLS builds)
    TlsConn     tls_conn_;                    // WSS mode
#ifndef AO_TLS
    TCPsocket        socket_     = nullptr;   // TCP/WS fallback without mbedtls
    SDLNet_SocketSet socket_set_ = nullptr;
#endif

    std::atomic<bool> stop_flag_  {false};
    std::atomic<bool> connected_  {false};

    char     host_[256] = {};
    uint16_t port_      = 0;
    ConnMode mode_      = ConnMode::TCP;

    // Recv ring buffer (shared by tcp_loop and ws_loop via extract_packets)
    static constexpr int RECV_BUF_CAP = 65536;
    uint8_t recv_buf_[RECV_BUF_CAP];
    int     recv_len_   = 0;

    char    error_msg_[256] = {};
};

} // namespace ao
