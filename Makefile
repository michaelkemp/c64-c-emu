# Phase 1 build system: a plain Makefile (see docs/roadmap.md's Phase 1
# entry and docs/testing-strategy.md -- CMake wasn't available on the
# machine this was first built on, and the roadmap treats either choice
# as acceptable as long as it's written down. gcc/clang + this Makefile
# is the whole toolchain; no other build-system dependency exists).
#
# Targets:
#   make             - builds and runs the hand-written unit test suite
#   make unit-test   - same (no ROMs needed -- tier 1)
#   make fetch-dormann - fetches Klaus Dormann's 6502 functional test
#                      suite on demand (never vendored -- see CLAUDE.md)
#   make dormann     - builds and runs the Dormann suite against the CPU
#                      core (requires fetch-dormann first; tier 2)
#   make integration - real-ROM integration tests (tier 3) -- needs the
#                      user's own dumps staged via scripts/stage_roms.sh
#                      first; prints SKIP (not a failure) if they aren't
#   make demo        - ad-hoc smoke test (tools/demos/): assembles a
#                      small self-contained 6502 program (needs ca65/
#                      ld65 from the cc65 suite, no ROMs), runs it
#                      through the CPU+VIC-II, and dumps a screenshot --
#                      see tools/demos/framebuffer_dump.c
#   make demo-real-rom - same idea, but boots your own staged real ROMs
#                      (scripts/stage_roms.sh) through a real reset --
#                      see tools/demos/real_rom_boot_dump.c. Never commit
#                      its output; it renders real ROM content.
#   make clean       - removes build/

CC := gcc
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -O2 -g -Isrc
LDLIBS := -lm
BUILD_DIR := build

# Every module lands here as its own phase's chip/subsystem gets built --
# see CLAUDE.md's repo map.
CORE_SRCS := src/cpu/cpu6502.c src/c64/memory.c src/c64/cia.c src/c64/keyboard.c src/c64/vic_ii.c src/c64/palette.c src/c64/sid.c src/c64/machine.c
CORE_HDRS := src/bus.h src/cpu/cpu6502.h src/c64/memory.h src/c64/cia.h src/c64/keyboard.h src/c64/vic_ii.h src/c64/palette.h src/c64/sid.h src/c64/machine.h

# One self-contained test binary per tests/unit/test_*.c file (each has
# its own main()), sharing the tiny test framework in testutil.c.
UNIT_TEST_SUPPORT_SRC := tests/unit/testutil.c
UNIT_TEST_MAIN_SRCS := $(wildcard tests/unit/test_*.c)
UNIT_TEST_BINS := $(patsubst tests/unit/%.c,$(BUILD_DIR)/unit/%,$(UNIT_TEST_MAIN_SRCS))

DORMANN_RUNNER_SRC := tests/dormann/run_dormann.c
DORMANN_BIN := $(BUILD_DIR)/run_dormann
DORMANN_VENDOR_DIR := tests/vendor/6502_functional_tests
DORMANN_TEST_BINARY := $(DORMANN_VENDOR_DIR)/bin_files/6502_functional_test.bin

# Real-ROM integration tests (tier 3, docs/testing-strategy.md) -- need
# the user's own dumps staged via scripts/stage_roms.sh first; kept as a
# separate target so the two tiers above never require any ROM at all.
INTEGRATION_MAIN_SRCS := $(wildcard tests/integration/test_*.c)
INTEGRATION_BINS := $(patsubst tests/integration/%.c,$(BUILD_DIR)/integration/%,$(INTEGRATION_MAIN_SRCS))

.PHONY: all test unit-test fetch-dormann dormann integration demo demo-real-rom clean

all: unit-test

$(BUILD_DIR) $(BUILD_DIR)/unit $(BUILD_DIR)/integration:
	mkdir -p $@

$(BUILD_DIR)/unit/%: tests/unit/%.c $(UNIT_TEST_SUPPORT_SRC) $(CORE_SRCS) $(CORE_HDRS) tests/unit/testutil.h | $(BUILD_DIR)/unit
	$(CC) $(CFLAGS) $< $(UNIT_TEST_SUPPORT_SRC) $(CORE_SRCS) -o $@ $(LDLIBS)

unit-test: $(UNIT_TEST_BINS)
	@for bin in $(UNIT_TEST_BINS); do \
		echo "== $$bin =="; \
		./$$bin || exit 1; \
	done

