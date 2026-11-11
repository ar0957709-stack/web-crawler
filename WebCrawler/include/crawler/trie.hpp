// Trie over URL components: reversed host labels first, then path segments.
//   http://www.example.com/a/b  ->  com / example / www / a / b
// so every page of a domain (or directory) shares one sub-tree.
#pragma once
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

namespace wc {

class Trie {
public:
    Trie();
    ~Trie();
    Trie(const Trie&) = delete;
    Trie& operator=(const Trie&) = delete;

    bool insert(const std::string& url);               // false if already present
    bool contains(const std::string& url) const;
    std::size_t size() const;
    std::size_t count_prefix(const std::string& prefix) const;
    std::vector<std::string> list_prefix(const std::string& prefix, std::size_t limit = 50) const;
    void print_tree(std::ostream& os, std::size_t max_depth = 3, std::size_t max_children = 6) const;

    static std::vector<std::string> components(const std::string& url_or_prefix);

private:
    struct Node;
    const Node* find_node(const std::vector<std::string>& comps) const;
    static void collect(const Node* n, std::vector<std::string>& out, std::size_t limit);
    static void print(const Node* n, std::ostream& os, const std::string& indent,
                      std::size_t depth, std::size_t max_depth, std::size_t max_children);

    std::unique_ptr<Node> root_;
};

}  // namespace wc
