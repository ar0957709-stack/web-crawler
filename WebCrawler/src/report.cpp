#include "crawler/report.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

#include "crawler/url_utils.hpp"

namespace wc {
namespace {

std::string csv_escape(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += '"'; out += c; }
    return out + "\"";
}

std::vector<int> ranked_order(const std::vector<double>& ranks) {
    std::vector<int> ids(ranks.size());
    std::iota(ids.begin(), ids.end(), 0);
    std::sort(ids.begin(), ids.end(), [&](int a, int b) { return ranks[a] > ranks[b]; });
    return ids;
}

std::string shorten(const std::string& s, std::size_t n) {
    return s.size() <= n ? s : s.substr(0, n - 3) + "...";
}

void print_rank_table(const Crawler& c, const std::vector<double>& ranks, std::size_t n, std::ostream& os) {
    const WebGraph& g = c.graph();
    std::vector<int> order = ranked_order(ranks);
    os << std::left << std::setw(5) << "#" << std::setw(11) << "PageRank" << std::setw(8) << "in-deg"
       << "URL\n";
    for (std::size_t i = 0; i < std::min(n, order.size()); ++i) {
        int id = order[i];
        os << std::left << std::setw(5) << (i + 1) << std::setw(11) << std::fixed << std::setprecision(5)
           << ranks[id] << std::setw(8) << g.in_degree(id) << shorten(g.url_of(id), 70) << "\n";
    }
}

}  // namespace

void print_summary(const Crawler& c, std::size_t top_n, std::ostream& os) {
    const WebGraph& g = c.graph();
    const CrawlStats& s = c.stats();
    std::size_t pages = s.fetched + s.failed;
    double secs = c.elapsed_seconds();

    os << "\n==================== CRAWL SUMMARY ====================\n";
    os << "Seeds              : ";
    for (const auto& u : c.seeds()) os << u << " ";
    os << "\nThreads            : " << c.config().threads
       << "   max depth: " << c.config().max_depth << "   max pages: " << c.config().max_pages
       << "   domain delay: " << c.config().delay_ms << " ms\n";
    os << "Pages crawled      : " << pages << "  (ok: " << s.fetched << ", failed: " << s.failed << ")\n";
    os << "Unique links found : " << s.links_found << "   duplicates rejected: " << s.duplicates << "\n";
    os << "Graph              : " << g.node_count() << " vertices, " << g.edge_count() << " edges\n";
    os << "Unique terms       : " << c.index().term_count() << "   trie URLs: " << c.trie().size() << "\n";
    os << "Downloaded         : " << std::fixed << std::setprecision(1) << s.bytes / 1024.0 << " KiB\n";
    os << "Elapsed            : " << std::setprecision(2) << secs << " s  ("
       << std::setprecision(1) << (secs > 0 ? pages / secs : 0.0) << " pages/s)\n";

    // Domains
    std::vector<std::pair<std::string, std::size_t>> doms;
    c.domain_counts().for_each([&](const std::string& d, const std::size_t& n) { doms.emplace_back(d, n); });
    std::sort(doms.begin(), doms.end(), [](auto& a, auto& b) { return a.second != b.second ? a.second > b.second : a.first < b.first; });
    os << "\n--- Pages per domain (top 10 of " << doms.size() << ") ---\n";
    for (std::size_t i = 0; i < std::min<std::size_t>(10, doms.size()); ++i)
        os << "  " << std::left << std::setw(32) << doms[i].first << doms[i].second << "\n";

    // PageRank
    std::vector<double> ranks = g.pagerank();
    os << "\n--- Top " << top_n << " pages by PageRank ---\n";
    print_rank_table(c, ranks, top_n, os);

    // Broken links with the first page that references them
    std::vector<int> broken = g.broken_nodes();
    std::vector<int> referrer(g.node_count(), -1);
    for (std::size_t u = 0; u < g.node_count(); ++u)
        for (int v : g.neighbors(static_cast<int>(u)))
            if (g.is_broken(v) && referrer[v] < 0) referrer[v] = static_cast<int>(u);
    os << "\n--- Broken links (" << broken.size() << ") ---\n";
    for (std::size_t i = 0; i < std::min<std::size_t>(15, broken.size()); ++i) {
        int id = broken[i];
        const PageInfo* pi = c.page_info(g.url_of(id));
        os << "  [" << (pi ? pi->status : 0) << "] " << shorten(g.url_of(id), 60);
        if (referrer[id] >= 0) os << "   <- " << shorten(g.url_of(referrer[id]), 45);
        os << "\n";
    }
    if (broken.size() > 15) os << "  ... and " << broken.size() - 15 << " more\n";

    // Keywords
    os << "\n--- Most common keywords (by number of pages) ---\n  ";
    for (const auto& kv : c.index().top_terms(12)) os << kv.first << "(" << kv.second << ") ";
    os << "\n";

    // Trie
    os << "\n--- URL trie (domains / directories, page counts) ---\n";
    c.trie().print_tree(os, 3, 5);

    // BFS levels
    if (!c.seeds().empty()) {
        int start = g.id_of(c.seeds().front());
        int depth = std::min(c.config().max_depth, 3);
        auto order = g.bfs(start, depth);
        std::vector<std::size_t> levels(depth + 1, 0);
        for (auto& p : order) ++levels[p.second];
        os << "\n--- BFS from first seed (pages reachable per depth) ---\n  ";
        for (int d = 0; d <= depth; ++d) os << "depth " << d << ": " << levels[d] << "   ";
        os << "\n";
    }
    os << "=======================================================\n";
}

