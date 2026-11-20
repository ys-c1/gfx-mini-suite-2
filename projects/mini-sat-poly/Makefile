CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
PYTHON ?= python3
all: build/test_sat build/test_circle build/circle_cli
build/test_sat: src/sat.c tests/test_sat.c src/sat.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/sat.c tests/test_sat.c -lm -o $@
build/test_circle: src/sat.c tests/test_circle.c src/sat.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/sat.c tests/test_circle.c -lm -o $@
build/circle_cli: src/sat.c tests/circle_cli.c src/sat.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/sat.c tests/circle_cli.c -lm -o $@
test: build/test_sat build/test_circle build/circle_cli
	./build/test_sat
	./build/test_circle
	$(PYTHON) -I tests/verify_circle.py ./build/circle_cli
bench: build/test_circle
	./build/test_circle | sed -n '/configurations/,/checks/p'
clean:
	rm -rf build
.PHONY: all test bench clean
