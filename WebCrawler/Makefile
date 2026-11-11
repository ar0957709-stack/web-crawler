# Simple Makefile (no CMake needed).
#   make            build ./webcrawler  (simulated web only)
#   make CURL=1     build with libcurl so it can crawl real websites
#   make test       build and run the unit tests
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pthread -Iinclude
LDFLAGS  ?= -pthread

ifeq ($(CURL),1)
CXXFLAGS += -DHAVE_CURL=1
LDLIBS   += -lcurl
endif

LIB_SRC := $(filter-out src/main.cpp,$(wildcard src/*.cpp))
LIB_OBJ := $(LIB_SRC:src/%.cpp=build/%.o)

all: webcrawler

build/%.o: src/%.cpp $(wildcard include/crawler/*.hpp)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

webcrawler: $(LIB_OBJ) build/main.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

run_tests: $(LIB_OBJ) tests/tests.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

test: run_tests
	./run_tests

clean:
	rm -rf build webcrawler run_tests

.PHONY: all test clean
