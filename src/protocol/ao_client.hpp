#pragma once
#include "../net/packet_queue.hpp"
#include "../state/game_state.hpp"
#include "packet.hpp"

namespace ao {

enum class HandshakeState {
    Idle,
    WaitDecryptor,  // ← decryptor#NOENCRYPT#%
    WaitId,         // ← ID + PN + FL [+ ASS]
    WaitSi,         // ← SI
    WaitSc,         // ← SC
    WaitSm,         // ← SM
    WaitDone,       // ← LE + CharsCheck + HP×2 + BN + DONE
    InLobby,        // fully connected, normal operation
};

// Drives the AO2 handshake and in-lobby packet dispatch.
// Call process() once per main-loop frame to drain the incoming queue.
class AOClient {
public:
    AOClient(OutQueue& out, GameState& state, const char* username);

    // Call when the TCP/WS connection is established — sends HI.
    void on_connected(const char* hdid = "ferris-ao-switch");

    // Call on disconnect.
    void on_disconnected();

    // Drain the incoming queue and process each packet.
    // Call once per frame from the main thread.
    void process(InQueue& in);

    HandshakeState handshake_state() const { return hs_state_; }

private:
    void send(const char* buf, int len);
    void send_fmt(const char* fmt, ...);

    void handle(const Packet& pkt);

    // Handshake handlers
    void on_decryptor (const Packet& p);
    void on_id        (const Packet& p);
    void on_pn        (const Packet& p);
    void on_fl        (const Packet& p);
    void on_si        (const Packet& p);
    void on_sc        (const Packet& p);
    void on_sm        (const Packet& p);
    void on_done      (const Packet& p);

    // In-lobby handlers
    void on_ms        (const Packet& p);
    void on_ct        (const Packet& p);
    void on_arup      (const Packet& p);
    void on_mc        (const Packet& p);
    void on_hp        (const Packet& p);
    void on_bn        (const Packet& p);
    void on_le        (const Packet& p);
    void on_chars_check(const Packet& p);
    void on_pv        (const Packet& p);
    void on_auth      (const Packet& p);
    void on_bd        (const Packet& p);
    void on_zz        (const Packet& p);
    void on_check     (const Packet& p);

    OutQueue&      out_;
    GameState&     state_;
    HandshakeState hs_state_ = HandshakeState::Idle;
    char           username_[64];
    char           hdid_[64]    = "ferris-ao-switch";

    // Pending parse buffer (across frames)
    char  parse_buf_[8192];
    int   parse_len_ = 0;
};

} // namespace ao
