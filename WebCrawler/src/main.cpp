// Advanced Concurrent Web Crawler and Web Analysis System - command line front-end.
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "crawler/crawler.hpp"
#include "crawler/report.hpp"
#include "crawler/url_utils.hpp"

using namespace wc;

namespace {

struct Options {
    CrawlConfig cfg;
    bool simulate = false, interactive = false, benchmark = false, help = false;
    int sim_sites = 5, sim_pages = 200, sim_latency = 50;
    int delay = -1;  // -1 = pick a default for the mode
    std::string out_dir;
    std::size_t top = 10, dot_nodes = 60;
    int timeout = 10;
};

void usage() {
    std::cout <<
R"(Advanced Concurrent Web Crawler and Web Analysis System

Usage:
  webcrawler [options] <seed-url> [<seed-url> ...]
  webcrawler --simulate [options]          crawl a built-in fake web (no network needed)
  webcrawler --benchmark [options]         compare 1,2,4,8,16 threads on the simulated web

Crawl options:
  -t, --threads N       worker threads                       (default 8)
  -d, --depth N         maximum link depth from the seeds    (default 3)
  -m, --max-pages N     stop after queueing N pages          (default 200)
      --delay MS        min. gap between requests to one domain
                        (default 200 real web, 20 simulated, 0 benchmark)
      --same-domain     only follow links on the seed domains
      --timeout S       per-request timeout in seconds       (default 10)
  -v, --verbose         log every fetched page
      --quiet           no live statistics line

Simulation options:
      --simulate        use the simulated web (default seed http://site0.example/p0)
      --sim-sites N     number of fake sites                 (default 5)
      --sim-pages N     pages per fake site                  (default 200)
      --sim-latency MS  average fake network latency         (default 50)

Output options:
  -o, --out DIR         write pages.csv, graph.dot, broken_links.txt, url_trie.txt
      --top N           rows in the PageRank table           (default 10)
      --dot-nodes N     nodes in graph.dot                   (default 60)
  -i, --interactive     query shell (keyword search, trie prefix, PageRank...) after the crawl
  -h, --help            this text
)";
}

bool parse_int(const std::string& s, long long& out) {
    try { std::size_t p; out = std::stoll(s, &p); return p == s.size(); } catch (...) { return false; }
}

bool parse_args(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next_int = [&](long long& v) {
            if (i + 1 >= argc || !parse_int(argv[i + 1], v)) {
                std::cerr << "Option " << a << " needs an integer argument\n";
                return false;
            }
            ++i;
            return true;
        };
        long long v = 0;
        if (a == "-h" || a == "--help") o.help = true;
        else if (a == "-t" || a == "--threads") { if (!next_int(v)) return false; o.cfg.threads = (int)v; }
        else if (a == "-d" || a == "--depth") { if (!next_int(v)) return false; o.cfg.max_depth = (int)v; }
        else if (a == "-m" || a == "--max-pages") { if (!next_int(v)) return false; o.cfg.max_pages = (std::size_t)v; }
        else if (a == "--delay") { if (!next_int(v)) return false; o.delay = (int)v; }
        else if (a == "--timeout") { if (!next_int(v)) return false; o.timeout = (int)v; }
        else if (a == "--same-domain") o.cfg.same_domain_only = true;
        else if (a == "-v" || a == "--verbose") o.cfg.verbose = true;
        else if (a == "--quiet") o.cfg.show_progress = false;
        else if (a == "--simulate") o.simulate = true;
        else if (a == "--sim-sites") { if (!next_int(v)) return false; o.sim_sites = (int)v; }
        else if (a == "--sim-pages") { if (!next_int(v)) return false; o.sim_pages = (int)v; }
        else if (a == "--sim-latency") { if (!next_int(v)) return false; o.sim_latency = (int)v; }
        else if (a == "-o" || a == "--out") { if (i + 1 >= argc) return false; o.out_dir = argv[++i]; }
        else if (a == "--top") { if (!next_int(v)) return false; o.top = (std::size_t)v; }
        else if (a == "--dot-nodes") { if (!next_int(v)) return false; o.dot_nodes = (std::size_t)v; }
        else if (a == "-i" || a == "--interactive") o.interactive = true;
        else if (a == "--benchmark") o.benchmark = true;
        else if (!a.empty() && a[0] == '-') { std::cerr << "Unknown option: " << a << "\n"; return false; }
        else o.cfg.seeds.push_back(a);
    }
    return true;
}


// ---- Interactive start-up questions (used when no URL is given on the command line) ----
std::string ask(const std::string& question, const std::string& def) {
    std::cout << question << " [" << def << "]: " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return def;  // EOF -> use default
    line = trim(line);
    return line.empty() ? def : line;
}

int ask_int(const std::string& question, int def, int min_value) {
    for (;;) {
        std::string a = ask(question, std::to_string(def));
        long long v;
        if (parse_int(a, v) && v >= min_value && v <= 100000) return static_cast<int>(v);
        std::cout << "  Please enter a whole number >= " << min_value << ".\n";
    }
}

