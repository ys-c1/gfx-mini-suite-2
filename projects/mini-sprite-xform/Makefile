CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
all: build/test_xform
build/test_xform: src/xform.cpp tests/test_xform.cpp src/xform.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/xform.cpp tests/test_xform.cpp -o $@
build/bench_xform: src/xform.cpp tests/bench_xform.cpp src/xform.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/xform.cpp tests/bench_xform.cpp -o $@
build/cachesim_xform: tests/cachesim_xform.cpp src/xform.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) tests/cachesim_xform.cpp -o $@
test: build/test_xform
	./build/test_xform
bench: build/bench_xform
	./build/bench_xform
cachesim: build/cachesim_xform
	./build/cachesim_xform
clean:
	rm -rf build
.PHONY: all test bench cachesim clean
