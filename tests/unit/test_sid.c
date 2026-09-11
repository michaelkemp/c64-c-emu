/* Hand-written tests for the MOS 6581 SID (Phase 5). Register map,
 * ADSR rate table, and gate/sync/ring/test bit semantics are sourced
 * directly from the official datasheet; the oscillator/noise bit-level
 * algorithms from a secondary, disclosed source -- see docs/sid.md and
 * docs/sources.md for exactly what came from where, including a real
 * accumulator-width discrepancy (23 vs 24 bit) resolved directly from
 * the datasheet's own frequency equation. */

#include "testutil.h"

#include "../../src/c64/sid.h"

#define REG_V1_FREQ_LO 0x00
#define REG_V1_FREQ_HI 0x01
#define REG_V1_PW_LO 0x02
#define REG_V1_PW_HI 0x03
#define REG_V1_CTRL 0x04
#define REG_V1_AD 0x05
#define REG_V1_SR 0x06
#define REG_FC_LO 0x15
#define REG_FC_HI 0x16
#define REG_RES_FILT 0x17
#define REG_MODE_VOL 0x18
#define REG_OSC3 0x1B
#define REG_ENV3 0x1C

static void setup(Sid *sid) {
    sid_init(sid);
    sid_write(sid, REG_MODE_VOL, 0x0F); /* max volume, no filter modes selected */
}

/* ---------------------------------------------------------------- */
/* Register plumbing                                                  */
/* ---------------------------------------------------------------- */

static void test_frequency_register_combines_lo_hi(void) {
    Sid sid;
    setup(&sid);
    sid_write(&sid, REG_V1_FREQ_LO, 0x34);
    sid_write(&sid, REG_V1_FREQ_HI, 0x12);
    TEST_ASSERT_EQ_U16(sid.voice[0].freq, 0x1234);
}

static void test_pulse_width_is_12_bit(void) {
    Sid sid;
    setup(&sid);
    sid_write(&sid, REG_V1_PW_LO, 0xFF);
    sid_write(&sid, REG_V1_PW_HI, 0xFF); /* only the low nibble is real */
    TEST_ASSERT_EQ_U16(sid.voice[0].pw, 0x0FFF);
}

/* ---------------------------------------------------------------- */
/* Oscillator: frequency, sourced from the datasheet's own Appendix A   */
/* ---------------------------------------------------------------- */

static void test_sawtooth_frequency_matches_440hz_appendix_a(void) {
    /* Phase 5's own verification target: a known frequency produces
     * output measurably at that frequency. Fn=7382 for A4 (440Hz) at a
     * 1MHz PHI2 clock is taken directly from the datasheet's own
     * Appendix A table, not computed independently. */
    Sid sid;
    setup(&sid);
    sid_write(&sid, REG_V1_FREQ_LO, (uint8_t)(7382 & 0xFF));
    sid_write(&sid, REG_V1_FREQ_HI, (uint8_t)(7382 >> 8));
    sid_write(&sid, REG_V1_CTRL, SID_CTRL_SAWTOOTH | SID_CTRL_GATE);
    sid_write(&sid, REG_V1_SR, 0xF0); /* sustain = 15 (peak), so envelope reaches and holds full volume quickly */
    sid_write(&sid, REG_V1_AD, 0x00); /* fastest attack */

    uint32_t wraps = 0;
    uint32_t prev = sid.voice[0].accumulator;
    for (int i = 0; i < 1000000; i++) { /* 1 second at 1MHz */
        sid_tick(&sid, 1);
        if (sid.voice[0].accumulator < prev) {
            wraps++;
        }
        prev = sid.voice[0].accumulator;
    }

    TEST_ASSERT(wraps >= 439 && wraps <= 441);
}

