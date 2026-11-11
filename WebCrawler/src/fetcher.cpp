#include "crawler/fetcher.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#include "crawler/url_utils.hpp"

#ifdef HAVE_CURL
#include <curl/curl.h>
#endif

namespace wc {
namespace {

std::uint64_t fnv1a(const std::string& s) {
    std::uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    return h;
}

double ms_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

bool parse_index(const std::string& s, std::size_t from, int& out) {
    if (s.size() <= from || s.size() - from > 6) return false;
    int v = 0;
    for (std::size_t i = from; i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (s[i] - '0');
    }
    out = v;
    return true;
}

const char* const kVocab[] = {
    "algorithm", "graph", "queue", "thread", "mutex", "crawler", "network", "search", "index",
    "hash", "heap", "trie", "cache", "server", "protocol", "latency", "packet", "kernel",
    "compiler", "database", "memory", "pointer", "recursion", "sorting", "pagerank", "balance",
    "stack", "buffer", "semaphore", "deadlock", "scheduler", "bandwidth", "routing", "browser",
    "parser", "token", "vector", "matrix", "tensor", "cluster"};
const int kVocabSize = sizeof(kVocab) / sizeof(kVocab[0]);

}  // namespace

SimulatedFetcher::SimulatedFetcher(int sites, int pages_per_site, int latency_ms, int broken_percent)
    : sites_(sites < 1 ? 1 : sites), pages_(pages_per_site < 1 ? 1 : pages_per_site),
      latency_ms_(latency_ms < 0 ? 0 : latency_ms), broken_percent_(broken_percent) {}

FetchResult SimulatedFetcher::fetch(const std::string& url) {
    auto t0 = std::chrono::steady_clock::now();
    FetchResult r;
    std::uint64_t h = fnv1a(url);
    std::mt19937_64 rng(h);
    // Network latency: 50% .. 150% of the configured value.
    int lat = latency_ms_ * static_cast<int>(50 + h % 100) / 100;
    std::this_thread::sleep_for(std::chrono::milliseconds(lat));

    ParsedUrl u = parse_url(url);
    int site = -1, page = -1;
    const std::string suffix = ".example";
    if (u.valid && u.host.size() > 4 + suffix.size() && u.host.compare(0, 4, "site") == 0 &&
        u.host.compare(u.host.size() - suffix.size(), suffix.size(), suffix) == 0) {
        parse_index(u.host.substr(0, u.host.size() - suffix.size()), 4, site);
    }
    if (site < 0 || site >= sites_) {
        r.status = 0;
        r.error = "could not resolve host (simulated)";
        r.elapsed_ms = ms_since(t0);
        return r;
    }
    if (u.path.size() < 3 || u.path.compare(0, 2, "/p") != 0 || !parse_index(u.path, 2, page) ||
        page >= pages_) {
        r.status = 404;
        r.error = "not found";
        r.body = "<html><body>404</body></html>";
        r.elapsed_ms = ms_since(t0);
        return r;
    }
    if (page != 0 && static_cast<int>(h % 100) < broken_percent_) {
        r.status = (h % 7 == 0) ? 500 : 404;
        r.error = (r.status == 500) ? "internal server error" : "not found";
        r.body = "<html><body>error</body></html>";
        r.elapsed_ms = ms_since(t0);
        return r;
    }

    int topic0 = page % 5;
    auto abs = [](int s, int p) { return "http://site" + std::to_string(s) + ".example/p" + std::to_string(p); };
    std::ostringstream o;
    o << "<!DOCTYPE html><html><head><title>S" << site << " P" << page << "</title>"
      << "<style>a{color:red}</style><script>var s='<a href=\"/ignored-in-script\">';</script></head>\n"
      << "<body><!-- <a href=\"/ignored-in-comment\">x</a> -->\n<h1>" << kVocab[(topic0 * 7) % kVocabSize] << "</h1>\n<p>";
    int topic = page % 5;
    for (int i = 0; i < 30; ++i) o << kVocab[(topic * 7 + rng() % 12) % kVocabSize] << ' ';
    o << "</p>\n<ul>\n";
    // Binary-tree children guarantee every page of a site is reachable from /p0.
    for (int c : {2 * page + 1, 2 * page + 2})
        if (c < pages_) o << "<li><a href=\"" << abs(site, c) << "\">" << kVocab[c % kVocabSize] << "</a></li>\n";
    int extra = 2 + static_cast<int>(rng() % 5);
    int first_random = -1;
    for (int i = 0; i < extra; ++i) {
        int t = static_cast<int>(rng() % pages_);
        if (first_random < 0) first_random = t;
        switch (rng() % 4) {
            case 0: o << "<li><a href='" << abs(site, t) << "'>" << kVocab[t % kVocabSize] << "</a></li>\n"; break;
            case 1: o << "<li><a href=\"/p" << t << "\">" << kVocab[t % kVocabSize] << "</a></li>\n"; break;
            case 2: o << "<li><A HREF=p" << t << ">" << kVocab[t % kVocabSize] << "</A></li>\n"; break;
            default: o << "<li><a class=\"nav\" href=\"./p" << t << "#section\">" << kVocab[t % kVocabSize] << "</a></li>\n"; break;
        }
    }
    if (rng() % 10 < 3 && sites_ > 1) {  // cross-site link
        int other = static_cast<int>(rng() % sites_);
        o << "<li><a href=\"" << abs(other, static_cast<int>(rng() % pages_)) << "\">cluster</a></li>\n";
    }
    if (first_random >= 0) o << "<li><a href=\"/p" << first_random << "\">buffer</a></li>\n";
    o << "<li><a href=\"mailto:info@site" << site << ".example\">kernel</a></li>\n"
      << "<li><a href=\"#top\">cache</a></li>\n<li><a href=\"/logo.png\">packet</a></li>\n"
      << "</ul></body></html>\n";
    r.status = 200;
    r.body = o.str();
    r.elapsed_ms = ms_since(t0);
    return r;
}

#ifdef HAVE_CURL
namespace {

struct WriteCtx {
    std::string* body;
    std::size_t max_bytes;
};

std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* ctx = static_cast<WriteCtx*>(userdata);
    std::size_t total = size * nmemb;
    if (ctx->body->size() < ctx->max_bytes) {
        std::size_t take = std::min(total, ctx->max_bytes - ctx->body->size());
        ctx->body->append(ptr, take);
    }
    return total;  // silently drop anything beyond max_bytes
}

class CurlFetcher : public Fetcher {
public:
    CurlFetcher(int timeout_s, std::size_t max_bytes) : timeout_(timeout_s), max_bytes_(max_bytes) {
        static std::once_flag once;
        std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    }

