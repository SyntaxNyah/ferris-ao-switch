#pragma once
#include <cstdint>
#include <cstddef>

namespace ao {

enum class WsOpcode : uint8_t {
    Continuation = 0x0,
    Text         = 0x1,
    Binary       = 0x2,
    Close        = 0x8,
    Ping         = 0x9,
    Pong         = 0xA,
};

enum class FrameResult {
    Complete,       // payload decoded into out_buf / out_len
    Incomplete,     // need more bytes (returns 0 consumed)
    Ping,           // server sent a ping; client should pong (payload in out_buf)
    Close,          // server closed the connection
    Error,          // protocol error
};

// Encode a client→server frame (always masked, binary opcode).
// Returns the number of bytes written into out_buf.
// out_buf must be at least payload_len + 10 bytes.
int ws_encode_frame(const char* payload, int payload_len,
                    uint8_t* out_buf, int out_cap);

// Decode a server→client frame from buf[0..buf_len).
// On Complete/Ping: out_buf receives the unmasked payload, out_len its length.
// `consumed` is set to the total frame size (header + payload) so the caller
// can advance its buffer — it must NOT re-derive this itself (the 8-byte
// length form is easy to get wrong). On Incomplete, consumed is 0.
FrameResult ws_decode_frame(const uint8_t* buf, int buf_len,
                             char* out_buf, int out_cap, int& out_len, int& consumed);

} // namespace ao
