/* Hand-written tests for the VIC-II (Phase 4). Every behavior fact
 * exercised here is sourced directly from Christian Bauer's cycle-by-
 * cycle article -- see docs/vic-ii.md and docs/sources.md. No real
 * ROMs needed: screen/charset/sprite content is synthetic, built by
 * these tests themselves, per docs/testing-strategy.md. */

#include "testutil.h"

#include <string.h>

#include "../../src/c64/vic_ii.h"

#define REG_D011 0x11
#define REG_D012 0x12
#define REG_D015 0x15
#define REG_D016 0x16
#define REG_D017 0x17
#define REG_D018 0x18
#define REG_D019 0x19
#define REG_D01A 0x1A
#define REG_D01B 0x1B
#define REG_D01C 0x1C
#define REG_D01D 0x1D
#define REG_D01E 0x1E
#define REG_D01F 0x1F
#define REG_D020 0x20
#define REG_D021 0x21

#define D011_DEN 0x10u
#define D011_RSEL 0x08u
#define D016_CSEL 0x08u

typedef struct Harness {
    uint8_t ram[65536];
    uint8_t char_rom[4096];
    uint8_t color_ram[1024];
    VicII vic;
} Harness;

/* 25-line/40-column standard setup: screen at $0400 (VM=1), charset at
 * $0000 (CB=0, deliberately NOT one of the two hardwired Character-ROM
 * windows, so this test's own synthetic character data in RAM is what
 * actually gets read -- see docs/vic-ii.md). YSCROLL=3/RSEL=1 is the
 * documented alignment for a 25-line display (first display/bad line
 * at raster 51). */
static void setup(Harness *h) {
    memset(h->ram, 0, sizeof(h->ram));
    memset(h->char_rom, 0, sizeof(h->char_rom));
    memset(h->color_ram, 0, sizeof(h->color_ram));
    vic_ii_init(&h->vic, h->ram, h->char_rom, h->color_ram);
    vic_ii_set_bank(&h->vic, 0);
    vic_ii_reg_write(&h->vic, REG_D018, 0x10); /* VM=1 ($0400), CB=0 ($0000) */
    vic_ii_reg_write(&h->vic, REG_D011, (uint8_t)(D011_DEN | D011_RSEL | 0x03u)); /* DEN, 25 lines, YSCROLL=3 */
    vic_ii_reg_write(&h->vic, REG_D016, D016_CSEL); /* 40 columns, XSCROLL=0, MCM=0 */
}

static void run_cycles(VicII *vic, int n) {
    for (int i = 0; i < n; i++) {
        vic_ii_cycle(vic);
    }
}

static void run_to_line_cycle1(VicII *vic, uint16_t target_line) {
    for (int guard = 0; guard < 400000; guard++) {
        if (vic->raster_line == target_line && vic->cycle == 1) return;
        vic_ii_cycle(vic);
    }
}

/* Sets a screen character + its 8x8 pattern + color, at video-matrix
 * column `col` (0-39). */
static void place_char(Harness *h, int col, uint8_t code, const uint8_t rows[8], uint8_t color) {
    h->ram[0x0400 + col] = code;
    h->color_ram[col] = color;
    for (int r = 0; r < 8; r++) {
        h->ram[(uint16_t)(code * 8 + r)] = rows[r];
    }
}

/* ---------------------------------------------------------------- */
/* Bad line detection                                                 */
/* ---------------------------------------------------------------- */

static void test_first_bad_line_matches_yscroll_alignment(void) {
    Harness h;
    setup(&h);
    /* YSCROLL=3 -> first bad line in [0x30,0xf7] with (raster&7)==3 is 51. */
    run_to_line_cycle1(&h.vic, 51);
    run_cycles(&h.vic, 14); /* through cycle 14: RC reset happens here on a bad line */
    TEST_ASSERT_EQ_U8(h.vic.rc, 0);
}

static void test_no_bad_line_without_den(void) {
    Harness h;
    memset(h.ram, 0, sizeof(h.ram));
    memset(h.char_rom, 0, sizeof(h.char_rom));
    memset(h.color_ram, 0, sizeof(h.color_ram));
    vic_ii_init(&h.vic, h.ram, h.char_rom, h.color_ram);
    vic_ii_reg_write(&h.vic, REG_D011, D011_RSEL | 0x03u); /* DEN deliberately clear */

    run_to_line_cycle1(&h.vic, 51);
    run_cycles(&h.vic, 20);
    /* Never having been a bad line, RC should have stayed at its
     * initial 0 and the VIC never left idle state. */
    TEST_ASSERT(!h.vic.display_state);
}

