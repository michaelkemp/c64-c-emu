/* MOS 6526 CIA. Every register-format/behavior fact here (control-
 * register bit layouts, timer-latch-vs-counter distinction, ICR mask-
 * write semantics, TOD latch/stop-start behavior, RES-pin reset state)
 * is sourced directly from the primary datasheet -- see
 * docs/sources.md and docs/cia.md. */

#include "cia.h"

#include <string.h>

void cia_init(Cia *cia, uint32_t cycles_per_tenth) {
    memset(cia, 0, sizeof(*cia));
    /* Real RES-pin behavior: "the timer control registers are set to
     * zero and the timer latches to all ones. All other registers are
     * reset to zero." (counters included -- they aren't singled out as
     * an exception, so they start at 0 like every other non-latch
     * register; the first tick after being started with an untouched
     * counter reloads from the all-ones latch with no spurious
     * interrupt, per the same "counter==0 means reload" rule an
     * underflow uses -- see decrement_timer_a/b below.) */
    cia->ta_latch = 0xFFFFu;
    cia->tb_latch = 0xFFFFu;
    cia->tod_running = true; /* RES doesn't document TOD being disabled; only a Hours-register write does that */
    cia->cycles_per_tenth = cycles_per_tenth;
}

/* ---------------------------------------------------------------- */
/* Ports                                                              */
/* ---------------------------------------------------------------- */

uint8_t cia_effective_port_a(const Cia *cia) {
    /* Datasheet: "On a READ, the PR reflects the information present
     * on the actual port pins... for both input and output bits." An
     * input pin floats high via a passive pull-up; an external
     * pulldown (a closed keyboard/joystick switch to ground) wins over
     * either an output-driven high or the pull-up, matching real
     * open-collector-style board wiring. */
    uint8_t driven = (uint8_t)((cia->pra & cia->ddra) | (uint8_t)~cia->ddra);
    return (uint8_t)(driven & (uint8_t)~cia->port_a_pulldown);
}

uint8_t cia_effective_port_b(const Cia *cia) {
    uint8_t driven = (uint8_t)((cia->prb & cia->ddrb) | (uint8_t)~cia->ddrb);
    return (uint8_t)(driven & (uint8_t)~cia->port_b_pulldown);
}

void cia_set_port_a_pulldown(Cia *cia, uint8_t mask) { cia->port_a_pulldown = mask; }
void cia_set_port_b_pulldown(Cia *cia, uint8_t mask) { cia->port_b_pulldown = mask; }

/* ---------------------------------------------------------------- */
/* Timers                                                             */
/* ---------------------------------------------------------------- */

static void set_icr_flag(Cia *cia, uint8_t flag) {
    cia->icr_data |= flag;
}

/* Returns true if this decrement underflowed (counter reached 0). Both
 * one-shot and continuous mode reload the counter from the latch on
 * underflow -- datasheet: "the timer will count down... to zero,
 * generate an interrupt, reload the latched value, then stop [one-
 * shot] / and repeat the procedure continuously [continuous]." The
 * only difference is one-shot also clears its own START bit. */
static bool decrement_timer_a(Cia *cia) {
    if (cia->ta_counter == 0) {
        cia->ta_counter = cia->ta_latch;
        return false;
    }
    cia->ta_counter--;
    if (cia->ta_counter != 0) {
        return false;
    }
    set_icr_flag(cia, CIA_ICR_TA);
    bool one_shot = (cia->cra & 0x08u) != 0;
    cia->ta_counter = cia->ta_latch;
    if (one_shot) {
        cia->cra = (uint8_t)(cia->cra & (uint8_t)~0x01u);
    }
    return true;
}

static void decrement_timer_b(Cia *cia) {
    if (cia->tb_counter == 0) {
        cia->tb_counter = cia->tb_latch;
        return;
    }
    cia->tb_counter--;
    if (cia->tb_counter != 0) {
        return;
    }
    set_icr_flag(cia, CIA_ICR_TB);
    bool one_shot = (cia->crb & 0x08u) != 0;
    cia->tb_counter = cia->tb_latch;
    if (one_shot) {
        cia->crb = (uint8_t)(cia->crb & (uint8_t)~0x01u);
    }
}

/* CRB bits 6,5 (in that bit order) select Timer B's input mode: 00 =
 * PHI2, 01 = CNT positive transitions, 10 = Timer A underflow, 11 =
 * Timer A underflow while CNT is high. (crb >> 5) & 0x03 conveniently
 * already produces exactly that 2-bit value in that order. */
static uint8_t timer_b_inmode(const Cia *cia) {
    return (uint8_t)((cia->crb >> 5) & 0x03u);
}

