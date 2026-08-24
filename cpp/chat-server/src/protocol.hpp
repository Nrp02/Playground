#ifndef CHAT_SERVER_PROTOCOL_HPP
#define CHAT_SERVER_PROTOCOL_HPP

#include <string>

namespace chat::protocol {

// Pure, socket-free helpers shared by server.cpp and client_handler.cpp.
// They live in their own translation unit specifically so they can be
// exercised by tests/test_protocol.cpp without spinning up a real socket,
// thread, or Server instance.

// Strips leading/trailing whitespace (space, tab, CR, LF).
std::string trim(const std::string& s);

// Formats a chat message for broadcast: "[senderName] message\n". If
// `senderName` is empty (used for system/join/leave announcements), the
// message is sent as-is with a trailing newline appended and no "[...]"
// prefix. Never doubles an already-present trailing newline.
std::string formatBroadcastLine(const std::string& message, const std::string& senderName);

// Current local wall-clock time formatted as "HH:MM:SS", used to prefix
// server log lines.
std::string timestamp();

}  // namespace chat::protocol

#endif  // CHAT_SERVER_PROTOCOL_HPP
