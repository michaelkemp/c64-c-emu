/* Hand-written 6502 core unit tests -- Phase 1's roadmap item to run
 * *before* the Dormann suite: these pin down specific instructions/
 * addressing modes/flag and cycle-count edge cases so a failure here
 * localizes to one instruction, instead of "the suite trapped at some
 * address" (see docs/6502-reference.md and docs/testing-strategy.md).
 * Not exhaustive -- the Dormann suite (Phase 1's other checklist item)
 * is the exhaustive tier. */

#include "testutil.h"

static void setup(Cpu6502 *cpu, FlatBus *fb, Bus *bus) {
    flat_bus_init(fb);
    *bus = flat_bus_as_bus(fb);
    cpu6502_init(cpu, bus);
    /* Point the reset vector somewhere harmless and reset so PC/S/P are
     * in their real post-reset state before each test loads its own
     * program at a fixed address. */
    fb->mem[0xFFFC] = 0x00;
    fb->mem[0xFFFD] = 0x80;
    cpu6502_reset(cpu);
}

/* ---------------------------------------------------------------- */
/* Load/store + flags                                                */
/* ---------------------------------------------------------------- */

static void test_lda_immediate_sets_nz(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0xA9; fb.mem[0x8001] = 0x00; /* LDA #$00 */
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0x00);
    TEST_ASSERT(cpu.p & CPU6502_FLAG_Z);
    TEST_ASSERT(!(cpu.p & CPU6502_FLAG_N));

    fb.mem[0x8002] = 0xA9; fb.mem[0x8003] = 0x80; /* LDA #$80 */
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0x80);
    TEST_ASSERT(!(cpu.p & CPU6502_FLAG_Z));
    TEST_ASSERT(cpu.p & CPU6502_FLAG_N);
}

static void test_sta_zp_writes_through_and_lda_reads_back(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0xA9; fb.mem[0x8001] = 0x42; /* LDA #$42 */
    fb.mem[0x8002] = 0x85; fb.mem[0x8003] = 0x10; /* STA $10 */
    fb.mem[0x8004] = 0xA9; fb.mem[0x8005] = 0x00; /* LDA #$00 */
    fb.mem[0x8006] = 0xA5; fb.mem[0x8007] = 0x10; /* LDA $10 */
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0x42);
    TEST_ASSERT_EQ_U8(fb.mem[0x10], 0x42);
}

/* ---------------------------------------------------------------- */
/* Indexed addressing + page-crossing cycle counts                   */
/* ---------------------------------------------------------------- */

static void test_lda_absx_page_cross_takes_extra_cycle(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0xA2; fb.mem[0x8001] = 0x01; /* LDX #$01 */
    fb.mem[0x8002] = 0xBD; fb.mem[0x8003] = 0xFF; fb.mem[0x8004] = 0x20; /* LDA $20FF,X -> $2100 */
    fb.mem[0x2100] = 0x55;
    run_one_instruction(&cpu); /* LDX */

    uint64_t before = cpu.total_cycles;
    run_one_instruction(&cpu); /* LDA absx, crosses page */
    TEST_ASSERT_EQ_U8(cpu.a, 0x55);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 5);
}

static void test_lda_absx_no_page_cross_is_four_cycles(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0xA2; fb.mem[0x8001] = 0x01; /* LDX #$01 */
    fb.mem[0x8002] = 0xBD; fb.mem[0x8003] = 0x00; fb.mem[0x8004] = 0x20; /* LDA $2000,X -> $2001 */
    fb.mem[0x2001] = 0x77;
    run_one_instruction(&cpu);

    uint64_t before = cpu.total_cycles;
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0x77);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 4);
}

static void test_sta_absx_always_five_cycles_regardless_of_crossing(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0xA2; fb.mem[0x8001] = 0x01; /* LDX #$01 */
    fb.mem[0x8002] = 0xA9; fb.mem[0x8003] = 0x99; /* LDA #$99 */
    fb.mem[0x8004] = 0x9D; fb.mem[0x8005] = 0x00; fb.mem[0x8006] = 0x20; /* STA $2000,X, no cross */
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);

    uint64_t before = cpu.total_cycles;
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(fb.mem[0x2001], 0x99);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 5);
}

