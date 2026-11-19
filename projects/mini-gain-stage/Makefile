PYTHON ?= python3
all:
	$(PYTHON) -m py_compile src/gain.py
test: all
	$(PYTHON) tests/test_gain.py
	$(PYTHON) tests/test_protect.py
report: all
	$(PYTHON) tests/report_protection.py
clean:
	rm -rf src/__pycache__ tests/__pycache__
.PHONY: all test clean
