/* Phase 6's own real verification target (docs/machine.md): boot the
 * real KERNAL+BASIC through the full cycle-interleaved Machine and
 * confirm the jiffy clock ($A0-$A2) actually increments at the correct
 * real-world rate -- this exercises CPU+CIA1 interleaving, real IRQ
 * delivery, and the CIA timer/ICR logic all at once.
 *
 * Needs the user's own staged ROMs (tier 3, docs/testing-strategy.md);
 * SKIPs (exit 0) rather than failing the build if they aren't present.
 *
 * Rather than hardcoding an assumed jiffy rate (or even assuming which
 * CIA1 Timer A setup is the KERNAL's final, settled one -- empirically,
 * the staged KERNAL here briefly enables Timer A's interrupt very early
 * during its own self-test, before the CPU's I flag is even clear, so
 * watching internal CIA registers alone is not a reliable signal), this
 * test watches the jiffy clock bytes themselves: wait for a real,
 * observed tick, then measure the rate against whatever CIA1 Timer A
 * reload value is actually in effect at that moment. Confirmed
 * empirically (see docs/sources.md / docs/cia.md) to be ~16421-17045
 * PHI2 cycles depending on boot stage, giving a real jiffy rate of
 * ~58-60Hz -- notably NOT the PAL video frame rate (~50.125Hz): the
 * KERNAL ROM content, and so this timer constant, is shared between
 * PAL and NTSC hardware, a real, documented C64 characteristic. */

#include <stdio.h>

#include "../../src/c64/machine.h"

/* $A0-$A2 is a big-endian 3-byte counter: $A0 is the HIGH byte, $A2 the
 * LOW byte (confirmed empirically against the real staged KERNAL --
 * see docs/sources.md; the opposite, more "natural"-looking assumption
 * is wrong). */
static uint32_t read_jiffy(const Machine *m) {
    return ((uint32_t)m->mem.ram[0xA0] << 16) | ((uint32_t)m->mem.ram[0xA1] << 8) | (uint32_t)m->mem.ram[0xA2];
}

int main(void) {
    Machine m;
    machine_init(&m);

    bool have_kernal = machine_load_kernal(&m, "roms/c64/kernal.rom");
    bool have_basic = machine_load_basic(&m, "roms/c64/basic.rom");
    bool have_chargen = machine_load_chargen(&m, "roms/c64/chargen.rom");
    if (!have_kernal || !have_basic || !have_chargen) {
        printf("SKIP: real C64 ROMs not staged under roms/c64/ -- run scripts/stage_roms.sh "
               "with your own legally-acquired dumps first (kernal=%s basic=%s chargen=%s)\n",
               have_kernal ? "ok" : "missing", have_basic ? "ok" : "missing",
               have_chargen ? "ok" : "missing");
        return 0;
    }

    machine_reset(&m);

    const uint64_t max_cycles_to_wait = 5000000;
    uint32_t initial = read_jiffy(&m);
    while (read_jiffy(&m) == initial && m.total_cycles < max_cycles_to_wait) {
        machine_cycle(&m);
    }
    if (read_jiffy(&m) == initial) {
        printf("FAIL: jiffy clock never ticked at all within %llu cycles.\n",
               (unsigned long long)max_cycles_to_wait);
        return 1;
    }
    printf("First real jiffy tick observed at cycle %llu\n", (unsigned long long)m.total_cycles);

    /* Whatever CIA1 Timer A reload value is in effect right now is
     * demonstrably the one actually driving real ticks -- measure
     * against it directly rather than assuming which boot stage we're in. */
    uint32_t reload = m.cia1.ta_latch;
    uint32_t jiffy_before = read_jiffy(&m);

    const uint64_t real_ticks_to_run = 100;
    machine_run_cycles(&m, (uint64_t)reload * real_ticks_to_run);

    uint32_t delta = read_jiffy(&m) - jiffy_before;
    printf("CIA1 Timer A reload = %u cycles (~%.2fHz at the real PAL clock); "
           "jiffy clock advanced by %u over %llu real PHI2 cycles (expected %llu)\n",
           reload, C64_PAL_PHI2_HZ / (double)reload, delta,
           (unsigned long long)((uint64_t)reload * real_ticks_to_run),
           (unsigned long long)real_ticks_to_run);

    /* A small tolerance for interrupt-service latency at the
     * measurement boundaries (a handful of cycles either side of an
     * exact tick), not for any systematic rate error. */
    if (delta < real_ticks_to_run - 2 || delta > real_ticks_to_run + 2) {
        printf("FAIL: jiffy clock rate doesn't match the real CIA1 timer reload value.\n");
        return 1;
    }

    printf("PASS: jiffy clock advances at the correct real rate for this ROM's own "
           "CIA1 Timer A reload value.\n");
    return 0;
}
