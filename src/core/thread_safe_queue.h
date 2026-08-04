#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <utility>

namespace ocarina {

template<typename T>
class ThreadSafeQueue {
public:
    void push(T item) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(item));
        }
        cv_.notify_one();
    }

    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void wait_and_pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return !queue_.empty(); });
        item = std::move(queue_.front());
        queue_.pop();
    }

    /// Waits until an item is available or @p should_stop returns true.
    /// Returns false when stopped with an empty queue (no item consumed).
    template<typename Pred>
    bool wait_and_pop(T& item, Pred&& should_stop) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this, &should_stop] {
            return !queue_.empty() || should_stop();
        });
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void notify_all() {
        cv_.notify_all();
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<T> queue_;
};

}// namespace ocarina
