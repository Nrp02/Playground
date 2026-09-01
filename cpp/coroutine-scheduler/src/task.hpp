#pragma once

#include <coroutine>
#include <exception>
#include <functional>
#include <optional>
#include <utility>

namespace cosched {

inline int& frameCount() {
    static int count = 0;
    return count;
}

struct FrameCounter {
    FrameCounter() { ++frameCount(); }
    FrameCounter(const FrameCounter&) = delete;
    FrameCounter& operator=(const FrameCounter&) = delete;
    ~FrameCounter() { --frameCount(); }
};

struct FinalAwaiter {
    bool await_ready() const noexcept { return false; }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto& promise = handle.promise();
        if (promise.onDone) {
            promise.onDone();
        }
        if (promise.continuation) {
            return promise.continuation;
        }
        return std::noop_coroutine();
    }

    void await_resume() noexcept {}
};

template <typename T>
class Task;

template <typename T>
struct PromiseBase : FrameCounter {
    std::coroutine_handle<> continuation;
    std::exception_ptr error;
    std::function<void()> onDone;

    std::suspend_always initial_suspend() noexcept { return {}; }
    FinalAwaiter final_suspend() noexcept { return {}; }
    void unhandled_exception() noexcept { error = std::current_exception(); }
};

template <typename T>
struct PromiseValue : PromiseBase<T> {
    std::optional<T> value;

    Task<T> get_return_object();

    template <typename U>
    void return_value(U&& v) {
        value = std::forward<U>(v);
    }
};

template <>
struct PromiseValue<void> : PromiseBase<void> {
    Task<void> get_return_object();
    void return_void() noexcept {}
};

template <typename T>
class Task {
public:
    using promise_type = PromiseValue<T>;

    explicit Task(std::coroutine_handle<promise_type> handle) : handle_(handle) {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    ~Task() { destroy(); }

    bool await_ready() const noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        handle_.promise().continuation = awaiting;
        return handle_;
    }

    T await_resume() {
        if (handle_.promise().error) {
            std::rethrow_exception(handle_.promise().error);
        }
        if constexpr (!std::is_void_v<T>) {
            return std::move(*handle_.promise().value);
        }
    }

    void start() { handle_.resume(); }

    bool done() const { return !handle_ || handle_.done(); }

    std::coroutine_handle<> handle() const { return handle_; }

    template <typename F>
    void onFinalize(F&& callback) {
        handle_.promise().onDone = std::forward<F>(callback);
    }

    T result() {
        if (handle_.promise().error) {
            std::rethrow_exception(handle_.promise().error);
        }
        if constexpr (!std::is_void_v<T>) {
            return std::move(*handle_.promise().value);
        }
    }

private:
    void destroy() {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }

    std::coroutine_handle<promise_type> handle_;
};

template <typename T>
inline Task<T> PromiseValue<T>::get_return_object() {
    return Task<T>(std::coroutine_handle<PromiseValue<T>>::from_promise(*this));
}

inline Task<void> PromiseValue<void>::get_return_object() {
    return Task<void>(std::coroutine_handle<PromiseValue<void>>::from_promise(*this));
}

}
