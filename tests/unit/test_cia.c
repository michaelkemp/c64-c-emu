/* Hand-written tests for the MOS 6526 CIA (Phase 3). Facts asserted
 * here are sourced directly from the primary datasheet -- see
 * docs/sources.md and docs/cia.md. No real ROMs needed. */

#include "testutil.h"

#include "../../src/c64/cia.h"

#define REG_PRA 0x0
#define REG_PRB 0x1
#define REG_DDRA 0x2
#define REG_DDRB 0x3
#define REG_TA_LO 0x4
#define REG_TA_HI 0x5
#define REG_TB_LO 0x6
#define REG_TB_HI 0x7
#define REG_TOD_TENTHS 0x8
#define REG_TOD_SEC 0x9
#define REG_TOD_MIN 0xA
#define REG_TOD_HR 0xB
#define REG_SDR 0xC
#define REG_ICR 0xD
#define REG_CRA 0xE
#define REG_CRB 0xF

#define CRA_START 0x01u
#define CRA_RUNMODE_ONESHOT 0x08u
#define CRA_LOAD 0x10u
#define CRA_INMODE_CNT 0x20u
#define CRB_ALARM 0x80u

static void setup(Cia *cia) {
    cia_init(cia, 10); /* 10 PHI2 cycles per tenth-of-a-second, a convenient round test rate */
}

/* ---------------------------------------------------------------- */
/* Reset state (RES pin, per datasheet)                               */
/* ---------------------------------------------------------------- */

static void test_reset_state(void) {
    Cia cia;
    setup(&cia);
    TEST_ASSERT_EQ_U8(cia.ddra, 0x00);
    TEST_ASSERT_EQ_U8(cia.ddrb, 0x00);
    TEST_ASSERT_EQ_U8(cia_effective_port_a(&cia), 0xFF); /* all-input, floats high */
    TEST_ASSERT_EQ_U8(cia_effective_port_b(&cia), 0xFF);
    TEST_ASSERT_EQ_U8(cia.cra, 0x00);
    TEST_ASSERT_EQ_U8(cia.crb, 0x00);
    TEST_ASSERT_EQ_U16(cia.ta_latch, 0xFFFF); /* "timer latches to all ones" */
    TEST_ASSERT_EQ_U16(cia.tb_latch, 0xFFFF);
}

/* ---------------------------------------------------------------- */
/* Ports: DDR-gated read-back + external pulldown wins                */
/* ---------------------------------------------------------------- */

static void test_port_output_reads_back_written_value(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_DDRA, 0xFF); /* all outputs */
    cia_write(&cia, REG_PRA, 0x5A);
    TEST_ASSERT_EQ_U8(cia_read(&cia, REG_PRA), 0x5A);
}

static void test_port_input_bits_float_high(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_DDRA, 0x0F); /* low nibble output, high nibble input */
    cia_write(&cia, REG_PRA, 0xFF);  /* only low nibble matters */
    TEST_ASSERT_EQ_U8(cia_read(&cia, REG_PRA), 0xFF);
    cia_write(&cia, REG_PRA, 0x00);
    TEST_ASSERT_EQ_U8(cia_read(&cia, REG_PRA), 0xF0); /* high nibble (input) floats high, low reflects the 0 written */
}

static void test_external_pulldown_overrides_output_high(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_DDRA, 0xFF);
    cia_write(&cia, REG_PRA, 0xFF); /* CPU drives every bit high */
    cia_set_port_a_pulldown(&cia, 0x04); /* an external switch shorts bit 2 to ground */
    TEST_ASSERT_EQ_U8(cia_read(&cia, REG_PRA), 0xFB);
}

/* ---------------------------------------------------------------- */
/* Timer A: one-shot vs continuous, latch-vs-counter, force load       */
/* ---------------------------------------------------------------- */

