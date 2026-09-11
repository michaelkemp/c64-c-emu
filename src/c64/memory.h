#ifndef C64EMU_C64_MEMORY_H
#define C64EMU_C64_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../bus.h"
#include "cia.h"
#include "sid.h"
#include "vic_ii.h"

/* The real C64 address space: RAM everywhere underneath, with the
 * BASIC/KERNAL/Character ROM views and I/O space switched in on top of
 * it depending on the 6510's $00/$01 I/O port (LORAM/HIRAM/CHAREN) --
 * see docs/memory-map.md for the full truth table and reasoning. This
 * is "just another Bus implementation" from the CPU core's point of
 * view (src/cpu/cpu6502.h) -- the core itself needed zero changes. */
typedef struct C64Memory {
    uint8_t ram[65536]; /* the real backing store everywhere, always written through */

    uint8_t kernal_rom[8192]; /* $E000-$FFFF when HIRAM=1 */
    uint8_t basic_rom[8192];  /* $A000-$BFFF when LORAM=1 && HIRAM=1 */
    uint8_t char_rom[4096];   /* $D000-$DFFF when CHAREN=0 */
    bool kernal_loaded;
    bool basic_loaded;
    bool char_loaded;

    /* $D800-$DBFF: only the low nibble of each byte is wired to real
     * hardware -- see docs/memory-map.md's Known Gaps. */
    uint8_t color_ram[1024];

    /* The 6510's built-in I/O port. $00 = data direction register
     * (1 bit = that pin is an output), $01 = the output data latch.
     * Bits 0-2 are LORAM/HIRAM/CHAREN; the rest (datasette control/
     * sense) aren't modeled yet -- see Known Gaps. */
    uint8_t cpu_port_ddr;  /* $00 */
    uint8_t cpu_port_data; /* $01 */

    /* When attached (c64memory_attach_vic()), $D000-$D3FF register
     * accesses (while I/O is switched in) dispatch to this real VicII
     * instance -- including the real 64-byte mirroring -- instead of
     * the Phase 2 stub. NULL means "no VIC-II yet", preserving the
     * original stub behavior unchanged (existing Phase 2/3 tests don't
     * attach one). This is a deliberately early, partial slice of
     * Phase 6's real job ("wire CPU + Bus + chips together") -- see
     * CLAUDE.md's status section and tools/demos/. */
    VicII *vic;

    /* Same pattern as `vic`: when attached, $DC00-$DCFF/$DD00-$DDFF/
     * $D400-$D7FF (while I/O is switched in) dispatch to these real
     * chip instances, including their real register mirroring, instead
     * of the Phase 2 stub. NULL preserves the original stub. */
    Cia *cia1;
    Cia *cia2;
    Sid *sid;
} C64Memory;

void c64memory_init(C64Memory *mem);

void c64memory_attach_vic(C64Memory *mem, VicII *vic);
void c64memory_attach_cia1(C64Memory *mem, Cia *cia1);
void c64memory_attach_cia2(C64Memory *mem, Cia *cia2);
void c64memory_attach_sid(C64Memory *mem, Sid *sid);

/* Load a real ROM dump the user staged via scripts/stage_roms.sh into
 * gitignored roms/c64/ -- this project never fetches or vendors these
 * itself (see CLAUDE.md's license discipline). Returns false (and
 * leaves the corresponding *_loaded flag false) if the file is missing
 * or the wrong size; callers should treat that as "ROM not staged",
 * not necessarily a fatal error. */
bool c64memory_load_kernal(C64Memory *mem, const char *path);
bool c64memory_load_basic(C64Memory *mem, const char *path);
bool c64memory_load_chargen(C64Memory *mem, const char *path);

/* The Bus-compatible read/write pair, and a convenience wrapper that
 * builds a Bus pointing at them. */
uint8_t c64memory_cpu_read(void *ctx, uint16_t addr);
void c64memory_cpu_write(void *ctx, uint16_t addr, uint8_t value);
Bus c64memory_as_bus(C64Memory *mem);

/* Exposed for tests/diagnostics -- computes the effective 8-bit value
 * of $01 given the current DDR: output bits reflect what was written,
 * input bits float high (see memory.c and docs/memory-map.md's Known
 * Gaps -- this is what makes LORAM=HIRAM=CHAREN=1 the real default
 * before any code has run, which is load-bearing for fetching the
 * reset vector through KERNAL ROM in the first place). */
uint8_t c64memory_effective_port(const C64Memory *mem);

#endif /* C64EMU_C64_MEMORY_H */