static void tick_one_cycle(Cia *cia) {
    bool ta_started = (cia->cra & 0x01u) != 0;
    bool ta_counts_phi2 = (cia->cra & 0x20u) == 0; /* CRA INMODE bit5: 0 = PHI2, 1 = CNT */
    bool ta_underflowed = false;
    if (ta_started && ta_counts_phi2) {
        ta_underflowed = decrement_timer_a(cia);
    }

    bool tb_started = (cia->crb & 0x01u) != 0;
    uint8_t tb_mode = timer_b_inmode(cia);
    bool tb_counts_now = false;
    if (tb_started) {
        if (tb_mode == 0) {
            tb_counts_now = true;
        } else if (tb_mode == 2) {
            tb_counts_now = ta_underflowed;
        } else if (tb_mode == 3) {
            tb_counts_now = ta_underflowed && cia->cnt_level;
        }
        /* tb_mode == 1 (CNT pulses) is edge-driven -- see cia_set_cnt_level(). */
    }
    if (tb_counts_now) {
        decrement_timer_b(cia);
    }
}

void cia_set_cnt_level(Cia *cia, bool level) {
    bool rising_edge = level && !cia->cnt_level;
    cia->cnt_level = level;
    if (!rising_edge) {
        return;
    }

    bool ta_started = (cia->cra & 0x01u) != 0;
    bool ta_counts_cnt = (cia->cra & 0x20u) != 0;
    if (ta_started && ta_counts_cnt) {
        decrement_timer_a(cia);
    }

    bool tb_started = (cia->crb & 0x01u) != 0;
    if (tb_started && timer_b_inmode(cia) == 1) {
        decrement_timer_b(cia);
    }
}

/* ---------------------------------------------------------------- */
/* TOD clock -- BCD-packed exactly like the real registers, so         */
/* cia_read()/cia_write() never need to convert to/from decimal.       */
/* ---------------------------------------------------------------- */

/* Increments the BCD digit at bit position `shift` (a nibble), wrapping
 * to 0 past `max_digit` and reporting whether it wrapped (a carry into
 * the next digit/unit is needed). */
static bool bcd_digit_inc(uint8_t *packed, int shift, uint8_t max_digit) {
    uint8_t digit = (uint8_t)((*packed >> shift) & 0x0Fu);
    digit++;
    bool wrapped = false;
    if (digit > max_digit) {
        digit = 0;
        wrapped = true;
    }
    *packed = (uint8_t)((*packed & (uint8_t)~(0x0Fu << shift)) | (uint8_t)(digit << shift));
    return wrapped;
}

/* Seconds/minutes are two BCD digits: ones (bits 0-3, 0-9) and tens
 * (bits 4-6, 0-5), giving 00-59. Returns true on a 59->00 carry. */
static bool bcd_60_inc(uint8_t *packed) {
    if (!bcd_digit_inc(packed, 0, 9)) {
        return false;
    }
    return bcd_digit_inc(packed, 4, 5);
}

/* Hours: bit7 = PM flag, bit4 = tens digit (0 or 1), bits0-3 = ones
 * digit -- a real 12-hour dial (1-12), not 0-23. Real, documented
 * quirk: AM/PM flips exactly on the 12->1 transition, not on 11->12. */
static void tod_increment_hours(Cia *cia) {
    uint8_t h = cia->tod_hours;
    bool pm = (h & 0x80u) != 0;
    int hour = ((h >> 4) & 0x01u) * 10 + (h & 0x0Fu);
    hour++;
    if (hour > 12) {
        hour = 1;
        pm = !pm;
    }
    uint8_t tens = (uint8_t)(hour / 10);
    uint8_t ones = (uint8_t)(hour % 10);
    cia->tod_hours = (uint8_t)((pm ? 0x80u : 0u) | (uint8_t)(tens << 4) | ones);
}

static void check_tod_alarm(Cia *cia) {
    if (cia->tod_tenths == cia->alarm_tenths && cia->tod_seconds == cia->alarm_seconds &&
        cia->tod_minutes == cia->alarm_minutes && cia->tod_hours == cia->alarm_hours) {
        set_icr_flag(cia, CIA_ICR_ALARM);
    }
}

static void tod_advance_one_tenth(Cia *cia) {
    if (!bcd_digit_inc(&cia->tod_tenths, 0, 9)) {
        check_tod_alarm(cia);
        return;
    }
    if (bcd_60_inc(&cia->tod_seconds)) {
        if (bcd_60_inc(&cia->tod_minutes)) {
            tod_increment_hours(cia);
        }
    }
    check_tod_alarm(cia);
}

void cia_tick(Cia *cia, uint32_t cycles) {
    for (uint32_t i = 0; i < cycles; i++) {
        tick_one_cycle(cia);
    }
    if (!cia->tod_running || cia->cycles_per_tenth == 0) {
        return;
    }
    /* Integer accumulator, not float -- see docs/cia.md on why an
     * inexact float accumulator would eventually drift a tick early or
     * late at some real time boundary. */
    cia->tod_cycle_accumulator += cycles;
    while (cia->tod_cycle_accumulator >= cia->cycles_per_tenth) {
        cia->tod_cycle_accumulator -= cia->cycles_per_tenth;
        tod_advance_one_tenth(cia);
    }
}

/* ---------------------------------------------------------------- */
/* Register read/write                                               */
/* ---------------------------------------------------------------- */

