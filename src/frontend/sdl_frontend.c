/* Phase 7: SDL2 peripherals -- screen, audio, keyboard, joystick. See
 * docs/peripherals.md. This is the ONLY place in the project that
 * includes SDL2 -- the emulation core (src/cpu, src/c64) has zero
 * knowledge of it, per docs/peripherals.md's own stated boundary.
 *
 * SDL version/API actually used (docs/peripherals.md's "Known gaps"
 * asks this be disclosed): SDL2, and the queue-based audio API
 * (SDL_QueueAudio), not a callback -- chosen because it lets the main
 * thread itself be the single place that both advances the machine and
 * decides how much audio to generate, with no cross-thread ring buffer
 * needed. The audio device's own real playback rate is what paces the
 * whole machine (docs/machine.md's pacing strategy 2, now that this
 * phase exists): every loop iteration tops the queued audio back up to
 * a small target buffer (50ms) by running exactly as many real PHI2
 * cycles as that many new samples need, so video and input naturally
 * track real time too -- nothing here uses a host wall-clock timer
 * directly. */

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "../c64/machine.h"
#include "../c64/palette.h"

#define AUDIO_SAMPLE_RATE 44100
#define AUDIO_TARGET_BUFFER_SECONDS 0.05 /* 50ms of headroom -- docs/peripherals.md's suggested starting point */
#define AUDIO_AMPLITUDE 20000.0          /* headroom below INT16_MAX for a resonant filter's occasional overshoot */

/* ------------------------------------------------------------------ */
/* Keyboard: SDL2 scancode -> C64 matrix position                      */
/* ------------------------------------------------------------------ */

/* Sourced from the same key-position layout as src/c64/keyboard.h
 * (http://sta.c64.org/cbm64kbdlay.html, see docs/sources.md), mapped
 * onto a standard US PC keyboard's physical positions where a direct
 * equivalent exists. Genuinely approximate/disclosed choices, not a
 * primary-sourced fact -- see docs/peripherals.md's "Known gaps":
 * - Cursor Left/Up are emulated as the real Cursor Right/Down matrix
 *   position plus a synthetic LSHIFT, exactly like real hardware's own
 *   single physical cursor keys (see src/c64/keyboard.h's own comment).
 * - F2/F4/F6/F8 (real hardware: SHIFT+F1/F3/F5/F7) are not separately
 *   mapped -- only F1/F3/F5/F7 are wired here.
 * - Left Alt -> Commodore key, Escape -> RUN/STOP, backtick -> the C64's
 *   own dedicated LEFT ARROW character key (distinct from the cursor
 *   arrows), Page Up -> RESTORE (not a matrix position -- see
 *   machine_set_restore_key()). */
