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
 * column `col` (0-39), avoiding columns 0-2 which read a forced $FF
 * character code on their c-access -- see the DMA-delay test below for
 * that specific, real hardware quirk instead. */
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

    /* Character column 5 spans x = FIRST_LINE_X + (15-1)*8 + 5*8 .. +7,
     * since c/g-access for column N happens at cycle 15+N. */
    uint16_t col_start = (uint16_t)((VIC_FIRST_LINE_X + (15 - 1 + 5) * 8) % VIC_X_MODULUS);
    const uint8_t *line = h.vic.framebuffer[51];
    TEST_ASSERT_EQ_U8(line[col_start], 2);       /* "1" pixel -> foreground color */
    TEST_ASSERT_EQ_U8(line[(col_start + 4) % VIC_X_MODULUS], 6); /* "0" pixel -> background color 0 */
}

static void test_first_three_c_accesses_read_forced_ff(void) {
    Harness h;
    setup(&h);
    /* Column 0's real character code is deliberately something whose
     * pattern would be visible if actually used... */
    uint8_t rows[8] = {0xFF, 0, 0, 0, 0, 0, 0, 0};
    place_char(&h, 0, 0x01, rows, 3);

    run_to_line_cycle1(&h.vic, 51);
    run_cycles(&h.vic, 63);

    /* ...but per the article's own documented DMA-delay quirk, the
     * first three c-accesses (columns 0-2) read $FF for the character
     * code regardless of what's actually in the video matrix, so
     * column 0 must NOT show character $01's pattern/color. */
    uint16_t col_start = (uint16_t)((VIC_FIRST_LINE_X + (15 - 1) * 8) % VIC_X_MODULUS);
    TEST_ASSERT(h.vic.framebuffer[51][col_start] != 3);
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
    test_first_three_c_accesses_read_forced_ff();

    test_upper_border_before_display_window();
    test_raster_split_border_color_mid_frame();

    test_raster_irq_fires_at_compare_line();
    test_d019_write_one_clears_not_read();

    test_sprite_appears_at_its_x_position();
    test_sprite_sprite_collision_detected();
    test_sprite_priority_higher_number_hidden_behind_lower();
    test_sprite_multicolor_rendering();

    test_bus_stolen_during_bad_line_window();
    test_bus_free_outside_bad_line_and_sprite_dma();

    printf("%d/%d assertions passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
