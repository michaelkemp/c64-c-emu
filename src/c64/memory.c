/* The C64 PLA-driven memory map. See docs/memory-map.md for the truth
 * table and reasoning this implements, and this file's own comments
 * for the specific disclosed simplifications (I/O register mirroring,
 * Color RAM's undefined high nibble, open-bus reads, unimplemented-
 * chip stubs) called out in that doc's Known Gaps section. */

#include "memory.h"

#include <stdio.h>
#include <string.h>

#define CPU_PORT_LORAM 0x01u
#define CPU_PORT_HIRAM 0x02u
#define CPU_PORT_CHAREN 0x04u

void c64memory_init(C64Memory *mem) {
    memset(mem->ram, 0, sizeof(mem->ram));
    memset(mem->kernal_rom, 0, sizeof(mem->kernal_rom));
    memset(mem->basic_rom, 0, sizeof(mem->basic_rom));
    memset(mem->char_rom, 0, sizeof(mem->char_rom));
    memset(mem->color_ram, 0, sizeof(mem->color_ram));
    mem->kernal_loaded = false;
    mem->basic_loaded = false;
    mem->char_loaded = false;

    /* Real 6510 post-reset state: DDR ($00) is all-zero, i.e. every pin
     * starts as an input. Combined with c64memory_effective_port()'s
     * "unconnected input pins float high" rule below, this correctly
     * gives LORAM=HIRAM=CHAREN=1 (BASIC+KERNAL+I/O visible) before any
     * code has run at all -- necessary for the CPU's very first act,
     * fetching the reset vector, to read through to real KERNAL ROM. */
    mem->cpu_port_ddr = 0x00;
    mem->cpu_port_data = 0x00;
    mem->vic = NULL;
}

void c64memory_attach_vic(C64Memory *mem, VicII *vic) {
    mem->vic = vic;
}

static bool load_rom_file(uint8_t *dest, size_t expected_size, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    size_t n = fread(dest, 1, expected_size, f);
    fclose(f);
    return n == expected_size;
}

bool c64memory_load_kernal(C64Memory *mem, const char *path) {
    mem->kernal_loaded = load_rom_file(mem->kernal_rom, sizeof(mem->kernal_rom), path);
    return mem->kernal_loaded;
}

bool c64memory_load_basic(C64Memory *mem, const char *path) {
    mem->basic_loaded = load_rom_file(mem->basic_rom, sizeof(mem->basic_rom), path);
    return mem->basic_loaded;
}

bool c64memory_load_chargen(C64Memory *mem, const char *path) {
    mem->char_loaded = load_rom_file(mem->char_rom, sizeof(mem->char_rom), path);
    return mem->char_loaded;
}

/* Unconnected (input) 6510 port pins float high on real hardware --
 * this is what makes BASIC/KERNAL/I/O the default visible view even
 * before the KERNAL's own init code has written anything to $00/$01.
 * Disclosed simplification: real bit 4 (cassette sense) reflects an
 * actual physical line with no datasette modeled yet, so it floats
 * high (== "no cassette button pressed") like every other input bit
 * here rather than tracking real datasette state -- see Known Gaps. */
uint8_t c64memory_effective_port(const C64Memory *mem) {
    return (uint8_t)((mem->cpu_port_data & mem->cpu_port_ddr) | (uint8_t)~mem->cpu_port_ddr);
}

/* $D000-$DFFF sub-map, active only while CHAREN=1 switches I/O in over
 * Character ROM. VIC-II/SID/CIA1/CIA2 aren't implemented yet (Phases
 * 3-5) -- per docs/memory-map.md, stubbed as "reads return 0, writes
 * are ignored" until each chip's own phase lands, which is enough for
 * KERNAL init code that pokes them during boot to not crash the bus
 * dispatch. Register mirroring within each chip's own window (VIC-II
 * every 64 bytes, SID every 32, CIA every 16) is deferred to that
 * chip's own phase/doc, which frame it as their decision to make. */
