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

    // SDLNet must be initialized before creating a thread that uses it
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
    if (socket_set_) {
        SDLNet_FreeSocketSet(socket_set_);
        socket_set_ = nullptr;
    }
    if (socket_) {
        SDLNet_TCP_Close(socket_);
        socket_ = nullptr;
    }
    connected_.store(false, std::memory_order_release);
    SDLNet_Quit();
}

int SDLCALL NetworkThread::thread_func(void* userdata) {
    reinterpret_cast<NetworkThread*>(userdata)->run();
    return 0;
}

void NetworkThread::run() {
    // Resolve host
    IPaddress ip;
    if (SDLNet_ResolveHost(&ip, host_, port_) != 0) {
        std::snprintf(error_msg_, sizeof(error_msg_),
            "DNS resolve failed: %s", SDLNet_GetError());
        return;
    }

    socket_ = SDLNet_TCP_Open(&ip);
    if (!socket_) {
        std::snprintf(error_msg_, sizeof(error_msg_),
            "TCP connect failed: %s", SDLNet_GetError());
        return;
    }

    socket_set_ = SDLNet_AllocSocketSet(1);
    SDLNet_TCP_AddSocket(socket_set_, socket_);

    if (mode_ == ConnMode::WS) {
        if (!ws_upgrade(socket_, host_)) {
            std::snprintf(error_msg_, sizeof(error_msg_),
                "WebSocket upgrade failed");
            SDLNet_TCP_Close(socket_);
            socket_ = nullptr;
            return;
        }
        connected_.store(true, std::memory_order_release);
        ws_loop();
    } else {
        connected_.store(true, std::memory_order_release);
        tcp_loop();
    }

    connected_.store(false, std::memory_order_release);

    // Push a synthetic disconnect notification
    InPacket disc;
    std::strncpy(disc.data, "__DISCONNECT#%", sizeof(disc.data));
    disc.len = (int)std::strlen(disc.data);
    in_queue_.push(disc);
}

// ── TCP loop ──────────────────────────────────────────────────────────────────
void NetworkThread::tcp_loop() {
    while (!stop_flag_.load(std::memory_order_acquire)) {
        // Check for incoming data (1 ms timeout)
        int ready = SDLNet_CheckSockets(socket_set_, 1);
        if (ready > 0 && SDLNet_SocketReady(socket_)) {
            int space = RECV_BUF_CAP - recv_len_;
            if (space <= 0) {
                std::fprintf(stderr, "recv_buf overflow — clearing\n");
                recv_len_ = 0;
                space = RECV_BUF_CAP;
            }
            int n = SDLNet_TCP_Recv(socket_,
                reinterpret_cast<char*>(recv_buf_) + recv_len_, space);
            if (n <= 0) break; // disconnected
            recv_len_ += n;
            extract_packets();
        }

        // Send any pending outgoing packets
        OutPacket out;
        while (out_queue_.pop(out)) {
            if (SDLNet_TCP_Send(socket_, out.data, out.len) < out.len) break;
        }
    }
}

// ── WebSocket loop ────────────────────────────────────────────────────────────
void NetworkThread::ws_loop() {
    // WS frame reassembly buffer
    static uint8_t frame_buf[65536];
    static int     frame_len = 0;

    while (!stop_flag_.load(std::memory_order_acquire)) {
        int ready = SDLNet_CheckSockets(socket_set_, 1);
        if (ready > 0 && SDLNet_SocketReady(socket_)) {
            int space = (int)sizeof(frame_buf) - frame_len;
            if (space <= 0) { frame_len = 0; space = (int)sizeof(frame_buf); }

            int n = SDLNet_TCP_Recv(socket_,
                reinterpret_cast<char*>(frame_buf) + frame_len, space);
            if (n <= 0) break;
            frame_len += n;

            // Try to consume complete WS frames
            int consumed_total = 0;
            while (true) {
                int remaining = frame_len - consumed_total;
                if (remaining <= 0) break;

                char payload[4096];
                int  payload_len = 0;
                FrameResult res = ws_decode_frame(
                    frame_buf + consumed_total, remaining,
                    payload, sizeof(payload), payload_len);

                if (res == FrameResult::Incomplete) break;

                if (res == FrameResult::Close) {
                    stop_flag_.store(true); break;
                }
                if (res == FrameResult::Error) {
                    std::fprintf(stderr, "WS frame error\n"); break;
                }
                if (res == FrameResult::Ping) {
                    send_pong(payload, payload_len);
                }

                // Accumulate payload into recv_buf_ for AO packet extraction
                if (payload_len > 0 &&
                    recv_len_ + payload_len < RECV_BUF_CAP) {
                    std::memcpy(recv_buf_ + recv_len_, payload, payload_len);
                    recv_len_ += payload_len;
                    extract_packets();
                }

                // Advance past this frame
                // Re-decode just to find the frame byte length
                int header = 2;
                uint8_t* fb = frame_buf + consumed_total;
                int plen = fb[1] & 0x7F;
                if (plen == 126) { header += 2; plen = ((int)fb[2]<<8)|fb[3]; }
                else if (plen == 127) { header += 8; plen = (int)fb[9]; }
                if (fb[1] & 0x80) header += 4; // mask
                consumed_total += header + plen;
            }

            // Shift unprocessed bytes to front
            if (consumed_total > 0 && consumed_total < frame_len) {
                std::memmove(frame_buf, frame_buf + consumed_total,
                             frame_len - consumed_total);
            }
            frame_len -= consumed_total;
            if (frame_len < 0) frame_len = 0;
        }

        // Outgoing — wrap each AO packet in a WS frame
        OutPacket out;
        while (out_queue_.pop(out)) {
            uint8_t frame[2200];
            int flen = ws_encode_frame(out.data, out.len, frame, sizeof(frame));
            if (flen > 0)
                SDLNet_TCP_Send(socket_, frame, flen);
        }
    }
}

// ── Packet extraction ─────────────────────────────────────────────────────────
void NetworkThread::extract_packets() {
    int start = 0;
    for (int i = 0; i < recv_len_; ++i) {
        if (recv_buf_[i] == '%') {
            int pkt_len = i - start + 1; // includes '%'
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
    // Shift unconsumed bytes to front
    if (start > 0) {
        std::memmove(recv_buf_, recv_buf_ + start, recv_len_ - start);
        recv_len_ -= start;
    }
}

void NetworkThread::send_pong(const char* payload, int len) {
    // Build a pong frame (opcode 0xA, no mask from server, but client must mask)
    uint8_t frame[128];
    frame[0] = 0x8A; // FIN + pong
    frame[1] = 0x80 | (uint8_t)(len < 125 ? len : 0); // mask bit
    // mask key (all zeros for pong — content doesn't matter)
    frame[2] = frame[3] = frame[4] = frame[5] = 0;
    for (int i = 0; i < len && i < 120; ++i)
        frame[6 + i] = (uint8_t)payload[i];
    int flen = 6 + (len < 120 ? len : 0);
    SDLNet_TCP_Send(socket_, frame, flen);
}

} // namespace ao
