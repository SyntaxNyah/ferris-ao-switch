#include "ws_frame.hpp"
#include <cstdlib>
#include <cstring>
#include <SDL2/SDL.h>    // for SDL_GetTicks (masking key seed)

namespace ao {

int ws_encode_frame(const char* payload, int payload_len,
                    uint8_t* out, int out_cap) {
    // Minimum frame header: 2 + 4 (mask) = 6 bytes; extended len adds 2 or 8.
    int header_len = 6;
    if (payload_len > 125 && payload_len <= 65535) header_len += 2;
    else if (payload_len > 65535)                   header_len += 8;

    if (out_cap < header_len + payload_len) return 0;

    int pos = 0;
    // Byte 0: FIN=1, opcode=TEXT(1). AO2 over WebSocket is a text protocol —
    // webAO sends text frames and servers (Whisker, akashi, tsuserver) only
    // read WS_OPCODE_TEXT, silently dropping binary (0x2) frames. Sending text
    // is what lets HI and every subsequent packet actually reach the server.
    out[pos++] = 0x81;
    // Byte 1: MASK=1, payload length
    if (payload_len <= 125) {
        out[pos++] = 0x80 | (uint8_t)payload_len;
    } else if (payload_len <= 65535) {
        out[pos++] = 0xFE;
        out[pos++] = (uint8_t)(payload_len >> 8);
        out[pos++] = (uint8_t)(payload_len     );
    } else {
        out[pos++] = 0xFF;
        for (int i = 7; i >= 0; --i)
            out[pos++] = (uint8_t)((payload_len >> (i * 8)) & 0xFF);
    }
    // 4-byte masking key
    uint8_t mask[4];
    uint32_t seed = SDL_GetTicks() ^ (uint32_t)(uintptr_t)payload;
    mask[0] = (seed      ) & 0xFF;
    mask[1] = (seed >>  8) & 0xFF;
    mask[2] = (seed >> 16) & 0xFF;
    mask[3] = (seed >> 24) & 0xFF;
    out[pos++] = mask[0];
    out[pos++] = mask[1];
    out[pos++] = mask[2];
    out[pos++] = mask[3];
    // Masked payload
    for (int i = 0; i < payload_len; ++i)
        out[pos++] = (uint8_t)payload[i] ^ mask[i & 3];

    return pos;
}

FrameResult ws_decode_frame(const uint8_t* buf, int buf_len,
                             char* out_buf, int out_cap, int& out_len) {
    out_len = 0;
    if (buf_len < 2) return FrameResult::Incomplete;

    bool fin    = (buf[0] & 0x80) != 0;
    auto opcode = static_cast<WsOpcode>(buf[0] & 0x0F);
    bool masked = (buf[1] & 0x80) != 0;
    int  plen   = buf[1] & 0x7F;
    int  pos    = 2;

    if (plen == 126) {
        if (buf_len < 4) return FrameResult::Incomplete;
        plen = ((int)buf[2] << 8) | buf[3];
        pos  = 4;
    } else if (plen == 127) {
        if (buf_len < 10) return FrameResult::Incomplete;
        // Treat as 32-bit (AO2 packets are never > 4 GB)
        plen = ((int)buf[6] << 24) | ((int)buf[7] << 16) |
               ((int)buf[8] <<  8) | buf[9];
        pos  = 10;
    }

    int mask_len = masked ? 4 : 0;
    if (buf_len < pos + mask_len + plen) return FrameResult::Incomplete;

    uint8_t mask[4] = {};
    if (masked) {
        mask[0] = buf[pos]; mask[1] = buf[pos+1];
        mask[2] = buf[pos+2]; mask[3] = buf[pos+3];
        pos += 4;
    }

    // Decode payload
    int copy = plen < out_cap - 1 ? plen : out_cap - 1;
    for (int i = 0; i < copy; ++i)
        out_buf[i] = (char)(buf[pos + i] ^ (masked ? mask[i & 3] : 0));
    out_buf[copy] = '\0';
    out_len = copy;

    (void)fin; // fragmentation: treat all as complete for now

    int consumed = pos + plen;

    switch (opcode) {
        case WsOpcode::Text:
        case WsOpcode::Binary:
        case WsOpcode::Continuation:
            return FrameResult::Complete;
        case WsOpcode::Ping:
            return FrameResult::Ping;
        case WsOpcode::Pong:
            // Ignore server pongs — just consume the frame
            out_len = 0;
            return FrameResult::Complete;
        case WsOpcode::Close:
            return FrameResult::Close;
        default:
            return FrameResult::Error;
    }
    (void)consumed;
}

} // namespace ao
