// Page fetching back-ends: a libcurl fetcher (real web) and a deterministic
// simulated web (offline demos, tests and benchmarks).
#pragma once
#include <memory>
#include <string>

namespace wc {

struct FetchResult {
    int status = 0;  // HTTP status, 0 = network/DNS failure
    std::string body;
    std::string error;
    bool is_html = true;
    double elapsed_ms = 0.0;
    bool ok() const { return status >= 200 && status < 300; }
};

class Fetcher {
public:
    virtual ~Fetcher() = default;
    // Must be thread-safe.
    virtual FetchResult fetch(const std::string& url) = 0;
};

// Hosts are site0.example ... site{N-1}.example, pages are /p0 ... /p{M-1}.
class SimulatedFetcher : public Fetcher {
public:
    SimulatedFetcher(int sites = 5, int pages_per_site = 200, int latency_ms = 50,
                     int broken_percent = 5);
    FetchResult fetch(const std::string& url) override;

private:
    int sites_, pages_, latency_ms_, broken_percent_;
};

bool curl_available();  // true if built with libcurl
std::shared_ptr<Fetcher> make_curl_fetcher(int timeout_s = 10,
                                           std::size_t max_bytes = 2 * 1024 * 1024);

}  // namespace wc
