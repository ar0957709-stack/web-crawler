// Bounded, thread-safe producer-consumer buffer (mutex + 2 condition variables).
#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <utility>

namespace wc {

template <typename T>
class BoundedBuffer {
public:
    explicit BoundedBuffer(std::size_t capacity) : capacity_(capacity ? capacity : 1) {}

    // Blocks while full. Returns false if the buffer was closed.
    bool push(T item) {
        std::unique_lock<std::mutex> lk(m_);
        not_full_.wait(lk, [&] { return closed_ || q_.size() < capacity_; });
        if (closed_) return false;
        q_.push_back(std::move(item));
        not_empty_.notify_one();
        return true;
    }

    // Blocks while empty. Returns false once closed AND drained.
    bool pop(T& out) {
        std::unique_lock<std::mutex> lk(m_);
        not_empty_.wait(lk, [&] { return closed_ || !q_.empty(); });
        if (q_.empty()) return false;
        out = std::move(q_.front());
        q_.pop_front();
        not_full_.notify_one();
        return true;
    }

    void close() {
        { std::lock_guard<std::mutex> lk(m_); closed_ = true; }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    std::size_t size() const { std::lock_guard<std::mutex> lk(m_); return q_.size(); }

private:
    mutable std::mutex m_;
    std::condition_variable not_full_, not_empty_;
    std::deque<T> q_;
    std::size_t capacity_;
    bool closed_ = false;
};

}  // namespace wc
