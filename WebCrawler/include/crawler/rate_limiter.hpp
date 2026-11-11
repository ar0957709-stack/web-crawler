// Per-domain politeness: guarantees at least `delay_ms` between two requests
// to the same domain, no matter how many threads are asking.
#pragma once
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include "crawler/hash_table.hpp"

namespace wc {

class RateLimiter {
    using Clock = std::chrono::steady_clock;

public:
    explicit RateLimiter(int delay_ms) : delay_(delay_ms) {}

    // Reserves the next free slot for `domain` (under the lock) and then
    // sleeps OUTSIDE the lock until that slot arrives.
    void acquire(const std::string& domain) {
        if (delay_.count() <= 0) return;
        Clock::time_point slot;
        {
            std::lock_guard<std::mutex> lk(m_);
            Clock::time_point& next = next_slot_.get_or_insert(domain);
            Clock::time_point now = Clock::now();
            slot = next > now ? next : now;
            next = slot + delay_;
        }
        std::this_thread::sleep_until(slot);
    }

private:
    std::chrono::milliseconds delay_;
    std::mutex m_;
    HashTable<std::string, Clock::time_point> next_slot_;
};

}  // namespace wc
