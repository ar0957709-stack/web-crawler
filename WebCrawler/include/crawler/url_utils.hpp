// URL parsing, normalization and relative-link resolution.
#pragma once
#include <string>

namespace wc {

struct ParsedUrl {
    std::string scheme, host, port, path, query;
    bool valid = false;
};

std::string to_lower(std::string s);
std::string trim(const std::string& s);

ParsedUrl parse_url(const std::string& url);
std::string to_string(const ParsedUrl& u);

// Canonical form: lower-case scheme/host, default port and #fragment removed,
// "." / ".." segments resolved. Returns "" if the URL is not a valid http(s) URL.
std::string normalize_url(const std::string& url);

// Resolves `href` found on page `base` into a normalized absolute URL.
// Returns "" for mailto:, javascript:, fragments-only, malformed links, ...
std::string resolve_url(const std::string& base, const std::string& href);

std::string get_domain(const std::string& url);

// False for obvious non-HTML resources (images, archives, css, js, ...).
bool is_crawlable(const std::string& url);

}  // namespace wc
