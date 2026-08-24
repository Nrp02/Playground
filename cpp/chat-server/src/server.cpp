#include "server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>

#include "client_handler.hpp"
#include "protocol.hpp"

namespace chat {

Server::Server(uint16_t port) : port_(port) {}

Server::~Server() {
    stop();
    joinAllWorkers();
    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
}

bool Server::start() {
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << '\n';
        return false;
    }

    int opt = 1;
    if (setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        std::cerr << "setsockopt(SO_REUSEADDR) failed: " << std::strerror(errno) << '\n';
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed on port " << port_ << ": " << std::strerror(errno) << '\n';
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    constexpr int kBacklog = 32;
    if (listen(listenFd_, kBacklog) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << '\n';
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }

    running_.store(true);
    std::cout << '[' << protocol::timestamp() << "] Chat server listening on port " << port_ << '\n';
    return true;
}

void Server::run() {
    while (running_.load()) {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);
        int clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

        if (clientFd < 0) {
            if (!running_.load()) {
                // Expected: stop() closed the listening socket to unblock us.
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "accept() failed: " << std::strerror(errno) << '\n';
            continue;
        }

        char ipBuf[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &clientAddr.sin_addr, ipBuf, sizeof(ipBuf));
        std::ostringstream addrStream;
        addrStream << ipBuf << ':' << ntohs(clientAddr.sin_port);

        // Disable Nagle's algorithm so chat messages are flushed immediately
        // rather than batched, which matters for interactive latency.
        int one = 1;
        setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

        reapFinishedWorkers();
        workers_.emplace_back(handleClient, std::ref(*this), clientFd, addrStream.str());
    }

    // Once the accept loop exits, tell every remaining client we're going
    // away and then shut down their sockets so the blocking recv() calls
    // in each client thread return and the threads can be joined. This
    // runs on the thread that called run() (not the signal handler that
    // triggered stop() — see main.cpp), so it's safe to take locks and do
    // normal I/O here.
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        static const std::string kGoodbye = "Server is shutting down. Goodbye!\n";
        for (auto& [fd, info] : clients_) {
            send(fd, kGoodbye.data(), kGoodbye.size(), MSG_NOSIGNAL);
            shutdown(fd, SHUT_RDWR);
        }
    }
}

void Server::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;  // already stopped (or never started)
    }
    if (listenFd_ >= 0) {
        // shutdown() unblocks a thread parked in accept(); close() alone
        // is not guaranteed to do that on all platforms.
        shutdown(listenFd_, SHUT_RDWR);
    }
}

void Server::addClient(const ClientInfo& info) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_[info.fd] = info;
}

void Server::removeClient(int fd) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.erase(fd);
    close(fd);
}

void Server::broadcast(const std::string& message, int senderFd, const std::string& senderName) {
    std::string line = protocol::formatBroadcastLine(message, senderName);

    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& [fd, info] : clients_) {
        if (fd == senderFd) {
            continue;
        }
        ssize_t sent = 0;
        while (sent < static_cast<ssize_t>(line.size())) {
            ssize_t n = send(fd, line.data() + sent, line.size() - sent, MSG_NOSIGNAL);
            if (n <= 0) {
                break;  // client is going away; its own thread will clean it up
            }
            sent += n;
        }
    }
}

std::string Server::listUsers() {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    std::ostringstream out;
    out << "Online (" << clients_.size() << "): ";
    bool first = true;
    for (auto& [fd, info] : clients_) {
        if (!first) out << ", ";
        out << info.name;
        first = false;
    }
    return out.str();
}

void Server::reapFinishedWorkers() {
    // std::thread doesn't expose "is this done yet" directly, so instead of
    // polling we simply detach threads once the vector grows large and let
    // the OS reclaim them; the vector itself is only ever fully joined in
    // joinAllWorkers() during shutdown. This keeps memory bounded without
    // needing a completion flag per thread.
    constexpr size_t kReapThreshold = 256;
    if (workers_.size() < kReapThreshold) {
        return;
    }
    for (auto& t : workers_) {
        if (t.joinable()) {
            t.detach();
        }
    }
    workers_.clear();
}

void Server::joinAllWorkers() {
    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
    workers_.clear();
}

}  // namespace chat