static C64Key scancode_to_c64key(SDL_Scancode sc, bool *out_is_cursor_left, bool *out_is_cursor_up) {
    *out_is_cursor_left = false;
    *out_is_cursor_up = false;
    switch (sc) {
        case SDL_SCANCODE_1: return C64KEY_1;
        case SDL_SCANCODE_2: return C64KEY_2;
        case SDL_SCANCODE_3: return C64KEY_3;
        case SDL_SCANCODE_4: return C64KEY_4;
        case SDL_SCANCODE_5: return C64KEY_5;
        case SDL_SCANCODE_6: return C64KEY_6;
        case SDL_SCANCODE_7: return C64KEY_7;
        case SDL_SCANCODE_8: return C64KEY_8;
        case SDL_SCANCODE_9: return C64KEY_9;
        case SDL_SCANCODE_0: return C64KEY_0;

        case SDL_SCANCODE_A: return C64KEY_A;
        case SDL_SCANCODE_B: return C64KEY_B;
        case SDL_SCANCODE_C: return C64KEY_C;
        case SDL_SCANCODE_D: return C64KEY_D;
        case SDL_SCANCODE_E: return C64KEY_E;
        case SDL_SCANCODE_F: return C64KEY_F;
        case SDL_SCANCODE_G: return C64KEY_G;
        case SDL_SCANCODE_H: return C64KEY_H;
        case SDL_SCANCODE_I: return C64KEY_I;
        case SDL_SCANCODE_J: return C64KEY_J;
        case SDL_SCANCODE_K: return C64KEY_K;
        case SDL_SCANCODE_L: return C64KEY_L;
        case SDL_SCANCODE_M: return C64KEY_M;
        case SDL_SCANCODE_N: return C64KEY_N;
        case SDL_SCANCODE_O: return C64KEY_O;
        case SDL_SCANCODE_P: return C64KEY_P;
        case SDL_SCANCODE_Q: return C64KEY_Q;
        case SDL_SCANCODE_R: return C64KEY_R;
        case SDL_SCANCODE_S: return C64KEY_S;
        case SDL_SCANCODE_T: return C64KEY_T;
        case SDL_SCANCODE_U: return C64KEY_U;
        case SDL_SCANCODE_V: return C64KEY_V;
        case SDL_SCANCODE_W: return C64KEY_W;
        case SDL_SCANCODE_X: return C64KEY_X;
        case SDL_SCANCODE_Y: return C64KEY_Y;
        case SDL_SCANCODE_Z: return C64KEY_Z;

        case SDL_SCANCODE_RETURN: return C64KEY_RETURN;
        case SDL_SCANCODE_SPACE: return C64KEY_SPACE;
        case SDL_SCANCODE_BACKSPACE: return C64KEY_INS_DEL;
        case SDL_SCANCODE_HOME: return C64KEY_CLR_HOME;
        case SDL_SCANCODE_ESCAPE: return C64KEY_RUN_STOP;
        case SDL_SCANCODE_LALT: return C64KEY_COMMODORE;
        case SDL_SCANCODE_LCTRL: return C64KEY_CTRL;
        case SDL_SCANCODE_LSHIFT: return C64KEY_LSHIFT;
        case SDL_SCANCODE_RSHIFT: return C64KEY_RSHIFT;
        case SDL_SCANCODE_GRAVE: return C64KEY_LEFT_ARROW;

        case SDL_SCANCODE_F1: return C64KEY_F1;
        case SDL_SCANCODE_F3: return C64KEY_F3;
        case SDL_SCANCODE_F5: return C64KEY_F5;
        case SDL_SCANCODE_F7: return C64KEY_F7;

        case SDL_SCANCODE_MINUS: return C64KEY_MINUS;
        case SDL_SCANCODE_EQUALS: return C64KEY_EQUALS;
        case SDL_SCANCODE_LEFTBRACKET: return C64KEY_AT;
        case SDL_SCANCODE_RIGHTBRACKET: return C64KEY_ASTERISK;
        case SDL_SCANCODE_SEMICOLON: return C64KEY_SEMICOLON;
        case SDL_SCANCODE_APOSTROPHE: return C64KEY_COLON;
        case SDL_SCANCODE_COMMA: return C64KEY_COMMA;
        case SDL_SCANCODE_PERIOD: return C64KEY_PERIOD;
        case SDL_SCANCODE_SLASH: return C64KEY_SLASH;
        case SDL_SCANCODE_BACKSLASH: return C64KEY_POUND;

        /* No standard PC keyboard has a physical key at the real C64's
         * up-arrow position (BASIC V2's exponentiation operator, `^`)
         * -- unlike every other punctuation mapping above, this one has
         * no honest "same physical position" answer, so it borrows the
         * otherwise-unused plain Insert key (distinct from Backspace,
         * already doing double duty as the real INST/DEL key above).
         * Shift+Insert is already claimed by the paste-as-typing
         * shortcut and is intercepted before reaching this function, so
         * there's no conflict -- only a bare, unshifted Insert press
         * reaches this mapping. */
        case SDL_SCANCODE_INSERT: return C64KEY_UP_ARROW;

        case SDL_SCANCODE_RIGHT: return C64KEY_CRSR_LR;
        case SDL_SCANCODE_LEFT:
            *out_is_cursor_left = true;
            return C64KEY_CRSR_LR;
        case SDL_SCANCODE_DOWN: return C64KEY_CRSR_UD;
        case SDL_SCANCODE_UP:
            *out_is_cursor_up = true;
            return C64KEY_CRSR_UD;

        default: return (C64Key)-1; /* not mapped */
    }
}

/* LSHIFT is asserted if the real Left Shift key is held OR the cursor-
 * key emulation above needs it -- see the comment on scancode_to_c64key. */
typedef struct KeyState {
    bool real_lshift_down;
    bool cursor_left_down;
    bool cursor_up_down;
} KeyState;

