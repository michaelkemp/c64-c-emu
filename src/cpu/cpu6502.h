#ifndef C64EMU_CPU6502_H
#define C64EMU_CPU6502_H

#include <stdbool.h>
#include <stdint.h>

#include "../bus.h"

#define CPU6502_FLAG_C 0x01u
#define CPU6502_FLAG_Z 0x02u
#define CPU6502_FLAG_I 0x04u
#define CPU6502_FLAG_D 0x08u
#define CPU6502_FLAG_B 0x10u /* not a real stored flag -- only meaningful as a pushed bit, see cpu6502.c */
#define CPU6502_FLAG_UNUSED 0x20u /* always reads as 1, there is no real bit-5 flag */
#define CPU6502_FLAG_V 0x40u
#define CPU6502_FLAG_N 0x80u

/* A plain, chip-agnostic 6502/6510 core. It knows nothing about the C64
 * memory map, bank-switching, or any other chip -- it only ever calls
 * bus_read8()/bus_write8() on the Bus it's given. This is what makes it
 * directly reusable, unmodified, for the 1541's own 6502 in Phase 9b.
 * See docs/6502-reference.md. */
typedef struct Cpu6502 {
    uint8_t a, x, y, s, p;
    uint16_t pc;
    Bus *bus;

    uint64_t total_cycles;

    /* Interrupt lines -- caller (Phase 6's main loop, or a test) drives
     * these directly. IRQ is level-triggered (OR of every real IRQ
     * source lives outside this module); NMI is edge-triggered. */
    bool irq_line;
    bool nmi_line;
    bool nmi_line_prev;
    bool nmi_pending;

    /* --- cycle-stepping state, internal --- */
    bool mid_instruction;
    uint8_t opcode;
    int step;
    bool servicing_interrupt; /* running the BRK-shaped sequence for a real IRQ/NMI, not a BRK instruction */
    bool servicing_nmi;       /* which vector to use when servicing_interrupt is true */
    bool i_flag_before_instruction; /* see cpu6502.c: this is what gives CLI/SEI/PLP their real one-instruction IRQ-polling delay */

    uint16_t addr;
    uint16_t addr_base;
    uint16_t ptr;
    uint8_t val;
    bool page_crossed;

    /* Diagnostics -- Phase 10 defers real undocumented-opcode behavior;
     * until then, hitting one sets these instead of crashing or silently
     * behaving like a NOP so a caller can detect and report it. */
    bool illegal_opcode_hit;
    uint8_t last_illegal_opcode;
} Cpu6502;

void cpu6502_init(Cpu6502 *cpu, Bus *bus);

/* Real 6502 reset spends 7 cycles doing dummy stack reads before
 * fetching the reset vector -- none of it is software-observable (R/W
 * stays high throughout), so this is modeled as an immediate state
 * change rather than 7 stepped cycles. Disclosed simplification. */
void cpu6502_reset(Cpu6502 *cpu);

/* Execute exactly one PHI2 cycle of the current instruction (or of
 * pending interrupt service, or of the next opcode fetch), then return.
 * This is the interface Phase 4 (scanline-accurate VIC-II) and Phase 6
 * (cycle-interleaved main loop) need -- see docs/machine.md. */
void cpu6502_cycle(Cpu6502 *cpu);

void cpu6502_set_irq_line(Cpu6502 *cpu, bool asserted);
void cpu6502_set_nmi_line(Cpu6502 *cpu, bool asserted);

#endif /* C64EMU_CPU6502_H */