static void test_timer_a_continuous_reloads_and_repeats(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_TA_LO, 3);
    cia_write(&cia, REG_TA_HI, 0); /* stopped -> force-loads counter too */
    TEST_ASSERT_EQ_U16(cia.ta_counter, 3);
    cia_write(&cia, REG_CRA, CRA_START); /* continuous (RUNMODE=0), start */

    cia_tick(&cia, 2);
    TEST_ASSERT_EQ_U16(cia.ta_counter, 1);
    TEST_ASSERT_EQ_U8(cia.icr_data & CIA_ICR_TA, 0);

    cia_tick(&cia, 1); /* 3rd cycle: underflow */
    TEST_ASSERT_EQ_U16(cia.ta_counter, 3); /* reloaded from latch */
    TEST_ASSERT_EQ_U8(cia.icr_data & CIA_ICR_TA, CIA_ICR_TA);
    TEST_ASSERT(cia.cra & CRA_START); /* still running */

    cia.icr_data = 0;
    cia_tick(&cia, 3); /* one full period again */
    TEST_ASSERT_EQ_U8(cia.icr_data & CIA_ICR_TA, CIA_ICR_TA);
}

static void test_timer_a_one_shot_stops_after_underflow(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_TA_LO, 2);
    cia_write(&cia, REG_TA_HI, 0);
    cia_write(&cia, REG_CRA, CRA_START | CRA_RUNMODE_ONESHOT);

    cia_tick(&cia, 2); /* underflows on the 2nd cycle */
    TEST_ASSERT_EQ_U8(cia.icr_data & CIA_ICR_TA, CIA_ICR_TA);
    TEST_ASSERT(!(cia.cra & CRA_START)); /* one-shot clears its own START bit */
    TEST_ASSERT_EQ_U16(cia.ta_counter, 2); /* reloaded from latch even in one-shot mode */

    cia.icr_data = 0;
    cia_tick(&cia, 10); /* stopped -- must not count or interrupt again */
    TEST_ASSERT_EQ_U8(cia.icr_data & CIA_ICR_TA, 0);
    TEST_ASSERT_EQ_U16(cia.ta_counter, 2);
}

static void test_writing_hi_byte_while_running_does_not_reload_counter(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_TA_LO, 100);
    cia_write(&cia, REG_TA_HI, 0);
    cia_write(&cia, REG_CRA, CRA_START);
    cia_tick(&cia, 5);
    TEST_ASSERT_EQ_U16(cia.ta_counter, 95);

    cia_write(&cia, REG_TA_HI, 0); /* latch updated, but timer is running -- counter must NOT reset to the latch */
    TEST_ASSERT_EQ_U16(cia.ta_counter, 95);
    TEST_ASSERT_EQ_U16(cia.ta_latch, 100);
}

static void test_force_load_strobe(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_TA_LO, 50);
    cia_write(&cia, REG_TA_HI, 0);
    cia_write(&cia, REG_CRA, CRA_START);
    cia_tick(&cia, 10);
    TEST_ASSERT_EQ_U16(cia.ta_counter, 40);

    cia_write(&cia, REG_CRA, CRA_START | CRA_LOAD); /* force load while running */
    TEST_ASSERT_EQ_U16(cia.ta_counter, 50);
    TEST_ASSERT_EQ_U8(cia_read(&cia, REG_CRA) & CRA_LOAD, 0); /* LOAD is a strobe, always reads back 0 */
}

/* ---------------------------------------------------------------- */
/* Timer B cascaded modes                                             */
/* ---------------------------------------------------------------- */

static void test_timer_b_counts_timer_a_underflow(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_TA_LO, 4);
    cia_write(&cia, REG_TA_HI, 0);
    cia_write(&cia, REG_CRA, CRA_START);

    cia_write(&cia, REG_TB_LO, 3);
    cia_write(&cia, REG_TB_HI, 0);
    cia_write(&cia, REG_CRB, 0x01u | 0x40u); /* START, INMODE=10 (CRB6=1,CRB5=0) -> counts TA underflow */

    /* TA underflows once every 4 cycles; TB should count down once per
     * TA underflow, so TB itself underflows after 3 TA underflows =
     * 12 PHI2 cycles. */
    cia_tick(&cia, 12);
    TEST_ASSERT_EQ_U8(cia.icr_data & CIA_ICR_TB, CIA_ICR_TB);
    TEST_ASSERT_EQ_U16(cia.tb_counter, 3);
}

/* ---------------------------------------------------------------- */
/* CNT-driven modes                                                   */
/* ---------------------------------------------------------------- */

