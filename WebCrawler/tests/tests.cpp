// Dependency-free unit tests. Run with `make test` or `ctest`.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <thread>

#include "crawler/bounded_buffer.hpp"
#include "crawler/crawler.hpp"
#include "crawler/frontier.hpp"
#include "crawler/hash_table.hpp"
#include "crawler/html_parser.hpp"
#include "crawler/inverted_index.hpp"
#include "crawler/priority_queue.hpp"
#include "crawler/rate_limiter.hpp"
#include "crawler/trie.hpp"
#include "crawler/url_utils.hpp"

using namespace wc;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++g_checks;                                                                   \
        if (!(cond)) { ++g_failed; std::cerr << "  FAILED " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; } \
    } while (0)
#define RUN(fn) do { std::cout << "[ RUN  ] " #fn "\n"; fn(); } while (0)

void test_hash_table() {
    HashTable<std::string, int> t;
    for (int i = 0; i < 10000; ++i) CHECK(t.insert("k" + std::to_string(i), i));
    CHECK(t.size() == 10000);
    CHECK(!t.insert("k5", 55));  // overwrite
    CHECK(*t.find("k5") == 55);
    CHECK(t.find("missing") == nullptr);
    CHECK(t.erase("k7") && !t.contains("k7") && !t.erase("k7"));
    CHECK(t.size() == 9999);
    t.get_or_insert("counter")++;
    t.get_or_insert("counter")++;
    CHECK(*t.find("counter") == 2);
    std::size_t visited = 0;
    t.for_each([&](const std::string&, int) { ++visited; });
    CHECK(visited == t.size());
    HashSet<std::string> s;
    CHECK(s.insert("a") && !s.insert("a") && s.contains("a") && s.size() == 1);
}

void test_heap() {
    MaxHeap<int> h;
    std::vector<int> in = {5, 1, 9, 3, 7, 9, 2, 8, 0, 6};
    for (int x : in) h.push(x);
    std::vector<int> out;
    while (!h.empty()) { out.push_back(h.top()); h.pop(); }
    std::sort(in.rbegin(), in.rend());
    CHECK(out == in);

    Frontier f;  // priority first, FIFO among equals
    f.push("low", 0, 1);
    f.push("hi-a", 0, 10);
    f.push("hi-b", 0, 10);
    UrlTask t;
    CHECK(f.pop(t) && t.url == "hi-a");
    CHECK(f.pop(t) && t.url == "hi-b");
    CHECK(f.pop(t) && t.url == "low");
    f.close();
    CHECK(!f.pop(t));
}

void test_bounded_buffer() {
    BoundedBuffer<int> buf(4);
    std::atomic<long long> sum{0};
    std::vector<std::thread> consumers, producers;
    for (int c = 0; c < 3; ++c)
        consumers.emplace_back([&] { int v; while (buf.pop(v)) sum += v; });
    for (int p = 0; p < 4; ++p)
        producers.emplace_back([&, p] { for (int i = 1; i <= 1000; ++i) buf.push(i + p * 0); });
    for (auto& t : producers) t.join();
    buf.close();
    for (auto& t : consumers) t.join();
    CHECK(sum == 4LL * (1000 * 1001 / 2));
}

void test_rate_limiter() {
    RateLimiter rl(40);
    auto t0 = std::chrono::steady_clock::now();
    std::vector<std::thread> ts;
    for (int i = 0; i < 4; ++i) ts.emplace_back([&] { rl.acquire("a.com"); });
    for (auto& t : ts) t.join();
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms >= 100.0);  // 4 requests => 3 gaps of 40 ms (minus scheduling slack)
    t0 = std::chrono::steady_clock::now();
    rl.acquire("b.com");  // other domain is not delayed
    ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    CHECK(ms < 30.0);
}

