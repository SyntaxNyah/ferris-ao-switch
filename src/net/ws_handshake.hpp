#pragma once

namespace ao {

// Perform an RFC 6455 WebSocket upgrade on an already-connected TCP socket.
// sock_fd:  platform socket descriptor (SDL_net TCPsocket cast to int, or
//           use the raw fd via SDLNet_TCP_GetPeerAddress hack).
// host:     the Host header value (e.g. "game.example.com")
// path:     the request path (e.g. "/" or "/ws")
//
// Returns true if the server replied with 101 Switching Protocols and a
// valid Sec-WebSocket-Accept header.
//
// This is a blocking call — call from the network thread only.

struct TCPsocket_tag;

bool ws_upgrade(void* sdl_tcp_socket, const char* host, const char* path = "/");

} // namespace ao