/* ---------------------------------------------------------------- */
/* Standard text mode rendering                                      */
/* ---------------------------------------------------------------- */

static void test_standard_text_mode_pixel_colors(void) {
    Harness h;
    setup(&h);
    vic_ii_reg_write(&h.vic, REG_D021, 6); /* background color 0 = 6 */

    uint8_t rows[8] = {0xF0, 0, 0, 0, 0, 0, 0, 0}; /* top row: left 4 pixels set, right 4 clear */
    place_char(&h, 5, 0x41, rows, 2);              /* color 2 for this character */

    run_to_line_cycle1(&h.vic, 51); /* first display row, RC will be 0 */
    run_cycles(&h.vic, 63);          /* finish the line so framebuffer[51] is committed */

    /* Character column 5 spans x = FIRST_LINE_X + (16-1)*8 + 5*8 + 4 .. +7:
     * c-access for column N happens at cycle 15+N, but the g-access
     * that actually renders its pixels happens one cycle later, at
     * cycle 16+N (a real one-cycle pipeline), and its visible pixel
     * output lands a further, empirically-confirmed 4 pixels later
     * still -- see the two comments in vic_ii_cycle() where these are
     * implemented. */
    uint16_t col_start = (uint16_t)((VIC_FIRST_LINE_X + (16 - 1 + 5) * 8 + 4) % VIC_X_MODULUS);
    const uint8_t *line = h.vic.framebuffer[51];
    TEST_ASSERT_EQ_U8(line[col_start], 2);       /* "1" pixel -> foreground color */
    TEST_ASSERT_EQ_U8(line[(col_start + 4) % VIC_X_MODULUS], 6); /* "0" pixel -> background color 0 */
}

/* Regression test for a real, confirmed bug: an earlier implementation
 * unconditionally forced $ff for the first three c-accesses of every
 * bad line, misapplying article section 3.14.3's FLI-specific,
 * artificially-late-bad-line-only effect as if it were a universal bad
 * line property. That corrupted every normal text display's first
 * couple of columns -- confirmed wrong directly against the primary
 * source (docs/sources.md) once a live, readable rendering (Phase 7)
 * made the bug visible, which also led to finding and fixing two
 * further real bugs in the same area (a missing one-cycle c/g-access
 * pipeline delay, and an empirically-confirmed 4-pixel g-access output
 * offset -- see vic_ii_cycle()'s own comments). Column 0 is fully
 * visible now too (see test_full_screen_columns_and_rows_render_
 * correctly below); column 2 is used here simply to keep this
 * regression test's own history, not because column 0 is special. */
static void test_normal_bad_line_c_access_reads_real_data_not_forced_ff(void) {
    Harness h;
    setup(&h);
    uint8_t rows[8] = {0xFF, 0, 0, 0, 0, 0, 0, 0};
    place_char(&h, 2, 0x01, rows, 3);

    run_to_line_cycle1(&h.vic, 51);
    run_cycles(&h.vic, 63);

    uint16_t col_start = (uint16_t)((VIC_FIRST_LINE_X + (16 - 1 + 2) * 8 + 4) % VIC_X_MODULUS);
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[51][col_start], 3);
}

/* Regression test for the two pipeline/offset bugs above, at full
 * screen scale: fills all 40x25 real video-matrix positions with solid
 * character cells (a distinct color per column) and checks that BOTH
 * edge columns (0 and 39) render their FULL 8-pixel width -- not just
 * their first pixel -- at BOTH the first and last display rows. This
 * is exactly the shape of check that caught the bug in the first place
 * (a live rendering showed column 0 only half-width); a test that only
 * sampled column 0's first pixel would not have caught it, since that
 * one pixel happened to be correct even while the other three weren't. */
