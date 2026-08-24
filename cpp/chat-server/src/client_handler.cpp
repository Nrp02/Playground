#include "client_handler.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <mutex>

#include "protocol.hpp"
#include "server.hpp"

namespace chat {

namespace {

constexpr size_t kRecvChunk = 4096;
constexpr size_t kMaxLineLength = 8192;

// std::cout is not synchronized across threads by the standard, so without
// this, log lines from two client threads logging at the same instant can
// interleave mid-string. Every client thread logs through this helper.
std::mutex g_logMutex;

void logLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::cout << '[' << protocol::timestamp() << "] " << line << '\n';
}

// Sends a raw string (no sender prefix) directly to one socket. Used for
// server-to-client system messages like the name prompt or welcome text.
bool sendRaw(int fd, const std::string& text) {
    size_t sent = 0;
    while (sent < text.size()) {
        ssize_t n = send(fd, text.data() + sent, text.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

}  // namespace

void handleClient(Server& server, int clientFd, std::string peerAddress) {
    if (!sendRaw(clientFd, "Welcome to the chat server. Enter your name: ")) {
        close(clientFd);
        return;
    }

    std::string buffer;
    buffer.reserve(kRecvChunk);
    char raw[kRecvChunk];

    // Helper: pull one newline-terminated line out of `buffer`, refilling
    // from the socket as needed. Returns false on EOF/error/oversize line.
    auto readLine = [&](std::string& outLine) -> bool {
        while (true) {
            size_t nl = buffer.find('\n');
            if (nl != std::string::npos) {
                outLine = buffer.substr(0, nl);
                buffer.erase(0, nl + 1);
                return true;
            }
            if (buffer.size() > kMaxLineLength) {
                return false;  // guard against unbounded memory growth
            }
            ssize_t n = recv(clientFd, raw, sizeof(raw), 0);
            if (n <= 0) {
                return false;  // 0 = orderly shutdown, <0 = error/interrupted
            }
            buffer.append(raw, static_cast<size_t>(n));
        }
    };

    std::string nameLine;
    if (!readLine(nameLine) || !server.isRunning()) {
        close(clientFd);
        return;
    }
    std::string name = protocol::trim(nameLine);
    if (name.empty()) {
        name = "anonymous-" + std::to_string(clientFd);
    }
    // Truncate absurdly long names rather than rejecting the connection.
    if (name.size() > 32) {
        name = name.substr(0, 32);
    }

    ClientInfo info;
    info.fd = clientFd;
    info.name = name;
    info.address = peerAddress;
    server.addClient(info);

    logLine("[+] " + name + " connected from " + peerAddress +
             " (fd=" + std::to_string(clientFd) + ")");

    sendRaw(clientFd, "Hello, " + name + "! Type a message and press enter to broadcast it.\n");
    server.broadcast(name + " has joined the chat.", clientFd, "");

    std::string line;
    while (server.isRunning() && readLine(line)) {
        std::string msg = protocol::trim(line);
        if (msg.empty()) {
            continue;
        }
        if (msg == "/quit") {
            break;
        }
        if (msg == "/who") {
            sendRaw(clientFd, server.listUsers() + "\n");
            continue;
        }
        logLine("[msg] " + name + ": " + msg);
        server.broadcast(msg, clientFd, name);
    }

    server.removeClient(clientFd);
    server.broadcast(name + " has left the chat.", clientFd, "");
    logLine("[-] " + name + " disconnected");
}

}  // namespace chat
