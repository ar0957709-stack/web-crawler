// The concurrent crawler engine.
//
//   seeds -> [Frontier: max-heap] -> N fetch workers -> [BoundedBuffer]
//                ^                                          |
//                |                                          v
//                +---- new links <--------------- 1 indexer/analysis thread
//                                    (visited set, graph, trie, inverted index)
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "crawler/bounded_buffer.hpp"
#include "crawler/fetcher.hpp"
#include "crawler/frontier.hpp"
#include "crawler/graph.hpp"
#include "crawler/hash_table.hpp"
#include "crawler/html_parser.hpp"
#include "crawler/inverted_index.hpp"
#include "crawler/rate_limiter.hpp"
#include "crawler/trie.hpp"

namespace wc {

struct CrawlConfig {
    std::vector<std::string> seeds;
    int threads = 8;
    int max_depth = 3;
    std::size_t max_pages = 200;
    int delay_ms = 200;            // minimum gap between requests to one domain
    bool same_domain_only = false; // stay on the seed domains
    bool verbose = false;          // log every page
    bool show_progress = true;     // live statistics line
    std::size_t buffer_capacity = 256;
};

struct PageInfo {
    int status = 0;
    int depth = 0;
    double fetch_ms = 0;
    std::string title;
    std::string error;
    std::size_t keywords = 0;
    std::size_t out_links = 0;
};

struct CrawlStats {
    std::atomic<std::size_t> fetched{0};     // successful fetches
    std::atomic<std::size_t> failed{0};      // 4xx/5xx/network errors
    std::atomic<std::size_t> links_found{0};
    std::atomic<std::size_t> duplicates{0};  // links rejected by the visited set
    std::atomic<std::size_t> bytes{0};
};

class Crawler {
public:
    Crawler(CrawlConfig cfg, std::shared_ptr<Fetcher> fetcher);

    void run();  // blocking; call once

    const CrawlConfig& config() const { return cfg_; }
    const CrawlStats& stats() const { return stats_; }
    const WebGraph& graph() const { return graph_; }
    const Trie& trie() const { return trie_; }
    const InvertedIndex& index() const { return index_; }
    const HashTable<std::string, std::size_t>& domain_counts() const { return domains_; }
    const std::vector<std::string>& seeds() const { return seed_urls_; }
    const PageInfo* page_info(const std::string& url) const { return metadata_.find(url); }
    double elapsed_seconds() const { return elapsed_; }

private:
    struct PageResult {
        UrlTask task;
        FetchResult fetch;
    };

    void worker_loop();
    void indexer_loop();
    void reporter_loop(std::chrono::steady_clock::time_point t0);
    void process_result(PageResult& r);
    bool enqueue_locked(const std::string& url, int depth);
    int compute_priority(const std::string& url, int depth) const;

    CrawlConfig cfg_;
    std::shared_ptr<Fetcher> fetcher_;
    Frontier frontier_;
    BoundedBuffer<PageResult> buffer_;
    RateLimiter limiter_;

    // Shared analysis state, guarded by data_mutex_.
    mutable std::mutex data_mutex_;
    HashSet<std::string> visited_;
    HashSet<std::string> seed_domains_;
    HashTable<std::string, PageInfo> metadata_;
    HashTable<std::string, std::size_t> domains_;
    WebGraph graph_;
    Trie trie_;
    InvertedIndex index_;
    std::vector<std::string> seed_urls_;
    std::size_t accepted_ = 0;

    // Termination detection: URLs queued, in flight, or waiting to be indexed.
    std::atomic<long long> outstanding_{0};
    CrawlStats stats_;
    double elapsed_ = 0;

    std::mutex report_mutex_;
    std::condition_variable report_cv_;
    bool finished_ = false;
};

}  // namespace wc
