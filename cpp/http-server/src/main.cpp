#include <cstdlib>
#include <iostream>

#include "server.hpp"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " <port> <docroot>\n";
        return 1;
    }

    int port = std::atoi(argv[1]);
    std::string docroot = argv[2];

    http::Server server(port, docroot);
    server.run();

    return 0;
}