static void test_indexed_indirect_and_indirect_indexed(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);

    /* (zp,X): pointer table at $20+X */
    fb.mem[0x8000] = 0xA2; fb.mem[0x8001] = 0x04; /* LDX #$04 */
    fb.mem[0x24] = 0x00; fb.mem[0x25] = 0x30;     /* -> $3000 */
    fb.mem[0x3000] = 0xAB;
    fb.mem[0x8002] = 0xA1; fb.mem[0x8003] = 0x20; /* LDA ($20,X) */
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0xAB);

    /* (zp),Y with a page crossing */
    fb.mem[0x8004] = 0xA0; fb.mem[0x8005] = 0x01; /* LDY #$01 */
    fb.mem[0x30] = 0xFF; fb.mem[0x31] = 0x30;     /* base $30FF, +Y=1 crosses into $3100 */
    fb.mem[0x3100] = 0xCD;
    fb.mem[0x8006] = 0xB1; fb.mem[0x8007] = 0x30; /* LDA ($30),Y */
    run_one_instruction(&cpu);

    uint64_t before = cpu.total_cycles;
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0xCD);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 6);
}

/* ---------------------------------------------------------------- */
/* Read-modify-write                                                  */
/* ---------------------------------------------------------------- */

static void test_inc_dec_zp_and_cycle_count(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x10] = 0x7F;
    fb.mem[0x8000] = 0xE6; fb.mem[0x8001] = 0x10; /* INC $10 */

    uint64_t before = cpu.total_cycles;
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(fb.mem[0x10], 0x80);
    TEST_ASSERT(cpu.p & CPU6502_FLAG_N);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 5);

    fb.mem[0x8002] = 0xC6; fb.mem[0x8003] = 0x10; /* DEC $10 */
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(fb.mem[0x10], 0x7F);
}

static void test_asl_accumulator_sets_carry(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0xA9; fb.mem[0x8001] = 0x81; /* LDA #$81 */
    fb.mem[0x8002] = 0x0A;                        /* ASL A */
    run_one_instruction(&cpu);

    uint64_t before = cpu.total_cycles;
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0x02);
    TEST_ASSERT(cpu.p & CPU6502_FLAG_C);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 2);
}

/* ---------------------------------------------------------------- */
/* Arithmetic: binary and decimal ADC/SBC                             */
/* ---------------------------------------------------------------- */

static void test_adc_binary_overflow_flag(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    /* 0x50 + 0x50 = 0xA0: signed overflow (positive+positive=negative) */
    fb.mem[0x8000] = 0xA9; fb.mem[0x8001] = 0x50; /* LDA #$50 */
    fb.mem[0x8002] = 0x18;                        /* CLC */
    fb.mem[0x8003] = 0x69; fb.mem[0x8004] = 0x50; /* ADC #$50 */
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0xA0);
    TEST_ASSERT(cpu.p & CPU6502_FLAG_V);
    TEST_ASSERT(cpu.p & CPU6502_FLAG_N);
    TEST_ASSERT(!(cpu.p & CPU6502_FLAG_C));
}

static void test_adc_decimal_mode(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    /* 58 + 46 = 104 in BCD -> result $04, carry set */
    fb.mem[0x8000] = 0xF8;                        /* SED */
    fb.mem[0x8001] = 0x18;                        /* CLC */
    fb.mem[0x8002] = 0xA9; fb.mem[0x8003] = 0x58; /* LDA #$58 */
    fb.mem[0x8004] = 0x69; fb.mem[0x8005] = 0x46; /* ADC #$46 */
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0x04);
    TEST_ASSERT(cpu.p & CPU6502_FLAG_C);
}

