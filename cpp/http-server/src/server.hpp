#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "http_parser.hpp"

namespace http {

class Server {
public:
    Server(int port, std::string docroot);
    ~Server();

    void run();

private:
    struct Connection {
        int fd = -1;
        RequestParser parser;
        std::string outbuf;
        size_t outpos = 0;
    };

    void accept_new();
    void handle_readable(int fd);
    void handle_writable(int fd);
    void close_connection(int fd);
    void handle_request(Connection& conn, const Request& req);
    void queue_response(Connection& conn, const std::string& response);

    int listen_fd_ = -1;
    int port_;
    std::string docroot_;
    std::unordered_map<int, Connection> connections_;
};

}
