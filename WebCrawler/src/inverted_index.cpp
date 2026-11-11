#include "crawler/inverted_index.hpp"

#include <algorithm>
#include <map>

#include "crawler/url_utils.hpp"

namespace wc {

void InvertedIndex::add_document(int page, const std::vector<std::string>& tokens) {
    std::map<std::string, int> tf;
    for (const auto& t : tokens) ++tf[t];
    for (const auto& kv : tf) idx_.get_or_insert(kv.first).push_back({page, kv.second});
}

std::vector<Posting> InvertedIndex::search(const std::string& term) const {
    const auto* v = idx_.find(to_lower(term));
    std::vector<Posting> out = v ? *v : std::vector<Posting>();
    std::sort(out.begin(), out.end(), [](const Posting& a, const Posting& b) {
        return a.count != b.count ? a.count > b.count : a.page < b.page;
    });
    return out;
}

std::vector<Posting> InvertedIndex::search_all(const std::vector<std::string>& terms) const {
    if (terms.empty()) return {};
    std::map<int, int> score;
    bool first = true;
    for (const auto& raw : terms) {
        const auto* v = idx_.find(to_lower(raw));
        std::map<int, int> next;
        if (v) {
            for (const Posting& p : *v) {
                if (first) next[p.page] = p.count;
                else {
                    auto it = score.find(p.page);
                    if (it != score.end()) next[p.page] = it->second + p.count;
                }
            }
        }
        score.swap(next);
        first = false;
        if (score.empty()) break;
    }
    std::vector<Posting> out;
    for (const auto& kv : score) out.push_back({kv.first, kv.second});
    std::sort(out.begin(), out.end(), [](const Posting& a, const Posting& b) {
        return a.count != b.count ? a.count > b.count : a.page < b.page;
    });
    return out;
}

std::vector<std::pair<std::string, std::size_t>> InvertedIndex::top_terms(std::size_t n) const {
    std::vector<std::pair<std::string, std::size_t>> all;
    idx_.for_each([&](const std::string& k, const std::vector<Posting>& v) {
        all.emplace_back(k, v.size());
    });
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    if (all.size() > n) all.resize(n);
    return all;
}

}  // namespace wc
