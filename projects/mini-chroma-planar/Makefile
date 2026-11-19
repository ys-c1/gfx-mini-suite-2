CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/test_chroma
build/test_chroma: src/chroma.c tests/test_chroma.c src/chroma.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/chroma.c tests/test_chroma.c -o $@
test: build/test_chroma
	./build/test_chroma
clean:
	rm -rf build
.PHONY: all test clean
