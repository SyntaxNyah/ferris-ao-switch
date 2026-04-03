#include "network_thread.hpp"
#include "ws_handshake.hpp"
#include "ws_frame.hpp"
#include <cstdio>
#include <cstring>

namespace ao {

NetworkThread::NetworkThread(InQueue& in, OutQueue& out)
    : in_queue_(in), out_queue_(out) {}

NetworkThread::~NetworkThread() {
    disconnect();
}

bool NetworkThread::connect(const char* host, uint16_t port, ConnMode mode) {
    if (thread_) disconnect();

    std::strncpy(host_, host, sizeof(host_) - 1);
    port_ = port;
    mode_ = mode;
    stop_flag_.store(false);

    // SDLNet is only needed for TCP and WS modes, but init is harmless for WSS.
    if (SDLNet_Init() != 0) {
        std::snprintf(error_msg_, sizeof(error_msg_),
            "SDLNet_Init: %s", SDLNet_GetError());
        return false;
    }

    thread_ = SDL_CreateThread(thread_func, "NetThread", this);
    return thread_ != nullptr;
}

void NetworkThread::disconnect() {
    stop_flag_.store(true, std::memory_order_release);
    if (thread_) {
        SDL_WaitThread(thread_, nullptr);
        thread_ = nullptr;
    }
    if (mode_ == ConnMode::WSS)
        tls_conn_.close();
#ifdef AO_TLS
    else
        raw_conn_.close();
#else
    if (socket_set_) { SDLNet_FreeSocketSet(socket_set_); socket_set_ = nullptr; }
    if (socket_)     { SDLNet_TCP_Close(socket_);         socket_     = nullptr; }
#endif
    connected_.store(false, std::memory_order_release);
    SDLNet_Quit();
}

int SDLCALL NetworkThread::thread_func(void* userdata) {
    reinterpret_cast<NetworkThread*>(userdata)->run();
    return 0;
}

void NetworkThread::run() {
    if (mode_ == ConnMode::WSS) {
        // ── WSS: TLS connect then WebSocket upgrade ────────────────────────────
        if (!tls_conn_.connect(host_, port_)) {
            std::snprintf(error_msg_, sizeof(error_msg_),
                "TLS connect failed: %s:%u", host_, port_);
            goto push_disconnect;
        }
        if (!ws_upgrade(cb_tls_send, cb_tls_recv, &tls_conn_, host_)) {
            std::snprintf(error_msg_, sizeof(error_msg_),
                "WSS WebSocket upgrade failed");
            tls_conn_.close();
            goto push_disconnect;
        }
        connected_.store(true, std::memory_order_release);
        ws_loop();
        tls_conn_.close();

    } else {
#ifdef AO_TLS
        // ── TCP / WS: non-blocking POSIX connect via mbedtls_net ─────────────
        // SDLNet_TCP_Open issues a blocking connect() which Ryujinx rejects.
        // mbedtls_net_connect uses POSIX sockets and works correctly.
        if (!raw_conn_.connect(host_, port_)) {
            std::snprintf(error_msg_, sizeof(error_msg_),
                "TCP connect failed: %s:%u", host_, port_);
            goto push_disconnect;
        }
        if (mode_ == ConnMode::WS) {
            if (!ws_upgrade(cb_raw_send, cb_raw_recv, &raw_conn_, host_)) {
                std::snprintf(error_msg_, sizeof(error_msg_),
                    "WebSocket upgrade failed");
                raw_conn_.close();
                goto push_disconnect;
            }
        }
        connected_.store(true, std::memory_order_release);
        if (mode_ == ConnMode::WS) ws_loop(); else tcp_loop();
        raw_conn_.close();
#else
        // ── TCP / WS: SDL_net fallback (desktop builds without mbedtls) ──────
        IPaddress ip;
        if (SDLNet_ResolveHost(&ip, host_, port_) != 0) {
            std::snprintf(error_msg_, sizeof(error_msg_),
                "DNS resolve failed: %s", SDLNet_GetError());
            goto push_disconnect;
        }
        socket_ = SDLNet_TCP_Open(&ip);
        if (!socket_) {
            std::snprintf(error_msg_, sizeof(error_msg_),
                "TCP connect failed: %s", SDLNet_GetError());
            goto push_disconnect;
        }
        socket_set_ = SDLNet_AllocSocketSet(1);
        SDLNet_TCP_AddSocket(socket_set_, socket_);
        if (mode_ == ConnMode::WS) {
            if (!ws_upgrade(cb_sdlnet_send, cb_sdlnet_recv, socket_, host_)) {
                std::snprintf(error_msg_, sizeof(error_msg_),
                    "WebSocket upgrade failed");
                goto push_disconnect;
            }
        }
        connected_.store(true, std::memory_order_release);
        if (mode_ == ConnMode::WS) ws_loop(); else tcp_loop();
#endif
    }

    connected_.store(false, std::memory_order_release);

push_disconnect:
    // Push a synthetic disconnect notification so the main thread can react.
    InPacket disc;
    std::strncpy(disc.data, "__DISCONNECT#%", sizeof(disc.data));
    disc.len = (int)std::strlen(disc.data);
    in_queue_.push(disc);
}

// ── Abstract IO helpers ────────────────────────────────────────────────────────

bool NetworkThread::net_poll(int timeout_ms) {
    if (mode_ == ConnMode::WSS) return tls_conn_.poll(timeout_ms);
#ifdef AO_TLS
    return raw_conn_.poll(timeout_ms);
#else
    return SDLNet_CheckSockets(socket_set_, (Uint32)timeout_ms) > 0
        && SDLNet_SocketReady(socket_);
#endif
}

int NetworkThread::net_recv(void* buf, int cap) {
    if (mode_ == ConnMode::WSS) return tls_conn_.recv(buf, cap);
#ifdef AO_TLS
    return raw_conn_.recv(buf, cap);
#else
    return SDLNet_TCP_Recv(socket_, reinterpret_cast<char*>(buf), cap);
#endif
}

int NetworkThread::net_send(const void* data, int len) {
    if (mode_ == ConnMode::WSS) return tls_conn_.send(data, len);
#ifdef AO_TLS
    return raw_conn_.send(data, len);
#else
    return SDLNet_TCP_Send(socket_, data, len);
#endif
}

// ── ws_upgrade callbacks ───────────────────────────────────────────────────────

int NetworkThread::cb_raw_send(void* ctx, const void* data, int len) {
    return reinterpret_cast<RawConn*>(ctx)->send(data, len);
}
int NetworkThread::cb_raw_recv(void* ctx, void* buf, int cap) {
    // RawConn uses SO_RCVTIMEO (5 ms) — recv blocks up to 5 ms then returns
    // WANT_READ (→ 0).  Retry until data arrives or the 10-second deadline.
    // Do NOT call conn->poll() here: it uses select() which is broken on Ryujinx.
    RawConn* conn = reinterpret_cast<RawConn*>(ctx);
    uint32_t deadline = SDL_GetTicks() + 10000;
    while (true) {
        int n = conn->recv(buf, cap);
        if (n != 0) return n;
        if ((int32_t)(deadline - SDL_GetTicks()) <= 0) return -1;
    }
}
int NetworkThread::cb_tls_send(void* ctx, const void* data, int len) {
    return reinterpret_cast<TlsConn*>(ctx)->send(data, len);
}
int NetworkThread::cb_tls_recv(void* ctx, void* buf, int cap) {
    return reinterpret_cast<TlsConn*>(ctx)->recv(buf, cap);
}
#ifndef AO_TLS
int NetworkThread::cb_sdlnet_send(void* ctx, const void* data, int len) {
    return SDLNet_TCP_Send(reinterpret_cast<TCPsocket>(ctx), data, len);
}
int NetworkThread::cb_sdlnet_recv(void* ctx, void* buf, int cap) {
    return SDLNet_TCP_Recv(reinterpret_cast<TCPsocket>(ctx),
                           reinterpret_cast<char*>(buf), cap);
}
#endif

// ── TCP loop ──────────────────────────────────────────────────────────────────
void NetworkThread::tcp_loop() {
    while (!stop_flag_.load(std::memory_order_acquire)) {
        // Receive without gating on net_poll — select() is unreliable on Ryujinx.
        int space = RECV_BUF_CAP - recv_len_;
        if (space <= 0) {
            std::fprintf(stderr, "recv_buf overflow — clearing\n");
            recv_len_ = 0; space = RECV_BUF_CAP;
        }
        int n = net_recv(recv_buf_ + recv_len_, space);
        if (n < 0) break;   // error / close
        if (n > 0) {
            recv_len_ += n;
            extract_packets();
        }
        // n == 0: SO_RCVTIMEO expired (no data in 5 ms) — fall through to sends
        OutPacket out;
        while (out_queue_.pop(out)) {
            if (net_send(out.data, out.len) < out.len) break;
        }
    }
}

// ── WebSocket loop ────────────────────────────────────────────────────────────
void NetworkThread::ws_loop() {
    // 128 KB — handles servers with 1000+ music tracks in a single SM frame.
    static uint8_t frame_buf[131072];
    static int     frame_len = 0;
    frame_len = 0; // reset for each new connection
    std::fprintf(stderr, "[ws_loop] started\n");

    while (!stop_flag_.load(std::memory_order_acquire)) {
        // Receive without gating on net_poll — select() is unreliable on Ryujinx.
        {
            int space = (int)sizeof(frame_buf) - frame_len;
            if (space <= 0) { frame_len = 0; space = (int)sizeof(frame_buf); }

            int n = net_recv(frame_buf + frame_len, space);
            if (n < 0) break;   // error / close
            if (n == 0) { goto send_outgoing; } // SO_RCVTIMEO expired — check sends
            frame_len += n;

            int consumed_total = 0;
            while (true) {
                int remaining = frame_len - consumed_total;
                if (remaining <= 0) break;

                static char payload[131072];
                int  payload_len = 0;
                FrameResult res = ws_decode_frame(
                    frame_buf + consumed_total, remaining,
                    payload, sizeof(payload), payload_len);

                if (res == FrameResult::Incomplete) break;
                if (res == FrameResult::Close)  { stop_flag_.store(true); break; }
                if (res == FrameResult::Error)  { std::fprintf(stderr, "WS frame error\n"); stop_flag_.store(true); break; }
                if (res == FrameResult::Ping)   { send_pong(payload, payload_len); }

                if (payload_len > 0 && recv_len_ + payload_len < RECV_BUF_CAP) {
                    std::memcpy(recv_buf_ + recv_len_, payload, payload_len);
                    recv_len_ += payload_len;
                    extract_packets();
                }

                // Advance past this frame
                int header = 2;
                uint8_t* fb = frame_buf + consumed_total;
                int plen = fb[1] & 0x7F;
                if (plen == 126) { header += 2; plen = ((int)fb[2]<<8)|fb[3]; }
                else if (plen == 127) { header += 8; plen = (int)fb[9]; }
                if (fb[1] & 0x80) header += 4; // mask present
                consumed_total += header + plen;
            }

            if (consumed_total > 0 && consumed_total < frame_len)
                std::memmove(frame_buf, frame_buf + consumed_total,
                             frame_len - consumed_total);
            frame_len -= consumed_total;
            if (frame_len < 0) frame_len = 0;
        }

send_outgoing:
        OutPacket out;
        while (out_queue_.pop(out)) {
            uint8_t frame[2200];
            int flen = ws_encode_frame(out.data, out.len, frame, sizeof(frame));
            if (flen > 0) net_send(frame, flen);
        }
    }
}

// ── Packet extraction ─────────────────────────────────────────────────────────
void NetworkThread::extract_packets() {
    int start = 0;
    for (int i = 0; i < recv_len_; ++i) {
        if (recv_buf_[i] == '%') {
            int pkt_len = i - start + 1;
            if (pkt_len > 0 && pkt_len < (int)sizeof(InPacket::data)) {
                InPacket pkt;
                std::memcpy(pkt.data, recv_buf_ + start, pkt_len);
                pkt.data[pkt_len] = '\0';
                pkt.len = pkt_len;
                in_queue_.push(pkt);
            }
            start = i + 1;
        }
    }
    if (start > 0) {
        std::memmove(recv_buf_, recv_buf_ + start, recv_len_ - start);
        recv_len_ -= start;
    }
}

void NetworkThread::send_pong(const char* payload, int len) {
    uint8_t frame[128];
    frame[0] = 0x8A; // FIN + pong opcode
    frame[1] = 0x80 | (uint8_t)(len < 125 ? len : 0); // mask bit set
    frame[2] = frame[3] = frame[4] = frame[5] = 0; // mask key = 0x00000000
    for (int i = 0; i < len && i < 120; ++i)
        frame[6 + i] = (uint8_t)payload[i];
    net_send(frame, 6 + (len < 120 ? len : 0));
}

} // namespace ao
