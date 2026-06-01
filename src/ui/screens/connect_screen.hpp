#pragma once
#include "../screen.hpp"
#include "../../net/network_thread.hpp"
#include <SDL2/SDL.h>

namespace ao {

struct ServerEntry {
    char name[128];
    char ip[256];
    char description[512];
    char asset_url[512];
    int  port     = 27017;
    int  ws_port  = 0;
    int  wss_port = 0;
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
    void scan_themes();              // find AO2 theme folders on the SD card
    void apply_theme(const char* name); // load it + persist the choice
    void parse_url(const char* src, char* out_host, int host_cap,
                   uint16_t* out_port, ConnMode* out_mode) const;
    void load_server_cfg();
    const char* lookup_asset_url(const char* host) const;

    // ── Per-server asset URL config (sdmc:/switch/ferris-ao/servers.cfg) ───────
    static constexpr int MAX_CFG_ENTRIES = 64;
    struct CfgEntry { char host[256]; char asset_url[512]; };
    CfgEntry cfg_[MAX_CFG_ENTRIES];
    int      cfg_count_ = 0;

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

    // ── Tab 2: Settings ─────────────────────────────────────────────────────────
    int  settings_sel_ = 0;  // 0=showname, 1=theme, 2=sfx vol, 3=music vol
    static constexpr int MAX_THEMES = 64;
    char themes_[MAX_THEMES][64];
    int  theme_count_ = 0;
    int  theme_sel_   = 0;   // index into themes_

    // ── Shared ────────────────────────────────────────────────────────────────
    int  tab_       = 0;   // 0=servers, 1=direct, 2=settings, 3=credits
    char status_[256] = {};

    static constexpr int VISIBLE_ROWS = 10;
    static constexpr int ROW_H        = 48;
};

} // namespace ao