void test_urls() {
    CHECK(normalize_url("HTTP://Example.COM:80/a/./b/../c#frag") == "http://example.com/a/c");
    CHECK(normalize_url("https://example.com") == "https://example.com/");
    CHECK(normalize_url("https://example.com:443/x?q=1") == "https://example.com/x?q=1");
    CHECK(normalize_url("ftp://example.com/").empty());
    CHECK(normalize_url("not a url").empty());
    const std::string base = "http://a.com/dir/page.html";
    CHECK(resolve_url(base, "other.html") == "http://a.com/dir/other.html");
    CHECK(resolve_url(base, "/root.html") == "http://a.com/root.html");
    CHECK(resolve_url(base, "../up.html") == "http://a.com/up.html");
    CHECK(resolve_url(base, "//b.com/x") == "http://b.com/x");
    CHECK(resolve_url(base, "?page=2") == "http://a.com/dir/page.html?page=2");
    CHECK(resolve_url(base, "https://c.com/z#top") == "https://c.com/z");
    CHECK(resolve_url(base, "/r?u=http://x.com").find("a.com/r?u=") != std::string::npos);
    CHECK(resolve_url(base, "mailto:x@y.z").empty());
    CHECK(resolve_url(base, "javascript:void(0)").empty());
    CHECK(resolve_url(base, "#section").empty());
    CHECK(get_domain("http://WWW.Test.org/x") == "www.test.org");
    CHECK(is_crawlable("http://a.com/page") && is_crawlable("http://a.com/x.html"));
    CHECK(!is_crawlable("http://a.com/img.PNG") && !is_crawlable("http://a.com/a/b.pdf"));
}

void test_html_parser() {
    const char* html =
        "<html><head><title> My Title </title><style>a{}</style>"
        "<script>var x = '<a href=\"/no\">';</script></head><body>"
        "<!-- <a href=\"/commented\"> --><p>Hello crawler world, hello again</p>"
        "<a href=\"/one\">1</a> <A HREF='two.html'>2</A> <a class=x href=three>3</a>"
        "<a name=anchor>none</a><a href=\"/q?a=1&amp;b=2\">4</a></body></html>";
    ParsedPage p = parse_html(html);
    CHECK(p.title == "My Title");
    CHECK(p.links.size() == 4);
    CHECK(p.links[0] == "/one" && p.links[1] == "two.html" && p.links[2] == "three");
    CHECK(p.links[3] == "/q?a=1&b=2");
    CHECK(std::count(p.keywords.begin(), p.keywords.end(), "hello") == 2);
    CHECK(std::find(p.keywords.begin(), p.keywords.end(), "commented") == p.keywords.end());
    CHECK(std::find(p.keywords.begin(), p.keywords.end(), "var") == p.keywords.end());
}

void test_trie() {
    Trie t;
    CHECK(t.insert("http://www.example.com/a/b"));
    CHECK(t.insert("http://www.example.com/a/c"));
    CHECK(t.insert("http://blog.example.com/x"));
    CHECK(t.insert("http://other.org/"));
    CHECK(!t.insert("http://www.example.com/a/b"));
    CHECK(t.size() == 4);
    CHECK(t.contains("http://www.example.com/a/c") && !t.contains("http://www.example.com/zzz"));
    CHECK(t.count_prefix("example.com") == 3);
    CHECK(t.count_prefix("http://www.example.com/a") == 2);
    CHECK(t.count_prefix("nothing.net") == 0);
    CHECK(t.list_prefix("example.com", 10).size() == 3);
    CHECK(t.list_prefix("example.com", 2).size() == 2);
    std::ostringstream os;
    t.print_tree(os);
    CHECK(os.str().find("example") != std::string::npos);
}

void test_graph() {
    WebGraph g;
    int a = g.add_node("a"), b = g.add_node("b"), c = g.add_node("c");
    CHECK(g.add_node("a") == a && g.node_count() == 3);
    g.add_edge(a, b); g.add_edge(b, c); g.add_edge(c, a);
    auto r = g.pagerank();
    CHECK(std::fabs(r[0] - 1.0 / 3) < 1e-6 && std::fabs(r[1] - 1.0 / 3) < 1e-6);

    WebGraph h;  // star: everybody links to "hub", hub is a dangling node
    int hub = h.add_node("hub");
    for (int i = 0; i < 5; ++i) h.add_edge(h.add_node("n" + std::to_string(i)), hub);
    auto hr = h.pagerank();
    double sum = 0;
    for (double x : hr) sum += x;
    CHECK(std::fabs(sum - 1.0) < 1e-6);
    CHECK(hr[hub] > hr[1]);

    WebGraph t;  // 0->1->2->3, 0->2
    for (int i = 0; i < 4; ++i) t.add_node(std::to_string(i));
    t.add_edge(0, 1); t.add_edge(1, 2); t.add_edge(2, 3); t.add_edge(0, 2);
    CHECK(t.bfs(0, 1).size() == 3);
    CHECK(t.bfs(0, 5).size() == 4);
    CHECK(t.dfs(0, 5).size() == 4 && t.dfs(0, 5).front().first == 0);
    CHECK(t.bfs(0, 5).back().second == 2);  // node 3 is two hops away
    t.set_broken(3, true);
    CHECK(t.broken_nodes() == std::vector<int>{3});
}

