/* Runs Klaus Dormann's 6502 functional test suite against the Phase 1
 * CPU core. See docs/6502-reference.md and docs/testing-strategy.md.
 *
 * The suite (fetched on demand by scripts/fetch_dormann_tests.sh, never
 * vendored -- see CLAUDE.md's license discipline) ships a prebuilt flat
 * 64KB binary image at tests/vendor/6502_functional_tests/bin_files/
 * 6502_functional_test.bin, confirmed directly from that fetched copy's
 * own listing (.lst) file -- not trusted from memory -- to:
 *   - load at address $0000 through $FFFF (file offset == address),
 *   - start execution at $0400 (its "start" label), NOT via a real
 *     reset -- its own reset vector deliberately points at a trap used
 *     to catch spurious resets during the test, so this harness sets
 *     PC directly instead of calling cpu6502_reset(),
 *   - report success by trapping (an infinite self-jump/self-branch
 *     loop, built with report=0's "jmp *"/"bxx *" trap macros) at
 *     $3469 specifically -- verified from the fetched .lst, not a
 *     number copied from secondhand documentation.
 * Any other trap address is a real correctness bug; consult that same
 * .lst file (search for the hex address in its left margin) to see
 * which specific opcode/addressing-mode/flag case was being exercised
 * near it. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../src/bus.h"
#include "../../src/cpu/cpu6502.h"

#define DEFAULT_START_ADDR 0x0400u
#define DEFAULT_SUCCESS_ADDR 0x3469u
#define MAX_CYCLES 400000000ULL

static uint8_t g_mem[65536];

static uint8_t mem_read(void *ctx, uint16_t addr) {
    (void)ctx;
    return g_mem[addr];
}

static void mem_write(void *ctx, uint16_t addr, uint8_t value) {
    (void)ctx;
    g_mem[addr] = value;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <path-to-6502_functional_test.bin> [start_addr_hex] [success_addr_hex]\n", argv[0]);
        return 2;
    }

    uint16_t start_addr = (argc > 2) ? (uint16_t)strtol(argv[2], NULL, 16) : DEFAULT_START_ADDR;
    uint16_t success_addr = (argc > 3) ? (uint16_t)strtol(argv[3], NULL, 16) : DEFAULT_SUCCESS_ADDR;

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", argv[1]);
        return 2;
    }
    size_t n = fread(g_mem, 1, sizeof(g_mem), f);
    fclose(f);
    if (n != sizeof(g_mem)) {
        fprintf(stderr, "warning: expected a flat 64KB image, only read %zu bytes\n", n);
    }

    Bus bus;
    bus.ctx = NULL;
    bus.read = mem_read;
    bus.write = mem_write;

    Cpu6502 cpu;
    cpu6502_init(&cpu, &bus);
    cpu.pc = start_addr;
    cpu.s = 0xFD;
    cpu.p = CPU6502_FLAG_UNUSED | CPU6502_FLAG_I;

    for (;;) {
        uint16_t pc_before = cpu.pc;

        cpu6502_cycle(&cpu); /* opcode fetch */
        while (cpu.mid_instruction) {
            cpu6502_cycle(&cpu);
        }

        if (cpu.illegal_opcode_hit) {
            fprintf(stderr, "FAIL: illegal opcode $%02X hit at $%04X (Phase 10 hasn't implemented it -- "
                             "the functional test suite should never execute one, so this points at a real bug)\n",
                    cpu.last_illegal_opcode, pc_before);
            return 1;
        }

        if (cpu.pc == pc_before) {
            break; /* self-jump/self-branch trap */
        }

        if (cpu.total_cycles > MAX_CYCLES) {
            fprintf(stderr, "FAIL: no trap after %llu cycles (stuck around PC=$%04X)\n",
                    (unsigned long long)cpu.total_cycles, cpu.pc);
            return 1;
        }
    }

    if (cpu.pc == success_addr) {
        printf("PASS: trapped at the documented success address $%04X after %llu cycles\n",
               cpu.pc, (unsigned long long)cpu.total_cycles);
        return 0;
    }

    fprintf(stderr,
            "FAIL: trapped at $%04X, expected success address $%04X.\n"
            "Look up $%04X in tests/vendor/6502_functional_tests/bin_files/6502_functional_test.lst\n"
            "to see which opcode/addressing-mode/flag case this is.\n",
            cpu.pc, success_addr, cpu.pc);
    return 1;
}
