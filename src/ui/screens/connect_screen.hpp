#pragma once
#include "../screen.hpp"
#include "../../net/network_thread.hpp"

namespace ao {

class ConnectScreen : public Screen {
public:
    explicit ConnectScreen(App& app);
    ~ConnectScreen() override = default;

    void on_enter() override;
    void handle_event(const SDL_Event& e) override;
    void update(uint32_t dt_ms) override;
    void render() override;

private:
    // Parse host_ for a URL scheme prefix (ws://, wss://) and return the
    // resolved bare hostname, port (overriding port_str_ if embedded), and
    // connection mode.  host_ may be any of:
    //   "192.168.1.1"          → TCP,  port from port_str_
    //   "ws://game.example.com"  → WS,   port from port_str_ (or :port in URL)
    //   "wss://game.example.com" → WSS,  port 443 default (or :port in URL)
    void parse_url(char* out_host, int host_cap,
                   uint16_t* out_port, ConnMode* out_mode) const;

    // Connection fields (populated via system keyboard)
    char host_[256]     = "127.0.0.1";
    char port_str_[8]   = "27017";
    char username_[64]  = "Switch";

    int  selected_field_ = 0;   // 0=host,1=port,2=username,3=connect button
    bool connecting_     = false;
    char status_msg_[256]= "Press A to edit a field, then ZR to connect";
};

} // namespace ao
