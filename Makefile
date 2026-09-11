# Phase 1 build system: a plain Makefile (see docs/roadmap.md's Phase 1
# entry and docs/testing-strategy.md -- CMake wasn't available on the
# machine this was first built on, and the roadmap treats either choice
# as acceptable as long as it's written down. gcc/clang + this Makefile
# is the whole toolchain; no other build-system dependency exists).
#
# Targets:
#   make            - builds and runs the hand-written unit test suite
#   make unit-test   - same
#   make fetch-dormann - fetches Klaus Dormann's 6502 functional test
#                      suite on demand (never vendored -- see CLAUDE.md)
#   make dormann     - builds and runs the Dormann suite against the CPU
#                      core (requires fetch-dormann, then assembling the
#                      suite with ca65 -- see tests/dormann/README.md)
#   make clean       - removes build/

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -g -Isrc
BUILD_DIR := build

CORE_SRCS := src/cpu/cpu6502.c
CORE_HDRS := src/bus.h src/cpu/cpu6502.h

UNIT_TEST_SRCS := tests/unit/test_cpu.c tests/unit/testutil.c
UNIT_TEST_BIN := $(BUILD_DIR)/unit_tests

DORMANN_RUNNER_SRC := tests/dormann/run_dormann.c
DORMANN_BIN := $(BUILD_DIR)/run_dormann
DORMANN_VENDOR_DIR := tests/vendor/6502_functional_tests
DORMANN_TEST_BINARY := $(DORMANN_VENDOR_DIR)/bin_files/6502_functional_test.bin

.PHONY: all test unit-test fetch-dormann dormann clean

all: unit-test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(UNIT_TEST_BIN): $(UNIT_TEST_SRCS) $(CORE_SRCS) $(CORE_HDRS) tests/unit/testutil.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(UNIT_TEST_SRCS) $(CORE_SRCS) -o $@

unit-test: $(UNIT_TEST_BIN)
	./$(UNIT_TEST_BIN)

test: unit-test

fetch-dormann:
	scripts/fetch_dormann_tests.sh

$(DORMANN_BIN): $(DORMANN_RUNNER_SRC) $(CORE_SRCS) $(CORE_HDRS) | $(BUILD_DIR)
	@if [ ! -d "$(DORMANN_VENDOR_DIR)" ]; then \
		echo "Dormann suite not fetched yet -- run 'make fetch-dormann' first." >&2; \
		exit 1; \
	fi
	$(CC) $(CFLAGS) $(DORMANN_RUNNER_SRC) $(CORE_SRCS) -o $@

dormann: $(DORMANN_BIN)
	./$(DORMANN_BIN) $(DORMANN_TEST_BINARY)

clean:
	rm -rf $(BUILD_DIR)
