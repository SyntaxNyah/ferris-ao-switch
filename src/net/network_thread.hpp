#pragma once
#include "packet_queue.hpp"
#include <SDL2/SDL.h>
#include <SDL2/SDL_net.h>
#include <atomic>

namespace ao {

enum class ConnMode { TCP, WS };

class NetworkThread {
public:
    NetworkThread(InQueue& in, OutQueue& out);
    ~NetworkThread();

    // Non-blocking — starts the background thread and returns immediately.
    // mode: TCP = plain AO2 over TCP,  WS = WebSocket (ws://)
    bool connect(const char* host, uint16_t port, ConnMode mode = ConnMode::TCP);

    // Thread-safe — sets the stop flag and waits for the thread to finish.
    void disconnect();

    bool is_connected() const { return connected_.load(std::memory_order_acquire); }

    // Last error message (written by network thread, read by main thread
    // only after is_connected() becomes false)
    const char* error() const { return error_msg_; }

private:
    static int SDLCALL thread_func(void* userdata);
    void run();

    void tcp_loop();
    void ws_loop();

    // Extract complete AO2 packets (terminated with '%') from recv_buf_
    // and push them onto in_queue_.
    void extract_packets();

    // Send a pong frame (WS mode)
    void send_pong(const char* payload, int len);

    InQueue&  in_queue_;
    OutQueue& out_queue_;

    SDL_Thread*  thread_    = nullptr;
    TCPsocket    socket_    = nullptr;
    SDLNet_SocketSet socket_set_ = nullptr;

    std::atomic<bool> stop_flag_  {false};
    std::atomic<bool> connected_  {false};

    char host_[256]     = {};
    uint16_t port_      = 0;
    ConnMode mode_      = ConnMode::TCP;

    // Recv ring buffer
    static constexpr int RECV_BUF_CAP = 65536;
    uint8_t recv_buf_[RECV_BUF_CAP];
    int     recv_len_   = 0;

    char    error_msg_[256] = {};
};

} // namespace ao
