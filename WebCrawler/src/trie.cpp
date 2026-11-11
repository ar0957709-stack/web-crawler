#include "crawler/trie.hpp"

#include <algorithm>
#include <map>
#include <ostream>

namespace wc {

struct Trie::Node {
    std::map<std::string, std::unique_ptr<Node>> children;
    bool terminal = false;
    std::string url;          // set on terminal nodes
    std::size_t count = 0;    // terminals in this sub-tree
};

Trie::Trie() : root_(new Node) {}
Trie::~Trie() = default;

namespace {
std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = s.find(sep, i);
        if (j == std::string::npos) j = s.size();
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j + 1;
    }
    return out;
}
}  // namespace

std::vector<std::string> Trie::components(const std::string& in) {
    std::string s = in;
    std::size_t p = s.find("://");
    if (p != std::string::npos) s = s.substr(p + 3);
    std::size_t h = s.find('#');
    if (h != std::string::npos) s.resize(h);
    std::string query;
    std::size_t q = s.find('?');
    if (q != std::string::npos) { query = s.substr(q); s.resize(q); }
    std::string host = s, path;
    std::size_t sl = s.find('/');
    if (sl != std::string::npos) { host = s.substr(0, sl); path = s.substr(sl); }

    std::vector<std::string> out = split(host, '.');
    std::reverse(out.begin(), out.end());
    for (auto& seg : split(path, '/')) out.push_back(seg);
    if (!query.empty()) out.push_back(query);
    return out;
}

bool Trie::insert(const std::string& url) {
    std::vector<std::string> comps = components(url);
    // First pass: is it already there? (keeps sub-tree counts exact)
    if (contains(url)) return false;
    Node* cur = root_.get();
    ++cur->count;
    for (const std::string& c : comps) {
        auto& child = cur->children[c];
        if (!child) child.reset(new Node);
        cur = child.get();
        ++cur->count;
    }
    cur->terminal = true;
    cur->url = url;
    return true;
}

const Trie::Node* Trie::find_node(const std::vector<std::string>& comps) const {
    const Node* cur = root_.get();
    for (const std::string& c : comps) {
        auto it = cur->children.find(c);
        if (it == cur->children.end()) return nullptr;
        cur = it->second.get();
    }
    return cur;
}

bool Trie::contains(const std::string& url) const {
    const Node* n = find_node(components(url));
    return n && n->terminal;
}

std::size_t Trie::size() const { return root_->count; }

std::size_t Trie::count_prefix(const std::string& prefix) const {
    const Node* n = find_node(components(prefix));
    return n ? n->count : 0;
}

void Trie::collect(const Node* n, std::vector<std::string>& out, std::size_t limit) {
    if (out.size() >= limit) return;
    if (n->terminal) out.push_back(n->url);
    for (const auto& kv : n->children) {
        if (out.size() >= limit) return;
        collect(kv.second.get(), out, limit);
    }
}

std::vector<std::string> Trie::list_prefix(const std::string& prefix, std::size_t limit) const {
    std::vector<std::string> out;
    if (const Node* n = find_node(components(prefix))) collect(n, out, limit);
    return out;
}

void Trie::print(const Node* n, std::ostream& os, const std::string& indent, std::size_t depth,
                 std::size_t max_depth, std::size_t max_children) {
    if (depth >= max_depth) return;
    std::vector<const std::pair<const std::string, std::unique_ptr<Node>>*> kids;
    for (const auto& kv : n->children) kids.push_back(&kv);
    std::sort(kids.begin(), kids.end(), [](auto* a, auto* b) {
        if (a->second->count != b->second->count) return a->second->count > b->second->count;
        return a->first < b->first;
    });
    std::size_t shown = std::min(kids.size(), max_children);
    for (std::size_t i = 0; i < shown; ++i) {
        bool last = (i + 1 == shown) && kids.size() == shown;
        os << indent << (last ? "`-- " : "|-- ") << kids[i]->first << "  (" << kids[i]->second->count
           << ")\n";
        print(kids[i]->second.get(), os, indent + (last ? "    " : "|   "), depth + 1, max_depth,
              max_children);
    }
    if (kids.size() > shown)
        os << indent << "`-- ... +" << (kids.size() - shown) << " more\n";
}

void Trie::print_tree(std::ostream& os, std::size_t max_depth, std::size_t max_children) const {
    os << "(root)  (" << root_->count << ")\n";
    print(root_.get(), os, "", 0, max_depth, max_children);
}

}  // namespace wc