static void update_lshift(Machine *m, const KeyState *ks) {
    keyboard_matrix_set_key(&m->keyboard, C64KEY_LSHIFT,
                             ks->real_lshift_down || ks->cursor_left_down || ks->cursor_up_down);
}

static void handle_key_event(Machine *m, KeyState *ks, SDL_Scancode sc, bool down) {
    if (sc == SDL_SCANCODE_PAGEUP) {
        machine_set_restore_key(m, down);
        return;
    }

    bool is_cursor_left = false;
    bool is_cursor_up = false;
    C64Key key = scancode_to_c64key(sc, &is_cursor_left, &is_cursor_up);
    if ((int)key < 0) {
        return; /* unmapped host key -- ignored */
    }

    if (is_cursor_left) {
        ks->cursor_left_down = down;
        keyboard_matrix_set_key(&m->keyboard, C64KEY_CRSR_LR, down);
        update_lshift(m, ks);
        return;
    }
    if (is_cursor_up) {
        ks->cursor_up_down = down;
        keyboard_matrix_set_key(&m->keyboard, C64KEY_CRSR_UD, down);
        update_lshift(m, ks);
        return;
    }
    if (key == C64KEY_LSHIFT) {
        ks->real_lshift_down = down;
        update_lshift(m, ks);
        return;
    }

    keyboard_matrix_set_key(&m->keyboard, key, down);
}

/* ------------------------------------------------------------------ */
/* Paste-as-typing: Ctrl+V / Shift+Insert types the clipboard's text as  */
/* a sequence of synthetic keypresses, exactly the convenience input     */
/* method docs/peripherals.md's Keyboard section anticipates ("a real,  */
/* common situation... hold each synthetic keypress for long enough     */
/* that the real KERNAL's interrupt-driven keyboard scan can actually   */
/* see it"). Built for the same reason as that section says: reliably   */
/* typing whole BASIC program listings by hand is slow and error-prone. */
/* ------------------------------------------------------------------ */

/* ASCII -> C64Key (+ whether LSHIFT is needed), for the characters a
 * pasted BASIC listing is realistically made of. Best-effort/disclosed
 * approximate for punctuation shift-mappings not central to typing
 * BASIC (docs/peripherals.md's own "Known gaps" convention) -- letters,
 * digits, space, and RETURN are exact; a handful of common symbols
 * (colon, semicolon, comma, period, quote, plus/minus, equals,
 * parentheses, slash, dollar, less-than/greater-than) are mapped to
 * their real C64 keyboard position -- `<`/`>` were missing from this
 * list's first version and silently dropped every comparison operator
 * out of a real, user-pasted BASIC program (`IF X<24 OR X>220`) before
 * being added; real hardware: SHIFT+comma/SHIFT+period. `$` was also
 * wrong in this list's first version -- mapped to the physical POUND
 * key, which actually types `£` (verified directly against the real
 * KERNAL: unshifted POUND produces screen code $1C, the £ glyph). Real
 * `$` is SHIFT+4 (screen code $24), following the same classic PETSCII
 * shifted-digit-row convention as `(` (SHIFT+8) and `)` (SHIFT+9)
 * above -- confirmed the same way. `^` was missing entirely from this
 * list's first version -- real Commodore BASIC V2's exponentiation
 * operator is the physical UP-ARROW key (unshifted), which is exactly
 * what a modern pasted listing's ASCII caret `^` represents; without
 * this mapping every `^` in a pasted program (e.g.
 * `programs/basic/sprite_test2.bas`'s `2^N`/`2^(7-BP)`) was silently
 * dropped, corrupting the expression and producing a real "SYNTAX
 * ERROR" when the mangled line was tokenized. Lowercase input maps to
 * the SAME
 * (unshifted) key as its
 * uppercase form, matching how a real C64 keyboard has only one set of
 * letter keys (producing uppercase PETSCII by default) -- there is no
 * real "lowercase" to type on a stock C64 in this mode. Unmapped
 * characters are silently skipped. */
