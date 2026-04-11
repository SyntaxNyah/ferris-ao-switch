#pragma once
#include "../net/packet_queue.hpp"
#include "../state/game_state.hpp"
#include "packet.hpp"

namespace ao {

enum class HandshakeState {
    Idle,
    WaitDecryptor,  // connected, waiting for decryptor packet (AO-SDL sends HI in response)
    WaitId,         // sent HI, waiting for ID + PN + FL [+ ASS]
    WaitSi,         // sent askchaa, waiting for SI
    WaitSc,         // sent RC, waiting for SC
    WaitSm,         // sent RM, waiting for SM
    WaitDone,       // sent RD, waiting for LE + CharsCheck + HP×2 + BN + DONE
    InLobby,        // fully connected, normal operation
};

// Drives the AO2 handshake and in-lobby packet dispatch.
// Call process() once per main-loop frame to drain the incoming queue.
class AOClient {
public:
    AOClient(OutQueue& out, GameState& state, const char* username);

    // Call when a TCP connection is established. AO-SDL flow: wait for the
    // server's decryptor packet, then send HI in response.
    void on_connected(const char* hdid = "ferris-ao-switch");

    // Call when a WebSocket connection is established. webAO flow: send HI
    // immediately on socket.onopen without waiting for decryptor. Vanilla
    // tsuserver sends a 758-byte PR/PU broadcast burst *before* the
    // decryptor and closes the socket if HI doesn't arrive in that window,
    // so we have to match webAO's behaviour to connect at all.
    void on_connected_ws(const char* hdid = "ferris-ao-switch");

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
    void on_ass       (const Packet& p);
    void on_si        (const Packet& p);
    void on_done      (const Packet& p);

    // Streaming handlers — take raw packet bytes directly because SC/SM/
    // CharsCheck on large servers can exceed Packet::MAX_FIELDS. `raw`/`len`
    // come from the same byte range parse_packet consumed.
    void on_sc_stream        (const char* raw, int len);
    void on_sm_stream        (const char* raw, int len);
    void on_chars_check_stream(const char* raw, int len);

    // In-lobby handlers
    void on_ms        (const Packet& p);
    void on_ct        (const Packet& p);
    void on_arup      (const Packet& p);
    void on_mc        (const Packet& p);
    void on_hp        (const Packet& p);
    void on_bn        (const Packet& p);
    void on_le        (const Packet& p);
    void on_pv        (const Packet& p);
    void on_auth      (const Packet& p);
    void on_bd        (const Packet& p);
    void on_zz        (const Packet& p);
    void on_check     (const Packet& p);

    // Akashi-specific handlers
    void on_fa        (const Packet& p); // full area list (replaces SM areas)
    void on_pr        (const Packet& p); // player roster add/remove
    void on_pu        (const Packet& p); // player state update
    void on_ti        (const Packet& p); // timer info

    OutQueue&      out_;
    GameState&     state_;
    HandshakeState hs_state_ = HandshakeState::Idle;
    char           username_[64];
    char           hdid_[64]    = "ferris-ao-switch";

    // Pending parse buffer (across frames) — must be >= InPacket::data (131072)
    char  parse_buf_[131072];
    int   parse_len_ = 0;

    // Handshake timeout — if we don't reach InLobby within 60s, disconnect.
    uint32_t hs_start_ms_ = 0;
};

} // namespace ao
