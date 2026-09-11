/* Hand-written tests for Phase 6: the real cycle-interleaved machine.
 * See docs/machine.md. All synthetic -- no ROMs needed. These are the
 * first tests in the project that actually observe an interrupt
 * (CIA timer, CIA-driven NMI, VIC-II raster) genuinely diverting a
 * running CPU program, rather than poking Cpu6502's irq_line/nmi_line
 * fields directly the way tests/unit/test_cpu.c does. */

#include "testutil.h"

#include "../../src/c64/machine.h"

#define CIA_REG_PRA 0x00
#define CIA_REG_PRB 0x01
#define CIA_REG_DDRA 0x02
#define CIA_REG_DDRB 0x03
#define CIA_REG_TA_LO 0x04
#define CIA_REG_TA_HI 0x05
#define CIA_REG_ICR 0x0D
#define CIA_REG_CRA 0x0E

/* Sets up a Machine with $A000-$BFFF and $E000-$FFFF switched to plain
 * RAM (LORAM=HIRAM=0) while keeping $D000-$DFFF as I/O (CHAREN=1), so
 * a test can freely poke a program and real 6502 interrupt vectors
 * into those ranges without needing any ROM staged -- see
 * docs/memory-map.md's truth table. */
static void setup_ram_everywhere(Machine *m) {
    machine_init(m);
    m->mem.cpu_port_ddr = 0x07;
    m->mem.cpu_port_data = 0x04; /* LORAM=0, HIRAM=0, CHAREN=1 */
}

/* IRQ handler: INC $C200 ; RTI -- lets a test just check a counter
 * instead of trying to catch the CPU mid-handler. Does NOT acknowledge
 * the interrupt source -- see the CIA1 test below for what that does
 * on real hardware if a handler forgets to. */
static void install_irq_counter_handler(Machine *m, uint16_t vector_addr) {
    m->mem.ram[vector_addr] = 0x00;
    m->mem.ram[vector_addr + 1] = 0xC1;
    m->mem.ram[0xC100] = 0xEE;
    m->mem.ram[0xC101] = 0x00;
    m->mem.ram[0xC102] = 0xC2;
    m->mem.ram[0xC103] = 0x40; /* RTI */
}

/* A more realistic handler that also acknowledges the interrupt source
 * first (reading a CIA's ICR clears it -- see docs/cia.md), giving a
 * clean, real interrupt period instead of the storm-of-re-triggers a
 * handler that never acknowledges anything produces (also real,
 * documented behavior -- see test_cia1_timer_irq_interrupts_running_cpu). */
static void install_irq_counter_handler_acking_cia(Machine *m, uint16_t vector_addr, uint16_t icr_addr) {
    m->mem.ram[vector_addr] = 0x00;
    m->mem.ram[vector_addr + 1] = 0xC1;
    m->mem.ram[0xC100] = 0xAD; /* LDA icr_addr */
    m->mem.ram[0xC101] = (uint8_t)(icr_addr & 0xFFu);
    m->mem.ram[0xC102] = (uint8_t)(icr_addr >> 8);
    m->mem.ram[0xC103] = 0xEE; /* INC $C200 */
    m->mem.ram[0xC104] = 0x00;
    m->mem.ram[0xC105] = 0xC2;
    m->mem.ram[0xC106] = 0x40; /* RTI */
}

static void install_main_loop_with_cli(Machine *m) {
    m->mem.ram[0xFFFC] = 0x00;
    m->mem.ram[0xFFFD] = 0xC0;
    m->mem.ram[0xC000] = 0x58; /* CLI */
    m->mem.ram[0xC001] = 0x4C;
    m->mem.ram[0xC002] = 0x01;
    m->mem.ram[0xC003] = 0xC0; /* JMP $C001 */
}

/* ---------------------------------------------------------------- */
/* IRQ: CIA1 timer really interrupts a running CPU program            */
/* ---------------------------------------------------------------- */

static void test_cia1_timer_irq_interrupts_running_cpu(void) {
    Machine m;
    setup_ram_everywhere(&m);
    install_main_loop_with_cli(&m);
    install_irq_counter_handler(&m, 0xFFFE); /* IRQ/BRK vector */

    machine_reset(&m);
    TEST_ASSERT_EQ_U16(m.cpu.pc, 0xC000);

    cia_write(&m.cia1, CIA_REG_TA_LO, 100);
    cia_write(&m.cia1, CIA_REG_TA_HI, 0); /* stopped -> force-loads counter; latch=100 */
    cia_write(&m.cia1, CIA_REG_ICR, (uint8_t)(0x80u | CIA_ICR_TA));
    cia_write(&m.cia1, CIA_REG_CRA, 0x01); /* START, continuous */

    for (int i = 0; i < 1000; i++) {
        machine_cycle(&m);
    }

    /* This handler never acknowledges the interrupt (never reads
     * $DC0D), so on real hardware the IRQ line stays asserted and the
     * CPU re-enters service almost immediately every time it becomes
     * eligible again -- a real, documented "interrupt storm" behavior,
     * not a bug. Comfortably more than the ~9 fires a well-behaved
     * ~107-cycle period would produce over 1000 cycles. */
    TEST_ASSERT(m.mem.ram[0xC200] > 9);
}