static void test_full_screen_columns_and_rows_render_correctly(void) {
    Harness h;
    setup(&h);

    uint8_t solid[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (int row = 0; row < 25; row++) {
        for (int col = 0; col < 40; col++) {
            h.ram[0x0400 + row * 40 + col] = 1;
            h.color_ram[row * 40 + col] = (uint8_t)((col % 15) + 1); /* avoid 0 (black), it'd be indistinguishable from an unrendered pixel */
        }
    }
    for (int r = 0; r < 8; r++) {
        h.ram[1 * 8 + r] = solid[r];
    }

    run_to_line_cycle1(&h.vic, 51); /* first display row (RC=0) */
    run_cycles(&h.vic, 63);

    uint16_t col0_start = (uint16_t)((VIC_FIRST_LINE_X + (16 - 1 + 0) * 8 + 4) % VIC_X_MODULUS);
    uint16_t col39_start = (uint16_t)((VIC_FIRST_LINE_X + (16 - 1 + 39) * 8 + 4) % VIC_X_MODULUS);
    uint8_t expected_col0 = (uint8_t)((0 % 15) + 1);
    uint8_t expected_col39 = (uint8_t)((39 % 15) + 1);

    for (int p = 0; p < 8; p++) {
        TEST_ASSERT_EQ_U8(h.vic.framebuffer[51][(col0_start + p) % VIC_X_MODULUS], expected_col0);
        TEST_ASSERT_EQ_U8(h.vic.framebuffer[51][(col39_start + p) % VIC_X_MODULUS], expected_col39);
    }

    run_to_line_cycle1(&h.vic, 243); /* last display row (row 24 of 25: 51 + 24*8) */
    run_cycles(&h.vic, 63);

    for (int p = 0; p < 8; p++) {
        TEST_ASSERT_EQ_U8(h.vic.framebuffer[243][(col0_start + p) % VIC_X_MODULUS], expected_col0);
        TEST_ASSERT_EQ_U8(h.vic.framebuffer[243][(col39_start + p) % VIC_X_MODULUS], expected_col39);
    }

    /* The border immediately outside both edge columns must NOT show
     * the column's color -- confirms the column isn't bleeding past
     * where it should stop (the flip side of "isn't clipped short"). */
    TEST_ASSERT(h.vic.framebuffer[51][(col0_start - 1 + VIC_X_MODULUS) % VIC_X_MODULUS] != expected_col0);
    TEST_ASSERT(h.vic.framebuffer[51][(col39_start + 8) % VIC_X_MODULUS] != expected_col39);
}

/* ---------------------------------------------------------------- */
/* Border                                                             */
/* ---------------------------------------------------------------- */

static void test_upper_border_before_display_window(void) {
    Harness h;
    setup(&h);
    vic_ii_reg_write(&h.vic, REG_D020, 14); /* border color */

    run_to_line_cycle1(&h.vic, 50); /* one line before the display window starts (RSEL=1 -> top=51) */
    run_cycles(&h.vic, 63);
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[50][100], 14);
}

static void test_raster_split_border_color_mid_frame(void) {
    /* Phase 4's own documented verification target: changing $D020
     * from a raster-IRQ-style mid-frame register write must produce a
     * visibly split-color frame when rendered scanline-by-scanline --
     * the concrete thing a frame-snapshot design cannot do. x=10 is
     * inside the permanent left border (CSEL=1 -> left comparator 24),
     * so this checks the border specifically, regardless of the
     * vertical border/display-window state at these (in-window) lines. */
    Harness h;
    setup(&h);
    vic_ii_reg_write(&h.vic, REG_D020, 2); /* red-ish */

    run_to_line_cycle1(&h.vic, 100);
    run_cycles(&h.vic, 63); /* line 100 rendered with border color 2 */

    vic_ii_reg_write(&h.vic, REG_D020, 5); /* changed "mid-frame", as a raster IRQ handler would */

    run_to_line_cycle1(&h.vic, 101);
    run_cycles(&h.vic, 63); /* line 101 rendered with border color 5 */

    TEST_ASSERT_EQ_U8(h.vic.framebuffer[100][10], 2);
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[101][10], 5);
}

/* ---------------------------------------------------------------- */
/* Blanking: the real video signal goes black, not border color,     */
/* during horizontal/vertical sync -- a real, disclosed gap surfaced */
/* by Phase 7's live SDL2 display (see docs/vic-ii.md), fixed here.  */
/* ---------------------------------------------------------------- */

static void test_vertical_blanking_forces_black_regardless_of_border_color(void) {
    Harness h;
    setup(&h);
    vic_ii_reg_write(&h.vic, REG_D020, 14); /* a non-black border color */

    /* Line 5 is inside the vblank window (VIC_FIRST_VISIBLE_LINE=16) --
     * the border flip-flop is still "on" here (well before the display
     * window even starts), so without the blanking override this would
     * read back as the border color, not black. */
    run_to_line_cycle1(&h.vic, 5);
    run_cycles(&h.vic, 63);
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[5][100], 0);

    /* A visible line's own border pixel is unaffected. */
    run_to_line_cycle1(&h.vic, 50);
    run_cycles(&h.vic, 63);
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[50][100], 14);
}

