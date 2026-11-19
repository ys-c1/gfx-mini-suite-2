CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/test_chroma build/test_guard
build/test_chroma: src/chroma.c tests/test_chroma.c src/chroma.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/chroma.c tests/test_chroma.c -o $@
build/test_guard: src/chroma.c tests/test_guard.c src/chroma.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/chroma.c tests/test_guard.c -o $@

test: build/test_chroma build/test_guard
	./build/test_chroma
	./build/test_guard

clean:
	rm -rf build

.PHONY: all test clean
