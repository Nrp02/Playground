#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace connpool {

class SimulatedConnection {
public:
    explicit SimulatedConnection(int id, bool flaky = false)
        : id_(id), flaky_(flaky) {}

    int id() const { return id_; }

    bool isHealthy() const { return healthy_.load(std::memory_order_acquire); }

    void markUnhealthy() { healthy_.store(false, std::memory_order_release); }

    void execute() {
        ++executeCount_;
        if (flaky_ && executeCount_ >= flakyThreshold_) {
            markUnhealthy();
        }
    }

    int executeCount() const { return executeCount_; }

private:
    int id_;
    bool flaky_;
    int executeCount_ = 0;
    static constexpr int flakyThreshold_ = 3;
    std::atomic<bool> healthy_{true};
};

class ConnectionPool {
public:
    using ConnectionPtr = std::unique_ptr<SimulatedConnection>;
    using Factory = std::function<ConnectionPtr(int)>;
    using HealthCheck = std::function<bool(SimulatedConnection&)>;

    class Handle {
    public:
        Handle() = default;

        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;

        Handle(Handle&& other) noexcept { moveFrom(std::move(other)); }

        Handle& operator=(Handle&& other) noexcept {
            if (this != &other) {
                release();
                moveFrom(std::move(other));
            }
            return *this;
        }

        ~Handle() { release(); }

        bool valid() const { return conn_ != nullptr; }

        SimulatedConnection& get() const { return *conn_; }

        SimulatedConnection* operator->() const { return conn_.get(); }

        SimulatedConnection& operator*() const { return *conn_; }

    private:
        friend class ConnectionPool;

        Handle(ConnectionPool* pool, ConnectionPtr conn)
            : pool_(pool), conn_(std::move(conn)) {}

        void moveFrom(Handle&& other) {
            pool_ = other.pool_;
            conn_ = std::move(other.conn_);
            other.pool_ = nullptr;
        }

        void release() {
            if (conn_ && pool_) {
                pool_->returnConnection(std::move(conn_));
            }
            pool_ = nullptr;
            conn_.reset();
        }

        ConnectionPool* pool_ = nullptr;
        ConnectionPtr conn_;
    };

    explicit ConnectionPool(std::size_t maxSize, Factory factory,
                             HealthCheck healthCheck = defaultHealthCheck,
                             std::chrono::milliseconds healthCheckInterval =
                                 std::chrono::milliseconds(0))
        : maxSize_(maxSize),
          factory_(std::move(factory)),
          healthCheck_(std::move(healthCheck)) {
        if (healthCheckInterval.count() > 0) {
            stopHealthChecker_ = false;
            healthCheckerThread_ =
                std::thread(&ConnectionPool::healthCheckLoop, this, healthCheckInterval);
        }
    }

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    ~ConnectionPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopHealthChecker_ = true;
        }
        cv_.notify_all();
        if (healthCheckerThread_.joinable()) {
            healthCheckerThread_.join();
        }
    }

    Handle acquire(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        auto deadline = std::chrono::steady_clock::now() + timeout;
        bool ready = cv_.wait_until(lock, deadline, [this] {
            return !available_.empty() || created_ < maxSize_;
        });
        if (!ready) {
            return Handle();
        }
        ConnectionPtr conn;
        if (!available_.empty()) {
            conn = std::move(available_.front());
            available_.pop_front();
        } else {
            conn = factory_(nextId_++);
            ++created_;
        }
        return Handle(this, std::move(conn));
    }

    std::size_t availableCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return available_.size();
    }

    std::size_t createdCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return created_;
    }

    std::size_t maxSize() const { return maxSize_; }

    static bool defaultHealthCheck(SimulatedConnection& conn) { return conn.isHealthy(); }

private:
    void returnConnection(ConnectionPtr conn) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (healthCheck_(*conn)) {
            available_.push_back(std::move(conn));
        } else {
            available_.push_back(factory_(nextId_++));
        }
        cv_.notify_one();
    }

    void healthCheckLoop(std::chrono::milliseconds interval) {
        std::unique_lock<std::mutex> lock(mutex_);
        while (!stopHealthChecker_) {
            cv_.wait_for(lock, interval, [this] { return stopHealthChecker_; });
            if (stopHealthChecker_) {
                break;
            }
            std::deque<ConnectionPtr> refreshed;
            while (!available_.empty()) {
                auto conn = std::move(available_.front());
                available_.pop_front();
                if (healthCheck_(*conn)) {
                    refreshed.push_back(std::move(conn));
                } else {
                    refreshed.push_back(factory_(nextId_++));
                }
            }
            available_ = std::move(refreshed);
            cv_.notify_all();
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::size_t maxSize_;
    std::size_t created_ = 0;
    int nextId_ = 1;
    std::deque<ConnectionPtr> available_;
    Factory factory_;
    HealthCheck healthCheck_;
    std::thread healthCheckerThread_;
    bool stopHealthChecker_ = true;
};

}
