CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/test_grad
build/test_grad: src/grad.c tests/test_grad.c src/grad.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/grad.c tests/test_grad.c -lm -o $@
test: build/test_grad
	./build/test_grad
clean:
	rm -rf build
.PHONY: all test clean
