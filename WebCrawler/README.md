# Concurrent Web Crawler and Web Analysis System

A multi-threaded web crawler in **C++17**. Give it one or more starting URLs and it downloads pages,
follows the links it finds, and then analyses what it collected: which pages matter most (PageRank),
which links are broken, which pages contain a keyword, and how each site is structured.

Built for the Data Structures Lab. The main data structures (hash table, heap, trie, graph, inverted
index, bounded buffer) are written from scratch in this repo rather than taken from a library.

---

## Quick start

```bash
# 1. Install tools (Ubuntu/Debian). libcurl is only needed to crawl the real web.
sudo apt install build-essential cmake libcurl4-openssl-dev

# 2. Build
cmake -S . -B build && cmake --build build -j

# 3. Run on a built-in fake web (no internet needed)
./build/webcrawler --simulate -o out

# 4. Crawl a real site
./build/webcrawler https://example.com --depth 2 --max-pages 50 --same-domain -i

# 5. Or just run it with no arguments and answer the questions
./build/webcrawler
```

**Running with no arguments** starts a short question-and-answer setup (press Enter to accept the
value in brackets, or type `sim` for the built-in fake web):

```
=== Web Crawler ===
Start URL [sim]: example.com
Crawl depth (link hops from the start) [3]: 2
Maximum pages [200]: 50
Worker threads [8]:
Stay on this website only? (y/n) [y]:
Open the search shell after the crawl? (y/n) [y]:
Save result files to folder (blank = don't save) []:
```

No CMake? Use `make` (or `make CURL=1` for real-web support) and run `./webcrawler`.
macOS: `brew install cmake`, then the same build commands. Windows: use WSL.

If libcurl isn't installed, the program still builds, but only `--simulate` works.

---

## What happens under the hood

### The big picture

Fetching a web page is slow because the program mostly **waits for the network**. A simple crawler
waits for one page, then the next. This crawler runs many downloads at the same time, so the waiting
overlaps.

```
 seed URLs
     |
     v
 [ Frontier ]  <------------------------------+
 (priority queue)                             |
     |  "give me the most important URL"      |
     v                                        |
 [ Worker 1 ] [ Worker 2 ] ... [ Worker N ]   |   <- download pages in parallel
     |                                        |
     v                                        |
 [ Buffer ]   (a waiting line for results)    |
     |                                        |
     v                                        |
 [ Indexer ]  reads each page, finds links ---+   <- new links go back to the Frontier
     |
     +--> Graph (who links to whom)
     +--> Trie (URLs grouped by site/folder)
     +--> Inverted index (word -> pages)
```

There are two kinds of threads:

- **Workers (many):** take a URL, download it, drop the result in the buffer. They never touch the analysis data.
- **Indexer (one):** takes results from the buffer, reads the HTML, and updates everything else.

Because only one thread changes the graph, trie and index, they don't fight over locks, and workers
spend their time downloading, not waiting on each other.

### The journey of one URL

1. **Normalize.** `HTTP://Example.com:80/a/../b#top` becomes `http://example.com/b`. Without this,
   the same page could look like many different URLs.
2. **Check for duplicates.** The URL is looked up in a **hash set** of everything already seen
   (O(1) on average). If it's there, it's skipped. This is what stops the crawler going in circles.
3. **Prioritize.** The URL goes into the **priority queue** (a max-heap). Pages closer to the start,
   on the starting site, with short paths score higher, so they're crawled first.
4. **Wait your turn.** A worker pops the best URL. The **rate limiter** makes sure the same domain isn't
   hit more often than the `--delay` allows, so the crawler stays polite.
5. **Download.** The worker fetches the page and puts the result in the **bounded buffer**.
6. **Analyse.** The indexer parses the HTML and pulls out the title, text and `<a href>` links
   (ignoring scripts, styles and comments). Each link is turned into a full normalized URL.
7. **Record.** The page and its links are added to the **graph**, the URL to the **trie**, and its words to
   the **inverted index**.
8. **Repeat.** Any new link that passes the checks (depth limit, page limit, same-domain, not an image/PDF)
   goes back into the frontier at step 2.

### What each data structure is for

| Structure | Job in the crawler | Why it fits |
|---|---|---|
| **Hash set / table** | "Have I seen this URL?"; stores page info | Constant-time lookup |
| **Max-heap** | Always hands out the most important URL next | Insert and remove best in O(log n) |
| **Trie** | Groups URLs by site, then folder, then page; prefix search like "everything under site3.example" | Shared prefixes are stored once |
| **Graph** | Pages are nodes, links are arrows; powers PageRank, BFS/DFS and broken-link finding | Natural model of the web |
| **Inverted index** | Maps a word to the pages containing it, so keyword search is instant | Avoids scanning every page |
| **Bounded buffer** | Hand-off between workers and the indexer; blocks when full or empty | Classic producer-consumer |

### How the threads stay safe

- **Mutex:** a lock so only one thread changes a shared structure at a time.
- **Condition variable:** lets a thread *sleep* until something happens (e.g. a worker sleeps until a URL
  arrives) instead of wasting CPU in a loop.
- The frontier and the buffer each have their own lock and condition variable. The analysis data is
  protected by one separate lock.
- The slow work, HTML parsing, is done *outside* that lock so it doesn't block others.
- The rate limiter books a future time slot for a domain while holding the lock, then sleeps *after
  releasing it*, so waiting on one domain never blocks work on another.

### How it knows when to stop

A counter tracks every URL that is **queued, being downloaded, or waiting to be analysed**.
It goes up *before* a URL enters the frontier, and down only *after* that page has been fully analysed
(including queueing its new links). So it can only hit zero when there is genuinely nothing left. At
that moment the frontier is closed, the workers wake up and exit, and then the indexer finishes. No timers or guessing.

