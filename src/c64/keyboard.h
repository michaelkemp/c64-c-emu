#ifndef C64EMU_C64_KEYBOARD_H
#define C64EMU_C64_KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

/* The C64's 8x8 keyboard matrix, wired to CIA1's Port A (column
 * select, output, active low) and Port B (row sense, input, active
 * low) -- see docs/cia.md and docs/sources.md. Each key's enum value
 * IS its (pa_bit, pb_bit) position, encoded as pa_bit*8 + pb_bit, so
 * the position is always recoverable from the enum without a separate
 * lookup table.
 *
 * IMPORTANT: this table's specific key-to-position layout is sourced
 * from http://sta.c64.org/cbm64kbdlay.html (see docs/sources.md) --
 * docs/cia.md explicitly flags this as one of (at least) two
 * commonly-cited community layouts, and it has NOT been independently
 * verified empirically against real KERNAL/hardware (that verification
 * needs real staged ROMs, which this project doesn't have -- see
 * docs/memory-map.md's Phase 2 status). Treat this as "sourced, not
 * yet cross-checked" until that verification happens.
 *
 * The RESTORE key is deliberately not part of this matrix -- on real
 * hardware it isn't a matrix position at all, it wires directly into
 * CIA2's NMI-generating path. That's Phase 6 territory. */
typedef enum {
    C64KEY_INS_DEL = 0 * 8 + 0,
    C64KEY_RETURN = 0 * 8 + 1,
    C64KEY_CRSR_LR = 0 * 8 + 2, /* cursor left/right -- one physical key, SHIFT picks the direction */
    C64KEY_F7 = 0 * 8 + 3,
    C64KEY_F1 = 0 * 8 + 4,
    C64KEY_F3 = 0 * 8 + 5,
    C64KEY_F5 = 0 * 8 + 6,
    C64KEY_CRSR_UD = 0 * 8 + 7, /* cursor up/down -- likewise one physical key */

    C64KEY_3 = 1 * 8 + 0,
    C64KEY_W = 1 * 8 + 1,
    C64KEY_A = 1 * 8 + 2,
    C64KEY_4 = 1 * 8 + 3,
    C64KEY_Z = 1 * 8 + 4,
    C64KEY_S = 1 * 8 + 5,
    C64KEY_E = 1 * 8 + 6,
    C64KEY_LSHIFT = 1 * 8 + 7,

    C64KEY_5 = 2 * 8 + 0,
    C64KEY_R = 2 * 8 + 1,
    C64KEY_D = 2 * 8 + 2,
    C64KEY_6 = 2 * 8 + 3,
    C64KEY_C = 2 * 8 + 4,
    C64KEY_F = 2 * 8 + 5,
    C64KEY_T = 2 * 8 + 6,
    C64KEY_X = 2 * 8 + 7,

    C64KEY_7 = 3 * 8 + 0,
    C64KEY_Y = 3 * 8 + 1,
    C64KEY_G = 3 * 8 + 2,
    C64KEY_8 = 3 * 8 + 3,
    C64KEY_B = 3 * 8 + 4,
    C64KEY_H = 3 * 8 + 5,
    C64KEY_U = 3 * 8 + 6,
    C64KEY_V = 3 * 8 + 7,

    C64KEY_9 = 4 * 8 + 0,
    C64KEY_I = 4 * 8 + 1,
    C64KEY_J = 4 * 8 + 2,
    C64KEY_0 = 4 * 8 + 3,
    C64KEY_M = 4 * 8 + 4,
    C64KEY_K = 4 * 8 + 5,
    C64KEY_O = 4 * 8 + 6,
    C64KEY_N = 4 * 8 + 7,

    C64KEY_PLUS = 5 * 8 + 0,
    C64KEY_P = 5 * 8 + 1,
    C64KEY_L = 5 * 8 + 2,
    C64KEY_MINUS = 5 * 8 + 3,
    C64KEY_PERIOD = 5 * 8 + 4,
    C64KEY_COLON = 5 * 8 + 5,
    C64KEY_AT = 5 * 8 + 6,
    C64KEY_COMMA = 5 * 8 + 7,

    C64KEY_POUND = 6 * 8 + 0,
    C64KEY_ASTERISK = 6 * 8 + 1,
    C64KEY_SEMICOLON = 6 * 8 + 2,
    C64KEY_CLR_HOME = 6 * 8 + 3,
    C64KEY_RSHIFT = 6 * 8 + 4,
    C64KEY_EQUALS = 6 * 8 + 5,
    C64KEY_UP_ARROW = 6 * 8 + 6,
    C64KEY_SLASH = 6 * 8 + 7,

    C64KEY_1 = 7 * 8 + 0,
    C64KEY_LEFT_ARROW = 7 * 8 + 1,
    C64KEY_CTRL = 7 * 8 + 2,
    C64KEY_2 = 7 * 8 + 3,
    C64KEY_SPACE = 7 * 8 + 4,
    C64KEY_COMMODORE = 7 * 8 + 5,
    C64KEY_Q = 7 * 8 + 6,
    C64KEY_RUN_STOP = 7 * 8 + 7
} C64Key;

typedef struct KeyboardMatrix {
    bool pressed[8][8]; /* [pa_bit][pb_bit], see C64Key's own encoding above */
} KeyboardMatrix;

void keyboard_matrix_init(KeyboardMatrix *kb);
void keyboard_matrix_set_key(KeyboardMatrix *kb, C64Key key, bool down);
bool keyboard_matrix_is_key_down(const KeyboardMatrix *kb, C64Key key);

/* select_port_value: the current effective value of the column-select
 * port (Port A), 0 bits meaning that column is selected -- see
 * cia_effective_port_a(). Returns the pulldown mask to apply to the
 * row-sense port (Port B) via cia_set_port_b_pulldown(): a 1 bit means
 * at least one currently-selected column has that row's key held. */
uint8_t keyboard_matrix_sense_pulldown(const KeyboardMatrix *kb, uint8_t select_port_value);

/* A simple digital joystick (up/down/left/right/fire). Real hardware:
 * joystick port 2 wires to CIA1 Port A, port 1 to CIA1 Port B; single-
 * joystick software conventionally expects port 2. If a caller maps
 * multiple physical keys (e.g. numpad) onto the same joystick
 * direction, it must reference-count overlapping presses itself --
 * this struct only tracks final direction/fire state, one bool each,
 * per docs/cia.md's own note on that (a Phase 7/SDL2 concern, not this
 * module's). */
typedef struct Joystick {
    bool up, down, left, right, fire;
} Joystick;

void joystick_init(Joystick *js);

/* The active-low pulldown mask in real hardware bit order: bit0=up,
 * bit1=down, bit2=left, bit3=right, bit4=fire. OR this into whichever
 * CIA1 port (A for joystick 2, B for joystick 1) via
 * cia_set_port_a/b_pulldown() -- combined with any keyboard pulldown
 * already targeting that same port, since both are simple wired-AND
 * contributions to the same physical pins. */
uint8_t joystick_pulldown(const Joystick *js);

#endif /* C64EMU_C64_KEYBOARD_H */
