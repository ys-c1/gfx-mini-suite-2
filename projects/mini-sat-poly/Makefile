CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/test_sat
build/test_sat: src/sat.c tests/test_sat.c src/sat.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/sat.c tests/test_sat.c -lm -o $@
test: build/test_sat
	./build/test_sat
clean:
	rm -rf build
.PHONY: all test clean