### PageRank in one paragraph

Every page starts with the same score. In each round, every page shares its score equally among the pages
it links to. A page linked from many (or from high-scoring) pages ends up with a high score. After a
few dozen rounds the scores settle, and the top of the list is the most "important" pages. Pages with
no outgoing links share their score with everyone, so score isn't lost.

### Broken links

If a page returns 404/500 or the download fails, its graph node is marked broken. The report then
shows each broken URL together with a page that links to it.

### The simulated web

`--simulate` uses a fake network of sites (`site0.example`, `site1.example`, ...) generated
deterministically, with a fake delay per request. It includes duplicate links, relative links, `mailto:`
links, images and some 404s, so every feature gets exercised with no internet. It's also how the tests and
the benchmark stay repeatable.

---

## Options

| Option | Meaning | Default |
|---|---|---|
| `<url> ...` | start URLs (if none, you are asked for one) | |
| `-t, --threads N` | number of worker threads | 8 |
| `-d, --depth N` | how many link-hops from the start | 3 |
| `-m, --max-pages N` | stop after N pages | 200 |
| `--delay MS` | minimum gap between requests to one domain | 200 (20 simulated) |
| `--same-domain` | don't leave the starting site(s) | off |
| `-o, --out DIR` | save result files | none |
| `-i, --interactive` | query shell after the crawl | off |
| `-v, --verbose` | print every page fetched | off |
| `--quiet` | hide the live stats line | off |
| `--simulate` | use the fake web | off |
| `--benchmark` | compare 1, 2, 4, 8, 16 threads | off |
| `--top N` | rows in the PageRank table | 10 |
| `--timeout S` | request timeout (real web) | 10 |
| `-h, --help` | show help | |

Extra simulation options: `--sim-sites N`, `--sim-pages N`, `--sim-latency MS`.

## What you get

**On screen:** totals, pages per domain, top pages by PageRank, broken links, common keywords, the URL
tree, and how many pages are reachable at each depth.

**With `-o out`:**

| File | Content |
|---|---|
| `pages.csv` | every URL with status, depth, links, PageRank, title |
| `broken_links.txt` | failed URLs |
| `graph.dot` | link graph of the top pages (view with `dot -Tsvg out/graph.dot -o graph.svg`, needs Graphviz) |
| `url_trie.txt` | full URL tree |

**With `-i`**, a small shell opens:

```
search kernel pointer     pages containing all these words
prefix site3.example      all pages under a site/folder (trie)
rank 5                    top 5 pages by PageRank
broken                    list broken links
info <url>                details for one page
bfs <url> 2               pages within 2 clicks
quit
```

## Speed-up from concurrency

`./build/webcrawler --benchmark` crawls the fake web with different thread counts. Sample run:

| Threads | Time (s) | Speed-up |
|---|---|---|
| 1 | 6.33 | 1.00x |
| 2 | 3.19 | 1.98x |
| 4 | 1.64 | 3.86x |
| 8 | 0.86 | 7.35x |
| 16 | 0.51 | 12.43x |

Speed-up is close to the thread count because the work is mostly waiting. It levels off once locks or the
per-domain delay become the limit. The fake web uses a sleep as latency, so real-world numbers will differ.

## Tests

```bash
ctest --test-dir build --output-on-failure      # or: make test
```

The tests cover each data structure, URL handling, the HTML parser, the rate limiter and full crawls. One
test crawls with 1 thread and with 8 and checks that both find exactly the same pages and links, which
would reveal race conditions.

## Code map

```
include/crawler/   headers: hash_table, priority_queue, bounded_buffer, frontier, rate_limiter,
                   trie, graph, inverted_index, url_utils, html_parser, fetcher, crawler, report
src/               implementations and main.cpp (command line)
tests/tests.cpp    unit and integration tests
CMakeLists.txt     Makefile    build files
```

Start reading at `src/crawler.cpp`: `run()`, `worker_loop()`, `indexer_loop()` and `process_result()` are
the whole pipeline described above.

## Complexity

*n* = unique URLs, *E* = links, *L* = URL length, *k* = keywords per page, *I* = PageRank rounds.

| Operation | Time | Space |
|---|---|---|
| Duplicate check (hash set) | O(L) average | O(n·L) |
| Frontier push / pop | O(log n) | O(n) |
| Trie insert / search | O(L) | O(n·L) worst case |
| Add page / link to graph | O(1) amortized | O(n + E) |
| Keyword index insert / lookup | O(k) per page / O(1) average | O(total keywords) |
| BFS / DFS | O(n + E) | O(n) |
| PageRank | O(I·(n + E)) | O(n) |

## Limitations

- `robots.txt` is **not** honoured. Only crawl sites you have permission to crawl and keep the default delay.
- JavaScript-generated content isn't seen; only plain HTML is parsed.
- Real-web crawling needs libcurl; the simulated mode and tests need nothing.

## Troubleshooting

| Problem | Fix |
|---|---|
| "no libcurl support" | `sudo apt install libcurl4-openssl-dev`, delete `build/`, rebuild (Makefile: `make clean && make CURL=1`) |
| Fewer pages than `--max-pages` | The crawl ran out of links within `--depth`; raise `--depth` |
| Slow on one site | That's the politeness delay; lower `--delay` only for servers you own |
| `dot: command not found` | Install Graphviz |

## References

1. Cormen, Leiserson, Rivest, Stein - *Introduction to Algorithms*
2. Silberschatz, Galvin, Gagne - *Operating System Concepts*
3. Brin and Page - *The Anatomy of a Large-Scale Hypertextual Web Search Engine*

MIT License (see `LICENSE`).
