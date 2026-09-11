/* Ad-hoc smoke-test tool (see tools/demos/README.md): boots the REAL,
 * user-staged KERNAL/BASIC/Character ROMs (scripts/stage_roms.sh --
 * never fetched/vendored by this project, see CLAUDE.md's license
 * discipline) through the full Phase 6 Machine -- real CPU+CIA1+CIA2+
 * VIC-II+SID cycle interleaving, with genuine IRQ/NMI delivery, not
 * just the CPU+VIC-II pair framebuffer_dump.c and the original version
 * of this file used. This means the jiffy clock and cursor blink are
 * now driven by real CIA1 timer interrupts, exactly like real hardware.
 *
 * Usage: real_rom_boot_dump <kernal.rom> <basic.rom> <chargen.rom> <out.ppm> [cycles]
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../src/c64/machine.h"
#include "../../src/c64/palette.h"

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <kernal.rom> <basic.rom> <chargen.rom> <out.ppm> [cycles]\n", argv[0]);
        return 2;
    }
    long cycles = (argc > 5) ? atol(argv[5]) : 3000000L; /* ~3 seconds at the real PAL clock -- comfortably past cold-start */

    static Machine m;
    machine_init(&m);

    bool ok = machine_load_kernal(&m, argv[1]) && machine_load_basic(&m, argv[2]) &&
              machine_load_chargen(&m, argv[3]);
    if (!ok) {
        fprintf(stderr, "error: failed to load one or more ROMs (wrong path or size)\n");
        return 2;
    }

    machine_reset(&m);
    printf("Reset vector: PC=$%04X\n", m.cpu.pc);

    for (long i = 0; i < cycles; i++) {
        machine_cycle(&m);
        if (m.cpu.illegal_opcode_hit) {
            fprintf(stderr, "warning: illegal opcode $%02X hit at PC~$%04X after %ld cycles\n",
                    m.cpu.last_illegal_opcode, m.cpu.pc, i);
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
            uint8_t color_index = m.vic.framebuffer[y][x] & 0x0Fu;
            fwrite(VIC_PALETTE_RGB[color_index], 1, 3, out);
        }
    }
    fclose(out);
    printf("Wrote %s after %ld cycles (%llu total machine cycles), final PC=$%04X\n", argv[4], cycles,
           (unsigned long long)m.total_cycles, m.cpu.pc);
    return 0;
}