    FetchResult fetch(const std::string& url) override {
        FetchResult r;
        CURL* h = curl_easy_init();
        if (!h) { r.error = "curl_easy_init failed"; return r; }
        WriteCtx ctx{&r.body, max_bytes_};
        curl_easy_setopt(h, CURLOPT_URL, url.c_str());
        curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(h, CURLOPT_MAXREDIRS, 5L);
        curl_easy_setopt(h, CURLOPT_TIMEOUT, static_cast<long>(timeout_));
        curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(h, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(h, CURLOPT_USERAGENT, "WebCrawler-JIIT/1.0 (educational project)");
        curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(h, CURLOPT_WRITEDATA, &ctx);
        CURLcode rc = curl_easy_perform(h);
        long code = 0;
        curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &code);
        char* ct = nullptr;
        curl_easy_getinfo(h, CURLINFO_CONTENT_TYPE, &ct);
        if (ct) r.is_html = to_lower(ct).find("html") != std::string::npos;
        double secs = 0;
        curl_easy_getinfo(h, CURLINFO_TOTAL_TIME, &secs);
        r.elapsed_ms = secs * 1000.0;
        if (rc != CURLE_OK) { r.status = 0; r.error = curl_easy_strerror(rc); }
        else { r.status = static_cast<int>(code); if (!r.ok()) r.error = "HTTP " + std::to_string(code); }
        curl_easy_cleanup(h);
        return r;
    }

private:
    int timeout_;
    std::size_t max_bytes_;
};

}  // namespace

bool curl_available() { return true; }
std::shared_ptr<Fetcher> make_curl_fetcher(int timeout_s, std::size_t max_bytes) {
    return std::make_shared<CurlFetcher>(timeout_s, max_bytes);
}
#else
bool curl_available() { return false; }
std::shared_ptr<Fetcher> make_curl_fetcher(int, std::size_t) { return nullptr; }
#endif

}  // namespace wc