static void test_horizontal_blanking_forces_black_regardless_of_border_color(void) {
    Harness h;
    setup(&h);
    vic_ii_reg_write(&h.vic, REG_D020, 14);

    run_to_line_cycle1(&h.vic, 100); /* a visible line */
    run_cycles(&h.vic, 63);

    /* x=10 is in the permanent left border -- visible, real border
     * color. x=420 falls inside horizontal blanking (VIC_LAST_VISIBLE_X
     * = 380, VIC_FIRST_VISIBLE_X = 480) -- forced black even though the
     * border flip-flop is "on" there too. */
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[100][10], 14);
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[100][420], 0);
}

/* ---------------------------------------------------------------- */
/* Raster IRQ + $D019 write-1-to-clear semantics                     */
/* ---------------------------------------------------------------- */

static void test_raster_irq_fires_at_compare_line(void) {
    Harness h;
    setup(&h);
    vic_ii_reg_write(&h.vic, REG_D01A, VIC_IRQ_RST); /* enable */
    vic_ii_reg_write(&h.vic, REG_D012, 100);          /* compare line 100 (RST8 clear -> just 100) */

    run_to_line_cycle1(&h.vic, 100);
    run_cycles(&h.vic, 1); /* the compare check happens in cycle 1 */

    TEST_ASSERT(h.vic.irq_latch & VIC_IRQ_RST);
    TEST_ASSERT(vic_ii_irq_asserted(&h.vic));
}

static void test_d019_write_one_clears_not_read(void) {
    Harness h;
    setup(&h);
    h.vic.irq_latch = VIC_IRQ_RST;

    uint8_t before = vic_ii_reg_read(&h.vic, REG_D019); /* a plain read must NOT clear it (unlike the CIA's ICR) */
    TEST_ASSERT(before & VIC_IRQ_RST);
    TEST_ASSERT(h.vic.irq_latch & VIC_IRQ_RST);

    vic_ii_reg_write(&h.vic, REG_D019, VIC_IRQ_RST); /* writing a 1 to the bit clears it */
    TEST_ASSERT_EQ_U8(h.vic.irq_latch & VIC_IRQ_RST, 0);
}

/* ---------------------------------------------------------------- */
/* Sprites                                                            */
/* ---------------------------------------------------------------- */

static void enable_sprite0(Harness *h, uint16_t x, uint8_t y, uint8_t pointer, const uint8_t data[3]) {
    vic_ii_reg_write(&h->vic, 0x00, (uint8_t)(x & 0xFFu));
    vic_ii_reg_write(&h->vic, 0x10, (uint8_t)((x >> 8) & 1u));
    vic_ii_reg_write(&h->vic, 0x01, y);
    vic_ii_reg_write(&h->vic, REG_D015, 0x01); /* enable sprite 0 */
    vic_ii_reg_write(&h->vic, 0x27, 7); /* sprite 0 color */
    /* Sprite data pointer table: video matrix base ($0400) + $3F8 + n. */
    h->ram[0x0400 + 0x3F8] = pointer;
    for (int i = 0; i < 3; i++) {
        h->ram[(uint16_t)(pointer * 64 + i)] = data[i];
    }
}

static void test_sprite_appears_at_its_x_position(void) {
    Harness h;
    setup(&h);
    const uint8_t data[3] = {0xFF, 0x00, 0x00}; /* first 8 pixels solid */
    enable_sprite0(&h, 100, 90, 4, data);

    run_to_line_cycle1(&h.vic, 90); /* sprite Y=90 -> first displayed on raster line 90 (Y is "one less" per the article) */
    run_cycles(&h.vic, 63);

    TEST_ASSERT_EQ_U8(h.vic.framebuffer[90][100], 7);
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[90][99], 0); /* just to the left: not sprite color */
}

