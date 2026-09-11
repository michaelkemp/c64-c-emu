/* Ad-hoc smoke-test tool: loads a small, self-contained raw 6502
 * binary (no KERNAL/BASIC ROM needed -- see tools/demos/hello_c64.s),
 * wires the CPU and VIC-II together over a real C64Memory bus (Phase 2
 * memory map + Phase 4 VIC-II, using c64memory_attach_vic() -- a
 * deliberately early, partial slice of Phase 6's real job), runs it
 * for a few frames, and dumps the VIC-II's framebuffer as a PPM image.
 *
 * This exists purely so a human (or Claude, which can also read the
 * resulting image) can *see* the VIC-II's actual output before Phase 6
 * (real machine loop) and Phase 7 (SDL2 peripherals) are built for
 * real -- see CLAUDE.md's status section. It is NOT a permanent
 * project deliverable and doesn't wire the CIAs, SID, or real IRQ/NMI
 * delivery -- the demo program it runs is written to not need any of
 * that (see hello_c64.s's own comments).
 *
 * Usage: framebuffer_dump <program.bin> <load_addr_hex> <out.ppm> [frames]
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../src/c64/cia.h"
#include "../../src/c64/memory.h"
#include "../../src/c64/palette.h"
#include "../../src/c64/vic_ii.h"
#include "../../src/cpu/cpu6502.h"

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <program.bin> <load_addr_hex> <out.ppm> [frames]\n", argv[0]);
        return 2;
    }

    uint16_t load_addr = (uint16_t)strtol(argv[2], NULL, 16);
    const char *out_path = argv[3];
    int frames = (argc > 4) ? atoi(argv[4]) : 3;

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "error: could not open '%s'\n", argv[1]);
        return 2;
    }

    static C64Memory mem;
    static VicII vic;
    c64memory_init(&mem);

    size_t n = fread(mem.ram + load_addr, 1, sizeof(mem.ram) - load_addr, f);
    fclose(f);
    printf("Loaded %zu bytes at $%04X\n", n, load_addr);

    vic_ii_init(&vic, mem.ram, mem.char_rom, mem.color_ram);
    vic_ii_set_bank(&vic, 0); /* bank 0, $0000-$3FFF -- matches hello_c64.s's addresses */
    c64memory_attach_vic(&mem, &vic);

    Bus bus = c64memory_as_bus(&mem);
    Cpu6502 cpu;
    cpu6502_init(&cpu, &bus);
    /* No ROMs exist, so no real reset vector to fetch -- start execution
     * directly at the demo's own entry point instead of calling
     * cpu6502_reset(). This sidesteps Phase 2's ROM dependency entirely,
     * which is the whole point of this tool. */
    cpu.pc = load_addr;
    cpu.s = 0xFD;
    cpu.p = CPU6502_FLAG_UNUSED | CPU6502_FLAG_I;

    /* Cycle-interleaved, matching docs/machine.md's shape: the VIC-II
     * steps first and may claim the bus; the CPU only steps if it
     * didn't. No CIAs/SID/interrupts are wired up yet -- hello_c64.s
     * is written to not need any of that. */
    long total_cycles = (long)frames * VIC_CYCLES_PER_LINE * VIC_LINES_PER_FRAME;
    for (long i = 0; i < total_cycles; i++) {
        bool stolen = vic_ii_cycle(&vic);
        if (!stolen) {
            cpu6502_cycle(&cpu);
        }
    }

    if (cpu.illegal_opcode_hit) {
        fprintf(stderr, "warning: illegal opcode $%02X hit at some point (last known PC $%04X)\n",
                cpu.last_illegal_opcode, cpu.pc);
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        fprintf(stderr, "error: could not create '%s'\n", out_path);
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
    printf("Wrote %s (%ux%u) after %d frame(s), final PC=$%04X, raster=$%03X\n",
           out_path, VIC_X_MODULUS, VIC_LINES_PER_FRAME, frames, cpu.pc, vic.raster_line);
    return 0;
}
