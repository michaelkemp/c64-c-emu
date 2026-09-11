/* Ad-hoc smoke-test tool (see tools/demos/README.md): boots the REAL,
 * user-staged KERNAL/BASIC/Character ROMs (scripts/stage_roms.sh --
 * never fetched/vendored by this project, see CLAUDE.md's license
 * discipline) through a real cpu6502_reset(), wires the VIC-II via
 * c64memory_attach_vic() same as framebuffer_dump.c, and dumps the
 * resulting screen as a PPM image.
 *
 * Unlike framebuffer_dump.c's synthetic hello_c64.s program, this
 * exercises the genuine KERNAL/BASIC cold-start path -- but note that
 * CIA1/CIA2 are still Phase 2's stub (reads 0, ignores writes) and the
 * CPU's IRQ line is never asserted (nothing drives it -- Phase 6's
 * job), so the jiffy clock never ticks and the cursor never blinks.
 * The static boot screen text should still render, since the KERNAL
 * cold-start prints it via direct, synchronous screen writes before
 * ever reaching its interrupt-driven main loop.
 *
 * Usage: real_rom_boot_dump <kernal.rom> <basic.rom> <chargen.rom> <out.ppm> [cycles]
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../src/c64/memory.h"
#include "../../src/c64/palette.h"
#include "../../src/c64/vic_ii.h"
#include "../../src/cpu/cpu6502.h"

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <kernal.rom> <basic.rom> <chargen.rom> <out.ppm> [cycles]\n", argv[0]);
        return 2;
    }
    long cycles = (argc > 5) ? atol(argv[5]) : 3000000L; /* ~3 seconds at 1MHz -- comfortably past cold-start */

    static C64Memory mem;
    static VicII vic;
    c64memory_init(&mem);

    bool ok = c64memory_load_kernal(&mem, argv[1]) && c64memory_load_basic(&mem, argv[2]) &&
              c64memory_load_chargen(&mem, argv[3]);
    if (!ok) {
        fprintf(stderr, "error: failed to load one or more ROMs (wrong path or size)\n");
        return 2;
    }

    vic_ii_init(&vic, mem.ram, mem.char_rom, mem.color_ram);
    vic_ii_set_bank(&vic, 0);
    c64memory_attach_vic(&mem, &vic);

    Bus bus = c64memory_as_bus(&mem);
    Cpu6502 cpu;
    cpu6502_init(&cpu, &bus);
    cpu6502_reset(&cpu); /* the real thing this time: fetches the genuine KERNAL reset vector */
    printf("Reset vector: PC=$%04X\n", cpu.pc);

    for (long i = 0; i < cycles; i++) {
        bool stolen = vic_ii_cycle(&vic);
        if (!stolen) {
            cpu6502_cycle(&cpu);
        }
        if (cpu.illegal_opcode_hit) {
            fprintf(stderr, "warning: illegal opcode $%02X hit at PC~$%04X after %ld cycles\n",
                    cpu.last_illegal_opcode, cpu.pc, i);
            break;
        }
    }

    FILE *out = fopen(argv[4], "wb");
    if (!out) {
        fprintf(stderr, "error: could not create '%s'\n", argv[4]);
        return 2;
    }
    fprintf(out, "P6\n%u %u\n255\n", VIC_X_MODULUS, VIC_LINES_PER_FRAME);
    for (unsigned y = 0; y < VIC_LINES_PER_FRAME; y++) {
        for (unsigned x = 0; x < VIC_X_MODULUS; x++) {
            uint8_t color_index = vic.framebuffer[y][x] & 0x0Fu;
            fwrite(VIC_PALETTE_RGB[color_index], 1, 3, out);
        }
    }
    fclose(out);
    printf("Wrote %s after %ld cycles, final PC=$%04X\n", argv[4], cycles, cpu.pc);
    return 0;
}
