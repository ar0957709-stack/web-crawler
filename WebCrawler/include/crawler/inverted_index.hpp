// Inverted index: keyword -> postings (page id, term frequency).
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "crawler/hash_table.hpp"

namespace wc {

struct Posting {
    int page;
    int count;
};

class InvertedIndex {
public:
    void add_document(int page, const std::vector<std::string>& tokens);
    std::vector<Posting> search(const std::string& term) const;                   // by tf desc
    std::vector<Posting> search_all(const std::vector<std::string>& terms) const;  // AND query
    std::size_t term_count() const { return idx_.size(); }
    std::vector<std::pair<std::string, std::size_t>> top_terms(std::size_t n) const;

private:
    HashTable<std::string, std::vector<Posting>> idx_;
};

}  // namespace wc
