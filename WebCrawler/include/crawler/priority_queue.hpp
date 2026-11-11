// Binary max-heap (the priority queue behind the URL frontier).
// push / pop: O(log n), top: O(1).
#pragma once
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

namespace wc {

template <typename T, typename Compare = std::less<T>>
class MaxHeap {
public:
    void push(T value) {
        data_.push_back(std::move(value));
        sift_up(data_.size() - 1);
    }
    const T& top() const { return data_.front(); }
    void pop() {
        std::swap(data_.front(), data_.back());
        data_.pop_back();
        if (!data_.empty()) sift_down(0);
    }
    bool empty() const { return data_.empty(); }
    std::size_t size() const { return data_.size(); }

private:
    void sift_up(std::size_t i) {
        while (i > 0) {
            std::size_t p = (i - 1) / 2;
            if (!cmp_(data_[p], data_[i])) break;
            std::swap(data_[p], data_[i]);
            i = p;
        }
    }
    void sift_down(std::size_t i) {
        const std::size_t n = data_.size();
        for (;;) {
            std::size_t l = 2 * i + 1, r = l + 1, big = i;
            if (l < n && cmp_(data_[big], data_[l])) big = l;
            if (r < n && cmp_(data_[big], data_[r])) big = r;
            if (big == i) break;
            std::swap(data_[i], data_[big]);
            i = big;
        }
    }

    std::vector<T> data_;
    Compare cmp_;
};

}  // namespace wc
