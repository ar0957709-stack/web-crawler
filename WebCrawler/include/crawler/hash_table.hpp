// Hand-written separate-chaining hash table and hash set.
// NOT thread-safe on its own: the crawler guards it with a mutex.
#pragma once
#include <cstddef>
#include <functional>
#include <vector>

namespace wc {

template <typename K, typename V, typename Hash = std::hash<K>>
class HashTable {
    struct Node {
        K key;
        V value;
        Node* next;
        Node(const K& k, const V& v, Node* n) : key(k), value(v), next(n) {}
    };

public:
    explicit HashTable(std::size_t initial_buckets = 16)
        : buckets_(initial_buckets < 8 ? 8 : initial_buckets, nullptr) {}
    ~HashTable() { clear(); }
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    // Returns true if a new key was inserted, false if an existing value was overwritten.
    bool insert(const K& key, const V& value) {
        std::size_t i = index(key);
        for (Node* n = buckets_[i]; n; n = n->next) {
            if (n->key == key) { n->value = value; return false; }
        }
        if ((size_ + 1) * 4 > buckets_.size() * 3) {  // load factor > 0.75
            rehash(buckets_.size() * 2);
            i = index(key);
        }
        buckets_[i] = new Node(key, value, buckets_[i]);
        ++size_;
        return true;
    }

    V* find(const K& key) {
        for (Node* n = buckets_[index(key)]; n; n = n->next)
            if (n->key == key) return &n->value;
        return nullptr;
    }
    const V* find(const K& key) const {
        for (const Node* n = buckets_[index(key)]; n; n = n->next)
            if (n->key == key) return &n->value;
        return nullptr;
    }
    bool contains(const K& key) const { return find(key) != nullptr; }

    // Value must be default constructible.
    V& get_or_insert(const K& key) {
        if (V* v = find(key)) return *v;
        insert(key, V());
        return *find(key);
    }

    bool erase(const K& key) {
        std::size_t i = index(key);
        Node* prev = nullptr;
        for (Node* n = buckets_[i]; n; prev = n, n = n->next) {
            if (n->key == key) {
                if (prev) prev->next = n->next; else buckets_[i] = n->next;
                delete n;
                --size_;
                return true;
            }
        }
        return false;
    }

    void clear() {
        for (Node*& head : buckets_) {
            while (head) { Node* nx = head->next; delete head; head = nx; }
        }
        size_ = 0;
    }

    template <typename F>
    void for_each(F&& f) const {
        for (const Node* head : buckets_)
            for (const Node* n = head; n; n = n->next) f(n->key, n->value);
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    std::size_t bucket_count() const { return buckets_.size(); }

private:
    std::size_t index(const K& key) const { return hash_(key) % buckets_.size(); }

    void rehash(std::size_t new_count) {
        std::vector<Node*> nb(new_count, nullptr);
        for (Node* head : buckets_) {
            while (head) {
                Node* nx = head->next;
                std::size_t i = hash_(head->key) % new_count;
                head->next = nb[i];
                nb[i] = head;
                head = nx;
            }
        }
        buckets_.swap(nb);
    }

    std::vector<Node*> buckets_;
    std::size_t size_ = 0;
    Hash hash_;
};

template <typename K, typename Hash = std::hash<K>>
class HashSet {
public:
    // Returns true if the key was newly inserted.
    bool insert(const K& key) { return table_.insert(key, 0); }
    bool contains(const K& key) const { return table_.contains(key); }
    bool erase(const K& key) { return table_.erase(key); }
    std::size_t size() const { return table_.size(); }
    bool empty() const { return table_.empty(); }
    template <typename F>
    void for_each(F&& f) const { table_.for_each([&](const K& k, char) { f(k); }); }

private:
    HashTable<K, char, Hash> table_;
};

}  // namespace wc