static void test_sprite_sprite_collision_detected(void) {
    Harness h;
    setup(&h);
    const uint8_t data[3] = {0xFF, 0, 0};
    enable_sprite0(&h, 100, 90, 4, data);

    /* Sprite 1 overlaps sprite 0 exactly. */
    vic_ii_reg_write(&h.vic, 0x02, 100);
    vic_ii_reg_write(&h.vic, 0x03, 90);
    vic_ii_reg_write(&h.vic, REG_D015, 0x03); /* enable sprites 0 and 1 */
    vic_ii_reg_write(&h.vic, 0x28, 8);
    h.ram[0x0400 + 0x3F8 + 1] = 5;
    for (int i = 0; i < 3; i++) {
        h.ram[(uint16_t)(5 * 64 + i)] = data[i];
    }

    run_to_line_cycle1(&h.vic, 90);
    run_cycles(&h.vic, 63);

    uint8_t collision = vic_ii_reg_read(&h.vic, REG_D01E);
    TEST_ASSERT_EQ_U8(collision, 0x03);
    TEST_ASSERT(h.vic.irq_latch & VIC_IRQ_MMC);
    /* Auto-clears on read. */
    TEST_ASSERT_EQ_U8(vic_ii_reg_read(&h.vic, REG_D01E), 0);
}

static void test_sprite_priority_higher_number_hidden_behind_lower(void) {
    Harness h;
    setup(&h);
    const uint8_t data[3] = {0xFF, 0, 0};
    enable_sprite0(&h, 100, 90, 4, data); /* sprite 0, color 7 */

    vic_ii_reg_write(&h.vic, 0x02, 100);
    vic_ii_reg_write(&h.vic, 0x03, 90);
    vic_ii_reg_write(&h.vic, REG_D015, 0x03);
    vic_ii_reg_write(&h.vic, 0x28, 9); /* sprite 1 color 9 */
    h.ram[0x0400 + 0x3F8 + 1] = 5;
    for (int i = 0; i < 3; i++) {
        h.ram[(uint16_t)(5 * 64 + i)] = data[i];
    }

    run_to_line_cycle1(&h.vic, 90);
    run_cycles(&h.vic, 63);

    /* Sprite 0 has the highest priority -- its color must win where both overlap. */
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[90][100], 7);
}

/* Regression test for a real, confirmed bug: composite_sprites_for_line()
 * never actually checked border state -- it only checked
 * line_is_foreground[x] (the graphics foreground/background
 * classification, for the UNRELATED sprite-behind-foreground MxDP
 * priority check), which update_border_and_render() sets false inside
 * the border for its own reasons, making the sprite-priority condition
 * unconditionally true there regardless of border. This let every
 * sprite draw over the border, directly contradicting article 3.9 rule
 * 1 ("border has strictly higher display priority than every sprite" --
 * already correctly documented, just not actually implemented). Found
 * via a user's own real BASIC sprite program: a sprite re-triggered by
 * the real, documented low-8-bit Y-match wraparound (article 3.8.1
 * rules 2/4; see this test's own neighbor below) landed partly in the
 * border on the wrapped-to frame and was visibly NOT hidden there. */
static void test_sprite_does_not_draw_over_border(void) {
    Harness h;
    setup(&h);
    vic_ii_reg_write(&h.vic, REG_D020, 14); /* border color 14 */
    const uint8_t data[3] = {0xFF, 0, 0};
    enable_sprite0(&h, 10, 90, 4, data); /* X=10 is well within the permanent left border (CSEL=1 -> 24) */

    run_to_line_cycle1(&h.vic, 90);
    run_cycles(&h.vic, 63);

    TEST_ASSERT_EQ_U8(h.vic.framebuffer[90][10], 14); /* border color, NOT the sprite's color 7 */
}

/* Confirms a real, documented (not emulator-specific) hardware quirk,
 * found via the same user program (Y=55, the exact value it was
 * bouncing through when the bug was reported): article 3.8.1's rules
 * 2/4 compare a sprite's Y register against the LOWER 8 BITS of
 * RASTER, not the full 9-bit PAL raster counter -- so DMA genuinely
 * re-triggers 256 lines later (mod 312, PAL's total line count) purely
 * because (55+256)=311 also satisfies "raster & 0xff == 55". This is a
 * real internal fact, not something to "fix".
 *
 * A same-frame wraparound match only exists for Y <= 55 (Y+256 must
 * stay within the valid 0-311 range), and for every such Y, the
 * wrapped line (256-311) provably always falls in either the real
 * bottom border (252-299, RSEL=1) or real vertical blanking (300-311)
 * -- never inside the actual 51-251 display window, regardless of
 * RSEL/CSEL. So on correctly-behaving hardware (and now, correctly
 * here) this internal re-trigger has NO visible symptom by itself.
 * What WAS visible, before the border-priority bug above was fixed:
 * the re-triggered sprite's 21-line display duration runs from line
 * 311 across the frame wrap into the new frame's lines 0-19, and lines
 * 16-19 of that span are real, visible upper border (not blanked) --
 * exactly where the border-priority bug let it bleed through. */