static bool char_to_c64key(char c, C64Key *out_key, bool *out_shift) {
    *out_shift = false;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');

    switch (c) {
        case 'A': *out_key = C64KEY_A; return true;
        case 'B': *out_key = C64KEY_B; return true;
        case 'C': *out_key = C64KEY_C; return true;
        case 'D': *out_key = C64KEY_D; return true;
        case 'E': *out_key = C64KEY_E; return true;
        case 'F': *out_key = C64KEY_F; return true;
        case 'G': *out_key = C64KEY_G; return true;
        case 'H': *out_key = C64KEY_H; return true;
        case 'I': *out_key = C64KEY_I; return true;
        case 'J': *out_key = C64KEY_J; return true;
        case 'K': *out_key = C64KEY_K; return true;
        case 'L': *out_key = C64KEY_L; return true;
        case 'M': *out_key = C64KEY_M; return true;
        case 'N': *out_key = C64KEY_N; return true;
        case 'O': *out_key = C64KEY_O; return true;
        case 'P': *out_key = C64KEY_P; return true;
        case 'Q': *out_key = C64KEY_Q; return true;
        case 'R': *out_key = C64KEY_R; return true;
        case 'S': *out_key = C64KEY_S; return true;
        case 'T': *out_key = C64KEY_T; return true;
        case 'U': *out_key = C64KEY_U; return true;
        case 'V': *out_key = C64KEY_V; return true;
        case 'W': *out_key = C64KEY_W; return true;
        case 'X': *out_key = C64KEY_X; return true;
        case 'Y': *out_key = C64KEY_Y; return true;
        case 'Z': *out_key = C64KEY_Z; return true;

        case '0': *out_key = C64KEY_0; return true;
        case '1': *out_key = C64KEY_1; return true;
        case '2': *out_key = C64KEY_2; return true;
        case '3': *out_key = C64KEY_3; return true;
        case '4': *out_key = C64KEY_4; return true;
        case '5': *out_key = C64KEY_5; return true;
        case '6': *out_key = C64KEY_6; return true;
        case '7': *out_key = C64KEY_7; return true;
        case '8': *out_key = C64KEY_8; return true;
        case '9': *out_key = C64KEY_9; return true;

        case ' ': *out_key = C64KEY_SPACE; return true;
        case '\n': *out_key = C64KEY_RETURN; return true;
        case '\r': return false; /* skip -- \r\n line endings would otherwise double-RETURN */

        case ':': *out_key = C64KEY_COLON; return true;
        case ';': *out_key = C64KEY_SEMICOLON; return true;
        case ',': *out_key = C64KEY_COMMA; return true;
        case '.': *out_key = C64KEY_PERIOD; return true;
        case '/': *out_key = C64KEY_SLASH; return true;
        case '+': *out_key = C64KEY_PLUS; return true;
        case '-': *out_key = C64KEY_MINUS; return true;
        case '=': *out_key = C64KEY_EQUALS; return true;
        case '*': *out_key = C64KEY_ASTERISK; return true;
        case '$': *out_key = C64KEY_4; *out_shift = true; return true;
        case '(': *out_key = C64KEY_8; *out_shift = true; return true;
        case ')': *out_key = C64KEY_9; *out_shift = true; return true;
        case '"': *out_key = C64KEY_2; *out_shift = true; return true;
        case '^': *out_key = C64KEY_UP_ARROW; return true; /* BASIC V2's exponentiation operator */
        case '<': *out_key = C64KEY_COMMA; *out_shift = true; return true;
        case '>': *out_key = C64KEY_PERIOD; *out_shift = true; return true;

        default: return false;
    }
}

/* State machine for typing out a pasted string one key at a time, at a
 * cadence tied to real machine cycles (not host wall-clock time, so it
 * stays correct however fast/slow the emulation itself is running).
 * Each character: press, hold for HOLD_CYCLES real PHI2 cycles, release,
 * wait GAP_CYCLES more before the next press -- comfortably longer than
 * one real jiffy period (~16421 cycles, docs/cia.md) so the KERNAL's
 * own interrupt-driven scan can't miss it between two scans, per
 * docs/peripherals.md's own explicit warning about this. */
typedef struct PasteState {
    char *text;   /* malloc'd by SDL_GetClipboardText(), freed when done */
    size_t pos;
    bool key_down;
    bool shift_was_down_before;
    bool current_key_needs_shift;
    C64Key current_key;
    uint64_t next_transition_cycle;
} PasteState;

