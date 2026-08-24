#include "server.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>

namespace http {

namespace {

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream oss;
    oss << file.rdbuf();
    out = oss.str();
    return true;
}

bool is_safe_path(const std::string& path) {
    return path.find("..") == std::string::npos;
}

}

Server::Server(int port, std::string docroot) : port_(port), docroot_(std::move(docroot)) {}

Server::~Server() {
    for (auto& [fd, conn] : connections_) {
        close(fd);
    }
    if (listen_fd_ >= 0) close(listen_fd_);
}

void Server::run() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        std::cerr << "socket() failed: " << strerror(errno) << "\n";
        return;
    }

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << strerror(errno) << "\n";
        return;
    }

    if (listen(listen_fd_, 128) < 0) {
        std::cerr << "listen() failed: " << strerror(errno) << "\n";
        return;
    }

    set_nonblocking(listen_fd_);
    std::cout << "http-server listening on port " << port_ << ", serving " << docroot_ << "\n";

    while (true) {
        std::vector<pollfd> fds;
        fds.push_back({listen_fd_, POLLIN, 0});
        for (auto& [fd, conn] : connections_) {
            short events = POLLIN;
            if (!conn.outbuf.empty()) events |= POLLOUT;
            fds.push_back({fd, events, 0});
        }

        int ready = poll(fds.data(), fds.size(), -1);
        if (ready < 0) {
            if (errno == EINTR) continue;
            std::cerr << "poll() failed: " << strerror(errno) << "\n";
            break;
        }

        for (auto& pfd : fds) {
            if (pfd.revents == 0) continue;
            if (pfd.fd == listen_fd_) {
                if (pfd.revents & POLLIN) accept_new();
                continue;
            }
            if (pfd.revents & (POLLHUP | POLLERR)) {
                close_connection(pfd.fd);
                continue;
            }
            if (pfd.revents & POLLIN) {
                handle_readable(pfd.fd);
                if (connections_.find(pfd.fd) == connections_.end()) continue;
            }
            if (pfd.revents & POLLOUT) {
                handle_writable(pfd.fd);
            }
        }
    }
}

void Server::accept_new() {
    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &len);
        if (fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "accept() failed: " << strerror(errno) << "\n";
            }
            break;
        }
        set_nonblocking(fd);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        connections_[fd] = Connection{};
        connections_[fd].fd = fd;
    }
}

void Server::handle_readable(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    Connection& conn = it->second;

    char buf[8192];
    while (true) {
        ssize_t n = recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            ParseStatus status = conn.parser.feed(buf, static_cast<size_t>(n));
            if (status == ParseStatus::Complete) {
                Request req = conn.parser.take_request();
                handle_request(conn, req);
            } else if (status == ParseStatus::Error) {
                queue_response(conn, build_error_response(400, "Bad Request", false));
                return;
            }
            if (static_cast<size_t>(n) < sizeof(buf)) break;
        } else if (n == 0) {
            close_connection(fd);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            close_connection(fd);
            return;
        }
    }
}

void Server::handle_request(Connection& conn, const Request& req) {
    bool keep_alive = is_connection_keep_alive(req);

    if (req.method != "GET" && req.method != "POST") {
        queue_response(conn, build_error_response(405, "Method Not Allowed", keep_alive));
        return;
    }

    if (!is_safe_path(req.path)) {
        queue_response(conn, build_error_response(403, "Forbidden", keep_alive));
        return;
    }

    if (req.method == "POST") {
        std::string body = "Received " + std::to_string(req.body.size()) + " bytes\n";
        queue_response(conn, build_response(200, "OK", "text/plain", body, keep_alive));
        return;
    }

    std::string path = req.path;
    if (path == "/") path = "/index.html";
    std::string full_path = docroot_ + path;

    struct stat st{};
    if (stat(full_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        queue_response(conn, build_error_response(404, "Not Found", keep_alive));
        return;
    }

    std::string content;
    if (!read_file(full_path, content)) {
        queue_response(conn, build_error_response(500, "Internal Server Error", keep_alive));
        return;
    }

    queue_response(conn, build_response(200, "OK", content_type_for_path(full_path), content, keep_alive));
}

void Server::queue_response(Connection& conn, const std::string& response) {
    conn.outbuf += response;
}

void Server::handle_writable(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    Connection& conn = it->second;

    while (conn.outpos < conn.outbuf.size()) {
        ssize_t n = send(fd, conn.outbuf.data() + conn.outpos, conn.outbuf.size() - conn.outpos, 0);
        if (n > 0) {
            conn.outpos += static_cast<size_t>(n);
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        } else {
            close_connection(fd);
            return;
        }
    }

    conn.outbuf.clear();
    conn.outpos = 0;
}

void Server::close_connection(int fd) {
    close(fd);
    connections_.erase(fd);
}

}