static void test_timer_a_counts_cnt_pulses(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_TA_LO, 2);
    cia_write(&cia, REG_TA_HI, 0);
    cia_write(&cia, REG_CRA, CRA_START | CRA_INMODE_CNT);

    cia_tick(&cia, 1000); /* PHI2 alone must not move a CNT-driven timer */
    TEST_ASSERT_EQ_U16(cia.ta_counter, 2);

    cia_set_cnt_level(&cia, true); /* rising edge 1 */
    TEST_ASSERT_EQ_U16(cia.ta_counter, 1);
    cia_set_cnt_level(&cia, false);
    cia_set_cnt_level(&cia, true); /* rising edge 2 -> underflow */
    TEST_ASSERT_EQ_U8(cia.icr_data & CIA_ICR_TA, CIA_ICR_TA);
    TEST_ASSERT_EQ_U16(cia.ta_counter, 2);
}

/* ---------------------------------------------------------------- */
/* ICR: mask write semantics, read-clears-all, IR bit                 */
/* ---------------------------------------------------------------- */

static void test_icr_mask_set_clear_semantics(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_ICR, 0x80u | CIA_ICR_TA | CIA_ICR_TB); /* set TA, TB */
    TEST_ASSERT_EQ_U8(cia.icr_mask, CIA_ICR_TA | CIA_ICR_TB);

    cia_write(&cia, REG_ICR, 0x00u | CIA_ICR_TA); /* clear TA only */
    TEST_ASSERT_EQ_U8(cia.icr_mask, CIA_ICR_TB);
}

static void test_icr_read_clears_all_flags_and_reports_ir(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_ICR, 0x80u | CIA_ICR_TA); /* enable TA only */
    cia.icr_data = CIA_ICR_TA | CIA_ICR_TB;       /* pretend both underflowed */

    uint8_t result = cia_read(&cia, REG_ICR);
    TEST_ASSERT_EQ_U8(result, CIA_ICR_IR | CIA_ICR_TA | CIA_ICR_TB); /* IR set because TA (masked) fired, but both flag bits show */
    TEST_ASSERT_EQ_U8(cia.icr_data, 0); /* read clears everything, not just the polled bit */
    TEST_ASSERT(!cia_irq_asserted(&cia));
}

static void test_irq_asserted_reflects_mask_and_data(void) {
    Cia cia;
    setup(&cia);
    cia.icr_data = CIA_ICR_TB; /* TB fired */
    TEST_ASSERT(!cia_irq_asserted(&cia)); /* not masked in yet */
    cia_write(&cia, REG_ICR, 0x80u | CIA_ICR_TB);
    TEST_ASSERT(cia_irq_asserted(&cia));
}

/* ---------------------------------------------------------------- */
/* TOD clock                                                          */
/* ---------------------------------------------------------------- */

static void test_tod_tenths_rollover_into_seconds(void) {
    Cia cia;
    setup(&cia);
    cia.tod_tenths = 0x09;
    cia_tick(&cia, 10); /* exactly one tenth at this test's 10-cycles-per-tenth rate */
    TEST_ASSERT_EQ_U8(cia.tod_tenths, 0x00);
    TEST_ASSERT_EQ_U8(cia.tod_seconds, 0x01);
}

static void test_tod_seconds_bcd_rollover_60(void) {
    Cia cia;
    setup(&cia);
    cia.tod_tenths = 0x09; /* the cascade only fires when tenths itself wraps 9->0 */
    cia.tod_seconds = 0x59;
    cia_tick(&cia, 10);
    TEST_ASSERT_EQ_U8(cia.tod_seconds, 0x00);
    TEST_ASSERT_EQ_U8(cia.tod_minutes, 0x01);
}

static void test_tod_hours_12_to_1_flips_ampm(void) {
    Cia cia;
    setup(&cia);
    cia.tod_hours = 0x12;   /* 12, AM (bit7=0) */
    cia.tod_minutes = 0x59;
    cia.tod_seconds = 0x59;
    cia.tod_tenths = 0x09;
    cia_tick(&cia, 10);
    TEST_ASSERT_EQ_U8(cia.tod_hours, 0x81); /* 1, PM */
}

static void test_tod_hours_11_to_12_keeps_ampm(void) {
    Cia cia;
    setup(&cia);
    cia.tod_hours = 0x11; /* 11 AM */
    cia.tod_minutes = 0x59;
    cia.tod_seconds = 0x59;
    cia.tod_tenths = 0x09;
    cia_tick(&cia, 10);
    TEST_ASSERT_EQ_U8(cia.tod_hours, 0x12); /* 12, still AM */
}

