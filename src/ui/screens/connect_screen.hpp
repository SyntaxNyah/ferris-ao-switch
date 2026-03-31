#pragma once
#include "../screen.hpp"
#include "../../net/network_thread.hpp"
#include <SDL2/SDL.h>

namespace ao {

struct ServerEntry {
    char name[128];
    char ip[256];
    char description[512];
    int  port     = 27017;
    int  ws_port  = 0;
    int  players  = 0;
};

static constexpr int MAX_SERVERS = 128;

class ConnectScreen : public Screen {
public:
    explicit ConnectScreen(App& app);
    ~ConnectScreen() override;

    void on_enter() override;
    void on_exit()  override;
    void handle_event(const SDL_Event& e) override;
    void update(uint32_t dt_ms) override;
    void render() override;

private:
    void start_fetch();
    void connect_to_server(const ServerEntry& s);
    void connect_direct();
    void open_keyboard(const char* hint, const char* initial, char* buf, int buf_sz, int max_len);
    void parse_url(const char* src, char* out_host, int host_cap,
                   uint16_t* out_port, ConnMode* out_mode) const;

    // ── Tab 0: Server browser ──────────────────────────────────────────────────
    char ms_url_[256]   = "https://servers.aceattorneyonline.com/servers";
    ServerEntry servers_[MAX_SERVERS];
    int  server_count_  = 0;
    int  server_sel_    = 0;
    int  scroll_offset_ = 0;

    enum class FetchState { Idle, Loading, Done, Error };
    // SDL_atomic_t used so the fetch thread can write without a mutex on the state
    SDL_atomic_t fetch_state_atom_;  // holds FetchState cast to int
    char         fetch_error_[128]  = {};
    SDL_Thread*  fetch_thread_      = nullptr;

    ServerEntry* fetch_buf_    = nullptr;  // SDL_malloc'd array, fetch thread writes
    int          fetch_count_  = 0;
    SDL_mutex*   fetch_mutex_  = nullptr;

    struct FetchArg { ConnectScreen* self; };
    static int SDLCALL fetch_thread_fn(void* ud);

    // ── Tab 1: Direct connect ──────────────────────────────────────────────────
    char host_[256]   = "";
    char port_str_[8] = "27017";

    int  direct_sel_  = 0;  // 0=host, 1=port, 2=username, 3=connect

    // ── Shared ────────────────────────────────────────────────────────────────
    int  tab_       = 0;   // 0=servers, 1=direct
    char status_[256] = {};

    static constexpr int VISIBLE_ROWS = 10;
    static constexpr int ROW_H        = 48;
};

} // namespace ao