static uint8_t io_read(C64Memory *mem, uint16_t addr) {
    if (addr <= 0xD3FFu) {
        /* VIC-II registers repeat every 64 bytes across this 1KB
         * window -- confirmed directly from Bauer's article (see
         * docs/sources.md): "register 0 appears on addresses $d000,
         * $d040, $d080 etc." Falls back to the pre-Phase-4 stub (reads
         * 0) if no VicII has been attached yet -- see
         * c64memory_attach_vic(). */
        if (mem->vic != NULL) {
            return vic_ii_reg_read(mem->vic, (uint8_t)((addr - 0xD000u) & 0x3Fu));
        }
        return 0;
    }
    if (addr <= 0xD7FFu) {
        return 0; /* SID -- Phase 5 */
    }
    if (addr <= 0xDBFFu) {
        /* Color RAM: only the low nibble is real; the high nibble is
         * undefined on real hardware. Disclosed simplification (see
         * docs/memory-map.md): we just return it as 0 rather than
         * modeling open-bus behavior. */
        return mem->color_ram[addr - 0xD800u] & 0x0Fu;
    }
    if (addr <= 0xDCFFu) {
        return 0; /* CIA 1 -- Phase 3 */
    }
    if (addr <= 0xDDFFu) {
        return 0; /* CIA 2 -- Phase 3 */
    }
    return 0; /* $DE00-$DFFF: cartridge I/O areas 1/2, unused for a generic setup -- Phase 8 */
}

static void io_write(C64Memory *mem, uint16_t addr, uint8_t value) {
    if (addr <= 0xD3FFu) {
        if (mem->vic != NULL) {
            vic_ii_reg_write(mem->vic, (uint8_t)((addr - 0xD000u) & 0x3Fu), value);
        }
        return;
    }
    if (addr >= 0xD800u && addr <= 0xDBFFu) {
        mem->color_ram[addr - 0xD800u] = value & 0x0Fu;
        return;
    }
    /* Every other I/O register is unimplemented until its own phase --
     * ignore the write rather than crashing the bus dispatch. */
}

uint8_t c64memory_cpu_read(void *ctx, uint16_t addr) {
    C64Memory *mem = (C64Memory *)ctx;

    if (addr == 0x0000u) {
        return mem->cpu_port_ddr;
    }
    if (addr == 0x0001u) {
        return c64memory_effective_port(mem);
    }

    if (addr >= 0xA000u && addr <= 0xBFFFu) {
        uint8_t port = c64memory_effective_port(mem);
        bool loram = (port & CPU_PORT_LORAM) != 0;
        bool hiram = (port & CPU_PORT_HIRAM) != 0;
        if (loram && hiram) {
            return mem->basic_rom[addr - 0xA000u];
        }
        return mem->ram[addr];
    }

    if (addr >= 0xD000u && addr <= 0xDFFFu) {
        uint8_t port = c64memory_effective_port(mem);
        bool charen = (port & CPU_PORT_CHAREN) != 0;
        if (charen) {
            return io_read(mem, addr);
        }
        return mem->char_rom[addr - 0xD000u];
    }

    if (addr >= 0xE000u) {
        uint8_t port = c64memory_effective_port(mem);
        bool hiram = (port & CPU_PORT_HIRAM) != 0;
        if (hiram) {
            return mem->kernal_rom[addr - 0xE000u];
        }
        return mem->ram[addr];
    }

    /* $0002-$9FFF and $C000-$CFFF: always plain RAM, no bank-switching
     * input affects these ranges on the base machine (Phase 8 extends
     * the truth table with cartridge EXROM/GAME, not this file). */
    return mem->ram[addr];
}

void c64memory_cpu_write(void *ctx, uint16_t addr, uint8_t value) {
    C64Memory *mem = (C64Memory *)ctx;

    if (addr == 0x0000u) {
        mem->cpu_port_ddr = value;
        return;
    }
    if (addr == 0x0001u) {
        mem->cpu_port_data = value;
        return;
    }

    if (addr >= 0xD000u && addr <= 0xDFFFu) {
        uint8_t port = c64memory_effective_port(mem);
        bool charen = (port & CPU_PORT_CHAREN) != 0;
        if (charen) {
            /* Real hardware: I/O being switched in here disables the
             * RAM chip-select entirely, so the write does NOT land in
             * the RAM underneath -- unlike the ROM-shadow case below.
             * See docs/memory-map.md. */
            io_write(mem, addr, value);
            return;
        }
        mem->ram[addr] = value; /* Character ROM switched in: write-through to RAM, same as any ROM view */
        return;
    }

    /* $A000-$BFFF and $E000-$FFFF (BASIC/KERNAL ROM views), and every
     * always-RAM range: the underlying RAM always exists and is always
     * written through, regardless of which ROM view is currently
     * switched in for reads. See docs/memory-map.md. */
    mem->ram[addr] = value;
}

Bus c64memory_as_bus(C64Memory *mem) {
    Bus bus;
    bus.ctx = mem;
    bus.read = c64memory_cpu_read;
    bus.write = c64memory_cpu_write;
    return bus;
}