static void test_tod_hours_write_stops_clock_tenths_write_restarts(void) {
    Cia cia;
    setup(&cia);
    cia_write(&cia, REG_TOD_HR, 0x12); /* writing Hours stops the clock */
    TEST_ASSERT(!cia.tod_running);
    cia_tick(&cia, 100);
    TEST_ASSERT_EQ_U8(cia.tod_tenths, 0x00); /* didn't move at all */

    cia_write(&cia, REG_TOD_TENTHS, 0x00); /* writing Tenths restarts it */
    TEST_ASSERT(cia.tod_running);
    cia_tick(&cia, 10);
    TEST_ASSERT_EQ_U8(cia.tod_tenths, 0x01);
}

static void test_tod_hours_read_latches_until_tenths_read(void) {
    Cia cia;
    setup(&cia);
    cia.tod_hours = 0x05;
    cia.tod_minutes = 0x30;
    cia.tod_seconds = 0x15;
    cia.tod_tenths = 0x03;

    uint8_t hr = cia_read(&cia, REG_TOD_HR); /* latches all four */
    TEST_ASSERT_EQ_U8(hr, 0x05);

    cia_tick(&cia, 100); /* the live clock keeps advancing underneath */
    TEST_ASSERT_EQ_U8(cia_read(&cia, REG_TOD_MIN), 0x30); /* still the latched snapshot */
    TEST_ASSERT_EQ_U8(cia_read(&cia, REG_TOD_SEC), 0x15);
    TEST_ASSERT_EQ_U8(cia_read(&cia, REG_TOD_TENTHS), 0x03); /* latched value returned once more... */

    /* ...and now unlatched: a fresh read reflects the real, advanced time. */
    uint8_t live_min = cia_read(&cia, REG_TOD_MIN);
    TEST_ASSERT(live_min != 0x30 || cia_read(&cia, REG_TOD_SEC) != 0x15);
}

static void test_tod_alarm_match_sets_icr_flag(void) {
    Cia cia;
    setup(&cia);
    cia.tod_hours = 0x12;
    cia.tod_minutes = 0x00;
    cia.tod_seconds = 0x00;
    cia.tod_tenths = 0x08;

    cia_write(&cia, REG_CRB, CRB_ALARM); /* select alarm registers for writes */
    cia_write(&cia, REG_TOD_HR, 0x12);
    cia_write(&cia, REG_TOD_MIN, 0x00);
    cia_write(&cia, REG_TOD_SEC, 0x00);
    cia_write(&cia, REG_TOD_TENTHS, 0x09); /* alarm = 12:00:00.9 */
    cia_write(&cia, REG_CRB, 0x00);        /* back to clock writes -- also restarts the clock via tenths write path? No: we only wrote Hours as clock earlier via direct field set, not via cia_write, so tod_running is whatever cia_init left it (true). */

    cia_tick(&cia, 10); /* clock ticks from .8 to .9, hitting the alarm time */
    TEST_ASSERT_EQ_U8(cia.icr_data & CIA_ICR_ALARM, CIA_ICR_ALARM);
}

int main(void) {
    test_reset_state();

    test_port_output_reads_back_written_value();
    test_port_input_bits_float_high();
    test_external_pulldown_overrides_output_high();

    test_timer_a_continuous_reloads_and_repeats();
    test_timer_a_one_shot_stops_after_underflow();
    test_writing_hi_byte_while_running_does_not_reload_counter();
    test_force_load_strobe();

    test_timer_b_counts_timer_a_underflow();
    test_timer_a_counts_cnt_pulses();

    test_icr_mask_set_clear_semantics();
    test_icr_read_clears_all_flags_and_reports_ir();
    test_irq_asserted_reflects_mask_and_data();

    test_tod_tenths_rollover_into_seconds();
    test_tod_seconds_bcd_rollover_60();
    test_tod_hours_12_to_1_flips_ampm();
    test_tod_hours_11_to_12_keeps_ampm();
    test_tod_hours_write_stops_clock_tenths_write_restarts();
    test_tod_hours_read_latches_until_tenths_read();
    test_tod_alarm_match_sets_icr_flag();

    printf("%d/%d assertions passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
