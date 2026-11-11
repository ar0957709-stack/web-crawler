#include "crawler/crawler.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <iostream>
#include <unordered_set>

#include "crawler/url_utils.hpp"

namespace wc {

Crawler::Crawler(CrawlConfig cfg, std::shared_ptr<Fetcher> fetcher)
    : cfg_(std::move(cfg)), fetcher_(std::move(fetcher)),
      buffer_(cfg_.buffer_capacity), limiter_(cfg_.delay_ms) {
    if (cfg_.threads < 1) cfg_.threads = 1;
}

// Higher = fetched sooner. Shallow pages, seed domains and short paths win.
int Crawler::compute_priority(const std::string& url, int depth) const {
    int p = 1000 - depth * 100;
    ParsedUrl u = parse_url(url);
    if (seed_domains_.contains(u.host)) p += 50;
    int segments = static_cast<int>(std::count(u.path.begin(), u.path.end(), '/'));
    p += std::max(0, 10 - segments) * 5;
    return p;
}

// Caller must hold data_mutex_. Applies all filters, then the visited set.
bool Crawler::enqueue_locked(const std::string& url, int depth) {
    if (depth > cfg_.max_depth) return false;
    if (!is_crawlable(url)) return false;
    if (cfg_.same_domain_only && !seed_domains_.contains(get_domain(url))) return false;
    if (visited_.contains(url)) { ++stats_.duplicates; return false; }
    if (accepted_ >= cfg_.max_pages) return false;
    visited_.insert(url);
    ++accepted_;
    outstanding_.fetch_add(1);  // BEFORE the push, so the count never hits 0 early
    frontier_.push(url, depth, compute_priority(url, depth));
    return true;
}

void Crawler::run() {
    auto t0 = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(data_mutex_);
        for (const auto& s : cfg_.seeds) {
            std::string n = normalize_url(s);
            if (n.empty()) { std::cerr << "Skipping invalid seed URL: " << s << "\n"; continue; }
            seed_urls_.push_back(n);
            seed_domains_.insert(get_domain(n));
        }
        for (const auto& n : seed_urls_) {
            graph_.add_node(n);
            enqueue_locked(n, 0);
        }
    }
    if (outstanding_.load() == 0) frontier_.close();

    std::vector<std::thread> workers;
    for (int i = 0; i < cfg_.threads; ++i) workers.emplace_back(&Crawler::worker_loop, this);
    std::thread indexer(&Crawler::indexer_loop, this);
    std::thread reporter;
    if (cfg_.show_progress) reporter = std::thread(&Crawler::reporter_loop, this, t0);

    for (auto& w : workers) w.join();  // frontier closed => workers drain and exit
    buffer_.close();                   // nothing more will be produced
    indexer.join();
    {
        std::lock_guard<std::mutex> lk(report_mutex_);
        finished_ = true;
    }
    report_cv_.notify_all();
    if (reporter.joinable()) reporter.join();
    elapsed_ = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// Producer: pops the most important URL, honours politeness, downloads it.
void Crawler::worker_loop() {
    UrlTask task;
    while (frontier_.pop(task)) {
        PageResult res;
        res.task = task;
        limiter_.acquire(get_domain(task.url));
        try {
            res.fetch = fetcher_->fetch(task.url);
        } catch (const std::exception& e) {
            res.fetch = FetchResult();
            res.fetch.error = e.what();
        }
        buffer_.push(std::move(res));
    }
}

// Consumer: analyses pages, grows the graph/index/trie, feeds the frontier.
void Crawler::indexer_loop() {
    PageResult res;
    while (buffer_.pop(res)) {
        process_result(res);
        if (outstanding_.fetch_sub(1) == 1) frontier_.close();  // nothing left anywhere
    }
}

void Crawler::process_result(PageResult& r) {
    const std::string& url = r.task.url;
    ParsedPage parsed;
    bool is_page = r.fetch.ok() && r.fetch.is_html;
    if (is_page) parsed = parse_html(r.fetch.body);  // CPU work done outside the lock

    std::lock_guard<std::mutex> lk(data_mutex_);
    int src = graph_.add_node(url);
    PageInfo info;
    info.status = r.fetch.status;
    info.depth = r.task.depth;
    info.fetch_ms = r.fetch.elapsed_ms;
    info.error = r.fetch.error;
    info.title = parsed.title;
    stats_.bytes += r.fetch.body.size();
    trie_.insert(url);
    ++domains_.get_or_insert(get_domain(url));

    if (!r.fetch.ok()) {
        graph_.set_broken(src, true);
        ++stats_.failed;
    } else {
        ++stats_.fetched;
    }
    if (cfg_.verbose)
        std::cout << "[" << r.fetch.status << "] d" << r.task.depth << " " << url
                  << (r.fetch.ok() ? "" : "  <-- " + r.fetch.error) << "\n";

    if (is_page) {
        std::unordered_set<std::string> seen;
        for (const std::string& raw : parsed.links) {
            std::string target = resolve_url(url, raw);
            if (target.empty() || target == url || !is_crawlable(target)) continue;  // skip images, css, pdf...
            if (!seen.insert(target).second) continue;
            ++stats_.links_found;
            int dst = graph_.add_node(target);
            graph_.add_edge(src, dst);
            ++info.out_links;
            enqueue_locked(target, r.task.depth + 1);
        }
        info.keywords = parsed.keywords.size();
        index_.add_document(src, parsed.keywords);
    }
    metadata_.insert(url, info);
}

void Crawler::reporter_loop(std::chrono::steady_clock::time_point t0) {
    auto print = [&](bool final_line) {
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::size_t done = stats_.fetched + stats_.failed;
        std::printf("\r[%6.1fs] fetched=%zu failed=%zu queued=%zu dupes=%zu | %.1f pages/s   ", secs,
                    stats_.fetched.load(), stats_.failed.load(), frontier_.size(),
                    stats_.duplicates.load(), secs > 0 ? done / secs : 0.0);
        if (final_line) std::printf("\n");
        std::fflush(stdout);
    };
    std::unique_lock<std::mutex> lk(report_mutex_);
    while (!report_cv_.wait_for(lk, std::chrono::milliseconds(500), [&] { return finished_; }))
        print(false);
    print(true);
}

}  // namespace wc