static void test_sbc_decimal_mode(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    /* 42 - 15 = 27 in BCD, with carry set on entry (no borrow) */
    fb.mem[0x8000] = 0xF8;                        /* SED */
    fb.mem[0x8001] = 0x38;                        /* SEC */
    fb.mem[0x8002] = 0xA9; fb.mem[0x8003] = 0x42; /* LDA #$42 */
    fb.mem[0x8004] = 0xE9; fb.mem[0x8005] = 0x15; /* SBC #$15 */
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0x27);
    TEST_ASSERT(cpu.p & CPU6502_FLAG_C);
}

static void test_sbc_binary_borrow(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    /* 0x05 - 0x06 with carry set (no incoming borrow) -> 0xFF, carry clear (borrow occurred) */
    fb.mem[0x8000] = 0x38;                        /* SEC */
    fb.mem[0x8001] = 0xA9; fb.mem[0x8002] = 0x05; /* LDA #$05 */
    fb.mem[0x8003] = 0xE9; fb.mem[0x8004] = 0x06; /* SBC #$06 */
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0xFF);
    TEST_ASSERT(!(cpu.p & CPU6502_FLAG_C));
    TEST_ASSERT(cpu.p & CPU6502_FLAG_N);
}

/* ---------------------------------------------------------------- */
/* Branches                                                           */
/* ---------------------------------------------------------------- */

static void test_branch_not_taken_is_two_cycles(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0xF0; fb.mem[0x8001] = 0x10; /* BEQ +$10, Z currently clear after reset */
    uint64_t before = cpu.total_cycles;
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 2);
    TEST_ASSERT_EQ_U16(cpu.pc, 0x8002);
}

static void test_branch_taken_same_page_is_three_cycles(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0xA9; fb.mem[0x8001] = 0x00; /* LDA #$00 -> sets Z */
    fb.mem[0x8002] = 0xF0; fb.mem[0x8003] = 0x10; /* BEQ +$10 -> $8014, same page */
    run_one_instruction(&cpu);

    uint64_t before = cpu.total_cycles;
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 3);
    TEST_ASSERT_EQ_U16(cpu.pc, 0x8014);
}

static void test_branch_taken_crossing_page_is_four_cycles(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x80F0] = 0xA9; fb.mem[0x80F1] = 0x00; /* LDA #$00 */
    fb.mem[0x80F2] = 0xF0; fb.mem[0x80F3] = 0x20; /* BEQ +$20 -> $8114, crosses page */
    cpu.pc = 0x80F0;
    run_one_instruction(&cpu);

    uint64_t before = cpu.total_cycles;
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 4);
    TEST_ASSERT_EQ_U16(cpu.pc, 0x8114);
}

static void test_branch_backward_negative_offset(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0xA9; fb.mem[0x8001] = 0x00; /* LDA #$00 */
    fb.mem[0x8002] = 0xF0; fb.mem[0x8003] = 0xFC; /* BEQ -4 -> back to $8000 (PC is $8004 when the offset is applied) */
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U16(cpu.pc, 0x8000);
}

/* ---------------------------------------------------------------- */
/* JMP, including the indirect page-boundary bug                     */
/* ---------------------------------------------------------------- */

static void test_jmp_absolute(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0x4C; fb.mem[0x8001] = 0x00; fb.mem[0x8002] = 0x90; /* JMP $9000 */
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U16(cpu.pc, 0x9000);
}

static void test_jmp_indirect_page_boundary_bug(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    /* Pointer at $30FF: real hardware fetches the high byte from $3000,
     * not $3100 -- the documented bug, not something to "fix". */
    fb.mem[0x30FF] = 0x34;
    fb.mem[0x3000] = 0x12;
    fb.mem[0x3100] = 0xFF; /* if this were used instead, we'd get $FF34 -- wrong */
    fb.mem[0x8000] = 0x6C; fb.mem[0x8001] = 0xFF; fb.mem[0x8002] = 0x30; /* JMP ($30FF) */
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U16(cpu.pc, 0x1234);
}

/* ---------------------------------------------------------------- */
/* Stack: JSR/RTS, PHA/PLA, PHP/PLP                                   */
/* ---------------------------------------------------------------- */

