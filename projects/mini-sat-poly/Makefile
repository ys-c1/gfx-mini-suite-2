CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/test_sat build/compat_sat
build/test_sat: src/sat.c tests/test_sat.c tests/exact_ref.c tests/exact_ref.h src/sat.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/sat.c tests/exact_ref.c tests/test_sat.c -lm -o $@
build/compat_sat: src/sat.c tests/compat_sat.c tests/orig_sat_snapshot.c src/sat.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/sat.c tests/compat_sat.c -lm -o $@
test: build/test_sat build/compat_sat
	./build/test_sat
	./build/compat_sat
clean:
	rm -rf build
.PHONY: all test clean
