#pragma once

namespace ao {

// Abstract send/recv callbacks so ws_upgrade works over both plain TCP
// (SDL_net) and TLS (mbedtls) without linking either directly.
//
// send_fn: write len bytes; returns bytes sent (>0) or error (<=0).
// recv_fn: read up to cap bytes; returns bytes read (>0) or error (<=0).
// ctx:     opaque pointer forwarded to both callbacks (e.g. TCPsocket* or TlsConn*).
typedef int (*WsSendFn)(void* ctx, const void* data, int len);
typedef int (*WsRecvFn)(void* ctx, void*       buf,  int cap);

// Perform an RFC 6455 WebSocket upgrade over an already-connected transport.
// Returns true if the server replies 101 Switching Protocols with a valid
// Sec-WebSocket-Accept header.  Blocking — call from the network thread only.
bool ws_upgrade(WsSendFn send_fn, WsRecvFn recv_fn, void* ctx,
                const char* host, const char* path = "/");

} // namespace ao
