/* Hand-written tests for the C64 PLA-driven memory map (Phase 2). No
 * real ROMs needed -- synthetic byte patterns stand in for BASIC/
 * KERNAL/Character ROM content, per docs/testing-strategy.md's rule
 * that the base test suite must run with zero ROMs staged. The real-
 * ROM verification target (Phase 2's "reaches the genuine KERNAL reset
 * routine") lives in tests/integration/test_boot.c instead, since it
 * needs the user's own staged dumps. */

#include "testutil.h"

#include "../../src/c64/memory.h"

static void fill(uint8_t *buf, size_t n, uint8_t value) {
    for (size_t i = 0; i < n; i++) {
        buf[i] = value;
    }
}

/* ---------------------------------------------------------------- */
/* Default (unwritten) port: floats high -> BASIC+KERNAL+I/O visible   */
/* ---------------------------------------------------------------- */

static void test_default_port_is_all_high(void) {
    C64Memory mem;
    c64memory_init(&mem);
    TEST_ASSERT_EQ_U8(c64memory_effective_port(&mem), 0xFF);
}

static void test_reset_vector_reads_through_kernal_by_default(void) {
    C64Memory mem;
    c64memory_init(&mem);
    fill(mem.kernal_rom, sizeof(mem.kernal_rom), 0);
    /* $FFFC/$FFFD live at the very end of an 8KB KERNAL ROM: offset
     * 0x1FFC/0x1FFD within kernal_rom[]. */
    mem.kernal_rom[0x1FFC] = 0x34;
    mem.kernal_rom[0x1FFD] = 0x12;
    Bus bus = c64memory_as_bus(&mem);
    TEST_ASSERT_EQ_U8(bus_read8(&bus, 0xFFFC), 0x34);
    TEST_ASSERT_EQ_U8(bus_read8(&bus, 0xFFFD), 0x12);
}

/* ---------------------------------------------------------------- */
/* Exhaustive bank-switching truth table (docs/memory-map.md)         */
/* ---------------------------------------------------------------- */

typedef enum { SRC_RAM, SRC_BASIC, SRC_KERNAL, SRC_CHARROM, SRC_IO } Source;

static void set_port(C64Memory *mem, bool loram, bool hiram, bool charen) {
    mem->cpu_port_ddr = 0x07; /* bits 0-2 driven as outputs */
    mem->cpu_port_data = (uint8_t)((loram ? 1 : 0) | (hiram ? 2 : 0) | (charen ? 4 : 0));
}

static void test_bank_switching_truth_table(void) {
    C64Memory mem;
    c64memory_init(&mem);
    fill(mem.ram, sizeof(mem.ram), 0x00);
    fill(mem.basic_rom, sizeof(mem.basic_rom), 0x11);
    fill(mem.kernal_rom, sizeof(mem.kernal_rom), 0x22);
    fill(mem.char_rom, sizeof(mem.char_rom), 0x33);
    Bus bus = c64memory_as_bus(&mem);

    struct {
        bool loram, hiram, charen;
        Source a000, d000, e000;
    } rows[] = {
        {1, 1, 1, SRC_BASIC, SRC_IO, SRC_KERNAL},
        {1, 1, 0, SRC_BASIC, SRC_CHARROM, SRC_KERNAL},
        {1, 0, 1, SRC_RAM, SRC_IO, SRC_RAM},
        {1, 0, 0, SRC_RAM, SRC_CHARROM, SRC_RAM},
        {0, 1, 1, SRC_RAM, SRC_IO, SRC_KERNAL},
        {0, 1, 0, SRC_RAM, SRC_CHARROM, SRC_KERNAL},
        {0, 0, 1, SRC_RAM, SRC_IO, SRC_RAM},
        {0, 0, 0, SRC_RAM, SRC_CHARROM, SRC_RAM},
    };

    for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        set_port(&mem, rows[i].loram, rows[i].hiram, rows[i].charen);

        uint8_t a000 = bus_read8(&bus, 0xA000);
        uint8_t expected_a000 = (rows[i].a000 == SRC_BASIC) ? 0x11 : 0x00;
        TEST_ASSERT_EQ_U8(a000, expected_a000);

        /* $D000 itself is VIC-II range when I/O is switched in, which is
         * stubbed to read 0 until Phase 4 -- distinguish "I/O" from
         * "Character ROM" using that, since both would otherwise not be
         * directly comparable to a single sentinel byte. */
        uint8_t d000 = bus_read8(&bus, 0xD000);
        uint8_t expected_d000 = (rows[i].d000 == SRC_CHARROM) ? 0x33 : 0x00;
        TEST_ASSERT_EQ_U8(d000, expected_d000);

        uint8_t e000 = bus_read8(&bus, 0xE000);
        uint8_t expected_e000 = (rows[i].e000 == SRC_KERNAL) ? 0x22 : 0x00;
        TEST_ASSERT_EQ_U8(e000, expected_e000);
    }
}

/* ---------------------------------------------------------------- */
/* RAM is always the real backing store, even under a ROM view        */
/* ---------------------------------------------------------------- */

