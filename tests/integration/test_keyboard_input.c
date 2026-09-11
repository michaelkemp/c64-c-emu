/* Closes the keyboard-matrix-layout empirical cross-check that
 * docs/cia.md, docs/machine.md, CLAUDE.md, and README.md have flagged
 * as open since Phase 3: does a synthetic key press, routed through
 * the real KeyboardMatrix -> CIA1 -> IRQ-driven KERNAL keyboard scan
 * (Phase 6's Machine, not Phase 2's raw CPU that tests/integration/
 * test_boot.c uses -- the KERNAL's keyboard scan is interrupt-driven,
 * tied to the jiffy IRQ, so it genuinely needs CIA1 + IRQ delivery
 * wired for real), produce the real KERNAL's own correct character in
 * screen memory?
 *
 * Types a representative string spanning 8 different matrix positions
 * across 5 of the matrix's 8 rows/columns (not an exhaustive all-64
 * sweep -- a disclosed, reasonable-confidence sample, not a claim of
 * full coverage) and confirms the exact expected screen-code sequence
 * appears, contiguously, in screen memory -- the same style of check
 * test_boot.c uses for "READY.".
 *
 * Needs the user's own staged ROMs (tier 3, docs/testing-strategy.md);
 * SKIPs (exit 0) rather than failing the build if they aren't present.
 *
 * Per docs/peripherals.md's own note: a synthetic key press must be
 * held long enough for the KERNAL's real ~60Hz jiffy-tied keyboard scan
 * to actually see it, or it can land between two scans and be silently
 * missed -- verified concretely here (typing a known string and reading
 * it back), not assumed. */

#include <stdio.h>
#include <string.h>

#include "../../src/c64/machine.h"

static const uint8_t READY_SCREEN_CODES[] = {'R' - 64, 'E' - 64, 'A' - 64, 'D' - 64, 'Y' - 64, '.'};

/* Presses then releases one key, holding each phase for several real
 * jiffy periods so the KERNAL's own interrupt-driven scan (and its
 * debounce) unambiguously sees a clean press-then-release, not two
 * keys blurring together. */
static void type_key(Machine *m, C64Key key) {
    keyboard_matrix_set_key(&m->keyboard, key, true);
    machine_run_cycles(m, (uint64_t)m->cia1.ta_latch * 3);
    keyboard_matrix_set_key(&m->keyboard, key, false);
    machine_run_cycles(m, (uint64_t)m->cia1.ta_latch * 3);
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

    const uint64_t max_cycles_to_boot = 5000000;
    bool found_ready = false;
    while (m.total_cycles < max_cycles_to_boot) {
        machine_cycle(&m);
        for (int pos = 0; pos <= 1000 - (int)sizeof(READY_SCREEN_CODES); pos++) {
            if (memcmp(&m.mem.ram[0x0400 + pos], READY_SCREEN_CODES, sizeof(READY_SCREEN_CODES)) == 0) {
                found_ready = true;
                break;
            }
        }
        if (found_ready) {
            break;
        }
    }
    if (!found_ready) {
        printf("FAIL: never reached the real \"READY.\" prompt within %llu cycles.\n",
               (unsigned long long)max_cycles_to_boot);
        return 1;
    }
    printf("Reached real \"READY.\" prompt at cycle %llu\n", (unsigned long long)m.total_cycles);

    /* Types "A1Z5MP ," -- 8 keys spanning matrix rows 0(space),1(A,Z),
     * 2(5),4(M),5(P,comma),7(1), and matrix columns 0,1,2,4,5,7, per
     * src/c64/keyboard.h's C64Key encoding. Unshifted letters/digits/
     * most punctuation map to their own well-established screen codes
     * (A-Z -> 1-26, digits/most punctuation unchanged from ASCII -- see
     * test_boot.c's own comment on this same fact). */
    static const C64Key keys_to_type[] = {C64KEY_A,     C64KEY_1, C64KEY_Z,     C64KEY_5,
                                           C64KEY_M,     C64KEY_P, C64KEY_SPACE, C64KEY_COMMA};
    static const uint8_t expected_codes[] = {1, '1', 26, '5', 13, 16, 32, ','};
    _Static_assert(sizeof(keys_to_type) / sizeof(keys_to_type[0]) == sizeof(expected_codes),
                   "one expected screen code per typed key");

    for (size_t i = 0; i < sizeof(keys_to_type) / sizeof(keys_to_type[0]); i++) {
        type_key(&m, keys_to_type[i]);
    }

    bool found_typed_sequence = false;
    int found_pos = -1;
    for (int pos = 0; pos <= 1000 - (int)sizeof(expected_codes); pos++) {
        if (memcmp(&m.mem.ram[0x0400 + pos], expected_codes, sizeof(expected_codes)) == 0) {
            found_typed_sequence = true;
            found_pos = pos;
            break;
        }
    }

    if (!found_typed_sequence) {
        printf("FAIL: typing \"A1Z5MP ,\" through the real KeyboardMatrix -> CIA1 -> IRQ-driven "
               "KERNAL keyboard scan did not produce the expected screen-code sequence anywhere "
               "in screen memory -- either src/c64/keyboard.h's matrix layout has a wrong "
               "position among these 8 keys, or a press wasn't held long enough for the real "
               "KERNAL scan to see it.\n");
        return 1;
    }

    printf("PASS: typing \"A1Z5MP ,\" (8 keys spanning matrix rows 0,1,2,4,5,7 and columns "
           "0,1,2,4,5,7) through the real KeyboardMatrix -> CIA1 -> IRQ-driven KERNAL keyboard "
           "scan produced the exact expected screen-code sequence at screen position %d. "
           "src/c64/keyboard.h's matrix layout is confirmed correct for these 8 positions "
           "against real hardware behavior (a representative sample, not an exhaustive sweep "
           "of all 64 positions).\n",
           found_pos);
    return 0;
}
