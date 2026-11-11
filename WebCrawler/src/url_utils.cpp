#include "crawler/url_utils.hpp"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <unordered_set>
#include <vector>

namespace wc {

std::string to_lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

namespace {

bool all_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(),
                                     [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Resolves "." and ".." segments and duplicate slashes.
std::string remove_dot_segments(const std::string& path) {
    std::vector<std::string> out;
    bool trailing = false;
    std::size_t i = 0;
    while (i <= path.size()) {
        std::size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        std::string seg = path.substr(i, j - i);
        bool last = (j == path.size());
        if (seg == "..") {
            if (!out.empty()) out.pop_back();
            if (last) trailing = true;
        } else if (seg == "." || seg.empty()) {
            if (last) trailing = true;
        } else {
            out.push_back(seg);
            if (last) trailing = false;
        }
        i = j + 1;
    }
    std::string r = "/";
    for (std::size_t k = 0; k < out.size(); ++k) {
        if (k) r += '/';
        r += out[k];
    }
    if (trailing && !out.empty()) r += '/';
    return r;
}

}  // namespace

ParsedUrl parse_url(const std::string& input) {
    ParsedUrl u;
    std::string s = trim(input);
    std::size_t p = s.find("://");
    if (p == std::string::npos || p == 0) return u;
    u.scheme = to_lower(s.substr(0, p));
    if (u.scheme != "http" && u.scheme != "https") return u;
    s = s.substr(p + 3);

    std::size_t h = s.find('#');
    if (h != std::string::npos) s.resize(h);

    std::size_t end = s.find_first_of("/?");
    std::string auth = s.substr(0, end);
    std::string rest = (end == std::string::npos) ? "" : s.substr(end);

    std::size_t at = auth.rfind('@');
    if (at != std::string::npos) auth = auth.substr(at + 1);
    std::size_t colon = auth.rfind(':');
    if (colon != std::string::npos) {
        u.port = auth.substr(colon + 1);
        auth.resize(colon);
        if (!all_digits(u.port)) return ParsedUrl{};
    }
    u.host = to_lower(auth);
    if (u.host.empty()) return ParsedUrl{};
    if ((u.scheme == "http" && u.port == "80") || (u.scheme == "https" && u.port == "443"))
        u.port.clear();

    std::string path;
    if (rest.empty()) {
        path = "/";
    } else if (rest[0] == '?') {
        path = "/";
        u.query = rest.substr(1);
    } else {
        std::size_t q = rest.find('?');
        path = rest.substr(0, q);
        if (q != std::string::npos) u.query = rest.substr(q + 1);
    }
    u.path = remove_dot_segments(path);
    u.valid = true;
    return u;
}

std::string to_string(const ParsedUrl& u) {
    std::string s = u.scheme + "://" + u.host;
    if (!u.port.empty()) s += ":" + u.port;
    s += u.path;
    if (!u.query.empty()) s += "?" + u.query;
    return s;
}

std::string normalize_url(const std::string& url) {
    ParsedUrl u = parse_url(url);
    return u.valid ? to_string(u) : std::string();
}

std::string resolve_url(const std::string& base, const std::string& href_in) {
    std::string href = trim(href_in);
    if (href.empty() || href[0] == '#') return "";
    std::string head = to_lower(href.substr(0, std::min<std::size_t>(href.size(), 11)));
    for (const char* bad : {"mailto:", "javascript:", "tel:", "data:", "ftp:", "file:", "sms:"})
        if (head.rfind(bad, 0) == 0) return "";
    if (href.find_first_of(" \t\r\n") != std::string::npos) return "";

    std::size_t p = href.find("://");
    if (p != std::string::npos && href.find_first_of("/?#") > p) return normalize_url(href);

    ParsedUrl b = parse_url(base);
    if (!b.valid) return "";
    if (href.compare(0, 2, "//") == 0) return normalize_url(b.scheme + ":" + href);

    std::string origin = b.scheme + "://" + b.host + (b.port.empty() ? "" : ":" + b.port);
    if (href[0] == '/') return normalize_url(origin + href);
    if (href[0] == '?') return normalize_url(origin + b.path + href);
    std::string dir = b.path.substr(0, b.path.rfind('/') + 1);
    return normalize_url(origin + dir + href);
}

std::string get_domain(const std::string& url) { return parse_url(url).host; }

bool is_crawlable(const std::string& url) {
    static const std::unordered_set<std::string> blocked = {
        "jpg", "jpeg", "png", "gif", "svg", "webp", "ico", "bmp", "css", "js", "json", "xml",
        "pdf", "zip", "gz", "tar", "rar", "7z", "mp3", "mp4", "avi", "mov", "wav", "exe", "dmg",
        "iso", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "woff", "woff2", "ttf"};
    ParsedUrl u = parse_url(url);
    if (!u.valid) return false;
    std::size_t slash = u.path.rfind('/');
    std::string last = u.path.substr(slash == std::string::npos ? 0 : slash + 1);
    std::size_t dot = last.rfind('.');
    if (dot == std::string::npos) return true;
    return blocked.count(to_lower(last.substr(dot + 1))) == 0;
}

}  // namespace wc
