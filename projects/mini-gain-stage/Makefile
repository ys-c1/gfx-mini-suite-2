PYTHON ?= python3
all:
	$(PYTHON) -m py_compile src/gain.py tests/materials.py
test: all
	$(PYTHON) tests/test_gain.py
	$(PYTHON) tests/test_loudness.py
report: all
	$(PYTHON) tests/report_normalize.py
clean:
	rm -rf src/__pycache__ tests/__pycache__
.PHONY: all test clean report