/* Hold/gap duration, in real PHI2 cycles, derived from the machine's
 * OWN currently-configured jiffy period (CIA1 Timer A's reload value --
 * see docs/cia.md) rather than a hardcoded constant, so this stays
 * correct even if something reprograms the timer. 1.5 jiffy periods to
 * hold (comfortably more than the one full period needed to guarantee
 * the KERNAL's scan catches it) plus 1 period as a release gap (so a
 * scan can register the release before the next press) -- 2.5 periods
 * per character total, versus this feature's first version's 6 (a
 * needlessly conservative 3+3), after the user found it typed too
 * slowly for pasting a real program. */
static uint32_t paste_hold_cycles(const Machine *m) {
    return m->cia1.ta_latch + m->cia1.ta_latch / 2u;
}
static uint32_t paste_gap_cycles(const Machine *m) {
    return m->cia1.ta_latch;
}

static void paste_start(Machine *m, PasteState *ps, KeyState *ks) {
    if (ps->text) {
        /* A previous paste was retriggered mid-flight (e.g. the user
         * pressed Ctrl+V again before the first one finished) -- if it
         * had a synthetic key physically "held" at this exact moment,
         * release it now. Without this it stays stuck down on the real
         * C64 keyboard matrix forever (until some later coincidence
         * happens to press the same key again), silently corrupting
         * every subsequent keypress's scan result. This was a real,
         * user-found bug: pasting the same file twice in a row produced
         * visibly scrambled/wrong characters. */
        if (ps->key_down) {
            keyboard_matrix_set_key(&m->keyboard, ps->current_key, false);
            if (ps->current_key_needs_shift) {
                ks->real_lshift_down = ps->shift_was_down_before;
                update_lshift(m, ks);
            }
        }
        SDL_free(ps->text);
        ps->text = NULL;
    }
    char *clip = SDL_GetClipboardText();
    if (clip == NULL || clip[0] == '\0') {
        if (clip) SDL_free(clip);
        return;
    }
    ps->text = clip;
    ps->pos = 0;
    ps->key_down = false;
    ps->shift_was_down_before = false; /* see the CTRL/SHIFT clearing below -- this trigger's own
                                         * modifier is never meant to still be "held" once typing starts */
    ps->next_transition_cycle = m->total_cycles; /* act immediately on the next tick */

    /* The trigger combo itself (Ctrl+V or Shift+Insert) has, by this
     * point, already pressed CTRL and/or LSHIFT on the real C64
     * keyboard matrix via the normal handle_key_event() path for their
     * own KEYDOWN events -- their matching KEYUP events won't arrive
     * until the host key is physically released, which can easily
     * still be true when the FIRST synthetic character starts typing
     * (e.g. CTRL+1 selects a color code on real hardware, not the
     * digit '1'). Force them off here so the very first pasted
     * character isn't corrupted by the shortcut used to start it. */
    keyboard_matrix_set_key(&m->keyboard, C64KEY_CTRL, false);
    keyboard_matrix_set_key(&m->keyboard, C64KEY_LSHIFT, false);
    keyboard_matrix_set_key(&m->keyboard, C64KEY_RSHIFT, false);
    ks->real_lshift_down = false;
    ks->cursor_left_down = false;
    ks->cursor_up_down = false;
}

static void paste_tick(Machine *m, PasteState *ps, KeyState *ks) {
    if (!ps->text) {
        return;
    }
    if (m->total_cycles < ps->next_transition_cycle) {
        return;
    }
    if (ps->key_down) {
        keyboard_matrix_set_key(&m->keyboard, ps->current_key, false);
        if (ps->current_key_needs_shift) {
            ks->real_lshift_down = ps->shift_was_down_before;
            update_lshift(m, ks);
        }
        ps->key_down = false;
        ps->next_transition_cycle = m->total_cycles + paste_gap_cycles(m);
        return;
    }

    while (ps->text[ps->pos] != '\0') {
        C64Key key;
        bool shift;
        char c = ps->text[ps->pos++];
        if (!char_to_c64key(c, &key, &shift)) {
            continue; /* unmapped character -- skip it, keep typing the rest */
        }
        if (shift) {
            ks->real_lshift_down = true;
            update_lshift(m, ks);
        }
        keyboard_matrix_set_key(&m->keyboard, key, true);
        ps->current_key = key;
        ps->current_key_needs_shift = shift;
        ps->key_down = true;
        ps->next_transition_cycle = m->total_cycles + paste_hold_cycles(m);
        return;
    }

    /* Reached the end of the pasted text. */
    SDL_free(ps->text);
    ps->text = NULL;
}

