CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/test_chroma build/test_yuvconv build/bench_yuvconv
build/test_chroma: src/chroma.c tests/test_chroma.c src/chroma.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/chroma.c tests/test_chroma.c -o $@
build/test_yuvconv: src/yuvconv.c src/chroma.c tests/test_yuvconv.c src/yuvconv.h src/chroma.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/yuvconv.c src/chroma.c tests/test_yuvconv.c -o $@
build/bench_yuvconv: src/yuvconv.c src/chroma.c bench/bench_yuvconv.c src/yuvconv.h src/chroma.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/yuvconv.c src/chroma.c bench/bench_yuvconv.c -o $@ -lm
test: build/test_chroma build/test_yuvconv
	./build/test_chroma
	./build/test_yuvconv
bench: build/bench_yuvconv
	./build/bench_yuvconv
clean:
	rm -rf build
.PHONY: all test bench clean
