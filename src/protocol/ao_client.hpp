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
    void on_ass       (const Packet& p);
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

    // Handshake timeout — if we don't reach InLobby within 15s, disconnect.
    uint32_t hs_start_ms_ = 0;

    // Akashi direct-lobby: track when last PR/PU arrived so we can send RC
    // proactively if CharsCheck never comes.
    bool     akashi_pr_seen_       = false;
    uint32_t akashi_pr_ms_         = 0;  // SDL_GetTicks() when last PR/PU was seen
    // After Akashi decryptor, if ID doesn't arrive within 3s the server wants
    // us to drive — send ID+askchaa to prompt SI.
    uint32_t akashi_decryptor_ms_  = 0;  // 0 = not waiting
    // Timestamp when we entered WaitSc; used to force InLobby if SC never arrives.
    uint32_t waitsc_start_ms_      = 0;  // 0 = not in WaitSc
};

} // namespace ao
