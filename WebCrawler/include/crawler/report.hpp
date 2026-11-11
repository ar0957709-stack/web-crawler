// Console report, file export and the interactive query shell.
#pragma once
#include <iosfwd>
#include <string>

#include "crawler/crawler.hpp"

namespace wc {

void print_summary(const Crawler& c, std::size_t top_n, std::ostream& os);
bool export_results(const Crawler& c, const std::string& dir, std::size_t dot_nodes, std::ostream& log);
void run_repl(const Crawler& c, std::istream& in, std::ostream& os);

}  // namespace wc
