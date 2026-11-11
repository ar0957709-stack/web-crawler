cmake_minimum_required(VERSION 3.12)
project(WebCrawler VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Threads REQUIRED)
find_package(CURL)   # optional: needed only to crawl the real web

add_library(wccore STATIC
  src/url_utils.cpp src/html_parser.cpp src/trie.cpp src/graph.cpp
  src/inverted_index.cpp src/fetcher.cpp src/crawler.cpp src/report.cpp)
target_include_directories(wccore PUBLIC include)
target_link_libraries(wccore PUBLIC Threads::Threads)
if(MSVC)
  target_compile_options(wccore PRIVATE /W4)
else()
  target_compile_options(wccore PRIVATE -Wall -Wextra)
endif()

if(CURL_FOUND)
  message(STATUS "libcurl found: real-web crawling enabled")
  target_compile_definitions(wccore PUBLIC HAVE_CURL=1)
  target_link_libraries(wccore PUBLIC CURL::libcurl)
else()
  message(WARNING "libcurl not found: only --simulate mode will work")
endif()

add_executable(webcrawler src/main.cpp)
target_link_libraries(webcrawler PRIVATE wccore)

enable_testing()
add_executable(run_tests tests/tests.cpp)
target_link_libraries(run_tests PRIVATE wccore)
add_test(NAME unit_tests COMMAND run_tests)
