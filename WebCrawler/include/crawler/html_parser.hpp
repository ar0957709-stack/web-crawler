// Minimal single-pass HTML scanner: title, <a href> links and keywords.
#pragma once
#include <string>
#include <vector>

namespace wc {

struct ParsedPage {
    std::string title;
    std::vector<std::string> links;     // raw href values (not yet resolved)
    std::vector<std::string> keywords;  // lower-case tokens, stop words removed
};

ParsedPage parse_html(const std::string& html);

// Lower-case alphabetic tokens (3..25 chars) without stop words.
std::vector<std::string> tokenize(const std::string& text);

}  // namespace wc