static void test_sprite_y_match_low_8_bits_retriggers_but_stays_hidden(void) {
    Harness h;
    setup(&h);
    vic_ii_reg_write(&h.vic, REG_D020, 14);
    const uint8_t data[3] = {0xFF, 0, 0};
    enable_sprite0(&h, 24, 55, 4, data);

    run_to_line_cycle1(&h.vic, 55);
    run_cycles(&h.vic, 63);
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[55][24], 7); /* the real occurrence */

    run_to_line_cycle1(&h.vic, 311); /* (55 + 256) % 312 -- the internal re-trigger point */
    TEST_ASSERT(!h.vic.sprite_dma[0]); /* confirm it wasn't already left on from the first occurrence */
    run_cycles(&h.vic, 63);
    TEST_ASSERT(h.vic.sprite_dma[0]); /* re-triggered, exactly per article rules 2/4 -- a real fact */
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[311][24], 0); /* real vertical blanking -- correctly forced black */

    run_to_line_cycle1(&h.vic, 18); /* within the re-triggered sprite's 21-line span, and within the */
    run_cycles(&h.vic, 63);         /* real, visible upper border (16-50) -- must NOT show the sprite */
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[18][24], 14); /* border color, not the sprite's color 7 */
}

static void test_sprite_multicolor_rendering(void) {
    Harness h;
    setup(&h);
    vic_ii_reg_write(&h.vic, REG_D01C, 0x01); /* sprite 0 multicolor */
    vic_ii_reg_write(&h.vic, 0x25, 11);       /* MM0 */
    vic_ii_reg_write(&h.vic, 0x26, 12);       /* MM1 */
    const uint8_t data[3] = {0x6Cu, 0, 0}; /* 0b01101100: pairs 01,10,11,00 */
    enable_sprite0(&h, 100, 90, 4, data);

    run_to_line_cycle1(&h.vic, 90);
    run_cycles(&h.vic, 63);

    TEST_ASSERT_EQ_U8(h.vic.framebuffer[90][100], 11); /* "01" -> MM0 */
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[90][102], 7);  /* "10" -> sprite color */
    TEST_ASSERT_EQ_U8(h.vic.framebuffer[90][104], 12); /* "11" -> MM1 */
}

/* ---------------------------------------------------------------- */
/* Bus stealing (BA)                                                  */
/* ---------------------------------------------------------------- */

static void test_bus_stolen_during_bad_line_window(void) {
    Harness h;
    setup(&h);
    run_to_line_cycle1(&h.vic, 51);
    run_cycles(&h.vic, 11); /* now at cycle 12 */
    bool stolen_at_12 = vic_ii_cycle(&h.vic); /* executes cycle 12 */
    TEST_ASSERT(stolen_at_12);
}

static void test_bus_free_outside_bad_line_and_sprite_dma(void) {
    Harness h;
    setup(&h);
    run_to_line_cycle1(&h.vic, 100); /* no bad line here (not aligned), no sprites enabled */
    run_cycles(&h.vic, 19);          /* skip past cycle 20-ish, well clear of refresh/sprite windows */
    bool stolen = vic_ii_cycle(&h.vic);
    TEST_ASSERT(!stolen);
}

int main(void) {
    test_first_bad_line_matches_yscroll_alignment();
    test_no_bad_line_without_den();

    test_standard_text_mode_pixel_colors();
    test_normal_bad_line_c_access_reads_real_data_not_forced_ff();
    test_full_screen_columns_and_rows_render_correctly();

    test_upper_border_before_display_window();
    test_raster_split_border_color_mid_frame();

    test_vertical_blanking_forces_black_regardless_of_border_color();
    test_horizontal_blanking_forces_black_regardless_of_border_color();

    test_raster_irq_fires_at_compare_line();
    test_d019_write_one_clears_not_read();

    test_sprite_appears_at_its_x_position();
    test_sprite_sprite_collision_detected();
    test_sprite_priority_higher_number_hidden_behind_lower();
    test_sprite_does_not_draw_over_border();
    test_sprite_y_match_low_8_bits_retriggers_but_stays_hidden();
    test_sprite_multicolor_rendering();

    test_bus_stolen_during_bad_line_window();
    test_bus_free_outside_bad_line_and_sprite_dma();

    printf("%d/%d assertions passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
