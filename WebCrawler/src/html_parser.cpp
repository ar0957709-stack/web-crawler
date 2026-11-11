#include "crawler/html_parser.hpp"

#include <cctype>
#include <unordered_set>

#include "crawler/url_utils.hpp"

namespace wc {
namespace {

// Value of attribute `name` inside the text of a tag, or "".
std::string get_attr(const std::string& attrs, const std::string& name) {
    std::string low = to_lower(attrs);
    std::size_t pos = 0;
    while ((pos = low.find(name, pos)) != std::string::npos) {
        bool boundary = (pos == 0) || std::isspace(static_cast<unsigned char>(low[pos - 1])) ||
                        low[pos - 1] == '"' || low[pos - 1] == '\'';
        std::size_t k = pos + name.size();
        while (k < low.size() && std::isspace(static_cast<unsigned char>(low[k]))) ++k;
        if (!boundary || k >= low.size() || low[k] != '=') { pos += name.size(); continue; }
        ++k;
        while (k < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[k]))) ++k;
        if (k >= attrs.size()) return "";
        std::string value;
        if (attrs[k] == '"' || attrs[k] == '\'') {
            char q = attrs[k++];
            std::size_t e = attrs.find(q, k);
            value = attrs.substr(k, e == std::string::npos ? std::string::npos : e - k);
        } else {
            std::size_t e = k;
            while (e < attrs.size() && !std::isspace(static_cast<unsigned char>(attrs[e]))) ++e;
            value = attrs.substr(k, e - k);
        }
        for (std::size_t a; (a = value.find("&amp;")) != std::string::npos;)
            value.replace(a, 5, "&");
        return trim(value);
    }
    return "";
}

const std::unordered_set<std::string>& stop_words() {
    static const std::unordered_set<std::string> s = {
        "the", "and", "for", "are", "but", "not", "you", "all", "any", "can", "had", "her",
        "was", "one", "our", "out", "has", "have", "this", "that", "with", "from", "they",
        "will", "would", "there", "their", "what", "about", "which", "when", "your", "been",
        "were", "into", "than", "then", "them", "these", "those", "some", "such", "only",
        "also", "its", "his", "she", "him", "who", "how", "why", "may", "more", "most"};
    return s;
}

}  // namespace

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] {
        if (cur.size() >= 3 && cur.size() <= 25 && !stop_words().count(cur)) out.push_back(cur);
        cur.clear();
    };
    for (unsigned char c : text) {
        if (std::isalpha(c)) cur.push_back(static_cast<char>(std::tolower(c)));
        else flush();
    }
    flush();
    return out;
}

ParsedPage parse_html(const std::string& html) {
    ParsedPage page;
    std::string text, lower_html;
    text.reserve(html.size() / 2);
    const std::size_t n = html.size();
    std::size_t i = 0;
    bool in_title = false;

    while (i < n) {
        if (html[i] != '<') {
            std::size_t j = html.find('<', i);
            if (j == std::string::npos) j = n;
            text.append(html, i, j - i);
            text.push_back(' ');
            if (in_title) page.title.append(html, i, j - i);
            i = j;
            continue;
        }
        if (html.compare(i, 4, "<!--") == 0) {
            std::size_t e = html.find("-->", i + 4);
            i = (e == std::string::npos) ? n : e + 3;
            continue;
        }
        std::size_t j = i + 1;
        bool closing = false;
        if (j < n && html[j] == '/') { closing = true; ++j; }
        std::size_t name_start = j;
        while (j < n && (std::isalnum(static_cast<unsigned char>(html[j])) || html[j] == '-')) ++j;
        std::string name = to_lower(html.substr(name_start, j - name_start));

        std::size_t k = j;
        char quote = 0;
        while (k < n) {
            char c = html[k];
            if (quote) { if (c == quote) quote = 0; }
            else if (c == '"' || c == '\'') quote = c;
            else if (c == '>') break;
            ++k;
        }
        std::string attrs = html.substr(j, k - j);
        i = (k < n) ? k + 1 : n;
        if (name.empty()) continue;

        if (closing) {
            if (name == "title") in_title = false;
        } else if (name == "a") {
            std::string href = get_attr(attrs, "href");
            if (!href.empty()) page.links.push_back(href);
        } else if (name == "title") {
            in_title = true;
        } else if (name == "script" || name == "style") {
            if (lower_html.empty()) lower_html = to_lower(html);
            std::size_t e = lower_html.find("</" + name, i);
            if (e == std::string::npos) { i = n; }
            else {
                std::size_t gt = html.find('>', e);
                i = (gt == std::string::npos) ? n : gt + 1;
            }
        }
    }
    page.title = trim(page.title);
    page.keywords = tokenize(text);
    return page;
}

}  // namespace wc
