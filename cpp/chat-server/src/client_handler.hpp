#ifndef CHAT_SERVER_CLIENT_HANDLER_HPP
#define CHAT_SERVER_CLIENT_HANDLER_HPP

#include <string>

namespace chat {

class Server;

// Owns the per-connection lifecycle: reads lines from a client socket,
// forwards them to the server for broadcast, and cleans up when the
// connection closes or the server is shutting down. One instance of
// handleClient runs per client thread.
void handleClient(Server& server, int clientFd, std::string peerAddress);

}  // namespace chat

#endif  // CHAT_SERVER_CLIENT_HANDLER_HPP
