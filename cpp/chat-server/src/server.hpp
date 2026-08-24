#ifndef CHAT_SERVER_SERVER_HPP
#define CHAT_SERVER_SERVER_HPP

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace chat {

// Represents a single connected client, tracked by the server so that
// broadcasts can reach every socket except the sender.
struct ClientInfo {
    int fd = -1;
    std::string name;
    std::string address;
};

// TCP chat server. Listens on a port, accepts connections on the calling
// thread's loop, and spins up one worker thread per client. All client
// bookkeeping is protected by a mutex since the accept loop and every
// client thread can touch it concurrently.
class Server {
public:
    explicit Server(uint16_t port);
    ~Server();

    // Not copyable: owns raw OS resources (the listening socket, threads).
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Creates the listening socket, binds, and starts listening.
    // Returns false and logs an error on failure.
    bool start();

    // Runs the accept loop on the calling thread. Blocks until stop() is
    // called from another thread (typically a signal handler) or a fatal
    // accept error occurs.
    void run();

    // Signals the accept loop and all client threads to shut down. Safe to
    // call from a signal handler because it only touches an atomic flag and
    // shuts down the listening socket.
    void stop();

    bool isRunning() const { return running_.load(); }

    // Sends `message` to every connected client whose fd is not
    // `senderFd` (pass -1 to send to everyone, e.g. server announcements).
    void broadcast(const std::string& message, int senderFd, const std::string& senderName);

    // Removes a client from the roster and closes its socket. Called by a
    // client's own handler thread when the connection ends.
    void removeClient(int fd);

    // Registers a newly-accepted client in the roster.
    void addClient(const ClientInfo& info);

    // Returns a human-readable list of currently connected user names.
    std::string listUsers();

private:
    uint16_t port_;
    int listenFd_ = -1;
    std::atomic<bool> running_{false};

    std::mutex clientsMutex_;
    std::unordered_map<int, ClientInfo> clients_;
    std::vector<std::thread> workers_;

    void reapFinishedWorkers();
    void joinAllWorkers();
};

}  // namespace chat

#endif  // CHAT_SERVER_SERVER_HPP