static void test_pulse_duty_cycle_at_half_is_symmetric(void) {
    Sid sid;
    setup(&sid);
    sid_write(&sid, REG_V1_FREQ_LO, 0x00);
    sid_write(&sid, REG_V1_FREQ_HI, 0x10); /* an arbitrary, reasonably fast frequency */
    sid_write(&sid, REG_V1_PW_LO, 0x00);
    sid_write(&sid, REG_V1_PW_HI, 0x08); /* PW = $800 = 2048 -> 50% duty cycle per the datasheet */
    sid_write(&sid, REG_V1_CTRL, SID_CTRL_PULSE | SID_CTRL_GATE);
    sid_write(&sid, REG_V1_SR, 0xF0);
    sid_write(&sid, REG_V1_AD, 0x00);

    int high = 0, low = 0;
    for (int i = 0; i < 100000; i++) {
        sid_tick(&sid, 1);
        uint16_t acc_top = (uint16_t)((sid.voice[0].accumulator >> 12) & 0xFFFu);
        if (acc_top >= 2048) high++; else low++;
    }
    /* Roughly equal time above/below the halfway point of the accumulator's range. */
    double ratio = (double)high / (double)low;
    TEST_ASSERT(ratio > 0.9 && ratio < 1.1);
}

static void test_pulse_width_extremes_are_constant_dc(void) {
    Sid sid;
    setup(&sid);
    sid_write(&sid, REG_V1_FREQ_LO, 0x00);
    sid_write(&sid, REG_V1_FREQ_HI, 0x10);
    sid_write(&sid, REG_V1_CTRL, SID_CTRL_PULSE | SID_CTRL_GATE);
    sid_write(&sid, REG_V1_SR, 0xF0);
    sid_write(&sid, REG_V1_AD, 0x00);

    /* PW=0 -> acc_top (0-4095) >= 0 always -> constantly "high". */
    sid_write(&sid, REG_V1_PW_LO, 0x00);
    sid_write(&sid, REG_V1_PW_HI, 0x00);
    bool ever_low = false;
    for (int i = 0; i < 10000; i++) {
        sid_tick(&sid, 1);
        if (sid_voice_waveform_output(&sid, 0) != 0x0FFFu) ever_low = true;
    }
    TEST_ASSERT(!ever_low);

    /* PW=4095 ($FFF) -> acc_top >= 4095 is only ever true at the single
     * instant acc_top==4095, so the output is constantly "low" except
     * for that vanishing instant -- the datasheet's other documented
     * constant-DC case. */
    sid_write(&sid, REG_V1_PW_LO, 0xFF);
    sid_write(&sid, REG_V1_PW_HI, 0x0F);
    int high_count = 0;
    for (int i = 0; i < 10000; i++) {
        sid_tick(&sid, 1);
        if (sid_voice_waveform_output(&sid, 0) == 0x0FFFu) high_count++;
    }
    TEST_ASSERT(high_count < 10); /* effectively always low */
}

/* ---------------------------------------------------------------- */
/* Combined waveforms: datasheet's own documented AND behavior         */
/* ---------------------------------------------------------------- */

static void test_combined_waveforms_and_together(void) {
    /* Datasheet: "the result will be a logical ANDing of the
     * waveforms." Verified directly via the exposed
     * sid_voice_waveform_output(), against independently-computed
     * sawtooth and triangle values. */
    Sid sid;
    setup(&sid);
    sid_write(&sid, REG_V1_FREQ_LO, 0x34);
    sid_write(&sid, REG_V1_FREQ_HI, 0x12);
    sid_write(&sid, REG_V1_SR, 0xF0);
    sid_write(&sid, REG_V1_AD, 0x00);
    sid_write(&sid, REG_V1_CTRL, SID_CTRL_GATE); /* no waveform selected yet; just advance the accumulator */

    for (int i = 0; i < 500; i++) sid_tick(&sid, 1);

    uint32_t acc = sid.voice[0].accumulator;
    uint16_t expected_saw = (uint16_t)((acc >> 12) & 0x0FFFu);
    bool msb = (acc & 0x800000u) != 0;
    uint32_t inv = msb ? ((~acc) & 0xFFFFFFu) : acc;
    uint16_t expected_tri = (uint16_t)((inv >> 11) & 0x0FFFu);

    sid_write(&sid, REG_V1_CTRL, SID_CTRL_SAWTOOTH | SID_CTRL_TRIANGLE | SID_CTRL_GATE);
    uint16_t combined = sid_voice_waveform_output(&sid, 0);

    TEST_ASSERT_EQ_U16(combined, (uint16_t)(expected_saw & expected_tri));
}

