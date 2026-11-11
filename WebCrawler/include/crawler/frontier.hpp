// URL frontier: a thread-safe priority queue. Workers block on a condition
// variable until a URL is available or the frontier is closed.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>

#include "crawler/priority_queue.hpp"

namespace wc {

struct UrlTask {
    std::string url;
    int depth = 0;
    int priority = 0;
    std::uint64_t seq = 0;  // FIFO tie-break among equal priorities
};

struct UrlTaskLess {
    bool operator()(const UrlTask& a, const UrlTask& b) const {
        if (a.priority != b.priority) return a.priority < b.priority;
        return a.seq > b.seq;  // lower seq = older = "greater"
    }
};

class Frontier {
public:
    void push(const std::string& url, int depth, int priority) {
        {
            std::lock_guard<std::mutex> lk(m_);
            heap_.push(UrlTask{url, depth, priority, next_seq_++});
        }
        cv_.notify_one();
    }

    // Blocks until a task is available. Returns false when closed and empty.
    bool pop(UrlTask& out) {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait(lk, [&] { return closed_ || !heap_.empty(); });
        if (heap_.empty()) return false;
        out = heap_.top();
        heap_.pop();
        return true;
    }

    void close() {
        { std::lock_guard<std::mutex> lk(m_); closed_ = true; }
        cv_.notify_all();
    }

    std::size_t size() const { std::lock_guard<std::mutex> lk(m_); return heap_.size(); }

private:
    mutable std::mutex m_;
    std::condition_variable cv_;
    MaxHeap<UrlTask, UrlTaskLess> heap_;
    std::uint64_t next_seq_ = 0;
    bool closed_ = false;
};

}  // namespace wc
