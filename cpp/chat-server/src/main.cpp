#include <csignal>
#include <cstdlib>
#include <iostream>

#include "server.hpp"

namespace {

// A signal handler can only safely touch objects with static storage
// duration and async-signal-safe operations, so we keep a raw pointer to
// the running server here and call stop() (which only sets an atomic flag
// and calls shutdown()) from within the handler.
chat::Server* g_server = nullptr;

extern "C" void handleSignal(int signum) {
    (void)signum;
    if (g_server != nullptr) {
        g_server->stop();
    }
}

void installSignalHandlers() {
    struct sigaction action{};
    action.sa_handler = handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);

    // Writing to a socket whose peer already closed its end raises SIGPIPE
    // by default, which would kill the whole process; we handle that
    // condition via send()'s return value instead (see MSG_NOSIGNAL usage).
    signal(SIGPIPE, SIG_IGN);
}

uint16_t parsePort(int argc, char** argv) {
    constexpr uint16_t kDefaultPort = 5555;
    if (argc < 2) {
        return kDefaultPort;
    }
    long port = std::strtol(argv[1], nullptr, 10);
    if (port <= 0 || port > 65535) {
        std::cerr << "Invalid port '" << argv[1] << "', using default " << kDefaultPort << '\n';
        return kDefaultPort;
    }
    return static_cast<uint16_t>(port);
}

}  // namespace

int main(int argc, char** argv) {
    // Flush after every insertion so log lines show up immediately even
    // when stdout is redirected to a file/pipe (fully buffered instead of
    // line buffered), which matters for a long-running server whose output
    // is typically being tailed or captured.
    std::cout << std::unitbuf;

    uint16_t port = parsePort(argc, argv);

    chat::Server server(port);
    g_server = &server;
    installSignalHandlers();

    if (!server.start()) {
        std::cerr << "Failed to start server on port " << port << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "Press Ctrl+C to shut down.\n";
    server.run();

    std::cout << "Server shut down cleanly.\n";
    g_server = nullptr;
    return EXIT_SUCCESS;
}
