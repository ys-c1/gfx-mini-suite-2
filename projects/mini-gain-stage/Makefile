PYTHON ?= python3
all:
	$(PYTHON) -m py_compile src/gain.py
test: all
	$(PYTHON) tests/test_gain.py
clean:
	rm -rf src/__pycache__ tests/__pycache__
.PHONY: all test clean