/* ------------------------------------------------------------------ */
/* Joystick: numpad fallback (docs/peripherals.md explicitly allows     */
/* this instead of the real SDL joystick/game-controller API), driving */
/* whichever of the two C64 ports is currently selected. F9 toggles     */
/* which port -- docs/cia.md's own suggested answer for "one physical   */
/* input device, two possible ports."                                   */
/* ------------------------------------------------------------------ */

static void handle_joystick_key(Machine *m, bool port_is_joystick2, SDL_Scancode sc, bool down) {
    Joystick *js = port_is_joystick2 ? &m->joystick2 : &m->joystick1;
    switch (sc) {
        case SDL_SCANCODE_KP_8: js->up = down; break;
        case SDL_SCANCODE_KP_2: js->down = down; break;
        case SDL_SCANCODE_KP_4: js->left = down; break;
        case SDL_SCANCODE_KP_6: js->right = down; break;
        case SDL_SCANCODE_KP_0: js->fire = down; break;
        default: break;
    }
}

/* ------------------------------------------------------------------ */
/* Video: blit the VIC-II's per-frame framebuffer to an SDL texture     */
/* ------------------------------------------------------------------ */

/* VicII.framebuffer[][] is indexed in the article's own native X
 * coordinate space (0-503), which is numbered from the raster-IRQ
 * reference point, not from the start of the visible picture -- so the
 * real visible window (docs' own VIC_FIRST_VISIBLE_X/VIC_LAST_VISIBLE_X,
 * see vic_ii.h) wraps across the array's 503/0 boundary: the real
 * on-screen image is columns [VIC_FIRST_VISIBLE_X..503] followed by
 * [0..VIC_LAST_VISIBLE_X], not a contiguous sub-range of the raw array.
 * A real monitor shows this as one unbroken picture, so this rotates
 * columns (and crops rows to the visible line range) into that
 * contiguous order before blitting -- a display-only concern, not a
 * VIC-II correctness one; the raw wrapped array is still what
 * tools/demos/ dumps and what every internal test checks against. */
#define VISIBLE_WIDTH ((VIC_X_MODULUS - VIC_FIRST_VISIBLE_X) + (VIC_LAST_VISIBLE_X + 1u))
#define VISIBLE_HEIGHT (VIC_LAST_VISIBLE_LINE - VIC_FIRST_VISIBLE_LINE + 1u)