static void test_writes_under_rom_views_land_in_ram(void) {
    C64Memory mem;
    c64memory_init(&mem);
    Bus bus = c64memory_as_bus(&mem);

    /* BASIC switched in ($A000-$BFFF): write, then switch to the RAM
     * view and confirm the write landed underneath. */
    set_port(&mem, true, true, true); /* BASIC visible */
    bus_write8(&bus, 0xA000, 0x99);
    TEST_ASSERT_EQ_U8(mem.ram[0xA000], 0x99);
    set_port(&mem, true, false, true); /* RAM visible at $A000-$BFFF */
    TEST_ASSERT_EQ_U8(bus_read8(&bus, 0xA000), 0x99);

    /* Character ROM switched in ($D000-$DFFF, CHAREN=0): write-through
     * to RAM still happens, same as any other ROM-shadowed region. */
    set_port(&mem, true, true, false); /* CharROM visible */
    bus_write8(&bus, 0xD100, 0x77);
    TEST_ASSERT_EQ_U8(mem.ram[0xD100], 0x77);
}

static void test_writes_to_io_do_not_reach_ram(void) {
    C64Memory mem;
    c64memory_init(&mem);
    Bus bus = c64memory_as_bus(&mem);

    set_port(&mem, true, true, true); /* I/O visible at $D000-$DFFF */
    mem.ram[0xD100] = 0xAB;           /* pre-existing RAM content, must survive */
    bus_write8(&bus, 0xD100, 0xCD);   /* VIC-II stub range -- ignored, not a RAM write */
    TEST_ASSERT_EQ_U8(mem.ram[0xD100], 0xAB);
}

/* ---------------------------------------------------------------- */
/* $0000/$0001 I/O port semantics                                     */
/* ---------------------------------------------------------------- */

static void test_cpu_port_ddr_and_data_readback(void) {
    C64Memory mem;
    c64memory_init(&mem);
    Bus bus = c64memory_as_bus(&mem);

    bus_write8(&bus, 0x0000, 0x2F); /* real KERNAL init value: bits 0-3,5 output */
    bus_write8(&bus, 0x0001, 0x37); /* LORAM=HIRAM=CHAREN=1, motor off, etc. */

    TEST_ASSERT_EQ_U8(bus_read8(&bus, 0x0000), 0x2F);
    /* Bits 0-3 and 5 are outputs (per the DDR just written) and reflect
     * the written data; bits 4, 6, 7 are inputs and float high. */
    uint8_t expected = (uint8_t)((0x37 & 0x2F) | (uint8_t)~0x2F);
    TEST_ASSERT_EQ_U8(bus_read8(&bus, 0x0001), expected);
}

static void test_zero_page_beyond_port_is_plain_ram(void) {
    C64Memory mem;
    c64memory_init(&mem);
    Bus bus = c64memory_as_bus(&mem);

    bus_write8(&bus, 0x0002, 0x42);
    bus_write8(&bus, 0x01FF, 0x99); /* top of the real stack page */
    TEST_ASSERT_EQ_U8(bus_read8(&bus, 0x0002), 0x42);
    TEST_ASSERT_EQ_U8(bus_read8(&bus, 0x01FF), 0x99);
}

/* ---------------------------------------------------------------- */
/* Color RAM: only the low nibble is real                             */
/* ---------------------------------------------------------------- */

static void test_color_ram_masks_to_low_nibble(void) {
    C64Memory mem;
    c64memory_init(&mem);
    Bus bus = c64memory_as_bus(&mem);
    set_port(&mem, true, true, true); /* I/O visible */

    bus_write8(&bus, 0xD800, 0xDD);
    TEST_ASSERT_EQ_U8(bus_read8(&bus, 0xD800), 0x0D);
    bus_write8(&bus, 0xDBFF, 0xFA);
    TEST_ASSERT_EQ_U8(bus_read8(&bus, 0xDBFF), 0x0A);
}

/* ---------------------------------------------------------------- */
/* $C000-$CFFF is always plain RAM, unaffected by any port bit        */
/* ---------------------------------------------------------------- */

static void test_c000_range_always_ram(void) {
    C64Memory mem;
    c64memory_init(&mem);
    Bus bus = c64memory_as_bus(&mem);

    set_port(&mem, false, false, false);
    bus_write8(&bus, 0xC500, 0x5A);
    set_port(&mem, true, true, true);
    TEST_ASSERT_EQ_U8(bus_read8(&bus, 0xC500), 0x5A);
}

/* ---------------------------------------------------------------- */
/* ROM loaders: missing/wrong-size files are reported, not fatal      */
/* ---------------------------------------------------------------- */

static void test_load_rom_reports_missing_file(void) {
    C64Memory mem;
    c64memory_init(&mem);
    TEST_ASSERT(!c64memory_load_kernal(&mem, "/nonexistent/path/kernal.rom"));
    TEST_ASSERT(!mem.kernal_loaded);
}

int main(void) {
    test_default_port_is_all_high();
    test_reset_vector_reads_through_kernal_by_default();

    test_bank_switching_truth_table();

    test_writes_under_rom_views_land_in_ram();
    test_writes_to_io_do_not_reach_ram();

    test_cpu_port_ddr_and_data_readback();
    test_zero_page_beyond_port_is_plain_ram();

    test_color_ram_masks_to_low_nibble();
    test_c000_range_always_ram();

    test_load_rom_reports_missing_file();

    printf("%d/%d assertions passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
