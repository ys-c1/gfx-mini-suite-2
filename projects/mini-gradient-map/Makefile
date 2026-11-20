CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11

all: build/test_grad build/test_chain

build/test_grad: src/grad.c tests/test_grad.c src/grad.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/grad.c tests/test_grad.c -lm -o $@

build/test_chain: src/grad.c src/grad_chain.c tests/test_chain.c src/grad.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/grad.c src/grad_chain.c tests/test_chain.c -lm -o $@

test: build/test_grad build/test_chain
	./build/test_grad
	./build/test_chain

clean:
	rm -rf build

.PHONY: all test clean