static void test_cia1_timer_irq_with_real_handler_fires_at_expected_period(void) {
    Machine m;
    setup_ram_everywhere(&m);
    install_main_loop_with_cli(&m);
    install_irq_counter_handler_acking_cia(&m, 0xFFFE, 0xDC0D); /* reading ICR clears it */

    machine_reset(&m);
    cia_write(&m.cia1, CIA_REG_TA_LO, 100);
    cia_write(&m.cia1, CIA_REG_TA_HI, 0);
    cia_write(&m.cia1, CIA_REG_ICR, (uint8_t)(0x80u | CIA_ICR_TA));
    cia_write(&m.cia1, CIA_REG_CRA, 0x01);

    /* A well-behaved handler should fire roughly once every 100 cycles
     * (the timer period) plus a little service overhead -- expect
     * about 1000/~110 ~= 9 fires over 1000 cycles, not the 90+ an
     * unacknowledged storm would produce. */
    for (int i = 0; i < 1000; i++) {
        machine_cycle(&m);
    }
    TEST_ASSERT(m.mem.ram[0xC200] >= 6 && m.mem.ram[0xC200] <= 12);
}

/* ---------------------------------------------------------------- */
/* NMI: CIA2 edge-triggers, doesn't need the I flag clear              */
/* ---------------------------------------------------------------- */

static void test_cia2_irq_edge_triggers_nmi(void) {
    Machine m;
    setup_ram_everywhere(&m);

    /* No CLI needed -- NMI isn't masked by the I flag. */
    m.mem.ram[0xFFFC] = 0x00;
    m.mem.ram[0xFFFD] = 0xC0;
    m.mem.ram[0xC000] = 0x4C;
    m.mem.ram[0xC001] = 0x00;
    m.mem.ram[0xC002] = 0xC0; /* JMP $C000 */
    install_irq_counter_handler(&m, 0xFFFA); /* NMI vector */

    machine_reset(&m);

    cia_write(&m.cia2, CIA_REG_TA_LO, 100);
    cia_write(&m.cia2, CIA_REG_TA_HI, 0);
    cia_write(&m.cia2, CIA_REG_ICR, (uint8_t)(0x80u | CIA_ICR_TA));
    cia_write(&m.cia2, CIA_REG_CRA, 0x01); /* one-shot would also do -- continuous just for consistency */

    for (int i = 0; i < 200; i++) {
        machine_cycle(&m);
    }
    TEST_ASSERT_EQ_U8(m.mem.ram[0xC200], 1); /* fired exactly once -- edge-triggered, not level */

    /* The CIA2 IRQ line is still asserted (nobody read $DD0D to clear
     * it) -- confirm NMI does NOT fire again without a fresh edge. */
    for (int i = 0; i < 1000; i++) {
        machine_cycle(&m);
    }
    TEST_ASSERT_EQ_U8(m.mem.ram[0xC200], 1);
}

/* ---------------------------------------------------------------- */
/* IRQ: the VIC-II's own raster interrupt ORs into the same line       */
/* ---------------------------------------------------------------- */

static void test_vic_raster_irq_interrupts_running_cpu(void) {
    Machine m;
    setup_ram_everywhere(&m);
    install_main_loop_with_cli(&m);
    install_irq_counter_handler(&m, 0xFFFE);
    machine_reset(&m);

    vic_ii_reg_write(&m.vic, 0x1A, VIC_IRQ_RST); /* enable raster IRQ */
    vic_ii_reg_write(&m.vic, 0x12, 60);          /* compare line 60 */

    /* One full frame is comfortably enough for raster to reach line 60
     * and the CPU to take the interrupt. */
    machine_run_cycles(&m, VIC_CYCLES_PER_LINE * VIC_LINES_PER_FRAME);

    TEST_ASSERT(m.mem.ram[0xC200] >= 1);
}

/* ---------------------------------------------------------------- */
/* VIC-II bank follows CIA2 Port A (inverted)                         */
/* ---------------------------------------------------------------- */

