/* Hand-written tests for the keyboard matrix + joystick model
 * (Phase 3). See docs/cia.md and docs/sources.md for the sourcing and
 * open-verification caveats on the exact key layout. */

#include "testutil.h"

#include "../../src/c64/keyboard.h"

static void test_single_key_press_pulls_its_row_low(void) {
    KeyboardMatrix kb;
    keyboard_matrix_init(&kb);
    keyboard_matrix_set_key(&kb, C64KEY_A, true); /* pa=1, pb=2 */

    /* Select only column (PA bit) 1 -- every other column bit held high. */
    uint8_t select = (uint8_t)~(1u << 1);
    uint8_t pulldown = keyboard_matrix_sense_pulldown(&kb, select);
    TEST_ASSERT_EQ_U8(pulldown, (uint8_t)(1u << 2));
}

static void test_unselected_column_does_not_contribute(void) {
    KeyboardMatrix kb;
    keyboard_matrix_init(&kb);
    keyboard_matrix_set_key(&kb, C64KEY_A, true); /* pa=1 */

    uint8_t select_all_but_1 = 0xFF; /* no columns selected at all */
    TEST_ASSERT_EQ_U8(keyboard_matrix_sense_pulldown(&kb, select_all_but_1), 0x00);
}

static void test_multiple_keys_same_column_combine(void) {
    KeyboardMatrix kb;
    keyboard_matrix_init(&kb);
    keyboard_matrix_set_key(&kb, C64KEY_3, true); /* pa=1, pb=0 */
    keyboard_matrix_set_key(&kb, C64KEY_W, true); /* pa=1, pb=1 */

    uint8_t select = (uint8_t)~(1u << 1);
    uint8_t pulldown = keyboard_matrix_sense_pulldown(&kb, select);
    TEST_ASSERT_EQ_U8(pulldown, 0x03);
}

static void test_scanning_all_columns_at_once_ors_every_row(void) {
    KeyboardMatrix kb;
    keyboard_matrix_init(&kb);
    keyboard_matrix_set_key(&kb, C64KEY_RETURN, true); /* pa=0, pb=1 */
    keyboard_matrix_set_key(&kb, C64KEY_SPACE, true);  /* pa=7, pb=4 */

    uint8_t pulldown = keyboard_matrix_sense_pulldown(&kb, 0x00); /* all columns selected */
    TEST_ASSERT_EQ_U8(pulldown, (uint8_t)((1u << 1) | (1u << 4)));
}

static void test_release_clears_state(void) {
    KeyboardMatrix kb;
    keyboard_matrix_init(&kb);
    keyboard_matrix_set_key(&kb, C64KEY_Q, true);
    TEST_ASSERT(keyboard_matrix_is_key_down(&kb, C64KEY_Q));
    keyboard_matrix_set_key(&kb, C64KEY_Q, false);
    TEST_ASSERT(!keyboard_matrix_is_key_down(&kb, C64KEY_Q));
    TEST_ASSERT_EQ_U8(keyboard_matrix_sense_pulldown(&kb, 0x00), 0x00);
}

static void test_key_encoding_matches_documented_matrix_position(void) {
    /* Spot-check a handful of positions against the sourced table
     * (docs/sources.md) rather than trusting the enum values alone. */
    TEST_ASSERT_EQ_INT((int)C64KEY_INS_DEL, 0);
    TEST_ASSERT_EQ_INT((int)C64KEY_RUN_STOP, 63);
    TEST_ASSERT_EQ_INT((int)C64KEY_LSHIFT / 8, 1);
    TEST_ASSERT_EQ_INT((int)C64KEY_LSHIFT % 8, 7);
    TEST_ASSERT_EQ_INT((int)C64KEY_RSHIFT / 8, 6);
    TEST_ASSERT_EQ_INT((int)C64KEY_RSHIFT % 8, 4);
}

static void test_joystick_pulldown_bit_order(void) {
    Joystick js;
    joystick_init(&js);
    js.up = true;
    js.fire = true;
    TEST_ASSERT_EQ_U8(joystick_pulldown(&js), 0x01u | 0x10u);

    joystick_init(&js);
    js.left = true;
    js.right = true; /* a real diagonal microswitch pair can both be closed */
    TEST_ASSERT_EQ_U8(joystick_pulldown(&js), 0x04u | 0x08u);
}

int main(void) {
    test_single_key_press_pulls_its_row_low();
    test_unselected_column_does_not_contribute();
    test_multiple_keys_same_column_combine();
    test_scanning_all_columns_at_once_ors_every_row();
    test_release_clears_state();
    test_key_encoding_matches_documented_matrix_position();
    test_joystick_pulldown_bit_order();

    printf("%d/%d assertions passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