test: unit-test

fetch-dormann:
	scripts/fetch_dormann_tests.sh

$(DORMANN_BIN): $(DORMANN_RUNNER_SRC) $(CORE_SRCS) $(CORE_HDRS) | $(BUILD_DIR)
	@if [ ! -d "$(DORMANN_VENDOR_DIR)" ]; then \
		echo "Dormann suite not fetched yet -- run 'make fetch-dormann' first." >&2; \
		exit 1; \
	fi
	$(CC) $(CFLAGS) $(DORMANN_RUNNER_SRC) $(CORE_SRCS) -o $@ $(LDLIBS)

dormann: $(DORMANN_BIN)
	./$(DORMANN_BIN) $(DORMANN_TEST_BINARY)

$(BUILD_DIR)/integration/%: tests/integration/%.c $(CORE_SRCS) $(CORE_HDRS) | $(BUILD_DIR)/integration
	$(CC) $(CFLAGS) $< $(CORE_SRCS) -o $@ $(LDLIBS)

integration: $(INTEGRATION_BINS)
	@for bin in $(INTEGRATION_BINS); do \
		echo "== $$bin =="; \
		./$$bin || exit 1; \
	done

DEMO_DIR := tools/demos
DEMO_ASM := $(DEMO_DIR)/hello_c64.s
DEMO_CFG := $(DEMO_DIR)/hello_c64.cfg
DEMO_BIN := $(BUILD_DIR)/hello_c64.bin
DEMO_TOOL_BIN := $(BUILD_DIR)/framebuffer_dump
DEMO_OUT_PPM := $(BUILD_DIR)/hello_c64.ppm

$(DEMO_BIN): $(DEMO_ASM) $(DEMO_CFG) | $(BUILD_DIR)
	@command -v ca65 >/dev/null && command -v ld65 >/dev/null || \
		{ echo "ca65/ld65 (the cc65 suite) not found -- install it to build the demo program." >&2; exit 1; }
	ca65 $(DEMO_ASM) -o $(BUILD_DIR)/hello_c64.o
	ld65 -C $(DEMO_CFG) $(BUILD_DIR)/hello_c64.o -o $(DEMO_BIN)

$(DEMO_TOOL_BIN): $(DEMO_DIR)/framebuffer_dump.c $(CORE_SRCS) $(CORE_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DEMO_DIR)/framebuffer_dump.c $(CORE_SRCS) -o $@ $(LDLIBS)

demo: $(DEMO_BIN) $(DEMO_TOOL_BIN)
	./$(DEMO_TOOL_BIN) $(DEMO_BIN) C000 $(DEMO_OUT_PPM) 3
	@echo "Wrote $(DEMO_OUT_PPM) -- view directly, or convert to PNG with:"
	@echo "  convert $(DEMO_OUT_PPM) $(BUILD_DIR)/hello_c64.png"

REAL_ROM_DEMO_BIN := $(BUILD_DIR)/real_rom_boot_dump
REAL_ROM_OUT_PPM := $(BUILD_DIR)/real_boot.ppm

$(REAL_ROM_DEMO_BIN): $(DEMO_DIR)/real_rom_boot_dump.c $(CORE_SRCS) $(CORE_HDRS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(DEMO_DIR)/real_rom_boot_dump.c $(CORE_SRCS) -o $@ $(LDLIBS)

demo-real-rom: $(REAL_ROM_DEMO_BIN)
	@if [ ! -f roms/c64/kernal.rom ] || [ ! -f roms/c64/basic.rom ] || [ ! -f roms/c64/chargen.rom ]; then \
		echo "Real ROMs not staged under roms/c64/ -- run scripts/stage_roms.sh with your own legally-acquired dumps first." >&2; \
		exit 1; \
	fi
	./$(REAL_ROM_DEMO_BIN) roms/c64/kernal.rom roms/c64/basic.rom roms/c64/chargen.rom $(REAL_ROM_OUT_PPM)
	@echo "Wrote $(REAL_ROM_OUT_PPM) -- view directly, or convert to PNG with:"
	@echo "  convert $(REAL_ROM_OUT_PPM) $(BUILD_DIR)/real_boot.png"
	@echo "(Never commit this output -- it renders real, copyrighted KERNAL/BASIC ROM content. See CLAUDE.md's license discipline.)"

clean:
	rm -rf $(BUILD_DIR)
