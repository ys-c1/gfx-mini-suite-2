CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
all: build/test_yuvrot build/exhaustive
build/test_yuvrot: src/yuvrot.c tests/test_yuvrot.c src/yuvrot.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/yuvrot.c tests/test_yuvrot.c -o $@
build/exhaustive: src/yuvrot.c tests/exhaustive.c src/yuvrot.h
	@mkdir -p build
	$(CC) $(CFLAGS) src/yuvrot.c tests/exhaustive.c -o $@
test: build/test_yuvrot build/exhaustive
	./build/test_yuvrot
	./build/exhaustive
clean:
	rm -rf build
.PHONY: all test clean
