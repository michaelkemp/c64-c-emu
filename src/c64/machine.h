#ifndef C64EMU_C64_MACHINE_H
#define C64EMU_C64_MACHINE_H

#include <stdbool.h>
#include <stdint.h>

#include "cia.h"
#include "keyboard.h"
#include "memory.h"
#include "sid.h"
#include "vic_ii.h"
#include "../bus.h"
#include "../cpu/cpu6502.h"

/* Phase 6: "where Phases 1-5's separately-built pieces get driven
 * together as one real machine" -- see docs/machine.md. Machine owns
 * every chip and wires them exactly as docs/machine.md's cycle-
 * interleaving sketch describes: the VIC-II steps first and may claim
 * the bus; the CPU only steps if it didn't; both CIAs and the SID
 * advance every cycle; IRQ (level, OR of both CIA1 and the VIC-II
 * raster interrupt) and NMI (edge, from CIA2) are re-evaluated every
 * cycle; the keyboard matrix/joysticks and the VIC-II's own bank
 * (driven by CIA2 Port A) are kept in sync every cycle too. */

/* The real PAL PHI2 clock, derived from the same 17.734472MHz crystal
 * (4x the PAL colorburst frequency) real hardware divides by 18 to
 * produce it -- a standard, widely-corroborated hardware fact (cross-
 * checks exactly against docs/vic-ii.md's independently-stated
 * ~50.1245Hz frame rate: C64_PAL_PHI2_HZ / 19656 cycles-per-frame). Use
 * this exact value anywhere real-world timing matters, never a rounded
 * "985248" or "1000000" -- see docs/references-and-gotchas.md. */
#define C64_PAL_MASTER_CLOCK_HZ 17734472.0
#define C64_PAL_PHI2_HZ (C64_PAL_MASTER_CLOCK_HZ / 18.0)

typedef struct Machine {
    Cpu6502 cpu;
    Bus bus; /* Cpu6502 only stores a pointer to its Bus, so this must outlive machine_init() -- lives here, not on the stack. */
    C64Memory mem;
    Cia cia1;
    Cia cia2;
    VicII vic;
    Sid sid;

    KeyboardMatrix keyboard;
    Joystick joystick1; /* CIA1 Port B */
    Joystick joystick2; /* CIA1 Port A -- shares the port with keyboard column select */

    /* RESTORE isn't a matrix position on real hardware (see
     * src/c64/keyboard.h) -- it wires directly into CIA2's NMI path
     * instead, so it lives here rather than in KeyboardMatrix. Set via
     * machine_set_restore_key(); ORed into the real NMI line every
     * cycle alongside CIA2's own IRQ output (see docs/machine.md). */
    bool restore_key_down;

    uint64_t total_cycles;
} Machine;

void machine_init(Machine *m);

/* Thin wrappers over C64Memory's own loaders -- see its docs for the
 * license-discipline rules (never fetched/vendored by this project). */
bool machine_load_kernal(Machine *m, const char *path);
bool machine_load_basic(Machine *m, const char *path);
bool machine_load_chargen(Machine *m, const char *path);

/* Real CPU reset: fetches the genuine reset vector through whatever
 * ROMs are loaded (or through RAM, reading zeroes, if none are). */
void machine_reset(Machine *m);

/* Advances exactly one real PHI2 cycle -- the interleaving described in
 * docs/machine.md's "Cycle interleaving" section. */
void machine_cycle(Machine *m);

/* RESTORE key state -- see the Machine struct's own comment above. A
 * frontend (Phase 7) should call this directly rather than routing
 * RESTORE through keyboard_matrix_set_key(), since real hardware
 * itself doesn't route it through the matrix either. */
void machine_set_restore_key(Machine *m, bool down);

static inline void machine_run_cycles(Machine *m, uint64_t cycles) {
    for (uint64_t i = 0; i < cycles; i++) {
        machine_cycle(m);
    }
}

/* Real-time pacing (docs/machine.md's "Real-time pacing" section,
 * strategy 1: host high-resolution timer as master clock -- the
 * simplest option, and the right one before Phase 7's audio device
 * exists to drive strategy 2 instead). The cycle-budget arithmetic
 * itself is a pure, trivially-testable function; machine_run_realtime()
 * is the thin (harder-to-unit-test, host-clock-dependent) wrapper that
 * actually sleeps. */
uint64_t machine_target_cycles_for_elapsed(double elapsed_seconds);

/* Runs the machine for approximately `seconds` of real wall-clock time,
 * executing cycles as needed to keep pace with a monotonic host clock
 * and sleeping briefly when running ahead of schedule (never spinning a
 * whole host CPU core). Blocking; returns once `seconds` have elapsed. */
void machine_run_realtime(Machine *m, double seconds);

#endif /* C64EMU_C64_MACHINE_H */
