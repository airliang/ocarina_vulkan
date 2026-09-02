#pragma once

#include <list>
#include <mutex>
#include <utility>

namespace ocarina {

/// Mutex-protected doubly-linked list for pending work that may stay queued across frames.
template<typename T>
class ThreadSafeList {
public:
    void push_back(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        list_.push_back(std::move(item));
    }

    /// Invokes @p fn for each element; removes the element when @p fn returns true.
    template<typename Fn>
    void for_each_remove_if(Fn&& fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = list_.begin(); it != list_.end();) {
            if (fn(*it)) {
                it = list_.erase(it);
            } else {
                ++it;
            }
        }
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return list_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::list<T> list_;
};

}// namespace ocarina
