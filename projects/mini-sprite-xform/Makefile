CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
N ?= 20000
SEED ?= 20261120

all: build/test_xform build/test_cull build/bench_cull build/gen_data

build/test_xform: src/xform.cpp tests/test_xform.cpp src/xform.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/xform.cpp tests/test_xform.cpp -o $@

build/test_cull: src/cull.cpp src/xform.cpp tests/test_cull.cpp src/cull.h src/xform.h tests/gen_data.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/cull.cpp src/xform.cpp tests/test_cull.cpp -o $@

build/bench_cull: src/cull.cpp tests/bench_cull.cpp src/cull.h tests/gen_data.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/cull.cpp tests/bench_cull.cpp -o $@

build/gen_data: tests/gen_data.cpp src/cull.h tests/gen_data.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/gen_data.cpp -o $@

# Committed fixtures are regenerated only on explicit demand.
data: build/gen_data
	./build/gen_data $(N) $(SEED)

test: build/test_xform build/test_cull
	@if [ ! -f data/dense_inside.csv ]; then $(MAKE) data; fi
	./build/test_xform
	./build/test_cull

bench: build/bench_cull
	./build/bench_cull

clean:
	rm -rf build

.PHONY: all test bench data clean
