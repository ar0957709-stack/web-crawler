#include "crawler/graph.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <ostream>

namespace wc {

int WebGraph::add_node(const std::string& url) {
    if (const int* id = ids_.find(url)) return *id;
    int id = static_cast<int>(urls_.size());
    ids_.insert(url, id);
    urls_.push_back(url);
    adj_.emplace_back();
    indeg_.push_back(0);
    broken_.push_back(0);
    return id;
}

bool WebGraph::add_edge(int from, int to) {
    if (from < 0 || to < 0 || from >= static_cast<int>(urls_.size()) ||
        to >= static_cast<int>(urls_.size()))
        return false;
    adj_[from].push_back(to);
    ++indeg_[to];
    ++edges_;
    return true;
}

int WebGraph::id_of(const std::string& url) const {
    const int* id = ids_.find(url);
    return id ? *id : -1;
}

std::vector<int> WebGraph::broken_nodes() const {
    std::vector<int> out;
    for (std::size_t i = 0; i < broken_.size(); ++i)
        if (broken_[i]) out.push_back(static_cast<int>(i));
    return out;
}

std::vector<double> WebGraph::pagerank(int iterations, double d) const {
    const std::size_t n = urls_.size();
    if (n == 0) return {};
    std::vector<double> rank(n, 1.0 / n), next(n);
    for (int it = 0; it < iterations; ++it) {
        std::fill(next.begin(), next.end(), (1.0 - d) / n);
        double dangling = 0;
        for (std::size_t i = 0; i < n; ++i) {
            if (adj_[i].empty()) { dangling += rank[i]; continue; }
            double share = d * rank[i] / adj_[i].size();
            for (int j : adj_[i]) next[j] += share;
        }
        double spread = d * dangling / n;
        double diff = 0;
        for (std::size_t i = 0; i < n; ++i) {
            next[i] += spread;
            diff += std::fabs(next[i] - rank[i]);
        }
        rank.swap(next);
        if (diff < 1e-10) break;
    }
    return rank;
}

std::vector<std::pair<int, int>> WebGraph::bfs(int start, int max_depth) const {
    std::vector<std::pair<int, int>> order;
    if (start < 0 || start >= static_cast<int>(urls_.size())) return order;
    std::vector<char> seen(urls_.size(), 0);
    std::size_t head = 0;
    order.emplace_back(start, 0);
    seen[start] = 1;
    while (head < order.size()) {
        auto [u, depth] = order[head++];
        if (depth >= max_depth) continue;
        for (int v : adj_[u])
            if (!seen[v]) { seen[v] = 1; order.emplace_back(v, depth + 1); }
    }
    return order;
}

std::vector<std::pair<int, int>> WebGraph::dfs(int start, int max_depth) const {
    std::vector<std::pair<int, int>> order;
    if (start < 0 || start >= static_cast<int>(urls_.size())) return order;
    std::vector<char> seen(urls_.size(), 0);
    std::vector<std::pair<int, int>> stack{{start, 0}};
    while (!stack.empty()) {
        auto [u, depth] = stack.back();
        stack.pop_back();
        if (seen[u]) continue;
        seen[u] = 1;
        order.emplace_back(u, depth);
        if (depth >= max_depth) continue;
        for (auto it = adj_[u].rbegin(); it != adj_[u].rend(); ++it)
            if (!seen[*it]) stack.emplace_back(*it, depth + 1);
    }
    return order;
}

namespace {
std::string dot_label(const std::string& url) {
    std::string s = url;
    std::size_t p = s.find("://");
    if (p != std::string::npos) s = s.substr(p + 3);
    if (s.size() > 42) s = s.substr(0, 39) + "...";
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out;
}
}  // namespace

void WebGraph::export_dot(std::ostream& os, const std::vector<double>& ranks,
                          std::size_t top_n) const {
    std::vector<int> ids(urls_.size());
    std::iota(ids.begin(), ids.end(), 0);
    top_n = std::min(top_n, ids.size());
    std::partial_sort(ids.begin(), ids.begin() + top_n, ids.end(),
                      [&](int a, int b) { return ranks[a] > ranks[b]; });
    ids.resize(top_n);
    std::vector<char> in(urls_.size(), 0);
    for (int id : ids) in[id] = 1;
    double max_rank = ids.empty() ? 1.0 : ranks[ids.front()];

    os << "digraph web {\n  rankdir=LR;\n  node [shape=box, style=filled, fontname=Helvetica];\n";
    for (int id : ids) {
        double rel = max_rank > 0 ? ranks[id] / max_rank : 0;
        const char* color = broken_[id] ? "#f4b6b6" : (rel > 0.66 ? "#7fb3f5" : (rel > 0.33 ? "#b5d3f8" : "#e3eefc"));
        os << "  n" << id << " [label=\"" << dot_label(urls_[id]) << "\", fillcolor=\"" << color
           << "\", fontsize=" << static_cast<int>(8 + 10 * rel) << "];\n";
    }
    for (int id : ids)
        for (int v : adj_[id])
            if (in[v]) os << "  n" << id << " -> n" << v << ";\n";
    os << "}\n";
}

}  // namespace wc