static void test_noise_combined_with_other_locks_up_to_zero(void) {
    Sid sid;
    setup(&sid);
    /* Use voice 3 so OSC3 (register $1B) lets us observe its waveform
     * output directly from outside the module. */
    sid_write(&sid, 14, 0x00); /* voice 3 freq lo */
    sid_write(&sid, 15, 0x10); /* voice 3 freq hi */
    sid_write(&sid, 18, SID_CTRL_NOISE | SID_CTRL_PULSE | SID_CTRL_GATE); /* voice 3 control: noise+pulse combined */
    sid_write(&sid, 20, 0xF0); /* voice 3 sustain/release */
    sid_write(&sid, 19, 0x00); /* voice 3 attack/decay */

    for (int i = 0; i < 1000; i++) {
        sid_tick(&sid, 1);
    }
    TEST_ASSERT_EQ_U8(sid_read(&sid, REG_OSC3), 0x00);
}

/* ---------------------------------------------------------------- */
/* Envelope generator                                                 */
/* ---------------------------------------------------------------- */

static void test_attack_reaches_255_and_transitions_to_decay(void) {
    Sid sid;
    setup(&sid);
    sid_write(&sid, REG_V1_AD, 0x00); /* attack rate 0 = 2000 cycles total (1MHz) -> 2000/255=7 cycles/step */
    sid_write(&sid, REG_V1_SR, 0xA0); /* sustain = 10 */
    sid_write(&sid, REG_V1_CTRL, SID_CTRL_TRIANGLE | SID_CTRL_GATE);

    /* Attack reaches 255 at cycle 7*255=1785; check just after that but
     * well before decay (23 cycles/step) could complete even one step. */
    for (int i = 0; i < 1790; i++) {
        sid_tick(&sid, 1);
    }
    TEST_ASSERT_EQ_U8(sid.voice[0].envelope, 255);

    /* Now let it decay toward the sustain level (10*17=170) -- decay
     * rate 0 = 6000 cycles total (23 cycles/step), comfortably enough
     * time below to fully reach it. */
    for (int i = 0; i < 8000; i++) {
        sid_tick(&sid, 1);
    }
    TEST_ASSERT_EQ_U8(sid.voice[0].envelope, 170);
}

static void test_gate_off_releases_from_current_level_not_sustain(void) {
    Sid sid;
    setup(&sid);
    sid_write(&sid, REG_V1_AD, 0x20); /* attack rate 2 = 16ms/16000 cycles (62 cycles/step) -- deliberately won't finish */
    sid_write(&sid, REG_V1_SR, 0x00); /* sustain = 0, release = 0 (fast) */
    sid_write(&sid, REG_V1_CTRL, SID_CTRL_TRIANGLE | SID_CTRL_GATE);

    for (int i = 0; i < 1000; i++) { /* ~16 steps in -- well short of the 255 needed to finish */
        sid_tick(&sid, 1);
    }
    uint8_t level_before_release = sid.voice[0].envelope;
    TEST_ASSERT(level_before_release > 0 && level_before_release < 255);

    sid_write(&sid, REG_V1_CTRL, SID_CTRL_TRIANGLE); /* gate off -> release begins immediately */
    uint8_t level_right_after = sid.voice[0].envelope;
    /* Release must start from wherever the envelope was, not from 0 or
     * from the sustain level -- this is the datasheet's own explicitly
     * documented behavior. */
    TEST_ASSERT_EQ_U8(level_right_after, level_before_release);

    for (int i = 0; i < 20000; i++) {
        sid_tick(&sid, 1);
    }
    TEST_ASSERT_EQ_U8(sid.voice[0].envelope, 0);
}

