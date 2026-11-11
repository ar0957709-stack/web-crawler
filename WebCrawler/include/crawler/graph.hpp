// Directed web graph (adjacency list): pages = vertices, hyperlinks = edges.
#pragma once
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

#include "crawler/hash_table.hpp"

namespace wc {

class WebGraph {
public:
    int add_node(const std::string& url);          // returns existing id if known
    bool add_edge(int from, int to);
    int id_of(const std::string& url) const;       // -1 if unknown
    const std::string& url_of(int id) const { return urls_[id]; }
    std::size_t node_count() const { return urls_.size(); }
    std::size_t edge_count() const { return edges_; }
    const std::vector<int>& neighbors(int id) const { return adj_[id]; }
    int in_degree(int id) const { return indeg_[id]; }

    void set_broken(int id, bool broken) { broken_[id] = broken; }
    bool is_broken(int id) const { return broken_[id] != 0; }
    std::vector<int> broken_nodes() const;

    // Iterative PageRank with dangling-node handling. Ranks sum to ~1.
    std::vector<double> pagerank(int iterations = 50, double damping = 0.85) const;

    // Depth-limited traversals; return (node, depth) in visit order.
    std::vector<std::pair<int, int>> bfs(int start, int max_depth) const;
    std::vector<std::pair<int, int>> dfs(int start, int max_depth) const;

    // Graphviz export of the `top_n` highest ranked pages.
    void export_dot(std::ostream& os, const std::vector<double>& ranks, std::size_t top_n) const;

private:
    HashTable<std::string, int> ids_;
    std::vector<std::string> urls_;
    std::vector<std::vector<int>> adj_;
    std::vector<int> indeg_;
    std::vector<char> broken_;
    std::size_t edges_ = 0;
};

}  // namespace wc