uint8_t cia_read(Cia *cia, uint8_t reg) {
    switch (reg & 0x0Fu) {
    case 0x0: return cia_effective_port_a(cia);
    case 0x1: return cia_effective_port_b(cia);
    case 0x2: return cia->ddra;
    case 0x3: return cia->ddrb;
    case 0x4: return (uint8_t)(cia->ta_counter & 0xFFu);
    case 0x5: return (uint8_t)(cia->ta_counter >> 8);
    case 0x6: return (uint8_t)(cia->tb_counter & 0xFFu);
    case 0x7: return (uint8_t)(cia->tb_counter >> 8);
    case 0x8: { /* TOD tenths: reading this un-latches (see $B) */
        uint8_t v = cia->tod_latched ? cia->latched_tenths : cia->tod_tenths;
        cia->tod_latched = false;
        return v;
    }
    case 0x9: return cia->tod_latched ? cia->latched_seconds : cia->tod_seconds;
    case 0xA: return cia->tod_latched ? cia->latched_minutes : cia->tod_minutes;
    case 0xB: /* TOD hours: reading this latches all four TOD registers */
        if (!cia->tod_latched) {
            cia->latched_tenths = cia->tod_tenths;
            cia->latched_seconds = cia->tod_seconds;
            cia->latched_minutes = cia->tod_minutes;
            cia->latched_hours = cia->tod_hours;
            cia->tod_latched = true;
        }
        return cia->latched_hours;
    case 0xC: return cia->sdr; /* stub -- see docs/cia.md */
    case 0xD: { /* ICR: read returns DATA (with computed IR bit) and clears it */
        uint8_t data = cia->icr_data & 0x1Fu;
        uint8_t ir = ((data & cia->icr_mask) != 0) ? CIA_ICR_IR : 0u;
        uint8_t result = (uint8_t)(ir | data);
        cia->icr_data = 0;
        return result;
    }
    case 0xE: return cia->cra;
    case 0xF: return cia->crb;
    default: return 0;
    }
}

void cia_write(Cia *cia, uint8_t reg, uint8_t value) {
    switch (reg & 0x0Fu) {
    case 0x0: cia->pra = value; break;
    case 0x1: cia->prb = value; break;
    case 0x2: cia->ddra = value; break;
    case 0x3: cia->ddrb = value; break;
    case 0x4: cia->ta_latch = (uint16_t)((cia->ta_latch & 0xFF00u) | value); break;
    case 0x5:
        cia->ta_latch = (uint16_t)((cia->ta_latch & 0x00FFu) | ((uint16_t)value << 8));
        /* "If the timer is running, a write to the high byte will load
         * the timer latch, but not reload the counter." -- so only
         * force-load the live counter when currently stopped. */
        if ((cia->cra & 0x01u) == 0) {
            cia->ta_counter = cia->ta_latch;
        }
        break;
    case 0x6: cia->tb_latch = (uint16_t)((cia->tb_latch & 0xFF00u) | value); break;
    case 0x7:
        cia->tb_latch = (uint16_t)((cia->tb_latch & 0x00FFu) | ((uint16_t)value << 8));
        if ((cia->crb & 0x01u) == 0) {
            cia->tb_counter = cia->tb_latch;
        }
        break;
    case 0x8: /* TOD tenths: which register (clock vs alarm) depends on CRB7 */
        if (cia->crb & 0x80u) {
            cia->alarm_tenths = (uint8_t)(value & 0x0Fu);
        } else {
            cia->tod_tenths = (uint8_t)(value & 0x0Fu);
            cia->tod_running = true; /* "will not start again until after a write to the 10ths of seconds register" */
        }
        break;
    case 0x9:
        if (cia->crb & 0x80u) {
            cia->alarm_seconds = value;
        } else {
            cia->tod_seconds = value;
        }
        break;
    case 0xA:
        if (cia->crb & 0x80u) {
            cia->alarm_minutes = value;
        } else {
            cia->tod_minutes = value;
        }
        break;
    case 0xB:
        if (cia->crb & 0x80u) {
            cia->alarm_hours = value;
        } else {
            cia->tod_hours = value;
            cia->tod_running = false; /* "TOD is automatically stopped whenever a write to the Hours register occurs" */
        }
        break;
    case 0xC: cia->sdr = value; break;
    case 0xD: /* ICR mask write: bit7 set = set these mask bits, bit7 clear = clear these mask bits */
        if (value & 0x80u) {
            cia->icr_mask = (uint8_t)(cia->icr_mask | (value & 0x1Fu));
        } else {
            cia->icr_mask = (uint8_t)(cia->icr_mask & (uint8_t)~(value & 0x1Fu));
        }
        break;
    case 0xE: /* bit4 (LOAD) is a strobe: "always reads back a zero and writing a zero has no effect" */
        cia->cra = (uint8_t)(value & (uint8_t)~0x10u);
        if (value & 0x10u) {
            cia->ta_counter = cia->ta_latch;
        }
        break;
    case 0xF:
        cia->crb = (uint8_t)(value & (uint8_t)~0x10u);
        if (value & 0x10u) {
            cia->tb_counter = cia->tb_latch;
        }
        break;
    default: break;
    }
}

bool cia_irq_asserted(const Cia *cia) {
    return (cia->icr_data & cia->icr_mask & 0x1Fu) != 0;
}