static void test_jsr_rts_roundtrip(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0x20; fb.mem[0x8001] = 0x00; fb.mem[0x8002] = 0x90; /* JSR $9000 */
    fb.mem[0x9000] = 0x60;                                              /* RTS */
    uint8_t s_before = cpu.s;

    uint64_t before = cpu.total_cycles;
    run_one_instruction(&cpu); /* JSR */
    TEST_ASSERT_EQ_U16(cpu.pc, 0x9000);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 6);
    TEST_ASSERT_EQ_U8(cpu.s, (uint8_t)(s_before - 2));

    /* Stack should hold return address - 1 ($8002), per real JSR/RTS convention. */
    TEST_ASSERT_EQ_U8(fb.mem[0x0100 | (uint8_t)(s_before)], 0x80);
    TEST_ASSERT_EQ_U8(fb.mem[0x0100 | (uint8_t)(s_before - 1)], 0x02);

    before = cpu.total_cycles;
    run_one_instruction(&cpu); /* RTS */
    TEST_ASSERT_EQ_U16(cpu.pc, 0x8003);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 6);
    TEST_ASSERT_EQ_U8(cpu.s, s_before);
}

static void test_pha_pla_roundtrip(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0xA9; fb.mem[0x8001] = 0x99; /* LDA #$99 */
    fb.mem[0x8002] = 0x48;                        /* PHA */
    fb.mem[0x8003] = 0xA9; fb.mem[0x8004] = 0x00; /* LDA #$00 */
    fb.mem[0x8005] = 0x68;                        /* PLA */
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U8(cpu.a, 0x99);
    TEST_ASSERT(!(cpu.p & CPU6502_FLAG_Z));
}

static void test_php_sets_break_and_unused_bits(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    cpu.p = 0x00; /* deliberately clear everything to isolate PHP's own forced bits */
    fb.mem[0x8000] = 0x08; /* PHP */
    uint8_t s_before = cpu.s;
    run_one_instruction(&cpu);
    uint8_t pushed = fb.mem[0x0100 | s_before];
    TEST_ASSERT(pushed & CPU6502_FLAG_B);
    TEST_ASSERT(pushed & CPU6502_FLAG_UNUSED);
}

/* ---------------------------------------------------------------- */
/* BRK / RTI and the documented CLI one-instruction IRQ delay          */
/* ---------------------------------------------------------------- */

static void test_brk_and_rti_roundtrip(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0xFFFE] = 0x00; fb.mem[0xFFFF] = 0x91; /* IRQ/BRK vector -> $9100 */
    fb.mem[0x8000] = 0x00; /* BRK (2-byte instruction: opcode + padding) */
    fb.mem[0x8001] = 0xEA; /* padding byte, discarded */
    fb.mem[0x9100] = 0x40; /* RTI */

    uint64_t before = cpu.total_cycles;
    run_one_instruction(&cpu); /* BRK */
    TEST_ASSERT_EQ_U16(cpu.pc, 0x9100);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 7);
    TEST_ASSERT(cpu.p & CPU6502_FLAG_I);

    before = cpu.total_cycles;
    run_one_instruction(&cpu); /* RTI */
    TEST_ASSERT_EQ_U16(cpu.pc, 0x8002);
    TEST_ASSERT_EQ_INT(cpu.total_cycles - before, 6);
}

static void test_irq_is_masked_by_i_flag(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0xFFFE] = 0x00; fb.mem[0xFFFF] = 0x91;
    fb.mem[0x8000] = 0xEA; /* NOP */
    fb.mem[0x8001] = 0xEA; /* NOP */
    cpu6502_set_irq_line(&cpu, true);
    /* I flag is set post-reset, so this IRQ must stay pending, not taken. */
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U16(cpu.pc, 0x8001);
}

