/* Phase 2's real verification target (docs/memory-map.md): with real
 * staged ROMs, the CPU must reach the genuine KERNAL reset routine and
 * execute real ROM code -- not just "doesn't crash".
 *
 * This is tier 3 of docs/testing-strategy.md and therefore needs the
 * user's own legally-acquired ROM dumps staged via scripts/
 * stage_roms.sh first (never fetched/vendored by this project -- see
 * CLAUDE.md's license discipline). If they aren't staged, this prints
 * a clear message and exits 0 rather than failing the build -- the
 * base test suite (`make unit-test`, `make dormann`) must keep working
 * with zero ROMs staged, and this target is intentionally the only one
 * that isn't part of that base suite. */

#include <stdio.h>
#include <string.h>

#include "../../src/c64/memory.h"
#include "../../src/cpu/cpu6502.h"

/* "READY." in default (unshifted) C64 screen codes: A-Z map to 1-26
 * (screen_code = ascii - 64), digits/most punctuation are unchanged --
 * a well-established, extremely widely-documented PETSCII/screen-code
 * fact, not specific to this ROM dump. */
static const uint8_t READY_SCREEN_CODES[] = {
    'R' - 64, 'E' - 64, 'A' - 64, 'D' - 64, 'Y' - 64, '.'};

int main(void) {
    C64Memory mem;
    c64memory_init(&mem);

    bool have_kernal = c64memory_load_kernal(&mem, "roms/c64/kernal.rom");
    bool have_basic = c64memory_load_basic(&mem, "roms/c64/basic.rom");
    bool have_chargen = c64memory_load_chargen(&mem, "roms/c64/chargen.rom");

    if (!have_kernal || !have_basic || !have_chargen) {
        printf("SKIP: real C64 ROMs not staged under roms/c64/ -- run scripts/stage_roms.sh "
               "with your own legally-acquired dumps first (kernal=%s basic=%s chargen=%s)\n",
               have_kernal ? "ok" : "missing", have_basic ? "ok" : "missing",
               have_chargen ? "ok" : "missing");
        return 0;
    }

    Bus bus = c64memory_as_bus(&mem);
    Cpu6502 cpu;
    cpu6502_init(&cpu, &bus);
    cpu6502_reset(&cpu); /* real vector fetch at $FFFC/$FFFD, reading through to KERNAL ROM */

    printf("Reset vector: PC=$%04X\n", cpu.pc);

    /* Run real elapsed PHI2 cycles (not just "some instructions") --
     * comfortably enough to get through the KERNAL's own cold-start
     * sequence and BASIC's cold-start into its keyboard-wait loop.
     * CIA1/CIA2 are still Phase 2's stub and the CPU's IRQ line is
     * never asserted (Phase 6's job), so the jiffy clock never ticks
     * and the cursor never blinks -- but the boot screen text is
     * printed via direct, synchronous KERNAL/BASIC writes before any
     * of that would matter. */
    const long cycles_to_run = 3000000L;
    bool crashed = false;
    for (long i = 0; i < cycles_to_run; i++) {
        cpu6502_cycle(&cpu);
        if (cpu.illegal_opcode_hit) {
            printf("FAIL: illegal opcode $%02X hit at PC=$%04X after %ld cycles -- "
                   "PC likely wandered into garbage\n",
                   cpu.last_illegal_opcode, cpu.pc, i);
            crashed = true;
            break;
        }
    }
    if (crashed) {
        return 1;
    }

    /* The real, concrete, behavioral check per docs/memory-map.md:
     * not just "it doesn't crash", but that the genuine KERNAL/BASIC
     * cold-start actually reached its normal "READY." prompt -- found
     * by scanning the whole 1000-byte video matrix ($0400-$07E7) for
     * the "READY." screen-code sequence, rather than assuming its
     * exact on-screen row/column position. */
    bool found_ready = false;
    for (int pos = 0; pos <= 1000 - (int)sizeof(READY_SCREEN_CODES); pos++) {
        if (memcmp(&mem.ram[0x0400 + pos], READY_SCREEN_CODES, sizeof(READY_SCREEN_CODES)) == 0) {
            found_ready = true;
            break;
        }
    }

    if (!found_ready) {
        printf("FAIL: ran %ld cycles (PC now $%04X) but never found \"READY.\" in screen memory --"
               " the KERNAL/BASIC cold-start did not reach its normal prompt.\n",
               cycles_to_run, cpu.pc);
        return 1;
    }

    printf("PASS: genuine KERNAL/BASIC cold-start reached its real \"READY.\" prompt "
           "(confirmed via actual screen memory content, not just address tracing) "
           "after %ld cycles. Final PC=$%04X.\n",
           cycles_to_run, cpu.pc);
    return 0;
}