static void render_frame(const Machine *m, SDL_Renderer *renderer, SDL_Texture *texture, uint8_t *rgb_scratch) {
    for (unsigned out_y = 0; out_y < VISIBLE_HEIGHT; out_y++) {
        unsigned y = out_y + VIC_FIRST_VISIBLE_LINE;
        for (unsigned out_x = 0; out_x < VISIBLE_WIDTH; out_x++) {
            unsigned x = (VIC_FIRST_VISIBLE_X + out_x) % VIC_X_MODULUS;
            uint8_t color_index = m->vic.framebuffer[y][x] & 0x0Fu;
            uint8_t *px = &rgb_scratch[(out_y * VISIBLE_WIDTH + out_x) * 3];
            px[0] = VIC_PALETTE_RGB[color_index][0];
            px[1] = VIC_PALETTE_RGB[color_index][1];
            px[2] = VIC_PALETTE_RGB[color_index][2];
        }
    }
    SDL_UpdateTexture(texture, NULL, rgb_scratch, (int)(VISIBLE_WIDTH * 3));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <kernal.rom> <basic.rom> <chargen.rom>\n", argv[0]);
        fprintf(stderr, "  (never fetched/vendored by this project -- see scripts/stage_roms.sh)\n");
        return 2;
    }

    static Machine m;
    machine_init(&m);
    bool ok = machine_load_kernal(&m, argv[1]) && machine_load_basic(&m, argv[2]) &&
              machine_load_chargen(&m, argv[3]);
    if (!ok) {
        fprintf(stderr, "error: failed to load one or more ROMs (wrong path or size)\n");
        return 2;
    }
    machine_reset(&m);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const int scale = 2;
    SDL_Window *window = SDL_CreateWindow("c64-c-emu", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           (int)VISIBLE_WIDTH * scale, (int)VISIBLE_HEIGHT * scale, 0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, 0);
    }
    SDL_Texture *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                                              (int)VISIBLE_WIDTH, (int)VISIBLE_HEIGHT);
    uint8_t *rgb_scratch = malloc((size_t)VISIBLE_WIDTH * VISIBLE_HEIGHT * 3);

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev == 0) {
        fprintf(stderr, "SDL_OpenAudioDevice failed: %s -- continuing without audio\n", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(audio_dev, 0);
    }

    const double cycles_per_sample = C64_PAL_PHI2_HZ / (double)(audio_dev ? have.freq : AUDIO_SAMPLE_RATE);
    double cycle_accum = 0.0;
    uint64_t last_frame_rendered = UINT64_MAX;
    KeyState keystate = {0};
    PasteState paste_state = {0};
    bool joystick_port2 = true; /* real hardware convention: single-joystick software expects port 2 */

    int16_t *sample_buf = malloc(sizeof(int16_t) * (size_t)(AUDIO_SAMPLE_RATE / 2));

    bool quit = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    quit = true;
                    break;
                case SDL_KEYDOWN:
                case SDL_KEYUP: {
                    bool down = (e.type == SDL_KEYDOWN);
                    if (e.key.keysym.scancode == SDL_SCANCODE_F9 && down && !e.key.repeat) {
                        joystick_port2 = !joystick_port2;
                        break;
                    }
                    /* Ctrl+V or Shift+Insert: paste the clipboard as a
                     * sequence of synthetic keypresses -- see the
                     * "Paste-as-typing" section above. */
                    bool mod_ctrl = (SDL_GetModState() & KMOD_CTRL) != 0;
                    bool mod_shift = (SDL_GetModState() & KMOD_SHIFT) != 0;
                    if (down && !e.key.repeat &&
                        ((e.key.keysym.scancode == SDL_SCANCODE_V && mod_ctrl) ||
                         (e.key.keysym.scancode == SDL_SCANCODE_INSERT && mod_shift))) {
                        paste_start(&m, &paste_state, &keystate);
                        break;
                    }
                    if (!e.key.repeat) {
                        handle_key_event(&m, &keystate, e.key.keysym.scancode, down);
                        handle_joystick_key(&m, joystick_port2, e.key.keysym.scancode, down);
                    }
                    break;
                }
                default:
                    break;
            }
        }

        paste_tick(&m, &paste_state, &keystate);

        if (audio_dev != 0) {
            Uint32 queued_bytes = SDL_GetQueuedAudioSize(audio_dev);
            Uint32 target_bytes = (Uint32)(have.freq * AUDIO_TARGET_BUFFER_SECONDS * sizeof(int16_t));
            if (queued_bytes < target_bytes) {
                size_t samples_needed = (target_bytes - queued_bytes) / sizeof(int16_t);
                size_t cap = (size_t)(AUDIO_SAMPLE_RATE / 2);
                if (samples_needed > cap) {
                    samples_needed = cap;
                }
                for (size_t i = 0; i < samples_needed; i++) {
                    cycle_accum += cycles_per_sample;
                    while (cycle_accum >= 1.0) {
                        machine_cycle(&m);
                        cycle_accum -= 1.0;
                    }
                    double s = sid_output(&m.sid);
                    if (s > 1.0) s = 1.0;
                    if (s < -1.0) s = -1.0;
                    sample_buf[i] = (int16_t)(s * AUDIO_AMPLITUDE);
                }
                SDL_QueueAudio(audio_dev, sample_buf, (Uint32)(samples_needed * sizeof(int16_t)));
            } else {
                SDL_Delay(1);
            }
        } else {
            /* No audio device -- fall back to advancing a fixed, small
             * cycle budget per iteration so video/input still progress. */
            machine_run_cycles(&m, 1000);
            SDL_Delay(1);
        }

        uint64_t frame_index = m.total_cycles / ((uint64_t)VIC_CYCLES_PER_LINE * VIC_LINES_PER_FRAME);
        if (frame_index != last_frame_rendered) {
            last_frame_rendered = frame_index;
            render_frame(&m, renderer, texture, rgb_scratch);
        }
    }

    if (paste_state.text) {
        SDL_free(paste_state.text);
    }
    if (audio_dev != 0) {
        SDL_CloseAudioDevice(audio_dev);
    }
    free(sample_buf);
    free(rgb_scratch);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