bool ask_yes_no(const std::string& question, bool def) {
    std::string a = to_lower(ask(question + " (y/n)", def ? "y" : "n"));
    return !a.empty() && a[0] == 'y';
}

// Fills `o` from the user's answers.
void run_wizard(Options& o) {
    std::cout << "\n=== Web Crawler ===\n"
                 "Press Enter to accept the value in [brackets]. Type 'sim' for the built-in fake web.\n\n";
    for (;;) {
        std::string in = ask("Start URL", "sim");
        if (to_lower(in) == "sim") { o.simulate = true; break; }
        if (in.find("://") == std::string::npos) in = "https://" + in;
        std::string n = normalize_url(in);
        if (n.empty()) { std::cout << "  That doesn't look like a valid http(s) URL, try again.\n"; continue; }
        o.cfg.seeds = {n};
        break;
    }
    o.cfg.max_depth = ask_int("Crawl depth (link hops from the start)", o.cfg.max_depth, 0);
    o.cfg.max_pages = static_cast<std::size_t>(ask_int("Maximum pages", static_cast<int>(o.cfg.max_pages), 1));
    o.cfg.threads = ask_int("Worker threads", o.cfg.threads, 1);
    if (!o.simulate) o.cfg.same_domain_only = ask_yes_no("Stay on this website only?", true);
    o.interactive = ask_yes_no("Open the search shell after the crawl?", true);
    std::string out = ask("Save result files to folder (blank = don't save)", "");
    if (out == "n" || out == "N") out.clear();
    o.out_dir = out;
    std::cout << "\n";
}

int run_benchmark(Options o) {
    o.cfg.show_progress = false;
    o.cfg.verbose = false;
    o.cfg.seeds = {"http://site0.example/p0"};
    std::cout << "Benchmark on the simulated web (" << o.sim_sites << " sites x " << o.sim_pages
              << " pages, ~" << o.sim_latency << " ms latency, domain delay " << o.cfg.delay_ms
              << " ms, " << o.cfg.max_pages << " pages max)\n\n";
    std::cout << std::left << std::setw(10) << "threads" << std::setw(12) << "time (s)"
              << std::setw(10) << "pages" << std::setw(12) << "pages/s" << "speed-up\n";
    double base = 0;
    for (int t : {1, 2, 4, 8, 16}) {
        o.cfg.threads = t;
        auto fetcher = std::make_shared<SimulatedFetcher>(o.sim_sites, o.sim_pages, o.sim_latency, 5);
        Crawler c(o.cfg, fetcher);
        c.run();
        std::size_t pages = c.stats().fetched + c.stats().failed;
        double secs = c.elapsed_seconds();
        if (t == 1) base = secs;
        std::cout << std::left << std::setw(10) << t << std::setw(12) << std::fixed << std::setprecision(2)
                  << secs << std::setw(10) << pages << std::setw(12) << std::setprecision(1)
                  << (secs > 0 ? pages / secs : 0.0) << std::setprecision(2) << (secs > 0 ? base / secs : 0.0)
                  << "x\n" << std::flush;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse_args(argc, argv, o)) { std::cerr << "Try --help\n"; return 2; }
    if (o.help) { usage(); return 0; }
    if (o.cfg.threads < 1 || o.cfg.max_depth < 0 || o.cfg.max_pages < 1) {
        std::cerr << "threads must be >= 1, depth >= 0 and max-pages >= 1\n";
        return 2;
    }
    if (o.benchmark) o.simulate = true;
    // No URL and no mode flag -> ask the user what to crawl.
    if (o.cfg.seeds.empty() && !o.simulate && !o.benchmark) run_wizard(o);
    if (o.cfg.seeds.empty()) o.simulate = true;
    if (o.delay < 0) o.delay = o.benchmark ? 0 : (o.simulate ? 20 : 200);
    o.cfg.delay_ms = o.delay;
    if (o.benchmark) return run_benchmark(o);

    std::shared_ptr<Fetcher> fetcher;
    if (o.simulate) {
        if (o.cfg.seeds.empty()) o.cfg.seeds = {"http://site0.example/p0"};
        fetcher = std::make_shared<SimulatedFetcher>(o.sim_sites, o.sim_pages, o.sim_latency, 5);
        std::cout << "[simulated web: " << o.sim_sites << " sites x " << o.sim_pages << " pages]\n";
    } else {
        fetcher = make_curl_fetcher(o.timeout);
        if (!fetcher) {
            std::cerr << "This build has no libcurl support, so it can only crawl the simulated web.\n"
                         "Install libcurl development files and rebuild (see README), or pass --simulate.\n";
            return 1;
        }
    }
    if (o.cfg.verbose) o.cfg.show_progress = false;

    Crawler crawler(o.cfg, fetcher);
    crawler.run();
    print_summary(crawler, o.top, std::cout);
    if (!o.out_dir.empty()) export_results(crawler, o.out_dir, o.dot_nodes, std::cout);
    if (o.interactive) run_repl(crawler, std::cin, std::cout);
    return 0;
}