bool export_results(const Crawler& c, const std::string& dir, std::size_t dot_nodes, std::ostream& log) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) { log << "Cannot create output directory '" << dir << "': " << ec.message() << "\n"; return false; }
    const WebGraph& g = c.graph();
    std::vector<double> ranks = g.pagerank();

    {
        std::ofstream f(fs::path(dir) / "pages.csv");
        f << "url,status,depth,fetch_ms,out_links,in_degree,pagerank,title\n";
        for (std::size_t i = 0; i < g.node_count(); ++i) {
            const PageInfo* pi = c.page_info(g.url_of(static_cast<int>(i)));
            f << csv_escape(g.url_of(static_cast<int>(i))) << ",";
            if (pi) f << pi->status << "," << pi->depth << "," << std::fixed << std::setprecision(1) << pi->fetch_ms << "," << pi->out_links;
            else f << ",,,";
            f << "," << g.in_degree(static_cast<int>(i)) << "," << std::setprecision(8) << ranks[i] << ","
              << csv_escape(pi ? pi->title : "") << "\n";
        }
    }
    {
        std::ofstream f(fs::path(dir) / "broken_links.txt");
        for (int id : g.broken_nodes()) {
            const PageInfo* pi = c.page_info(g.url_of(id));
            f << (pi ? pi->status : 0) << "\t" << g.url_of(id) << "\t" << (pi ? pi->error : "") << "\n";
        }
    }
    {
        std::ofstream f(fs::path(dir) / "graph.dot");
        g.export_dot(f, ranks, dot_nodes);
    }
    {
        std::ofstream f(fs::path(dir) / "url_trie.txt");
        c.trie().print_tree(f, 6, 50);
    }
    log << "Exported pages.csv, broken_links.txt, graph.dot, url_trie.txt to '" << dir << "'\n"
        << "Render the graph with:  dot -Tsvg " << (fs::path(dir) / "graph.dot").string() << " -o graph.svg\n";
    return true;
}

void run_repl(const Crawler& c, std::istream& in, std::ostream& os) {
    const WebGraph& g = c.graph();
    os << "\nInteractive mode. Commands: search <words...> | prefix <url-or-domain> | rank [n] | "
          "broken | info <url> | bfs <url> <depth> | help | quit\n";
    std::string line;
    while (os << "crawler> " << std::flush, std::getline(in, line)) {
        std::istringstream ss(line);
        std::string cmd;
        ss >> cmd;
        if (cmd.empty()) continue;
        if (cmd == "quit" || cmd == "exit" || cmd == "q") break;
        if (cmd == "help") {
            os << "  search <words...>   pages containing ALL words (inverted index)\n"
                  "  prefix <url>        pages under a domain/directory (trie), e.g. prefix site1.example\n"
                  "  rank [n]            top n pages by PageRank\n"
                  "  broken              list broken links\n"
                  "  info <url>          details about one page\n"
                  "  bfs <url> <depth>   pages reachable within <depth> clicks\n";
        } else if (cmd == "search") {
            std::vector<std::string> words;
            for (std::string w; ss >> w;) words.push_back(w);
            auto hits = c.index().search_all(words);
            os << hits.size() << " page(s)\n";
            for (std::size_t i = 0; i < std::min<std::size_t>(15, hits.size()); ++i)
                os << "  " << std::setw(4) << hits[i].count << "  " << g.url_of(hits[i].page) << "\n";
        } else if (cmd == "prefix") {
            std::string p;
            ss >> p;
            os << c.trie().count_prefix(p) << " page(s) under '" << p << "'\n";
            for (const auto& u : c.trie().list_prefix(p, 15)) os << "  " << u << "\n";
        } else if (cmd == "rank") {
            std::size_t n = 10;
            ss >> n;
            print_rank_table(c, g.pagerank(), n, os);
        } else if (cmd == "broken") {
            for (int id : g.broken_nodes()) os << "  " << g.url_of(id) << "\n";
            if (g.broken_nodes().empty()) os << "  none\n";
        } else if (cmd == "info") {
            std::string u;
            ss >> u;
            u = normalize_url(u);
            const PageInfo* pi = c.page_info(u);
            int id = g.id_of(u);
            if (id < 0) { os << "  unknown URL\n"; continue; }
            os << "  in-degree " << g.in_degree(id) << ", out-degree " << g.neighbors(id).size() << "\n";
            if (pi) os << "  status " << pi->status << ", depth " << pi->depth << ", title \"" << pi->title
                       << "\", keywords " << pi->keywords << "\n";
            else os << "  discovered but not crawled\n";
        } else if (cmd == "bfs") {
            std::string u;
            int d = 1;
            ss >> u >> d;
            int id = g.id_of(normalize_url(u));
            auto order = g.bfs(id, d);
            os << "  " << order.size() << " page(s) within " << d << " click(s)\n";
            for (std::size_t i = 0; i < std::min<std::size_t>(15, order.size()); ++i)
                os << "  d" << order[i].second << " " << g.url_of(order[i].first) << "\n";
        } else {
            os << "  unknown command, type 'help'\n";
        }
    }
}

}  // namespace wc