void test_inverted_index() {
    InvertedIndex idx;
    idx.add_document(0, {"graph", "graph", "queue"});
    idx.add_document(1, {"graph", "heap"});
    idx.add_document(2, {"heap", "queue", "queue"});
    auto g = idx.search("Graph");
    CHECK(g.size() == 2 && g[0].page == 0 && g[0].count == 2);
    CHECK(idx.search_all({"graph", "queue"}).size() == 1);
    CHECK(idx.search_all({"queue", "heap"}).size() == 1);
    CHECK(idx.search_all({"nothing"}).empty());
    CHECK(idx.top_terms(1).size() == 1);
}

static std::size_t crawl_and_count(int threads, std::size_t* edges, std::size_t* nodes, std::size_t* dups) {
    CrawlConfig cfg;
    cfg.seeds = {"http://site0.example/p0"};
    cfg.threads = threads;
    cfg.max_depth = 100;
    cfg.max_pages = 100000;  // exhaustive: the result set must not depend on thread timing
    cfg.delay_ms = 0;
    cfg.show_progress = false;
    Crawler c(cfg, std::make_shared<SimulatedFetcher>(2, 60, 1, 5));
    c.run();
    *edges = c.graph().edge_count();
    *nodes = c.graph().node_count();
    *dups = c.stats().duplicates;
    return c.stats().fetched + c.stats().failed;
}

void test_concurrent_crawl_is_consistent() {
    std::size_t e1, n1, d1, e8, n8, d8;
    std::size_t p1 = crawl_and_count(1, &e1, &n1, &d1);
    std::size_t p8 = crawl_and_count(8, &e8, &n8, &d8);
    CHECK(p1 == 120);                 // 2 sites x 60 pages, all reachable
    CHECK(p1 == p8 && e1 == e8 && n1 == n8 && d1 == d8);   // no lost or duplicated work
}

void test_limits() {
    CrawlConfig cfg;
    cfg.seeds = {"http://site0.example/p0"};
    cfg.threads = 4; cfg.max_depth = 100; cfg.max_pages = 25; cfg.delay_ms = 0; cfg.show_progress = false;
    Crawler c(cfg, std::make_shared<SimulatedFetcher>(3, 100, 1, 0));
    c.run();
    CHECK(c.stats().fetched + c.stats().failed == 25);

    cfg.max_pages = 1000; cfg.max_depth = 1;
    Crawler d(cfg, std::make_shared<SimulatedFetcher>(3, 100, 1, 0));
    d.run();
    for (std::size_t i = 0; i < d.graph().node_count(); ++i) {
        const PageInfo* pi = d.page_info(d.graph().url_of((int)i));
        if (pi) CHECK(pi->depth <= 1);
    }

    cfg.same_domain_only = true; cfg.max_depth = 100;
    Crawler s(cfg, std::make_shared<SimulatedFetcher>(3, 20, 1, 0));
    s.run();
    CHECK(s.domain_counts().size() == 1);

    CrawlConfig bad; bad.seeds = {"garbage"}; bad.show_progress = false;
    Crawler b(bad, std::make_shared<SimulatedFetcher>());
    b.run();  // must terminate immediately without hanging
    CHECK(b.stats().fetched == 0);
}

int main() {
    RUN(test_hash_table);
    RUN(test_heap);
    RUN(test_bounded_buffer);
    RUN(test_rate_limiter);
    RUN(test_urls);
    RUN(test_html_parser);
    RUN(test_trie);
    RUN(test_graph);
    RUN(test_inverted_index);
    RUN(test_concurrent_crawl_is_consistent);
    RUN(test_limits);
    std::cout << "\n" << (g_checks - g_failed) << "/" << g_checks << " checks passed\n";
    return g_failed ? 1 : 0;
}