static void test_cli_delays_irq_by_one_instruction(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0xFFFE] = 0x00; fb.mem[0xFFFF] = 0x91;
    fb.mem[0x8000] = 0x58; /* CLI */
    fb.mem[0x8001] = 0xEA; /* NOP -- real hardware always executes this before the IRQ is taken */
    fb.mem[0x8002] = 0xEA; /* NOP */
    fb.mem[0x9100] = 0xEA;

    cpu6502_set_irq_line(&cpu, true);
    run_one_instruction(&cpu); /* CLI */
    TEST_ASSERT_EQ_U16(cpu.pc, 0x8001);

    run_one_instruction(&cpu); /* the NOP right after CLI must still run, not the IRQ */
    TEST_ASSERT_EQ_U16(cpu.pc, 0x8002);

    run_one_instruction(&cpu); /* now the IRQ should be taken instead of the second NOP */
    TEST_ASSERT_EQ_U16(cpu.pc, 0x9100);
}

static void test_nmi_is_edge_triggered_not_level(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0xFFFA] = 0x00; fb.mem[0xFFFB] = 0x92; /* NMI vector -> $9200 */
    fb.mem[0x8000] = 0xEA;
    fb.mem[0x8001] = 0xEA;
    fb.mem[0x9200] = 0x40; /* RTI back out */

    cpu6502_set_nmi_line(&cpu, true);
    /* NMI is polled *before* the NOP at $8000 is fetched, so it's the
     * NOP that gets delayed, not skipped -- the pushed/returned PC is
     * still $8000, the address of the not-yet-executed instruction. */
    run_one_instruction(&cpu); /* NMI taken instead of fetching $8000 */
    TEST_ASSERT_EQ_U16(cpu.pc, 0x9200);
    run_one_instruction(&cpu); /* RTI back to $8000 */
    TEST_ASSERT_EQ_U16(cpu.pc, 0x8000);

    /* Line is still held high, but no *new* edge has occurred, so a
     * second NMI must not fire again -- the NOP at $8000 now finally
     * executes for real. */
    run_one_instruction(&cpu);
    TEST_ASSERT_EQ_U16(cpu.pc, 0x8001);
}

/* ---------------------------------------------------------------- */
/* Illegal opcode diagnostics (Phase 10 defers real behavior)         */
/* ---------------------------------------------------------------- */

static void test_illegal_opcode_is_flagged_not_silent(void) {
    Cpu6502 cpu; FlatBus fb; Bus bus;
    setup(&cpu, &fb, &bus);
    fb.mem[0x8000] = 0x02; /* an undocumented opcode (KIL/JAM-family), unimplemented until Phase 10 */
    run_one_instruction(&cpu);
    TEST_ASSERT(cpu.illegal_opcode_hit);
    TEST_ASSERT_EQ_U8(cpu.last_illegal_opcode, 0x02);
}

int main(void) {
    test_lda_immediate_sets_nz();
    test_sta_zp_writes_through_and_lda_reads_back();

    test_lda_absx_page_cross_takes_extra_cycle();
    test_lda_absx_no_page_cross_is_four_cycles();
    test_sta_absx_always_five_cycles_regardless_of_crossing();
    test_indexed_indirect_and_indirect_indexed();

    test_inc_dec_zp_and_cycle_count();
    test_asl_accumulator_sets_carry();

    test_adc_binary_overflow_flag();
    test_adc_decimal_mode();
    test_sbc_decimal_mode();
    test_sbc_binary_borrow();

    test_branch_not_taken_is_two_cycles();
    test_branch_taken_same_page_is_three_cycles();
    test_branch_taken_crossing_page_is_four_cycles();
    test_branch_backward_negative_offset();

    test_jmp_absolute();
    test_jmp_indirect_page_boundary_bug();

    test_jsr_rts_roundtrip();
    test_pha_pla_roundtrip();
    test_php_sets_break_and_unused_bits();

    test_brk_and_rti_roundtrip();
    test_irq_is_masked_by_i_flag();
    test_cli_delays_irq_by_one_instruction();
    test_nmi_is_edge_triggered_not_level();

    test_illegal_opcode_is_flagged_not_silent();

    printf("%d/%d assertions passed\n", g_tests_run - g_tests_failed, g_tests_run);
    return g_tests_failed == 0 ? 0 : 1;
}
