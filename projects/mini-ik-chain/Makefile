CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/test_ik
build/test_ik: src/ik.c tests/test_ik.c src/ik.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/ik.c tests/test_ik.c -lm -o $@
test: build/test_ik
	./build/test_ik
clean:
	rm -rf build
.PHONY: all test clean