static void test_vic_bank_follows_cia2_port_a(void) {
    Machine m;
    machine_init(&m);
    cia_write(&m.cia2, CIA_REG_DDRA, 0x03); /* bits 0-1 outputs */

    cia_write(&m.cia2, CIA_REG_PRA, 0x03); /* 11 -> bank 0 */
    machine_cycle(&m);
    TEST_ASSERT_EQ_U16(m.vic.bank_base, 0x0000);

    cia_write(&m.cia2, CIA_REG_PRA, 0x00); /* 00 -> bank 3 */
    machine_cycle(&m);
    TEST_ASSERT_EQ_U16(m.vic.bank_base, 0xC000);

    cia_write(&m.cia2, CIA_REG_PRA, 0x02); /* 10 -> bank 1 */
    machine_cycle(&m);
    TEST_ASSERT_EQ_U16(m.vic.bank_base, 0x4000);
}

/* ---------------------------------------------------------------- */
/* Keyboard matrix + joystick wiring through CIA1                     */
/* ---------------------------------------------------------------- */

static void test_keyboard_matrix_wiring_through_cia1(void) {
    Machine m;
    machine_init(&m);
    keyboard_matrix_set_key(&m.keyboard, C64KEY_A, true); /* pa=1, pb=2 */

    cia_write(&m.cia1, CIA_REG_DDRA, 0xFF); /* Port A: all outputs (column select) */
    cia_write(&m.cia1, CIA_REG_DDRB, 0x00); /* Port B: all inputs (row sense) */
    cia_write(&m.cia1, CIA_REG_PRA, (uint8_t)~(1u << 1)); /* select column 1 only */

    machine_cycle(&m); /* lets machine_cycle() recompute the Port B pulldown */

    uint8_t port_b = cia_read(&m.cia1, CIA_REG_PRB);
    TEST_ASSERT_EQ_U8(port_b & (1u << 2), 0); /* row 2 pulled low -- key is down */
    TEST_ASSERT_EQ_U8(port_b & (uint8_t)~(1u << 2), (uint8_t)~(1u << 2)); /* every other row still high */
}

static void test_joystick2_wiring_through_cia1_port_a(void) {
    Machine m;
    machine_init(&m);
    cia_write(&m.cia1, CIA_REG_DDRA, 0x00); /* Port A as input -- floats high, joystick pulls down */
    m.joystick2.up = true;
    m.joystick2.fire = true;

    machine_cycle(&m);

    uint8_t port_a = cia_read(&m.cia1, CIA_REG_PRA);
    TEST_ASSERT_EQ_U8(port_a & 0x01u, 0);        /* up */
    TEST_ASSERT_EQ_U8(port_a & 0x10u, 0);        /* fire */
    TEST_ASSERT_EQ_U8(port_a & 0x02u, 0x02u);    /* down: not pressed, stays high */
}

/* ---------------------------------------------------------------- */
/* Real-time pacing: the pure cycle-budget arithmetic                 */
/* ---------------------------------------------------------------- */

static void test_pacing_cycle_budget_matches_pal_clock(void) {
    uint64_t one_second = machine_target_cycles_for_elapsed(1.0);
    /* Must match the exact computed PAL clock, not a rounded ~985248 --
     * see docs/references-and-gotchas.md. */
    TEST_ASSERT(one_second == (uint64_t)C64_PAL_PHI2_HZ);
    TEST_ASSERT_EQ_INT((int)machine_target_cycles_for_elapsed(0.0), 0);
    TEST_ASSERT_EQ_INT((int)machine_target_cycles_for_elapsed(-1.0), 0);
}

static void test_run_realtime_smoke(void) {
    /* Not a precision timing test (host scheduling jitter would make
     * that flaky) -- just confirms machine_run_realtime() actually
     * advances the machine by roughly the right order of magnitude
     * without hanging or crashing. */
    Machine m;
    machine_init(&m);
    machine_run_realtime(&m, 0.05);
    uint64_t expected = machine_target_cycles_for_elapsed(0.05);
    TEST_ASSERT(m.total_cycles > 0);
    TEST_ASSERT(m.total_cycles > expected / 2);
    TEST_ASSERT(m.total_cycles < expected * 4 + 100000);
}

int main(void) {
    test_cia1_timer_irq_interrupts_running_cpu();
    test_cia1_timer_irq_with_real_handler_fires_at_expected_period();
    test_cia2_irq_edge_triggers_nmi();
    test_vic_raster_irq_interrupts_running_cpu();

    test_vic_bank_follows_cia2_port_a();

    test_keyboard_matrix_wiring_through_cia1();
    test_joystick2_wiring_through_cia1_port_a();

    test_pacing_cycle_budget_matches_pal_clock();
    test_run_realtime_smoke();

    printf("%d/%d assertions passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
