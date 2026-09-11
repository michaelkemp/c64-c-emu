#ifndef C64EMU_C64_CIA_H
#define C64EMU_C64_CIA_H

#include <stdbool.h>
#include <stdint.h>

/* ICR flag/mask bits (register $D) -- see docs/sources.md's CIA
 * datasheet entry for the exact bit layout this matches. */
#define CIA_ICR_TA 0x01u
#define CIA_ICR_TB 0x02u
#define CIA_ICR_ALARM 0x04u
#define CIA_ICR_SP 0x08u
#define CIA_ICR_FLAG 0x10u
#define CIA_ICR_IR 0x80u /* read-only, computed, never part of the mask */

/* One MOS 6526 CIA. The C64 has two (CIA1 at $DC00-$DCFF drives the
 * keyboard/joystick and IRQ; CIA2 at $DD00-$DDFF drives the serial/IEC
 * bus, RS-232, VIC-II bank select, and NMI) -- same chip, different
 * board wiring, per docs/cia.md: "implement one Cia module and
 * instantiate it twice." This module knows nothing about keyboards,
 * joysticks, or which interrupt line it feeds -- see src/c64/keyboard.h
 * for the keyboard-matrix/joystick model that wires into Port A/B here,
 * and Phase 6 for wiring cia_irq_asserted() into the real IRQ/NMI line.
 *
 * Facts below (register map, control-register bit layout, timer-latch
 * behavior, ICR mask-write semantics, TOD latch/stop-start behavior)
 * are sourced directly from the primary datasheet -- see
 * docs/sources.md. */
typedef struct Cia {
    /* Ports. See cia_read()/cia_write() for the effective-pin formula,
     * which matches the datasheet's own "a READ reflects the actual
     * port pins, for both input and output bits" wording. */
    uint8_t pra, prb;   /* Peripheral Data Registers ($0/$1) */
    uint8_t ddra, ddrb; /* Data Direction Registers ($2/$3): 1 bit = that pin is an output */

    /* Bits externally pulled low right now regardless of DDR/PR (e.g. a
     * closed keyboard-matrix or joystick switch shorting a pin to
     * ground) -- see src/c64/keyboard.c. The caller is expected to
     * update these (via cia_set_port_a_pulldown/cia_set_port_b_pulldown)
     * before each read that needs a fresh value; they're pure
     * combinational inputs, not latched/clocked state. */
    uint8_t port_a_pulldown;
    uint8_t port_b_pulldown;

    /* Timers. ta_counter/tb_counter are the live down-counters (what a
     * READ of $4-$7 returns); ta_latch/tb_latch are the reload values
     * (what a WRITE to $4-$7 sets) -- these are genuinely different
     * registers on real hardware, not read/write views of the same
     * storage. */
    uint16_t ta_counter, ta_latch;
    uint16_t tb_counter, tb_latch;
    uint8_t cra, crb; /* Control Registers A/B ($E/$F) */

    /* CNT line (serial shift clock / external timer input). A real
     * physical pin; nothing in this project drives it yet (no
     * datasette or IEC bus modeled until Phase 9b) -- see
     * cia_set_cnt_level() and docs/cia.md's Known Gaps. */
    bool cnt_level;

    /* Interrupt Control Register: icr_data holds the five real flag
     * bits (TA, TB, ALARM, SP, FLAG -- bits 0-4); the IR bit (bit 7) is
     * computed on read, not stored. icr_mask holds which of those five
     * are currently enabled to assert the IRQ/NMI line. */
    uint8_t icr_data;
    uint8_t icr_mask;

    /* Time-of-day clock. Each byte matches the real register's own BCD
     * packing (see cia_read()/cia_write() for the exact bit layout per
     * register) so external code never needs to convert BCD itself. */
    uint8_t tod_tenths, tod_seconds, tod_minutes, tod_hours;
    uint8_t alarm_tenths, alarm_seconds, alarm_minutes, alarm_hours;
    bool tod_running;
    bool tod_latched;
    uint8_t latched_tenths, latched_seconds, latched_minutes, latched_hours;
    /* Integer accumulator driving BCD tenths-of-a-second ticks from
     * whole PHI2 cycles -- see docs/cia.md on why this must be integer,
     * not float, arithmetic. */
    uint32_t tod_cycle_accumulator;
    uint32_t cycles_per_tenth;

    /* Serial Data Register ($C) -- stubbed per docs/cia.md; real
     * shift-register timing/interrupt behavior is Phase 9b territory
     * (the IEC bus). Reads/writes just pass through as a plain byte. */
    uint8_t sdr;
} Cia;

/* cycles_per_tenth: how many PHI2 cycles make up 1/10 second at the
 * real PAL clock rate (docs/vic-ii.md) -- the caller computes this
 * once from PAL_CLOCK_HZ and passes it in, so this module never
 * hardcodes a clock rate itself. */
void cia_init(Cia *cia, uint32_t cycles_per_tenth);

/* reg is the register index 0x0-0xF (the caller/bus glue is responsible
 * for the real 16-byte mirroring within each CIA's 256-byte I/O range --
 * see docs/memory-map.md and docs/cia.md). */
uint8_t cia_read(Cia *cia, uint8_t reg);
void cia_write(Cia *cia, uint8_t reg, uint8_t value);

/* Advances the timers' PHI2-driven counting and the TOD clock by
 * exactly `cycles` PHI2 cycles. Call this every cycle (or in whatever
 * batch size the main loop uses) from Phase 6's cycle-interleaved main
 * loop, the same real-elapsed-cycles value the CPU core and VIC-II are
 * also driven by. */
void cia_tick(Cia *cia, uint32_t cycles);

/* Updates the physical CNT line level and applies edge/level-gated
 * counting modes (Timer A/B's CNT-pulse INMODE, and CRB's "Timer A
 * underflow while CNT is high" mode) accordingly. See docs/cia.md's
 * Known Gaps -- nothing in this project drives this yet. */
void cia_set_cnt_level(Cia *cia, bool level);

void cia_set_port_a_pulldown(Cia *cia, uint8_t mask);
void cia_set_port_b_pulldown(Cia *cia, uint8_t mask);

/* The port's current effective pin value -- what the *other* side of a
 * board-level connection (e.g. the keyboard matrix reading which
 * columns Port A currently has selected) would see, without going
 * through this chip's own register read path. */
uint8_t cia_effective_port_a(const Cia *cia);
uint8_t cia_effective_port_b(const Cia *cia);

/* True whenever an enabled interrupt source has a flag set -- the
 * caller (Phase 6) ORs this with the other CIA's and the VIC-II's own
 * raster-interrupt line for IRQ, or wires it alone for CIA2's NMI. */
bool cia_irq_asserted(const Cia *cia);

#endif /* C64EMU_C64_CIA_H */
