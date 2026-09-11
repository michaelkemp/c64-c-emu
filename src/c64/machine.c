/* Phase 6: the real machine. See machine.h and docs/machine.md. */

/* Needed for clock_gettime()/CLOCK_MONOTONIC/nanosleep() under -std=c11
 * (strict ISO C11 alone doesn't declare the POSIX.1b real-time
 * extensions this file's pacing code uses). */
#define _POSIX_C_SOURCE 199309L

#include "machine.h"

#include <time.h>

void machine_init(Machine *m) {
    c64memory_init(&m->mem);
    m->bus = c64memory_as_bus(&m->mem);
    cpu6502_init(&m->cpu, &m->bus);

    /* TOD's own tenth-of-a-second period, in real PHI2 cycles -- an
     * integer cycle count derived once from the exact PAL clock, per
     * docs/cia.md's "use integer arithmetic" rule (cia_tick() itself
     * does the actual integer accumulation; this is just the constant
     * it accumulates against). */
    uint32_t cycles_per_tenth = (uint32_t)(C64_PAL_PHI2_HZ / 10.0 + 0.5);
    cia_init(&m->cia1, cycles_per_tenth);
    cia_init(&m->cia2, cycles_per_tenth);

    vic_ii_init(&m->vic, m->mem.ram, m->mem.char_rom, m->mem.color_ram);
    vic_ii_set_bank(&m->vic, 0);

    sid_init(&m->sid);

    keyboard_matrix_init(&m->keyboard);
    joystick_init(&m->joystick1);
    joystick_init(&m->joystick2);

    c64memory_attach_vic(&m->mem, &m->vic);
    c64memory_attach_cia1(&m->mem, &m->cia1);
    c64memory_attach_cia2(&m->mem, &m->cia2);
    c64memory_attach_sid(&m->mem, &m->sid);

    m->total_cycles = 0;
}

bool machine_load_kernal(Machine *m, const char *path) { return c64memory_load_kernal(&m->mem, path); }
bool machine_load_basic(Machine *m, const char *path) { return c64memory_load_basic(&m->mem, path); }
bool machine_load_chargen(Machine *m, const char *path) { return c64memory_load_chargen(&m->mem, path); }

void machine_reset(Machine *m) {
    /* Real hardware: the system RES line fans out to the CPU, both
     * CIAs, and the SID (which documents its own RES pin) -- the
     * VIC-II has no RES pin of its own and is deliberately left alone,
     * matching real behavior (its raster position free-runs across a
     * CPU reset). */
    uint32_t cycles_per_tenth = m->cia1.cycles_per_tenth;
    cia_init(&m->cia1, cycles_per_tenth);
    cia_init(&m->cia2, cycles_per_tenth);
    sid_init(&m->sid);
    cpu6502_reset(&m->cpu);
}

void machine_cycle(Machine *m) {
    /* Keyboard/joystick -> CIA1 port pulldowns. Recomputed every cycle
     * since the KERNAL can change which columns are selected on Port A
     * between any two Port B reads -- see docs/cia.md. Port A's own
     * pulldown (joystick 2) is applied first so the column-select value
     * the keyboard matrix reacts to already reflects it, matching real
     * open-collector-wired-AND board behavior. */
    cia_set_port_a_pulldown(&m->cia1, joystick_pulldown(&m->joystick2));
    uint8_t effective_a = cia_effective_port_a(&m->cia1);
    uint8_t kb_pulldown = keyboard_matrix_sense_pulldown(&m->keyboard, effective_a);
    cia_set_port_b_pulldown(&m->cia1, (uint8_t)(kb_pulldown | joystick_pulldown(&m->joystick1)));

    /* VIC-II bank: CIA2 Port A bits 0-1, inverted (00=bank3 ... 11=bank0) -- see docs/vic-ii.md. */
    uint8_t cia2_port_a = cia_effective_port_a(&m->cia2);
    vic_ii_set_bank(&m->vic, (uint8_t)(3u - (cia2_port_a & 0x03u)));

    bool bus_stolen = vic_ii_cycle(&m->vic);
    if (!bus_stolen) {
        cpu6502_cycle(&m->cpu);
    }

    cia_tick(&m->cia1, 1);
    cia_tick(&m->cia2, 1);
    sid_tick(&m->sid, 1);

    /* IRQ is level-triggered: OR of CIA1 and the VIC-II raster
     * interrupt (docs/machine.md). NMI is edge-triggered, from CIA2;
     * the CPU core's own nmi_line/nmi_pending logic (src/cpu/
     * cpu6502.c) does the actual edge-detection -- this just supplies
     * the current level every cycle, same as any other line. */
    cpu6502_set_irq_line(&m->cpu, cia_irq_asserted(&m->cia1) || vic_ii_irq_asserted(&m->vic));
    cpu6502_set_nmi_line(&m->cpu, cia_irq_asserted(&m->cia2));

    m->total_cycles++;
}

uint64_t machine_target_cycles_for_elapsed(double elapsed_seconds) {
    if (elapsed_seconds <= 0.0) {
        return 0;
    }
    return (uint64_t)(elapsed_seconds * C64_PAL_PHI2_HZ);
}

static double monotonic_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void machine_run_realtime(Machine *m, double seconds) {
    double start = monotonic_seconds();
    double deadline = start + seconds;
    uint64_t start_cycles = m->total_cycles;

    for (;;) {
        double now = monotonic_seconds();
        if (now >= deadline) {
            return;
        }
        uint64_t target = start_cycles + machine_target_cycles_for_elapsed(now - start);
        while (m->total_cycles < target) {
            machine_cycle(m);
        }
        /* Caught up to real time -- sleep briefly rather than busy-wait
         * a whole host core until the next cycle is actually due. A
         * short, fixed sleep is simple and avoids the more elaborate
         * "sleep exactly until the next cycle boundary" calculation;
         * the loop re-checks the deadline/target every time it wakes,
         * so this doesn't cost accuracy, only (bounded) responsiveness. */
        struct timespec req = {0, 1000000}; /* 1ms */
        nanosleep(&req, NULL);
    }
}
