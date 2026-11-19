CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
all: build/test_xform
build/test_xform: src/xform.cpp tests/test_xform.cpp src/xform.h
	@mkdir -p build
	$(CXX) $(CXXFLAGS) src/xform.cpp tests/test_xform.cpp -o $@
test: build/test_xform
	./build/test_xform
clean:
	rm -rf build
.PHONY: all test clean
