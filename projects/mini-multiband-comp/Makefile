PYTHON ?= python3
all:
	$(PYTHON) -m py_compile src/mbc.py
test: all
	$(PYTHON) tests/test_mbc.py
bench: all
	$(PYTHON) tests/bench_mbc.py
clean:
	rm -rf src/__pycache__ tests/__pycache__
.PHONY: all test bench clean