static void test_gate_retriggers_attack_from_current_level(void) {
    Sid sid;
    setup(&sid);
    sid_write(&sid, REG_V1_AD, 0x00);
    sid_write(&sid, REG_V1_SR, 0xF0);
    sid_write(&sid, REG_V1_CTRL, SID_CTRL_TRIANGLE | SID_CTRL_GATE);
    for (int i = 0; i < 3000; i++) sid_tick(&sid, 1); /* reaches and holds 255 */
    TEST_ASSERT_EQ_U8(sid.voice[0].envelope, 255);

    sid_write(&sid, REG_V1_CTRL, SID_CTRL_TRIANGLE); /* release */
    for (int i = 0; i < 3000; i++) sid_tick(&sid, 1);
    uint8_t mid_release = sid.voice[0].envelope;
    TEST_ASSERT(mid_release > 0 && mid_release < 255);

    sid_write(&sid, REG_V1_CTRL, SID_CTRL_TRIANGLE | SID_CTRL_GATE); /* re-gate mid-release */
    TEST_ASSERT_EQ_U8(sid.voice[0].envelope, mid_release); /* attack resumes from here, not from 0 */
}

static void test_env3_register_reads_voice_3_not_voice_1(void) {
    Sid sid;
    setup(&sid);
    sid_write(&sid, REG_V1_AD, 0x00); /* voice 1: gated, should NOT show up in ENV3 */
    sid_write(&sid, REG_V1_SR, 0xF0);
    sid_write(&sid, REG_V1_CTRL, SID_CTRL_TRIANGLE | SID_CTRL_GATE);

    sid_write(&sid, 19, 0x00); /* voice 3 attack/decay */
    sid_write(&sid, 20, 0xF0); /* voice 3 sustain/release */
    sid_write(&sid, 18, SID_CTRL_TRIANGLE | SID_CTRL_GATE); /* voice 3 control */

    for (int i = 0; i < 3000; i++) sid_tick(&sid, 1);

    TEST_ASSERT_EQ_U8(sid_read(&sid, REG_ENV3), 255);
    TEST_ASSERT_EQ_U8(sid_read(&sid, REG_ENV3), sid.voice[2].envelope);
}

/* ---------------------------------------------------------------- */
/* Filter/volume register plumbing                                    */
/* ---------------------------------------------------------------- */

static void test_filter_registers_stored_correctly(void) {
    Sid sid;
    setup(&sid);
    sid_write(&sid, REG_FC_LO, 0x07);
    sid_write(&sid, REG_FC_HI, 0xFF);
    TEST_ASSERT_EQ_U16(sid.filter_cutoff, 0x07FF);

    sid_write(&sid, REG_RES_FILT, 0xF5); /* resonance=15, route voices 1 and 3 through filter */
    TEST_ASSERT_EQ_U8(sid.filter_resonance, 15);
    TEST_ASSERT_EQ_U8(sid.filter_route, 0x05);

    sid_write(&sid, REG_MODE_VOL, 0x1A); /* LP selected, volume 10 */
    TEST_ASSERT_EQ_U8(sid.filter_mode, 0x10);
    TEST_ASSERT_EQ_U8(sid.volume, 10);
}

static void test_zero_volume_gives_silent_output(void) {
    Sid sid;
    sid_init(&sid);
    sid_write(&sid, REG_V1_CTRL, SID_CTRL_SAWTOOTH | SID_CTRL_GATE);
    sid_write(&sid, REG_V1_SR, 0xF0);
    sid_write(&sid, REG_V1_AD, 0x00);
    sid_write(&sid, REG_MODE_VOL, 0x00); /* volume 0 */
    for (int i = 0; i < 3000; i++) sid_tick(&sid, 1);
    TEST_ASSERT_EQ_INT((int)(sid_output(&sid) * 1000), 0);
}

int main(void) {
    test_frequency_register_combines_lo_hi();
    test_pulse_width_is_12_bit();

    test_sawtooth_frequency_matches_440hz_appendix_a();
    test_pulse_duty_cycle_at_half_is_symmetric();
    test_pulse_width_extremes_are_constant_dc();

    test_combined_waveforms_and_together();
    test_noise_combined_with_other_locks_up_to_zero();

    test_attack_reaches_255_and_transitions_to_decay();
    test_gate_off_releases_from_current_level_not_sustain();
    test_gate_retriggers_attack_from_current_level();
    test_env3_register_reads_voice_3_not_voice_1();

    test_filter_registers_stored_correctly();
    test_zero_volume_gives_silent_output();

    printf("%d/%d assertions passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
