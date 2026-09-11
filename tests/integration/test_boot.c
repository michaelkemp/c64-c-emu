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

#include "../../src/c64/memory.h"
#include "../../src/cpu/cpu6502.h"

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

    /* Trace the first several thousand instructions and report the
     * distinct addresses visited -- "it doesn't crash" isn't sufficient
     * per docs/memory-map.md, but this project also doesn't hardcode
     * specific real KERNAL disassembly addresses from memory (that's
     * exactly the kind of unverified-secondhand-fact the project's own
     * methodology warns against -- see docs/references-and-gotchas.md).
     * A human (or a future pass that reads the real, disassembled ROM
     * this session doesn't have access to) should confirm the addresses
     * printed here correspond to the genuine KERNAL reset routine
     * (RAM test, I/O init, screen init, cold-start into BASIC). */
    const int instructions_to_trace = 5000;
    for (int i = 0; i < instructions_to_trace; i++) {
        if (cpu.illegal_opcode_hit) {
            printf("FAIL: illegal opcode $%02X hit at PC=$%04X after %d instructions -- "
                   "PC likely wandered into garbage\n",
                   cpu.last_illegal_opcode, cpu.pc, i);
            return 1;
        }
        cpu6502_cycle(&cpu);
        while (cpu.mid_instruction) {
            cpu6502_cycle(&cpu);
        }
    }

    printf("PASS (weak): executed %d real KERNAL instructions with no illegal opcode and no crash. "
           "PC is now $%04X. This does NOT yet confirm the traced addresses match the genuine "
           "KERNAL reset routine -- see this file's own comment.\n",
           instructions_to_trace, cpu.pc);
    return 0;
}
