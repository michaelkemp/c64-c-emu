#ifndef C64EMU_TESTUTIL_H
#define C64EMU_TESTUTIL_H

/* Tiny, dependency-free assert-based test runner -- see
 * docs/testing-strategy.md: the Phase 1 build system choice (a plain
 * Makefile) deliberately avoids pulling in a third-party test
 * framework. */

#include <stdint.h>
#include <stdio.h>

#include "../../src/bus.h"
#include "../../src/cpu/cpu6502.h"

extern int g_tests_run;
extern int g_tests_failed;

#define TEST_ASSERT(cond)                                                    \
    do {                                                                     \
        g_tests_run++;                                                       \
        if (!(cond)) {                                                       \
            g_tests_failed++;                                                \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                    \
    } while (0)

#define TEST_ASSERT_EQ_U8(actual, expected)                                              \
    do {                                                                                 \
        uint8_t a_ = (uint8_t)(actual);                                                  \
        uint8_t e_ = (uint8_t)(expected);                                                \
        g_tests_run++;                                                                   \
        if (a_ != e_) {                                                                  \
            g_tests_failed++;                                                            \
            fprintf(stderr, "FAIL %s:%d: %s == %s -- got $%02X, expected $%02X\n",       \
                    __FILE__, __LINE__, #actual, #expected, a_, e_);                      \
        }                                                                                 \
    } while (0)

#define TEST_ASSERT_EQ_U16(actual, expected)                                             \
    do {                                                                                 \
        uint16_t a_ = (uint16_t)(actual);                                                \
        uint16_t e_ = (uint16_t)(expected);                                              \
        g_tests_run++;                                                                   \
        if (a_ != e_) {                                                                  \
            g_tests_failed++;                                                            \
            fprintf(stderr, "FAIL %s:%d: %s == %s -- got $%04X, expected $%04X\n",       \
                    __FILE__, __LINE__, #actual, #expected, a_, e_);                      \
        }                                                                                 \
    } while (0)

#define TEST_ASSERT_EQ_INT(actual, expected)                                             \
    do {                                                                                 \
        long a_ = (long)(actual);                                                        \
        long e_ = (long)(expected);                                                      \
        g_tests_run++;                                                                   \
        if (a_ != e_) {                                                                  \
            g_tests_failed++;                                                            \
            fprintf(stderr, "FAIL %s:%d: %s == %s -- got %ld, expected %ld\n",           \
                    __FILE__, __LINE__, #actual, #expected, a_, e_);                      \
        }                                                                                 \
    } while (0)

/* Flat 64KB RAM test harness -- the CPU core is bus-agnostic (see
 * docs/6502-reference.md), so no C64 memory map is needed to test it,
 * only this trivial bus. */
typedef struct FlatBus {
    uint8_t mem[65536];
} FlatBus;

void flat_bus_init(FlatBus *fb);
Bus flat_bus_as_bus(FlatBus *fb);

/* Runs cpu6502_cycle() until the instruction that was in flight when
 * called completes (mid_instruction becomes false again). Convenience
 * for hand-written unit tests that think in whole instructions; the
 * Dormann harness and, later, Phase 6's real main loop use
 * cpu6502_cycle() directly, one PHI2 cycle at a time. */
void run_one_instruction(Cpu6502 *cpu);

#endif /* C64EMU_TESTUTIL_H */
